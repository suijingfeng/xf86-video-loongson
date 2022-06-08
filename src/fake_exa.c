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

#include <exa.h>
#include <xf86.h>
#include <fbpict.h>
#include <unistd.h>
#include <fcntl.h>

#include "driver.h"
#include "dumb_bo.h"

#include "fake_exa.h"

#include "loongson_options.h"
#include "loongson_pixmap.h"
#include "loongson_debug.h"


struct ms_exa_prepare_args {
    struct {
        int alu;
        Pixel planemask;
        Pixel fg;
    } solid;

    struct {
        PixmapPtr pSrcPixmap;
        int alu;
        Pixel planemask;
    } copy;

    struct {
        int op;
        PicturePtr pSrcPicture;
        PicturePtr pMaskPicture;
        PicturePtr pDstPicture;
        PixmapPtr pSrc;
        PixmapPtr pMask;
        PixmapPtr pDst;

        int rotate;
        Bool reflect_y;
    } composite;
};

static struct ms_exa_prepare_args exa_prepare_args = {{0}};


/////////////////////////////////////////////////////////////////////////

static Bool PrepareSolidFail(PixmapPtr pPixmap, int alu, Pixel planemask,
        Pixel fill_colour)
{
    return FALSE;
}

static Bool PrepareCopyFail(PixmapPtr pSrc, PixmapPtr pDst, int xdir, int ydir,
        int alu, Pixel planemask)
{
    return FALSE;
}

static Bool CheckCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
        PicturePtr pDstPicture)
{
    return FALSE;
}

static Bool PrepareCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
        PicturePtr pDstPicture, PixmapPtr pSrc, PixmapPtr pMask, PixmapPtr pDst)
{
    return FALSE;
}

/////////////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////////////////
/////////////    solid    ////////////////////////////////////////////////


static Bool ms_exa_prepare_solid(PixmapPtr pPixmap,
                                 int alu,
                                 Pixel planemask,
                                 Pixel fg)
{
    exa_prepare_args.solid.alu = alu;
    exa_prepare_args.solid.planemask = planemask;
    exa_prepare_args.solid.fg = fg;

    return TRUE;
}


static void ms_exa_solid(PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
    ScreenPtr screen = pPixmap->drawable.pScreen;
    GCPtr gc = GetScratchGC(pPixmap->drawable.depth, screen);
    ChangeGCVal val[3];

    val[0].val = exa_prepare_args.solid.alu;
    val[1].val = exa_prepare_args.solid.planemask;
    val[2].val = exa_prepare_args.solid.fg;
    ChangeGC(NullClient, gc, GCFunction | GCPlaneMask | GCForeground, val);
    ValidateGC(&pPixmap->drawable, gc);

    ms_exa_prepare_access(pPixmap, 0);
    fbFill(&pPixmap->drawable, gc, x1, y1, x2 - x1, y2 - y1);
    ms_exa_finish_access(pPixmap, 0);

    FreeScratchGC(gc);
}


static void ms_exa_solid_done(PixmapPtr pPixmap)
{

}

//////////////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////////////////
/////////////     copy    ////////////////////////////////////////////////

static Bool ms_exa_prepare_copy(PixmapPtr pSrcPixmap,
                    PixmapPtr pDstPixmap,
                    int dx, int dy, int alu, Pixel planemask)
{
    exa_prepare_args.copy.pSrcPixmap = pSrcPixmap;
    exa_prepare_args.copy.alu = alu;
    exa_prepare_args.copy.planemask = planemask;

    return TRUE;
}


static void ms_exa_copy(PixmapPtr pDstPixmap, int srcX, int srcY,
            int dstX, int dstY, int width, int height)
{
    PixmapPtr pSrcPixmap = exa_prepare_args.copy.pSrcPixmap;
    ScreenPtr screen = pDstPixmap->drawable.pScreen;
    ChangeGCVal val[2];
    GCPtr gc;

    gc = GetScratchGC(pDstPixmap->drawable.depth, screen);

    val[0].val = exa_prepare_args.copy.alu;
    val[1].val = exa_prepare_args.copy.planemask;
    ChangeGC(NullClient, gc, GCFunction | GCPlaneMask, val);
    ValidateGC(&pDstPixmap->drawable, gc);

    ms_exa_prepare_access(pSrcPixmap, 0);
    ms_exa_prepare_access(pDstPixmap, 0);

    fbCopyArea(&pSrcPixmap->drawable, &pDstPixmap->drawable, gc,
               srcX, srcY, width, height, dstX, dstY);

    ms_exa_finish_access(pDstPixmap, 0);
    ms_exa_finish_access(pSrcPixmap, 0);

    FreeScratchGC(gc);
}

static void ms_exa_copy_done(PixmapPtr pPixmap)
{

}

//////////////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////////////////
/////////////     coposite    ////////////////////////////////////////////

static Bool ms_exa_check_composite(int op, PicturePtr pSrcPicture,
                       PicturePtr pMaskPicture, PicturePtr pDstPicture)
{
    if (!pSrcPicture->pDrawable)
        return FALSE;

    return TRUE;
}

static Bool ms_exa_prepare_composite(int op,
                         PicturePtr pSrcPicture,
                         PicturePtr pMaskPicture,
                         PicturePtr pDstPicture,
                         PixmapPtr pSrc, PixmapPtr pMask, PixmapPtr pDst)
{
    exa_prepare_args.composite.op = op;
    exa_prepare_args.composite.pSrcPicture = pSrcPicture;
    exa_prepare_args.composite.pMaskPicture = pMaskPicture;
    exa_prepare_args.composite.pDstPicture = pDstPicture;
    exa_prepare_args.composite.pSrc = pSrc;
    exa_prepare_args.composite.pMask = pMask;

    return TRUE;
}


static void ms_exa_composite(PixmapPtr pDst, int srcX, int srcY,
                 int maskX, int maskY, int dstX, int dstY,
                 int width, int height)
{
    PicturePtr pSrcPicture = exa_prepare_args.composite.pSrcPicture;
    PicturePtr pMaskPicture = exa_prepare_args.composite.pMaskPicture;
    PicturePtr pDstPicture = exa_prepare_args.composite.pDstPicture;
    PixmapPtr pSrc = exa_prepare_args.composite.pSrc;
    PixmapPtr pMask = exa_prepare_args.composite.pMask;
    int op = exa_prepare_args.composite.op;

    if (pMask)
    {
        ms_exa_prepare_access(pMask, 0);
    }

    ms_exa_prepare_access(pSrc, 0);
    ms_exa_prepare_access(pDst, 0);

    fbComposite(op, pSrcPicture, pMaskPicture, pDstPicture,
                srcX, srcY, maskX, maskY, dstX, dstY, width, height);

    ms_exa_finish_access(pDst, 0);
    ms_exa_finish_access(pSrc, 0);

    if (pMask)
    {
        ms_exa_finish_access(pMask, 0);
    }
}

static void ms_exa_composite_done(PixmapPtr pPixmap)
{

}


//////////////////////////////////////////////////////////////////////////


/*

static Bool
ms_exa_upload_to_screen(PixmapPtr pDst, int x, int y, int w, int h,
                        char *src, int src_pitch)
{
    return FALSE;
}

static Bool
ms_exa_download_from_screen(PixmapPtr pSrc, int x, int y, int w, int h,
                            char *dst, int dst_pitch)
{
    return FALSE;
}

*/


static void ms_exa_wait_marker(ScreenPtr pScreen, int marker)
{
    // TODO:
}

/*

static int ms_exa_mark_sync(ScreenPtr pScreen)
{
    // TODO: return latest request(marker).
    return 0;
}

*/


///////////////////////////////////////////////////////////////////////

/*
static const char * ms_exa_index_to_string(int index)
{
    switch (index)
    {
        case EXA_PREPARE_DEST:
            return "DEST";
        case EXA_PREPARE_SRC:
            return "SRC";
        case EXA_PREPARE_MASK:
            return "MASK";
        case EXA_PREPARE_AUX_DEST:
            return "AUX_DEST";
        case EXA_PREPARE_AUX_SRC:
            return "AUX_SRC";
        case EXA_PREPARE_AUX_MASK:
            return "AUX_MASK";
        default:
            return "unknown";
    }
}
*/

/**
 * PrepareAccess() is called before CPU access to an offscreen pixmap.
 *
 * @param pPix the pixmap being accessed
 * @param index the index of the pixmap being accessed.
 *
 * PrepareAccess() will be called before CPU access to an offscreen pixmap.
 * This can be used to set up hardware surfaces for byteswapping or untiling,
 * or to adjust the pixmap's devPrivate.ptr for the purpose of making CPU
 * access use a different aperture.
 *
 * The index is one of
 *
 * #EXA_PREPARE_DEST,
 * #EXA_PREPARE_SRC,
 * #EXA_PREPARE_MASK,
 * #EXA_PREPARE_AUX_DEST,
 * #EXA_PREPARE_AUX_SRC, or
 * #EXA_PREPARE_AUX_MASK.
 *
 *
 * Since only up to #EXA_NUM_PREPARE_INDICES pixmaps will have PrepareAccess()
 * called on them per operation, drivers can have a small, statically-allocated
 * space to maintain state for PrepareAccess() and FinishAccess() in.
 *
 * Note that PrepareAccess() is only called once per pixmap and operation,
 * regardless of whether the pixmap is used as a destination and/or source,
 * and the index may not reflect the usage.
 *
 * PrepareAccess() may fail.  An example might be the case of hardware that
 * can set up 1 or 2 surfaces for CPU access, but not 3.  If PrepareAccess()
 * fails, EXA will migrate the pixmap to system memory.
 *
 * DownloadFromScreen() must be implemented and must not fail if a driver
 * wishes to fail in PrepareAccess().  PrepareAccess() must not fail when
 * pPix is the visible screen, because the visible screen can not be
 * migrated.
 *
 * @return TRUE if PrepareAccess() successfully prepared the pixmap for CPU
 * drawing.
 * @return FALSE if PrepareAccess() is unsuccessful and EXA should use
 * DownloadFromScreen() to migate the pixmap out.
 */

Bool ms_exa_prepare_access(PixmapPtr pPix, int index)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct ms_exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPix);
    int ret;

    if (pPix->devPrivate.ptr)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                    "%s: already prepared\n", __func__);

        return TRUE;
    }

    if (LS_IsDumbPixmap(priv->usage_hint))
    {
        ret = dumb_bo_map(pDrmMode->fd, priv->bo);
        if (ret)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "%s: dumb bo map failed: %s, ret=%d\n",
                       __func__, strerror(errno), ret);
            return FALSE;
        }
        pPix->devPrivate.ptr = priv->bo->ptr;
    }
    else
    {
        pPix->devPrivate.ptr = priv->pBuf->pDat;
    }

    /* When !NULL, devPrivate.ptr points to the raw pixel data */
    return pPix->devPrivate.ptr != NULL;
}


/**
 * FinishAccess() is called after CPU access to an offscreen pixmap.
 *
 * @param pPixmap the pixmap being accessed
 * @param index the index of the pixmap being accessed.
 *
 * FinishAccess() will be called after finishing CPU access of an offscreen
 * pixmap set up by PrepareAccess().  Note that the FinishAccess() will not be
 * called if PrepareAccess() failed and the pixmap was migrated out.
 */
void ms_exa_finish_access(PixmapPtr pPixmap, int index)
{
    struct ms_exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);

    if (priv && priv->bo)
    {
        pPixmap->devPrivate.ptr = NULL;
    }
}


static void ms_exa_destroy_pixmap(ScreenPtr pScreen, void *driverPriv)
{
    struct ms_exa_pixmap_priv *pPriv = (struct ms_exa_pixmap_priv *) driverPriv;

    if (LS_IsDumbPixmap(pPriv->usage_hint))
    {
        LS_DestroyDumbPixmap(pScreen, driverPriv);
    }
    else
    {
        LS_DestroyExaPixmap(pScreen, driverPriv);
    }
}

/*
static Bool ms_exa_modify_pixmap_header(PixmapPtr pPixmap,
        int width, int height, int depth, int bitsPerPixel,
        int devKind, pointer pPixData)
{
    struct ms_exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);

    if ( LS_IsDumbPixmap(priv->usage_hint) )
    {
        return LS_ModifyDumbPixmapHeader(pPixmap, width, height,
                depth, bitsPerPixel, devKind, pPixData);
    }
    else
    {
        return LS_ModifyExaPixmapHeader(pPixmap, width, height,
                depth, bitsPerPixel, devKind, pPixData);
    }
}
*/

/* Hooks to allow driver to its own pixmap memory management */

static void *ms_exa_create_pixmap2(ScreenPtr pScreen,
                                   int width,
                                   int height,
                                   int depth,
                                   int usage_hint,
                                   int bitsPerPixel,
                                   int *new_fb_pitch)
{
    if (LS_IsDumbPixmap(usage_hint))
    {
        return LS_CreateDumbPixmap(pScreen, width, height, depth,
            usage_hint, bitsPerPixel, new_fb_pitch);
    }
    else
    {
        return LS_CreateExaPixmap(pScreen, width, height, depth,
            usage_hint, bitsPerPixel, new_fb_pitch);
    }
}


/**
 * PixmapIsOffscreen() is an optional driver replacement to exaPixmapHasGpuCopy().
 * Set to NULL if you want the standard behaviour of exaPixmapHasGpuCopy().
 *
 * @param pPix the pixmap
 * @return TRUE if the given drawable is in framebuffer memory.
 *
 * exaPixmapHasGpuCopy() is used to determine if a pixmap is in offscreen memory,
 * meaning that acceleration could probably be done to it, and that it will need
 * to be wrapped by PrepareAccess()/FinishAccess() when accessing it with the CPU.
 */
static Bool ms_exa_pixmap_is_offscreen(PixmapPtr pPixmap)
{
    //
    // offscreen means in 'gpu accessible memory', not that it's off the
    // visible screen.  We currently have no special constraints,
    // since fake exa has a flat memory model (no separate GPU memory).
    // If individual EXA implementation has additional constraints,
    // like buffer size or mapping in GPU MMU, it should wrap this function.
    //
    struct ms_exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);

    if (priv == NULL)
    {
        return FALSE;
    }

    if (LS_IsDumbPixmap(priv->usage_hint))
    {
        return (priv->bo != NULL);
    }
    else
    {
        return (priv->pBuf->pDat != NULL);
    }
}



////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


Bool ms_exa_set_pixmap_bo(ScrnInfoPtr pScrn, PixmapPtr pPixmap,
                     struct dumb_bo *bo, Bool owned)
{
    struct ms_exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);
    struct LoongsonRec *ls = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ls->drmmode;
    int prime_fd;
    int ret;

    if (priv == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: priv is NULL\n", __func__);
        return FALSE;
    }

    if (ls->exaDrvPtr == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: exaDrvPtr is NULL\n", __func__);
        return FALSE;
    }

    // destroy old backing memory, and update it with new.
    if (priv->fd > 0)
    {
        close(priv->fd);
    }

    if (priv->owned && priv->bo)
    {
        dumb_bo_destroy(pDrmMode->fd, priv->bo);
    }

    ret = drmPrimeHandleToFD(pDrmMode->fd, bo->handle, DRM_CLOEXEC, &prime_fd);
    if (ret)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: failed to get dmabuf fd: %d\n", __func__, ret);
        return FALSE;
    }

    priv->bo = bo;
    priv->fd = prime_fd;
    priv->pitch = bo->pitch;
    priv->owned = owned;

    pPixmap->devPrivate.ptr = NULL;
    pPixmap->devKind = priv->pitch;

    return TRUE;
}

void print_pixmap(PixmapPtr pPixmap)
{
    xf86Msg(X_INFO, "refcnt: %d\n", pPixmap->refcnt);
    xf86Msg(X_INFO, "devKind: %d\n", pPixmap->devKind);
    xf86Msg(X_INFO, "screen_x: %d\n", pPixmap->screen_x);
    xf86Msg(X_INFO, "screen_y: %d\n", pPixmap->screen_y);
    xf86Msg(X_INFO, "usage hint: %u\n", pPixmap->usage_hint);

    xf86Msg(X_INFO, "raw pixel data: %p\n", pPixmap->devPrivate.ptr);
}


/*
 *  Return the dumb bo of the pixmap if success,
 *  otherwise return NULL.
 */
struct dumb_bo *dumb_bo_from_pixmap(ScreenPtr pScreen, PixmapPtr pPixmap)
{
    struct ms_exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);

    if (priv == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: priv is NULL\n", __func__);
        return NULL;
    }

    if (lsp->exaDrvPtr == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: exaDrvPtr is NULL\n", __func__);
        return NULL;
    }

    if (pPixmap)
        print_pixmap(pPixmap);

    if (LS_IsDumbPixmap(priv->usage_hint) == TRUE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: priv is dumb\n", __func__);
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: is not a dumb\n", __func__);
        return NULL;
    }

    return priv->bo;
}


void ms_exa_exchange_buffers(PixmapPtr front, PixmapPtr back)
{
    struct ms_exa_pixmap_priv *front_priv = exaGetPixmapDriverPrivate(front);
    struct ms_exa_pixmap_priv *back_priv = exaGetPixmapDriverPrivate(back);
    struct ms_exa_pixmap_priv tmp_priv;

    tmp_priv = *front_priv;
    *front_priv = *back_priv;
    *back_priv = tmp_priv;
}

//////////////////////////////////////////////////////////////////////
//   This two is for PRIME and Reverse Prime, not tested ...
//////////////////////////////////////////////////////////////////////

Bool ms_exa_back_pixmap_from_fd(PixmapPtr pixmap,
                                int fd,
                                CARD16 width,
                                CARD16 height,
                                CARD16 stride,
                                CARD8 depth,
                                CARD8 bpp)
{
    ScreenPtr pScreen = pixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct dumb_bo *bo;
    Bool ret;

    bo = dumb_get_bo_from_fd(pDrmMode->fd, fd, stride, stride * height);
    if (!bo)
    {
        return FALSE;
    }

    pScreen->ModifyPixmapHeader(pixmap, width, height,
                                depth, bpp, stride, NULL);

    ret = ms_exa_set_pixmap_bo(pScrn, pixmap, bo, TRUE);
    if (ret == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                        "%s: ms_exa_set_pixmap_bo failed\n", __func__);

        dumb_bo_destroy(pDrmMode->fd, bo);
    }

    return ret;
}


int ms_exa_shareable_fd_from_pixmap(ScreenPtr pScreen,
                                    PixmapPtr pixmap,
                                    CARD16 *stride,
                                    CARD32 *size)
{
    struct ms_exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ls = loongsonPTR(pScrn);

    if ((ls->exaDrvPtr == NULL) || (priv == NULL) || (priv->fd == 0))
    {
        return -1;
    }

    return priv->fd;
}


/////////////////////////////////////////////////////////////////////////////////
//                  this guy do the real necessary initial job
////////////////////////////////////////////////////////////////////////////////

static Bool ms_setup_exa(ScrnInfoPtr pScrn, ExaDriverPtr pExaDrv)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;

    TRACE_ENTER();

    pExaDrv->exa_major = EXA_VERSION_MAJOR;
    pExaDrv->exa_minor = EXA_VERSION_MINOR;

    pExaDrv->pixmapOffsetAlign = 16;
    pExaDrv->pixmapPitchAlign = 256;

    pExaDrv->maxX = 8192;
    pExaDrv->maxY = 8192;

    // bo based pixmap ops
    pExaDrv->flags =
        EXA_HANDLES_PIXMAPS | EXA_SUPPORTS_PREPARE_AUX | EXA_OFFSCREEN_PIXMAPS;


    //// solid
    pExaDrv->PrepareSolid = ms_exa_prepare_solid;
    pExaDrv->Solid = ms_exa_solid;
    pExaDrv->DoneSolid = ms_exa_solid_done;

    //// copy
    pExaDrv->PrepareCopy = ms_exa_prepare_copy;
    pExaDrv->Copy = ms_exa_copy;
    pExaDrv->DoneCopy = ms_exa_copy_done;

    //// composite
    pExaDrv->CheckComposite = ms_exa_check_composite;
    pExaDrv->PrepareComposite = ms_exa_prepare_composite;
    pExaDrv->Composite = ms_exa_composite;
    pExaDrv->DoneComposite = ms_exa_composite_done;


    /* TODO: Impl upload/download */
    // pExaDrv->UploadToScreen = ms_exa_upload_to_screen;
    // pExaDrv->DownloadFromScreen = ms_exa_download_from_screen;

    pExaDrv->WaitMarker = ms_exa_wait_marker;
    // pExaDrv->MarkSync = ms_exa_mark_sync;
    pExaDrv->DestroyPixmap = ms_exa_destroy_pixmap;
    pExaDrv->CreatePixmap2 = ms_exa_create_pixmap2;
    pExaDrv->PrepareAccess = ms_exa_prepare_access;
    pExaDrv->FinishAccess = ms_exa_finish_access;
    pExaDrv->PixmapIsOffscreen = ms_exa_pixmap_is_offscreen;


    if (pDrmMode->exa_acc_type == EXA_ACCEL_TYPE_FAKE)
    {
        /* Always fallback for software operations */
        pExaDrv->PrepareCopy = PrepareCopyFail;
        pExaDrv->PrepareSolid = PrepareSolidFail;
        pExaDrv->CheckComposite = CheckCompositeFail;
        pExaDrv->PrepareComposite = PrepareCompositeFail;
    }

    TRACE_EXIT();

    return TRUE;
}



void try_enable_exa(ScrnInfoPtr pScrn)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;

    const char *accel_method_str = xf86GetOptValString(pDrmMode->Options,
                                                       OPTION_ACCEL_METHOD);
    Bool do_exa = ((accel_method_str != NULL) &&
                   ((strcmp(accel_method_str, "exa") == 0) ||
                    (strcmp(accel_method_str, "EXA") == 0)));

    if (do_exa)
    {
        const char * pExaType2D;
        pDrmMode->exa_enabled = TRUE;

        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "EXA enabled.\n");

        if (NULL == xf86LoadSubModule(pScrn, "exa"))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                    "Loading exa submodule failed.\n");
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
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                    "EXA Acceleration type: fake.\n");

            // default is fake exa
            pDrmMode->exa_acc_type = EXA_ACCEL_TYPE_FAKE;
        }
    }
    else
    {
        pDrmMode->exa_enabled = FALSE;
        // don't care this
        pDrmMode->exa_acc_type = EXA_ACCEL_TYPE_FAKE;

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: No EXA support in this driver.\n", __func__);
    }
}


/////////////////////////////////////////////////////////////////////
//         EXA Layer Initial and Destroy
/////////////////////////////////////////////////////////////////////

Bool LS_InitExaLayer(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);

    ExaDriverPtr pExaDrv = exaDriverAlloc();
    if (pExaDrv == NULL)
        return FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: Initializing EXA.\n", __func__);

    if (ms_setup_exa(pScrn, pExaDrv) == FALSE)
    {
        free(pExaDrv);
        return FALSE;
    }

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

    if (lsp->exaDrvPtr != NULL)
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
