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
#include <malloc.h>
#include <xf86.h>

#include "driver.h"
#include "loongson_helpers.h"

char *LS_DRICreatePCIBusID(const struct pci_device *dev)
{
    char *busID;

    if (asprintf(&busID, "pci:%04x:%02x:%02x.%d",
                 dev->domain, dev->bus, dev->dev, dev->func) == -1)
        return NULL;

    return busID;
}

/*
 * Return TRUE if prime is supported
 */
Bool LS_CheckPrime(int fd)
{
    int ret;
    uint64_t value = 0;

    ret = drmGetCap(fd, DRM_CAP_PRIME, &value);
    if (ret == 0)
    {
        if (value & DRM_PRIME_CAP_EXPORT)
        {
             return TRUE;
        }
    }

    return FALSE;
}


int LS_CheckOutputs(int fd, int *count)
{
    drmModeResPtr res;
    Bool ret;

    res = drmModeGetResources(fd);
    if (!res)
    {
        return FALSE;
    }

    ret = res->count_connectors > 0;

    if (count)
    {
        *count = res->count_connectors;
    }

    drmModeFreeResources(res);

    if (ret == FALSE)
    {
        ret = LS_CheckPrime(fd);
    }

    return ret;
}


int LS_OpenHW(const char *dev)
{
    int fd;

    if (dev)
    {
        xf86Msg(X_INFO, "%s: Opening %s ...\n", __func__, dev);

        fd = open(dev, O_RDWR | O_CLOEXEC, 0);
    }
    else
    {
        dev = getenv("KMSDEVICE");
        if ((NULL == dev) || ((fd = open(dev, O_RDWR | O_CLOEXEC, 0)) == -1))
        {
            dev = "/dev/dri/card0";
            fd = open(dev, O_RDWR | O_CLOEXEC, 0);
        }
    }

    if (fd == -1)
    {
        xf86Msg(X_ERROR, "%s: %s: %s\n",
                    __func__, dev, strerror(errno));
    }

    return fd;
}
