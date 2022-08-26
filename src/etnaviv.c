/*
 * Vivante GPU Acceleration Xorg driver
 *
 * Written by Russell King, 2012, derived in part from the
 * Intel xorg X server driver.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include <armada_bufmgr.h>

#include "armada_accel.h"
#include "common_drm_dri2.h"

#include "fb.h"
#include "gcstruct.h"
#include "xf86.h"
#include "compat-api.h"

#include "cpu_access.h"
#include "fbutil.h"
#include "gal_extension.h"
#include "mark.h"
#include "pixmaputil.h"
#include "unaccel.h"

#include "etnaviv_accel.h"
#include "etnaviv_dri2.h"
#include "etnaviv_dri3.h"
#include "etnaviv_render.h"
#include "etnaviv_utils.h"
#include "etnaviv_xv.h"

#include <etnaviv/etna_bo.h>
#include <etnaviv/state_2d.xml.h>
#include "etnaviv_compat.h"

etnaviv_Key etnaviv_pixmap_index;
etnaviv_Key etnaviv_screen_index;
int etnaviv_private_index = -1;

enum {
	OPTION_DRI2,
	OPTION_DRI3,
};

const OptionInfoRec etnaviv_options[] = {
	{ OPTION_DRI2,		"DRI",		OPTV_BOOLEAN, {0}, TRUE },
	{ OPTION_DRI3,		"DRI3",		OPTV_BOOLEAN, {0}, TRUE },
	{ -1,			NULL,		OPTV_NONE,    {0}, FALSE }
};

void etnaviv_finish_fences(struct etnaviv *etnaviv, uint32_t fence)
{
	uint32_t last;

	while (1) {
		last = etnaviv_fence_retire_id(&etnaviv->fence_head, fence);
		if (last == fence)
			break;

		if (viv_fence_finish(etnaviv->conn, last, 0) != VIV_STATUS_OK)
			break;

		fence = last;
	}

	etnaviv->last_fence = fence;
}

static void etnaviv_retire_freemem_fence(struct etnaviv_fence_head *fh,
	struct etnaviv_fence *f)
{
	struct etnaviv *etnaviv = container_of(fh, struct etnaviv, fence_head);
	struct etnaviv_usermem_node *n = container_of(f,
					struct etnaviv_usermem_node, fence);

	etna_bo_del(etnaviv->conn, n->bo, NULL);
	free(n->mem);
	free(n);
}

void etnaviv_add_freemem(struct etnaviv *etnaviv,
	struct etnaviv_usermem_node *n)
{
	n->fence.retire = etnaviv_retire_freemem_fence;
	etnaviv_fence_add(&etnaviv->fence_head, &n->fence);
}

static CARD32 etnaviv_cache_expire(OsTimerPtr timer, CARD32 time, pointer arg)
{
	return 0;
}

/*
 * We are about to respond to a client.  Ensure that all pending rendering
 * is flushed to the GPU prior to the response being delivered.
 */
static void etnaviv_flush_callback(CallbackListPtr *list, pointer user_data,
	pointer call_data)
{
	ScrnInfoPtr pScrn = user_data;
	struct etnaviv *etnaviv = pScrn->privates[etnaviv_private_index].ptr;

	if (pScrn->vtSema && etnaviv_fence_batch_pending(&etnaviv->fence_head))
		etnaviv_commit(etnaviv, FALSE);
}

/* Etnaviv pixmap memory management */
static void etnaviv_put_vpix(struct etnaviv *etnaviv,
	struct etnaviv_pixmap *vPix)
{
	if (--vPix->refcnt == 0) {
		if (vPix->etna_bo) {
			struct etna_bo *etna_bo = vPix->etna_bo;

			if (!vPix->bo && vPix->state & ST_CPU_RW)
				etna_bo_cpu_fini(etna_bo);
			etna_bo_del(etnaviv->conn, etna_bo, NULL);
		}
		if (vPix->bo)
			drm_armada_bo_put(vPix->bo);
		free(vPix);
	}
}

static void etnaviv_retire_vpix_fence(struct etnaviv_fence_head *fh,
	struct etnaviv_fence *f)
{
	struct etnaviv *etnaviv = container_of(fh, struct etnaviv, fence_head);
	struct etnaviv_pixmap *vpix = container_of(f, struct etnaviv_pixmap,
						   fence);

	etnaviv_put_vpix(etnaviv, vpix);
}

static void etnaviv_free_pixmap(PixmapPtr pixmap)
{
	struct etnaviv_pixmap *vPix = etnaviv_get_pixmap_priv(pixmap);

	if (vPix) {
		struct etnaviv *etnaviv;

		etnaviv_set_pixmap_priv(pixmap, NULL);

		etnaviv = etnaviv_get_screen_priv(pixmap->drawable.pScreen);

		/*
		 * Put the pixmap - if it's on one of the batch or fence
		 * lists, they will hold a refcount, which will be dropped
		 * once the GPU operation is complete.
		 */
		etnaviv_put_vpix(etnaviv, vPix);
	}
}


/* Determine whether this GC and target Drawable can be accelerated */
static Bool etnaviv_GC_can_accel(GCPtr pGC, DrawablePtr pDrawable)
{
	if (!etnaviv_drawable(pDrawable))
		return FALSE;

	/* Must be full-planes */
	return !pGC || fb_full_planemask(pDrawable, pGC->planemask);
}

static Bool etnaviv_GCfill_can_accel(GCPtr pGC, DrawablePtr pDrawable)
{
	switch (pGC->fillStyle) {
	case FillSolid:
		return TRUE;

	case FillTiled:
		/* Single pixel tiles are just solid colours */
		if (pGC->tileIsPixel)
			return TRUE;

		/* If the tile pixmap is a single pixel, it's also a solid fill */
		if (pGC->tile.pixmap->drawable.width == 1 &&
		    pGC->tile.pixmap->drawable.height == 1)
			return TRUE;

		/* In theory, we could do !tileIsPixel as well, which means
		 * copying the tile (possibly) multiple times to the drawable.
		 * This is something we should do, especially if the size of
		 * the tile matches the size of the drawable and the tile
		 * offsets are zero (iow, it's a plain copy.)
		 */
		return FALSE;

	default:
		return FALSE;
	}
}


static void
etnaviv_FillSpans(DrawablePtr pDrawable, GCPtr pGC, int n, DDXPointPtr ppt,
	int *pwidth, int fSorted)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pDrawable->pScreen);

	assert(etnaviv_GC_can_accel(pGC, pDrawable));

	if (etnaviv->force_fallback ||
	    !etnaviv_GCfill_can_accel(pGC, pDrawable) ||
	    !etnaviv_accel_FillSpans(pDrawable, pGC, n, ppt, pwidth, fSorted))
		unaccel_FillSpans(pDrawable, pGC, n, ppt, pwidth, fSorted);
}

static void
etnaviv_PutImage(DrawablePtr pDrawable, GCPtr pGC, int depth, int x, int y,
	int w, int h, int leftPad, int format, char *bits)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pDrawable->pScreen);

	assert(etnaviv_GC_can_accel(pGC, pDrawable));

	if (etnaviv->force_fallback ||
	    !etnaviv_accel_PutImage(pDrawable, pGC, depth, x, y, w, h, leftPad,
				    format, bits))
		unaccel_PutImage(pDrawable, pGC, depth, x, y, w, h, leftPad,
					 format, bits);
}

static RegionPtr
etnaviv_CopyArea(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
	int srcx, int srcy, int w, int h, int dstx, int dsty)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pDst->pScreen);

	assert(etnaviv_GC_can_accel(pGC, pDst));

	if (etnaviv->force_fallback)
		return unaccel_CopyArea(pSrc, pDst, pGC, srcx, srcy, w, h,
					dstx, dsty);

	return miDoCopy(pSrc, pDst, pGC, srcx, srcy, w, h, dstx, dsty,
			etnaviv_accel_CopyNtoN, 0, NULL);
}

static void
etnaviv_PolyPoint(DrawablePtr pDrawable, GCPtr pGC, int mode, int npt,
	DDXPointPtr ppt)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pDrawable->pScreen);

	assert(etnaviv_GC_can_accel(pGC, pDrawable));

	if (etnaviv->force_fallback ||
	    !etnaviv_GCfill_can_accel(pGC, pDrawable) ||
	    !etnaviv_accel_PolyPoint(pDrawable, pGC, mode, npt, ppt))
		unaccel_PolyPoint(pDrawable, pGC, mode, npt, ppt);
}

static void
etnaviv_PolyLines(DrawablePtr pDrawable, GCPtr pGC, int mode, int npt,
	DDXPointPtr ppt)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pDrawable->pScreen);

	assert(etnaviv_GC_can_accel(pGC, pDrawable));

	if (etnaviv->force_fallback ||
	    pGC->lineWidth != 0 || pGC->lineStyle != LineSolid ||
	    pGC->fillStyle != FillSolid ||
	    !etnaviv_accel_PolyLines(pDrawable, pGC, mode, npt, ppt))
		unaccel_PolyLines(pDrawable, pGC, mode, npt, ppt);
}

static void
etnaviv_PolySegment(DrawablePtr pDrawable, GCPtr pGC, int nseg, xSegment *pSeg)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pDrawable->pScreen);

	assert(etnaviv_GC_can_accel(pGC, pDrawable));

	if (etnaviv->force_fallback ||
	    pGC->lineWidth != 0 || pGC->lineStyle != LineSolid ||
	    pGC->fillStyle != FillSolid ||
	    !etnaviv_accel_PolySegment(pDrawable, pGC, nseg, pSeg))
		unaccel_PolySegment(pDrawable, pGC, nseg, pSeg);
}

static void
etnaviv_PolyFillRect(DrawablePtr pDrawable, GCPtr pGC, int nrect,
	xRectangle * prect)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pDrawable->pScreen);
	PixmapPtr pPix = drawable_pixmap(pDrawable);

	if (etnaviv->force_fallback ||
	    (pPix->drawable.width == 1 && pPix->drawable.height == 1))
		goto fallback;

	assert(etnaviv_GC_can_accel(pGC, pDrawable));

	if (etnaviv_GCfill_can_accel(pGC, pDrawable)) {
		if (etnaviv_accel_PolyFillRectSolid(pDrawable, pGC, nrect, prect))
			return;
	} else if (pGC->fillStyle == FillTiled) {
		if (etnaviv_accel_PolyFillRectTiled(pDrawable, pGC, nrect, prect))
			return;
	}

 fallback:
	unaccel_PolyFillRect(pDrawable, pGC, nrect, prect);
}

static GCOps etnaviv_GCOps = {
	etnaviv_FillSpans,
	unaccel_SetSpans,
	etnaviv_PutImage,
	etnaviv_CopyArea,
	unaccel_CopyPlane,
	etnaviv_PolyPoint,
	etnaviv_PolyLines,
	etnaviv_PolySegment,
	miPolyRectangle,
	miPolyArc,
	miFillPolygon,
	etnaviv_PolyFillRect,
	miPolyFillArc,
	miPolyText8,
	miPolyText16,
	miImageText8,
	miImageText16,
	unaccel_ImageGlyphBlt,
	unaccel_PolyGlyphBlt,
	unaccel_PushPixels
};

static GCOps etnaviv_unaccel_GCOps = {
	unaccel_FillSpans,
	unaccel_SetSpans,
	unaccel_PutImage,
	unaccel_CopyArea,
	unaccel_CopyPlane,
	unaccel_PolyPoint,
	unaccel_PolyLines,
	unaccel_PolySegment,
	miPolyRectangle,
	miPolyArc,
	miFillPolygon,
	unaccel_PolyFillRect,
	miPolyFillArc,
	miPolyText8,
	miPolyText16,
	miImageText8,
	miImageText16,
	unaccel_ImageGlyphBlt,
	unaccel_PolyGlyphBlt,
	unaccel_PushPixels
};

static void
etnaviv_ValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr pDrawable)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pDrawable->pScreen);

#ifdef FB_24_32BIT
	if (changes & GCTile && fbGetRotatedPixmap(pGC)) {
		pGC->pScreen->DestroyPixmap(fbGetRotatedPixmap(pGC));
		fbGetRotatedPixmap(pGC) = NULL;
	}
	if (pGC->fillStyle == FillTiled) {
		PixmapPtr pOldTile = pGC->tile.pixmap;
		PixmapPtr pNewTile;

		if (pOldTile->drawable.bitsPerPixel != pDrawable->bitsPerPixel) {
			pNewTile = fbGetRotatedPixmap(pGC);
			if (!pNewTile || pNewTile->drawable.bitsPerPixel != pDrawable->bitsPerPixel) {
				if (pNewTile)
					pGC->pScreen->DestroyPixmap(pNewTile);
				prepare_cpu_drawable(&pOldTile->drawable, CPU_ACCESS_RO);
				pNewTile = fb24_32ReformatTile(pOldTile, pDrawable->bitsPerPixel);
				finish_cpu_drawable(&pOldTile->drawable, CPU_ACCESS_RO);
			}
			if (pNewTile) {
				fbGetRotatedPixmap(pGC) = pOldTile;
				pGC->tile.pixmap = pNewTile;
				changes |= GCTile;
			}
		}
	}
#endif
	if (changes & GCTile) {
		if (!pGC->tileIsPixel &&
		    FbEvenTile(pGC->tile.pixmap->drawable.width *
			       pDrawable->bitsPerPixel)) {
			prepare_cpu_drawable(&pGC->tile.pixmap->drawable, CPU_ACCESS_RW);
			fbPadPixmap(pGC->tile.pixmap);
			finish_cpu_drawable(&pGC->tile.pixmap->drawable, CPU_ACCESS_RW);
		}
		/* mask out gctile changes now that we've done the work */
		changes &= ~GCTile;
	}
	if (changes & GCStipple && pGC->stipple) {
		prepare_cpu_drawable(&pGC->stipple->drawable, CPU_ACCESS_RW);
		fbValidateGC(pGC, changes, pDrawable);
		finish_cpu_drawable(&pGC->stipple->drawable, CPU_ACCESS_RW);
	} else {
		fbValidateGC(pGC, changes, pDrawable);
	}

	/*
	 * Select the GC ops depending on whether we have any
	 * chance to accelerate with this GC.
	 */
	if (!etnaviv->force_fallback && etnaviv_GC_can_accel(pGC, pDrawable))
		pGC->ops = &etnaviv_GCOps;
	else
		pGC->ops = &etnaviv_unaccel_GCOps;
}

static GCFuncs etnaviv_GCFuncs = {
	etnaviv_ValidateGC,
	miChangeGC,
	miCopyGC,
	miDestroyGC,
	miChangeClip,
	miDestroyClip,
	miCopyClip
};


static Bool etnaviv_CloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pScreen);
	PixmapPtr pixmap;

	DeleteCallback(&FlushCallback, etnaviv_flush_callback, pScrn);

	etnaviv_render_close_screen(pScreen);

	pScreen->CloseScreen = etnaviv->CloseScreen;
	pScreen->GetImage = etnaviv->GetImage;
	pScreen->GetSpans = etnaviv->GetSpans;
	pScreen->ChangeWindowAttributes = etnaviv->ChangeWindowAttributes;
	pScreen->CopyWindow = etnaviv->CopyWindow;
	pScreen->CreatePixmap = etnaviv->CreatePixmap;
	pScreen->DestroyPixmap = etnaviv->DestroyPixmap;
	pScreen->CreateGC = etnaviv->CreateGC;
	pScreen->BitmapToRegion = etnaviv->BitmapToRegion;
	pScreen->BlockHandler = etnaviv->BlockHandler;

#ifdef HAVE_DRI2
	etnaviv_dri2_CloseScreen(CLOSE_SCREEN_ARGS);
#endif

	/* Ensure everything has been committed */
	etnaviv_commit(etnaviv, TRUE);

	pixmap = pScreen->GetScreenPixmap(pScreen);
	etnaviv_free_pixmap(pixmap);

	etnaviv_accel_shutdown(etnaviv);

	return pScreen->CloseScreen(CLOSE_SCREEN_ARGS);
}

static void
etnaviv_GetImage(DrawablePtr pDrawable, int x, int y, int w, int h,
	unsigned int format, unsigned long planeMask, char *d)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pDrawable->pScreen);

	if (etnaviv->force_fallback ||
	    !etnaviv_accel_GetImage(pDrawable, x, y, w, h, format, planeMask,
				    d))
		unaccel_GetImage(pDrawable, x, y, w, h, format, planeMask, d);
}

static void
etnaviv_CopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgnSrc)
{
	PixmapPtr pPixmap = pWin->drawable.pScreen->GetWindowPixmap(pWin);
	RegionRec rgnDst;
	int dx, dy;

	dx = ptOldOrg.x - pWin->drawable.x;
	dy = ptOldOrg.y - pWin->drawable.y;
	RegionTranslate(prgnSrc, -dx, -dy);
	RegionInit(&rgnDst, NullBox, 0);
	RegionIntersect(&rgnDst, &pWin->borderClip, prgnSrc);

#ifdef COMPOSITE
	if (pPixmap->screen_x || pPixmap->screen_y)
		RegionTranslate(&rgnDst, -pPixmap->screen_x,
				-pPixmap->screen_y);
#endif

	miCopyRegion(&pPixmap->drawable, &pPixmap->drawable, NULL,
		     &rgnDst, dx, dy, etnaviv_accel_CopyNtoN, 0, NULL);

	RegionUninit(&rgnDst);
}

#ifdef HAVE_DRI2
Bool etnaviv_pixmap_flink(PixmapPtr pixmap, uint32_t *name)
{
	struct etnaviv_pixmap *vpix = etnaviv_get_pixmap_priv(pixmap);
	Bool ret = FALSE;

	if (!vpix)
		return FALSE;

	if (vpix->name) {
		*name = vpix->name;
		ret = TRUE;
	} else if (vpix->bo && !drm_armada_bo_flink(vpix->bo, name)) {
		vpix->name = *name;
		ret = TRUE;
	} else if (!etna_bo_flink(vpix->etna_bo, name)) {
		vpix->name = *name;
		ret = TRUE;
	}

	return ret;
}
#endif

static Bool etnaviv_alloc_armada_bo(ScreenPtr pScreen, struct etnaviv *etnaviv,
	PixmapPtr pixmap, int w, int h, struct etnaviv_format fmt,
	unsigned usage_hint)
{
	struct etnaviv_pixmap *vpix;
	struct drm_armada_bo *bo;
	unsigned pitch, bpp = pixmap->drawable.bitsPerPixel;

#ifndef HAVE_DRM_ARMADA_BO_CREATE_SIZE
	bo = drm_armada_bo_create(etnaviv->bufmgr, w, h, bpp);
	if (!bo) {
		xf86DrvMsg(etnaviv->scrnIndex, X_ERROR,
			   "etnaviv: failed to allocate armada bo for %dx%d %dbpp\n",
			   w, h, bpp);
		return FALSE;
	}

	pitch = bo->pitch;
#else
	unsigned size;

	if (usage_hint & CREATE_PIXMAP_USAGE_TILE) {
		pitch = etnaviv_tile_pitch(w, bpp);
		size = pitch * etnaviv_tile_height(h);
		fmt.tile = 1;
	} else {
		pitch = etnaviv_pitch(w, bpp);
		size = pitch * h;
	}

	size = ALIGN(size, 4096);

	bo = drm_armada_bo_create_size(etnaviv->bufmgr, size);
	if (!bo) {
		xf86DrvMsg(etnaviv->scrnIndex, X_ERROR,
			   "etnaviv: failed to allocate armada bo for %dx%d %dbpp\n",
			   w, h, bpp);
		return FALSE;
	}
#endif

	if (drm_armada_bo_map(bo))
		goto free_bo;

	/*
	 * Do not store our data pointer in the pixmap - only do so (via
	 * prepare_cpu_drawable()) when required to directly access the
	 * pixmap.  This provides us a way to validate that we do not have
	 * any spurious unchecked accesses to the pixmap data while the GPU
	 * has ownership of the pixmap.
	 */
	pScreen->ModifyPixmapHeader(pixmap, w, h, 0, 0, pitch, NULL);

	vpix = etnaviv_alloc_pixmap(pixmap, fmt);
	if (!vpix)
		goto free_bo;

	vpix->bo = bo;

	etnaviv_set_pixmap_priv(pixmap, vpix);

#ifdef DEBUG_PIXMAP
	dbg("Pixmap %p: vPix=%p armada_bo=%p format=%u/%u/%u\n",
	    pixmap, vPix, bo, fmt.format, fmt.swizzle, fmt.tile);
#endif

	return TRUE;

 free_bo:
	drm_armada_bo_put(bo);
	return FALSE;
}




static PixmapPtr etnaviv_CreatePixmap(ScreenPtr pScreen,
                                      int w,
                                      int h,
                                      int depth,
                                      unsigned usage_hint)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pScreen);
	struct etnaviv_format fmt = { .swizzle = DE_SWIZZLE_ARGB, };
	PixmapPtr pixmap;

	if (w > 32768 || h > 32768)
		return NullPixmap;

	if (depth == 1 || etnaviv->force_fallback)
		goto fallback;

	if (usage_hint == CREATE_PIXMAP_USAGE_GLYPH_PICTURE &&
	    w <= 32 && h <= 32)
		goto fallback;

	pixmap = etnaviv->CreatePixmap(pScreen, 0, 0, depth, usage_hint);
	if (pixmap == NullPixmap || w == 0 || h == 0)
		return pixmap;

	/* Create the appropriate format for this pixmap */
	switch (pixmap->drawable.bitsPerPixel) {
	case 8:
		if (usage_hint & CREATE_PIXMAP_USAGE_GPU) {
			fmt.format = DE_FORMAT_A8;
			break;
		}
		goto fallback_free_pix;

	case 16:
		if (pixmap->drawable.depth == 15)
			fmt.format = DE_FORMAT_A1R5G5B5;
		else
			fmt.format = DE_FORMAT_R5G6B5;
		break;

	case 32:
		fmt.format = DE_FORMAT_A8R8G8B8;
		break;

	default:
		goto fallback_free_pix;
	}

    if (!etnaviv_alloc_etna_bo(pScreen, etnaviv, pixmap,
                               w, h, fmt, usage_hint))
        goto fallback_free_pix;
    goto out;

 fallback_free_pix:
	etnaviv->DestroyPixmap(pixmap);
 fallback:
	/* GPU pixmaps must fail rather than fall back */
	if (usage_hint & CREATE_PIXMAP_USAGE_GPU)
		return NULL;

	pixmap = etnaviv->CreatePixmap(pScreen, w, h, depth, usage_hint);

 out:
#ifdef DEBUG_PIXMAP
	dbg("Created pixmap %p %dx%d %d %d %x\n",
	    pixmap, w, h, depth, pixmap->drawable.bitsPerPixel, usage);
#endif

	return pixmap;
}

static Bool etnaviv_DestroyPixmap(PixmapPtr pixmap)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pixmap->drawable.pScreen);
	if (pixmap->refcnt == 1) {
#ifdef DEBUG_PIXMAP
		dbg("Destroying pixmap %p\n", pixmap);
#endif
		etnaviv_free_pixmap(pixmap);
	}
	return etnaviv->DestroyPixmap(pixmap);
}

static Bool etnaviv_CreateGC(GCPtr pGC)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pGC->pScreen);
	Bool ret;

	ret = etnaviv->CreateGC(pGC);
	if (ret)
		pGC->funcs = &etnaviv_GCFuncs;

	return ret;
}

/* Commit any pending GPU operations */
static void etnaviv_BlockHandler(BLOCKHANDLER_ARGS_DECL)
{
	SCREEN_PTR(arg);
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pScreen);

	if (etnaviv_fence_batch_pending(&etnaviv->fence_head))
		etnaviv_commit(etnaviv, FALSE);

	mark_flush();

	pScreen->BlockHandler = etnaviv->BlockHandler;
	pScreen->BlockHandler(BLOCKHANDLER_ARGS);
	etnaviv->BlockHandler = pScreen->BlockHandler;
	pScreen->BlockHandler = etnaviv_BlockHandler;

	/*
	 * Check for any completed fences.  If the fence numberspace
	 * wraps, it can allow an idle pixmap to become "active" again.
	 * This prevents that occuring.  Periodically check for completed
	 * fences.
	 */
	if (etnaviv_fence_fences_pending(&etnaviv->fence_head)) {
		UpdateCurrentTimeIf();
		etnaviv_finish_fences(etnaviv, etnaviv->last_fence);
		if (etnaviv_fence_fences_pending(&etnaviv->fence_head)) {
			etnaviv->cache_timer = TimerSet(etnaviv->cache_timer,
							0, 500,
							etnaviv_cache_expire,
							etnaviv);
		}
	}
}

static Bool etnaviv_pre_init(ScrnInfoPtr pScrn, int drm_fd)
{
	struct etnaviv *etnaviv;
	OptionInfoPtr options;

	etnaviv = calloc(1, sizeof *etnaviv);
	if (!etnaviv)
		return FALSE;

	options = malloc(sizeof(etnaviv_options));
	if (!options) {
		free(etnaviv);
		return FALSE;
	}

	memcpy(options, etnaviv_options, sizeof(etnaviv_options));
	xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, options);

#ifdef HAVE_DRI2
	etnaviv->dri2_enabled = xf86ReturnOptValBool(options, OPTION_DRI2,
						     TRUE);
#endif
#ifdef HAVE_DRI3
	/*
	 * We default to DRI3 disabled, as we are unable to support
	 * flips with etnaviv-allocated buffer objects, whereas DRI2
	 * can (and does) provide support for this.
	 */
	etnaviv->dri3_enabled = xf86ReturnOptValBool(options, OPTION_DRI3,
						     FALSE);
#endif

	etnaviv->scrnIndex = pScrn->scrnIndex;

	if (etnaviv_private_index == -1)
		etnaviv_private_index = xf86AllocateScrnInfoPrivateIndex();

	pScrn->privates[etnaviv_private_index].ptr = etnaviv;

	free(options);

	return TRUE;
}

static Bool etnaviv_ScreenInit(ScreenPtr pScreen, struct drm_armada_bufmgr *mgr)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct etnaviv *etnaviv = pScrn->privates[etnaviv_private_index].ptr;

	if (!etnaviv_CreateKey(&etnaviv_pixmap_index, PRIVATE_PIXMAP) ||
	    !etnaviv_CreateKey(&etnaviv_screen_index, PRIVATE_SCREEN))
		return FALSE;

	etnaviv->bufmgr = mgr;

	if (!etnaviv_accel_init(etnaviv))
		goto fail_accel;

	etnaviv_fence_head_init(&etnaviv->fence_head);

	etnaviv_set_screen_priv(pScreen, etnaviv);

	if (!AddCallback(&FlushCallback, etnaviv_flush_callback, pScrn)) {
		etnaviv_accel_shutdown(etnaviv);
		goto fail_accel;
	}

#ifdef HAVE_DRI2
	if (!etnaviv->dri2_enabled) {
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
			   "direct rendering: %s %s\n", "DRI2", "disabled");
	} else {
		const char *name;
		drmVersionPtr version;
		int dri_fd = -1;

		/*
		 * Use drmGetVersion() to check whether the etnaviv fd
		 * is a DRM fd.
		 */
		version = drmGetVersion(etnaviv->conn->fd);
		if (version) {
			drmFreeVersion(version);

			/* etnadrm fd, etnadrm buffer management */
			dri_fd = etnaviv->conn->fd;
			name = "etnaviv";
		}

		if (dri_fd == -1) {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,
				   "direct rendering: unusuable devices\n");
		} else if (!etnaviv_dri2_ScreenInit(pScreen, dri_fd, name)) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "direct rendering: %s %s\n", "DRI2",
				   "failed");
			etnaviv->dri2_enabled = FALSE;
		} else {
			xf86DrvMsg(pScrn->scrnIndex, X_INFO,
				   "direct rendering: %s %s\n", "DRI2",
				   "enabled");
		}
	}
#endif
#ifdef HAVE_DRI3
	if (!etnaviv->dri3_enabled) {
		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
			   "direct rendering: %s %s\n", "DRI3", "disabled");
	} else if (!etnaviv_dri3_ScreenInit(pScreen)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "direct rendering: %s %s\n", "DRI3", "failed");
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO,
			   "direct rendering: %s %s\n", "DRI3", "enabled");
	}
#endif

	etnaviv->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = etnaviv_CloseScreen;
	etnaviv->GetImage = pScreen->GetImage;
	pScreen->GetImage = etnaviv_GetImage;
	etnaviv->GetSpans = pScreen->GetSpans;
	pScreen->GetSpans = unaccel_GetSpans;
	etnaviv->ChangeWindowAttributes = pScreen->ChangeWindowAttributes;
	pScreen->ChangeWindowAttributes = unaccel_ChangeWindowAttributes;
	etnaviv->CopyWindow = pScreen->CopyWindow;
	pScreen->CopyWindow = etnaviv_CopyWindow;
	etnaviv->CreatePixmap = pScreen->CreatePixmap;
	pScreen->CreatePixmap = etnaviv_CreatePixmap;
	etnaviv->DestroyPixmap = pScreen->DestroyPixmap;
	pScreen->DestroyPixmap = etnaviv_DestroyPixmap;
	etnaviv->CreateGC = pScreen->CreateGC;
	pScreen->CreateGC = etnaviv_CreateGC;
	etnaviv->BitmapToRegion = pScreen->BitmapToRegion;
	pScreen->BitmapToRegion = unaccel_BitmapToRegion;
	etnaviv->BlockHandler = pScreen->BlockHandler;
	pScreen->BlockHandler = etnaviv_BlockHandler;

	etnaviv_render_screen_init(pScreen);

	return TRUE;

fail_accel:
	free(etnaviv);
	return FALSE;
}

static void etnaviv_align_bo_size(ScreenPtr pScreen, int *width, int *height,
	int bpp)
{
	*width = etnaviv_pitch(*width, bpp) * 8 / bpp;
}

static Bool etnaviv_format(struct etnaviv_format *fmt, unsigned int depth,
	unsigned int bpp)
{
	static const struct etnaviv_format template =
		{ .swizzle = DE_SWIZZLE_ARGB, };
	*fmt = template;
	switch (bpp) {
	case 16:
		if (depth == 15)
			fmt->format = DE_FORMAT_A1R5G5B5;
		else
			fmt->format = DE_FORMAT_R5G6B5;
		return TRUE;

	case 32:
		fmt->format = DE_FORMAT_A8R8G8B8;
		return TRUE;

	default:
		return FALSE;
	}
}

static struct etnaviv_pixmap *etnaviv_pixmap_attach_dmabuf(
	struct etnaviv *etnaviv, PixmapPtr pixmap, struct etnaviv_format fmt,
	int fd)
{
	struct etnaviv_pixmap *vpix;
	struct etna_bo *bo;

	bo = etna_bo_from_dmabuf(etnaviv->conn, fd, PROT_READ | PROT_WRITE);
	if (!bo) {
		xf86DrvMsg(etnaviv->scrnIndex, X_ERROR,
			   "etnaviv: gpu dmabuf map failed: %s\n",
			   strerror(errno));
		return NULL;
	}

	vpix = etnaviv_alloc_pixmap(pixmap, fmt);
	if (!vpix) {
		etna_bo_del(etnaviv->conn, bo, NULL);
		return NULL;
	}

	vpix->etna_bo = bo;

	etnaviv_set_pixmap_priv(pixmap, vpix);

	return vpix;
}

PixmapPtr etnaviv_pixmap_from_dmabuf(ScreenPtr pScreen, int fd,
        CARD16 width, CARD16 height, CARD16 stride, CARD8 depth, CARD8 bpp)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pScreen);
	struct etnaviv_format fmt;
	PixmapPtr pixmap;

	if (!etnaviv_format(&fmt, depth, bpp))
		return NullPixmap;

	pixmap = etnaviv->CreatePixmap(pScreen, 0, 0, depth, 0);
	if (pixmap == NullPixmap)
		return pixmap;

	pScreen->ModifyPixmapHeader(pixmap, width, height, 0, 0, stride, NULL);

	if (!etnaviv_pixmap_attach_dmabuf(etnaviv, pixmap, fmt, fd)) {
		etnaviv->DestroyPixmap(pixmap);
		return NullPixmap;
	}

	return pixmap;
}

/* Scanout pixmaps are never tiled. */
static Bool etnaviv_import_dmabuf(ScreenPtr pScreen, PixmapPtr pPixmap, int fd)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pScreen);
	struct etnaviv_pixmap *vpix;
	struct etnaviv_format fmt;

	etnaviv_free_pixmap(pPixmap);

	if (!etnaviv_format(&fmt, pPixmap->drawable.depth,
			    pPixmap->drawable.bitsPerPixel))
		return TRUE;

	vpix = etnaviv_pixmap_attach_dmabuf(etnaviv, pPixmap, fmt, fd);
	if (!vpix)
		return FALSE;

	/*
	 * Pixmaps imported via dmabuf are write-combining, so don't
	 * need CPU cache state tracking.  We still need to track
	 * whether we have operations outstanding on the GPU.
	 */
	vpix->state |= ST_DMABUF;

#ifdef DEBUG_PIXMAP
	dbg("Pixmap %p: vPix=%p etna_bo=%p format=%u/%u/%u\n",
	    pixmap, vPix, vPix->etna_bo, fmt.format, fmt.swizzle, fmt.tile);
#endif

	return TRUE;
}


static void etnaviv_attach_name(ScreenPtr pScreen, PixmapPtr pPixmap,
	uint32_t name)
{
#ifdef HAVE_DRI2
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pScreen);
	struct etnaviv_pixmap *vPix = etnaviv_get_pixmap_priv(pPixmap);

	/* If we are using our KMS DRM for buffer management, save its name */
	if (etnaviv->dri2_armada && vPix)
		vPix->name = name;
#endif
}

static int etnaviv_export_name(ScreenPtr pScreen, uint32_t name)
{
	struct etnaviv *etnaviv = etnaviv_get_screen_priv(pScreen);
	struct etna_bo *bo;
	int fd;

	bo = etna_bo_from_name(etnaviv->conn, name);
	if (!bo) {
		xf86DrvMsg(etnaviv->scrnIndex, X_ERROR,
			   "etna_bo_from_name failed: 0x%08x: %s\n",
			   name, strerror(errno));
		return -1;
	}

	fd = etna_bo_to_dmabuf(etnaviv->conn, bo);
	etna_bo_del(etnaviv->conn, bo, NULL);
	if (fd < 0) {
		xf86DrvMsg(etnaviv->scrnIndex, X_ERROR,
			   "etna_bo_to_dmabuf failed: %s\n",
			   strerror(errno));
		return -1;
	}

	return fd;
}

const struct armada_accel_ops etnaviv_ops = {
	.pre_init	= etnaviv_pre_init,
	.screen_init	= etnaviv_ScreenInit,
	.align_bo_size	= etnaviv_align_bo_size,
	.import_dmabuf	= etnaviv_import_dmabuf,
	.attach_name	= etnaviv_attach_name,
	.free_pixmap	= etnaviv_free_pixmap,
	.xv_init	= etnaviv_xv_init,
	.export_name	= etnaviv_export_name,
};
