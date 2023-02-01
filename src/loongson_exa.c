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

#include <exa.h>
#include <xf86.h>
#include <fbpict.h>
#include <unistd.h>
#include <fcntl.h>

#include "driver.h"
#include "dumb_bo.h"

#include "loongson_options.h"
#include "loongson_pixmap.h"
#include "loongson_debug.h"
#include "loongson_exa.h"

#include "fake_exa.h"
#include "etnaviv_exa.h"

#if HAVE_LIBDRM_GSGPU
#include "gsgpu_exa.h"
#endif

void print_pixmap_info(PixmapPtr pPixmap)
{

    xf86Msg(X_INFO, "refcnt: %d\n", pPixmap->refcnt);
    xf86Msg(X_INFO, "devKind: %d\n", pPixmap->devKind);
    xf86Msg(X_INFO, "screen_x: %d\n", pPixmap->screen_x);
    xf86Msg(X_INFO, "screen_y: %d\n", pPixmap->screen_y);
    xf86Msg(X_INFO, "usage hint: %u\n", pPixmap->usage_hint);

    xf86Msg(X_INFO, "location: %p, raw pixel data: %p\n",
            pPixmap, pPixmap->devPrivate.ptr);
}


void ms_exa_exchange_buffers(PixmapPtr front, PixmapPtr back)
{
    struct exa_pixmap_priv *front_priv = exaGetPixmapDriverPrivate(front);
    struct exa_pixmap_priv *back_priv = exaGetPixmapDriverPrivate(back);
    struct exa_pixmap_priv tmp_priv;

    tmp_priv = *front_priv;
    *front_priv = *back_priv;
    *back_priv = tmp_priv;
}

/*
 *  Return the dumb bo of the pixmap if success,
 *  otherwise return NULL.
 */
struct dumb_bo *dumb_bo_from_pixmap(ScreenPtr pScreen, PixmapPtr pPixmap)
{
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

    if (priv == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: priv is NULL\n", __func__);
        return NULL;
    }

    if (priv->gbo)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: priv is backing by GTT\n", __func__);
        return NULL;
    }

    return priv->bo;
}

Bool loongson_set_pixmap_dumb_bo(ScrnInfoPtr pScrn,
                                 PixmapPtr pPixmap,
                                 struct dumb_bo *dbo,
                                 int usage_hint,
                                 int prime_fd)
{
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);
    struct LoongsonRec *lsp = loongsonPTR(pScrn);

    if (priv == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: priv is NULL\n", __func__);
        return FALSE;
    }

    priv->usage_hint = usage_hint;

    // destroy old backing memory, and update it with new.
    if (priv->fd > 0)
    {
        close(priv->fd);
        priv->fd = prime_fd;
    }

    if (priv->bo)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: destroy old backing bo\n", __func__);
        dumb_bo_destroy(lsp->fd, priv->bo);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "%s: set backing dumb bo of %p: handle: %d, pitch: %d\n",
               __func__, pPixmap, dumb_bo_handle(dbo) ,dumb_bo_pitch(dbo));

    priv->bo = dbo;
    priv->pitch = dumb_bo_pitch(dbo);

    pPixmap->devKind = priv->pitch;

    return TRUE;
}

int loongson_exa_shareable_fd_from_pixmap(ScreenPtr pScreen,
                                          PixmapPtr pixmap,
                                          CARD16 *stride,
                                          CARD32 *size)
{
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);

    if ((lsp->exaDrvPtr == NULL) || (priv == NULL) || (priv->fd <= 0))
    {
        return -1;
    }

    return priv->fd;
}

/////////////////////////////////////////////////////////////////////////////
//  EXA driver instance governor
/////////////////////////////////////////////////////////////////////////////

Bool LS_InitExaLayer(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    ExaDriverPtr pExaDrv = exaDriverAlloc();
    if (pExaDrv == NULL)
        return FALSE;

    if (pDrmMode->exa_acc_type == EXA_ACCEL_TYPE_FAKE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Initializing Fake EXA\n");
        if (setup_fake_exa(pScrn, pExaDrv) == FALSE)
        {
            free(pExaDrv);
            return FALSE;
        }
    }

    if (pDrmMode->exa_acc_type == EXA_ACCEL_TYPE_SOFTWARE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Initializing software EXA\n");
        if (setup_fake_exa(pScrn, pExaDrv) == FALSE)
        {
            free(pExaDrv);
            return FALSE;
        }
    }

#if HAVE_LIBDRM_ETNAVIV
    if (pDrmMode->exa_acc_type == EXA_ACCEL_TYPE_ETNAVIV)
    {
        if (etnaviv_setup_exa(pScrn, pExaDrv) == FALSE)
        {
            free(pExaDrv);
            return FALSE;
        }
    }
#endif

#if HAVE_LIBDRM_GSGPU
    if (pDrmMode->exa_acc_type == EXA_ACCEL_TYPE_GSGPU)
    {
        if (gsgpu_setup_exa(pScrn, pExaDrv) == FALSE)
        {
            free(pExaDrv);
            return FALSE;
        }
    }
#endif

    // exaDriverInit sets up EXA given a driver record filled in by the driver.
    // pScreenInfo should have been allocated by exaDriverAlloc().
    if (exaDriverInit(pScreen, pExaDrv))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EXA initialized successful.\n");

        lsp->exaDrvPtr = pExaDrv;

        return TRUE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "EXA initialization failed.\n");

    return FALSE;
}


Bool LS_DestroyExaLayer(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;

    if (lsp->exaDrvPtr)
    {
        PixmapPtr screen_pixmap = pScreen->GetScreenPixmap(pScreen);

        if (screen_pixmap == pScreen->devPrivate)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "%s: Destroy exa screen pixmap.\n", __func__);
            pScreen->DestroyPixmap(screen_pixmap);
            pScreen->devPrivate = NULL;
        }

        exaDriverFini(pScreen);

        free(lsp->exaDrvPtr);

        lsp->exaDrvPtr = NULL;

        pDrmMode->exa_enabled = FALSE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Shutdown EXA.\n");

    return TRUE;
}

Bool try_enable_exa(ScrnInfoPtr pScrn)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    const char *accel_method_str;
    Bool do_exa;

    accel_method_str = xf86GetOptValString(pDrmMode->Options,
                                           OPTION_ACCEL_METHOD);
    do_exa = ((accel_method_str != NULL) &&
              ((strcmp(accel_method_str, "exa") == 0) ||
               (strcmp(accel_method_str, "EXA") == 0)));

    if (do_exa)
    {
        const char *pExaType2D = NULL;

        if (NULL == xf86LoadSubModule(pScrn, "exa"))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                    "Loading exa submodule failed.\n");
            return FALSE;
        }

        pExaType2D = xf86GetOptValString(pDrmMode->Options, OPTION_EXA_TYPE);
        if (pExaType2D != NULL)
        {
            if (strcmp(pExaType2D, "fake") == 0)
            {
                pDrmMode->exa_acc_type = EXA_ACCEL_TYPE_FAKE;
                xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                    "EXA Acceleration type: fake.\n");
            }
            else if (strcmp(pExaType2D, "software") == 0)
            {
                pDrmMode->exa_acc_type = EXA_ACCEL_TYPE_SOFTWARE;
                xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                           "EXA Acceleration type: software.\n");
            }
            else if (strcmp(pExaType2D, "vivante") == 0)
            {
                pDrmMode->exa_acc_type = EXA_ACCEL_TYPE_VIVANTE;
            }
            else if (strcmp(pExaType2D, "etnaviv") == 0)
            {
                pDrmMode->exa_acc_type = EXA_ACCEL_TYPE_ETNAVIV;
            }
            else if (strcmp(pExaType2D, "gsgpu") == 0)
            {
                pDrmMode->exa_acc_type = EXA_ACCEL_TYPE_GSGPU;

            }

            pDrmMode->exa_shadow_enabled = xf86ReturnOptValBool(
                    pDrmMode->Options, OPTION_SHADOW_FB, FALSE);

            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                       "EXA enabled, acceleraton method: %s\n", pExaType2D);

            if (pDrmMode->exa_shadow_enabled)
                xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "ShadowFB on EXA enabled\n");

            pDrmMode->exa_enabled = TRUE;
            return TRUE;
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                    "EXA Acceleration type: fake.\n");

            // default is fake exa
            pDrmMode->exa_acc_type = EXA_ACCEL_TYPE_FAKE;
        }

        return TRUE;
    }
    else
    {
        pDrmMode->exa_enabled = FALSE;
        // don't care this
        pDrmMode->exa_acc_type = EXA_ACCEL_TYPE_FAKE;

        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "EXA support is not enabled\n");
    }

    return FALSE;
}
