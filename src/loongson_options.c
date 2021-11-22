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

#include <xf86str.h>
#include <xf86.h>

#include "loongson_options.h"

static const OptionInfoRec Options[] = {
    {OPTION_SW_CURSOR, "SWcursor", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_DEVICE_PATH, "kmsdev", OPTV_STRING, {0}, FALSE},
    {OPTION_SHADOW_FB, "ShadowFB", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_ACCEL_METHOD, "AccelMethod", OPTV_STRING, {0}, FALSE},
    {OPTION_EXA_TYPE, "ExaType", OPTV_STRING, {0}, FALSE},
    {OPTION_PAGEFLIP, "PageFlip", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_ZAPHOD_HEADS, "ZaphodHeads", OPTV_STRING, {0}, FALSE},
    {OPTION_DOUBLE_SHADOW, "DoubleShadow", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_ATOMIC, "Atomic", OPTV_BOOLEAN, {0}, FALSE},
    {OPTION_DEBUG, "Debug", OPTV_BOOLEAN, {0}, FALSE},
    {-1, NULL, OPTV_NONE, {0}, FALSE}
};


const OptionInfoRec *LS_AvailableOptions(int chipid, int busid)
{
    xf86Msg(X_INFO, " %s: chipid=%d, busid=%d.\n",
        __func__, chipid, busid);
    return Options;
}


Bool LS_ProcessOptions(ScrnInfoPtr pScrn, OptionInfoPtr *ppOptions)
{
    OptionInfoPtr pTmpOps;

    xf86CollectOptions(pScrn, NULL);

    pTmpOps = (OptionInfoPtr) malloc(sizeof(Options));
    if (NULL == pTmpOps)
    {
        return FALSE;
    }

    memcpy(pTmpOps, Options, sizeof(Options));

    *ppOptions = pTmpOps;

    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, pTmpOps);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: Options Processed.\n", __func__);

    return TRUE;
}

void LS_FreeOptions(ScrnInfoPtr pScrn, OptionInfoPtr *ppOptions)
{
    OptionInfoPtr pTmpOps = *ppOptions;

    free(pTmpOps);

    *ppOptions = NULL;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Options Freed.\n");
}
