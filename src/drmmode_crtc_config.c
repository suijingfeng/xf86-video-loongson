#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/mman.h>
#include <unistd.h>
#include <malloc.h>
#include <drm_mode.h>

#include <xf86drm.h>

#include "driver.h"
#include "loongson_glamor.h"
#include "loongson_scanout.h"
#include "drmmode_crtc_config.h"

static void drmmode_clear_pixmap(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ls = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ls->drmmode;
    DrawablePtr pDrawable = &pPixmap->drawable;
    GCPtr gc;
#ifdef GLAMOR_HAS_GBM
    struct GlamorAPI *pGlamorAPI;
#endif

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: start\n", __func__);

#ifdef GLAMOR_HAS_GBM
    pGlamorAPI = &ls->glamor;
    if (pDrmMode->glamor_enabled)
    {
        pGlamorAPI->clear_pixmap(pPixmap);
        return;
    }
#endif

    gc = GetScratchGC(pDrawable->depth, pScreen);
    if (gc)
    {
        miClearDrawable(pDrawable, gc);
        FreeScratchGC(gc);
    }

    xf86Msg(X_INFO,
               "%s: pixmap->drawable.depth = %d\n",
               __func__, pDrawable->depth);
}

Bool drmmode_xf86crtc_resize(ScrnInfoPtr pScrn, int width, int height)
{
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    loongsonPtr ms = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;
    struct DrmModeBO * const pNewFrontBO = &pDrmMode->front_bo;
    struct DrmModeBO old_front = pDrmMode->front_bo;
    uint32_t old_fb_id;
    int i, pitch, old_width, old_height, old_pitch;
    /* user space */
    const int cpp = (pScrn->bitsPerPixel + 7) / 8;
    /* kernel space */
    const int kcpp = (pDrmMode->kbpp + 7) / 8;
    PixmapPtr ppix = pScreen->GetScreenPixmap(pScreen);
    void *new_pixels = NULL;
    Bool bo_create_res;

    if ((pScrn->virtualX == width) && (pScrn->virtualY == height))
        return TRUE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "%s: Allocate new framebuffer %dx%d, cpp=%d, kcpp=%d\n",
               __func__, width, height, cpp, kcpp);

    old_width = pScrn->virtualX;
    old_height = pScrn->virtualY;
    old_pitch = drmmode_bo_get_pitch(&pDrmMode->front_bo);
    old_fb_id = pDrmMode->fb_id;
    pDrmMode->fb_id = 0;

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        bo_create_res = ls_glamor_create_gbm_bo(pScrn,
                                                &pDrmMode->front_bo,
                                                width,
                                                height,
                                                pDrmMode->kbpp);
    }
    else
#endif
    {
         pNewFrontBO->width = (width + 63) & ~63;
         pNewFrontBO->height = height;

         pNewFrontBO->dumb = dumb_bo_create(pDrmMode->fd,
                                            pNewFrontBO->width,
                                            pNewFrontBO->height,
                                            pDrmMode->kbpp);
         if (pNewFrontBO->dumb == NULL)
         {
             xf86Msg(X_ERROR, "%s: Create Dumb BO(%dx%d, bpp=%d) failed\n",
                              __func__, width, height, pDrmMode->kbpp);

             bo_create_res = FALSE;
         }
         else
         {
             bo_create_res = TRUE;
         }
    }

    if (bo_create_res == FALSE)
        goto fail;

    pitch = drmmode_bo_get_pitch(&pDrmMode->front_bo);

    pScrn->virtualX = pNewFrontBO->width;
    pScrn->virtualY = pNewFrontBO->height;
    pScrn->displayWidth = pitch / kcpp;

    xf86Msg(X_INFO, "%s: Dumb BO(%dx%d, bpp=%d. pitch=%d) created\n",
                     __func__, pNewFrontBO->width, pNewFrontBO->height,
                               pDrmMode->kbpp, pitch);

    if (pDrmMode->gbm == NULL)
    {
        new_pixels = LS_MapFrontBO(pScrn, pDrmMode);
        if (!new_pixels)
            goto fail;
    }

    if (pDrmMode->shadow_enable)
    {
        uint32_t size = pScrn->displayWidth * pScrn->virtualY * cpp;
        new_pixels = calloc(1, size);
        if (new_pixels == NULL)
            goto fail;

        free(pDrmMode->shadow_fb);
        pDrmMode->shadow_fb = new_pixels;
    }

    if (pDrmMode->shadow_enable2)
    {
        uint32_t size = pScrn->displayWidth * pScrn->virtualY * cpp;
        void *fb2 = calloc(1, size);
        if (fb2 == NULL)
            goto fail;

        free(pDrmMode->shadow_fb2);
        pDrmMode->shadow_fb2 = fb2;
    }

    pScreen->ModifyPixmapHeader(ppix, width, height, -1, -1,
                                pScrn->displayWidth * cpp, new_pixels);

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        if (!ls_glamor_handle_new_screen_pixmap(pScrn, &pDrmMode->front_bo))
            goto fail;
    }
#endif

    drmmode_clear_pixmap(ppix);

    for (i = 0; i < xf86_config->num_crtc; i++)
    {
        xf86CrtcPtr crtc = xf86_config->crtc[i];

        if (!crtc->enabled)
            continue;

        drmmode_set_mode_major(crtc, &crtc->mode,
                               crtc->rotation, crtc->x, crtc->y);
    }

    if (old_fb_id)
    {
        drmModeRmFB(pDrmMode->fd, old_fb_id);
        drmmode_bo_destroy(pDrmMode, &old_front);
        xf86Msg(X_INFO, "%s: Old FB(%dx%d, pitch=%d, id=%u) destroyed\n",
                      __func__, old_width, old_height, old_pitch, old_fb_id);
    }

    return TRUE;

 fail:
    drmmode_bo_destroy(pDrmMode, &pDrmMode->front_bo);
    pDrmMode->front_bo = old_front;
    pScrn->virtualX = old_width;
    pScrn->virtualY = old_height;
    pScrn->displayWidth = old_pitch / kcpp;
    pDrmMode->fb_id = old_fb_id;

    return FALSE;
}


static int drmmode_create_lease(RRLeasePtr lease, int *fd)
{
    ScreenPtr screen = lease->screen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(pScrn);
    drmmode_ptr drmmode = &ms->drmmode;
    int ncrtc = lease->numCrtcs;
    int noutput = lease->numOutputs;
    int nobjects;
    int c, o;
    int i;
    int lease_fd;
    uint32_t *objects;
    drmmode_lease_private_ptr   lease_private;

    nobjects = ncrtc + noutput;

    if (ms->atomic_modeset)
        nobjects += ncrtc; /* account for planes as well */

    if (nobjects == 0)
        return BadValue;

    lease_private = calloc(1, sizeof (drmmode_lease_private_rec));
    if (!lease_private)
        return BadAlloc;

    objects = xallocarray(nobjects, sizeof (uint32_t));

    if (!objects) {
        free(lease_private);
        return BadAlloc;
    }

    i = 0;

    /* Add CRTC and plane ids */
    for (c = 0; c < ncrtc; c++) {
        xf86CrtcPtr crtc = lease->crtcs[c]->devPrivate;
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        objects[i++] = drmmode_crtc->mode_crtc->crtc_id;
        if (ms->atomic_modeset)
            objects[i++] = drmmode_crtc->plane_id;
    }

    /* Add connector ids */

    for (o = 0; o < noutput; o++) {
        xf86OutputPtr   output = lease->outputs[o]->devPrivate;
        drmmode_output_private_ptr drmmode_output = output->driver_private;

        objects[i++] = drmmode_output->mode_output->connector_id;
    }

    /* call kernel to create lease */
    assert (i == nobjects);

    lease_fd = drmModeCreateLease(drmmode->fd, objects, nobjects, 0, &lease_private->lessee_id);

    free(objects);

    if (lease_fd < 0) {
        free(lease_private);
        return BadMatch;
    }

    lease->devPrivate = lease_private;

    xf86CrtcLeaseStarted(lease);

    *fd = lease_fd;
    return Success;
}


static void drmmode_terminate_lease(RRLeasePtr lease)
{
    ScreenPtr screen = lease->screen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(screen);
    modesettingPtr ms = modesettingPTR(pScrn);
    drmmode_ptr drmmode = &ms->drmmode;
    drmmode_lease_private_ptr lease_private = lease->devPrivate;

    if (drmModeRevokeLease(drmmode->fd, lease_private->lessee_id) == 0) {
        free(lease_private);
        lease->devPrivate = NULL;
        xf86CrtcLeaseTerminated(lease);
    }
}


const xf86CrtcConfigFuncsRec ls_xf86crtc_config_funcs = {
    .resize = drmmode_xf86crtc_resize,
    .create_lease = drmmode_create_lease,
    .terminate_lease = drmmode_terminate_lease
};
