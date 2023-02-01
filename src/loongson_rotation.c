/*
 * Copyright (C) 2022 Loongson Corporation
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

#include <xf86drm.h>
#include "driver.h"
#include "drmmode_display.h"
#include "loongson_scanout.h"
#include "loongson_pixmap.h"
#include "loongson_shadow.h"
#include "loongson_damage.h"
#include "loongson_rotation.h"

void *loongson_rotation_allocate_shadow(xf86CrtcPtr crtc,
                                        int width,
                                        int height)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec *pDrmMode = &lsp->drmmode;
    struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
    Bool res;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: %dx%d\n",
               __func__, width, height);

    res = loongson_create_scanout_pixmap(pScrn,
                                         width,
                                         height,
                                         &drmmode_crtc->rotate_pixmap);
    if (res == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s: %d: failed\n", __func__, __LINE__);
    }

    res = loongson_pixmap_get_fb_id(drmmode_crtc->rotate_pixmap,
                                    &drmmode_crtc->rotate_fb_id);
    if (res == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s: %d: failed\n", __func__, __LINE__);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "%s: %d: Rotated Dumb BO(%dx%d) created, rotated fb id=%d\n",
               __func__, __LINE__, width, height, drmmode_crtc->rotate_fb_id);

    pDrmMode->shadow_present = TRUE;

    return drmmode_crtc->rotate_pixmap;
}

/**
 * Create shadow pixmap for rotation support
 */
PixmapPtr loongson_rotation_create_pixmap(xf86CrtcPtr crtc,
                                          void *data,
                                          int width,
                                          int height)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;

    if (!data)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s:%d: %dx%d\n",
                   __func__, __LINE__, width, height);

        data = loongson_rotation_allocate_shadow(crtc, width, height);
        if (!data)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Couldn't allocate shadow pixmap for rotated CRTC\n");
            return NULL;
        }
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s:%d: %dx%d\n",
               __func__, __LINE__, width, height);

    return drmmode_crtc->rotate_pixmap;
}

void loongson_rotation_destroy(xf86CrtcPtr crtc,
                               PixmapPtr rotate_pixmap,
                               void *data)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec *pDrmMode = &lsp->drmmode;
    struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;

    if (rotate_pixmap)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: %d\n", __func__, __LINE__);

        rotate_pixmap->drawable.pScreen->DestroyPixmap(rotate_pixmap);
    }

    if (data)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: %d\n", __func__, __LINE__);

        drmModeRmFB(lsp->fd, drmmode_crtc->rotate_fb_id);
        drmmode_crtc->rotate_fb_id = 0;
    }

    pDrmMode->shadow_present = FALSE;
}
