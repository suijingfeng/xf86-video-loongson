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
#include <fcntl.h>
#include <sys/stat.h>

#include <xf86.h>
#include <dri3.h>
#include <misyncshm.h>

#include "driver.h"
#include "gsgpu_dri3.h"
#include "gsgpu_bo_helper.h"
#include "loongson_debug.h"
#include "loongson_pixmap.h"

static int LS_IsRenderNode(int fd, struct stat *st)
{
    if (fstat(fd, st))
        return 0;

    if (!S_ISCHR(st->st_mode))
        return 0;

    return st->st_rdev & 0x80;
}


static int gsgpu_dri3_open_client(ClientPtr client,
                                   ScreenPtr pScreen,
                                   RRProviderPtr provider,
                                   int *fdp)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmode = &lsp->drmmode;
    int fd;
    drm_magic_t magic;
    int ret;
    struct stat master;

    if (LS_IsRenderNode(lsp->fd, &master))
    {
        return TRUE;
    }

    fd = open(lsp->render_node, O_RDWR | O_CLOEXEC, 0);
    if (fd < 0)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "DRI3Open: cannot open %s.\n",
                   lsp->render_node);
        return BadAlloc;
    }
    else
    {
        DEBUG_MSG("%s: %s opened in %d.",
                  __func__, lsp->render_node, fd);
    }

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
            *fdp = fd;
            return Success;
        }
        else
        {
            close(fd);
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "DRI3: cannot get magic : ret %d\n", ret);
            return BadMatch;
        }
    }

    ret = drmAuthMagic(pDrmode->fd, magic);
    if (ret < 0)
    {
        close(fd);
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "DRI3: cannot auth magic: ret %d\n", ret);
        return BadMatch;
    }

    *fdp = fd;
    return Success;
}

static struct gsgpu_bo *
gsgpu_bo_from_dma_buf_fd(struct gsgpu_device *pDev, int dmabuf_fd)
{
    struct gsgpu_bo_import_result result = {0};
    int ret;

    ret = gsgpu_bo_import(pDev,
                          gsgpu_bo_handle_type_dma_buf_fd,
                          (uint32_t)dmabuf_fd,
                          &result);

    if (ret)
    {
        xf86Msg(X_ERROR, "GSGPU: DRI3: import bo failed.\n");
        return NULL;
    }

    return result.buf_handle;
}

static PixmapPtr gsgpu_dri3_pixmap_from_fd(ScreenPtr pScreen,
                                           int fd,
                                           CARD16 width,
                                           CARD16 height,
                                           CARD16 stride,
                                           CARD8 depth,
                                           CARD8 bpp)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct gsgpu_bo *gbo = NULL;
    PixmapPtr pPixmap;
    Bool ret;

    TRACE_ENTER();

    /* width and height of 0 means don't allocate any pixmap data */
    pPixmap = pScreen->CreatePixmap(pScreen, 0, 0, depth,
                                    CREATE_PIXMAP_USAGE_DRI3);

    if (!pPixmap)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "GSGPU: DRI3: cannot create pixmap.\n");
        return NullPixmap;
    }

    ret = pScreen->ModifyPixmapHeader(pPixmap, width, height, depth, bpp,
                                      stride, NULL);
    if (ret == FALSE)
    {
        pScreen->DestroyPixmap(pPixmap);
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "GSGPU: DRI3: ModifyPixmapHeader failed.\n");
        return NullPixmap;
    }

    gbo = gsgpu_bo_from_dma_buf_fd(lsp->gsgpu, fd);

    if (NULL == gbo)
    {
        pScreen->DestroyPixmap(pPixmap);

        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "GSGPU: DRI3: bo from dma buf failed: %dx%d %d/%d %d->%d",
                   width, height, depth, bpp, stride, pPixmap->devKind);
        return NULL;
    }

    ret = gsgpu_set_pixmap_bo(pScrn, pPixmap, gbo, fd);
    if (ret == FALSE)
    {
        pScreen->DestroyPixmap(pPixmap);
        gsgpu_bo_free(gbo);

        return NULL;
    }

    TRACE_EXIT();
    return pPixmap;
}

static int gsgpu_dri3_fd_from_pixmap(ScreenPtr pScreen,
                                     PixmapPtr pixmap,
                                     CARD16 *stride,
                                     CARD32 *size)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    struct gsgpu_bo *gbo;
    struct gsgpu_bo_info bo_info;
    uint32_t prime_fd;
    int ret;

    TRACE_ENTER();

    gbo = gsgpu_get_pixmap_bo(pixmap);
    if (gbo == NULL)
    {
        return -1;
    }

    if (gsgpu_bo_query_info(gbo, &bo_info) != 0)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to get bo info\n");

        return -1;
    }

    ret = gsgpu_bo_export(gbo, gsgpu_bo_handle_type_dma_buf_fd, &prime_fd);
    if (ret != 0)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to get dmabuf fd from gsgpu bo: %d\n", ret);
        return ret;
    }

    *stride = pixmap->devKind;
    *size = bo_info.alloc_size;

    TRACE_EXIT();

    return prime_fd;
}

static const dri3_screen_info_rec gsgpu_dri3_info = {
    .version = 1,
    .open_client = gsgpu_dri3_open_client,
    .pixmap_from_fd = gsgpu_dri3_pixmap_from_fd,
    .fd_from_pixmap = gsgpu_dri3_fd_from_pixmap,
};


Bool gsgpu_dri3_init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    int fd;

    TRACE_ENTER();

    if (!miSyncShmScreenInit(pScreen))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to initialize sync support.\n");
        return FALSE;
    }

    fd = drmOpenWithType("gsgpu", NULL, DRM_NODE_RENDER);
    if (fd != -1)
    {
        drmVersionPtr version = drmGetVersion(fd);
        if (version)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Version: %d.%d.%d\n",
                       version->version_major, version->version_minor,
                       version->version_patchlevel);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Name: %s\n", version->name);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Date: %s\n", version->date);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Description: %s\n", version->desc);
            drmFreeVersion(version);
        }

        lsp->render_node = drmGetDeviceNameFromFd2(fd);

        drmClose(fd);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "DRI3 Screen init: device name: %s.\n",
               lsp->render_node);

    TRACE_EXIT();

    return dri3_screen_init(pScreen, &gsgpu_dri3_info);
}
