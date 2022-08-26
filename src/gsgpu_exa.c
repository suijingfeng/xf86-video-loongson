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

#include "gsgpu_exa.h"
#include "loongson_buffer.h"
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

static Bool fake_exa_prepare_access(PixmapPtr pPix, int index)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPix);
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
static void fake_exa_finish_access(PixmapPtr pPixmap, int index)
{
/*
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);

    if (priv && priv->bo)
    {
        pPixmap->devPrivate.ptr = NULL;
    }
*/
}

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

    fake_exa_prepare_access(pPixmap, 0);
    fbFill(&pPixmap->drawable, gc, x1, y1, x2 - x1, y2 - y1);
    fake_exa_finish_access(pPixmap, 0);

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

    fake_exa_prepare_access(pSrcPixmap, 0);
    fake_exa_prepare_access(pDstPixmap, 0);

    fbCopyArea(&pSrcPixmap->drawable, &pDstPixmap->drawable, gc,
               srcX, srcY, width, height, dstX, dstY);

    fake_exa_finish_access(pDstPixmap, 0);
    fake_exa_finish_access(pSrcPixmap, 0);

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
        fake_exa_prepare_access(pMask, 0);
    }

    fake_exa_prepare_access(pSrc, 0);
    fake_exa_prepare_access(pDst, 0);

    fbComposite(op, pSrcPicture, pMaskPicture, pDstPicture,
                srcX, srcY, maskX, maskY, dstX, dstY, width, height);

    fake_exa_finish_access(pDst, 0);
    fake_exa_finish_access(pSrc, 0);

    if (pMask)
    {
        fake_exa_finish_access(pMask, 0);
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


static void fake_exa_wait_marker(ScreenPtr pScreen, int marker)
{
    // TODO:
}

/**
 * WaitMarker() waits for all rendering before the given marker to have
 * completed.  If the driver does not implement MarkSync(), marker is
 * meaningless, and all rendering by the hardware should be completed before
 * WaitMarker() returns.
 *
 * Note that drivers should call exaWaitSync() to wait for all acceleration
 * to finish, as otherwise EXA will be unaware of the driver having
 * synchronized, resulting in excessive WaitMarker() calls.
 *
 * WaitMarker() is required of all drivers.
 */
static int fake_exa_mark_sync(ScreenPtr pScreen)
{
    // TODO: return latest request(marker).
    return 0;
}

static void fake_exa_destroy_pixmap(ScreenPtr pScreen, void *driverPriv)
{
    struct exa_pixmap_priv *pPriv = (struct exa_pixmap_priv *) driverPriv;

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
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);

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

static void *fake_exa_create_pixmap2(ScreenPtr pScreen,
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
static Bool fake_exa_pixmap_is_offscreen(PixmapPtr pPixmap)
{
    //
    // offscreen means in 'gpu accessible memory', not that it's off the
    // visible screen.  We currently have no special constraints,
    // since fake exa has a flat memory model (no separate GPU memory).
    // If individual EXA implementation has additional constraints,
    // like buffer size or mapping in GPU MMU, it should wrap this function.
    //
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);

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


//////////////////////////////////////////////////////////////////////
//   This two is for PRIME and Reverse Prime, not tested ...
//////////////////////////////////////////////////////////////////////

static Bool ms_exa_back_pixmap_from_fd(PixmapPtr pixmap,
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

    ret = ls_exa_set_pixmap_bo(pScrn, pixmap, bo, TRUE);
    if (ret == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                        "%s: ms_exa_set_pixmap_bo failed\n", __func__);

        dumb_bo_destroy(pDrmMode->fd, bo);
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////////
//                  this guy do the real necessary initial job
////////////////////////////////////////////////////////////////////////////////

Bool gsgpu_setup_exa(ScrnInfoPtr pScrn, ExaDriverPtr pExaDrv)
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

    pExaDrv->WaitMarker = fake_exa_wait_marker;
    pExaDrv->MarkSync = fake_exa_mark_sync;
    pExaDrv->DestroyPixmap = fake_exa_destroy_pixmap;
    pExaDrv->CreatePixmap2 = fake_exa_create_pixmap2;
    pExaDrv->PrepareAccess = fake_exa_prepare_access;
    pExaDrv->FinishAccess = fake_exa_finish_access;
    pExaDrv->PixmapIsOffscreen = fake_exa_pixmap_is_offscreen;


    if (1)
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