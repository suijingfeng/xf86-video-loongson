/*
 * Copyright © 2013 Keith Packard
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

/** @file vblank.c
 *
 * Support for tracking the DRM's vblank events.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <xf86.h>
#include <xf86Crtc.h>
#include "driver.h"
#include "box.h"
#include "vblank.h"
#include "drmmode_display.h"
#include "loongson_entity.h"
#include "loongson_debug.h"

/**
 * Tracking for outstanding events queued to the kernel.
 *
 * Each list entry is a struct ls_drm_queue, which has a uint32_t
 * value generated from drm_seq that identifies the event and a
 * reference back to the crtc/screen associated with the event.
 * It's done this way rather than in the screen because we want
 * to be able to drain the list of event handlers that should be
 * called at server regen time, even though we don't close the
 * drm fd and have no way to actually drain the kernel events.
 */
static struct xorg_list ls_drm_queue;
static uint32_t ls_drm_seq;

static void ms_box_intersect(BoxPtr dest, BoxPtr a, BoxPtr b)
{
    dest->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    dest->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    if (dest->x1 >= dest->x2)
    {
        dest->x1 = dest->x2 = dest->y1 = dest->y2 = 0;
        return;
    }

    dest->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    dest->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    if (dest->y1 >= dest->y2)
        dest->x1 = dest->x2 = dest->y1 = dest->y2 = 0;
}

static void ms_crtc_box(xf86CrtcPtr crtc, BoxPtr crtc_box)
{
    if (crtc->enabled) {
        crtc_box->x1 = crtc->x;
        crtc_box->x2 =
            crtc->x + xf86ModeWidth(&crtc->mode, crtc->rotation);
        crtc_box->y1 = crtc->y;
        crtc_box->y2 =
            crtc->y + xf86ModeHeight(&crtc->mode, crtc->rotation);
    } else
        crtc_box->x1 = crtc_box->x2 = crtc_box->y1 = crtc_box->y2 = 0;
}


/*
 * return false if this crtc is not in use
 */
Bool ls_is_crtc_on(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    return crtc->enabled && drmmode_crtc->dpms_mode == DPMSModeOn;
}

/*
 * Return the first output which is connected to an active CRTC on this screen.
 *
 * RRFirstOutput() will return an output from a slave screen if it is primary,
 * which is not the behavior that ms_covering_crtc() wants.
 */

static RROutputPtr ms_first_output(ScreenPtr pScreen)
{
    rrScrPriv(pScreen);
    RROutputPtr output;
    int i, j;

    if (!pScrPriv)
        return NULL;

    if (pScrPriv->primaryOutput && pScrPriv->primaryOutput->crtc &&
        (pScrPriv->primaryOutput->pScreen == pScreen)) {
        return pScrPriv->primaryOutput;
    }

    for (i = 0; i < pScrPriv->numCrtcs; i++) {
        RRCrtcPtr crtc = pScrPriv->crtcs[i];

        for (j = 0; j < pScrPriv->numOutputs; j++) {
            output = pScrPriv->outputs[j];
            if (output->crtc == crtc)
                return output;
        }
    }
    return NULL;
}

/*
 * Return the crtc covering 'box'. If two crtcs cover a portion of
 * 'box', then prefer the crtc with greater coverage.
 */
static xf86CrtcPtr ms_covering_xf86_crtc(ScreenPtr pScreen,
                                         BoxPtr box,
                                         Bool screen_is_ms)
{
    ScrnInfoPtr scrn = xf86ScreenToScrn(pScreen);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CrtcPtr crtc, best_crtc;
    int coverage, best_coverage;
    int c;
    BoxRec crtc_box, cover_box;

    best_crtc = NULL;
    best_coverage = 0;

    if (!xf86_config)
    {
        ERROR_MSG("xf86_config is NULL");
        return NULL;
    }

    for (c = 0; c < xf86_config->num_crtc; c++)
    {
        Bool crtc_on;
        crtc = xf86_config->crtc[c];

        if (screen_is_ms)
            crtc_on = ls_is_crtc_on(crtc);
        else
            crtc_on = crtc->enabled;

        /* If the CRTC is off, treat it as not covering */
        if (!crtc_on)
            continue;

        ms_crtc_box(crtc, &crtc_box);
        ms_box_intersect(&cover_box, &crtc_box, box);
        coverage = box_area(&cover_box);
        if (coverage > best_coverage) {
            best_crtc = crtc;
            best_coverage = coverage;
        }
    }

    return best_crtc;
}


xf86CrtcPtr ms_dri2_crtc_covering_drawable(DrawablePtr pDraw)
{
    ScreenPtr pScreen = pDraw->pScreen;
    BoxRec box;

    box.x1 = pDraw->x;
    box.y1 = pDraw->y;
    box.x2 = box.x1 + pDraw->width;
    box.y2 = box.y1 + pDraw->height;

    return ms_covering_xf86_crtc(pScreen, &box, TRUE);
}


static Bool
ms_get_kernel_ust_msc(xf86CrtcPtr crtc,
                      uint64_t *msc, uint64_t *ust)
{
    ScreenPtr screen = crtc->randr_crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    loongsonPtr ms = loongsonPTR(scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmVBlank vbl;
    int ret;

    if (ms->has_queue_sequence || !ms->tried_queue_sequence) {
        uint64_t ns;
        ms->tried_queue_sequence = TRUE;

        ret = drmCrtcGetSequence(ms->fd, drmmode_crtc->mode_crtc->crtc_id,
                                 msc, &ns);
        if (ret != -1 || (errno != ENOTTY && errno != EINVAL)) {
            ms->has_queue_sequence = TRUE;
            if (ret == 0)
                *ust = ns / 1000;
            return ret == 0;
        }
    }
    /* Get current count */
    vbl.request.type = DRM_VBLANK_RELATIVE | drmmode_crtc->vblank_pipe;
    vbl.request.sequence = 0;
    vbl.request.signal = 0;
    ret = drmWaitVBlank(ms->fd, &vbl);
    if (ret) {
        *msc = 0;
        *ust = 0;
        return FALSE;
    } else {
        *msc = vbl.reply.sequence;
        *ust = (CARD64) vbl.reply.tval_sec * 1000000 + vbl.reply.tval_usec;
        return TRUE;
    }
}

Bool
ms_queue_vblank(xf86CrtcPtr crtc, ms_queue_flag flags,
                uint64_t msc, uint64_t *msc_queued, uint32_t seq)
{
    ScreenPtr screen = crtc->randr_crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    loongsonPtr ms = loongsonPTR(scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmVBlank vbl;
    int ret;

    for (;;) {
        /* Queue an event at the specified sequence */
        if (ms->has_queue_sequence || !ms->tried_queue_sequence) {
            uint32_t drm_flags = 0;
            uint64_t kernel_queued;

            ms->tried_queue_sequence = TRUE;

            if (flags & MS_QUEUE_RELATIVE)
                drm_flags |= DRM_CRTC_SEQUENCE_RELATIVE;
            if (flags & MS_QUEUE_NEXT_ON_MISS)
                drm_flags |= DRM_CRTC_SEQUENCE_NEXT_ON_MISS;

            ret = drmCrtcQueueSequence(ms->fd, drmmode_crtc->mode_crtc->crtc_id,
                                       drm_flags, msc, &kernel_queued, seq);
            if (ret == 0) {
                if (msc_queued)
                    *msc_queued = ms_kernel_msc_to_crtc_msc(crtc, kernel_queued, TRUE);
                ms->has_queue_sequence = TRUE;
                return TRUE;
            }

            if (ret != -1 || (errno != ENOTTY && errno != EINVAL)) {
                ms->has_queue_sequence = TRUE;
                goto check;
            }
        }
        vbl.request.type = DRM_VBLANK_EVENT | drmmode_crtc->vblank_pipe;
        if (flags & MS_QUEUE_RELATIVE)
            vbl.request.type |= DRM_VBLANK_RELATIVE;
        else
            vbl.request.type |= DRM_VBLANK_ABSOLUTE;
        if (flags & MS_QUEUE_NEXT_ON_MISS)
            vbl.request.type |= DRM_VBLANK_NEXTONMISS;

        vbl.request.sequence = msc;
        vbl.request.signal = seq;
        ret = drmWaitVBlank(ms->fd, &vbl);
        if (ret == 0) {
            if (msc_queued)
                *msc_queued = ms_kernel_msc_to_crtc_msc(crtc, vbl.reply.sequence, FALSE);
            return TRUE;
        }
    check:
        if (errno != EBUSY) {
            ms_drm_abort_seq(scrn, seq);
            return FALSE;
        }
        ms_flush_drm_events(screen);
    }
}

/**
 * Convert a 32-bit or 64-bit kernel MSC sequence number to a 64-bit local
 * sequence number, adding in the high 32 bits, and dealing with 32-bit
 * wrapping if needed.
 */
uint64_t ms_kernel_msc_to_crtc_msc(xf86CrtcPtr crtc,
                                   uint64_t sequence,
                                   Bool is64bit)
{
    struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;

    if (!is64bit)
    {
        /* sequence is provided as a 32 bit value from one of the 32 bit apis,
         * e.g., drmWaitVBlank(), classic vblank events, or pageflip events.
         *
         * Track and handle 32-Bit wrapping, somewhat robust against occasional
         * out-of-order not always monotonically increasing sequence values.
         */
        if ((int64_t) sequence < ((int64_t) drmmode_crtc->msc_prev - 0x40000000))
            drmmode_crtc->msc_high += 0x100000000L;

        if ((int64_t) sequence > ((int64_t) drmmode_crtc->msc_prev + 0x40000000))
            drmmode_crtc->msc_high -= 0x100000000L;

        drmmode_crtc->msc_prev = sequence;

        return drmmode_crtc->msc_high + sequence;
    }

    /* True 64-Bit sequence from Linux 4.15+ 64-Bit drmCrtcGetSequence /
     * drmCrtcQueueSequence apis and events. Pass through sequence unmodified,
     * but update the 32-bit tracking variables with reliable ground truth.
     *
     * With 64-Bit api in use, the only !is64bit input is from pageflip events,
     * and any pageflip event is usually preceded by some is64bit input from
     * swap scheduling, so this should provide reliable mapping for pageflip
     * events based on true 64-bit input as baseline as well.
     */
    drmmode_crtc->msc_prev = sequence;
    drmmode_crtc->msc_high = sequence & 0xffffffff00000000;

    return sequence;
}


int ms_get_crtc_ust_msc(xf86CrtcPtr crtc, CARD64 *ust, CARD64 *msc)
{
    ScreenPtr screen = crtc->randr_crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    loongsonPtr ms = loongsonPTR(scrn);
    uint64_t kernel_msc;

    if (!ms_get_kernel_ust_msc(crtc, &kernel_msc, ust))
        return BadMatch;
    *msc = ms_kernel_msc_to_crtc_msc(crtc, kernel_msc, ms->has_queue_sequence);

    return Success;
}

/**
 * Check for pending DRM events and process them.
 */
static void ls_socket_handler_cb(int fd, int ready, void *data)
{
    ScreenPtr pScreen = (ScreenPtr)data;
    ScrnInfoPtr pScrn;
    loongsonPtr lsp;
    int ret;

    if (data == NULL)
    {
        xf86DrvMsg(-1, X_WARNING,
                   "%s: data=NULL: fd=%d, ready=%d\n",
                   __func__, fd, ready);
        return;
    }

    // runtime
    pScrn = xf86ScreenToScrn(pScreen);
    lsp = loongsonPTR(pScrn);

    ret = drmHandleEvent(fd, &lsp->event_context);
    if (ret < 0)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s: %d\n", __func__, ret);
}

/*
 * Enqueue a potential drm response; when the associated response
 * appears, we've got data to pass to the handler from here
 */
uint32_t ms_drm_queue_alloc(xf86CrtcPtr crtc,
                            void *data,
                            ms_drm_handler_proc handler,
                            ms_drm_abort_proc abort)
{
    ScreenPtr pScreen = crtc->randr_crtc->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct ls_drm_queue *q = calloc(1, sizeof(struct ls_drm_queue));

    if (!q)
        return 0;

    if (ls_drm_seq == 0)
        ++ls_drm_seq;

    q->seq = ls_drm_seq++;
    q->scrn = pScrn;
    q->crtc = crtc;
    q->data = data;
    q->handler = handler;
    q->abort = abort;

    xorg_list_add(&q->list, &ls_drm_queue);

    return q->seq;
}

/**
 * Abort one queued DRM entry, removing it
 * from the list, calling the abort function and
 * freeing the memory
 */
static void ms_drm_abort_one(struct ls_drm_queue *q)
{
    xorg_list_del(&q->list);
    q->abort(q->data);
    free(q);
}

/**
 * Abort all queued entries on a specific scrn, used
 * when resetting the X server
 */
static void ls_abort_scrn(ScrnInfoPtr pScrn)
{
    struct ls_drm_queue *q, *tmp;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s\n", __func__);

    xorg_list_for_each_entry_safe(q, tmp, &ls_drm_queue, list) {
        if (q->scrn == pScrn)
            ms_drm_abort_one(q);
    }
}

/**
 * Abort by drm queue sequence number.
 */
void ms_drm_abort_seq(ScrnInfoPtr scrn, uint32_t seq)
{
    struct ls_drm_queue *q, *tmp;

    xorg_list_for_each_entry_safe(q, tmp, &ls_drm_queue, list)
    {
        if (q->seq == seq)
        {
            ms_drm_abort_one(q);
            break;
        }
    }
}

/*
 * Externally usable abort function that uses a callback to match a single
 * queued entry to abort
 */
void
ms_drm_abort(ScrnInfoPtr scrn, Bool (*match)(void *data, void *match_data),
             void *match_data)
{
    struct ls_drm_queue *q;

    xorg_list_for_each_entry(q, &ls_drm_queue, list) {
        if (match(q->data, match_data)) {
            ms_drm_abort_one(q);
            break;
        }
    }
}

/*
 * General DRM kernel handler.
 * Looks for the matching sequence number in the
 * drm event queue and calls the handler for it.
 */
static void ls_sequence_handler(int fd,
                                uint64_t frame,
                                uint64_t ns,
                                Bool is64bit,
                                uint64_t user_data)
{
    struct ls_drm_queue *q, *tmp;
    uint32_t seq = (uint32_t) user_data;

    xorg_list_for_each_entry_safe(q, tmp, &ls_drm_queue, list)
    {
        if (q->seq == seq)
        {
            uint64_t msc;

            DEBUG_MSG("%s, seq=%u\n", __func__, seq);

            msc = ms_kernel_msc_to_crtc_msc(q->crtc, frame, is64bit);
            xorg_list_del(&q->list);
            q->handler(msc, ns / 1000, q->data);
            free(q);
            break;
        }
    }
}


static void ls_sequence_handler_64bit(int fd,
                                      uint64_t frame,
                                      uint64_t ns,
                                      uint64_t user_data)
{
    DEBUG_MSG("%s, fd=%d, frame=%lu, ns=%lu\n",
              __func__, fd, frame, ns);

    /* frame is true 64 bit wrapped into 64 bit */
    ls_sequence_handler(fd, frame, ns, TRUE, user_data);
}


static void ls_vblank_handler(int fd,
                              uint32_t frame,
                              uint32_t sec,
                              uint32_t usec,
                              void *user_ptr)
{
    uint64_t ns;

    xf86Msg(X_INFO, "%s, fd=%d, frame=%u, sec=%u, usec=%u\n",
                    __func__, fd, frame, sec, usec);

    ns = ((uint64_t) sec * 1000000 + usec) * 1000;

    /* frame is 32 bit wrapped into 64 bit */
    ls_sequence_handler(fd, frame, ns, FALSE,
                            (uint32_t) (uintptr_t) user_ptr);
}

static void ls_pageflip_handler(int fd,
                                uint32_t frame,
                                uint32_t sec,
                                uint32_t usec,
                                void *user_ptr)
{
    uint64_t ns;

#if defined(DEBUG_PAGE_FLIP)
    xf86Msg(X_INFO, "%s, fd=%d, frame=%u, sec=%u, usec=%u\n",
                    __func__, fd, frame, sec, usec);
#endif

    ns = ((uint64_t) sec * 1000000 + usec) * 1000;

    /* frame is 32 bit wrapped into 64 bit */
    ls_sequence_handler(fd, frame, ns, FALSE,
                            (uint32_t) (uintptr_t) user_ptr);
}


Bool ms_vblank_screen_init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);

    xorg_list_init(&ls_drm_queue);

    lsp->event_context.version = 4;
    lsp->event_context.vblank_handler = ls_vblank_handler;
    lsp->event_context.page_flip_handler = ls_pageflip_handler;
    lsp->event_context.sequence_handler = ls_sequence_handler_64bit;

    /* We need to re-register the DRM fd for the synchronisation
     * feedback on every server generation, so perform the
     * registration within ScreenInit and not PreInit.
     */
    if (serverGeneration != LS_EntityGetFd_wakeup(pScrn))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: %d\n", __func__, __LINE__);
        /*
         * Registers a callback to be invoked when the specified
         * file descriptor becomes readable.
         */
        SetNotifyFd(lsp->fd, ls_socket_handler_cb, X_NOTIFY_READ, pScreen);
        LS_EntityInitFd_wakeup(pScrn, serverGeneration);
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: %d\n", __func__, __LINE__);
        LS_EntityIncRef_weakeup(pScrn);
    }

    return TRUE;
}


void ms_vblank_close_screen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);

    ls_abort_scrn(pScrn);

    if ((serverGeneration == LS_EntityGetFd_wakeup(pScrn)) &&
        (0 == LS_EntityDecRef_weakeup(pScrn)))
    {
        RemoveNotifyFd(lsp->fd);
    }
}
