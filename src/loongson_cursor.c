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
#include <stdint.h>
#include <xf86drmMode.h>
#include <xf86str.h>
#include <xf86drm.h>

#include "driver.h"
#include "drmmode_display.h"

#include "loongson_options.h"
#include "loongson_cursor.h"

// The suffix K stand for Kernel
void LS_GetCursorDimK(ScrnInfoPtr pScrn)
{
    loongsonPtr ms = loongsonPTR(pScrn);
    int ret = -1;
    uint64_t value = 0;

    // cusor related
    if (xf86ReturnOptValBool(ms->drmmode.Options, OPTION_SW_CURSOR, FALSE))
    {
        ms->drmmode.sw_cursor = TRUE;
    }

    ms->cursor_width = 64;
    ms->cursor_height = 64;

    ret = drmGetCap(ms->fd, DRM_CAP_CURSOR_WIDTH, &value);
    if (!ret)
    {
        ms->cursor_width = value;
    }

    ret = drmGetCap(ms->fd, DRM_CAP_CURSOR_HEIGHT, &value);
    if (!ret)
    {
        ms->cursor_height = value;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            " %s Cursor: width x height = %dx%d\n",
            ms->drmmode.sw_cursor ? "Software" : "Hardware",
            ms->cursor_width, ms->cursor_height );
}

/* cursor bo is just a dumb */
Bool LS_CreateCursorBO(ScrnInfoPtr pScrn, struct drmmode_rec * const drmmode)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    int width = lsp->cursor_width;
    int height = lsp->cursor_height;
    int bpp = 32;
    int i;

    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    const int nCrtc = xf86_config->num_crtc;

    for (i = 0; i < nCrtc; ++i)
    {
        xf86CrtcPtr pCrtc = xf86_config->crtc[i];
        drmmode_crtc_private_ptr drmmode_crtc = pCrtc->driver_private;
        struct dumb_bo *pCursorBO = dumb_bo_create(
                                     drmmode->fd, width, height, bpp);
        if (pCursorBO == NULL)
        {
              xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                      "Cursor BO %d created (%dx%d, bpp=%d)\n",
                      i, width, height, bpp);

              return FALSE;
        }
        drmmode_crtc->cursor_bo = pCursorBO;
    }
    return TRUE;
}

// there may be multiple cursor bo
Bool LS_MapCursorBO(ScrnInfoPtr pScrn, struct drmmode_rec * const drmmode)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    const int nCrtc = xf86_config->num_crtc;
    int i, ret;

    for (i = 0; i < nCrtc; ++i)
    {
        xf86CrtcPtr pCrtc = xf86_config->crtc[i];
        drmmode_crtc_private_ptr drmmode_crtc = pCrtc->driver_private;

        ret = dumb_bo_map(drmmode->fd, drmmode_crtc->cursor_bo);
        if (ret)
            return FALSE;

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                      "Cursor%d's BO mapped.\n", i);
    }
    return TRUE;
}

void LS_FreeCursorBO(ScrnInfoPtr pScrn, struct drmmode_rec * const pDrmMode)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    const int nCrtc = xf86_config->num_crtc;
    int i;

    for (i = 0; i < nCrtc; ++i)
    {
        xf86CrtcPtr pCrtc = xf86_config->crtc[i];
        drmmode_crtc_private_ptr drmmode_crtc = pCrtc->driver_private;

        dumb_bo_destroy(pDrmMode->fd, drmmode_crtc->cursor_bo);
        drmmode_crtc->cursor_bo = NULL;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                      "Cursor%d's BO freed.\n", i);
    }
}

