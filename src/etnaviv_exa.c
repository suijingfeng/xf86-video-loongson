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

#include <exa.h>
#include <fbpict.h>

#include <xf86drm.h>
#include <etnaviv_drmif.h>

#include "driver.h"
#include "dumb_bo.h"

#include "etnaviv_exa.h"
#include "loongson_buffer.h"
#include "loongson_options.h"
#include "loongson_pixmap.h"
#include "loongson_debug.h"

#include "common.xml.h"


#define VIV2D_STREAM_SIZE 1024*32

#define ETNAVIV_3D_WIDTH_ALIGN                16
#define ETNAVIV_3D_HEIGHT_ALIGN               8

#define ALIGN(v,a) (((v) + (a) - 1) & ~((a) - 1))

static inline unsigned int etnaviv_pitch(unsigned width, unsigned bpp)
{
	unsigned pitch = bpp != 4 ? width * ((bpp + 7) / 8) : width / 2;

	/* GC320 and GC600 needs pitch aligned to 16 */
	return ALIGN(pitch, 16);
}

static inline unsigned int etnaviv_align_pitch(unsigned width, unsigned bpp)
{
    return etnaviv_pitch(ALIGN(width, ETNAVIV_3D_WIDTH_ALIGN), bpp);
}

static inline unsigned int etnaviv_align_height(unsigned int height)
{
    return ALIGN(height, ETNAVIV_3D_HEIGHT_ALIGN);
}

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


static Bool etnaviv_is_etna_bo(int usage_hint)
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
        return FALSE;
    }

    return FALSE;
}

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

static Bool ls_exa_prepare_access(PixmapPtr pPix, int index)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    // struct EtnavivRec *pGpu = &lsp->etna;
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPix);
    void *ptr = NULL;

    if (pPix->devPrivate.ptr)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                    "%s: already prepared\n", __func__);

        return TRUE;
    }

    if (priv->bo != NULL)
    {
        int ret = dumb_bo_map(pDrmMode->fd, priv->bo);
        if (ret)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "%s: dumb bo map failed: %s, ret=%d\n",
                       __func__, strerror(errno), ret);
            return FALSE;
        }
        pPix->devPrivate.ptr = priv->bo->ptr;
    }
    else if (priv->etna_bo && etnaviv_is_etna_bo(priv->usage_hint))
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
static void ls_exa_finish_access(PixmapPtr pPixmap, int index)
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

    ls_exa_prepare_access(pPixmap, 0);
    fbFill(&pPixmap->drawable, gc, x1, y1, x2 - x1, y2 - y1);
    ls_exa_finish_access(pPixmap, 0);

    FreeScratchGC(gc);
}


static void ms_exa_solid_done(PixmapPtr pPixmap)
{

}


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

    ls_exa_prepare_access(pSrcPixmap, 0);
    ls_exa_prepare_access(pDstPixmap, 0);

    fbCopyArea(&pSrcPixmap->drawable, &pDstPixmap->drawable, gc,
               srcX, srcY, width, height, dstX, dstY);

    ls_exa_finish_access(pDstPixmap, 0);
    ls_exa_finish_access(pSrcPixmap, 0);

    FreeScratchGC(gc);
}

static void ms_exa_copy_done(PixmapPtr pPixmap)
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
        ls_exa_prepare_access(pMask, 0);
    }

    ls_exa_prepare_access(pSrc, 0);
    ls_exa_prepare_access(pDst, 0);

    fbComposite(op, pSrcPicture, pMaskPicture, pDstPicture,
                srcX, srcY, maskX, maskY, dstX, dstY, width, height);

    ls_exa_finish_access(pDst, 0);
    ls_exa_finish_access(pSrc, 0);

    if (pMask)
    {
        ls_exa_finish_access(pMask, 0);
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



static int ms_exa_mark_sync(ScreenPtr pScreen)
{
    // TODO: return latest request(marker).
    return 0;
}


//////////////////////////////////////////////////////////////////////////////

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
    struct EtnavivRec *etnaviv = &lsp->etna;
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

    pitch = etnaviv_pitch(width, bitsPerPixel);
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

    if (new_fb_pitch)
    {
        *new_fb_pitch = pitch;
    }

    priv->pitch = pitch;


    return priv;
}


static void etnaviv_destroy_pixmap(ScreenPtr pScreen, void *driverPriv)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct exa_pixmap_priv *priv = (struct exa_pixmap_priv *)driverPriv;

    if (!priv)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "etnaviv: priv is NULL\n");
        return;
    }

    if (priv->fd > 0)
    {
        drmClose(priv->fd);
    }

    if (priv->etna_bo)
    {
        etna_bo_del(priv->etna_bo);
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "etnaviv: isn't a etna_bo\n");
    }

    free(priv);
}


static void ls_exa_destroy_pixmap(ScreenPtr pScreen, void *driverPriv)
{
    struct exa_pixmap_priv *pPriv = (struct exa_pixmap_priv *) driverPriv;

    if (etnaviv_is_etna_bo(pPriv->usage_hint))
    {
        etnaviv_destroy_pixmap(pScreen, driverPriv);
    }
    else
    {
        LS_DestroyExaPixmap(pScreen, driverPriv);
    }
}

/* Hooks to allow driver to its own pixmap memory management */

static void *ls_exa_create_pixmap2(ScreenPtr pScreen,
                                   int width,
                                   int height,
                                   int depth,
                                   int usage_hint,
                                   int bitsPerPixel,
                                   int *new_fb_pitch)
{
    if (etnaviv_is_etna_bo(usage_hint))
    {
        return etnaviv_create_pixmap(pScreen, width, height, depth,
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

    if (priv == NULL)
    {
        return FALSE;
    }

    if (etnaviv_is_etna_bo(priv->usage_hint))
    {
        return (priv->etna_bo != NULL);
    }
    else
    {
        return (priv->pBuf->pDat != NULL);
    }
}


static int etnaviv_report_features(ScrnInfoPtr pScrn,
                                   struct etna_gpu *gpu,
                                   struct EtnavivRec *pEnt)
{
    uint64_t val;
    /* HALTI (gross architecture) level. -1 for pre-HALTI. */
    int halti;

    if (etna_gpu_get_param(gpu, ETNA_GPU_MODEL, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_MODEL");
       goto fail;
    }
    pEnt->model = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Vivante GC%x\n", (uint32_t)val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_REVISION, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_REVISION");
       goto fail;
    }
    pEnt->revision = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "revision %x\n", (uint32_t)val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_0, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_0");
       goto fail;
    }
    pEnt->features[0] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[0]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_1, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_1");
       goto fail;
    }
    pEnt->features[1] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[1]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_2, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_2");
       goto fail;
    }
    pEnt->features[2] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[2]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_3, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_3");
       goto fail;
    }
    pEnt->features[3] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[3]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_4, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_4");
       goto fail;
    }
    pEnt->features[4] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[4]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_5, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_5");
       goto fail;
    }
    pEnt->features[5] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[5]: %lx\n", val);

    if (etna_gpu_get_param(gpu, ETNA_GPU_FEATURES_6, &val)) {
       DEBUG_MSG("could not get ETNA_GPU_FEATURES_6");
       goto fail;
    }
    pEnt->features[6] = val;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "features[6]: %lx\n", val);


   if (etna_gpu_get_param(gpu, ETNA_GPU_INSTRUCTION_COUNT, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_INSTRUCTION_COUNT");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ETNA_GPU_INSTRUCTION_COUNT: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "vertex_output_buffer_size: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_VERTEX_CACHE_SIZE, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_VERTEX_CACHE_SIZE");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "vertex_cache_size: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_SHADER_CORE_COUNT, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_SHADER_CORE_COUNT");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "shader_core_count: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_STREAM_COUNT, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_STREAM_COUNT");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "gpu stream count: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_REGISTER_MAX, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_REGISTER_MAX");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "max_registers: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_PIXEL_PIPES, &val)) {
      DEBUG_MSG("could not get ETNA_GPU_PIXEL_PIPES");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "pixel pipes: %lx\n", val);

   if (etna_gpu_get_param(gpu, ETNA_GPU_NUM_CONSTANTS, &val)) {
      DEBUG_MSG("could not get %s", "ETNA_GPU_NUM_CONSTANTS");
      goto fail;
   }
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "num of constants: %lx\n", val);

   #define VIV_FEATURE(screen, word, feature) \
        ((screen->features[viv_ ## word] & (word ## _ ## feature)) != 0)

   /* Figure out gross GPU architecture. See rnndb/common.xml for a specific
    * description of the differences. */
   if (VIV_FEATURE(pEnt, chipMinorFeatures5, HALTI5))
      halti = 5; /* New GC7000/GC8x00  */
   else if (VIV_FEATURE(pEnt, chipMinorFeatures5, HALTI4))
      halti = 4; /* Old GC7000/GC7400 */
   else if (VIV_FEATURE(pEnt, chipMinorFeatures5, HALTI3))
      halti = 3; /* None? */
   else if (VIV_FEATURE(pEnt, chipMinorFeatures4, HALTI2))
      halti = 2; /* GC2500/GC3000/GC5000/GC6400 */
   else if (VIV_FEATURE(pEnt, chipMinorFeatures2, HALTI1))
      halti = 1; /* GC900/GC4000/GC7000UL */
   else if (VIV_FEATURE(pEnt, chipMinorFeatures1, HALTI0))
      halti = 0; /* GC880/GC2000/GC7000TM */
   else
      halti = -1; /* GC7000nanolite / pre-GC2000 except GC880 */

   if (halti >= 0)
      xf86DrvMsg(pScrn->scrnIndex, X_INFO, "etnaviv: GPU arch: HALTI%d", halti);
   else
      xf86DrvMsg(pScrn->scrnIndex, X_INFO, "etnaviv: GPU arch: pre-HALTI");


 fail:
    return -1;
}

Bool etnaviv_setup_exa(ScrnInfoPtr pScrn, ExaDriverPtr pExaDrv)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    // struct drmmode_rec * const pDrmMode = &lsp->drmmode;

{
    struct EtnavivRec *pGpu = &lsp->etna;
    struct etna_device *dev;
    struct etna_gpu *gpu;
    struct etna_pipe *pipe;
    struct etna_cmd_stream *stream;
    uint64_t model, revision;
    int fd;

    fd = drmOpenWithType("etnaviv", NULL, DRM_NODE_PRIMARY);
    if (fd != -1)
    {
        drmVersionPtr version = drmGetVersion(fd);
        if (version)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Version: %d.%d.%d\n",
                       version->version_major, version->version_minor,
                       version->version_patchlevel);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Name: %s\n", version->name);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Date: %s\n", version->date);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Description: %s\n", version->desc);
            drmFreeVersion(version);
        }
    }

    dev = etna_device_new(fd);

    /* we assume that core 0 is a 2D capable one */
    gpu = etna_gpu_new(dev, 0);
    pipe = etna_pipe_new(gpu, ETNA_PIPE_2D);

    stream = etna_cmd_stream_new(pipe, VIV2D_STREAM_SIZE, NULL, NULL);

    pGpu->fd = fd;
    pGpu->dev = dev;
    pGpu->gpu = gpu;
    pGpu->pipe = pipe;
    pGpu->stream = stream;

    etna_gpu_get_param(gpu, ETNA_GPU_MODEL, &model);
    etna_gpu_get_param(gpu, ETNA_GPU_REVISION, &revision);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "EXA: Vivante GC%x GPU revision %x found!",
               (uint32_t)model, (uint32_t)revision);

    etnaviv_report_features(pScrn, gpu, pGpu);

}

    TRACE_ENTER();

    pExaDrv->exa_major = EXA_VERSION_MAJOR;
    pExaDrv->exa_minor = EXA_VERSION_MINOR;

    pExaDrv->pixmapOffsetAlign = 16;
    pExaDrv->pixmapPitchAlign = 256;

    pExaDrv->maxX = 8192;
    pExaDrv->maxY = 8192;

    // bo based pixmap ops
    pExaDrv->flags = EXA_HANDLES_PIXMAPS |
                     EXA_SUPPORTS_PREPARE_AUX |
                     EXA_OFFSCREEN_PIXMAPS;

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
    pExaDrv->MarkSync = ms_exa_mark_sync;
    pExaDrv->DestroyPixmap = ls_exa_destroy_pixmap;
    pExaDrv->CreatePixmap2 = ls_exa_create_pixmap2;
    pExaDrv->PrepareAccess = ls_exa_prepare_access;
    pExaDrv->FinishAccess = ls_exa_finish_access;
    pExaDrv->PixmapIsOffscreen = etnaviv_is_offscreen_pixmap;

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
