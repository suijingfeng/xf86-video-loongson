#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <fcntl.h>
#include <xf86.h>

#ifdef XSERVER_PLATFORM_BUS
#include <xf86platformBus.h>
#endif

#include <randrstr.h>

#include "driver.h"
#include "vblank.h"
#include "loongson_prime.h"
#include "loongson_randr.h"
#include "drmmode_crtc_config.h"

static Bool drmmode_set_target_scanout_pixmap_gpu(xf86CrtcPtr pCrtc,
                                                  PixmapPtr ppix,
                                                  PixmapPtr *target)
{
    ScreenPtr screen = xf86ScrnToScreen(pCrtc->scrn);
    PixmapPtr screenpix = screen->GetScreenPixmap(screen);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pCrtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = pCrtc->driver_private;
    struct drmmode_rec * const drmmode = drmmode_crtc->drmmode;
    int c, total_width = 0, max_height = 0, this_x = 0;

    if (*target)
    {
        PixmapStopDirtyTracking(&(*target)->drawable, screenpix);
        if (drmmode->fb_id) {
            drmModeRmFB(drmmode->fd, drmmode->fb_id);
            drmmode->fb_id = 0;
        }
        drmmode_crtc->prime_pixmap_x = 0;
        *target = NULL;
    }

    if (!ppix)
        return TRUE;

    /* iterate over all the attached crtcs to work out the bounding box */
    for (c = 0; c < xf86_config->num_crtc; c++)
    {
        xf86CrtcPtr iter = xf86_config->crtc[c];
        if (!iter->enabled && iter != pCrtc)
            continue;
        if (iter == pCrtc) {
            this_x = total_width;
            total_width += ppix->drawable.width;
            if (max_height < ppix->drawable.height)
                max_height = ppix->drawable.height;
        } else {
            total_width += iter->mode.HDisplay;
            if (max_height < iter->mode.VDisplay)
                max_height = iter->mode.VDisplay;
        }
    }

    if ((total_width != screenpix->drawable.width) ||
        (max_height != screenpix->drawable.height))
    {

        if (!drmmode_xf86crtc_resize(pCrtc->scrn, total_width, max_height))
            return FALSE;

        screenpix = screen->GetScreenPixmap(screen);
        screen->width = screenpix->drawable.width = total_width;
        screen->height = screenpix->drawable.height = max_height;
    }
    drmmode_crtc->prime_pixmap_x = this_x;
    PixmapStartDirtyTracking(&ppix->drawable, screenpix, 0, 0, this_x, 0,
                             RR_Rotate_0);
    *target = ppix;
    return TRUE;
}

static Bool drmmode_set_target_scanout_pixmap_cpu(xf86CrtcPtr crtc,
                                                  PixmapPtr ppix,
                                                  PixmapPtr *target)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    msPixmapPrivPtr ppriv;
    void *ptr;

    if (*target) {
        ppriv = msGetPixmapPriv(drmmode, *target);
        drmModeRmFB(drmmode->fd, ppriv->fb_id);
        ppriv->fb_id = 0;
        if (ppriv->slave_damage) {
            DamageUnregister(ppriv->slave_damage);
            ppriv->slave_damage = NULL;
        }
        *target = NULL;
    }

    if (!ppix)
        return TRUE;

    ppriv = msGetPixmapPriv(drmmode, ppix);
    if (!ppriv->slave_damage) {
        ppriv->slave_damage = DamageCreate(NULL, NULL,
                                           DamageReportNone,
                                           TRUE,
                                           crtc->randr_crtc->pScreen,
                                           NULL);
    }
    ptr = drmmode_map_slave_bo(drmmode, ppriv);
    ppix->devPrivate.ptr = ptr;
    DamageRegister(&ppix->drawable, ppriv->slave_damage);

    if (ppriv->fb_id == 0) {
        drmModeAddFB(drmmode->fd, ppix->drawable.width,
                     ppix->drawable.height,
                     ppix->drawable.depth,
                     ppix->drawable.bitsPerPixel,
                     ppix->devKind, ppriv->backing_bo->handle, &ppriv->fb_id);
    }
    *target = ppix;
    return TRUE;
}

Bool drmmode_set_target_scanout_pixmap(xf86CrtcPtr crtc,
                                       PixmapPtr ppix,
                                       PixmapPtr *target)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    if ( 0 /* drmmode->reverse_prime_offload_mode */)
        return drmmode_set_target_scanout_pixmap_gpu(crtc, ppix, target);
    else
        return drmmode_set_target_scanout_pixmap_cpu(crtc, ppix, target);
}


static Bool drmmode_EnableSharedPixmapFlipping(xf86CrtcPtr crtc,
                                               drmmode_ptr drmmode,
                                               PixmapPtr front,
                                               PixmapPtr back)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    drmmode_crtc->enable_flipping = TRUE;

    /* Set front scanout pixmap */
    drmmode_crtc->enable_flipping &=
        drmmode_set_target_scanout_pixmap(crtc, front,
                                          &drmmode_crtc->prime_pixmap);
    if (!drmmode_crtc->enable_flipping)
        return FALSE;

    /* Set back scanout pixmap */
    drmmode_crtc->enable_flipping &=
        drmmode_set_target_scanout_pixmap(crtc, back,
                                          &drmmode_crtc->prime_pixmap_back);
    if (!drmmode_crtc->enable_flipping)
    {
        drmmode_set_target_scanout_pixmap(crtc, NULL,
                                          &drmmode_crtc->prime_pixmap);
        return FALSE;
    }

    return TRUE;
}


static Bool msEnableSharedPixmapFlipping(RRCrtcPtr crtc,
                                         PixmapPtr front,
                                         PixmapPtr back)
{
    ScreenPtr screen = crtc->pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(scrn);
    EntityInfoPtr pEnt = ms->pEnt;
    xf86CrtcPtr xf86Crtc = crtc->devPrivate;

    if (!xf86Crtc)
        return FALSE;

    /* Not supported if we can't flip */
    if (!ms->drmmode.pageflip)
        return FALSE;

#ifdef XSERVER_PLATFORM_BUS
    if (pEnt->location.type == BUS_PLATFORM)
    {
        char *syspath =
            xf86_platform_device_odev_attributes(pEnt->location.id.plat)->syspath;

        /* Not supported for devices using USB transport due to misbehaved
         * vblank events */
        if (syspath && strstr(syspath, "usb"))
            return FALSE;

        /* EVDI uses USB transport but is platform device, not usb.
         * Blacklist it explicitly */
        if (syspath && strstr(syspath, "evdi"))
        {
            return FALSE;
        }
    }
#endif

    return drmmode_EnableSharedPixmapFlipping(xf86Crtc, &ms->drmmode, front, back);
}


void drmmode_FiniSharedPixmapFlipping(xf86CrtcPtr crtc, drmmode_ptr drmmode)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    if (!drmmode_crtc->flipping_active)
        return;

    drmmode_crtc->flipping_active = FALSE;

    /* Abort page flip event handler on prime_pixmap */
    {
        uint32_t seq;
        msPixmapPrivPtr pPrivPixmap;

        pPrivPixmap = msGetPixmapPriv(drmmode, drmmode_crtc->prime_pixmap);
        seq = pPrivPixmap->flip_seq;
        if (seq)
            ms_drm_abort_seq(crtc->scrn, seq);
    }


    /* Abort page flip event handler on prime_pixmap_back */
    {
        uint32_t seq;
        msPixmapPrivPtr pPrivPixmap;

        pPrivPixmap = msGetPixmapPriv(drmmode, drmmode_crtc->prime_pixmap_back);
        seq = pPrivPixmap->flip_seq;
        if (seq)
            ms_drm_abort_seq(crtc->scrn, seq);
    }
}



static void drmmode_DisableSharedPixmapFlipping(xf86CrtcPtr crtc,
                                                drmmode_ptr drmmode)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    drmmode_crtc->enable_flipping = FALSE;

    drmmode_FiniSharedPixmapFlipping(crtc, drmmode);

    drmmode_set_target_scanout_pixmap(crtc, NULL, &drmmode_crtc->prime_pixmap);

    drmmode_set_target_scanout_pixmap(crtc, NULL,
                                      &drmmode_crtc->prime_pixmap_back);
}

static void msDisableSharedPixmapFlipping(RRCrtcPtr crtc)
{
    ScreenPtr pScreen = crtc->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(pScrn);
    xf86CrtcPtr xf86Crtc = crtc->devPrivate;

    if (xf86Crtc)
    {
        drmmode_DisableSharedPixmapFlipping(xf86Crtc, &ms->drmmode);
    }
}

static PixmapDirtyUpdatePtr ms_dirty_get_ent(ScreenPtr pScreen, PixmapPtr slave_dst)
{
    PixmapDirtyUpdatePtr ent;

    if (xorg_list_is_empty(&pScreen->pixmap_dirty_list))
    {
        return NULL;
    }

    xorg_list_for_each_entry(ent, &pScreen->pixmap_dirty_list, ent)
    {
        if (ent->slave_dst == slave_dst)
            return ent;
    }

    return NULL;
}



static Bool msStartFlippingPixmapTracking(RRCrtcPtr crtc, DrawablePtr src,
                              PixmapPtr slave_dst1, PixmapPtr slave_dst2,
                              int x, int y, int dst_x, int dst_y,
                              Rotation rotation)
{
    ScreenPtr pScreen = src->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    modesettingPtr ms = modesettingPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;

    msPixmapPrivPtr ppriv1 = msGetPixmapPriv(pDrmMode, slave_dst1->master_pixmap);
    msPixmapPrivPtr ppriv2 = msGetPixmapPriv(pDrmMode, slave_dst2->master_pixmap);

    if (!PixmapStartDirtyTracking(src, slave_dst1, x, y, dst_x, dst_y, rotation))
    {
        return FALSE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Pixmap Dirty Tracking On slave_dst1 Started\n");

    if (!PixmapStartDirtyTracking(src, slave_dst2, x, y, dst_x, dst_y, rotation))
    {
        PixmapStopDirtyTracking(src, slave_dst1);
        return FALSE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Pixmap Dirty Tracking On slave_dst2 Started\n");

    ppriv1->slave_src = src;
    ppriv2->slave_src = src;

    ppriv1->dirty = ms_dirty_get_ent(pScreen, slave_dst1);
    ppriv2->dirty = ms_dirty_get_ent(pScreen, slave_dst2);

    ppriv1->defer_dirty_update = TRUE;
    ppriv2->defer_dirty_update = TRUE;

    return TRUE;
}

void LS_InitRandR(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Hook up RandR related stuff.\n");

    if (dixPrivateKeyRegistered(rrPrivKey))
    {
        rrScrPrivPtr pScrPriv = rrGetScrPriv(pScreen);

        pScrPriv->rrEnableSharedPixmapFlipping = msEnableSharedPixmapFlipping;
        pScrPriv->rrDisableSharedPixmapFlipping = msDisableSharedPixmapFlipping;

        pScrPriv->rrStartFlippingPixmapTracking = msStartFlippingPixmapTracking;
    }
}
