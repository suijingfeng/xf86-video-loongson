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

#ifndef LOONGSON_PROBE_H_
#define LOONGSON_PROBE_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86drm.h>

#ifdef XSERVER_LIBPCIACCESS
#include <pciaccess.h>
#endif


//#define PCI_VENDOR_AMD                0x1022
//#define PCI_VENDOR_ATI                0x1002
#define PCI_CHIP_ID_RX560               0x67ff

#define PCI_VENDOR_LOONGSON             0x0014
#define PCI_CHIP_ID_7A1000              0x7a06

#define PCI_VENDOR_ASPEED               0x1a03
#define PCI_CHIP_ID_AST2400             0x2000

// LEMOTE 3A3000 BOARD HAVE 0x00030000
#define LOONGSON_DEVICE_MATCH_V1                   \
    {                                              \
        PCI_VENDOR_LOONGSON, PCI_CHIP_ID_7A1000,   \
        PCI_MATCH_ANY, PCI_MATCH_ANY,              \
        0x00030000, 0x00ffff00, 0                  \
    }

// 3A4000 DEV & ENVALUE BOARD HAVE 0x00038000
#define LOONGSON_DEVICE_MATCH_V2                   \
    {                                              \
        PCI_VENDOR_LOONGSON, PCI_CHIP_ID_7A1000,   \
        PCI_MATCH_ANY, PCI_MATCH_ANY,              \
        0x00038000, 0x00ffff00, 0                  \
    }

// glamor developing on amdgpu rx560...
/*
#define AMDGPU_DEVICE_RX560                        \
    {                                              \
        0x1002, 0x67ff, 0x1da2, 0xe348,            \
        0x00030000, 0x00ffff00, 0                  \
    }
*/

Bool LS_Probe(DriverPtr drv, int flags);

Bool LS_PciProbe(DriverPtr driver,
             int entity_num, struct pci_device *pci_dev, intptr_t match_data);

Bool LS_PlatformProbe(DriverPtr driver,
        int entity_num, int flags, struct xf86_platform_device *platform_dev,
        intptr_t match_data);

#endif
