/*
 * Copyright (C) 2014 Intel Corporation
 * Copyright (C) 2022 Loongson Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/poll.h>
#include <xf86drm.h>

#include "driver.h"
#include "vblank.h"
#include "gsgpu_bo_helper.h"
#include "loongson_scanout.h"
/*
 * Flush the DRM event queue when full; makes space for new events.
 *
 * Returns a negative value on error, 0 if there was nothing to process,
 * or 1 if we handled any events.
 */
int ms_flush_drm_events(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = loongsonPTR(pScrn);

    struct pollfd p = {
        .fd = ms->fd,
        .events = POLLIN
    };
    int r;

    do {
            r = poll(&p, 1, 0);
    } while (r == -1 && (errno == EINTR || errno == EAGAIN));

    xf86Msg(X_WARNING, "flip queue: carrier alloc failed.\n");

    /* If there was an error, r will be < 0.  Return that.  If there was
     * nothing to process, r == 0.  Return that.
     */
    if (r <= 0)
        return r;

    /* Try to handle the event.  If there was an error, return it. */
    r = drmHandleEvent(ms->fd, &ms->event_context);
    if (r < 0)
        return r;

    /* Otherwise return 1 to indicate that we handled an event. */
    return 1;
}


/*
 * Event data for an in progress flip.
 * This contains a pointer to the vblank event,
 * and information about the flip in progress.
 * a reference to this is stored in the per-crtc
 * flips.
 */
struct ms_flipdata {
    ScreenPtr screen;
    void *event;
    pageflip_handler_cb event_handler;
    pageflip_abort_cb abort_handler;
    /* number of CRTC events referencing this */
    int flip_count;
    uint64_t fe_msc;
    uint64_t fe_usec;
    uint32_t old_fb_id;
};

/*
 * Per crtc pageflipping infomation,
 * These are submitted to the queuing code
 * one of them per crtc per flip.
 */
struct ms_crtc_pageflip {
    Bool on_reference_crtc;
    /* reference to the ms_flipdata */
    struct ms_flipdata *flipdata;
};

/**
 * Free an ms_crtc_pageflip.
 *
 * Drops the reference count on the flipdata.
 */
static void ls_pageflip_free(struct ms_crtc_pageflip *flip)
{
    struct ms_flipdata *flipdata = flip->flipdata;

    free(flip);
    if (--flipdata->flip_count > 0)
        return;
    free(flipdata);
}

/**
 * Callback for the DRM event queue when a single flip has completed
 *
 * Once the flip has been completed on all pipes, notify the
 * extension code telling it when that happened
 */
static void ls_pageflip_handler_cb(uint64_t msc, uint64_t ust, void *data)
{
    struct ms_crtc_pageflip *flip = data;
    struct ms_flipdata *flipdata = flip->flipdata;
    ScreenPtr pScreen = flipdata->screen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);

    if (flip->on_reference_crtc)
    {
        flipdata->fe_msc = msc;
        flipdata->fe_usec = ust;
    }

    if (flipdata->flip_count == 1)
    {
        flipdata->event_handler(lsp,
                                flipdata->fe_msc,
                                flipdata->fe_usec,
                                flipdata->event);

        drmModeRmFB(lsp->fd, flipdata->old_fb_id);
    }

    ls_pageflip_free(flip);
}

/*
 * Callback for the DRM queue abort code.  A flip has been aborted.
 */
static void ls_pageflip_abort_cb(void *data)
{
    struct ms_crtc_pageflip *flip = data;
    struct ms_flipdata *flipdata = flip->flipdata;
    ScreenPtr pScreen = flipdata->screen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);

    if (flipdata->flip_count == 1)
        flipdata->abort_handler(lsp, flipdata->event);

    ls_pageflip_free(flip);
}



static Bool do_queue_flip_on_crtc(loongsonPtr lsp,
                                  xf86CrtcPtr crtc,
                                  uint32_t flags,
                                  uint32_t seq)
{
    return drmmode_crtc_flip(crtc,
                             lsp->drmmode.fb_id,
                             flags,
                             (void *) (uintptr_t) seq);
}

static Bool queue_flip_on_crtc(ScreenPtr pScreen,
                               xf86CrtcPtr crtc,
                               struct ms_flipdata *flipdata,
                               int ref_crtc_vblank_pipe,
                               uint32_t flags)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    struct ms_crtc_pageflip *flip;
    uint32_t seq;
    int err;

    flip = calloc(1, sizeof(struct ms_crtc_pageflip));
    if (flip == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "flip queue: carrier alloc failed.\n");
        return FALSE;
    }

    /*
     * Only the reference crtc will finally deliver its page flip
     * completion event. All other crtc's events will be discarded.
     */

    #if defined(DEBUG_PAGE_FLIP)

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%d, %d\n",
               drmmode_crtc->vblank_pipe, ref_crtc_vblank_pipe);

    #endif

    flip->on_reference_crtc =
          (drmmode_crtc->vblank_pipe == ref_crtc_vblank_pipe);
    flip->flipdata = flipdata;

    seq = ms_drm_queue_alloc(crtc,
                             flip,
                             ls_pageflip_handler_cb,
                             ls_pageflip_abort_cb);
    if (!seq)
    {
        free(flip);
        return FALSE;
    }

    /* take a reference on flipdata for use in flip */
    flipdata->flip_count++;

    while (do_queue_flip_on_crtc(lsp, crtc, flags, seq))
    {
        err = errno;
        /* We may have failed because the event queue was full.  Flush it
         * and retry.  If there was nothing to flush, then we failed for
         * some other reason and should just return an error.
         */
        if (ms_flush_drm_events(pScreen) <= 0)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "flip queue failed: %s\n", strerror(err));
            /* Aborting will also decrement flip_count and free(flip). */
            ms_drm_abort_seq(pScrn, seq);
            return FALSE;
        }

        /* We flushed some events, so try again. */
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "flip queue retry\n");
    }

    /* The page flip succeded. */
    return TRUE;
}


Bool ms_do_pageflip(ScreenPtr pScreen,
                    PixmapPtr pNewFrontPixmap,
                    void *event,
                    int ref_crtc_vblank_pipe,
                    Bool async,
                    pageflip_handler_cb pHandlerCB,
                    pageflip_abort_cb pAbortCB,
                    const char *log_prefix)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
    struct DrmModeBO front_bo_tmp;
    struct DrmModeBO *new_front_bo = &front_bo_tmp;
    uint32_t flags;
    int i;
    struct ms_flipdata *flipdata;

    if (pDrmMode->glamor_enabled)
    {
#ifdef GLAMOR_HAS_GBM
        struct GlamorAPI * const pGlamorAPI = &lsp->glamor;

        pGlamorAPI->block_handler(pScreen);
        new_front_bo->gbm = pGlamorAPI->gbm_bo_from_pixmap(pScreen,
                                                           pNewFrontPixmap);
        if (new_front_bo->gbm == NULL)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s: Failed to get GBM BO for flip to new front.\n",
                   log_prefix);
            return FALSE;
        }

        new_front_bo->dumb = NULL;
#endif
    }
    else if (pDrmMode->exa_enabled &&
             pDrmMode->exa_acc_type == EXA_ACCEL_TYPE_GSGPU)
    {
#ifdef HAVE_LIBDRM_GSGPU
        /*
         * the backing memory is gtt bo when use x server with window manager
         */
        new_front_bo->dumb = dumb_bo_from_pixmap(pScreen, pNewFrontPixmap);
        if (!new_front_bo->dumb)
        {
            new_front_bo->gbo = gsgpu_get_pixmap_bo(pNewFrontPixmap);
            if (!new_front_bo->gbo)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                           "Failed to get backing bo for pageflip\n");

                return FALSE;
            }
            new_front_bo->pitch = pNewFrontPixmap->devKind;
        }

        new_front_bo->gbm = NULL;
#endif
    }
    else if (pDrmMode->exa_enabled)
    {
        /*
         * what if the backing memory is not a dumb ?
         */
        new_front_bo->dumb = dumb_bo_from_pixmap(pScreen, pNewFrontPixmap);
        if (new_front_bo->dumb == NULL)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "exa: Failed to get dumb bo for flip\n");
            return FALSE;
        }

        new_front_bo->gbm = NULL;
    }
    else
    {
        return FALSE;
    }


    flipdata = calloc(1, sizeof(struct ms_flipdata));
    if (!flipdata)
    {
        drmmode_bo_destroy(pDrmMode, new_front_bo);
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s: Failed to allocate flipdata\n", log_prefix);
        return FALSE;
    }

    flipdata->event = event;
    flipdata->screen = pScreen;
    flipdata->event_handler = pHandlerCB;
    flipdata->abort_handler = pAbortCB;

    /*
     * Take a local reference on flipdata.
     * if the first flip fails, the sequence abort
     * code will free the crtc flip data, and drop
     * it's reference which would cause this to be
     * freed when we still required it.
     */
    flipdata->flip_count++;

    /* Create a new handle for the back buffer */
    flipdata->old_fb_id = pDrmMode->fb_id;

    new_front_bo->width = pNewFrontPixmap->drawable.width;
    new_front_bo->height = pNewFrontPixmap->drawable.height;
    if (drmmode_bo_import(pDrmMode, new_front_bo, &pDrmMode->fb_id))
    {
        if (!pDrmMode->flip_bo_import_failed)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "%s: Import BO failed: %s\n",
                       log_prefix, strerror(errno));
            pDrmMode->flip_bo_import_failed = TRUE;
        }
        goto error_out;
    }
    else
    {
        if (pDrmMode->flip_bo_import_failed &&
            pNewFrontPixmap != pScreen->GetScreenPixmap(pScreen))
        {
            pDrmMode->flip_bo_import_failed = FALSE;
        }
    }

#if defined(DEBUG_PAGE_FLIP)
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "new front bo fb id: %d\n", pDrmMode->fb_id);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "old front bo fb id: %d\n", flipdata->old_fb_id);
#endif

    flags = DRM_MODE_PAGE_FLIP_EVENT;
    if (async)
        flags |= DRM_MODE_PAGE_FLIP_ASYNC;

    /* Queue flips on all enabled CRTCs.
     *
     * Note that if/when we get per-CRTC buffers, we'll have to update this.
     * Right now it assumes a single shared fb across all CRTCs, with the
     * kernel fixing up the offset of each CRTC as necessary.
     *
     * Also, flips queued on disabled or incorrectly configured displays
     * may never complete; this is a configuration error.
     */
    for (i = 0; i < config->num_crtc; i++)
    {
        xf86CrtcPtr pCrtc = config->crtc[i];

        if (!ls_is_crtc_on(pCrtc))
            continue;

        if (!queue_flip_on_crtc(pScreen,
                                pCrtc,
                                flipdata,
                                ref_crtc_vblank_pipe,
                                flags))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "%s: Queue flip on CRTC %d failed: %s\n",
                       log_prefix, i, strerror(errno));
            goto error_undo;
        }
    }

    drmmode_bo_destroy(pDrmMode, new_front_bo);

    /*
     * Do we have more than our local reference,
     * if so and no errors, then drop our local
     * reference and return now.
     */
    if (flipdata->flip_count > 1)
    {
        flipdata->flip_count--;

#if defined(DEBUG_PAGE_FLIP)
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "flip_count=%d\n",
                                     flipdata->flip_count);
#endif
        return TRUE;
    }

error_undo:

    /*
     * Have we just got the local reference?
     * free the framebuffer if so since nobody successfully
     * submitted anything
     */
    if (flipdata->flip_count == 1)
    {
        drmModeRmFB(lsp->fd, pDrmMode->fb_id);
        pDrmMode->fb_id = flipdata->old_fb_id;
    }

error_out:
    xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "Page flip failed: %s\n",
               strerror(errno));
    drmmode_bo_destroy(pDrmMode, new_front_bo);
    /* if only the local reference - free the structure,
     * else drop the local reference and return */
    if (flipdata->flip_count == 1)
        free(flipdata);
    else
        flipdata->flip_count--;

    return FALSE;
}
