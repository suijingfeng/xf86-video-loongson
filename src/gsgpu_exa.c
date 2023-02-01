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

#include <fb.h>
#include "driver.h"
#include "dumb_bo.h"

#include "loongson_buffer.h"
#include "loongson_options.h"
#include "loongson_pixmap.h"
#include "loongson_debug.h"
#include "gsgpu_dri3.h"
#include "gsgpu_exa.h"
#include "gsgpu_bo_helper.h"
#include "gsgpu_resolve.h"
#include "loongson_blt.h"

#define GSGPU_BO_ALIGN_SIZE (16 * 1024)

static struct ms_exa_prepare_args gsgpu_exa_prepare_args = {{0}};

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

static Bool gsgpu_exa_prepare_access(PixmapPtr pPix, int index)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPix);
    int ret;

    if (pPix->devPrivate.ptr)
    {
        // xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
        //            "%s: already prepared\n", __func__);

        return TRUE;
    }

    if (priv->bo)
    {
        ret = dumb_bo_map(pDrmMode->fd, priv->bo);
        if (ret)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "%s: dumb bo map failed: %s, ret=%d\n",
                       __func__, strerror(errno), ret);
            return FALSE;
        }

        DEBUG_MSG("%s: pixmap(%p) is a dumb\n", __func__, pPix);
        if (pDrmMode->shadow_fb)
            pPix->devPrivate.ptr = pDrmMode->shadow_fb;
        else
            pPix->devPrivate.ptr = dumb_bo_cpu_addr(priv->bo);
        priv->is_mapped = TRUE;
        return TRUE;
    }

    if (priv->gbo)
    {
        gsgpu_bo_cpu_map(priv->gbo, &pPix->devPrivate.ptr);
        priv->is_mapped = TRUE;
        return TRUE;
    }

    if (priv->pBuf)
    {
        pPix->devPrivate.ptr = priv->pBuf->pDat;
        priv->is_mapped = TRUE;
        return TRUE;
    }

    return FALSE;
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
static void gsgpu_exa_finish_access(PixmapPtr pPixmap, int index)
{
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);

    if (!priv)
        return;

    /* recongize that if a bo is dumb or don't have a priv,
     * it is likely that it is front bo or shadow of front bo,
     * x server will access it through its life time, no need
     * to unmap it.
     */

/*
    if (priv->bo)
    {
        dumb_bo_unmap(priv->bo);
        return;
    }
*/

    if (priv->gbo)
    {
        gsgpu_bo_cpu_unmap(priv->gbo);
        priv->is_mapped = FALSE;
    }

    /* Don't worry, prepare access will re-assign this */
    pPixmap->devPrivate.ptr = NULL;
}


static void
gsgpu_resolve_n_to_n(DrawablePtr pSrcDrawable,
                     DrawablePtr pDstDrawable,
                     GCPtr pGC,
                     BoxPtr pbox,
                     int nbox,
                     int dx,
                     int dy,
                     Bool reverse,
                     Bool upsidedown,
                     Pixel bitplane,
                     void *closure)
{
    FbBits *src;
    FbStride srcStride;
    int srcBpp;
    int srcXoff, srcYoff;
    FbBits *dst;
    FbStride dstStride;
    int dstBpp;
    int dstXoff, dstYoff;

    fbGetDrawable(pSrcDrawable, src, srcStride, srcBpp, srcXoff, srcYoff);
    fbGetDrawable(pDstDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

    while (nbox--)
    {
        lsx_resolve_gsgpu_tile_4x4(src,
                                   dst,
                                   srcStride,
                                   dstStride,
                                   srcBpp,
                                   dstBpp,
                                   (pbox->x1 + dx + srcXoff),
                                   (pbox->y1 + dy + srcYoff),
                                   (pbox->x1 + dstXoff),
                                   (pbox->y1 + dstYoff),
                                   (pbox->x2 - pbox->x1),
                                   (pbox->y2 - pbox->y1));
        pbox++;
    }
}

static void
swCopyNtoN(DrawablePtr pSrcDrawable,
           DrawablePtr pDstDrawable,
           GCPtr pGC,
           BoxPtr pbox,
           int nbox,
           int dx,
           int dy, Bool reverse, Bool upsidedown, Pixel bitplane, void *closure)
{
    CARD8 alu = pGC ? pGC->alu : GXcopy;
    FbBits pm = pGC ? fbGetGCPrivate(pGC)->pm : FB_ALLONES;
    FbBits *src;
    FbStride srcStride;
    int srcBpp;
    int srcXoff, srcYoff;
    FbBits *dst;
    FbStride dstStride;
    int dstBpp;
    int dstXoff, dstYoff;

    fbGetDrawable(pSrcDrawable, src, srcStride, srcBpp, srcXoff, srcYoff);
    fbGetDrawable(pDstDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

    while (nbox--)
    {
        if (pm == FB_ALLONES && alu == GXcopy && !reverse && !upsidedown)
        {
            if (!pixman_blt(
                 (uint32_t *) src, (uint32_t *) dst, srcStride, dstStride,
                 srcBpp, dstBpp, (pbox->x1 + dx + srcXoff),
                 (pbox->y1 + dy + srcYoff), (pbox->x1 + dstXoff),
                 (pbox->y1 + dstYoff), (pbox->x2 - pbox->x1),
                 (pbox->y2 - pbox->y1)))
                goto fallback;
            else
                goto next;
        }

fallback:

        DEBUG_MSG("%s: fallback to fbBlt, srcBpp: %d, dstBpp: %d\n",
                  __func__, srcBpp, dstBpp);

        fbBlt(src + (pbox->y1 + dy + srcYoff) * srcStride,
              srcStride,
              (pbox->x1 + dx + srcXoff) * srcBpp,
              dst + (pbox->y1 + dstYoff) * dstStride,
              dstStride,
              (pbox->x1 + dstXoff) * dstBpp,
              (pbox->x2 - pbox->x1) * dstBpp,
              (pbox->y2 - pbox->y1), alu, pm, dstBpp, reverse, upsidedown);
next:
        pbox++;
    }

    fbFinishAccess(pDstDrawable);
    fbFinishAccess(pSrcDrawable);
}

/////////////////////////////////////////////////////////////////////////

static Bool PrepareSolidFail(PixmapPtr pPixmap, int alu, Pixel planemask,
        Pixel fill_colour)
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
    gsgpu_exa_prepare_args.solid.alu = alu;
    gsgpu_exa_prepare_args.solid.planemask = planemask;
    gsgpu_exa_prepare_args.solid.fg = fg;

    return TRUE;
}


static void ms_exa_solid(PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
    ScreenPtr screen = pPixmap->drawable.pScreen;
    GCPtr gc = GetScratchGC(pPixmap->drawable.depth, screen);
    ChangeGCVal val[3];

    val[0].val = gsgpu_exa_prepare_args.solid.alu;
    val[1].val = gsgpu_exa_prepare_args.solid.planemask;
    val[2].val = gsgpu_exa_prepare_args.solid.fg;
    ChangeGC(NullClient, gc, GCFunction | GCPlaneMask | GCForeground, val);
    ValidateGC(&pPixmap->drawable, gc);

    gsgpu_exa_prepare_access(pPixmap, 0);
    fbFill(&pPixmap->drawable, gc, x1, y1, x2 - x1, y2 - y1);
    gsgpu_exa_finish_access(pPixmap, 0);

    FreeScratchGC(gc);
}


static void ms_exa_solid_done(PixmapPtr pPixmap)
{

}

//////////////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////////////////
/////////////     copy    ////////////////////////////////////////////////

/**
 * PrepareCopy() sets up the driver for doing a copy within video memory.
 *
 * @param pSrcPixmap source pixmap
 * @param pDstPixmap destination pixmap
 * @param dx X copy direction
 * @param dy Y copy direction
 * @param alu raster operation
 * @param planemask write mask for the fill
 *
 * This call should set up the driver for doing a series of copies from the
 * the pSrcPixmap to the pDstPixmap.  The dx flag will be positive if the
 * hardware should do the copy from the left to the right, and dy will be
 * positive if the copy should be done from the top to the bottom.  This
 * is to deal with self-overlapping copies when pSrcPixmap == pDstPixmap.
 * If your hardware can only support blits that are (left to right, top to
 * bottom) or (right to left, bottom to top), then you should set
 * #EXA_TWO_BITBLT_DIRECTIONS, and EXA will break down Copy operations to
 * ones that meet those requirements.  The alu raster op is one of the GX*
 * graphics functions listed in X.h, and typically maps to a similar
 * single-byte "ROP" setting in all hardware.  The planemask controls which
 * bits of the destination should be affected, and will only represent the
 * bits up to the depth of pPixmap.
 *
 * Note that many drivers will need to store some of the data in the driver
 * private record, for sending to the hardware with each drawing command.
 *
 * The PrepareCopy() call is required of all drivers, but it may fail for any
 * reason.  Failure results in a fallback to software rendering.
 */
static Bool gsgpu_exa_prepare_copy(PixmapPtr pSrcPixmap,
                                   PixmapPtr pDstPixmap,
                                   int dx, int dy, int alu, Pixel planemask)
{
    struct exa_pixmap_priv *pSrcPriv = exaGetPixmapDriverPrivate(pSrcPixmap);

    if (!pSrcPriv)
        return FALSE;

    gsgpu_exa_prepare_args.copy.pSrcPixmap = pSrcPixmap;
    gsgpu_exa_prepare_args.copy.alu = alu;
    gsgpu_exa_prepare_args.copy.planemask = planemask;

    if (pSrcPriv->tiling_info == GSGPU_SURF_MODE_TILED4)
        return TRUE;

    if (pSrcPriv->tiling_info == GSGPU_SURF_MODE_LINEAR)
        return TRUE;

    return FALSE;
}

/**
 * Copy() performs a copy set up in the last PrepareCopy call.
 *
 * @param pDstPixmap destination pixmap
 * @param srcX source X coordinate
 * @param srcY source Y coordinate
 * @param dstX destination X coordinate
 * @param dstY destination Y coordinate
 * @param width width of the rectangle to be copied
 * @param height height of the rectangle to be copied.
 *
 * Performs the copy set up by the last PrepareCopy() call, copying the
 * rectangle from (srcX, srcY) to (srcX + width, srcY + width) in the source
 * pixmap to the same-sized rectangle at (dstX, dstY) in the destination
 * pixmap.  Those rectangles may overlap in memory, if
 * pSrcPixmap == pDstPixmap.  Note that this call does not receive the
 * pSrcPixmap as an argument -- if it's needed in this function, it should
 * be stored in the driver private during PrepareCopy().  As with Solid(),
 * the coordinates are in the coordinate space of each pixmap, so the driver
 * will need to set up source and destination pitches and offsets from those
 * pixmaps, probably using exaGetPixmapOffset() and exaGetPixmapPitch().
 *
 * This call is required if PrepareCopy ever succeeds.
 */
static void gsgpu_exa_do_copy(PixmapPtr pDstPixmap,
                              int srcX, int srcY,
                              int dstX, int dstY,
                              int width, int height)
{
    PixmapPtr pSrcPixmap = gsgpu_exa_prepare_args.copy.pSrcPixmap;
    struct exa_pixmap_priv *pSrcPriv = exaGetPixmapDriverPrivate(pSrcPixmap);
    ScreenPtr pScreen = pDstPixmap->drawable.pScreen;
    miCopyProc pFnCopyProc = swCopyNtoN;
    ChangeGCVal val[2];
    GCPtr gc;

#if defined(GSGPU_DEBUG_EXA_COPY)
    xf86Msg(X_WARNING, "pSrcPixmap(%p): srcX=%d, srcY=%d, dstX=%d, dstY=%d\n",
                        pSrcPixmap, srcX, srcY, dstX, dstY);

    xf86Msg(X_WARNING, "pDstPixmap(%p): %dx%d\n",
            pDstPixmap
            pDstPixmap->drawable.width,
            pDstPixmap->drawable.height);
#endif

    gc = GetScratchGC(pDstPixmap->drawable.depth, pScreen);

    val[0].val = gsgpu_exa_prepare_args.copy.alu;
    val[1].val = gsgpu_exa_prepare_args.copy.planemask;
    ChangeGC(NullClient, gc, GCFunction | GCPlaneMask, val);
    ValidateGC(&pDstPixmap->drawable, gc);

    gsgpu_exa_prepare_access(pSrcPixmap, 0);
    gsgpu_exa_prepare_access(pDstPixmap, 0);

    // TODO: Add resolve Tile8 support
    if (pSrcPriv->tiling_info == GSGPU_SURF_MODE_TILED4)
        pFnCopyProc = gsgpu_resolve_n_to_n;
    else if (pSrcPriv->tiling_info == GSGPU_SURF_MODE_LINEAR)
        pFnCopyProc = swCopyNtoN;

    miDoCopy(&pSrcPixmap->drawable, &pDstPixmap->drawable, gc,
             srcX, srcY, width, height, dstX, dstY, pFnCopyProc, 0, 0);

    gsgpu_exa_finish_access(pDstPixmap, 0);
    gsgpu_exa_finish_access(pSrcPixmap, 0);

    FreeScratchGC(gc);
}

static void gsgpu_exa_copy_done(PixmapPtr pPixmap)
{

}

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
    gsgpu_exa_prepare_args.composite.op = op;
    gsgpu_exa_prepare_args.composite.pSrcPicture = pSrcPicture;
    gsgpu_exa_prepare_args.composite.pMaskPicture = pMaskPicture;
    gsgpu_exa_prepare_args.composite.pDstPicture = pDstPicture;
    gsgpu_exa_prepare_args.composite.pSrc = pSrc;
    gsgpu_exa_prepare_args.composite.pMask = pMask;

    return TRUE;
}


static void ms_exa_composite(PixmapPtr pDst, int srcX, int srcY,
                 int maskX, int maskY, int dstX, int dstY,
                 int width, int height)
{
    PicturePtr pSrcPicture = gsgpu_exa_prepare_args.composite.pSrcPicture;
    PicturePtr pMaskPicture = gsgpu_exa_prepare_args.composite.pMaskPicture;
    PicturePtr pDstPicture = gsgpu_exa_prepare_args.composite.pDstPicture;
    PixmapPtr pSrc = gsgpu_exa_prepare_args.composite.pSrc;
    PixmapPtr pMask = gsgpu_exa_prepare_args.composite.pMask;
    int op = gsgpu_exa_prepare_args.composite.op;

    if (pMask)
    {
        gsgpu_exa_prepare_access(pMask, 0);
    }

    gsgpu_exa_prepare_access(pSrc, 0);
    gsgpu_exa_prepare_access(pDst, 0);

    fbComposite(op, pSrcPicture, pMaskPicture, pDstPicture,
                srcX, srcY, maskX, maskY, dstX, dstY, width, height);

    gsgpu_exa_finish_access(pDst, 0);
    gsgpu_exa_finish_access(pSrc, 0);

    if (pMask)
    {
        gsgpu_exa_finish_access(pMask, 0);
    }
}

static void ms_exa_composite_done(PixmapPtr pPixmap)
{

}


//////////////////////////////////////////////////////////////////////////

/**
 * UploadToScreen() loads a rectangle of data from src into pDst.
 *
 * @param pDst destination pixmap
 * @param x destination X coordinate.
 * @param y destination Y coordinate
 * @param width width of the rectangle to be copied
 * @param height height of the rectangle to be copied
 * @param src pointer to the beginning of the source data
 * @param src_pitch pitch (in bytes) of the lines of source data.
 *
 * UploadToScreen() copies data in system memory beginning at src (with
 * pitch src_pitch) into the destination pixmap from (x, y) to
 * (x + width, y + height).  This is typically done with hostdata uploads,
 * where the CPU sets up a blit command on the hardware with instructions
 * that the blit data will be fed through some sort of aperture on the card.
 *
 * If UploadToScreen() is performed asynchronously, it is up to the driver
 * to call exaMarkSync().  This is in contrast to most other acceleration
 * calls in EXA.
 *
 * UploadToScreen() can aid in pixmap migration, but is most important for
 * the performance of exaGlyphs() (antialiased font drawing) by allowing
 * pipelining of data uploads, avoiding a sync of the card after each glyph.
 *
 * @return TRUE if the driver successfully uploaded the data.  FALSE
 * indicates that EXA should fall back to doing the upload in software.
 *
 * UploadToScreen() is not required, but is recommended if Composite
 * acceleration is supported.
 */
static Bool gsgpu_exa_upload_to_screen(PixmapPtr pPix,
                                       int x,
                                       int y,
                                       int w,
                                       int h,
                                       char *pSrc,
                                       int src_stride)
{
    char *pDst;
    unsigned int dst_stride;
    unsigned int len;
    int cpp;
    int i;

    cpp = (pPix->drawable.bitsPerPixel + 7) / 8;

    gsgpu_exa_prepare_access(pPix, 0);

    pDst = (char *)pPix->devPrivate.ptr;

    dst_stride = exaGetPixmapPitch(pPix);

    DEBUG_MSG("%s: (%dx%d) surface at (%d, %d) stride=%d, src_stride=%d\n",
               __func__, w, h, x, y, dst_stride, src_stride);

    pDst += y * dst_stride + x * cpp;
    len = w * cpp;
    for (i = 0; i < h; ++i)
    {
        loongson_blt((uint8_t *)pDst, (uint8_t *)pSrc, len);
        pDst += dst_stride;
        pSrc += src_stride;
    }

    gsgpu_exa_finish_access(pPix, 0);

    return TRUE;
}

static Bool gsgpu_exa_download_from_screen(PixmapPtr pPix,
                                           int x,
                                           int y,
                                           int w,
                                           int h,
                                           char *pDst,
                                           int dst_stride)
{
    char *pSrc;
    unsigned int src_stride;
    unsigned int len;
    int cpp;
    int i;

    cpp = (pPix->drawable.bitsPerPixel + 7) / 8;

    gsgpu_exa_prepare_access(pPix, 0);

    pSrc = (char *)pPix->devPrivate.ptr;

    src_stride = exaGetPixmapPitch(pPix);

    DEBUG_MSG("%s: (%dx%d) surface at (%d, %d) stride=%d, src_stride=%d\n",
               __func__, w, h, x, y, dst_stride, src_stride);

    pSrc += y * src_stride + x * cpp;
    len = w * cpp;
    for (i = 0; i < h; ++i)
    {
        loongson_blt((uint8_t *)pDst, (uint8_t *)pSrc, len);
        pDst += dst_stride;
        pSrc += src_stride;
    }

    gsgpu_exa_finish_access(pPix, 0);

    return TRUE;
}


static void gsgpu_exa_wait_marker(ScreenPtr pScreen, int marker)
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
static int gsgpu_exa_mark_sync(ScreenPtr pScreen)
{
    // TODO: return latest request(marker).
    return 0;
}

static void gsgpu_exa_destroy_pixmap(ScreenPtr pScreen, void *driverPriv)
{
    struct exa_pixmap_priv *pPriv = (struct exa_pixmap_priv *) driverPriv;

    TRACE_ENTER();

    if (!pPriv)
        return;

    if (pPriv->fd > 0)
    {
        close(pPriv->fd);
        pPriv->fd = 0;
    }

    if (pPriv->bo)
    {
        return LS_DestroyDumbPixmap(pScreen, driverPriv);
        pPriv->bo = NULL;
    }

    if (pPriv->gbo)
    {
        gsgpu_bo_free(pPriv->gbo);
        pPriv->gbo = NULL;
    }

    if (pPriv->pBuf)
    {
        LS_DestroyExaPixmap(pScreen, driverPriv);
        pPriv->pBuf = NULL;
    }

    free(pPriv);

    TRACE_EXIT();
}

/* returns pitch alignment in bytes */
static int gsgpu_bo_compute_pitch(int width, int bitsPerPixel)
{
    int pitch = width * ((bitsPerPixel + 7) / 8);

    return LOONGSON_ALIGN(pitch, LOONGSON_DUMB_BO_ALIGN);
}

static void *gsgpu_create_pixmap(ScreenPtr pScreen,
                                 int width,
                                 int height,
                                 int depth,
                                 int usage_hint,
                                 int bitsPerPixel,
                                 int *new_pitch)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct gsgpu_device *gsgpu = lsp->gsgpu;
    struct exa_pixmap_priv *priv;
    struct gsgpu_bo *gbo;
    unsigned pitch;

    priv = calloc(1, sizeof(struct exa_pixmap_priv));
    if (!priv)
    {
        return NullPixmap;
    }

    priv->width = width;
    priv->height = height;
    priv->usage_hint = usage_hint;
    priv->is_gtt = TRUE;
    priv->is_dumb = FALSE;
    priv->is_mapped = FALSE;
    priv->tiling_info = 0;

    if ((0 == width) || (0 == height))
    {
        return priv;
    }

    pitch = gsgpu_bo_compute_pitch(width, bitsPerPixel);

    gbo = gsgpu_bo_create(gsgpu, pitch * height, GSGPU_BO_ALIGN_SIZE, GSGPU_GEM_DOMAIN_GTT);
    if (!gbo)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "gsgpu: failed to allocate bo for %dx%d %dbpp\n",
                   width, height, bitsPerPixel);

        free(priv);
        return NullPixmap;
    }

    priv->gbo = gbo;
    priv->pitch = pitch;

    if (new_pitch)
        *new_pitch = pitch;

    return priv;
}

/* Hooks to allow driver to its own pixmap memory management */

static void *gsgpu_exa_create_pixmap(ScreenPtr pScreen,
                                     int width,
                                     int height,
                                     int depth,
                                     int usage_hint,
                                     int bitsPerPixel,
                                     int *new_fb_pitch)
{
    if (usage_hint == CREATE_PIXMAP_USAGE_SCANOUT)
    {
        xf86Msg(X_INFO, "gsgpu: allocate %dx%d dumb bo\n", width, height);

        return LS_CreateDumbPixmap(pScreen, width, height, depth,
                                   usage_hint, bitsPerPixel, new_fb_pitch);
    }

    if (1)
    {
        return gsgpu_create_pixmap(pScreen, width, height, depth,
                                   usage_hint, bitsPerPixel, new_fb_pitch);
    }

    return LS_CreateExaPixmap(pScreen, width, height, depth,
                              usage_hint, bitsPerPixel, new_fb_pitch);
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
static Bool gsgpu_exa_pixmap_is_offscreen(PixmapPtr pPixmap)
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

    if (priv->bo)
    {
        return TRUE;
    }

    if (priv->gbo)
    {
        return TRUE;
    }

    if (priv->pBuf)
    {
        return TRUE;
    }

    return FALSE;
}


Bool gsgpu_setup_exa(ScrnInfoPtr pScrn, ExaDriverPtr pExaDrv)
{
    TRACE_ENTER();

    pExaDrv->exa_major = EXA_VERSION_MAJOR;
    pExaDrv->exa_minor = EXA_VERSION_MINOR;

    pExaDrv->pixmapOffsetAlign = 16;
    pExaDrv->pixmapPitchAlign = LOONGSON_DUMB_BO_ALIGN;

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
    pExaDrv->PrepareCopy = gsgpu_exa_prepare_copy;
    pExaDrv->Copy = gsgpu_exa_do_copy;
    pExaDrv->DoneCopy = gsgpu_exa_copy_done;

    //// composite
    pExaDrv->CheckComposite = ms_exa_check_composite;
    pExaDrv->PrepareComposite = ms_exa_prepare_composite;
    pExaDrv->Composite = ms_exa_composite;
    pExaDrv->DoneComposite = ms_exa_composite_done;

    pExaDrv->UploadToScreen = gsgpu_exa_upload_to_screen;
    pExaDrv->DownloadFromScreen = gsgpu_exa_download_from_screen;

    pExaDrv->WaitMarker = gsgpu_exa_wait_marker;
    pExaDrv->MarkSync = gsgpu_exa_mark_sync;
    pExaDrv->DestroyPixmap = gsgpu_exa_destroy_pixmap;
    pExaDrv->CreatePixmap2 = gsgpu_exa_create_pixmap;
    pExaDrv->PrepareAccess = gsgpu_exa_prepare_access;
    pExaDrv->FinishAccess = gsgpu_exa_finish_access;
    pExaDrv->PixmapIsOffscreen = gsgpu_exa_pixmap_is_offscreen;

    if (1)
    {
        /* fallbacks for software operations */
        pExaDrv->PrepareSolid = PrepareSolidFail;
        pExaDrv->CheckComposite = CheckCompositeFail;
        pExaDrv->PrepareComposite = PrepareCompositeFail;
    }

    TRACE_EXIT();

    return TRUE;
}
