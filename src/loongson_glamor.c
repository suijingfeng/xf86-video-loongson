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
// dlsym()
#include <dlfcn.h>
#include <fcntl.h>
#include <malloc.h>
#include <xf86.h>
#include <drm_fourcc.h>

#include "driver.h"
#include "loongson_options.h"
#include "loongson_glamor.h"


#ifdef GLAMOR_HAS_GBM
#define GLAMOR_FOR_XORG 1
#include <glamor.h>
#include <gbm.h>
#endif

#ifdef GLAMOR_HAS_GBM

static Bool load_glamor(ScrnInfoPtr pScrn)
{
    void *mod = xf86LoadSubModule(pScrn, GLAMOR_EGL_MODULE_NAME);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct GlamorAPI * const pGlamor = &lsp->glamor;

    if (!mod)
        return FALSE;

    pGlamor->back_pixmap_from_fd = LoaderSymbol("glamor_back_pixmap_from_fd");
    pGlamor->block_handler = LoaderSymbol("glamor_block_handler");
    pGlamor->clear_pixmap = LoaderSymbol("glamor_clear_pixmap");
    pGlamor->egl_create_textured_pixmap = LoaderSymbol("glamor_egl_create_textured_pixmap");
    pGlamor->egl_create_textured_pixmap_from_gbm_bo = LoaderSymbol("glamor_egl_create_textured_pixmap_from_gbm_bo");
    pGlamor->egl_exchange_buffers = LoaderSymbol("glamor_egl_exchange_buffers");
    pGlamor->egl_get_gbm_device = LoaderSymbol("glamor_egl_get_gbm_device");
    pGlamor->egl_init = LoaderSymbol("glamor_egl_init");
    pGlamor->finish = LoaderSymbol("glamor_finish");
    pGlamor->gbm_bo_from_pixmap = LoaderSymbol("glamor_gbm_bo_from_pixmap");
    pGlamor->init = LoaderSymbol("glamor_init");
    pGlamor->name_from_pixmap = LoaderSymbol("glamor_name_from_pixmap");
    pGlamor->set_drawable_modifiers_func = LoaderSymbol("glamor_set_drawable_modifiers_func");
    pGlamor->shareable_fd_from_pixmap = LoaderSymbol("glamor_shareable_fd_from_pixmap");
    pGlamor->supports_pixmap_import_export = LoaderSymbol("glamor_supports_pixmap_import_export");
    pGlamor->xv_init = LoaderSymbol("glamor_xv_init");
    pGlamor->egl_get_driver_name = LoaderSymbol("glamor_egl_get_driver_name");

    return TRUE;
}

#endif


Bool try_enable_glamor(ScrnInfoPtr pScrn)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    const char *accel_method_str = xf86GetOptValString(pDrmMode->Options,
                                                       OPTION_ACCEL_METHOD);
#ifdef GLAMOR_HAS_GBM
    struct GlamorAPI * const pGlamor = &lsp->glamor;
#endif
    Bool do_glamor = (!accel_method_str ||
                      (strcmp(accel_method_str, "glamor") == 0));

    pDrmMode->glamor_enabled = FALSE;

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->force_24_32)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                       "Cannot use glamor with 24bpp packed fb\n");
        return FALSE;
    }

    if (do_glamor == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "glamor disabled\n");
        return FALSE;
    }

    if (load_glamor(pScrn))
    {
        if (pGlamor->egl_init(pScrn, lsp->fd))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "glamor initialized\n");
            pDrmMode->glamor_enabled = TRUE;
            return TRUE;
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "glamor initialization failed\n");
        }
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to load glamor module.\n");
        return FALSE;
    }
#else
    if (do_glamor)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "No glamor support in the X Server\n");

        return FALSE;
    }
#endif

    return pDrmMode->glamor_enabled;
}

uint32_t get_opaque_format(uint32_t format)
{
    switch (format)
    {
    case DRM_FORMAT_ARGB8888:
        return DRM_FORMAT_XRGB8888;
    case DRM_FORMAT_ARGB2101010:
        return DRM_FORMAT_XRGB2101010;
    default:
        return format;
    }
}

#ifdef GBM_BO_WITH_MODIFIERS
static uint32_t get_modifiers_set(ScrnInfoPtr pScrn,
                                  uint32_t format,
                                  uint64_t **modifiers,
                                  Bool enabled_crtc_only,
                                  Bool exclude_multiplane)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    int c, i, j, k, count_modifiers = 0;
    uint64_t *tmp, *ret = NULL;

    /* BOs are imported as opaque surfaces, so pretend the same thing here */
    format = get_opaque_format(format);

    *modifiers = NULL;
    for (c = 0; c < xf86_config->num_crtc; c++)
    {
        xf86CrtcPtr crtc = xf86_config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        if (enabled_crtc_only && !crtc->enabled)
            continue;

        for (i = 0; i < drmmode_crtc->num_formats; i++) {
            drmmode_format_ptr iter = &drmmode_crtc->formats[i];

            if (iter->format != format)
                continue;

            for (j = 0; j < iter->num_modifiers; j++) {
                Bool found = FALSE;

                /* Don't choose multi-plane formats for our screen pixmap.
                 * These will get used with frontbuffer rendering, which will
                 * lead to worse-than-tearing with multi-plane formats, as the
                 * primary and auxiliary planes go out of sync. */
                if (exclude_multiplane &&
                    gbm_device_get_format_modifier_plane_count(pDrmMode->gbm,
                                  format, iter->modifiers[j]) > 1) {
                    continue;
                }

                for (k = 0; k < count_modifiers; k++) {
                    if (iter->modifiers[j] == ret[k])
                        found = TRUE;
                }
                if (!found) {
                    count_modifiers++;
                    tmp = realloc(ret, count_modifiers * sizeof(uint64_t));
                    if (!tmp) {
                        free(ret);
                        return 0;
                    }
                    ret = tmp;
                    ret[count_modifiers - 1] = iter->modifiers[j];
                }
            }
        }
    }

    *modifiers = ret;
    return count_modifiers;
}

/*
static present_screen_priv_ptr present_screen_priv(ScreenPtr screen)
{
    return (present_screen_priv_ptr)dixLookupPrivate(&(screen)->devPrivates, &present_screen_private_key);
}

static Bool present_can_window_flip(WindowPtr window)
{
    ScreenPtr screen = window->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);

    return screen_priv->can_window_flip(window);
}
*/

static Bool get_drawable_modifiers(DrawablePtr draw, uint32_t format,
                       uint32_t *num_modifiers, uint64_t **modifiers)
{
    ScreenPtr pScreen = draw->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;

/*
    TODO : here we hasty comment this, uncomment this and made it work.
    if (!present_can_window_flip((WindowPtr) draw))
    {
        *num_modifiers = 0;
        *modifiers = NULL;
        return TRUE;
    }
*/
    if (!pDrmMode->pageflip || pDrmMode->dri2_flipping || !pScrn->vtSema)
    {
        *num_modifiers = 0;
        *modifiers = NULL;
        return TRUE;
    }

    *num_modifiers = get_modifiers_set(pScrn, format, modifiers, TRUE, FALSE);
    return TRUE;
}
#endif

Bool ls_glamor_create_gbm_bo(ScrnInfoPtr pScrn,
                             drmmode_bo *bo,
                             unsigned width,
                             unsigned height,
                             unsigned bpp)
{
#ifdef GLAMOR_HAS_GBM
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;

#ifdef GBM_BO_WITH_MODIFIERS
    uint32_t num_modifiers;
    uint64_t *modifiers = NULL;
#endif
    uint32_t format;

    bo->width = width;
    bo->height = height;

    switch (pScrn->depth)
    {
        case 15:
            format = GBM_FORMAT_ARGB1555;
            break;
        case 16:
            format = GBM_FORMAT_RGB565;
            break;
        case 30:
            format = GBM_FORMAT_ARGB2101010;
            break;
        default:
            format = GBM_FORMAT_ARGB8888;
            break;
    }

#ifdef GBM_BO_WITH_MODIFIERS
    num_modifiers = get_modifiers_set(pScrn, format, &modifiers, FALSE, TRUE);
    if ((num_modifiers > 0) &&
            !((num_modifiers == 1) && (modifiers[0] == DRM_FORMAT_MOD_INVALID)))
    {
            bo->gbm = gbm_bo_create_with_modifiers(pDrmMode->gbm,
                                                   width,
                                                   height,
                                                   format,
                                                   modifiers,
                                                   num_modifiers);
            free(modifiers);
            if (bo->gbm)
            {
                bo->used_modifiers = TRUE;
                return TRUE;
            }
    }
#endif

    bo->gbm = gbm_bo_create(pDrmMode->gbm,
                            width,
                            height,
                            format,
                            GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
    bo->used_modifiers = FALSE;

    return bo->gbm != NULL;
#endif
    return TRUE;
}



Bool ls_glamor_init(ScrnInfoPtr pScrn)
{
#ifdef GLAMOR_HAS_GBM
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    loongsonPtr ls = loongsonPTR(pScrn);
    struct GlamorAPI * const pGlamor = &ls->glamor;

    if (pGlamor->init(pScreen, GLAMOR_USE_EGL_SCREEN) == FALSE)
    {
        return FALSE;
    }

#ifdef GBM_BO_WITH_MODIFIERS
    pGlamor->set_drawable_modifiers_func(pScreen, get_drawable_modifiers);
#endif
#endif

    return TRUE;
}

Bool glamor_set_pixmap_bo(ScrnInfoPtr pScrn,
                          PixmapPtr pPixmap,
                          drmmode_bo *bo)
{
#ifdef GLAMOR_HAS_GBM
    loongsonPtr ls = loongsonPTR(pScrn);
    struct GlamorAPI * const pGlamor = &ls->glamor;

    if (!pGlamor->egl_create_textured_pixmap_from_gbm_bo(pPixmap,
                    bo->gbm, bo->used_modifiers))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create pixmap\n");
        return FALSE;
    }
#endif

    return TRUE;
}

Bool ls_glamor_handle_new_screen_pixmap(ScrnInfoPtr pScrn,
                                        struct DrmModeBO * const pFrontBO)
{
#ifdef GLAMOR_HAS_GBM
    loongsonPtr ls = loongsonPTR(pScrn);
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    PixmapPtr screen_pixmap = pScreen->GetScreenPixmap(pScreen);
    struct GlamorAPI * const pGlamorAPI = &ls->glamor;

    if (!pGlamorAPI->egl_create_textured_pixmap_from_gbm_bo(
                    screen_pixmap,
                    pFrontBO->gbm,
                    pFrontBO->used_modifiers))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create pixmap\n");
        return FALSE;
    }
#endif

    return TRUE;
}
