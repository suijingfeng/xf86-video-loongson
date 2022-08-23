/*
 * Vivante GPU Acceleration Xorg driver
 *
 * Written by Russell King, 2015
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

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
#include "etnaviv_dri3.h"
#include "loongson_debug.h"

static Bool etnaviv_dri3_authorise(struct drmmode_rec * const pDrmmode, int fd)
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

    ret = drmAuthMagic(pDrmmode->fd, magic);
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
    struct drmmode_rec * const pDrmmode = &lsp->drmmode;
    int fd;

    TRACE_ENTER();

    fd = open(pDrmmode->dri3_device_name, O_RDWR | O_CLOEXEC);
    if (fd < 0)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "DRI3: cannot open %s.\n",
                   pDrmmode->dri3_device_name);

        return BadAlloc;
    }

    if (!etnaviv_dri3_authorise(pDrmmode, fd))
    {
        close(fd);
        return BadMatch;
    }

    *o = fd;

    TRACE_EXIT();

    return Success;
}

/*
static PixmapPtr etnaviv_dri3_pixmap_from_fd(ScreenPtr pScreen,
                                             int fd,
                                             CARD16 width,
                                             CARD16 height,
                                             CARD16 stride,
                                             CARD8 depth,
                                             CARD8 bpp)
{
    struct etnaviv *etnaviv = etnaviv_get_screen_priv(pScreen);
    struct etnaviv_format fmt;
    PixmapPtr pixmap;

    if (!etnaviv_format(&fmt, depth, bpp))
        return NullPixmap;

    pixmap = etnaviv->CreatePixmap(pScreen, 0, 0, depth, 0);
    if (pixmap == NullPixmap)
        return pixmap;

    pScreen->ModifyPixmapHeader(pixmap, width, height, 0, 0, stride, NULL);

    if (!etnaviv_pixmap_attach_dmabuf(etnaviv, pixmap, fmt, fd))
    {
        etnaviv->DestroyPixmap(pixmap);
        return NullPixmap;
    }

    return pixmap;
}
*/

static PixmapPtr etnaviv_dri3_pixmap_from_fd(ScreenPtr pScreen,
                                             int fd,
                                             CARD16 width,
                                             CARD16 height,
                                             CARD16 stride,
                                             CARD8 depth,
                                             CARD8 bpp)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmmode = &lsp->drmmode;
    struct dumb_bo *bo = NULL;
    PixmapPtr pPixmap;
    Bool ret;

    TRACE_ENTER();

    /* width and height of 0 means don't allocate any pixmap data */
    pPixmap = pScreen->CreatePixmap(pScreen, 0, 0, depth,
                                    CREATE_PIXMAP_USAGE_BACKING_PIXMAP);

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

    /* pDrmmode->fd or GPU fd ? */
    bo = dumb_get_bo_from_fd(pDrmmode->fd, fd, stride, stride * height);
    if (NULL == bo)
    {
        pScreen->DestroyPixmap(pPixmap);
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "DRI3: get bo from fd(%d) failed: %dx%d, %d, %d, %d",
                   fd, width, height, depth, bpp, stride);

        return NullPixmap;
    }

    ret = ms_exa_set_pixmap_bo(pScrn, pPixmap, bo, TRUE);
    if (ret == FALSE)
    {
        pScreen->DestroyPixmap(pPixmap);
        dumb_bo_destroy(pDrmmode->fd, bo);

        return NullPixmap;
    }

    return pPixmap;
}

/*
static int etnaviv_dri3_fd_from_pixmap(ScreenPtr pScreen,
                                       PixmapPtr pixmap,
                                       CARD16 *stride,
                                       CARD32 *size)
{
    struct etnaviv *etnaviv = etnaviv_get_screen_priv(pScreen);
    struct etnaviv_pixmap *vPix = etnaviv_get_pixmap_priv(pixmap);

    // Only support pixmaps backed by an etnadrm bo
    if (!vPix || !vPix->etna_bo)
        return BadMatch;

    *stride = pixmap->devKind;
    *size = etna_bo_size(vPix->etna_bo);

    return etna_bo_to_dmabuf(etnaviv->conn, vPix->etna_bo);
}
*/

static int etnaviv_dri3_fd_from_pixmap(ScreenPtr pScreen,
                                       PixmapPtr pixmap,
                                       CARD16 *stride,
                                       CARD32 *size)
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
                   "dri3: failed to get bo from pixmap\n");
        return -1;
    }

    ret = drmPrimeHandleToFD(pDrmMode->fd, bo->handle, DRM_CLOEXEC, &prime_fd);
    if (ret)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "dri3: failed to get dmabuf fd: %d\n", ret);
        return ret;
    }

    *stride = bo->pitch;
    *size = bo->size;

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
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    int fd;

    TRACE_ENTER();

    pDrmMode->dri3_device_name = NULL;

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

        pDrmMode->dri3_device_name = drmGetDeviceNameFromFd2(fd);

        drmClose(fd);
    }

    if (pDrmMode->dri3_device_name == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "DRI3: failed to open renderer node\n");
        return FALSE;

    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "DRI3: renderer node name: %s\n",
                   pDrmMode->dri3_device_name);
    }

    TRACE_EXIT();

    return dri3_screen_init(pScreen, &etnaviv_dri3_info);
}
