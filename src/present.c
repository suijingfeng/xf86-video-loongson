/*
 * Copyright © 2014 Intel Corporation
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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include <xf86.h>
#include <xf86Crtc.h>
#include <xf86drm.h>
#include <xf86str.h>
#include <present.h>

#include "driver.h"
#include "box.h"
#include "vblank.h"
#include "drmmode_display.h"
#include "loongson_debug.h"


struct ms_present_vblank_event {
    uint64_t event_id;
    Bool unflip;
};


static void ls_randr_crtc_box(RRCrtcPtr crtc, BoxPtr crtc_box)
{
    if (crtc->mode)
    {
        crtc_box->x1 = crtc->x;
        crtc_box->y1 = crtc->y;
        switch (crtc->rotation)
        {
            case RR_Rotate_0:
            case RR_Rotate_180:
            default:
                crtc_box->x2 = crtc->x + crtc->mode->mode.width;
                crtc_box->y2 = crtc->y + crtc->mode->mode.height;
                break;
            case RR_Rotate_90:
            case RR_Rotate_270:
                crtc_box->x2 = crtc->x + crtc->mode->mode.height;
                crtc_box->y2 = crtc->y + crtc->mode->mode.width;
                break;
        }
    }
    else
    {
        crtc_box->x1 = crtc_box->x2 = crtc_box->y1 = crtc_box->y2 = 0;
    }
}


static RRCrtcPtr ls_covering_randr_crtc(ScreenPtr pScreen, BoxPtr box)
{
    rrScrPrivPtr pScrPriv;
    RRCrtcPtr best_crtc;
    int c;
    int coverage, best_coverage;
    BoxRec crtc_box, cover_box;

    best_crtc = NULL;
    best_coverage = 0;

    if (!dixPrivateKeyRegistered(rrPrivKey))
    {
        ERROR_MSG("rrPrivKey is not registered");
        return NULL;
    }

    pScrPriv = rrGetScrPriv(pScreen);
    if (!pScrPriv)
    {
        ERROR_MSG("can not get screen private");
        return NULL;
    }

    for (c = 0; c < pScrPriv->numCrtcs; c++)
    {
        RRCrtcPtr crtc;

        crtc = pScrPriv->crtcs[c];

        /* If the CRTC is off, treate it as not covering */
        if (ls_is_crtc_on((xf86CrtcPtr) crtc->devPrivate) == FALSE)
            continue;

        DEBUG_MSG("crtc-%d is on", c);

        ls_randr_crtc_box(crtc, &crtc_box);
        box_get_intersect(&cover_box, &crtc_box, box);
        coverage = box_area(&cover_box);
        if (coverage > best_coverage)
        {
            best_crtc = crtc;
            best_coverage = coverage;
        }
    }

    return best_crtc;
}

/*
 *   Runtime called frequently
 */
static RRCrtcPtr ls_present_get_crtc(WindowPtr window)
{
    DrawablePtr pDraw = &window->drawable;
    ScreenPtr pScreen = pDraw->pScreen;
    BoxRec box;

    box.x1 = pDraw->x;
    box.y1 = pDraw->y;
    box.x2 = box.x1 + pDraw->width;
    box.y2 = box.y1 + pDraw->height;

    // DEBUG_MSG("(%d, %d), (%d, %d)\n", box.x1, box.y1, box.x2, box.y2);

    // Allow calling on non modesetting Screens
    //
    // 99% of the code in ms_covering_crtc is video-driver agnostic.
    // Add a screen_is_ms parameter when when FALSE skips the one
    // ms specific check, this will allow calling ms_covering_crtc
    // on slave GPUs.
    return ls_covering_randr_crtc(pScreen, &box);
}

static int ms_present_get_ust_msc(RRCrtcPtr crtc, CARD64 *ust, CARD64 *msc)
{
    xf86CrtcPtr xf86_crtc = crtc->devPrivate;

    return ms_get_crtc_ust_msc(xf86_crtc, ust, msc);
}

/*
 * Called when the queued vblank event has occurred
 */
static void ms_present_vblank_handler(uint64_t msc, uint64_t usec, void *data)
{
    struct ms_present_vblank_event *event = data;

    DEBUG_MSG("\t\t %s %lld msc %llu\n",
                 __func__,
                 (long long) event->event_id,
                 (long long) msc);

    present_event_notify(event->event_id, usec, msc);
    free(event);
}

/*
 * Called when the queued vblank is aborted
 */
static void ms_present_vblank_abort(void *data)
{
    struct ms_present_vblank_event *event = data;

    DEBUG_MSG("\t\t %s %lld\n", __func__, (long long) event->event_id);

    free(event);
}

/*
 * Queue an event to report back to the Present extension when the specified
 * MSC has past
 */
static int ms_present_queue_vblank(RRCrtcPtr crtc, uint64_t event_id,
                                   uint64_t msc)
{
    xf86CrtcPtr xf86_crtc = crtc->devPrivate;
    struct ms_present_vblank_event *event;
    uint32_t seq;

    event = calloc(sizeof(struct ms_present_vblank_event), 1);
    if (!event)
        return BadAlloc;
    event->event_id = event_id;
    seq = ms_drm_queue_alloc(xf86_crtc, event,
                             ms_present_vblank_handler,
                             ms_present_vblank_abort);
    if (!seq)
    {
        free(event);
        return BadAlloc;
    }

    if (!ms_queue_vblank(xf86_crtc, MS_QUEUE_ABSOLUTE, msc, NULL, seq))
        return BadAlloc;

    DEBUG_MSG("\t\t %s %lld seq %u msc %llu\n",
                 __func__, (long long) event_id, seq, (long long) msc);
    return Success;
}

static Bool ms_present_event_match(void *data, void *match_data)
{
    struct ms_present_vblank_event *event = data;
    uint64_t *match = match_data;

    return *match == event->event_id;
}

/*
 * Remove a pending vblank event from the DRM queue so that it is not reported
 * to the extension
 */
static void ms_present_abort_vblank(RRCrtcPtr crtc, uint64_t event_id, uint64_t msc)
{
    ScreenPtr screen = crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);

    ms_drm_abort(scrn, ms_present_event_match, &event_id);
}

/*
 * Flush our batch buffer when requested by the Present extension.
 */
static void ms_present_flush(WindowPtr window)
{
#ifdef GLAMOR_HAS_GBM
    ScreenPtr pScreen = window->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = modesettingPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;
    struct GlamorAPI * const pGlamor = &ms->glamor;

    if (pDrmMode->glamor_enabled)
    {
        pGlamor->block_handler(pScreen);
    }
#endif
}

#ifdef GLAMOR_HAS_GBM

/**
 * Callback for the DRM event queue when a flip has completed on all pipes
 *
 * Notify the extension code
 */
static void ms_present_flip_handler(loongsonPtr ls,
                                    uint64_t msc,
                                    uint64_t ust,
                                    void *data)
{
    struct ms_present_vblank_event * const pEvent = data;
    struct drmmode_rec * const pDrmMode = &ls->drmmode;

    DEBUG_MSG( "\t\t %s: %lld msc %llu ust %llu\n",
                  __func__,
                  (long long) pEvent->event_id,
                  (long long) msc, (long long) ust);

    if (pEvent->unflip)
        pDrmMode->present_flipping = FALSE;

    present_event_notify(pEvent->event_id, ust, msc);
    free(pEvent);
}

/*
 * Callback for the DRM queue abort code.  A flip has been aborted.
 */
static void ms_present_flip_abort(loongsonPtr ms, void *data)
{
    struct ms_present_vblank_event *event = data;

    DEBUG_MSG("\t\t %s:fa %lld\n", __func__, (long long) event->event_id);

    free(event);
}

/*
 * Test to see if page flipping is possible on the target crtc
 *
 * We ignore sw-cursors when *disabling* flipping, we may very well be
 * returning to scanning out the normal framebuffer *because* we just
 * switched to sw-cursor mode and check_flip just failed because of that.
 */
static Bool ms_present_check_unflip(RRCrtcPtr crtc,
                                    WindowPtr window,
                                    PixmapPtr pixmap,
                                    Bool sync_flip,
                                    PresentFlipReason *reason)
{
    ScreenPtr pScreen = window->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
    int num_crtcs_on = 0;
    int i;

    if (pDrmMode->pageflip == FALSE)
        return FALSE;

    if (pDrmMode->dri2_flipping)
        return FALSE;

    if (!pScrn->vtSema)
        return FALSE;

    for (i = 0; i < config->num_crtc; i++)
    {
        drmmode_crtc_private_ptr drmmode_crtc = config->crtc[i]->driver_private;

        /* Don't do pageflipping if CRTCs are rotated. */
        if (drmmode_crtc->rotate_bo.gbm)
        {
            INFO_MSG("Don't do pageflipping because of CRTCs are rotated");
            return FALSE;
        }

        if (ls_is_crtc_on(config->crtc[i]))
            num_crtcs_on++;
    }

    /* We can't do pageflipping if all the CRTCs are off */
    if (num_crtcs_on == 0)
    {
        return FALSE;
    }

    /* Check stride, can't change that on flip */
    if (ms->atomic_modeset == FALSE)
    {
        uint32_t fbo_patch = drmmode_bo_get_pitch(&pDrmMode->front_bo);
        if (pixmap->devKind != fbo_patch)
        {
            INFO_MSG("fbo_patch: %d", fbo_patch);
            return FALSE;
        }
    }

#ifdef GBM_BO_WITH_MODIFIERS
    if (pDrmMode->glamor_enabled)
    {
        struct GlamorAPI  * const pGlamor = &ms->glamor;
        struct gbm_bo *gbm;
        /* Check if buffer format/modifier is supported by all active CRTCs */
        gbm = pGlamor->gbm_bo_from_pixmap(pScreen, pixmap);

        if (gbm)
        {
            uint32_t format;
            uint64_t modifier;

            DEBUG_MSG("GBM\n");

            format = gbm_bo_get_format(gbm);
            modifier = gbm_bo_get_modifier(gbm);
            gbm_bo_destroy(gbm);

            if (!drmmode_is_format_supported(pScrn, format, modifier))
            {
                if (reason)
                    *reason = PRESENT_FLIP_REASON_BUFFER_FORMAT;
                return FALSE;
            }
        }
    }
#endif

    /* Make sure there's a bo we can get to */
    /* XXX: actually do this.  also...is it sufficient?
     * if (!glamor_get_pixmap_private(pixmap))
     *     return FALSE;
     */

    TRACE_EXIT();

    return TRUE;
}


/*
 * Same as 'check_flip' but it can return a 'reason' why the flip would fail.
 */
static Bool ls_present_check_flip(RRCrtcPtr crtc,
                                  WindowPtr window,
                                  PixmapPtr pixmap,
                                  Bool sync_flip,
                                  PresentFlipReason *reason)
{
    ScreenPtr pScreen = window->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = modesettingPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;

    if (pDrmMode->sprites_visible > 0)
    {
        INFO_MSG("visible sprites: %d\n", pDrmMode->sprites_visible);
        return FALSE;
    }

    return ms_present_check_unflip(crtc, window, pixmap, sync_flip, reason);
}

/*
 * Queue a flip on 'crtc' to 'pixmap' at 'target_msc'.
 */
/* Flip pixmap, return false if it didn't happen.
 *
 * 'crtc' is to be used for any necessary synchronization.
 *
 * 'sync_flip' requests that the flip be performed at the next
 * vertical blank interval to avoid tearing artifacts.
 * If false, the flip should be performed as soon as possible.
 *
 * present_event_notify should be called with 'event_id'
 * when the flip occurs
 */

static Bool ls_present_flip(RRCrtcPtr crtc,
                            uint64_t event_id,
                            uint64_t target_msc,
                            PixmapPtr pixmap,
                            Bool sync_flip)
{
    ScreenPtr screen = crtc->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(screen);
    loongsonPtr ls = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ls->drmmode;

    xf86CrtcPtr xf86_crtc = crtc->devPrivate;
    drmmode_crtc_private_ptr drmmode_crtc = xf86_crtc->driver_private;
    Bool ret;
    struct ms_present_vblank_event *event;

    ret = ls_present_check_flip(crtc, screen->root, pixmap, sync_flip, NULL);
    if (ret == FALSE)
    {
        INFO_MSG("\t %s: %lld msc %llu\n",
                  __func__, (long long) event_id, (long long) target_msc);

        return FALSE;
    }

    event = calloc(1, sizeof(struct ms_present_vblank_event));
    if (!event)
        return FALSE;

    event->event_id = event_id;
    event->unflip = FALSE;

    ret = ms_do_pageflip(screen, pixmap, event, drmmode_crtc->vblank_pipe,
                         !sync_flip,
                         ms_present_flip_handler,
                         ms_present_flip_abort,
                         "Present-flip");
    if (ret == TRUE)
        pDrmMode->present_flipping = TRUE;

    return ret;
}

/*
 * Queue a flip back to the normal frame buffer
 */
static void ls_present_unflip(ScreenPtr pScreen, uint64_t event_id)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = modesettingPTR(scrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;
    PixmapPtr pixmap = pScreen->GetScreenPixmap(pScreen);
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(scrn);
    int nCrtc = config->num_crtc;
    int i;
    Bool ret;

    struct ms_present_vblank_event *event;

    event = calloc(1, sizeof(struct ms_present_vblank_event));
    if (!event)
        return;

    DEBUG_MSG("event_id: %lu\n", event_id);

    event->event_id = event_id;
    event->unflip = TRUE;

    ret = ms_present_check_unflip(NULL, pScreen->root, pixmap, TRUE, NULL);
    if (ret == TRUE)
    {
        INFO_MSG("ms_present_check_unflip() return true.\n");
        if (pDrmMode->glamor_enabled)
        {
            ret = ms_do_pageflip(pScreen, pixmap, event, -1, FALSE,
                       ms_present_flip_handler, ms_present_flip_abort,
                       "Present-unflip");
            if (ret)
                return;
        }
    }

    DEBUG_MSG("nCrtc = %d.\n", nCrtc);

    for (i = 0; i < nCrtc; ++i)
    {
        xf86CrtcPtr crtc = config->crtc[i];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        DEBUG_MSG("crtc version: %d\n", crtc->version);

        if (crtc->enabled == FALSE)
        {
            DEBUG_MSG("crtc %d is being disabled.\n", i);
            continue;
        }
        /* info->drmmode.fb_id still points to the FB for the last flipped BO.
         * Clear it, drmmode_set_mode_major will re-create it
         */
        if (drmmode_crtc->drmmode->fb_id)
        {
            DEBUG_MSG("RmFB %d.\n", drmmode_crtc->drmmode->fb_id);

            drmModeRmFB(drmmode_crtc->drmmode->fd, drmmode_crtc->drmmode->fb_id);
            drmmode_crtc->drmmode->fb_id = 0;
        }

        if (drmmode_crtc->dpms_mode == DPMSModeOn)
        {
            crtc->funcs->set_mode_major(crtc, &crtc->mode, crtc->rotation,
                    crtc->x, crtc->y);

            DEBUG_MSG("DPMSModeOn\n");
        }
        else
        {
            DEBUG_MSG("Not DPMSModeOn\n");
            drmmode_crtc->need_modeset = TRUE;
        }
    }

    present_event_notify(event_id, 0, 0);
    pDrmMode->present_flipping = FALSE;
}
#endif

static present_screen_info_rec loongson_present_screen = {
    .version = PRESENT_SCREEN_INFO_VERSION,

    .get_crtc = ls_present_get_crtc,
    .get_ust_msc = ms_present_get_ust_msc,
    .queue_vblank = ms_present_queue_vblank,
    .abort_vblank = ms_present_abort_vblank,
    .flush = ms_present_flush,

    .capabilities = PresentCapabilityNone,
    .check_flip = NULL,
//    .check_flip2 = ls_present_check_flip,
//    .flip = ls_present_flip,
//    .unflip = ls_present_unflip,
};


Bool ms_present_screen_init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = modesettingPTR(pScrn);
    uint64_t value;
    int ret;

    ret = drmGetCap(ms->fd, DRM_CAP_ASYNC_PAGE_FLIP, &value);
    if ((ret == 0) && (value == 1))
    {
        loongson_present_screen.capabilities |= PresentCapabilityAsync;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Async present is supported.\n");
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Async present is NOT supported.\n");
    }

    return present_screen_init(pScreen, &loongson_present_screen);
}
