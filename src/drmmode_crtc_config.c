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
#include <malloc.h>
#include <drm_mode.h>
#include <xf86drm.h>

#include "driver.h"
#include "loongson_glamor.h"
#include "loongson_shadow.h"
#include "loongson_scanout.h"
#include "drmmode_crtc_config.h"
#include "loongson_buffer.h"
#include "loongson_pixmap.h"

static void drmmode_clear_pixmap(PixmapPtr pPixmap)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    DrawablePtr pDrawable = &pPixmap->drawable;
    GCPtr gc;
#ifdef GLAMOR_HAS_GBM
    struct GlamorAPI *pGlamorAPI;
#endif

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: start\n", __func__);

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        pGlamorAPI = &lsp->glamor;

        if (pGlamorAPI->clear_pixmap)
            pGlamorAPI->clear_pixmap(pPixmap);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: finished\n", __func__);
        return;
    }
#endif

    gc = GetScratchGC(pDrawable->depth, pScreen);
    if (gc)
    {
        miClearDrawable(pDrawable, gc);
        FreeScratchGC(gc);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "%s finished: pDrawable = %p get cleared\n",
               __func__, pDrawable);
}


/*
 * Requests that the driver resize the screen.
 *
 * The driver is responsible for updating scrn->virtualX and scrn->virtualY.
 * If the requested size cannot be set, the driver should leave those values
 * alone and return FALSE.
 *
 * A naive driver that cannot reallocate the screen may simply change
 * virtual[XY].  A more advanced driver will want to also change the
 * devPrivate.ptr and devKind of the screen pixmap, update any offscreen
 * pixmaps it may have moved, and change pScrn->displayWidth.
 */
Bool drmmode_xf86crtc_resize(ScrnInfoPtr pScrn, int width, int height)
{
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct DrmModeBO *pOldFront = pDrmMode->front_bo;
    int kcpp = (pDrmMode->kbpp + 7) / 8;
    uint32_t old_fb_id;
    void *old_shadow_fb = pDrmMode->shadow_fb;
    int old_width, old_height, old_pitch;
    int i, pitch;

    PixmapPtr pRootPixmap = pScreen->GetScreenPixmap(pScreen);
    void *new_pixels = NULL;
    struct DrmModeBO *pNewFrontBO;
    Bool res;

    if ((pScrn->virtualX == width) && (pScrn->virtualY == height))
        return TRUE;

    old_width = pScrn->virtualX;
    old_height = pScrn->virtualY;
    old_pitch = drmmode_bo_get_pitch(pOldFront);
    old_fb_id = pDrmMode->fb_id;

    pScrn->virtualX = width;
    pScrn->virtualY = height;

    pDrmMode->fb_id = 0;

    if (pDrmMode->glamor_enabled)
    {
#ifdef GLAMOR_HAS_GBM
        pNewFrontBO = ls_glamor_create_gbm_bo(pScrn,
                                              width,
                                              height,
                                              pDrmMode->kbpp);
        pDrmMode->front_bo = pNewFrontBO;
#endif
    }
    else
    {
        pNewFrontBO = LS_CreateFrontBO(pScrn,
                                       lsp->fd,
                                       width,
                                       height,
                                       pDrmMode->kbpp);
        if (!pNewFrontBO)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "%s: Create front bo failed.\n", __func__);

            goto fail;
        }

        pDrmMode->front_bo = pNewFrontBO;

        new_pixels = LS_MapFrontBO(pScrn, lsp->fd, pNewFrontBO);
        if (!new_pixels)
            goto fail;

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: New Dumb BO(handle=%u) of the Front BO\n",
                   __func__, drmmode_bo_get_handle(pNewFrontBO));
    }

    if (pDrmMode->shadow_enable || pDrmMode->exa_shadow_enabled)
    {
        res = LS_ShadowAllocFB(pScrn,
                               width,
                               height,
                               pDrmMode->kbpp,
                               &pDrmMode->shadow_fb);
        if (res == FALSE)
            goto fail;

        new_pixels = pDrmMode->shadow_fb;
    }

    pitch = drmmode_bo_get_pitch(pNewFrontBO);

    if (pDrmMode->exa_enabled)
    {
        loongson_set_pixmap_dumb_bo(pScrn,
                                    pRootPixmap,
                                    pNewFrontBO->dumb,
                                    CREATE_PIXMAP_USAGE_SCANOUT,
                                    -1);
    }

    pScreen->ModifyPixmapHeader(pRootPixmap, width, height, -1, -1,
                                pitch, new_pixels);

    pScrn->displayWidth = pitch / kcpp;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "%s: New framebuffer %dx%d, %d bpp, pitch=%d, Created -> %p\n",
               __func__, width, height, pDrmMode->kbpp, pitch, pRootPixmap);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "pitch: %d, displayWidth: %d\n",
                pitch, pScrn->displayWidth);

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        if (!ls_glamor_handle_new_screen_pixmap(pScrn, pNewFrontBO))
            goto fail;
    }
#endif

    drmmode_clear_pixmap(pRootPixmap);

    for (i = 0; i < xf86_config->num_crtc; i++)
    {
        xf86CrtcPtr crtc = xf86_config->crtc[i];

        if (!crtc->enabled)
            continue;

        xf86Msg(X_INFO, "\n");

        drmmode_set_mode_major(crtc,
                               &crtc->mode,
                               crtc->rotation,
                               crtc->x,
                               crtc->y);

        xf86Msg(X_INFO, "\n");
    }

    if (old_fb_id)
    {
        LS_FreeFrontBO(pScrn, pDrmMode->fd, old_fb_id, pOldFront);
        LS_ShadowFreeFB(pScrn, &old_shadow_fb);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: Old FB(%dx%d, pitch=%d, id=%u) destroyed\n",
                   __func__, old_width, old_height, old_pitch, old_fb_id);
    }

    return TRUE;

 fail:
    drmmode_bo_destroy(pDrmMode, pDrmMode->front_bo);
    pDrmMode->front_bo = pOldFront;
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
    loongsonPtr lsp = loongsonPTR(pScrn);
    drmmode_ptr drmmode = &lsp->drmmode;
    int ncrtc = lease->numCrtcs;
    int noutput = lease->numOutputs;
    int nobjects;
    int c, o;
    int i;
    int lease_fd;
    uint32_t *objects;
    drmmode_lease_private_ptr   lease_private;

    nobjects = ncrtc + noutput;

    if (lsp->atomic_modeset)
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
        if (lsp->atomic_modeset)
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
    ScreenPtr pScreen = lease->screen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    drmmode_lease_private_ptr lease_private = lease->devPrivate;

    if (drmModeRevokeLease(pDrmMode->fd, lease_private->lessee_id) == 0)
    {
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
