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

/* gsgpu */

#include "driver.h"

#include <gsgpu_drm.h>
#include <gsgpu.h>

#include "gsgpu_device.h"

void gsgpu_device_init(ScrnInfoPtr pScrn)
{
    loongsonPtr lsp = loongsonPTR(pScrn);
    uint32_t major_version;
    uint32_t minor_version;
    int ret;

    ret = gsgpu_device_initialize(lsp->fd,
                                  &major_version,
                                  &minor_version,
                                  &lsp->gsgpu);
    if (ret)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "gsgpu_device_initialize failed\n");
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "gsgpu device initialized, version: %u.%u\n",
               major_version, minor_version);
}
