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
#include <fcntl.h>
#include <drm_fourcc.h>
#include <sys/stat.h>

#include <xf86.h>
#include <dri3.h>
#include <misyncshm.h>

#include "driver.h"
#include "lsdc_dri3.h"
#include "loongson_debug.h"

static int LS_IsRenderNode(int fd, struct stat *st)
{
    if (fstat(fd, st))
        return 0;

    if (!S_ISCHR(st->st_mode))
        return 0;

    return st->st_rdev & 0x80;
}

static int ms_exa_dri3_open_client(ClientPtr client,
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

    fd = open(pDrmode->dri3_device_name, O_RDWR | O_CLOEXEC, 0);
    if (fd < 0)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "DRI3Open: cannot open %s.\n",
                       pDrmode->dri3_device_name);
        return BadAlloc;
    }
    else
    {
        DEBUG_MSG("%s: %s opened in %d.",
                  __func__, pDrmode->dri3_device_name, fd);
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
                    "DRI3Open: cannot get magic : ret %d\n", ret);
            return BadMatch;
        }
    }

    ret = drmAuthMagic(pDrmode->fd, magic);
    if (ret < 0)
    {
        close(fd);
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "DRI3Open: cannot auth magic: ret %d\n", ret);
        return BadMatch;
    }

    *fdp = fd;
    return Success;
}


static PixmapPtr ms_exa_pixmap_from_fds(ScreenPtr pScreen,
                                        CARD8 num_fds,
                                        const int *fds,
                                        CARD16 width,
                                        CARD16 height,
                                        const CARD32 *strides,
                                        const CARD32 *offsets,
                                        CARD8 depth,
                                        CARD8 bpp,
                                        uint64_t modifier)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmode = &lsp->drmmode;

    PixmapPtr pPixmap;
    struct dumb_bo *bo = NULL;
    Bool ret;

    TRACE_ENTER();

    /* modifier != DRM_FORMAT_MOD_INVALID */
    if ((num_fds != 1) || offsets[0])
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
        "DRI3: num_fds=%d, offsets[0]=%d, modifier=%ld, %lld\n",
        num_fds, offsets[0], modifier, DRM_FORMAT_MOD_INVALID);

        TRACE_EXIT();
        return NULL;
    }

    /* width and height of 0 means don't allocate any pixmap data */
    pPixmap = pScreen->CreatePixmap(pScreen, 0, 0, depth,
                                    CREATE_PIXMAP_USAGE_BACKING_PIXMAP);

    if (pPixmap == NullPixmap)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "DRI3: cannot create pixmap.\n");
        TRACE_EXIT();
        return NullPixmap;
    }


    ret = pScreen->ModifyPixmapHeader(pPixmap,
                             width, height, depth, bpp, strides[0], NULL);
    if (ret == FALSE)
    {
        pScreen->DestroyPixmap(pPixmap);
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "DRI3: ModifyPixmapHeader failed.\n");
        TRACE_EXIT();
        return NullPixmap;
    }

    bo = dumb_get_bo_from_fd(pDrmode->fd, fds[0],
                             strides[0], strides[0] * height);

    DEBUG_MSG("DRI3: PixmapFromFD: pixmap:%p %dx%d %d/%d %d->%d",
        pPixmap, width, height, depth, bpp, strides[0], pPixmap->devKind);

    if (NULL == bo)
    {
        pScreen->DestroyPixmap(pPixmap);

        TRACE_EXIT();
        return NULL;
    }

    ret = ls_exa_set_pixmap_bo(pScrn, pPixmap, bo, TRUE);
    if (ret == FALSE)
    {
        pScreen->DestroyPixmap(pPixmap);
        dumb_bo_destroy(pDrmode->fd, bo);

        TRACE_EXIT();
        return NULL;;
    }

    TRACE_EXIT();
    return pPixmap;
}


static int ms_exa_egl_fd_from_pixmap(ScreenPtr pScreen,
        PixmapPtr pixmap, CARD16 *stride, CARD32 *size)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct dumb_bo *bo;
    int prime_fd;
    int ret;

    TRACE_ENTER();

    bo = dumb_bo_from_pixmap(pScreen, pixmap);
    if (bo == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: failed to get bo from pixmap\n", __func__);
        return -1;
    }

    ret = drmPrimeHandleToFD(pDrmMode->fd, bo->handle, DRM_CLOEXEC, &prime_fd);
    if (ret)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: failed to get dmabuf fd: %d\n", __func__, ret);
        return ret;
    }

    *stride = bo->pitch;
    *size = bo->size;

    TRACE_EXIT();

    return prime_fd;
}


static int ms_exa_egl_fds_from_pixmap(ScreenPtr pScreen,
                                      PixmapPtr pixmap,
                                      int *fds,
                                      uint32_t *strides,
                                      uint32_t *offsets,
                                      uint64_t *modifier)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    struct dumb_bo *bo = dumb_bo_from_pixmap(pScreen, pixmap);
    int prime_fd;
    int ret;

    if (bo == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: failed to get bo from pixmap\n", __func__);
        return 0;
    }

    ret = drmPrimeHandleToFD(pDrmMode->fd, bo->handle, DRM_CLOEXEC, &prime_fd);
    if (ret)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "%s: failed to get dmabuf fd: %d\n", __func__, ret);
        return ret;
    }

    fds[0] = prime_fd;
    strides[0] = bo->pitch;
    offsets[0] = 0;
    *modifier = DRM_FORMAT_MOD_LINEAR;

    return 1;
}

static Bool ms_exa_get_formats(ScreenPtr screen,
        CARD32 *num_formats, CARD32 **formats)
{
    /* TODO: Return formats */
    *num_formats = 0;
    return TRUE;
}


static Bool ms_exa_get_modifiers(ScreenPtr screen,
        uint32_t format, uint32_t *num_modifiers, uint64_t **modifiers)
{
    *num_modifiers = 0;
    return TRUE;
}


static Bool ms_exa_get_drawable_modifiers(DrawablePtr draw,
        uint32_t format, uint32_t *num_modifiers, uint64_t **modifiers)
{
    *num_modifiers = 0;
    return TRUE;
}


static const dri3_screen_info_rec loongson_dri3_info = {
    .version = 2,
    .open_client = ms_exa_dri3_open_client,
    .pixmap_from_fds = ms_exa_pixmap_from_fds,
    .fd_from_pixmap = ms_exa_egl_fd_from_pixmap,
    .fds_from_pixmap = ms_exa_egl_fds_from_pixmap,
    .get_formats = ms_exa_get_formats,
    .get_modifiers = ms_exa_get_modifiers,
    .get_drawable_modifiers = ms_exa_get_drawable_modifiers,
};


Bool LS_DRI3_Init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
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
                       version->version_major, version->version_minor,
                       version->version_patchlevel);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Name: %s\n", version->name);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Date: %s\n", version->date);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,"  Description: %s\n", version->desc);
            drmFreeVersion(version);
        }
        drmClose(fd);
    }

    pDrmMode->dri3_device_name = drmGetDeviceNameFromFd2(pDrmMode->fd);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
        "DRI3 Screen init: device name: %s.\n",
        pDrmMode->dri3_device_name);

    TRACE_EXIT();

    return dri3_screen_init(pScreen, &loongson_dri3_info);
}
