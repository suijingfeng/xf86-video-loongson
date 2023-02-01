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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <string.h>
#include <xf86.h>

#include "lasx_blt.h"
#include "lsx_blt.h"
#include "loongson_blt.h"

#define LOONGARCH_CFG2  0x2
#define LOONGARCH_LSX   (1 << 6)
#define LOONGARCH_LASX  (1 << 7)


void (*loongson_blt)(void *pDst, const void *pSrc, long unsigned int len);


#if defined(__loongarch__)
static int loongarch_detect_cpu_features(void)
{
    uint32_t cfg2 = 0;

    __asm__ volatile(
        "cpucfg %0, %1 \n\t"
        : "+&r"(cfg2)
        : "r"(LOONGARCH_CFG2)
    );
    return cfg2;
}
#endif


Bool loongarch_have_feature(int feature)
{
#if defined(__loongarch__)
    int loongarch_cpu_features;

    loongarch_cpu_features = loongarch_detect_cpu_features();

    return (loongarch_cpu_features & feature) == feature;
#else
    return FALSE;
#endif
}

static void loongson_memcpy(void *pDst,
                            const void *pSrc,
                            long unsigned int len)
{
    memcpy(pDst, pSrc, len);
}

void loongson_init_blitter(void)
{
    if (loongarch_have_feature(LOONGARCH_LASX))
    {
        loongson_blt = lasx_blt_one_line_u8;
        xf86Msg(X_INFO, "LoongArch: have LASX and LSX support\n");
        return;
    }

    if (loongarch_have_feature(LOONGARCH_LSX))
    {
        loongson_blt = lsx_blt_one_line_u8;
        xf86Msg(X_INFO, "LoongArch: have LSX support\n");
        return;
    }

    loongson_blt = loongson_memcpy;
}
