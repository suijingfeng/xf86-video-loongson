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

#include <errno.h>
#include <unistd.h>
#include <malloc.h>

#include "dumb_bo.h"
#include "driver.h"
#include "loongson_scanout.h"

//
// NOTE: loongson's front bo(scanout buffer) is a dumb,
// not a gbm bo, we want it clear
Bool LS_CreateFrontBO(ScrnInfoPtr pScrn, struct drmmode_rec * const pDrmMode)
{
    // What's the difference between ms->drmmode and drmmode ?
    int bpp = pDrmMode->kbpp;
    int cpp = (bpp + 7) / 8;
    int width = pScrn->virtualX;
    int height = pScrn->virtualY;
    struct DrmModeBO * const pFrontBO = &pDrmMode->front_bo;

    pFrontBO->dumb = dumb_bo_create(pDrmMode->fd, width, height, bpp);
    if (pFrontBO->dumb == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
            "%s: create dumb BO(%dx%d, bpp=%d) failed\n",
            __func__, width, height, bpp);
        return FALSE;
    }

    pFrontBO->width = width;
    pFrontBO->height = height;

    /* pitch */
    pScrn->displayWidth = pFrontBO->dumb->pitch / cpp;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "%s: front BO (%dx%d, bpp=%d) created\n",
            __func__, width, height, bpp);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "CPP (number of bytes per pixel) = %d\n", cpp);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "Display Width (number of pixels a line) = %d\n",
             pScrn->displayWidth);

    return TRUE;
}

void LS_FreeFrontBO(ScrnInfoPtr pScrn, struct drmmode_rec * const pDrmMode)
{
    int ret;
    drmmode_bo * const pFBO = &pDrmMode->front_bo;

    if (pDrmMode->fb_id)
    {
        drmModeRmFB(pDrmMode->fd, pDrmMode->fb_id);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Front BO(fb_id = %d) get removed from FB.\n", pDrmMode->fb_id);
        pDrmMode->fb_id = 0;
    }

    // suijingfeng : check is not necessary ...
    // for loongson-drm, we are sure that the front bo is just a dumb ...
    if (pFBO->dumb)
    {
        ret = dumb_bo_destroy(pDrmMode->fd, pFBO->dumb);
        if (ret == 0)
        {
            pFBO->dumb = NULL;
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "Front BO get freed.\n");
        }
    }
}


// Is a DUMB, not GBM, this is guarenteed.
void *LS_MapFrontBO(ScrnInfoPtr pScrn, struct drmmode_rec * const pDrmMode)
{
    int ret;
    struct DrmModeBO * const pFBO = &pDrmMode->front_bo;

    if (pFBO->dumb->ptr)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "Front BO already Mapped.\n");
        return pFBO->dumb->ptr;
    }

    ret = dumb_bo_map(pDrmMode->fd, pFBO->dumb);
    if (ret)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                                     "Failed map front BO: %d.\n", ret);
        return NULL;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: Front BO Mapped.\n", __func__);

    return pFBO->dumb->ptr;
}
