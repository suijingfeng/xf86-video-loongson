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

#include <unistd.h>
#include <xf86str.h>
#include <xf86drm.h>
#include <fcntl.h>

#include "driver.h"
#include "loongson_pixmap.h"
#include "loongson_debug.h"

#include "gsgpu_bo_helper.h"

struct gsgpu_bo *gsgpu_bo_create(struct gsgpu_device *gdev,
                                 uint32_t alloc_size,
                                 uint32_t phys_alignment,
                                 uint32_t domains)
{
    struct gsgpu_bo_alloc_request alloc_request = {0};
    struct gsgpu_bo *bo = NULL;

    alloc_request.alloc_size = alloc_size;
    alloc_request.phys_alignment = phys_alignment;
    alloc_request.preferred_heap = domains;

    if (gsgpu_bo_alloc(gdev, &alloc_request, &bo))
    {
        return NULL;
    }

    return bo;
}

struct gsgpu_bo *gsgpu_get_pixmap_bo(PixmapPtr pPixmap)
{
    struct exa_pixmap_priv *priv;

    priv = exaGetPixmapDriverPrivate(pPixmap);

    return priv ? priv->gbo : NULL;
}


Bool gsgpu_set_pixmap_bo(ScrnInfoPtr pScrn,
                         PixmapPtr pPixmap,
                         struct gsgpu_bo *gbo,
                         int prime_fd)
{
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);
    struct gsgpu_bo_info bo_info;
    int ret;

    if (priv->fd > 0)
    {
        // destroy old backing memory, and update it with new.
        close(priv->fd);
    }

    if (priv->gbo)
    {
        if (priv->gbo == gbo)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "%s: pixmap bo is already setted\n", __func__);

            return TRUE;
        }

        ret = gsgpu_bo_free(priv->gbo);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: Free old pixmap gsgpu bo: %s\n",
                   __func__, ret ? "success" : "failed");
    }

    memset(&bo_info, 0, sizeof(struct gsgpu_bo_info));
    ret = gsgpu_bo_query_info(gbo, &bo_info);
    if (ret)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "GSGPU: DRI3: query bo info failed\n");
        priv->tiling_info = 0;
    }
    else
    {
         priv->tiling_info = bo_info.metadata.tiling_info & GSGPU_SURF_MODE_MASK;
         DEBUG_MSG("pixmap %p is backing by gsgpu bo, tiling: %lx\n",
                   pPixmap, priv->tiling_info);
    }

    priv->gbo = gbo;
    priv->fd = prime_fd;

    return TRUE;
}
