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
#include <fcntl.h>
#include <malloc.h>
#include <xf86.h>

#include <fb.h>

#include "loongson_options.h"
#include "loongson_shadow.h"
#include "driver.h"


Bool LS_ShadowAllocFB(ScrnInfoPtr pScrn, void **ppShadowFB)
{
    int bit2byte = (pScrn->bitsPerPixel + 7) >> 3;
    void *pFB;

    pFB = calloc(1, pScrn->displayWidth * pScrn->virtualY * bit2byte);

    *ppShadowFB = pFB;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "Alloc Shadow FB: %dx%d, bytes per pixels=%d\n",
            pScrn->displayWidth, pScrn->virtualY, bit2byte);

    return (pFB != NULL);
}

void LS_ShadowFreeFB(ScrnInfoPtr pScrn, void **ppShadowFB)
{
    if (*ppShadowFB)
    {
        free(*ppShadowFB);
        *ppShadowFB = NULL;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Free Shadow FB\n");
}


static Bool LS_ShadowShouldDouble(ScrnInfoPtr pScrn)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    Bool ret = FALSE, asked;
    int from;

    if (pDrmMode->is_lsdc)
        return FALSE;

    asked = xf86GetOptValBool(pDrmMode->Options,
                              OPTION_DOUBLE_SHADOW, &ret);

    if (asked)
        from = X_CONFIG;
    else
        from = X_INFO;

    xf86DrvMsg(pScrn->scrnIndex, from,
               "Double-buffered shadow updates: %s\n",
               ret ? "on" : "off");

    return ret;
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

    pDrmMode->shadow_enable2 = pDrmMode->shadow_enable ?
        LS_ShadowShouldDouble(pScrn) : FALSE;
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
    int stride = pScrn->displayWidth * pDrmMode->kbpp / 8;

    *pSize = stride;

    return ((uint8_t *) pDrmMode->front_bo.dumb->ptr + row * stride + offset);
}


static Bool LS_UpdateIntersect(struct drmmode_rec * const pDrmMode,
                               shadowBufPtr pBuf,
                               BoxPtr box,
                               xRectangle * const pRect)
{
    const unsigned int stride = pBuf->pPixmap->devKind;
    const unsigned int line_width = (box->x2 - box->x1) * pDrmMode->cpp;
    const unsigned int num_lines = box->y2 - box->y1;

    unsigned char *old = pDrmMode->shadow_fb2;
    unsigned char *new = pDrmMode->shadow_fb;
    unsigned int go_to_start = box->y1 * stride + box->x1 * pDrmMode->cpp;
    unsigned int i;
    Bool dirty = FALSE;

    old += go_to_start;
    new += go_to_start;

    for (i = 0; i < num_lines; ++i)
    {
        // unsigned char *o = old + i * stride;
        // unsigned char *n = new + i * stride;
        if (memcmp(old, new, line_width) != 0)
        {
            dirty = TRUE;
            memcpy(old, new, line_width);
        }

        old += stride;
        new += stride;
    }

    if (dirty)
    {
        pRect->x = box->x1;
        pRect->y = box->y1;
        pRect->width = box->x2 - box->x1;
        pRect->height = box->y2 - box->y1;
    }

    return dirty;
}

static void LS_DoubleShadowUpdate(struct drmmode_rec * const pDrmMode,
                                  struct _shadowBuf * const pBuf)
{
/* somewhat arbitrary tile size, in pixels */
#define TILE 16

    RegionPtr damage = DamageRegion(pBuf->pDamage);
    RegionPtr pTiles;
    BoxPtr extents = RegionExtents(damage);

    int i, j;

    int tx1 = extents->x1 / TILE;
    int tx2 = (extents->x2 + TILE - 1) / TILE;
    int ty1 = extents->y1 / TILE;
    int ty2 = (extents->y2 + TILE - 1) / TILE;

    int nrects = (tx2 - tx1) * (ty2 - ty1);

    xRectangle * const pRect = calloc(nrects, sizeof(xRectangle));
    if (pRect == NULL)
        return;

    nrects = 0;
    for (j = ty2 - 1; j >= ty1; j--)
    {
        for (i = tx2 - 1; i >= tx1; i--)
        {
            BoxRec box;

            box.x1 = max(i * TILE, extents->x1);
            box.y1 = max(j * TILE, extents->y1);
            box.x2 = min((i+1) * TILE, extents->x2);
            box.y2 = min((j+1) * TILE, extents->y2);

            if (RegionContainsRect(damage, &box) != rgnOUT)
            {
                if (LS_UpdateIntersect(pDrmMode, pBuf, &box, pRect+nrects))
                {
                    nrects++;
                }
            }
        }
    }

    pTiles = RegionFromRects(nrects, pRect, CT_NONE);
    RegionIntersect(damage, damage, pTiles);
    RegionDestroy(pTiles);

    free(pRect);

#undef TILE

}

static void LS_ShadowUpdate32(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;

    RegionPtr damage = DamageRegion(pBuf->pDamage);
    PixmapPtr pShadow = pBuf->pPixmap;
    DrawablePtr pDrawable = &pShadow->drawable;
    int nbox = RegionNumRects(damage);
    BoxPtr pbox = RegionRects(damage);

    _X_UNUSED int shaXoff, shaYoff;

    PixmapPtr pPix;
    fbGetDrawablePixmap(pDrawable, pPix, shaXoff, shaYoff);

    uint32_t *shaBase = (uint32_t *) pPix->devPrivate.ptr;
    uint32_t *winBase = (uint32_t *) pDrmMode->front_bo.dumb->ptr;

    uint32_t src_stride = pPix->devKind / sizeof (uint32_t);
    uint32_t dst_stride = pScrn->displayWidth;

    while (nbox--)
    {
        int x = pbox->x1;
        int y = pbox->y1;
        int w = pbox->x2 - pbox->x1;
        int h = pbox->y2 - pbox->y1;
        uint32_t *src = shaBase + y * src_stride + x;
        uint32_t *dst = winBase + y * dst_stride + x;
        int len = w * 4;

        while (h--)
        {
            memcpy(dst, src, len);

            src += src_stride;
            dst += dst_stride;
        }
        pbox++;
    }
}


void LS_ShadowUpdatePacked(ScreenPtr pScreen,
                           struct _shadowBuf * const pSdwBuf)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct ShadowAPI * const pFnShadow = &lsp->shadow;

    if (pDrmMode->shadow_enable2 && pDrmMode->shadow_fb2)
    {
        LS_DoubleShadowUpdate(pDrmMode, pSdwBuf);
    }

    if (pScrn->bitsPerPixel == 32)
    {
        LS_ShadowUpdate32(pScreen, pSdwBuf);
    }
    else
    {
        pFnShadow->UpdatePacked(pScreen, pSdwBuf);
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
    pShadowAPI->Update32 = LS_ShadowUpdate32;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "Shadow API's symbols loaded.\n");

    return TRUE;
}
