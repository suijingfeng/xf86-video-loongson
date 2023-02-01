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
#include <fcntl.h>
#include <xf86.h>
#include <exa.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include <etnaviv_drmif.h>
#include "driver.h"

#include "etnaviv_exa.h"
#include "etnaviv_resolve.h"
#include "loongson_buffer.h"
#include "loongson_options.h"
#include "loongson_pixmap.h"
#include "loongson_debug.h"

#include "common.xml.h"

#define ETNAVIV_3D_HEIGHT_ALIGN               8

static unsigned int etnaviv_align_pitch(unsigned width, unsigned bpp)
{
    unsigned pitch = width * ((bpp + 7) / 8);

    /* GC320 and GC600 needs pitch aligned to 16 */
    /* supertile needs the pitch aligned to 64 pixel(256 bytes) */
    return LOONGSON_ALIGN(pitch, 256);
}

static unsigned int etnaviv_align_height(unsigned int height)
{
    return LOONGSON_ALIGN(height, ETNAVIV_3D_HEIGHT_ALIGN);
}

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

static Bool etnaviv_exa_prepare_access(PixmapPtr pPix, int index)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPix);
    void *ptr = NULL;

    if (pPix->devPrivate.ptr)
    {
        DEBUG_MSG("Pixmap %p: already prepared\n", pPix);

        return TRUE;
    }

    if (!priv)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s: priv is NULL\n", __func__);
        return FALSE;
    }

    if (priv->bo)
    {
        int ret = dumb_bo_map(pDrmMode->fd, priv->bo);
        if (ret)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "%s: dumb bo map failed: %s, ret=%d\n",
                       __func__, strerror(errno), ret);
            return FALSE;
        }
        if (pDrmMode->shadow_fb)
            pPix->devPrivate.ptr = pDrmMode->shadow_fb;
        else
            pPix->devPrivate.ptr = dumb_bo_cpu_addr(priv->bo);
        priv->is_mapped = TRUE;
        return TRUE;
    }

    if (priv->etna_bo)
    {
        ptr = etna_bo_map(priv->etna_bo);
        if (!ptr)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "%s: etna_bo map failed: %s\n",
                       __func__, strerror(errno));
            return FALSE;
        }
        pPix->devPrivate.ptr = ptr;
        priv->is_mapped = TRUE;
        return TRUE;
    }

    if (priv->pBuf)
    {
        pPix->devPrivate.ptr = priv->pBuf->pDat;
        priv->is_mapped = TRUE;
        return TRUE;
    }

    /* When !NULL, devPrivate.ptr points to the raw pixel data */
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
static void etnaviv_exa_finish_access(PixmapPtr pPixmap, int index)
{
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);

    if (!priv)
        return;

    if (priv && priv->bo)
    {
        // dumb_bo_unmap(priv->bo);
    }

    pPixmap->devPrivate.ptr = NULL;
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

    etnaviv_exa_prepare_access(pPixmap, 0);
    fbFill(&pPixmap->drawable, gc, x1, y1, x2 - x1, y2 - y1);
    etnaviv_exa_finish_access(pPixmap, 0);

    FreeScratchGC(gc);
}


static void ms_exa_solid_done(PixmapPtr pPixmap)
{

}


//////////////////////////////////////////////////////////////////////////
/////////////     copy    ////////////////////////////////////////////////

/*
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
static Bool etnaviv_exa_prepare_copy(PixmapPtr pSrcPixmap,
                                     PixmapPtr pDstPixmap,
                                     int dx,
                                     int dy,
                                     int alu,
                                     Pixel planemask)
{
    struct exa_pixmap_priv *pSrcPriv = exaGetPixmapDriverPrivate(pSrcPixmap);

    if (!pSrcPriv)
        return FALSE;

    exa_prepare_args.copy.pSrcPixmap = pSrcPixmap;
    exa_prepare_args.copy.alu = alu;
    exa_prepare_args.copy.planemask = planemask;

    if (pSrcPriv->tiling_info == DRM_FORMAT_MOD_VIVANTE_TILED)
        return TRUE;

    if (pSrcPriv->tiling_info == DRM_FORMAT_MOD_VIVANTE_SUPER_TILED)
        return TRUE;

    return FALSE;
}


static void
swCopyNtoN(DrawablePtr pSrcDrawable,
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
            if (!pixman_blt
                ((uint32_t *) src, (uint32_t *) dst, srcStride, dstStride,
                 srcBpp, dstBpp, (pbox->x1 + dx + srcXoff),
                 (pbox->y1 + dy + srcYoff), (pbox->x1 + dstXoff),
                 (pbox->y1 + dstYoff), (pbox->x2 - pbox->x1),
                 (pbox->y2 - pbox->y1)))
                goto fallback;
            else
                goto next;
        }
 fallback:
        fbBlt(src + (pbox->y1 + dy + srcYoff) * srcStride,
              srcStride,
              (pbox->x1 + dx + srcXoff) * srcBpp,
              dst + (pbox->y1 + dstYoff) * dstStride,
              dstStride,
              (pbox->x1 + dstXoff) * dstBpp,
              (pbox->x2 - pbox->x1) * dstBpp,
              (pbox->y2 - pbox->y1),
              alu, pm, dstBpp, reverse, upsidedown);
 next:
        pbox++;
    }

    fbFinishAccess(pDstDrawable);
    fbFinishAccess(pSrcDrawable);

}

static void
etnaviv_blit_tile_n_to_n(DrawablePtr pSrcDrawable,
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
    FbBits *pSrc;
    FbStride srcStride;
    int srcXoff, srcYoff;
    FbBits *pDst;
    FbStride dstStride;
    int dstXoff, dstYoff;
    int srcBpp;
    int dstBpp;

    TRACE_ENTER();

    fbGetDrawable(pSrcDrawable, pSrc, srcStride, srcBpp, srcXoff, srcYoff);
    fbGetDrawable(pDstDrawable, pDst, dstStride, dstBpp, dstXoff, dstYoff);

    while (nbox--)
    {
        lsx_resolve_etnaviv_tile_4x4(pSrc,
                                     pDst,
                                     srcStride,
                                     dstStride,
                                     (pbox->x1 + dx + srcXoff),
                                     (pbox->y1 + dy + srcYoff),
                                     (pbox->x1 + dstXoff),
                                     (pbox->y1 + dstYoff),
                                     (pbox->x2 - pbox->x1),
                                     (pbox->y2 - pbox->y1));
        pbox++;
    }

    TRACE_EXIT();
}

static void
etnaviv_blit_supertile_n_to_n(DrawablePtr pSrcDrawable,
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
    FbBits *pSrc;
    FbStride srcStride;
    int srcXoff, srcYoff;
    FbBits *pDst;
    FbStride dstStride;
    int dstXoff, dstYoff;
    int srcBpp;
    int dstBpp;

    TRACE_ENTER();

    fbGetDrawable(pSrcDrawable, pSrc, srcStride, srcBpp, srcXoff, srcYoff);
    fbGetDrawable(pDstDrawable, pDst, dstStride, dstBpp, dstXoff, dstYoff);

    while (nbox--)
    {
#if HAVE_LSX
        etnaviv_supertile_to_linear_lsx(pSrc,
                                        pDst,
                                        srcStride,
                                        dstStride,
                                        (pbox->x1 + dx + srcXoff),
                                        (pbox->y1 + dy + srcYoff),
                                        (pbox->x1 + dstXoff),
                                        (pbox->y1 + dstYoff),
                                        (pbox->x2 - pbox->x1),
                                        (pbox->y2 - pbox->y1));
#elif HAVE_MSA
        etnaviv_supertile_to_linear_msa(pSrc,
                                        pDst,
                                        srcStride,
                                        dstStride,
                                        (pbox->x1 + dx + srcXoff),
                                        (pbox->y1 + dy + srcYoff),
                                        (pbox->x1 + dstXoff),
                                        (pbox->y1 + dstYoff),
                                        (pbox->x2 - pbox->x1),
                                        (pbox->y2 - pbox->y1));
#else
        etnaviv_supertile_to_linear_generic(pSrc,
                                            pDst,
                                            srcStride,
                                            dstStride,
                                            (pbox->x1 + dx + srcXoff),
                                            (pbox->y1 + dy + srcYoff),
                                            (pbox->x1 + dstXoff),
                                            (pbox->y1 + dstYoff),
                                            (pbox->x2 - pbox->x1),
                                            (pbox->y2 - pbox->y1));
#endif
        pbox++;
    }

    TRACE_EXIT();
}

static void etnaviv_exa_do_copy(PixmapPtr pDstPixmap,
                                int srcX,
                                int srcY,
                                int dstX,
                                int dstY,
                                int width,
                                int height)
{
    PixmapPtr pSrcPixmap = exa_prepare_args.copy.pSrcPixmap;
    ScreenPtr screen = pDstPixmap->drawable.pScreen;
    struct exa_pixmap_priv *src_priv;
    ChangeGCVal val[2];
    GCPtr gc;

    gc = GetScratchGC(pDstPixmap->drawable.depth, screen);

    val[0].val = exa_prepare_args.copy.alu;
    val[1].val = exa_prepare_args.copy.planemask;
    ChangeGC(NullClient, gc, GCFunction | GCPlaneMask, val);
    ValidateGC(&pDstPixmap->drawable, gc);

    etnaviv_exa_prepare_access(pSrcPixmap, 0);
    etnaviv_exa_prepare_access(pDstPixmap, 0);

    src_priv = exaGetPixmapDriverPrivate(pSrcPixmap);

    /* TODO: check its format */
    if (src_priv->tiling_info == DRM_FORMAT_MOD_VIVANTE_TILED)
    {
        miDoCopy(&pSrcPixmap->drawable,
                 &pDstPixmap->drawable,
                 gc,
                 srcX, srcY,
                 width, height,
                 dstX, dstY,
                 etnaviv_blit_tile_n_to_n,
                 0, 0);
    }
    else if (src_priv->tiling_info == DRM_FORMAT_MOD_VIVANTE_SUPER_TILED)
    {
        struct etna_bo *etna_bo;

        etna_bo = src_priv->etna_bo;
        etna_bo_cpu_prep(etna_bo, DRM_ETNA_PREP_READ);

        miDoCopy(&pSrcPixmap->drawable,
                 &pDstPixmap->drawable,
                 gc,
                 srcX, srcY,
                 width, height,
                 dstX, dstY,
                 etnaviv_blit_supertile_n_to_n,
                 0, 0);

        etna_bo_cpu_fini(etna_bo);
    }
    else
    {
        miDoCopy(&pSrcPixmap->drawable,
                 &pDstPixmap->drawable,
                 gc,
                 srcX, srcY,
                 width, height,
                 dstX, dstY,
                 swCopyNtoN,
                 0, 0);
    }

    etnaviv_exa_finish_access(pDstPixmap, 0);
    etnaviv_exa_finish_access(pSrcPixmap, 0);

    FreeScratchGC(gc);
}


static void etnaviv_exa_copy_done(PixmapPtr pPixmap)
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
        etnaviv_exa_prepare_access(pMask, 0);
    }

    etnaviv_exa_prepare_access(pSrc, 0);
    etnaviv_exa_prepare_access(pDst, 0);

    fbComposite(op, pSrcPicture, pMaskPicture, pDstPicture,
                srcX, srcY, maskX, maskY, dstX, dstY, width, height);

    etnaviv_exa_finish_access(pDst, 0);
    etnaviv_exa_finish_access(pSrc, 0);

    if (pMask)
    {
        etnaviv_exa_finish_access(pMask, 0);
    }
}

static void ms_exa_composite_done(PixmapPtr pPixmap)
{

}

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

static Bool
etnaviv_exa_upload_to_screen(PixmapPtr pPix,
                             int x, int y, int w, int h,
                             char *pSrc, int src_pitch)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPix);
    char *pDst;
    unsigned int dst_stride;
    unsigned int len;
    int cpp;
    int i;
    Bool ret;

    if (!priv)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s: priv is NULL\n", __func__);
        return FALSE;
    }

    cpp = (pPix->drawable.bitsPerPixel + 7) / 8;

    ret = etnaviv_exa_prepare_access(pPix, 0);
    if (ret == FALSE)
        return FALSE;

    pDst = (char *)pPix->devPrivate.ptr;

    dst_stride = exaGetPixmapPitch(pPix);

/*
    xf86Msg(X_INFO, "%s: (%dx%d) surface at (%d, %d)\n",
            __func__, w, h, x, y);

    xf86Msg(X_INFO, "%s: stride=%d, src_pitch=%d, mDestAddr is 0x%p\n",
            __func__, dst_stride, src_pitch, pDst);
*/
    pDst += y * dst_stride + x * cpp;
    len = w * cpp;
    for (i = 0; i < h; ++i)
    {
        memcpy(pDst, pSrc, len);
        pDst += dst_stride;
        pSrc += src_pitch;
    }

    etnaviv_exa_finish_access(pPix, 0);

    return TRUE;
}

/**
 * DownloadFromScreen() loads a rectangle of data from pSrc into dst
 *
 * @param pSrc source pixmap
 * @param x source X coordinate.
 * @param y source Y coordinate
 * @param width width of the rectangle to be copied
 * @param height height of the rectangle to be copied
 * @param dst pointer to the beginning of the destination data
 * @param dst_pitch pitch (in bytes) of the lines of destination data.
 *
 * DownloadFromScreen() copies data from offscreen memory in pSrc from
 * (x, y) to (x + width, y + height), to system memory starting at
 * dst (with pitch dst_pitch).  This would usually be done
 * using scatter-gather DMA, supported by a DRM call, or by blitting to AGP
 * and then synchronously reading from AGP.  Because the implementation
 * might be synchronous, EXA leaves it up to the driver to call
 * exaMarkSync() if DownloadFromScreen() was asynchronous.  This is in
 * contrast to most other acceleration calls in EXA.
 *
 * DownloadFromScreen() can aid in the largest bottleneck in pixmap
 * migration, which is the read from framebuffer when evicting pixmaps from
 * framebuffer memory.  Thus, it is highly recommended, even though
 * implementations are typically complicated.
 *
 * @return TRUE if the driver successfully downloaded the data.  FALSE
 * indicates that EXA should fall back to doing the download in software.
 *
 * DownloadFromScreen() is not required, but is highly recommended.
 */

/**
 * Does fake acceleration of DownloadFromScren using memcpy.
 */
static Bool
etnaviv_exa_download_from_screen(PixmapPtr pPix,
                                 int x, int y, int w, int h,
                                 char *pDst, int dst_stride)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPix);
    char *pSrc;
    unsigned int src_stride;
    unsigned int len;
    int cpp;
    int i;

    if (!priv)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s: priv is NULL\n", __func__);
        return FALSE;
    }

    cpp = (pPix->drawable.bitsPerPixel + 7) / 8;

    etnaviv_exa_prepare_access(pPix, 0);

    pSrc = (char *)pPix->devPrivate.ptr;

    src_stride = exaGetPixmapPitch(pPix);

/*
    xf86Msg(X_INFO, "%s: (%dx%d) surface at (%d, %d)\n",
            __func__, w, h, x, y);
*/

    pSrc += y * src_stride + x * cpp;
    len = w * cpp;
    for (i = 0; i < h; ++i)
    {
        memcpy(pDst, pSrc, len);
        pDst += dst_stride;
        pSrc += src_stride;
    }

    etnaviv_exa_finish_access(pPix, 0);

    return TRUE;
}

static void etnaviv_exa_wait_marker(ScreenPtr pScreen, int marker)
{
    // TODO:
}

static int etnaviv_exa_mark_sync(ScreenPtr pScreen)
{
    // TODO: return latest request(marker).
    return 0;
}

static void *etnaviv_create_pixmap(ScreenPtr pScreen,
                                   int width,
                                   int height,
                                   int depth,
                                   int usage_hint,
                                   int bitsPerPixel,
                                   int *new_fb_pitch)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct EtnavivRec *etnaviv = &lsp->etnaviv;
    struct exa_pixmap_priv *priv;
    struct etna_bo *etna_bo;
    unsigned pitch, size;

    priv = calloc(1, sizeof(struct exa_pixmap_priv));
    if (!priv)
    {
        return NullPixmap;
    }

    priv->width = width;
    priv->height = height;
    priv->usage_hint = usage_hint;

    if ((0 == width) || (0 == height))
    {
        return priv;
    }

    pitch = etnaviv_align_pitch(width, bitsPerPixel);
    size = pitch * etnaviv_align_height(height);

    etna_bo = etna_bo_new(etnaviv->dev, size, DRM_ETNA_GEM_CACHE_CACHED);
    if (!etna_bo)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "etnaviv: failed to allocate bo for %dx%d %dbpp\n",
                   width, height, bitsPerPixel);

        free(priv);
        return NullPixmap;
    }

    priv->etna_bo = etna_bo;
    priv->pitch = pitch;
    priv->is_mapped = FALSE;
    priv->is_dumb = FALSE;

    if (new_fb_pitch)
    {
        *new_fb_pitch = pitch;
    }

    return priv;
}


static void etnaviv_exa_destroy_pixmap(ScreenPtr pScreen, void *driverPriv)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct exa_pixmap_priv *priv = (struct exa_pixmap_priv *) driverPriv;

    if (!priv)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "etnaviv: priv is NULL\n");
        return;
    }

    if (priv->fd > 0)
    {
        drmClose(priv->fd);
        priv->fd = -1;
    }

    if (priv->etna_bo)
    {
        etna_bo_del(priv->etna_bo);
        priv->etna_bo = NULL;
    }

    if (priv->pBuf)
    {
        LS_DestroyExaPixmap(pScreen, driverPriv);
        priv->pBuf = NULL;
    }

    free(priv);
}

/* Hooks to allow driver to its own pixmap memory management */

static void *etnaviv_exa_create_pixmap(ScreenPtr pScreen,
                                       int width,
                                       int height,
                                       int depth,
                                       int usage_hint,
                                       int bitsPerPixel,
                                       int *new_fb_pitch)
{
    if (usage_hint == CREATE_PIXMAP_USAGE_SCANOUT)
    {
        xf86Msg(X_INFO, "etnaviv: allocate %dx%d dumb bo\n", width, height);

        return LS_CreateDumbPixmap(pScreen, width, height, depth,
                                   usage_hint, bitsPerPixel, new_fb_pitch);
    }

    if (1)
    {
        return etnaviv_create_pixmap(pScreen, width, height, depth,
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
static Bool etnaviv_is_offscreen_pixmap(PixmapPtr pPixmap)
{
    //
    // offscreen means in 'gpu accessible memory', not that it's off the
    // visible screen.  We currently have no special constraints,
    // since fake exa has a flat memory model (no separate GPU memory).
    // If individual EXA implementation has additional constraints,
    // like buffer size or mapping in GPU MMU, it should wrap this function.
    //
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);

    if (!priv)
    {
        xf86Msg(X_INFO, "%s:%d\n", __func__, __LINE__);
        return FALSE;
    }

    if (priv->bo)
    {
        return TRUE;
    }

    if (priv->etna_bo)
    {
        return TRUE;
    }

    return TRUE;
}

Bool etnaviv_setup_exa(ScrnInfoPtr pScrn, ExaDriverPtr pExaDrv)
{
    TRACE_ENTER();

    pExaDrv->exa_major = EXA_VERSION_MAJOR;
    pExaDrv->exa_minor = EXA_VERSION_MINOR;

    pExaDrv->pixmapOffsetAlign = 16;
    pExaDrv->pixmapPitchAlign = LOONGSON_DUMB_BO_ALIGN;

    pExaDrv->maxX = 8192;
    pExaDrv->maxY = 8192;

    // bo based pixmap ops
    //
    // EXA_HANDLES_PIXMAPS indicates to EXA that the driver can handle
    // all pixmap addressing and migration.
    //
    // EXA_SUPPORTS_PREPARE_AUX indicates to EXA that the driver can
    // handle the EXA_PREPARE_AUX* indices in the Prepare/FinishAccess hooks.
    // If there are no such hooks, this flag has no effect.
    //
    // EXA_OFFSCREEN_PIXMAPS indicates to EXA that the driver can support
    // offscreen pixmaps.
    //
    pExaDrv->flags = EXA_HANDLES_PIXMAPS |
                     EXA_SUPPORTS_PREPARE_AUX |
                     EXA_OFFSCREEN_PIXMAPS;

    //// solid
    pExaDrv->PrepareSolid = ms_exa_prepare_solid;
    pExaDrv->Solid = ms_exa_solid;
    pExaDrv->DoneSolid = ms_exa_solid_done;

    //// copy
    pExaDrv->PrepareCopy = etnaviv_exa_prepare_copy;
    pExaDrv->Copy = etnaviv_exa_do_copy;
    pExaDrv->DoneCopy = etnaviv_exa_copy_done;

    //// composite
    pExaDrv->CheckComposite = ms_exa_check_composite;
    pExaDrv->PrepareComposite = ms_exa_prepare_composite;
    pExaDrv->Composite = ms_exa_composite;
    pExaDrv->DoneComposite = ms_exa_composite_done;

    pExaDrv->UploadToScreen = etnaviv_exa_upload_to_screen;
    pExaDrv->DownloadFromScreen = etnaviv_exa_download_from_screen;

    pExaDrv->WaitMarker = etnaviv_exa_wait_marker;
    pExaDrv->MarkSync = etnaviv_exa_mark_sync;

    /* Hooks to allow driver to its own pixmap memory management
     * and for drivers with tiling support. Driver MUST fill out
     * new_fb_pitch with valid pitch of pixmap
     */
    pExaDrv->CreatePixmap2 = etnaviv_exa_create_pixmap;
    pExaDrv->DestroyPixmap = etnaviv_exa_destroy_pixmap;

    pExaDrv->PrepareAccess = etnaviv_exa_prepare_access;
    pExaDrv->FinishAccess = etnaviv_exa_finish_access;
    pExaDrv->PixmapIsOffscreen = etnaviv_is_offscreen_pixmap;

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
