/*
 * Copyright (C) 2022 Loongson Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Sui Jingfeng <suijingfeng@loongson.cn>
 */

/**
 * @file dri2.c
 *
 * Implements generic support for DRI2 on KMS, using loongson pixmaps
 * for color buffer management (no support for other aux buffers), and
 * the DRM vblank ioctls.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <list.h>
#include <xf86.h>
#include <dri2.h>
#include <sys/ioctl.h>
#include <gsgpu_drm.h>
#include <gsgpu.h>

#include "driver.h"

#include "gsgpu_dri2.h"
#include "vblank.h"
#include "loongson_prime.h"
#include "loongson_pixmap.h"
#include "loongson_debug.h"


enum gsgpu_dri2_frame_event_type {
    MS_DRI2_QUEUE_SWAP,
    MS_DRI2_QUEUE_FLIP,
    MS_DRI2_WAIT_MSC,
};

struct gsgpu_dri2_frame_event {
    ScreenPtr screen;

    DrawablePtr drawable;
    ClientPtr client;
    enum gsgpu_dri2_frame_event_type type;
    int frame;
    xf86CrtcPtr crtc;

    struct xorg_list drawable_resource, client_resource;

    /* for swaps & flips only */
    DRI2SwapEventPtr event_complete;
    void *event_data;
    DRI2BufferPtr front;
    DRI2BufferPtr back;
};

struct gsgpu_dri2_buffer_private {
    int refcnt;
    PixmapPtr pixmap;
};

static DevPrivateKeyRec gsgpu_dri2_client_key;
static RESTYPE frame_event_client_type, frame_event_drawable_type;
static int gsgpu_dri2_server_generation;

struct gsgpu_dri2_resource {
    XID id;
    RESTYPE type;
    struct xorg_list list;
};

static struct gsgpu_dri2_resource *gsgpu_get_resource(XID id, RESTYPE type)
{
    struct gsgpu_dri2_resource *resource;
    void *ptr = NULL;

    dixLookupResourceByType(&ptr, id, type, NULL, DixWriteAccess);
    if (ptr)
        return ptr;

    resource = malloc(sizeof(*resource));
    if (resource == NULL)
        return NULL;

    if (!AddResource(id, type, resource))
        return NULL;

    resource->id = id;
    resource->type = type;
    xorg_list_init(&resource->list);

    return resource;
}

static inline PixmapPtr get_drawable_pixmap(DrawablePtr drawable)
{
    ScreenPtr pScreen = drawable->pScreen;

    if (drawable->type == DRAWABLE_PIXMAP)
        return (PixmapPtr) drawable;
    else
        return pScreen->GetWindowPixmap((WindowPtr) drawable);
}


/* Get GEM flink name for a pixmap */
static Bool gsgpu_get_flink_name(int drmfd, PixmapPtr pPixmap, uint32_t *name)
{
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);
    struct gsgpu_bo *gbo;
    int ret;

    if (!priv)
    {
        xf86Msg(X_ERROR, "dri2: pixmap(%p) has no backing store\n",
                pPixmap);
        return FALSE;
    }

    gbo = priv->gbo;
    if (gbo)
    {
        ret = gsgpu_bo_export(gbo, gsgpu_bo_handle_type_gem_flink_name, name);
        if (ret == 0)
        {
            return TRUE;
        }

        xf86Msg(X_ERROR, "dri2: failed get flink name from pixmap(%p)\n",
                pPixmap);
    }

    /* backing by dumb ? */
    if (priv->bo)
    {
        struct drm_gem_flink flink;

        xf86Msg(X_INFO, "dri2: pixmap(%p) is backing by dumb\n", pPixmap);

        flink.handle = dumb_bo_handle(priv->bo);
        if (ioctl(drmfd, DRM_IOCTL_GEM_FLINK, &flink) < 0)
        {
            xf86Msg(X_INFO, "dri2: failed to get a flink name from dumb bo\n");
            return FALSE;
        }

        *name = flink.name;

        return TRUE;
    }

    return FALSE;
}

static DRI2Buffer2Ptr
gsgpu_dri2_create_buffer2(ScreenPtr pScreen,
                          DrawablePtr drawable,
                          unsigned int attachment,
                          unsigned int format)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    PixmapPtr pPixmap = NULL;
    DRI2Buffer2Ptr buffer;
    struct gsgpu_dri2_buffer_private *private;
    Bool res;

    TRACE_ENTER();

    buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL)
        return NULL;

    private = calloc(1, sizeof(*private));
    if (private == NULL)
    {
        free(buffer);
        return NULL;
    }

    if (attachment == DRI2BufferFrontLeft)
    {
        pPixmap = get_drawable_pixmap(drawable);
        if (pPixmap && pPixmap->drawable.pScreen != pScreen)
            pPixmap = NULL;

        if (pPixmap)
            pPixmap->refcnt++;
    }

    if (pPixmap == NULL)
    {
        int pixmap_width = drawable->width;
        int pixmap_height = drawable->height;
        int pixmap_cpp = (format != 0) ? format : drawable->depth;

        /* Assume that non-color-buffers require special device-specific
         * handling. Mesa currently makes no requests for non-color aux
         * buffers.
         */
        switch (attachment)
        {
        case DRI2BufferAccum:
        case DRI2BufferBackLeft:
        case DRI2BufferBackRight:
        case DRI2BufferFakeFrontLeft:
        case DRI2BufferFakeFrontRight:
        case DRI2BufferFrontLeft:
        case DRI2BufferFrontRight:
            break;

        case DRI2BufferStencil:
        case DRI2BufferDepth:
        case DRI2BufferDepthStencil:
        case DRI2BufferHiz:
        default:
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "Request for DRI2 buffer attachment %d unsupported\n",
                       attachment);
            free(private);
            free(buffer);
            return NULL;
        }

        pPixmap = pScreen->CreatePixmap(pScreen,
                                        pixmap_width,
                                        pixmap_height,
                                        pixmap_cpp,
                                        0);
        if (pPixmap == NULL)
        {
            free(private);
            free(buffer);
            return NULL;
        }
    }

    buffer->attachment = attachment;
    buffer->cpp = pPixmap->drawable.bitsPerPixel / 8;
    buffer->format = format;
    /* The buffer's flags field is unused by the client drivers in
     * Mesa currently.
     */
    buffer->flags = 0;

    res = gsgpu_get_flink_name(lsp->fd, pPixmap, &buffer->name);
    if (res == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to get DRI2 name for pixmap\n");
        pScreen->DestroyPixmap(pPixmap);
        free(private);
        free(buffer);
        return NULL;
    }

    buffer->pitch = pPixmap->devKind;
    buffer->driverPrivate = private;
    private->refcnt = 1;
    private->pixmap = pPixmap;

    TRACE_EXIT();

    return buffer;
}

static DRI2Buffer2Ptr
gsgpu_dri2_create_buffer(DrawablePtr drawable,
                         unsigned int attachment,
                         unsigned int format)
{
    TRACE_ENTER();

    return gsgpu_dri2_create_buffer2(drawable->pScreen,
                                     drawable,
                                     attachment,
                                     format);

    TRACE_EXIT();
}

static void gsgpu_dri2_reference_buffer(DRI2Buffer2Ptr buffer)
{
    if (buffer)
    {
        struct gsgpu_dri2_buffer_private *private = buffer->driverPrivate;
        private->refcnt++;
    }
}

static void gsgpu_dri2_destroy_buffer2(ScreenPtr pScreen,
                                       DrawablePtr drawable,
                                       DRI2Buffer2Ptr buffer)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

    TRACE_ENTER();

    if (!buffer)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "gsgpu: dri2: Attempted to destroy NULL buffer\n");
        return;
    }

    if (buffer->driverPrivate)
    {
        struct gsgpu_dri2_buffer_private *private = buffer->driverPrivate;
        if (--private->refcnt == 0)
        {
             if (private->pixmap)
                pScreen->DestroyPixmap(private->pixmap);

            free(private);
            free(buffer);
        }
    }
    else
    {
        free(buffer);
    }

    TRACE_EXIT();
}

static void gsgpu_dri2_destroy_buffer(DrawablePtr drawable,
                                      DRI2Buffer2Ptr buffer)
{
    TRACE_ENTER();

    gsgpu_dri2_destroy_buffer2(drawable->pScreen, drawable, buffer);

    TRACE_EXIT();
}

static void
gsgpu_dri2_copy_region2(ScreenPtr screen,
                        DrawablePtr drawable,
                        RegionPtr pRegion,
                        DRI2BufferPtr destBuffer,
                        DRI2BufferPtr sourceBuffer)
{
    struct gsgpu_dri2_buffer_private *src_priv = sourceBuffer->driverPrivate;
    struct gsgpu_dri2_buffer_private *dst_priv = destBuffer->driverPrivate;
    PixmapPtr src_pixmap = src_priv->pixmap;
    PixmapPtr dst_pixmap = dst_priv->pixmap;
    DrawablePtr src = (sourceBuffer->attachment == DRI2BufferFrontLeft)
        ? drawable : &src_pixmap->drawable;
    DrawablePtr dst = (destBuffer->attachment == DRI2BufferFrontLeft)
        ? drawable : &dst_pixmap->drawable;
    int off_x = 0, off_y = 0;
    Bool translate = FALSE;
    RegionPtr pCopyClip;
    GCPtr gc;

    if (destBuffer->attachment == DRI2BufferFrontLeft &&
        drawable->pScreen != screen)
    {
        dst = DRI2UpdatePrime(drawable, destBuffer);
        if (!dst)
            return;
        if (dst != drawable)
            translate = TRUE;
    }

    if (translate && drawable->type == DRAWABLE_WINDOW)
    {
#ifdef COMPOSITE
        PixmapPtr pixmap = get_drawable_pixmap(drawable);
        off_x = -pixmap->screen_x;
        off_y = -pixmap->screen_y;
#endif
        off_x += drawable->x;
        off_y += drawable->y;
    }

    gc = GetScratchGC(dst->depth, screen);
    if (!gc)
        return;

    pCopyClip = REGION_CREATE(screen, NULL, 0);
    REGION_COPY(screen, pCopyClip, pRegion);
    if (translate)
        REGION_TRANSLATE(screen, pCopyClip, off_x, off_y);
    (*gc->funcs->ChangeClip) (gc, CT_REGION, pCopyClip, 0);
    ValidateGC(dst, gc);

    /* It's important that this copy gets submitted before the direct
     * rendering client submits rendering for the next frame, but we
     * don't actually need to submit right now.  The client will wait
     * for the DRI2CopyRegion reply or the swap buffer event before
     * rendering, and we'll hit the flush callback chain before those
     * messages are sent.  We submit our batch buffers from the flush
     * callback chain so we know that will happen before the client
     * tries to render again.
     */
    gc->ops->CopyArea(src, dst, gc,
                      0, 0,
                      drawable->width, drawable->height,
                      off_x, off_y);

    FreeScratchGC(gc);
}

static void
gsgpu_dri2_copy_region(DrawablePtr drawable,
                       RegionPtr pRegion,
                       DRI2BufferPtr destBuffer,
                       DRI2BufferPtr sourceBuffer)
{
    TRACE_ENTER();

    gsgpu_dri2_copy_region2(drawable->pScreen,
                            drawable,
                            pRegion,
                            destBuffer,
                            sourceBuffer);

    TRACE_EXIT();
}

static uint64_t gettime_us(void)
{
    struct timespec tv;

    if (clock_gettime(CLOCK_MONOTONIC, &tv))
        return 0;

    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_nsec / 1000;
}

/**
 * Get current frame count and frame count timestamp,
 * based on drawable's CRTC.
 */
static int gsgpu_dri2_get_msc(DrawablePtr draw, CARD64 *ust, CARD64 *msc)
{
    xf86CrtcPtr crtc = ms_dri2_crtc_covering_drawable(draw);
    int ret;

    /* Drawable not displayed, make up a *monotonic* value */
    if (crtc == NULL)
    {
        *ust = gettime_us();
        *msc = 0;
        return TRUE;
    }

    ret = ms_get_crtc_ust_msc(crtc, ust, msc);
    if (ret)
        return FALSE;

    return TRUE;
}

static XID gsgpu_get_client_id(ClientPtr client)
{
    XID *ptr = dixGetPrivateAddr(&client->devPrivates, &gsgpu_dri2_client_key);
    if (*ptr == 0)
        *ptr = FakeClientID(client->index);

    return *ptr;
}

/*
 * Hook this frame event into the server resource database
 * so we can clean it up if the drawable or client exits
 * while the swap is pending
 */
static Bool gsgpu_dri2_add_frame_event(struct gsgpu_dri2_frame_event *info)
{
    struct gsgpu_dri2_resource *resource;

    resource = gsgpu_get_resource(gsgpu_get_client_id(info->client),
                                  frame_event_client_type);
    if (resource == NULL)
        return FALSE;

    xorg_list_add(&info->client_resource, &resource->list);

    resource = gsgpu_get_resource(info->drawable->id,
                                  frame_event_drawable_type);
    if (resource == NULL)
    {
        xorg_list_del(&info->client_resource);
        return FALSE;
    }

    xorg_list_add(&info->drawable_resource, &resource->list);

    return TRUE;
}

static void gsgpu_dri2_del_frame_event(struct gsgpu_dri2_frame_event *info)
{
    xorg_list_del(&info->client_resource);
    xorg_list_del(&info->drawable_resource);

    if (info->front)
        gsgpu_dri2_destroy_buffer(NULL, info->front);
    if (info->back)
        gsgpu_dri2_destroy_buffer(NULL, info->back);

    free(info);
}

static void gsgpu_dri2_blit_swap(DrawablePtr drawable,
                                 DRI2BufferPtr dst,
                                 DRI2BufferPtr src)
{
    BoxRec box;
    RegionRec region;

    box.x1 = 0;
    box.y1 = 0;
    box.x2 = drawable->width;
    box.y2 = drawable->height;
    REGION_INIT(pScreen, &region, &box, 0);

    gsgpu_dri2_copy_region(drawable, &region, dst, src);
}

struct gsgpu_dri2_vblank_event {
    XID drawable_id;
    ClientPtr client;
    DRI2SwapEventPtr event_complete;
    void *event_data;
};

static void gsgpu_dri2_flip_abort(loongsonPtr lsp, void *data)
{
    struct ms_present_vblank_event *event = data;
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;

    pDrmMode->dri2_flipping = FALSE;
    free(event);
}


static void gsgpu_dri2_flip_handler(loongsonPtr lsp,
                                    uint64_t msc,
                                    uint64_t ust,
                                    void *data)
{
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct gsgpu_dri2_vblank_event *event = data;
    uint32_t frame = msc;
    uint32_t tv_sec = ust / 1000000;
    uint32_t tv_usec = ust % 1000000;
    DrawablePtr drawable;
    int status;

    status = dixLookupDrawable(&drawable,
                               event->drawable_id,
                               serverClient,
                               M_ANY,
                               DixWriteAccess);
    if (status == Success)
        DRI2SwapComplete(event->client, drawable, frame, tv_sec, tv_usec,
                         DRI2_FLIP_COMPLETE, event->event_complete,
                         event->event_data);

    pDrmMode->dri2_flipping = FALSE;
    free(event);
}

static Bool gsgpu_dri2_schedule_flip(struct gsgpu_dri2_frame_event * info)
{
    DrawablePtr draw = info->drawable;
    ScreenPtr screen = draw->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    loongsonPtr lsp = loongsonPTR(scrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct gsgpu_dri2_buffer_private * back_priv = info->back->driverPrivate;
    struct gsgpu_dri2_vblank_event *event;
    drmmode_crtc_private_ptr drmmode_crtc = info->crtc->driver_private;

    event = calloc(1, sizeof(struct gsgpu_dri2_vblank_event));
    if (!event)
        return FALSE;

    event->drawable_id = draw->id;
    event->client = info->client;
    event->event_complete = info->event_complete;
    event->event_data = info->event_data;

    if (ms_do_pageflip(screen, back_priv->pixmap, event,
                       drmmode_crtc->vblank_pipe, FALSE,
                       gsgpu_dri2_flip_handler,
                       gsgpu_dri2_flip_abort,
                       "DRI2-flip"))
    {
        pDrmMode->dri2_flipping = TRUE;
        return TRUE;
    }

    return FALSE;
}

static Bool gsgpu_update_front(DrawablePtr draw, DRI2BufferPtr front)
{
    ScreenPtr pScreen = draw->pScreen;
    PixmapPtr pPixmap = get_drawable_pixmap(draw);
    struct gsgpu_dri2_buffer_private * priv = front->driverPrivate;
    loongsonPtr lsp = loongsonPTR(xf86ScreenToScrn(pScreen));

    if (!gsgpu_get_flink_name(lsp->fd, pPixmap, &front->name))
    {
        xf86Msg(X_ERROR, "update front: Failed to get DRI2 flink name\n");
        return FALSE;
    }

    (*pScreen->DestroyPixmap) (priv->pixmap);
    front->pitch = pPixmap->devKind;
    front->cpp = pPixmap->drawable.bitsPerPixel / 8;
    priv->pixmap = pPixmap;
    pPixmap->refcnt++;

    return TRUE;
}

static Bool can_exchange(ScrnInfoPtr scrn,
                         DrawablePtr draw,
                         DRI2BufferPtr front,
                         DRI2BufferPtr back)
{
    struct gsgpu_dri2_buffer_private * front_priv = front->driverPrivate;
    struct gsgpu_dri2_buffer_private * back_priv = back->driverPrivate;
    PixmapPtr front_pixmap;
    PixmapPtr back_pixmap = back_priv->pixmap;
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(scrn);
    int num_crtcs_on = 0;
    int i;

    for (i = 0; i < config->num_crtc; i++)
    {
        drmmode_crtc_private_ptr drmmode_crtc = config->crtc[i]->driver_private;

        /* Don't do pageflipping if CRTCs are rotated. */
#ifdef GLAMOR_HAS_GBM
        if (drmmode_crtc->rotate_bo->gbm)
            return FALSE;
#endif

        if (ls_is_crtc_on(config->crtc[i]))
            num_crtcs_on++;
    }

    /* We can't do pageflipping if all the CRTCs are off. */
    if (num_crtcs_on == 0)
        return FALSE;

    if (!gsgpu_update_front(draw, front))
        return FALSE;

    front_pixmap = front_priv->pixmap;

    if (front_pixmap->drawable.width != back_pixmap->drawable.width)
        return FALSE;

    if (front_pixmap->drawable.height != back_pixmap->drawable.height)
        return FALSE;

    if (front_pixmap->drawable.bitsPerPixel != back_pixmap->drawable.bitsPerPixel)
        return FALSE;

    if (front_pixmap->devKind != back_pixmap->devKind)
        return FALSE;

    return TRUE;
}

static Bool can_flip(ScrnInfoPtr pScrn, DrawablePtr draw,
                     DRI2BufferPtr front, DRI2BufferPtr back)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;

    return (draw->type == DRAWABLE_WINDOW) &&
           (pDrmMode->pageflip == TRUE) &&
           (pDrmMode->sprites_visible == 0) &&
           (pDrmMode->present_flipping == FALSE) &&
           (pScrn->vtSema == TRUE) &&
           DRI2CanFlip(draw) &&
           can_exchange(pScrn, draw, front, back);
}

static void gsgpu_dri2_exchange_buffers(DrawablePtr draw,
                                        DRI2BufferPtr front,
                                        DRI2BufferPtr back)
{
    struct gsgpu_dri2_buffer_private *front_priv = front->driverPrivate;
    struct gsgpu_dri2_buffer_private *back_priv = back->driverPrivate;
    ScreenPtr screen = draw->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    loongsonPtr ms = loongsonPTR(scrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;
    msPixmapPrivPtr front_pix = msGetPixmapPriv(pDrmMode, front_priv->pixmap);
    msPixmapPrivPtr back_pix = msGetPixmapPriv(pDrmMode, back_priv->pixmap);
    msPixmapPrivRec tmp_pix;
    RegionRec region;
    int tmp;

    /* Swap BO names so DRI works */
    tmp = front->name;
    front->name = back->name;
    back->name = tmp;

    /* Swap pixmap privates */
    tmp_pix = *front_pix;
    *front_pix = *back_pix;
    *back_pix = tmp_pix;

    if (pDrmMode->glamor_enabled)
    {
        ms->glamor.egl_exchange_buffers(front_priv->pixmap, back_priv->pixmap);
    }
    else if (pDrmMode->exa_enabled)
    {
        ms_exa_exchange_buffers(front_priv->pixmap, back_priv->pixmap);
    }

    /* Post damage on the front buffer so that listeners, such
     * as DisplayLink know take a copy and shove it over the USB.
     */
    region.extents.x1 = region.extents.y1 = 0;
    region.extents.x2 = front_priv->pixmap->drawable.width;
    region.extents.y2 = front_priv->pixmap->drawable.height;
    region.data = NULL;
    DamageRegionAppend(&front_priv->pixmap->drawable, &region);
    DamageRegionProcessPending(&front_priv->pixmap->drawable);
}

static void gsgpu_dri2_frame_event_handler(uint64_t msc,
                                           uint64_t usec,
                                           void *data)
{
    struct gsgpu_dri2_frame_event * frame_info = data;
    DrawablePtr drawable = frame_info->drawable;
    ScreenPtr screen = frame_info->screen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    uint32_t tv_sec = usec / 1000000;
    uint32_t tv_usec = usec % 1000000;

    if (!drawable)
    {
        gsgpu_dri2_del_frame_event(frame_info);
        return;
    }

    switch (frame_info->type)
    {
    case MS_DRI2_QUEUE_FLIP:
        if (can_flip(scrn, drawable, frame_info->front, frame_info->back) &&
            gsgpu_dri2_schedule_flip(frame_info))
        {
            gsgpu_dri2_exchange_buffers(drawable,
                                        frame_info->front,
                                        frame_info->back);
            break;
        }
        /* else fall through to blit */
    case MS_DRI2_QUEUE_SWAP:
        gsgpu_dri2_blit_swap(drawable, frame_info->front, frame_info->back);
        DRI2SwapComplete(frame_info->client,
                         drawable, msc, tv_sec, tv_usec,
                         DRI2_BLIT_COMPLETE,
                         frame_info->client ? frame_info->event_complete : NULL,
                         frame_info->event_data);
        break;

    case MS_DRI2_WAIT_MSC:
        if (frame_info->client)
            DRI2WaitMSCComplete(frame_info->client, drawable,
                                msc, tv_sec, tv_usec);
        break;

    default:
        xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                   "%s: unknown vblank event (type %d) received\n", __func__,
                   frame_info->type);
        break;
    }

    gsgpu_dri2_del_frame_event(frame_info);
}

static void gsgpu_dri2_frame_event_abort(void *data)
{
    struct gsgpu_dri2_frame_event *frame_info = data;

    gsgpu_dri2_del_frame_event(frame_info);
}

/**
 * Request a DRM event when the requested conditions will be satisfied.
 *
 * We need to handle the event and ask the server to wake up the client when
 * we receive it.
 */
static int gsgpu_dri2_schedule_wait_msc(ClientPtr client,
                                        DrawablePtr draw,
                                        CARD64 target_msc,
                                        CARD64 divisor,
                                        CARD64 remainder)
{
    ScreenPtr screen = draw->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    xf86CrtcPtr crtc = ms_dri2_crtc_covering_drawable(draw);
    CARD64 current_msc, current_ust, request_msc;
    struct gsgpu_dri2_frame_event * wait_info;
    int ret;
    uint32_t seq;
    uint64_t queued_msc;

    /* Drawable not visible, return immediately */
    if (!crtc)
        goto out_complete;

    wait_info = calloc(1, sizeof(*wait_info));
    if (!wait_info)
        goto out_complete;

    wait_info->screen = screen;
    wait_info->drawable = draw;
    wait_info->client = client;
    wait_info->type = MS_DRI2_WAIT_MSC;

    if (!gsgpu_dri2_add_frame_event(wait_info))
    {
        free(wait_info);
        wait_info = NULL;
        goto out_complete;
    }

    /* Get current count */
    ret = ms_get_crtc_ust_msc(crtc, &current_ust, &current_msc);

    /*
     * If divisor is zero, or current_msc is smaller than target_msc,
     * we just need to make sure target_msc passes  before waking up the
     * client.
     */
    if (divisor == 0 || current_msc < target_msc)
    {
        /* If target_msc already reached or passed, set it to
         * current_msc to ensure we return a reasonable value back
         * to the caller. This keeps the client from continually
         * sending us MSC targets from the past by forcibly updating
         * their count on this call.
         */
        seq = ms_drm_queue_alloc(crtc,
                                 wait_info,
                                 gsgpu_dri2_frame_event_handler,
                                 gsgpu_dri2_frame_event_abort);
        if (!seq)
            goto out_free;

        if (current_msc >= target_msc)
            target_msc = current_msc;

        ret = ms_queue_vblank(crtc, MS_QUEUE_ABSOLUTE, target_msc, &queued_msc, seq);
        if (!ret)
        {
            static int limit = 5;
            if (limit) {
                xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                           "%s:%d get vblank counter failed: %s\n",
                           __FUNCTION__, __LINE__,
                           strerror(errno));
                limit--;
            }
            goto out_free;
        }

        wait_info->frame = queued_msc;
        DRI2BlockClient(client, draw);
        return TRUE;
    }

    /*
     * If we get here, target_msc has already passed or we don't have one,
     * so we queue an event that will satisfy the divisor/remainder equation.
     */
    request_msc = current_msc - (current_msc % divisor) +
        remainder;
    /*
     * If calculated remainder is larger than requested remainder,
     * it means we've passed the last point where
     * seq % divisor == remainder, so we need to wait for the next time
     * that will happen.
     */
    if ((current_msc % divisor) >= remainder)
        request_msc += divisor;

    seq = ms_drm_queue_alloc(crtc,
                             wait_info,
                             gsgpu_dri2_frame_event_handler,
                             gsgpu_dri2_frame_event_abort);
    if (!seq)
        goto out_free;

    if (!ms_queue_vblank(crtc, MS_QUEUE_ABSOLUTE, request_msc, &queued_msc, seq))
    {
        static int limit = 5;
        if (limit)
        {
            xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                       "%s:%d get vblank counter failed: %s\n",
                       __FUNCTION__, __LINE__,
                       strerror(errno));
            limit--;
        }
        goto out_free;
    }

    wait_info->frame = queued_msc;

    DRI2BlockClient(client, draw);

    return TRUE;

 out_free:
    gsgpu_dri2_del_frame_event(wait_info);
 out_complete:
    DRI2WaitMSCComplete(client, draw, target_msc, 0, 0);
    return TRUE;
}

/**
 * ScheduleSwap is responsible for requesting a DRM vblank event for
 * the appropriate frame, or executing the swap immediately if it
 * doesn't need to wait.
 *
 * When the swap is complete, the driver should call into the server so it
 * can send any swap complete events that have been requested.
 */
static int
gsgpu_dri2_schedule_swap(ClientPtr client,
                         DrawablePtr draw,
                         DRI2BufferPtr front,
                         DRI2BufferPtr back,
                         CARD64 *target_msc,
                         CARD64 divisor,
                         CARD64 remainder,
                         DRI2SwapEventPtr func,
                         void *data)
{
    ScreenPtr screen = draw->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    int ret, flip = 0;
    xf86CrtcPtr crtc = ms_dri2_crtc_covering_drawable(draw);
    struct gsgpu_dri2_frame_event * frame_info = NULL;
    uint64_t current_msc, current_ust;
    uint64_t request_msc;
    uint32_t seq;
    ms_queue_flag ms_flag = MS_QUEUE_ABSOLUTE;
    uint64_t queued_msc;

    /* Drawable not displayed... just complete the swap */
    if (!crtc)
        goto blit_fallback;

    TRACE_ENTER();

    frame_info = calloc(1, sizeof(*frame_info));
    if (!frame_info)
        goto blit_fallback;

    frame_info->screen = screen;
    frame_info->drawable = draw;
    frame_info->client = client;
    frame_info->event_complete = func;
    frame_info->event_data = data;
    frame_info->front = front;
    frame_info->back = back;
    frame_info->crtc = crtc;
    frame_info->type = MS_DRI2_QUEUE_SWAP;

    if (!gsgpu_dri2_add_frame_event(frame_info))
    {
        free(frame_info);
        frame_info = NULL;
        goto blit_fallback;
    }

    gsgpu_dri2_reference_buffer(front);
    gsgpu_dri2_reference_buffer(back);

    ret = ms_get_crtc_ust_msc(crtc, &current_ust, &current_msc);
    if (ret != Success)
        goto blit_fallback;

    /* Flips need to be submitted one frame before */
    if (can_flip(scrn, draw, front, back)) {
        frame_info->type = MS_DRI2_QUEUE_FLIP;
        flip = 1;
    }

    /* Correct target_msc by 'flip' if frame_info->type == MS_DRI2_QUEUE_FLIP.
     * Do it early, so handling of different timing constraints
     * for divisor, remainder and msc vs. target_msc works.
     */
    if (*target_msc > 0)
        *target_msc -= flip;

    /* If non-pageflipping, but blitting/exchanging, we need to use
     * DRM_VBLANK_NEXTONMISS to avoid unreliable timestamping later
     * on.
     */
    if (flip == 0)
        ms_flag |= MS_QUEUE_NEXT_ON_MISS;

    /*
     * If divisor is zero, or current_msc is smaller than target_msc
     * we just need to make sure target_msc passes before initiating
     * the swap.
     */
    if (divisor == 0 || current_msc < *target_msc)
    {

        /* If target_msc already reached or passed, set it to
         * current_msc to ensure we return a reasonable value back
         * to the caller. This makes swap_interval logic more robust.
         */
        if (current_msc >= *target_msc)
            *target_msc = current_msc;

        seq = ms_drm_queue_alloc(crtc, frame_info,
                                 gsgpu_dri2_frame_event_handler,
                                 gsgpu_dri2_frame_event_abort);
        if (!seq)
            goto blit_fallback;

        if (!ms_queue_vblank(crtc, ms_flag, *target_msc, &queued_msc, seq))
        {
            xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                       "divisor 0 get vblank counter failed: %s\n",
                       strerror(errno));
            goto blit_fallback;
        }

        *target_msc = queued_msc + flip;
        frame_info->frame = *target_msc;

        return TRUE;
    }

    /*
     * If we get here, target_msc has already passed or we don't have one,
     * and we need to queue an event that will satisfy the divisor/remainder
     * equation.
     */

    request_msc = current_msc - (current_msc % divisor) +
        remainder;

    /*
     * If the calculated deadline vbl.request.sequence is smaller than
     * or equal to current_msc, it means we've passed the last point
     * when effective onset frame seq could satisfy
     * seq % divisor == remainder, so we need to wait for the next time
     * this will happen.

     * This comparison takes the DRM_VBLANK_NEXTONMISS delay into account.
     */
    if (request_msc <= current_msc)
        request_msc += divisor;

    seq = ms_drm_queue_alloc(crtc,
                             frame_info,
                             gsgpu_dri2_frame_event_handler,
                             gsgpu_dri2_frame_event_abort);
    if (!seq)
        goto blit_fallback;

    /* Account for 1 frame extra pageflip delay if flip > 0 */
    if (!ms_queue_vblank(crtc, ms_flag, request_msc - flip, &queued_msc, seq))
    {
        xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                   "final get vblank counter failed: %s\n",
                   strerror(errno));
        goto blit_fallback;
    }

    /* Adjust returned value for 1 fame pageflip offset of flip > 0 */
    *target_msc = queued_msc + flip;
    frame_info->frame = *target_msc;

    TRACE_EXIT();

    return TRUE;

 blit_fallback:
    gsgpu_dri2_blit_swap(draw, front, back);
    DRI2SwapComplete(client, draw, 0, 0, 0, DRI2_BLIT_COMPLETE, func, data);
    if (frame_info)
        gsgpu_dri2_del_frame_event(frame_info);
    *target_msc = 0; /* offscreen, so zero out target vblank count */

    return TRUE;
}

static int gsgpu_dri2_frame_event_client_gone(void *data, XID id)
{
    struct gsgpu_dri2_resource *resource = data;

    while (!xorg_list_is_empty(&resource->list))
    {
        struct gsgpu_dri2_frame_event *info =
            xorg_list_first_entry(&resource->list,
                                  struct gsgpu_dri2_frame_event ,
                                  client_resource);

        xorg_list_del(&info->client_resource);
        info->client = NULL;
    }
    free(resource);

    return Success;
}

static int gsgpu_dri2_frame_event_drawable_gone(void *data, XID id)
{
    struct gsgpu_dri2_resource *resource = data;

    while (!xorg_list_is_empty(&resource->list))
    {
        struct gsgpu_dri2_frame_event *info =
            xorg_list_first_entry(&resource->list,
                                  struct gsgpu_dri2_frame_event,
                                  drawable_resource);

        xorg_list_del(&info->drawable_resource);
        info->drawable = NULL;
    }
    free(resource);

    return Success;
}

static Bool gsgpu_dri2_register_frame_event_resource_types(void)
{
    frame_event_client_type =
        CreateNewResourceType(gsgpu_dri2_frame_event_client_gone,
                              "Frame Event Client");
    if (!frame_event_client_type)
        return FALSE;

    frame_event_drawable_type =
        CreateNewResourceType(gsgpu_dri2_frame_event_drawable_gone,
                              "Frame Event Drawable");
    if (!frame_event_drawable_type)
        return FALSE;

    return TRUE;
}


Bool gsgpu_dri2_screen_init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    const char *driver_names[2] = {"gsgpu", "gsgpu"};
    int minor = 0;
    int major = 0;
    Bool ret;
    DRI2InfoRec info;

    if (!xf86LoaderCheckSymbol("DRI2Version"))
        return FALSE;

    DRI2Version(&major, &minor);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "DRI2Version: major=%d, minor=%d\n", major, minor);

    if (!dixRegisterPrivateKey(&gsgpu_dri2_client_key,
                               PRIVATE_CLIENT,
                               sizeof(XID)))
    {
        return FALSE;
    }

    if (serverGeneration != gsgpu_dri2_server_generation)
    {
        gsgpu_dri2_server_generation = serverGeneration;
        if (!gsgpu_dri2_register_frame_event_resource_types())
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "Cannot register DRI2 frame event resources\n");
            return FALSE;
        }
    }

    memset(&info, '\0', sizeof(info));
    info.version = 9;
    info.fd = lsp->fd;
    info.driverName = "gsgpu";
    info.deviceName = drmGetDeviceNameFromFd2(lsp->fd);

    info.CreateBuffer = gsgpu_dri2_create_buffer;
    info.DestroyBuffer = gsgpu_dri2_destroy_buffer;
    info.CopyRegion = gsgpu_dri2_copy_region;
    info.Wait = NULL;

    /* added in version 4 */
    info.ScheduleSwap = gsgpu_dri2_schedule_swap;
    info.GetMSC = gsgpu_dri2_get_msc;
    info.ScheduleWaitMSC = gsgpu_dri2_schedule_wait_msc;

    info.numDrivers = 2;
    info.driverNames = driver_names;

    /* added in version 5 */
    info.AuthMagic = drmAuthMagic,
    /* added in version 6 */
    info.CreateBuffer2 = gsgpu_dri2_create_buffer2;
    info.DestroyBuffer2 = gsgpu_dri2_destroy_buffer2;
    info.CopyRegion2 = gsgpu_dri2_copy_region2;

    ret = DRI2ScreenInit(pScreen, &info);

    if (ret)
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "DRI2 initialized\n");

    return ret;
}

void gsgpu_dri2_close_screen(ScreenPtr screen)
{
    DRI2CloseScreen(screen);
}
