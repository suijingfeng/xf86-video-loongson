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

#include <xf86drm.h>
#include "driver.h"
#include "loongson_damage.h"

DamagePtr loongson_damage_create(ScreenPtr pScreen, PixmapPtr pRootPixmap)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    DamagePtr pDamage;

    pDamage = DamageCreate(NULL,
                           NULL,
                           DamageReportNone,
                           TRUE,
                           pScreen,
                           pRootPixmap);
    if (!pDamage)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to create screen damage record\n");
        return NULL;
    }

    DamageRegister(&pRootPixmap->drawable, pDamage);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Damage tracking initialized\n");

    return pDamage;
}

void loongson_damage_destroy(ScreenPtr pScreen, DamagePtr *ppDamage)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    DamagePtr pDamage = *ppDamage;

    if (!pDamage)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "pDamage is NULL\n");
        return;
    }

    DamageUnregister(pDamage);
    DamageDestroy(pDamage);
    *ppDamage = NULL;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Damage tracking destroyed\n");
}
