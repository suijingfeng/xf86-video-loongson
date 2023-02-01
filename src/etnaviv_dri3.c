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
 * Xorg 2D driver for the DC & GPU in LS7A1000
 *
 * Authors:
 *    Sui Jingfeng <suijingfeng@loongson.cn>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <xf86.h>
#include <dri3.h>
#include <misyncshm.h>
#include <xf86drm.h>
#include <etnaviv_drmif.h>
#include <drm_fourcc.h>

#include "driver.h"
#include "etnaviv_dri3.h"
#include "loongson_debug.h"
#include "loongson_pixmap.h"

static Bool etnaviv_dri3_authorise(struct EtnavivRec *pGpu, int fd)
{
    struct stat st;
    drm_magic_t magic;
    int ret;

    if (fstat(fd, &st) || !S_ISCHR(st.st_mode))
        return FALSE;

    /*
     * If the device is a render node, we don't need to auth it.
     * Render devices start at minor number 128 and up, though it
     * would be nice to have some other test for this.
     */
    if (st.st_rdev & 0x80)
        return TRUE;

    /*
     * Before FD passing in the X protocol with DRI3 (and increased
     * security of rendering with per-process address spaces on the
     * GPU), the kernel had to come up with a way to have the server
     * decide which clients got to access the GPU, which was done by
     * each client getting a unique (magic) number from the kernel,
     * passing it to the server, and the server then telling the
     * kernel which clients were authenticated for using the device.
     *
     * Now that we have FD passing, the server can just set up the
     * authentication on its own and hand the prepared FD off to the
     * client.
     */
    ret = drmGetMagic(fd, &magic);
    if (ret < 0)
    {
        if (errno == EACCES)
        {
            /* Assume that we're on a render node, and the fd is
             * already as authenticated as it should be.
             */
            return TRUE;
        }
        else
        {
            close(fd);
            xf86Msg(X_ERROR, "DRI3: cannot get magic: %d\n", ret);
            return FALSE;
        }
    }

    ret = drmAuthMagic(pGpu->fd, magic);
    if (ret < 0)
    {
        close(fd);
        xf86Msg(X_ERROR, "DRI3: cannot auth magic: %d\n", ret);
        return FALSE;
    }

    return TRUE;
}

static int etnaviv_dri3_open(ScreenPtr pScreen, RRProviderPtr provider, int *o)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct EtnavivRec *pGpu = &lsp->etnaviv;
    int fd;

    TRACE_ENTER();

    fd = open(pGpu->render_node, O_RDWR | O_CLOEXEC);
    if (fd < 0)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "DRI3: cannot open %s\n",
                   pGpu->render_node);

        return BadAlloc;
    }

    if (!etnaviv_dri3_authorise(pGpu, fd))
    {
        close(fd);
        return BadMatch;
    }

    *o = fd;

    TRACE_EXIT();

    return Success;
}

static PixmapPtr etnaviv_dri3_pixmap_from_fd(ScreenPtr pScreen,
                                             int dmabuf_fd,
                                             CARD16 width,
                                             CARD16 height,
                                             CARD16 stride,
                                             CARD8 depth,
                                             CARD8 bpp)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct EtnavivRec *gpu = &lsp->etnaviv;
    struct etna_bo *ebo = NULL;
    struct exa_pixmap_priv *priv = NULL;
    PixmapPtr pPixmap = NULL;
    Bool ret;

    TRACE_ENTER();

    /* width and height of 0 means don't allocate any pixmap data */
    pPixmap = pScreen->CreatePixmap(pScreen, 0, 0, depth,
                                    CREATE_PIXMAP_USAGE_DRI3);

    if (pPixmap == NullPixmap)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "DRI3: cannot create pixmap\n");
        return NullPixmap;
    }

    ret = pScreen->ModifyPixmapHeader(pPixmap, width, height, depth, bpp,
                                      stride, NULL);
    if (ret == FALSE)
    {
        pScreen->DestroyPixmap(pPixmap);
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "DRI3: ModifyPixmapHeader failed.\n");
        return NullPixmap;
    }

    ebo = etna_bo_from_dmabuf(gpu->dev, dmabuf_fd);
    if (!ebo)
    {
        pScreen->DestroyPixmap(pPixmap);
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "DRI3: get bo from fd(%d) failed: %dx%d, %d, %d, %d",
                   dmabuf_fd, width, height, depth, bpp, stride);

        return NullPixmap;
    }

    priv = exaGetPixmapDriverPrivate(pPixmap);

    priv->etna_bo = ebo;
    priv->pitch = stride;
    priv->fd = dmabuf_fd;
    priv->is_dumb = FALSE;
    priv->width = width;
    priv->height = height;

    /* TODO; get it for the bo */
    priv->tiling_info = DRM_FORMAT_MOD_VIVANTE_TILED;

    priv->tiling_info = DRM_FORMAT_MOD_VIVANTE_SUPER_TILED;

    return pPixmap;
}

static struct etna_bo *
etna_bo_from_pixmap(ScreenPtr pScreen, PixmapPtr pPixmap)
{
    struct exa_pixmap_priv *priv = exaGetPixmapDriverPrivate(pPixmap);
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);

    if (priv == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: priv is NULL\n", __func__);
        return NULL;
    }

    if (lsp->exaDrvPtr == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: exaDrvPtr is NULL\n", __func__);
        return NULL;
    }

    return priv->etna_bo;
}

static int etnaviv_dri3_fd_from_pixmap(ScreenPtr pScreen,
                                       PixmapPtr pPixmap,
                                       CARD16 *stride,
                                       CARD32 *size)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct etna_bo *bo;
    int prime_fd;

    TRACE_ENTER();

    bo = etna_bo_from_pixmap(pScreen, pPixmap);
    if (bo == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "dri3: failed to get bo from pixmap\n");
        return -1;
    }

    prime_fd = etna_bo_dmabuf(bo);

    *stride = pPixmap->devKind;
    *size = etna_bo_size(bo);

    TRACE_EXIT();

    return prime_fd;
}

static dri3_screen_info_rec etnaviv_dri3_info = {
    .version = 0,
    .open = etnaviv_dri3_open,
    .pixmap_from_fd = etnaviv_dri3_pixmap_from_fd,
    .fd_from_pixmap = etnaviv_dri3_fd_from_pixmap,
};


Bool etnaviv_dri3_ScreenInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct EtnavivRec *gpu = &lsp->etnaviv;
    int fd;

    TRACE_ENTER();

    if (!miSyncShmScreenInit(pScreen))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to initialize sync support.\n");
        return FALSE;
    }

    fd = drmOpenWithType("etnaviv", NULL, DRM_NODE_RENDER);
    if (fd != -1)
    {
        drmVersionPtr version = drmGetVersion(fd);
        if (version)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Version: %d.%d.%d\n",
                       version->version_major,
                       version->version_minor,
                       version->version_patchlevel);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Name: %s\n", version->name);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Date: %s\n", version->date);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Description: %s\n", version->desc);
            drmFreeVersion(version);
        }

        gpu->render_node = drmGetDeviceNameFromFd2(fd);
        drmClose(fd);
    }

    if (gpu->render_node == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "DRI3: failed to open renderer node\n");
        return FALSE;

    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "DRI3: renderer node: %s\n",
                   gpu->render_node);
    }

    TRACE_EXIT();

    return dri3_screen_init(pScreen, &etnaviv_dri3_info);
}
