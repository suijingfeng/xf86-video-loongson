/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright 2011 Dave Airlie
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 * Original Author: Alan Hourihane <alanh@tungstengraphics.com>
 * Rewrite: Dave Airlie <airlied@redhat.com>
 *          Sui Jingfeng <suijingfeng@loongson.cn>
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <fcntl.h>
#include <xf86.h>
#include <xf86Pci.h>

// miPointerScreenPtr
#include <mipointrst.h>
// miClearVisualTypes
#include <micmap.h>

#include <fb.h>

#include <X11/extensions/randr.h>


#ifdef XSERVER_PLATFORM_BUS
#include <xf86platformBus.h>
#endif
#ifdef XSERVER_LIBPCIACCESS
#include <pciaccess.h>
#endif
#include "driver.h"

#include "fake_exa.h"

#include "loongson_options.h"
#include "loongson_debug.h"
#include "loongson_helpers.h"
#include "loongson_cursor.h"
#include "loongson_shadow.h"
#include "loongson_entity.h"

#include "loongson_glamor.h"
#include "loongson_scanout.h"
#include "loongson_prime.h"
#include "loongson_randr.h"

#include "sprite.h"

static Bool PreInit(ScrnInfoPtr pScrn, int flags);
static Bool ScreenInit(ScreenPtr pScreen, int argc, char **argv);

static void FreeScreen(ScrnInfoPtr pScrn);
static Bool CloseScreen(ScreenPtr pScreen);

// The server takes control of the console.
static Bool EnterVT(ScrnInfoPtr pScrn);
// The server releases control of the console.
static void LeaveVT(ScrnInfoPtr pScrn);

static ModeStatus ValidMode(ScrnInfoPtr pScrn, DisplayModePtr mode,
                            Bool verbose, int flags);
static Bool SwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode);
static void AdjustFrame(ScrnInfoPtr pScrn, int x, int y);


//
// A driver and any module it uses may allocate per-screen
// private storage in either the ScreenRec (DIX level) or
// ScrnInfoRec (XFree86 common layer level).
//
// ScreenRec storage persists only for a single server generation,
// and ScrnInfoRec storage persists across generations for the
// life time of the server.
//
// The ScreenRec devPrivates data must be reallocated/initialised
// at the start of each new generation.
//
// This is normally done from the ScreenInit() function,
// and Init functions for other modules that it calls.
// Data allocated in this way should be freed by the driver’s
// CloseScreen() functions, and Close functions for other modules
// that it calls.
//
// A new devPrivates entry is allocated by calling the
// AllocateScreenPrivateIndex() function.


void LS_SetupScrnHooks(ScrnInfoPtr pScrn,
                       Bool (* pFnProbe)(DriverPtr, int))
{
    pScrn->driverVersion = 1;
    pScrn->driverName = "loongson";
    // Name to prefix messages
    pScrn->name = "loongson";

    pScrn->Probe = pFnProbe;
    pScrn->PreInit = PreInit;
    pScrn->ScreenInit = ScreenInit;
    pScrn->SwitchMode = SwitchMode;
    pScrn->AdjustFrame = AdjustFrame;
    pScrn->EnterVT = EnterVT;
    pScrn->LeaveVT = LeaveVT;
    pScrn->FreeScreen = FreeScreen;
    pScrn->ValidMode = ValidMode;
}


static Bool LS_AllocDriverPrivate(ScrnInfoPtr pScrn)
{
    //
    // Per-screen driver specific data that cannot be accommodated
    // with the static ScrnInfoRec fields is held in a driver-defined
    // data structure, a pointer to which is assigned to the
    // ScrnInfoRec’s driverPrivate field.
    //
    // Driver specific information should be stored in a structure
    // hooked into the ScrnInfoRec’s driverPrivate field.
    //
    // Any other modules which require persistent data (ie data that
    // persists across server generations) should be initialised in
    // this function, and they should allocate a "privates" index
    // to hook their data into. The "privates" data is persistent.
    //
    if (NULL == pScrn->driverPrivate)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: Allocate for driver private.\n", __func__);
        //void *calloc(size_t nmemb, size_t size);
        pScrn->driverPrivate = xnfcalloc(1, sizeof(loongsonRec));
        if (NULL == pScrn->driverPrivate)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s: Failed allocate for driver private.\n", __func__);
            return FALSE;
        }
    }

    return TRUE;
}


static void redisplay_dirty(ScreenPtr pScreen,
                            PixmapDirtyUpdatePtr dirty,
                            int *timeout)
{
    RegionRec pixregion;
    // Shared / Scanout pixmap
    PixmapPtr pSlaveDst = dirty->slave_dst;
    PixmapRegionInit(&pixregion, pSlaveDst);
    DamageRegionAppend(&pSlaveDst->drawable, &pixregion);
    PixmapSyncDirtyHelper(dirty);

    if (!pScreen->isGPU)
    {
#ifdef GLAMOR_HAS_GBM
        loongsonPtr lsp = loongsonPTR(xf86ScreenToScrn(pScreen));
        struct drmmode_rec * const pDrmMode = &lsp->drmmode;

        if (pDrmMode->glamor_enabled)
        {
            struct GlamorAPI * const pGlamor = &lsp->glamor;

            /*
             * When copying from the master framebuffer to the shared pixmap,
             * we must ensure the copy is complete before the slave starts a
             * copy to its own framebuffer (some slaves scanout directly from
             * the shared pixmap, but not all).
             */
            pGlamor->finish(pScreen);
        }
#endif

        INFO_MSG("%s: is not GPU\n", __func__);

        /* Ensure the slave processes the damage immediately */
        if (timeout)
            *timeout = 0;
    }

    DamageRegionProcessPending(&pSlaveDst->drawable);
    RegionUninit(&pixregion);
}


static void ls_dirty_update(ScreenPtr pScreen, int *timeout)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;

    PixmapDirtyUpdatePtr ent;

    /* dirty pixmaps is tracked in the server */
    if (xorg_list_is_empty(&pScreen->pixmap_dirty_list))
    {
        return;
    }

    xorg_list_for_each_entry(ent, &pScreen->pixmap_dirty_list, ent)
    {
        RegionPtr region = DamageRegion(ent->damage);
        /* Shared / scanout pixmap */
        PixmapPtr pSlaveDst = ent->slave_dst;

        if (RegionNotEmpty(region))
        {
            if (!pScreen->isGPU)
            {
                msPixmapPrivPtr ppriv =
                    msGetPixmapPriv(pDrmMode, pSlaveDst->master_pixmap);

                if (ppriv->notify_on_damage)
                {
                    ppriv->notify_on_damage = FALSE;

                    pSlaveDst->drawable.pScreen->SharedPixmapNotifyDamage(
                                                     pSlaveDst);
                }

                /* Requested manual updating */
                if (ppriv->defer_dirty_update)
                    continue;
            }

            redisplay_dirty(pScreen, ent, timeout);
            DamageEmpty(ent->damage);
        }
    }
}


static void msBlockHandler(ScreenPtr pScreen, void *timeout)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);

    pScreen->BlockHandler = lsp->BlockHandler;
    pScreen->BlockHandler(pScreen, timeout);
    lsp->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = msBlockHandler;

    if (pScreen->isGPU)
    {
        xf86DrvMsg(X_INFO, pScrn->scrnIndex,
                   "%s IS GPU, dispatch dirty\n", __func__);
        LS_DispatchSlaveDirty(pScreen);
    }
    else if (lsp->dirty_enabled)
    {
        xf86DrvMsg(X_INFO, pScrn->scrnIndex,
                   "%s: dispatch dirty\n", __func__);
        LS_DispatchDirty(pScreen);
    }

    ls_dirty_update(pScreen, timeout);
}


/*
 * Both radeon and amdgpu don't set the mode until the first blockhandler,
 * this means everything should be rendered on the screen correctly by then.
 *
 * This ports this code, it also removes the tail call of EnterVT from
 * ScreenInit, it really isn't necessary and causes us to set a dirty mode
 * with -modesetting always anyways.
 *
 * reorder set desired modes vs block handler as done for amdgpu.
 */
static void LS_BlockHandler_Oneshot(ScreenPtr pScreen, void *pTimeout)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ls = loongsonPTR(pScrn);
    struct drmmode_rec *pDrmMode = &ls->drmmode;

    xf86Msg(X_INFO, "%s begin\n", __func__);

    msBlockHandler(pScreen, pTimeout);

    drmmode_set_desired_modes(pScrn, pDrmMode, TRUE);

    xf86Msg(X_INFO, "%s finished\n", __func__);
}


static void FreeRec(ScrnInfoPtr pScrn)
{
    loongsonPtr ms;
    struct drmmode_rec * pDrmMode;
    BusRec * pBusLoc;

    ms = loongsonPTR(pScrn);
    if (!ms)
        return;

    pDrmMode = &ms->drmmode;
    pBusLoc = &ms->pEnt->location;

    if (ms->fd > 0)
    {
        if (0 == LS_EntityDecreaseFdReference(pScrn))
        {
            int ret;
            if (pBusLoc->type == BUS_PCI)
            {
                ret = drmClose(ms->fd);
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "PCI: Close file descriptor %d %s.\n",
                   ms->fd, ret ? "failed" : "successful");
            }
            else
            {
#ifdef XF86_PDEV_SERVER_FD
                if ((pBusLoc->type == BUS_PLATFORM) &&
                    (pBusLoc->id.plat->flags & XF86_PDEV_SERVER_FD))
                {
                    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "Platform: Server managed fd, we don't care.\n");
                }
                else
#endif
                {
                    ret = close(ms->fd);

                    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "Platform: Close file descriptor %d %s.\n",
                        ms->fd, ret ? "failed" : "successful");
                }
            }
        }
    }

    pScrn->driverPrivate = NULL;

    LS_FreeOptions(pScrn, &pDrmMode->Options);
    free(ms);
}



static Bool LS_GetDrmMasterFd(ScrnInfoPtr pScrn)
{
    loongsonPtr ms = loongsonPTR(pScrn);
    EntityInfoPtr pEnt = ms->pEnt;
    int cached_fd = LS_EntityGetCachedFd(pScrn);

    if (cached_fd != 0)
    {
        ms->fd = cached_fd;
        LS_EntityIncreaseFdReference(pScrn);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "Reusing fd %d for second head.\n", cached_fd);

        return TRUE;
    }

#ifdef XSERVER_PLATFORM_BUS
    if (pEnt->location.type == BUS_PLATFORM)
    {
        struct xf86_platform_device *pPlatDev;
        struct OdevAttributes *pAttr;

        pPlatDev = pEnt->location.id.plat;
        pAttr = xf86_platform_device_odev_attributes(pPlatDev);
#ifdef XF86_PDEV_SERVER_FD
        // server manage fd is not working on our platform now.
        // We don't know what's the reason and how to enable that.
        if (pPlatDev->flags & XF86_PDEV_SERVER_FD)
        {
            ms->fd = pAttr->fd;

            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "Get the fd(=%d) from server managed fd.\n", ms->fd);
        }
        else
#endif
        {
            char *path = pAttr->path;

            if (NULL != path)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "path = %s, got from PLATFORM.\n", path);
            }

            ms->fd = LS_OpenHW(path);
        }
    }
    else
#endif
#ifdef XSERVER_LIBPCIACCESS
    if (pEnt->location.type == BUS_PCI)
    {
        char *BusID = NULL;
        struct pci_device *PciInfo;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "BUS: PCI\n");

        PciInfo = xf86GetPciInfoForEntity(pEnt->index);
        if (PciInfo)
        {
            if ((BusID = LS_DRICreatePCIBusID(PciInfo)) != NULL)
            {
                ms->fd = drmOpen(NULL, BusID);

                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        " BusID = %s, got from pci bus\n", BusID);

                free(BusID);
            }
        }
    }
    else
#endif
    {
        const char *devicename;
        devicename = xf86FindOptionValue(pEnt->device->options, "kmsdev");

        if (devicename)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "kmsdev=%s, got from conf\n", devicename);
        }

        ms->fd = LS_OpenHW(devicename);
    }

    if (ms->fd < 0)
        return FALSE;

    LS_EntityInitFd(pScrn, ms->fd);

    return TRUE;
}


/* This is called by PreInit to set up the default visual */
static Bool InitDefaultVisual(ScrnInfoPtr pScrn)
{
    loongsonPtr ms = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;
    int defaultdepth, defaultbpp;
    int bppflags;

    drmmode_get_default_bpp(pScrn, pDrmMode, &defaultdepth, &defaultbpp);

    //
    // By default, a 24bpp screen will use 32bpp images, this avoids
    // problems with many applications which just can't handle packed pixels.
    // If you want real 24bit images, include a 24bpp format in the pixmap
    // formats
    //

    if ((defaultdepth == 24) && (defaultbpp == 24))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "24bpp hw front buffer is not supported\n");
    }
    else
    {
        pDrmMode->kbpp = defaultbpp;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "kbpp = %d\n", defaultbpp);
    }

    bppflags = PreferConvert24to32 | SupportConvert24to32 | Support32bppFb;

    if (!xf86SetDepthBpp(pScrn, defaultdepth, defaultdepth, defaultbpp, bppflags))
    {
        return FALSE;
    }

    switch (pScrn->depth)
    {
        case 15:
        case 16:
        case 24:
            break;
        case 30:
        default:
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Given depth (%d) is not supported by the driver\n",
                   pScrn->depth);
            return FALSE;
    }

    xf86PrintDepthBpp(pScrn);
    if (pDrmMode->kbpp == 0)
    {
        pDrmMode->kbpp = pScrn->bitsPerPixel;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "drmmode.kbpp = %d\n", pDrmMode->kbpp);
    }

    {
        rgb defaultWeight = { 0, 0, 0 };
        if (!xf86SetWeight(pScrn, defaultWeight, defaultWeight))
            return FALSE;
    }

    if (!xf86SetDefaultVisual(pScrn, -1))
    {
        return FALSE;
    }

    return TRUE;
}

static void LS_ProbeKMS(ScrnInfoPtr pScrn,
                        struct drmmode_rec * const pDrmMode)
{
    drmVersionPtr version;

    version = drmGetVersion(pDrmMode->fd);

    if (version)
    {
        xf86Msg(X_INFO,"\n");

        xf86Msg(X_INFO, " Version: %d.%d.%d\n",
                    version->version_major,
                    version->version_minor,
                    version->version_patchlevel);
        xf86Msg(X_INFO,"  Name: %s\n", version->name);
        xf86Msg(X_INFO,"  Date: %s\n", version->date);
        xf86Msg(X_INFO,"  Description: %s\n", version->desc);

        if (!strncmp("lsdc", version->name, version->name_len))
        {
            pDrmMode->is_lsdc = TRUE;
        }
        else if (!strncmp("20190831", version->date, version->date_len))
        {
            pDrmMode->is_lsdc = TRUE;
        }
        else
        {
            pDrmMode->is_lsdc = FALSE;
        }

        drmFreeVersion(version);

        xf86Msg(X_INFO,"  Is lsdc: %s\n",
                      pDrmMode->is_lsdc ? "Yes" : "no");
        xf86Msg(X_INFO,"\n");
    }
}

static void LS_ProbeGPU(ScrnInfoPtr pScrn,
                        struct drmmode_rec * const pDrmMode)
{
    const char * const vivante_gpu_dev = "/dev/galcore";

    if (access(vivante_gpu_dev, F_OK) == 0)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "%s: %s is exist\n",
                        __func__, vivante_gpu_dev);
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "%s: %s is NOT exist\n",
                        __func__, vivante_gpu_dev);
    }
}

static Bool PreInit(ScrnInfoPtr pScrn, int flags)
{
    loongsonPtr ms;
    struct drmmode_rec * pDrmMode;
    uint64_t value = 0;
    int ret;
    int connector_count;
    Bool is_prime_supported = FALSE;

    xf86Msg(X_INFO, "\n");
    xf86Msg(X_INFO, "-------- %s started --------\n", __func__);

    if (pScrn->numEntities != 1)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: pScrn->numEntities = %d.\n",
                   __func__, pScrn->numEntities);
        return FALSE;
    }

    if (flags & PROBE_DETECT)
    {
        // support the \"-configure\" or \"-probe\" command line arguments.
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: PROBE DETECT only.\n", __func__);
        return FALSE;
    }

    if (FALSE == LS_AllocDriverPrivate(pScrn))
    {
        return FALSE;
    }

    ms = loongsonPTR(pScrn);

    // This function hands information from the EntityRec struct to
    // the drivers. The EntityRec structure itself remains invisible
    // to the driver.

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "Entity ID = %d\n", pScrn->entityList[0]);

    ms->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);
    pDrmMode = &ms->drmmode;

    pDrmMode->is_secondary = FALSE;
    pScrn->displayWidth = 640;  /* default it */

    {
        int entityIndex = pScrn->entityList[0];
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: Entity index is %d\n", __func__, entityIndex);

        if (xf86IsEntityShared(entityIndex))
        {
            if (xf86IsPrimInitDone(entityIndex))
            {
                pDrmMode->is_secondary = TRUE;
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: Primary init is done.\n", __func__);
            }
            else
            {
                xf86SetPrimInitDone(entityIndex);

                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: Primary init is NOT done, set it.\n", __func__);
            }
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "Entity %d is not shared\n",
                       entityIndex);
        }
    }

    pScrn->monitor = pScrn->confScreen->monitor;
    pScrn->progClock = TRUE;
    pScrn->rgbBits = 8;

    if (!LS_GetDrmMasterFd(pScrn))
    {
        return FALSE;
    }

    pDrmMode->fd = ms->fd;

    if (!LS_CheckOutputs(ms->fd, &connector_count))
    {
        return FALSE;
    }

    /* get kernel driver name */
    LS_ProbeKMS(pScrn, pDrmMode);

    /*  Vivante or Etnaviv ? */
    LS_ProbeGPU(pScrn, pDrmMode);

    InitDefaultVisual(pScrn);

    /* Process the options */
    LS_ProcessOptions(pScrn, &pDrmMode->Options);

    LS_GetCursorDimK(pScrn);

    LS_PrepareDebug(pScrn);

    is_prime_supported = LS_CheckPrime(ms->fd);

    // first try glamor, then try EXA
    // if both failed, using the shadowfb
    if (try_enable_glamor(pScrn) == FALSE)
    {
        // if prime is not supported by the kms, fallback to shadow.
        if (is_prime_supported)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                "DRM PRIME is supported, trying fake EXA + DRI3.\n");

            try_enable_exa(pScrn);
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                "DRM PRIME is NOT supported, will fallback to shadow.\n");
                pDrmMode->exa_enabled = FALSE;
        }
    }


    if ((FALSE == pDrmMode->glamor_enabled) &&
        (FALSE == pDrmMode->exa_enabled))
    {
        LS_TryEnableShadow(pScrn);
    }


    // Modules may be loaded at any point in this function,
    // and all modules that the driver will need must be loaded
    // before the end of this function.
    //
    // Load the required sub modules
    if (!xf86LoadSubModule(pScrn, "fb"))
    {
        return FALSE;
    }

    pDrmMode->pageflip =
        xf86ReturnOptValBool(pDrmMode->Options, OPTION_PAGEFLIP, TRUE);

    xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
        "PageFlip %s enabled.\n", pDrmMode->pageflip ? "is" : "is NOT");

    pScrn->capabilities = 0;
    if (is_prime_supported)
    {
        if (connector_count && (value & DRM_PRIME_CAP_IMPORT))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                 "DRM PRIME: support import(sink).\n");
            pScrn->capabilities |= RR_Capability_SinkOutput;

            if (pDrmMode->glamor_enabled)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                 "DRM PRIME: support offload(sink).\n");
                pScrn->capabilities |= RR_Capability_SinkOffload;
            }

            if (pDrmMode->exa_enabled)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                 "DRM PRIME: support offload(sink).\n");

                pScrn->capabilities |= RR_Capability_SinkOffload;
            }
        }
#ifdef GLAMOR_HAS_GBM_LINEAR
        if (value & DRM_PRIME_CAP_EXPORT && pDrmMode->glamor_enabled)
        {
            pScrn->capabilities |= RR_Capability_SourceOutput |
                                   RR_Capability_SourceOffload;
        }
#endif
    }

    if (xf86ReturnOptValBool(pDrmMode->Options, OPTION_ATOMIC, FALSE))
    {
        ret = drmSetClientCap(ms->fd, DRM_CLIENT_CAP_ATOMIC, 1);
        ms->atomic_modeset = (ret == 0);
    }
    else
    {
        ms->atomic_modeset = FALSE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
        "Atomic modeset enabled ? %s.\n",
            ms->atomic_modeset ? "YES" : "NO" );

    ms->kms_has_modifiers = FALSE;
    ret = drmGetCap(ms->fd, DRM_CAP_ADDFB2_MODIFIERS, &value);
    if (ret == 0 && value != 0)
    {
        ms->kms_has_modifiers = TRUE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, ms->kms_has_modifiers ?
        "KMS has modifier support.\n" : "KMS doesn't have modifier support\n");

    if (drmmode_pre_init(pScrn, pDrmMode, pScrn->bitsPerPixel / 8) == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "KMS setup failed\n");
        goto fail;
    }

    /*
     * If the driver can do gamma correction, it should call xf86SetGamma() here.
     */
    {
        Gamma zeros = { 0.0, 0.0, 0.0 };

        if (!xf86SetGamma(pScrn, zeros))
        {
            return FALSE;
        }
    }

    if (pScrn->modes == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No modes.\n");
        return FALSE;
    }

    pScrn->currentMode = pScrn->modes;

    /* Set display resolution */
    xf86SetDpi(pScrn, 0, 0);


    if (pDrmMode->shadow_enable)
    {
        LS_ShadowLoadAPI(pScrn);
    }


    xf86Msg(X_INFO, "-------- %s finished --------\n", __func__);
    xf86Msg(X_INFO, "\n");

    //
    // It is expected that if the PreInit() function returns TRUE,
    // then the only reasons that subsequent stages in the driver might
    // fail are lack or resources (like xalloc failures).
    //
    // All other possible reasons for failure should be determined
    // by the ChipPreInit() function.
    //
    return TRUE;

 fail:
    //
    // PreInit() returns FALSE when the configuration is unusable
    // in some way (unsupported depth, no valid modes, not enough
    // video memory, etc), and TRUE if it is usable.
    //
    return FALSE;
}

/*
 *  PresentSharedPixmap is a function exposed by the source driver
 *  for the X server or sink driver to call to request a present
 *  on a given shared pixmap. This way, presents can be driven by
 *  the sink's vblank instead of a timer or similar mechanism.
 */
static Bool msPresentSharedPixmap(PixmapPtr slave_dst)
{
    // pointer to master copy of pixmap for pixmap sharing
    PixmapPtr pMasterPix = slave_dst->master_pixmap;
    ScreenPtr pScreen = pMasterPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;

    msPixmapPrivPtr ppriv = msGetPixmapPriv(pDrmMode, pMasterPix);
    RegionPtr region = DamageRegion(ppriv->dirty->damage);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "\n");
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "-------- %s started --------\n", __func__);

    if (RegionNotEmpty(region))
    {
        redisplay_dirty(ppriv->slave_src->pScreen, ppriv->dirty, NULL);
        DamageEmpty(ppriv->dirty->damage);

        return TRUE;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "-------- %s finished --------\n", __func__);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "\n");

    return FALSE;
}

static Bool msStopFlippingPixmapTracking(DrawablePtr src,
                             PixmapPtr slave_dst1, PixmapPtr slave_dst2)
{
    ScreenPtr pScreen = src->pScreen;
    loongsonPtr lsp = loongsonPTR(xf86ScreenToScrn(pScreen));
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    msPixmapPrivPtr ppriv1 = msGetPixmapPriv(pDrmMode, slave_dst1->master_pixmap);
    msPixmapPrivPtr ppriv2 = msGetPixmapPriv(pDrmMode, slave_dst2->master_pixmap);

    Bool ret = TRUE;

    ret &= PixmapStopDirtyTracking(src, slave_dst1);
    ret &= PixmapStopDirtyTracking(src, slave_dst2);

    if (ret)
    {
        ppriv1->slave_src = NULL;
        ppriv2->slave_src = NULL;

        ppriv1->dirty = NULL;
        ppriv2->dirty = NULL;

        ppriv1->defer_dirty_update = FALSE;
        ppriv2->defer_dirty_update = FALSE;
    }

    return ret;
}


static Bool LS_CreateScreenResources(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr lsp = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &lsp->drmmode;
    void *pixels = NULL;

    PixmapPtr rootPixmap;
    Bool ret;
    int err;

    xf86Msg(X_INFO, "\n");

    xf86Msg(X_INFO, "-------- %s stated --------\n", __func__);

    pScreen->CreateScreenResources = lsp->createScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = LS_CreateScreenResources;

    if (!drmmode_set_desired_modes(pScrn, pDrmMode, pScrn->is_gpu))
    {
        return FALSE;
    }

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        if (!ls_glamor_handle_new_screen_pixmap(pScrn, &pDrmMode->front_bo))
        {
            return FALSE;
        }
    }
#endif

    drmmode_uevent_init(pScrn, pDrmMode);

    if (pDrmMode->sw_cursor == FALSE)
    {
        LS_MapCursorBO(pScrn, pDrmMode);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "Hardware cursor enabled, mapping it.\n");
    }

    if (pDrmMode->gbm == NULL)
    {
        pixels = LS_MapFrontBO(pScrn, pDrmMode);
        if (pixels == NULL)
        {
            return FALSE;
        }
    }

    rootPixmap = pScreen->GetScreenPixmap(pScreen);

    if (pDrmMode->shadow_enable)
    {
        pixels = pDrmMode->shadow_fb;
    }

    if (pDrmMode->shadow_enable2)
    {
        LS_ShadowAllocFB(pScrn, &pDrmMode->shadow_fb2);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "Virtual Address of the front bo: %p\n", pixels);

    // Recall the comment of of miCreateScreenResources()
    // create a pixmap with no data, then redirect it to point to the screen".
    //
    // The routine that created the empty pixmap was (*pScreen->CreatePixmap)
    // actually fbCreatePixmap() and the routine that (*pScreen->ModifyPixmapHeader),
    // which is actually miModifyPixmapHeader() sets the address of the pixmap
    // to the screen memory address.
    //
    //
    // The address is passed as the last argument of (*pScreen->ModifyPixmapHeader)
    // and as seen in miCreateScreenResources() this is pScrInitParms->pbits.
    // This was set to pbits by miScreenDevPrivateInit() and pbits replaces
    // the FBStart fbScreenInit(), which is the screen memory address.
    //
    // "Mga->FbStart is equal to pMga->FbBase since YDstOrg (the offset
    // in bytes from video start to usable memory) is usually zero".
    //
    // Additionally, if an aperture used to access video memory is unmapped
    // and remapped in this fashion, EnterVT() will also need to notify the
    // framebuffer layers of the aperture's new location in virtual memory.
    // This is done with a call to the screen's ModifyPixmapHeader() function
    //
    // Where the rootPixmap field in a ScrnInfoRec points to the pixmap used
    // by the screen's SaveRestoreImage() function to hold the screen's
    // contents while switched out.
    //
    // pixels is assumed to be the pixmap data; it will be stored in an
    // implementation-dependent place (usually pPixmap->devPrivate.ptr).
    if (!pScreen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, -1, pixels))
    {
        FatalError("Couldn't adjust screen pixmap\n");
    }

    if (pDrmMode->shadow_enable)
    {
        struct ShadowAPI *pShadowAPI = &lsp->shadow;

        pShadowAPI->Add(pScreen,
                        rootPixmap,
                        LS_ShadowUpdatePacked,
                        LS_ShadowWindow, 0, NULL);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ShadowAPI->add() finished\n");
    }

    err = drmModeDirtyFB(lsp->fd, pDrmMode->fb_id, NULL, 0);

    if ((err != -EINVAL) && (err != -ENOSYS))
    {
        lsp->damage = DamageCreate(NULL, NULL, DamageReportNone, TRUE,
                                  pScreen, rootPixmap);
        if (lsp->damage)
        {
            DamageRegister(&rootPixmap->drawable, lsp->damage);
            lsp->dirty_enabled = TRUE;
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Damage tracking initialized\n");
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to create screen damage record\n");
            return FALSE;
        }
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                       "[drm] dirty fb failed: %d\n", err);
    }

    LS_InitRandR(pScreen);

    xf86Msg(X_INFO, "-------- %s finished --------\n", __func__);
    xf86Msg(X_INFO, "\n");

    return ret;
}


static Bool msRequestSharedPixmapNotifyDamage(PixmapPtr pPix)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;

    msPixmapPrivPtr ppriv = msGetPixmapPriv(pDrmMode, pPix->master_pixmap);

    ppriv->notify_on_damage = TRUE;

    return TRUE;
}


static Bool msSharedPixmapNotifyDamage(PixmapPtr ppix)
{
    Bool ret = FALSE;
    int c;

    ScreenPtr screen = ppix->drawable.pScreen;
    ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
    loongsonPtr ms = loongsonPTR(scrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);

    msPixmapPrivPtr ppriv = msGetPixmapPriv(&ms->drmmode, ppix);

    if (!ppriv->wait_for_damage)
        return ret;
    ppriv->wait_for_damage = FALSE;

    for (c = 0; c < xf86_config->num_crtc; c++)
    {
        xf86CrtcPtr crtc = xf86_config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        if (!drmmode_crtc)
            continue;
        if (!(drmmode_crtc->prime_pixmap && drmmode_crtc->prime_pixmap_back))
            continue;

        // Received damage on master screen pixmap, schedule present on vblank
        ret |= drmmode_SharedPixmapPresentOnVBlank(ppix, crtc, &ms->drmmode);
    }

    return ret;
}


static Bool SetMaster(ScrnInfoPtr pScrn)
{
    loongsonPtr ms = loongsonPTR(pScrn);
    int ret;

#ifdef XF86_PDEV_SERVER_FD
    if ( (ms->pEnt->location.type == BUS_PLATFORM) &&
        (ms->pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD))
        return TRUE;
#endif

    ret = drmSetMaster(ms->fd);
    if (ret)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "drmSetMaster failed: %s\n",
                   strerror(errno));

    return ret == 0;
}

/* When the root window is created, initialize the screen contents from
 * console if -background none was specified on the command line
 */
static Bool CreateWindow_oneshot(WindowPtr pWin)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = loongsonPTR(pScrn);
    Bool ret;

    pScreen->CreateWindow = ms->CreateWindow;
    ret = pScreen->CreateWindow(pWin);

    if (ret)
        drmmode_copy_fb(pScrn, &ms->drmmode);
    return ret;
}


//
// When ScreenInit() phase is done the common level will determine
// which shared resources are requested by more than one driver and
// set the access functions accordingly.
//
// This is done following these rules:
//
// The sharable resources registered by each entity are compared.
// If a resource is registered by more than one entity the entity
// will be marked to need to share this resources type (IO or MEM).
//
// A resource marked “disabled” during OPERATING state will be
// ignored entirely.
//
// A resource marked “unused” will only conflicts with an overlapping
// resource of an other entity if the second is actually in use during
// OPERATING state.
//
// If an “unused” resource was found to conflict however the entity
// does not use any other resource of this type the entire resource
// type will be disabled for that entity.
//
//
// The driver has the choice among different ways to control access
// to certain resources:
//
// 1. It can rely on the generic access functions. This is probably the
// most common case. Here the driver only needs to register any resource
// it is going to use.
//
// 2. It can replace the generic access functions by driver specific ones.
// This will mostly be used in cases where no generic access functions are
// available. In this case the driver has to make sure these resources are
// disabled when entering the PreInit() stage. Since the replacement
// functions are registered in PreInit() the driver will have to enable
// these resources itself if it needs to access them during this state.
// The driver can specify if the replacement functions can control memory
// and/or I/O resources separately.

// The driver can enable resources itself when it needs them.
// Each driver function enabling them needs to disable them
// before it will return. This should be used if a resource
// which can be controlled in a device dependent way is only
// required during SETUP state.
// This way it can be marked “unused” during OPERATING state.


static Bool ScreenInit(ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = loongsonPTR(pScrn);
    struct drmmode_rec * pDrmMode = &ms->drmmode;
    int ret;
#ifdef GLAMOR_HAS_GBM
    struct GlamorAPI * const pGlamor = &ms->glamor;
#endif

    xf86Msg(X_INFO, "\n");
    xf86Msg(X_INFO, "-------- %s started --------\n", __func__);

    pScrn->pScreen = pScreen;

    if (SetMaster(pScrn) == FALSE)
    {
        return FALSE;
    }

    /* HW dependent - FIXME */
    /* loongson's display controller require the stride is 256 byte aligned */
    pScrn->virtualX = (pScrn->virtualX + 63) & ~63;

    pScrn->displayWidth = pScrn->virtualX;


#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        int bpp = pDrmMode->kbpp;
        int width = pScrn->virtualX;
        int height = pScrn->virtualY;
        Bool bo_create_res;

        pDrmMode->gbm = pGlamor->egl_get_gbm_device(pScreen);

        bo_create_res = ls_glamor_create_gbm_bo(pScrn,
                                                &pDrmMode->front_bo,
                                                width,
                                                height,
                                                bpp);
        if (bo_create_res == FALSE)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "GBM: Create front bo failed.\n");

            return FALSE;
        }
    }
    else
#endif
    {
        ret = LS_CreateFrontBO(pScrn, pDrmMode);
        if (ret == FALSE)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "DUMB: Create front bo failed.\n");

            return FALSE;
        }
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
            "virtualX=%d, virtuaY=%d\n",
            pScrn->virtualX, pScrn->virtualY);

    if (LS_CreateCursorBO(pScrn, pDrmMode) == FALSE)
    {
        return FALSE;
    }

    if (pDrmMode->shadow_enable)
    {
        LS_ShadowAllocFB(pScrn, &pDrmMode->shadow_fb);
    }

    /* Reset the visual list. */
    miClearVisualTypes();

    if (!miSetVisualTypes(pScrn->depth,
                          miGetDefaultVisualMask(pScrn->depth),
                          pScrn->rgbBits, pScrn->defaultVisual))
    {
        return FALSE;
    }

    if (!miSetPixmapDepths())
    {
        return FALSE;
    }

    /* OUTPUT SLAVE SUPPORT */
    if (!dixRegisterScreenSpecificPrivateKey(pScreen,
            &pDrmMode->pixmapPrivateKeyRec,
            PRIVATE_PIXMAP,
            sizeof(msPixmapPrivRec)))
    {
        return FALSE;
    }

    pScrn->memPhysBase = 0;
    pScrn->fbOffset = 0;

    //
    // The DDX layer's ScreenInit() function usually calls another layer's
    // ScreenInit() function (e.g., miScreenInit() or fbScreenInit()) to
    // initialize the fallbacks that the DDX driver does not specifically
    // handle.
    //
    // fbScreenInit() is used to tell the fb layer where the video card
    // framebuffer is.
    //
    if (!fbScreenInit(pScreen, NULL,
                      pScrn->virtualX,
                      pScrn->virtualY,
                      pScrn->xDpi,
                      pScrn->yDpi,
                      pScrn->displayWidth,
                      pScrn->bitsPerPixel))
    {
        return FALSE;
    }

    if (pScrn->bitsPerPixel > 8)
    {
        VisualPtr visual;

        /* Fixup RGB ordering */
        visual = pScreen->visuals + pScreen->numVisuals;
        while (--visual >= pScreen->visuals)
        {
            if ((visual->class | DynamicClass) == DirectColor)
            {
                visual->offsetRed = pScrn->offset.red;
                visual->offsetGreen = pScrn->offset.green;
                visual->offsetBlue = pScrn->offset.blue;
                visual->redMask = pScrn->mask.red;
                visual->greenMask = pScrn->mask.green;
                visual->blueMask = pScrn->mask.blue;
            }
        }
    }

    fbPictureInit(pScreen, NULL, 0);

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        if (ls_glamor_init(pScrn) == FALSE)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to initialize glamor at ScreenInit() time.\n");
            return FALSE;
        }
    }
#endif

    if (pDrmMode->shadow_enable)
    {
        if (ms->shadow.Setup(pScreen) == FALSE)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Shadow fb init failed.\n");

            return FALSE;
        }
    }

    /*
     * With the introduction of pixmap privates, the "screen pixmap" can no
     * longer be created in miScreenInit, since all the modules that could
     * possibly ask for pixmap private space have not been initialized at
     * that time.  pScreen->CreateScreenResources is called after all
     * possible private-requesting modules have been inited; we create the
     * screen pixmap here.
     */
    ms->createScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = LS_CreateScreenResources;


    /* Set the initial black & white colormap indices: */
    xf86SetBlackWhitePixels(pScreen);
    /* Initialize backing store: */
    xf86SetBackingStore(pScreen);
    /* Enable cursor position updates by mouse signal handler: */
    xf86SetSilkenMouse(pScreen);

    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    /* If pageflip is enabled hook the screen's cursor-sprite (swcursor) funcs.
     * So that we can disabe page-flipping on fallback to a swcursor.
     */
    if (pDrmMode->pageflip)
    {
        loongson_hookup_sprite(pScreen);
    }

    /* Need to extend HWcursor support to handle mask interleave */
    if (pDrmMode->sw_cursor == FALSE)
    {
        xf86_cursors_init(pScreen, ms->cursor_width, ms->cursor_height,
                          HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_64 |
                          HARDWARE_CURSOR_UPDATE_UNHIDDEN |
                          HARDWARE_CURSOR_ARGB);
    }
    /* Must force it before EnterVT, so we are in control of VT and
     * later memory should be bound when allocating, e.g rotate_mem */
    pScrn->vtSema = TRUE;

    if (pDrmMode->exa_enabled == TRUE)
    {
        if (!LS_InitExaLayer(pScreen))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "%s: initial EXA Layer failed\n", __func__);
        }
    }


    if ((serverGeneration == 1) && bgNoneRoot && pDrmMode->glamor_enabled)
    {
        ms->CreateWindow = pScreen->CreateWindow;
        pScreen->CreateWindow = CreateWindow_oneshot;
    }

    //
    // After calling another layer's ScreenInit() function, any screen-specific
    // functions either wrap or replace the other layer's function pointers.
    // If a function is to be wrapped, each of the old function pointers from
    // the other layer are stored in a screen private area. Common functions
    // to wrap are CloseScreen() and SaveScreen().
    //
    pScreen->SaveScreen = xf86SaveScreen;
    ms->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = CloseScreen;

    ms->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = LS_BlockHandler_Oneshot;

    // pixmap sharing infrastructure
    //
    // This is a hooks for pixmap sharing and tracking.
    //
    // The pixmap sharing ones get an integer handle for the pixmap
    // and use a handle to be the backing for a pixmap.
    //
    // The tracker interface is to be used when a GPU needs to
    // track pixmaps to be updated for another GPU.
    //
    // pass slave to sharing so it can use it to work out driver.

    pScreen->SharePixmapBacking = LS_SharePixmapBacking;
    /* OUTPUT SLAVE SUPPORT */
    pScreen->SetSharedPixmapBacking = LS_SetSharedPixmapBacking;
    pScreen->StartPixmapTracking = PixmapStartDirtyTracking;
    pScreen->StopPixmapTracking = PixmapStopDirtyTracking;

    pScreen->SharedPixmapNotifyDamage = msSharedPixmapNotifyDamage;
    pScreen->RequestSharedPixmapNotifyDamage =
        msRequestSharedPixmapNotifyDamage;

    pScreen->PresentSharedPixmap = msPresentSharedPixmap;
    pScreen->StopFlippingPixmapTracking = msStopFlippingPixmapTracking;

    if (!xf86CrtcScreenInit(pScreen))
    {
        return FALSE;
    }

    if (!drmmode_setup_colormap(pScreen, pScrn))
    {
        return FALSE;
    }

    /*
        CRTCs and outputs needs to be enabled/disabled when the current
        DPMS mode is changed. We also try to do it in an atomic commit
        when possible.
    */
    if (ms->atomic_modeset)
    {
        xf86DPMSInit(pScreen, drmmode_set_dpms, 0);
    }
    else
    {
        xf86DPMSInit(pScreen, xf86DPMSSet, 0);
    }

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        XF86VideoAdaptorPtr glamor_adaptor;

        glamor_adaptor = pGlamor->xv_init(pScreen, 16);
        if (glamor_adaptor != NULL)
            xf86XVScreenInit(pScreen, &glamor_adaptor, 1);
        else
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to initialize XV support.\n");
    }
#endif

    if (serverGeneration == 1)
    {
        xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);
    }

    if (!ms_vblank_screen_init(pScreen))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to initialize vblank support.\n");
        return FALSE;
    }

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        if (!(pDrmMode->dri2_enable = ms_dri2_screen_init(pScreen)))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to initialize the DRI2 extension.\n");
        }

        if (!(pDrmMode->present_enable = ms_present_screen_init(pScreen)))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to initialize the Present extension.\n");
        }
    }
    else
#endif
    {
        if (pDrmMode->exa_enabled)
        {
            pDrmMode->dri2_enable = FALSE;
            #if 0
            // TODO : add exa + dri2 support
            ms->drmmode.dri2_enable = ms_dri2_screen_init(pScreen);
            if ( ms->drmmode.dri2_enable == FALSE)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                        "Failed to initialize the DRI2 extension.\n");
            }
            #endif

            pDrmMode->present_enable = ms_present_screen_init(pScreen);
            if (pDrmMode->present_enable == FALSE)
            {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Failed to initialize the Present extension.\n");
            }
            else
            {
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "Present extension enabled.\n");
            }
        }
    }

#ifdef DRI3
    if (pDrmMode->exa_enabled)
    {
        if (!LS_DRI3_Init(pScreen))
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to initialize the DRI3 extension.\n");
        }
    }
#endif

    pScrn->vtSema = TRUE;

    xf86Msg(X_INFO, "-------- %s finished --------\n", __func__);
    xf86Msg(X_INFO, "\n");

    return TRUE;
}


static void AdjustFrame(ScrnInfoPtr pScrn, int x, int y)
{
    loongsonPtr ms = loongsonPTR(pScrn);

    drmmode_adjust_frame(pScrn, &ms->drmmode, x, y);
}


static void FreeScreen(ScrnInfoPtr pScrn)
{
    if (pScrn)
    {
        FreeRec(pScrn);
    }
}


static void LeaveVT(ScrnInfoPtr pScrn)
{
    loongsonPtr lsp = loongsonPTR(pScrn);

    xf86_hide_cursors(pScrn);

    pScrn->vtSema = FALSE;

#ifdef XF86_PDEV_SERVER_FD
    if (lsp->pEnt->location.type == BUS_PLATFORM &&
        (lsp->pEnt->location.id.plat->flags & XF86_PDEV_SERVER_FD))
    {
        return;
    }
#endif

    drmDropMaster(lsp->fd);
}

/*
 * This gets called when gaining control of the VT, and from ScreenInit().
 */
static Bool EnterVT(ScrnInfoPtr pScrn)
{
    loongsonPtr lsp = loongsonPTR(pScrn);

    pScrn->vtSema = TRUE;

    SetMaster(pScrn);

    if (!drmmode_set_desired_modes(pScrn, &lsp->drmmode, TRUE))
    {
        return FALSE;
    }

    return TRUE;
}


static Bool SwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    return xf86SetSingleMode(pScrn, mode, RR_Rotate_0);
}


static Bool CloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    loongsonPtr ms = loongsonPTR(pScrn);
    struct drmmode_rec * const pDrmMode = &ms->drmmode;

    LS_EntityClearAssignedCrtc(pScrn);

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->dri2_enable)
    {
        ms_dri2_close_screen(pScreen);
    }
#endif

    ms_vblank_close_screen(pScreen);

    if (ms->damage)
    {
        DamageUnregister(ms->damage);
        DamageDestroy(ms->damage);
        ms->damage = NULL;
    }

    if (pDrmMode->exa_enabled)
    {
        LS_DestroyExaLayer(pScreen);
    }


    if (pDrmMode->shadow_enable)
    {
        ms->shadow.Remove(pScreen, pScreen->GetScreenPixmap(pScreen));

        LS_ShadowFreeFB(pScrn, &pDrmMode->shadow_fb);

        LS_ShadowFreeFB(pScrn, &pDrmMode->shadow_fb2);
    }

    drmmode_uevent_fini(pScrn, pDrmMode);

    LS_FreeFrontBO(pScrn, pDrmMode);

    LS_FreeCursorBO(pScrn, pDrmMode);

    if (pDrmMode->pageflip)
    {
        loongson_unhookup_sprite(pScreen);
    }

    if (pScrn->vtSema)
    {
        LeaveVT(pScrn);
    }

    pScreen->CreateScreenResources = ms->createScreenResources;
    pScreen->BlockHandler = ms->BlockHandler;

    pScrn->vtSema = FALSE;
    pScreen->CloseScreen = ms->CloseScreen;
    return (*pScreen->CloseScreen) (pScreen);
}

static ModeStatus ValidMode(ScrnInfoPtr arg, DisplayModePtr mode,
                            Bool verbose, int flags)
{
    return MODE_OK;
}
