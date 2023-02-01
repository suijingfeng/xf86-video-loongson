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

#include <unistd.h>
#include <malloc.h>
#include <xf86.h>

#include "loongson_options.h"
#include "loongson_shadow.h"
#include "loongson_blt.h"
#include "driver.h"

Bool LS_ShadowAllocFB(ScrnInfoPtr pScrn,
                      int width,
                      int height,
                      int bpp,
                      void **ppShadowFB)
{
    unsigned int bit2byte = (bpp + 7) >> 3;
    unsigned int pitch = width * bit2byte;
    void *pFB;

    pitch += 255;
    pitch &= ~255;

    pFB = calloc(1, pitch * height);
    if (!pFB)
        return FALSE;

    *ppShadowFB = pFB;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "Alloc Shadow FB: %dx%d, bytes per pixels=%d\n",
               width, height, bit2byte);

    return TRUE;
}

void LS_ShadowFreeFB(ScrnInfoPtr pScrn, void **ppShadowFB)
{
    if (*ppShadowFB)
    {
        free(*ppShadowFB);
        *ppShadowFB = NULL;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Shadow FB Freed\n");
    }
}

void LS_TryEnableShadow(ScrnInfoPtr pScrn)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    Bool prefer_shadow = TRUE;
    uint64_t value = 0;
    int ret;

    ret = drmGetCap(lsp->fd, DRM_CAP_DUMB_PREFER_SHADOW, &value);
    if (ret == 0)
    {
        prefer_shadow = !!value;
    }

    pDrmMode->shadow_enable = xf86ReturnOptValBool(
        pDrmMode->Options, OPTION_SHADOW_FB, prefer_shadow);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "ShadowFB: preferred %s, enabled %s\n",
               prefer_shadow ? "YES" : "NO",
               pDrmMode->shadow_enable ? "YES" : "NO");
}


void *LS_ShadowWindow(ScreenPtr pScreen,
                      CARD32 row,
                      CARD32 offset,
                      int mode,
                      CARD32 *pSize,
                      void *closure)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct DrmModeBO * const pFrontBO = pDrmMode->front_bo;
    int stride = pScrn->displayWidth * pDrmMode->kbpp / 8;
    uint8_t *base;

    base = dumb_bo_cpu_addr(pFrontBO->dumb);
    *pSize = stride;

    return (base + row * stride + offset);
}

static void loongson_damage_update_u32(ScreenPtr pScreen,
                                       PixmapPtr pShadow,
                                       RegionPtr damage)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct dumb_bo *pFB = pDrmMode->front_bo->dumb;
    uint8_t *winBase = (uint8_t *) dumb_bo_cpu_addr(pFB);
    uint8_t *shaBase = (uint8_t *) pDrmMode->shadow_fb;
    uint32_t dst_stride = dumb_bo_pitch(pFB);
    uint32_t src_stride = pShadow->devKind;
    int nbox = RegionNumRects(damage);
    BoxPtr pbox = RegionRects(damage);

    while (nbox--)
    {
        int x = pbox->x1;
        int y = pbox->y1;
        int w = pbox->x2 - pbox->x1;
        int h = pbox->y2 - pbox->y1;
        uint8_t *pSrc = shaBase + y * src_stride + x * 4;
        uint8_t *pDst = winBase + y * dst_stride + x * 4;
        int len = w * 4;

        while (h--)
        {
            loongson_blt(pDst, pSrc, len);
            pSrc += src_stride;
            pDst += dst_stride;
        }
        pbox++;
    }
}

void LS_ShadowUpdatePacked(ScreenPtr pScreen,
                           struct _shadowBuf * const pSdwBuf)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct ShadowAPI * const pFnShadow = &lsp->shadow;

    if (pScrn->bitsPerPixel == 32)
    {
        loongson_damage_update_u32(pScreen,
                                   pSdwBuf->pPixmap,
                                   DamageRegion(pSdwBuf->pDamage));
    }
    else
    {
        pFnShadow->UpdatePacked(pScreen, pSdwBuf);
    }
}

void loongson_dispatch_dirty(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    PixmapPtr pPixmap = pScreen->GetScreenPixmap(pScreen);
    RegionPtr pRegion;

    pRegion = DamageRegion(lsp->damage);
    if (RegionNotEmpty(pRegion))
    {
        loongson_damage_update_u32(pScreen, pPixmap, pRegion);
        DamageEmpty(lsp->damage);
    }
}

Bool LS_ShadowLoadAPI(ScrnInfoPtr pScrn)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct ShadowAPI * const pShadowAPI = &lsp->shadow;
    void *pMod = xf86LoadSubModule(pScrn, "shadow");

    if (NULL == pMod)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed loading shadow module.\n");
        return FALSE;
    }

    // suijingfeng: LoaderSymbolFromModule is not get exported
    // This is embarassing.
    pShadowAPI->Setup = LoaderSymbol("shadowSetup");
    pShadowAPI->Add = LoaderSymbol("shadowAdd");
    pShadowAPI->Remove = LoaderSymbol("shadowRemove");
    pShadowAPI->Update32to24 = LoaderSymbol("shadowUpdate32to24");
    pShadowAPI->UpdatePacked = LS_ShadowUpdatePacked;
    pShadowAPI->Update32 = loongson_damage_update_u32;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "Shadow API's symbols loaded.\n");

    return TRUE;
}
