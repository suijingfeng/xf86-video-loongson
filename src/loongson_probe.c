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
#include <xf86.h>

#ifdef XSERVER_PLATFORM_BUS
#include <xf86platformBus.h>
#endif

#include "driver.h"
#include "loongson_probe.h"
#include "loongson_helpers.h"
#include "loongson_entity.h"

// The purpose of this is to identify all instances of hardware supported
// by the driver. The flags value is currently either 0, PROBE_DEFAULT or
// PROBE_DETECT.
//
//
// PROBE_DETECT is used if "-configure" or "-probe" command line arguments
// are given and indicates to the Probe() function that it should not
// configure the bus entities and that no xorg.conf information is
// available.
//
//
// The probe must be a minimal probe. It should just determine if there is
// a card present that the driver can drive. It should use the least
// intrusive probe methods possible.
// It must not do anything that is not essential, like probing for other
// details such as the amount of memory installed, etc.
//
//
// When the Probe() function of each driver is called the device sections
// are matched against the devices found in the system. The driver may
// probe devices at this stage that cannot be identified by using device
// independent methods. Access to all resources that can be controlled in
// a device independent way is disabled.
//
// The Probe() function should register all non-relocatable resources at
// this stage. If a resource conflict is found between exclusive resources
// the driver will fail immediately.
//
Bool LS_Probe(DriverPtr drv, int flags)
{
    int i, numDevSections;
    GDevPtr *devSections = NULL;
    Bool foundScreen = FALSE;
    ScrnInfoPtr scrn = NULL;

    /* For now, just bail out for PROBE_DETECT. */
    if (flags & PROBE_DETECT)
    {
        xf86Msg(X_INFO, "LS_Probe: PROBE_DETECT.\n");
        return FALSE;
    }
    // The probe must find the active device sections that match the driver by 
    // calling xf86MatchDevice().
    // If no matches are found, the function should return FALSE immediately.

    // This function takes the name of the driver and returns via driversectlist
    // a list of device sections that match the driver name. The function return
    // value is the number of matches found. If a fatal error is encountered the
    // return value is -1.
    // The caller should use xfree() to free *driversectlist when it is no longer
    // needed.
    numDevSections = xf86MatchDevice("loongson", &devSections);
    if (numDevSections <= 0)
    {
        xf86Msg(X_WARNING, "Cant not find matched device. \n");
        return FALSE;
    }
    else
    {
        xf86Msg(X_INFO, "LS_Probe: %d matched device (loongson) found. \n",
                numDevSections);
    }

    // Devices that cannot be identified by using device-independent methods
    // should be probed at this stage (keeping in mind that access to all
    // resources that can be disabled in a device-independent way are disabled
    // during this phase).
    //
    for (i = 0; i < numDevSections; ++i)
    {
        const char *dev;
        int entity_num;
        int res = 0;
        int fd;

        dev = xf86FindOptionValue(devSections[i]->options, "kmsdev");

        fd = LS_OpenHW(dev);
        if (fd != -1)
        {
            xf86Msg(X_INFO, "LS_Probe: LS_OpenHW(%s) successful.\n",
                    dev ? dev : NULL);
            res = LS_CheckOutputs(fd, NULL);

            close(fd);
        }

        if (res)
        {
            // suijingfeng: why I am need to claim this ?
            entity_num = xf86ClaimFbSlot(drv, 0, devSections[i], TRUE);
            scrn = xf86ConfigFbEntity(scrn, 0, entity_num, NULL, NULL, NULL, NULL);
            xf86Msg(X_INFO, "LS_Probe: ClaimFbSlot: entity_num=%d.\n", entity_num);
        }
        // The probe must register all non-relocatable resources at this stage.
        // If a resource conflict is found between exclusive resources the driver
        // will fail immediately.
        if (scrn)
        {
            foundScreen = TRUE;
            LS_SetupScrnHooks(scrn, LS_Probe);

            xf86DrvMsg(scrn->scrnIndex, X_INFO,
                       "LS_Probe: using %s\n", dev ? dev : "default device");

            LS_SetupEntity(scrn, entity_num);
        }
    }

    free(devSections);

    return foundScreen;
}

#ifdef XSERVER_LIBPCIACCESS

static Bool probe_pci_hw(const char *dev, struct pci_device *pdev)
{
    int ret = FALSE;
    int fd = LS_OpenHW(dev);
    char *id, *devid;
    drmSetVersion sv;

    if (fd == -1)
    {
        return FALSE;
    }

    sv.drm_di_major = 1;
    sv.drm_di_minor = 4;
    sv.drm_dd_major = -1;
    sv.drm_dd_minor = -1;
    if (drmSetInterfaceVersion(fd, &sv))
    {
        close(fd);
        return FALSE;
    }

    id = drmGetBusid(fd);
    devid = LS_DRICreatePCIBusID(pdev);

    if(id)
    {
        xf86Msg(X_INFO, "pci probe: id : %s\n", id);
    }

    if(devid)
    {
        xf86Msg(X_INFO, "pci probe: devid : %s\n", devid);
    }

    if (id && devid && !strcmp(id, devid))
    {
        ret = LS_CheckOutputs(fd, NULL);
    }

    close(fd);
    free(id);
    free(devid);
    return ret;
}


Bool LS_PciProbe(DriverPtr driver,
             int entity_num, struct pci_device *dev, intptr_t match_data)
{
    ScrnInfoPtr scrn = NULL;

    scrn = xf86ConfigPciEntity(scrn, 0, entity_num, NULL,
                               NULL, NULL, NULL, NULL, NULL);

    if (scrn)
    {
        const char *devpath;
        GDevPtr devSection = xf86GetDevFromEntity(
                scrn->entityList[0], scrn->entityInstanceList[0]);

        devpath = xf86FindOptionValue(devSection->options, "kmsdev");

        xf86DrvMsg(scrn->scrnIndex, X_CONFIG, "PCI probe: kmsdev=%s\n",
                (NULL != devpath) ? devpath : "NULL");

        if (probe_pci_hw(devpath, dev))
        {
            // suijingfeng: why here we don't pass LS_PciProbe and
            // hook it with probe ?
            LS_SetupScrnHooks(scrn, NULL);

            xf86DrvMsg(scrn->scrnIndex, X_CONFIG,
                       "claimed PCI slot %d@%d:%d:%d\n",
                       dev->bus, dev->domain, dev->dev, dev->func);
            xf86DrvMsg(scrn->scrnIndex, X_INFO,
                       "using %s\n", devpath ? devpath : "default device");

            LS_SetupEntity(scrn, entity_num);
        }
        else
        {
            scrn = NULL;
        }
    }
    return scrn != NULL;
}
#endif



#ifdef XSERVER_PLATFORM_BUS

static Bool probe_hw(struct xf86_platform_device *platform_dev)
{
    int fd;
    const char *path = xf86_get_platform_device_attrib(platform_dev, ODEV_ATTRIB_PATH);
//
// Since xf86platformBus.h is only included when XSERVER_PLATFORM_BUS is
// defined, and configure.ac only defines that on systems with udev,
// remove XF86_PDEV_SERVER_FD will breaks the build on non-udev systems
// like Solaris.
//
// XF86_PDEV_SERVER_FD is defined since 2014
// With systemd-logind support, the xserver, rather than the drivers will
// be responsible for opening/closing the fd for drm nodes.
//
// This commit adds a fd member to OdevAttributes to store the fd to pass
// it along to the driver.
//
// systemd-logind tracks devices by their chardev major + minor numbers,
// so also add OdevAttributes to store the major and minor.

#ifdef XF86_PDEV_SERVER_FD
    if (platform_dev && (platform_dev->flags & XF86_PDEV_SERVER_FD))
    {
        // suijingfeng: here we print to make sure that
        // it is server maneged case ...
        xf86Msg(X_INFO, "XF86: SERVER MANAGED FD\n");

        fd = xf86_platform_device_odev_attributes(platform_dev)->fd;
        if (fd == -1)
        {
            // here why we dont give another try to
            // call LS_OpenHW with path ?
            xf86Msg(X_INFO, "Platform probe: get fd from platform failed.\n");

            return FALSE;
        }

        return LS_CheckOutputs(fd, NULL);
    }
#endif

    if(NULL == path)
    {
         xf86Msg(X_INFO, "Platform probe: get path from platform failed.\n");
    }

    fd = LS_OpenHW(path);
    if (fd != -1)
    {
        int ret = LS_CheckOutputs(fd, NULL);
        close(fd);

        xf86Msg(X_INFO, "Platform probe: using drv %s\n",
                path ? path : "default device");

        return ret;
    }

    return FALSE;
}


Bool LS_PlatformProbe(DriverPtr driver, int entity_num, int flags,
              struct xf86_platform_device *dev, intptr_t match_data)
{
    ScrnInfoPtr scrn = NULL;

    int scr_flags = 0;

    if (flags & PLATFORM_PROBE_GPU_SCREEN)
    {
        scr_flags = XF86_ALLOCATE_GPU_SCREEN;
        xf86Msg(X_INFO, "XF86_ALLOCATE_GPU_SCREEN\n");
    }

    if (probe_hw(dev))
    {
        // This function allocates a new ScrnInfoRec in the xf86Screens[] array.
        // This function is normally called by the video driver Probe() functions.
        // The return value is a pointer to the newly allocated ScrnInfoRec.
        //
        // The scrnIndex, origIndex, module and drv fields are initialised.
        // The reference count in drv is incremented.
        //
        // The storage for any currently allocated ``privates'' pointers
        // is also allocated and the privates field initialised (the
        // privates data is of course not allocated or initialised).
        //
        // This function never returns on failure. If the allocation fails,
        // the server exits with a fatal error.
        // The flags value is not currently used, and should be set to zero.
        //
        // At the completion of this, a list of ScrnInfoRecs have been
        // allocated in the xf86Screens[] array, and the associated entities
        // and fixed resources have been claimed.
        //
        // The following ScrnInfoRec fields must be initialised at this point:
        //
        //  driverVersion
        //  driverName
        //  scrnIndex(*)
        //  origIndex(*)
        //  drv(*)
        //  module(*)
        //  name
        //  Probe
        //  PreInit
        //  ScreenInit
        //  EnterVT
        //  LeaveVT
        //  numEntities
        //  entityList
        //  access
        //
        //  (*) These are initialised when the ScrnInfoRec is allocated,
        //  and not explicitly by the driver.
        //
        //  The following ScrnInfoRec fields must be initialised
        //  if the driver is going to use them:
        //
        //    SwitchMode
        //    AdjustFrame
        //    FreeScreen
        //    ValidMode
        //
        scrn = xf86AllocateScreen(driver, scr_flags);

        if (xf86IsEntitySharable(entity_num))
        {
            xf86SetEntityShared(entity_num);
            xf86Msg(X_INFO, "Entity %d is sharable.\n", entity_num);
        }
        else
        {
            xf86Msg(X_INFO, "Entity %d is NOT sharable.\n", entity_num);
        }

        xf86AddEntityToScreen(scrn, entity_num);

        LS_SetupScrnHooks(scrn, NULL);

        LS_SetupEntity(scrn, entity_num);
    }

    return scrn != NULL;
}
#endif
