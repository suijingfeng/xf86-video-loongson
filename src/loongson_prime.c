/*
 * Copyright © 2020 Loongson Corporation
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

#include <unistd.h>
#include <malloc.h>

#include <xf86str.h>
#include <xf86Crtc.h>
#include <xf86drm.h>

#include "driver.h"
#include "dumb_bo.h"
#include "drmmode_display.h"
#include "loongson_prime.h"
#include "loongson_exa.h"

/* OUTPUT SLAVE SUPPORT */
static Bool SetSlaveBO(PixmapPtr ppix,
                       int fd_handle,
                       int pitch,
                       int size,
                       drmmode_ptr drmmode)
{
    msPixmapPrivPtr pPixPriv = msGetPixmapPriv(drmmode, ppix);

    pPixPriv->backing_bo = dumb_get_bo_from_fd(drmmode->fd, fd_handle, pitch, size);
    if (pPixPriv->backing_bo == NULL)
    {
        return FALSE;
    }

    close(fd_handle);
    return TRUE;
}

/* OUTPUT SLAVE SUPPORT */
void *drmmode_map_slave_bo(drmmode_ptr drmmode, msPixmapPrivPtr ppriv)
{
    int ret;

    if (ppriv->backing_bo->ptr)
    {
        return ppriv->backing_bo->ptr;
    }

    ret = dumb_bo_map(drmmode->fd, ppriv->backing_bo);
    if (ret)
    {
        return NULL;
    }

    return ppriv->backing_bo->ptr;
}


static int dispatch_dirty_region(ScrnInfoPtr pScrn,
                                 PixmapPtr pixmap,
                                 DamagePtr damage,
                                 int fb_id)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    RegionPtr pDirty = DamageRegion(damage);
    const unsigned int nClipRects = REGION_NUM_RECTS(pDirty);
    BoxPtr pRect = REGION_RECTS(pDirty);
    int ret = 0;

    if (nClipRects)
    {
        drmModeClip *pClip;
        unsigned int i;

        pClip = xallocarray(nClipRects, sizeof(drmModeClip));
        if (pClip == NULL)
        {
            return -ENOMEM;
        }

        /* XXX no need for copy? */
        for (i = 0; i < nClipRects; ++i)
        {
            pClip[i].x1 = pRect->x1;
            pClip[i].y1 = pRect->y1;
            pClip[i].x2 = pRect->x2;
            pClip[i].y2 = pRect->y2;

            ++pRect;
        }

        /* TODO query connector property to see if this is needed */
        ret = drmModeDirtyFB(lsp->fd, fb_id, pClip, nClipRects);

        /* if we're swamping it with work, try one at a time */
        if (ret == -EINVAL)
        {
            for (i = 0; i < nClipRects; i++)
            {
                ret = drmModeDirtyFB(lsp->fd, fb_id, &pClip[i], 1);
                if (ret < 0)
                    break;
            }
        }

        free(pClip);
        DamageEmpty(damage);
    }

    return ret;
}

/* OUTPUT SLAVE SUPPORT */
void LS_DispatchDirty(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    PixmapPtr pixmap = pScreen->GetScreenPixmap(pScreen);
    int fb_id = lsp->drmmode.fb_id;
    int ret;

    ret = dispatch_dirty_region(pScrn, pixmap, lsp->damage, fb_id);
    if ((ret == -EINVAL) || (ret == -ENOSYS))
    {
        lsp->dirty_enabled = FALSE;
        DamageUnregister(lsp->damage);
        DamageDestroy(lsp->damage);
        lsp->damage = NULL;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "Disabling kernel dirty updates, not required.\n");
        return;
    }
}


// ifdef MODESETTING_OUTPUT_SLAVE_SUPPORT
/* OUTPUT SLAVE SUPPORT */
void LS_DispatchSlaveDirty(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = loongsonPTR(pScrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    const int nCrtc = xf86_config->num_crtc;
    msPixmapPrivPtr ppriv;
    int c;

    for (c = 0; c < nCrtc; ++c)
    {
        xf86CrtcPtr pCrtc = xf86_config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = pCrtc->driver_private;
        PixmapPtr pPix;

        if (!drmmode_crtc)
            continue;

        pPix = drmmode_crtc->prime_pixmap;
        if (pPix)
        {
            // dispatch_dirty_pixmap(pScrn, pCrtc, drmmode_crtc->prime_pixmap);
            ppriv = msGetPixmapPriv(&ms->drmmode, pPix);
            dispatch_dirty_region(pScrn, pPix, ppriv->slave_damage, ppriv->fb_id);
        }

        pPix = drmmode_crtc->prime_pixmap_back;
        if (pPix)
        {
            // dispatch_dirty_pixmap(pScrn, pCrtc, drmmode_crtc->prime_pixmap_back);
            ppriv = msGetPixmapPriv(&ms->drmmode, pPix);
            dispatch_dirty_region(pScrn, pPix, ppriv->slave_damage, ppriv->fb_id);
        }
    }
}


Bool LS_SharePixmapBacking(PixmapPtr pPix, ScreenPtr slave, void **handle)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
#ifdef GLAMOR_HAS_GBM
    struct GlamorAPI * const pGlamor = &lsp->glamor;
#endif
    int ret = -1;
    CARD16 stride;
    CARD32 size;


    xf86Msg(X_INFO, "\n");

    xf86Msg(X_INFO, "-------- %s stated --------\n", __func__);

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        ret = pGlamor->shareable_fd_from_pixmap(pScreen, pPix, &stride, &size);
        if (ret == -1)
        {
            return FALSE;
        }

        *handle = (void *)(long)(ret);

        xf86Msg(X_INFO, "-------- %s true finished --------\n", __func__);
        xf86Msg(X_INFO, "\n");

        return TRUE;
    }
#endif

    if (pDrmMode->exa_enabled)
    {
        ret = ls_exa_shareable_fd_from_pixmap(pScreen, pPix, &stride, &size);
        if (ret == -1)
        {
            return FALSE;
        }

        *handle = (void *)(long)(ret);

        xf86Msg(X_INFO, "-------- %s true finished --------\n", __func__);
        xf86Msg(X_INFO, "\n");

        return TRUE;
    }


    xf86Msg(X_INFO, "-------- %s false finished --------\n", __func__);
    xf86Msg(X_INFO, "\n");

    return FALSE;
}


/* OUTPUT SLAVE SUPPORT */
Bool LS_SetSharedPixmapBacking(PixmapPtr pPix, void *fd_handle)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    int size = pPix->devKind * pPix->drawable.height;
    Bool ret = FALSE;
    int ihandle = (int) (long) fd_handle;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "%s: stride=%d, height=%d, fd=%d\n",
                __func__, pPix->devKind, pPix->drawable.height, ihandle);

    /* suijingfeng: pass -1 means unshare slave pixmap */
    if (ihandle == -1)
    {
           msPixmapPrivPtr pPixPriv = msGetPixmapPriv(pDrmMode, pPix);

           dumb_bo_destroy(pDrmMode->fd, pPixPriv->backing_bo);
           pPixPriv->backing_bo = NULL;
           return TRUE;
    }

    ret = SetSlaveBO(pPix, ihandle, pPix->devKind, size, pDrmMode);

    return ret;
}
