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
#include <xf86.h>

#include "driver.h"
#include "loongson_buffer.h"
#include "loongson_debug.h"
#include "loongson_pixmap.h"

//
// For pixmaps that are scanout, backing for windows or bigger than,
// "accelerate" them by allocating them via GEM.
//
// For all other pixmaps where we never expect DRI2 CreateBuffer to
// be called. We just malloc them, which turns out to be much faster.
//
// return TRUE if it is a dumb, return FALSE otherwise.

Bool LS_IsDumbPixmap(int usage_hint)
{
    if (usage_hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP)
    {
        return TRUE;
    }

    if (usage_hint == CREATE_PIXMAP_USAGE_SHARED)
    {
        return TRUE;
    }

    if (usage_hint == CREATE_PIXMAP_USAGE_GLYPH_PICTURE)
    {
        // TODO : debug this
        // suijingfeng: bad looking if using dumb, strange !
        return FALSE;
    }

    if (usage_hint == CREATE_PIXMAP_USAGE_SCRATCH)
    {
        return FALSE;
    }

    if (usage_hint == CREATE_PIXMAP_USAGE_SCANOUT)
    {
        return TRUE;
    }

    return TRUE;
}


////////////////////////////////////////////////////////////////////////////
//                Only allocate DRI2/DRI3 pixmaps with GEM
//
// The driver is currently set up (via EXA) to handle all pixmap allocations
// itself, and it passes them to GEM.
//
// However, running non-3D apps tends to generate a large number of small
// pixmap allocations that we're never going to pass to GL, and it is
// expensive and pointless to send them through GEM in this case.
//
// To solve this, fbturbo and my earlier armsoc work for r3p2 strips out EXA,
// falling back to X's fast default implementation of pixmap allocation in
// the fb layer, then we would migrate such pixmaps to GEM the first time
// DRI2 interaction is requested.
//
// However, especially with growing interest in using the CPU cache for a
// future performance increase, EXA's PrepareAccess and FinishAccess hooks
// are looking more and more useful. In order to use them, we also have to
// take over allocation of all pixmaps, but we can do it better: we can
// use usage_hint to determine which pixmaps are backing buffers for windows
// (hence likely candidates for DRI2 interaction) and which aren't.
//
// The only remaining complication is to detect which pixmap is going to be
// used for scanout, this is a bit hacky but it seems to always be the first
// pixmap created after ScreenInit.
///////////////////////////////////////////////////////////////////////////

void *LS_CreateExaPixmap(ScreenPtr pScreen,
                         int width, int height, int depth,
                         int usage_hint, int bitsPerPixel,
                         int *new_fb_pitch)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct exa_pixmap_priv *priv;

    TRACE_ENTER();

    priv = calloc(1, sizeof(struct exa_pixmap_priv));
    if (NULL == priv)
    {
        return NULL;
    }

    priv->pBuf = calloc(1, sizeof(struct LoongsonBuf));
    if (NULL == priv->pBuf)
    {
        free(priv);
        return NULL;
    }

    priv->usage_hint = usage_hint;
    priv->is_dumb = FALSE;

    if ((width > 0) && (height > 0) && (depth > 0) && (bitsPerPixel > 0))
    {
        // DEBUG_MSG ( "Create Exa Pixmap %dx%d %d %d",
        //        width, height, depth, bitsPerPixel);

        LS_AllocBuf(width, height, depth, bitsPerPixel, usage_hint, priv->pBuf);
        if (NULL == priv->pBuf->pDat)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "failed to allocate %dx%d mem", width, height);

            free(priv->pBuf);
            free(priv);
            return NULL;
        }
    }

    if (new_fb_pitch)
    {
        *new_fb_pitch = priv->pBuf->pitch;
    }

    TRACE_EXIT();

    return priv;
}



void LS_DestroyExaPixmap(ScreenPtr pScreen, void *driverPriv)
{
    // ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    // modesettingPtr ms = loongsonPTR(pScrn);

    struct exa_pixmap_priv *priv = (struct exa_pixmap_priv *)driverPriv;

    // struct LoongsonBuf * pBuf = &priv->buf;

    LS_FreeBuf(priv->pBuf);

    free(priv->pBuf);
    free(priv);

#ifdef FAKE_EXA_DEBUG
    INFO_MSG("Destroy EXA Pixmap buf: %p", pBuf->pDat);
#endif
}


///////////////////////////////////////////////////////////////////////////
//
// With the introduction of pixmap privates, the "screen pixmap" can no
// longer be created in miScreenInit, since all the modules that could
// possibly ask for pixmap private space have not been initialized at
// that time.  pScreen->CreateScreenResources is called after all
// possible private-requesting modules have been inited; we create the
// screen pixmap here.
//
///////////////////////////////////////////////////////////////////////////

void *LS_CreateDumbPixmap(ScreenPtr pScreen,
                          int width,
                          int height,
                          int depth,
                          int usage_hint,
                          int bitsPerPixel,
                          int *new_fb_pitch)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct exa_pixmap_priv *priv = calloc(1, sizeof(struct exa_pixmap_priv));

    if (NULL == priv)
    {
        return NULL;
    }

    priv->usage_hint = usage_hint;

    if ((0 == width) && (0 == height))
    {
        return priv;
    }

    priv->bo = dumb_bo_create(pDrmMode->fd, width, height, bitsPerPixel);
    if (NULL == priv->bo)
    {
        free(priv);

        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "failed to allocate %dx%d bo\n", width, height);

        return NULL;
    }

    priv->owned = TRUE;
    priv->is_dumb = TRUE;
    priv->pitch = priv->bo->pitch;

    if (new_fb_pitch)
    {
        *new_fb_pitch = priv->pitch;
    }

    return priv;
}


void LS_DestroyDumbPixmap(ScreenPtr pScreen, void *driverPriv)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct exa_pixmap_priv *priv = (struct exa_pixmap_priv *)driverPriv;

    if (priv->fd > 0)
    {
        close(priv->fd);
    }

    if ((priv->is_dumb == TRUE) && (priv->bo != NULL))
    {
        dumb_bo_destroy(lsp->drmmode.fd, priv->bo);

#ifdef FAKE_EXA_DEBUG
        INFO_MSG("DestroyPixmap bo:%p", priv->bo);
#endif
    }

    free(priv);
}


PixmapPtr drmmode_create_pixmap_header(ScreenPtr pScreen,
                                       int width,
                                       int height,
                                       int depth,
                                       int bitsPerPixel,
                                       int devKind,
                                       void *pPixData)
{
    PixmapPtr pixmap;

    /* width and height of 0 means don't allocate any pixmap data */
    pixmap = pScreen->CreatePixmap(pScreen, 0, 0, depth, 0);

    if (pixmap)
    {
        if (pScreen->ModifyPixmapHeader(pixmap, width, height, depth,
                                           bitsPerPixel, devKind, pPixData))
        {
            return pixmap;
        }

        pScreen->DestroyPixmap(pixmap);
    }

    return NullPixmap;
}
