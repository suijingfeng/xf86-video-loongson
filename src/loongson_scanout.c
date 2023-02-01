/*
 * Copyright (C) 2020 Loongson Corporation
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <malloc.h>

#include "dumb_bo.h"
#include "driver.h"
#include "loongson_scanout.h"
#include "loongson_pixmap.h"
#include "loongson_glamor.h"

#ifdef HAVE_LIBDRM_GSGPU
#include <gsgpu_drm.h>
#include <gsgpu.h>
#endif
/*
 * Front bo(scanout buffer) create with this function is a dumb,
 * not a gbm bo, we want it clear
 */
struct DrmModeBO *
LS_CreateFrontBO(ScrnInfoPtr pScrn,
                 int drm_fd,
                 int width,
                 int height,
                 int bpp)
{
    struct DrmModeBO *pFront;
    struct dumb_bo *dumb;

    pFront = calloc(1, sizeof(struct DrmModeBO));
    if (!pFront)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s: no memmory\n", __func__);
        return NULL;
    }

    dumb = dumb_bo_create(drm_fd, width, height, bpp);
    if (!dumb)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s: create dumb BO(%dx%d, bpp=%d) failed\n",
                   __func__, width, height, bpp);
        return NULL;
    }

    pFront->dumb = dumb;
    pFront->width = width;
    pFront->height = height;
    pFront->pitch = dumb_bo_pitch(dumb);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "%s: New Front BO (%dx%d, bpp=%d, pitch=%d) created\n",
               __func__, width, height, bpp, pFront->pitch);

    return pFront;
}

void LS_FreeFrontBO(ScrnInfoPtr pScrn,
                    int drm_fd,
                    uint32_t fb_id,
                    struct DrmModeBO *pFB)
{
    int ret;

    if (fb_id)
    {
        drmModeRmFB(drm_fd, fb_id);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "Front FB(fb_id = %d) get removed\n", fb_id);
    }

    if (!pFB)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Null FB\n");
        return;
    }

    // suijingfeng : check is not necessary ...
    // for loongson-drm, we are sure that the front bo is just a dumb ...
    if (pFB->dumb)
    {
        ret = dumb_bo_destroy(drm_fd, pFB->dumb);
        if (ret == 0)
        {
            pFB->dumb = NULL;
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Front Dumb BO get freed\n");
        }
    }

    free(pFB);
}

void *LS_MapFrontBO(ScrnInfoPtr pScrn,
                    int drm_fd,
                    struct DrmModeBO *pFrontBO)
{
    int ret;

    ret = dumb_bo_map(drm_fd, pFrontBO->dumb);
    if (ret)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s: Failed map front BO: %d.\n", __func__, ret);
        return NULL;
    }

    return dumb_bo_cpu_addr(pFrontBO->dumb);
}


/*
 * Return TRUE if success
 */
Bool loongson_crtc_get_fb_id(xf86CrtcPtr crtc,
                             uint32_t *fb_id,
                             int *x,
                             int *y)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr pDrmMode = drmmode_crtc->drmmode;
    PixmapPtr prime_pixmap_ptr = drmmode_crtc->prime_pixmap;
    int ret;

    *fb_id = 0;

    if (prime_pixmap_ptr)
    {
        xf86Msg(X_INFO, "%s: prime_pixmap=%p\n", __func__, prime_pixmap_ptr);

        if (1 /* !drmmode->reverse_prime_offload_mode */)
        {
            msPixmapPrivPtr ppriv = msGetPixmapPriv(pDrmMode, prime_pixmap_ptr);
            *fb_id = ppriv->fb_id;
            *x = 0;
        } else
            *x = drmmode_crtc->prime_pixmap_x;
        *y = 0;
    }
    else if (drmmode_crtc->rotate_fb_id)
    {
        *fb_id = drmmode_crtc->rotate_fb_id;
        *x = *y = 0;
        xf86Msg(X_INFO, "%s: rotate_fb_id=%d\n", __func__,
                drmmode_crtc->rotate_fb_id);
    }
    else
    {
        *fb_id = pDrmMode->fb_id;
        *x = crtc->x;
        *y = crtc->y;

        xf86Msg(X_INFO, "%s: %d: fb_id=%d\n",
                __func__, __LINE__, pDrmMode->fb_id);
    }

    if (*fb_id == 0)
    {
        xf86Msg(X_INFO, "%s: Front bo haven't been scanout, scanout it now\n",
                __func__);

        ret = drmmode_bo_import(pDrmMode,
                                pDrmMode->front_bo,
                                &pDrmMode->fb_id);
        if (ret < 0)
        {
            xf86Msg(X_ERROR, "failed to add fb %d\n", ret);
            return FALSE;
        }
        *fb_id = pDrmMode->fb_id;
    }

    return TRUE;
}

static struct drmmode_fb *loongson_pixmap_get_fb_ptr(PixmapPtr pPix)
{
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPix);

    if (priv)
        return priv->fb;

    return NULL;
}

static Bool loongson_pixmap_set_fb_ptr(PixmapPtr pPix, struct drmmode_fb *fb)
{
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPix);

    if (!priv)
    {
        return FALSE;
    }

    priv->fb = fb;

    return TRUE;
}

static Bool loongson_pixmap_get_handle(PixmapPtr pPixmap, uint32_t *pHandle)
{
    struct exa_pixmap_priv *priv;

    priv = exaGetPixmapDriverPrivate(pPixmap);
    if (!priv)
    {
        return FALSE;
    }

    if (priv->bo)
    {
        *pHandle = dumb_bo_handle(priv->bo);
        return TRUE;
    }

#ifdef HAVE_LIBDRM_GSGPU
    if (priv->gbo)
    {
        gsgpu_bo_export(priv->gbo, gsgpu_bo_handle_type_kms, pHandle);
        return TRUE;
    }
#endif

    return FALSE;
}

static struct drmmode_fb *
loongson_fb_create(ScrnInfoPtr pScrn,
                   int drm_fd,
                   uint32_t width,
                   uint32_t height,
                   uint32_t pitch,
                   uint32_t handle)
{
    struct drmmode_fb *fb = malloc(sizeof(*fb));
    int ret;

    if (!fb)
        return NULL;

    fb->refcnt = 1;

    ret = drmModeAddFB(drm_fd, width, height,
                       pScrn->depth, pScrn->bitsPerPixel,
                       pitch, handle, &fb->id);

    if (ret)
    {
        free(fb);

        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s Failed at %d, return %d\n", __func__, __LINE__, ret);
        return NULL;
    }

    return fb;
}

Bool loongson_pixmap_get_fb_id(PixmapPtr pPixmap, uint32_t *fb_id)
{
    uint32_t handle;
    struct drmmode_fb *fb;
    Bool ret = TRUE;

    fb = loongson_pixmap_get_fb_ptr(pPixmap);
    if (fb)
    {
        *fb_id = fb->id;
        return ret;
    }

    xf86Msg(X_INFO, "%s: don't have fb attach to pixmap(%p), create one\n",
            __func__, pPixmap);

    if (loongson_pixmap_get_handle(pPixmap, &handle))
    {
        ScreenPtr pScreen = pPixmap->drawable.pScreen;
        ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
        loongsonPtr lsp = loongsonPTR(pScrn);

        fb = loongson_fb_create(pScrn,
                                lsp->fd,
                                pPixmap->drawable.width,
                                pPixmap->drawable.height,
                                pPixmap->devKind,
                                handle);

        /* After the fb have been scanout, feed the fb_id to the caller */
        *fb_id = fb->id;

        ret = loongson_pixmap_set_fb_ptr(pPixmap, fb);
    }

    return ret;
}


static void loongson_crtc_scanout_destroy(PixmapPtr *ppScanout)
{
    PixmapPtr pScanout = *ppScanout;
    if (!pScanout)
        return;

    pScanout->drawable.pScreen->DestroyPixmap(pScanout);
    pScanout = NULL;
}

Bool loongson_create_scanout_pixmap(ScrnInfoPtr pScrn,
                                    int width,
                                    int height,
                                    PixmapPtr *ppScanoutPix)
{
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    PixmapPtr pScanout = *ppScanoutPix;

    if (pScanout)
    {
        if (pScanout->drawable.width == width &&
            pScanout->drawable.height == height)
            return TRUE;

        loongson_crtc_scanout_destroy(ppScanoutPix);
    }

    pScanout = pScreen->CreatePixmap(pScreen,
                                     width,
                                     height,
                                     pScrn->depth,
                                     CREATE_PIXMAP_USAGE_SCANOUT);
    if (!pScanout)
    {
        ErrorF("failed to create CRTC scanout pixmap\n");
        return FALSE;
    }

    *ppScanoutPix = pScanout;

    return TRUE;
}

int drmmode_bo_import(drmmode_ptr drmmode, drmmode_bo *bo, uint32_t *fb_id)
{
    ScrnInfoPtr pScrn = drmmode->scrn;
    loongsonPtr lsp = loongsonPTR(pScrn);
    uint32_t kms_handle;
    uint32_t pitch;

#ifdef GLAMOR_HAS_GBM
    if (bo->gbm && lsp->kms_has_modifiers)
    {
        ls_glamor_bo_import(drmmode, bo, fb_id);
    }
#endif

    kms_handle = drmmode_bo_get_handle(bo);
    pitch = drmmode_bo_get_pitch(bo);

    if (bo->dumb)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: Add DUMB BO(handle=%u): %dx%d, pitch:%u cpu addr: %p\n",
                   __func__, kms_handle, bo->width, bo->height, pitch,
                   dumb_bo_cpu_addr(bo->dumb));
    }

    return drmModeAddFB(drmmode->fd, bo->width, bo->height,
                        pScrn->depth, drmmode->kbpp,
                        pitch, kms_handle, fb_id);
}


uint32_t drmmode_bo_get_pitch(struct DrmModeBO * const pBO)
{
#ifdef GLAMOR_HAS_GBM
    if (pBO->gbm)
        return gbm_bo_get_stride(pBO->gbm);
#endif

    if (pBO->dumb)
        return dumb_bo_pitch(pBO->dumb);

    if (pBO->gbo)
        return pBO->pitch;

    xf86Msg(X_ERROR, "%s: drmmode_bo don't have a valid pitch\n", __func__);

    return -1;
}

int drmmode_bo_destroy(struct drmmode_rec * const pDrmMode,
                       struct DrmModeBO * const pBO)
{
    int ret;

#ifdef GLAMOR_HAS_GBM
    if (pBO->gbm)
    {
        gbm_bo_destroy(pBO->gbm);
        pBO->gbm = NULL;
    }
#endif

    if (pBO->dumb)
    {
        ret = dumb_bo_destroy(pDrmMode->fd, pBO->dumb);
        if (ret == 0)
            pBO->dumb = NULL;
    }

    return 0;
}
