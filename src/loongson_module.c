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
#include <xf86.h>
#include <xf86Pci.h>

#ifdef XSERVER_PLATFORM_BUS
#include <xf86platformBus.h>
#endif

#include "driver.h"

#include "loongson_options.h"
#include "loongson_entity.h"
#include "loongson_probe.h"

static void Identify(int flags);
static Bool DriverFunc(ScrnInfoPtr scrn, xorgDriverFuncOp op, void *data);


static SymTabRec Chipsets[] =
{
        {0, "loongson-drm"},
        {1, "lsdc"},
        {2, "gsgpu"},
	{-1, NULL}
};

static void Identify(int flags)
{
    xf86PrintChipsets("loongson", "Userspace driver for Kernel Drivers", Chipsets);
}


static Bool DriverFunc(ScrnInfoPtr scrn, xorgDriverFuncOp op, void *data)
{
    xorgHWFlags *flag;

    switch (op)
    {
        case GET_REQUIRED_HW_INTERFACES:
        {
            flag = (CARD32 *) data;
            *flag = 0;

            xf86Msg(X_INFO, "loongson: mips actually not require hw io.\n");
            return TRUE;
        }
        case SUPPORTS_SERVER_FDS:
        {
            xf86Msg(X_INFO, "loongson: supported server managed fd.\n");
            return TRUE;
        }
        default:
            return FALSE;
    }
}

static const struct pci_id_match loongson_device_match[] = {
        LOONGSON_DEVICE_MATCH_V1 ,
        LOONGSON_DEVICE_MATCH_V2 ,
        LOONGSON_DEVICE_MATCH_DC_IN_7A2000 ,
        LOONGSON_DEVICE_MATCH_GSGPU ,
	{0, 0, 0},
};

static MODULESETUPPROTO(fnSetup);

static XF86ModuleVersionInfo VersRec = {
    "loongson",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    XORG_VERSION_MAJOR,
    XORG_VERSION_MINOR,
    XORG_VERSION_PATCH,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0, 0, 0, 0}
};


//
// suijingfeng: I_ means Interface
//
_X_EXPORT DriverRec I_LoongsonDrv = {
    .driverVersion = 1,
    .driverName = "loongson",
    .Identify = Identify,
    .Probe = LS_Probe,
    .AvailableOptions = LS_AvailableOptions,
    .module = NULL,
    .refCount = 0,
    .driverFunc = DriverFunc,
    .supported_devices = loongson_device_match,
    .PciProbe = LS_PciProbe,
#ifdef XSERVER_PLATFORM_BUS
    .platformProbe = LS_PlatformProbe,
#endif
};


static void * fnSetup(void *module, void *opts, int *errmaj, int *errmin)
{
    static Bool setupDone = FALSE;

    /* This module should be loaded only once, but check to be sure.
     */
    if (!setupDone)
    {
        setupDone = TRUE;
        xf86AddDriver(&I_LoongsonDrv, module, HaveDriverFuncs);

        /*
         * The return value must be non-NULL on success even though there
         * is no TearDownProc.
         */
        return (void *) 1;
    }
    else
    {
        if (errmaj)
        {
            *errmaj = LDR_ONCEONLY;
        }
        return NULL;
    }
}

_X_EXPORT XF86ModuleData loongsonModuleData =
{
    .vers = &VersRec,
    .setup = fnSetup,
    .teardown = NULL
};
