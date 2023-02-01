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

#ifdef HAVE_LASX
#include <lasxintrin.h>
#endif

#ifdef HAVE_LSX
#include <lsxintrin.h>
#endif

#include <stdint.h>
#include <string.h>
#include "lasx_blt.h"

void lasx_blt_one_line_u8(void *pDst, const void *pSrc, long unsigned int w)
{
#ifdef HAVE_LASX
    if ((uintptr_t)pDst & 1)
    {
        *(uint8_t *)pDst = *(uint8_t *)pSrc;
        pSrc += 1;
        pDst += 1;
        w -= 1;
    }

    /* the dst is 2 byte aligned here */
    if (w >= 2)
    {
        if ((uintptr_t)pDst & 3)
        {
            *(uint16_t *)pDst = *(uint16_t *)pSrc;
            pSrc += 2;
            pDst += 2;
            w -= 2;
        }
    }
    else
    {
        goto COPY_LESS_THAN_2;
    }

    /* the dst is 4 byte aligned here */
    if (w >= 4)
    {
        if ((uintptr_t)pDst & 7)
        {
            *(uint32_t *)pDst = *(uint32_t *)pSrc;

            w -= 4;
            pSrc += 4;
            pDst += 4;
        }
    }
    else
    {
        goto COPY_LESS_THAN_4;
    }

    /* the dst is 8 byte aligned here */
    if (w >= 8)
    {
        if ((uintptr_t)pDst & 15)
        {
            *(uint64_t *)pDst = *(uint64_t *)pSrc;

            w -= 8;
            pSrc += 8;
            pDst += 8;
        }
    }
    else
    {
        goto COPY_LESS_THAN_8;
    }

    /* the dst is 16 byte aligned here */
    if (w >= 16)
    {
        if ((uintptr_t)pDst & 31)
        {
            __lsx_vst(__lsx_vld(pSrc, 0), pDst, 0);

            w -= 16;
            pSrc += 16;
            pDst += 16;
        }
    }
    else
    {
        goto COPY_LESS_THAN_16;
    }

    /* the dst is 32 byte aligned here */
    if (w >= 32)
    {
        if ((uintptr_t)pDst & 63)
        {
            __lasx_xvst(__lasx_xvld(pSrc, 0), pDst, 0);

            w -= 32;
            pSrc += 32;
            pDst += 32;
        }
    }
    else
    {
        goto COPY_LESS_THAN_32;
    }

    while (w >= 128)
    {
         __m256i xv0, xv1, xv2, xv3;

         xv0 = __lasx_xvld(pSrc, 0);
         xv1 = __lasx_xvld(pSrc, 32);
         xv2 = __lasx_xvld(pSrc, 64);
         xv3 = __lasx_xvld(pSrc, 96);

         __lasx_xvst(xv0, pDst, 0);
         __lasx_xvst(xv1, pDst, 32);
         __lasx_xvst(xv2, pDst, 64);
         __lasx_xvst(xv3, pDst, 96);

         w -= 128;
         pSrc += 128;
         pDst += 128;
    }

    /* the dst is cache line aligned here */
    if (w >= 64)
    {
        __m256i xv0, xv1;
        xv0 = __lasx_xvld(pSrc, 0);
        xv1 = __lasx_xvld(pSrc, 32);

        __lasx_xvst(xv0, pDst, 0);
        __lasx_xvst(xv1, pDst, 32);

        w -= 64;
        pSrc += 64;
        pDst += 64;
    }

    if (w >= 32)
    {
         __lasx_xvst(__lasx_xvld(pSrc, 0), pDst, 0);

        w -= 32;
        pSrc += 32;
        pDst += 32;
    }

COPY_LESS_THAN_32:
    if (w >= 16)
    {
        __lsx_vst(__lsx_vld(pSrc, 0), pDst, 0);

        w -= 16;
        pSrc += 16;
        pDst += 16;
    }

COPY_LESS_THAN_16:
    if (w >= 8)
    {
        *(uint64_t *)pDst = *(uint64_t *)pSrc;

        w -= 8;
        pSrc += 8;
        pDst += 8;
    }

COPY_LESS_THAN_8:
    if (w >= 4)
    {
        *(uint32_t *)pDst = *(uint32_t *)pSrc;

        w -= 4;
        pSrc += 4;
        pDst += 4;
    }

COPY_LESS_THAN_4:
    if (w >= 2)
    {
        *(uint16_t *)pDst = *(uint16_t *)pSrc;
        pSrc += 2;
        pDst += 2;
        w -= 2;
    }

COPY_LESS_THAN_2:
    if (w)
    {
        /* copy the last one bytes if there is */
        *(uint8_t *)pDst = *(uint8_t *)pSrc;
    }
#else
    memcpy(pDst, pSrc, w);
#endif
}
