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

#include <xf86.h>

#ifdef HAVE_LSX
#include <lsxintrin.h>
#endif

#include "gsgpu_resolve.h"

/* TODO: provide a none simd version */

Bool lsx_resolve_gsgpu_tile_4x4(uint32_t *src_bits,
                                uint32_t *dst_bits,
                                int src_stride,
                                int dst_stride,
                                int src_bpp,
                                int dst_bpp,
                                int src_x,
                                int src_y,
                                int dest_x,
                                int dest_y,
                                int width,
                                int height)
{
#ifdef HAVE_LSX
    int i, j, l, t;
    int d = 0, r = 0;
    uint8_t *pix_src, *pix_dst;
    __m128i v0, v1, v2, v3, v4, v5, v6, v7;

    l = src_x & 0x3;
    if (l)
    {
        l = 4 - l;
        src_x += l;
        dest_x += l;
        width -= l;
    }

    t = src_y & 0x3;
    if (t)
    {
        t = 4 - t;
        src_y += t;
        dest_y += t;
        height -= t;
    }

    if (height > 0)
    {
        d = height & 0x3;
        height -= d;
    }

    if (width > 0)
    {
        r = width & 0x3;
        width -= r;
    }

    dst_bits += dst_stride * (dest_y - src_y) + dest_x - src_x;
    dst_stride <<= 2;
    src_stride <<= 2;

    for (i = src_y; i < height + src_y; i += 4)
    {
        for (j = src_x; j < width + src_x; j += 4)
        {
            pix_src = (uint8_t *)src_bits + i * src_stride + (j << 4);
            pix_dst = (uint8_t *)dst_bits + i * dst_stride + (j << 2);

            v0 = __lsx_vld(pix_src, 0);
            v1 = __lsx_vld(pix_src, 16);
            v2 = __lsx_vld(pix_src, 32);
            v3 = __lsx_vld(pix_src, 48);

            v4 = __lsx_vilvl_d(v1, v0);
            v5 = __lsx_vilvh_d(v1, v0);
            v6 = __lsx_vilvl_d(v3, v2);
            v7 = __lsx_vilvh_d(v3, v2);

            __lsx_vst(v4, pix_dst, 0);
            __lsx_vst(v5, pix_dst + dst_stride, 0);
            __lsx_vst(v6, pix_dst + dst_stride * 2, 0);
            __lsx_vst(v7, pix_dst + dst_stride * 3, 0);
        }

        if (l)
        {
            /* happens if the width of the window can not be devide by 4 */
            j = src_x - 4;
            pix_src = (uint8_t *)src_bits + i * src_stride + (j << 4);
            pix_dst = (uint8_t *)dst_bits + i * dst_stride + (j << 2);

            v0 = __lsx_vld(pix_src, 0);
            v1 = __lsx_vld(pix_src, 16);
            v2 = __lsx_vld(pix_src, 32);
            v3 = __lsx_vld(pix_src, 48);

            v4 = __lsx_vilvl_d(v1, v0);
            v5 = __lsx_vilvh_d(v1, v0);
            v6 = __lsx_vilvl_d(v3, v2);
            v7 = __lsx_vilvh_d(v3, v2);

            switch (l)
            {
            case 1:
                __lsx_vstelm_w(v4, pix_dst, 12, 3);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 12, 3);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 12, 3);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 12, 3);
                break;
            case 2:
                __lsx_vstelm_w(v4, pix_dst, 8, 2);
                __lsx_vstelm_w(v4, pix_dst, 12, 3);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 8, 2);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 12, 3);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 8, 2);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 12, 3);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 8, 2);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 12, 3);
                break;
            case 3:
                __lsx_vstelm_w(v4, pix_dst, 4, 1);
                __lsx_vstelm_w(v4, pix_dst, 8, 2);
                __lsx_vstelm_w(v4, pix_dst, 12, 3);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 4, 1);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 8, 2);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 12, 3);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 4, 1);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 8, 2);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 12, 3);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 4, 1);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 8, 2);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 12, 3);
                break;
            default:
                break;
            }
        }
        if (r)
        {
            j = width + src_x;
            /* happens if the width of the window can not be devide by 4 */

            pix_src = (uint8_t *)src_bits + i * src_stride + (j << 4);
            pix_dst = (uint8_t *)dst_bits + i * dst_stride + (j << 2);

            v0 = __lsx_vld(pix_src, 0);
            v1 = __lsx_vld(pix_src, 16);
            v2 = __lsx_vld(pix_src, 32);
            v3 = __lsx_vld(pix_src, 48);

            v4 = __lsx_vilvl_d(v1, v0);
            v5 = __lsx_vilvh_d(v1, v0);
            v6 = __lsx_vilvl_d(v3, v2);
            v7 = __lsx_vilvh_d(v3, v2);

            switch (r)
            {
            case 1:
                __lsx_vstelm_w(v4, pix_dst, 0, 0);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 0, 0);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 0, 0);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 0, 0);
                break;
            case 2:
                __lsx_vstelm_w(v4, pix_dst, 0, 0);
                __lsx_vstelm_w(v4, pix_dst, 4, 1);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 0, 0);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 4, 1);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 0, 0);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 4, 1);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 0, 0);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 4, 1);
                break;
            case 3:
                __lsx_vstelm_w(v4, pix_dst, 0, 0);
                __lsx_vstelm_w(v4, pix_dst, 4, 1);
                __lsx_vstelm_w(v4, pix_dst, 8, 2);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 0, 0);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 4, 1);
                __lsx_vstelm_w(v5, pix_dst + dst_stride, 8, 2);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 0, 0);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 4, 1);
                __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 8, 2);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 0, 0);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 4, 1);
                __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 8, 2);
                break;
            }
        }
    }

    if (t)
    {
        int t_height = t + height;

        i = src_y - 4;
        switch (t)
        {
        case 3:
            for (j = src_x; j < width + src_x; j += 4)
            {
                pix_src = (uint8_t *)src_bits + (j << 4) + i * src_stride;
                pix_dst = (uint8_t *)dst_bits + (j << 2) + i * dst_stride;

                v0 = __lsx_vld(pix_src, 0);
                v1 = __lsx_vld(pix_src, 16);
                v2 = __lsx_vld(pix_src, 32);
                v3 = __lsx_vld(pix_src, 48);

                v5 = __lsx_vilvh_d(v1, v0);
                v6 = __lsx_vilvl_d(v3, v2);
                v7 = __lsx_vilvh_d(v3, v2);
                if (t_height == 1)
                {
                    __lsx_vst(v5, pix_dst + dst_stride, 0);
                }
                else if (t_height == 2)
                {
                    __lsx_vst(v5, pix_dst + dst_stride, 0);
                    __lsx_vst(v6, pix_dst + dst_stride * 2, 0);
                }
                else
                {
                    __lsx_vst(v5, pix_dst + dst_stride, 0);
                    __lsx_vst(v6, pix_dst + dst_stride * 2, 0);
                    __lsx_vst(v7, pix_dst + dst_stride * 3, 0);
                }
            }
            break;
        case 2:
            for (j = src_x; j < width + src_x; j += 4)
            {
                pix_src = (uint8_t *)src_bits + (j << 4) + i * src_stride;
                pix_dst = (uint8_t *)dst_bits + (j << 2) + i * dst_stride;

                v2 = __lsx_vld(pix_src, 32);
                v3 = __lsx_vld(pix_src, 48);

                v6 = __lsx_vilvl_d(v3, v2);
                v7 = __lsx_vilvh_d(v3, v2);
                if (t_height == 1)
                    __lsx_vst(v6, pix_dst + dst_stride * 2, 0);
                else
                {
                    __lsx_vst(v6, pix_dst + dst_stride * 2, 0);
                    __lsx_vst(v7, pix_dst + dst_stride * 3, 0);
                }
            }
            break;
        case 1:
            for (j = src_x; j < width + src_x; j += 4)
            {
                pix_src = (uint8_t *)src_bits + (j << 4) + i * src_stride;
                pix_dst = (uint8_t *)dst_bits + (j << 2) + i * dst_stride;

                v2 = __lsx_vld(pix_src, 32);
                v3 = __lsx_vld(pix_src, 48);

                v7 = __lsx_vilvh_d(v3, v2);

                __lsx_vst(v7, pix_dst + dst_stride * 3, 0);
            }
            break;
        default:
            break;
        }
        if (l)
        {
            /* happens if the width of the window can not be devide by 4 */
            j = src_x - 4;
            pix_src = (uint8_t *)src_bits + i * src_stride + (j << 4);
            pix_dst = (uint8_t *)dst_bits + i * dst_stride + (j << 2);

            v0 = __lsx_vld(pix_src, 0);
            v1 = __lsx_vld(pix_src, 16);
            v2 = __lsx_vld(pix_src, 32);
            v3 = __lsx_vld(pix_src, 48);

            v4 = __lsx_vilvl_d(v1, v0);
            v5 = __lsx_vilvh_d(v1, v0);
            v6 = __lsx_vilvl_d(v3, v2);
            v7 = __lsx_vilvh_d(v3, v2);

            switch (l)
            {
            case 1:
                switch (t)
                {
                case 3:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 12, 3);
                    if (t_height == 1)
                        break;
                case 2:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 12, 3);
                    if (t_height <= 2)
                        break;
                case 1:
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 12, 3);
                    break;
                default:
                    break;
                }
                break;
            case 2:
                switch (t)
                {
                case 3:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 8, 2);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 12, 3);
                    if (t_height == 1)
                        break;
                case 2:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 8, 2);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 12, 3);
                    if (t_height <= 2)
                        break;
                case 1:
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 8, 2);
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 12, 3);
                    break;
                default:
                    break;
                }
                break;
            case 3:
                switch (t)
                {
                case 3:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 4, 1);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 8, 2);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 12, 3);
                    if (t_height == 1)
                        break;
                case 2:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 4, 1);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 8, 2);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 12, 3);
                    if (t_height <= 2)
                        break;
                case 1:
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 4, 1);
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 8, 2);
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 12, 3);
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }
        if (r)
        {
            /* happens if the width of the window can not be devide by 4 */
            j = width + src_x;
            pix_src = (uint8_t *)src_bits + i * src_stride + (j << 4);
            pix_dst = (uint8_t *)dst_bits + i * dst_stride + (j << 2);

            v0 = __lsx_vld(pix_src, 0);
            v1 = __lsx_vld(pix_src, 16);
            v2 = __lsx_vld(pix_src, 32);
            v3 = __lsx_vld(pix_src, 48);

            v4 = __lsx_vilvl_d(v1, v0);
            v5 = __lsx_vilvh_d(v1, v0);
            v6 = __lsx_vilvl_d(v3, v2);
            v7 = __lsx_vilvh_d(v3, v2);

            switch (r)
            {
            case 1:
                switch (t)
                {
                case 3:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 0, 0);
                    if (t_height == 1)
                        break;
                case 2:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 0, 0);
                    if (t_height <= 2)
                        break;
                case 1:
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 0, 0);
                    break;
                default:
                    break;
                }
                break;
            case 2:
                switch (t)
                {
                case 3:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 0, 0);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 4, 1);
                    if (t_height == 1)
                        break;
                case 2:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 0, 0);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 4, 1);
                    if (t_height <= 2)
                        break;
                case 1:
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 0, 0);
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 4, 1);
                    break;
                default:
                    break;
                }
                break;
            case 3:
                switch (t)
                {
                case 3:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 0, 0);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 4, 1);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 8, 2);
                    if (t_height == 1)
                        break;
                case 2:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 0, 0);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 4, 1);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 8, 2);
                    if (t_height <= 2)
                        break;
                case 1:
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 0, 0);
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 4, 1);
                    __lsx_vstelm_w(v7, pix_dst + dst_stride * 3, 8, 2);
                    break;
                default:
                    break;
                }
                break;
            }
        }
    }

    if (d)
    {
        i = height + src_y;
        switch (d)
        {
        case 3:
            for (j = src_x; j < width + src_x; j += 4)
            {
                pix_src = (uint8_t *)src_bits + i * src_stride + (j << 4);
                pix_dst = (uint8_t *)dst_bits + i * dst_stride + (j << 2);

                v0 = __lsx_vld(pix_src, 0);
                v1 = __lsx_vld(pix_src, 16);
                v2 = __lsx_vld(pix_src, 32);
                v3 = __lsx_vld(pix_src, 48);

                v4 = __lsx_vilvl_d(v1, v0);
                v5 = __lsx_vilvh_d(v1, v0);
                v6 = __lsx_vilvl_d(v3, v2);

                __lsx_vst(v4, pix_dst, 0);
                __lsx_vst(v5, pix_dst + dst_stride, 0);
                __lsx_vst(v6, pix_dst + dst_stride * 2, 0);
            }
            break;
        case 2:
            for (j = src_x; j < width + src_x; j += 4)
            {
                pix_src = (uint8_t *)src_bits + i * src_stride + (j << 4);
                pix_dst = (uint8_t *)dst_bits + i * dst_stride + (j << 2);

                v0 = __lsx_vld(pix_src, 0);
                v1 = __lsx_vld(pix_src, 16);

                v4 = __lsx_vilvl_d(v1, v0);
                v5 = __lsx_vilvh_d(v1, v0);

                __lsx_vst(v4, pix_dst, 0);
                __lsx_vst(v5, pix_dst + dst_stride, 0);
            }
            break;
        case 1:
            for (j = src_x; j < width + src_x; j += 4)
            {
                pix_src = (uint8_t *)src_bits + i * src_stride + (j << 4);
                pix_dst = (uint8_t *)dst_bits + i * dst_stride + (j << 2);

                v0 = __lsx_vld(pix_src, 0);
                v1 = __lsx_vld(pix_src, 16);

                v4 = __lsx_vilvl_d(v1, v0);

                __lsx_vst(v4, pix_dst, 0);
            }
            break;
        default:
            break;
        }
        if (l)
        {
            /* happens if the width of the window can not be devide by 4 */
            j = src_x - 4;
            pix_src = (uint8_t *)src_bits + i * src_stride + (j << 4);
            pix_dst = (uint8_t *)dst_bits + i * dst_stride + (j << 2);

            v0 = __lsx_vld(pix_src, 0);
            v1 = __lsx_vld(pix_src, 16);
            v2 = __lsx_vld(pix_src, 32);
            v3 = __lsx_vld(pix_src, 48);

            v4 = __lsx_vilvl_d(v1, v0);
            v5 = __lsx_vilvh_d(v1, v0);
            v6 = __lsx_vilvl_d(v3, v2);
            v7 = __lsx_vilvh_d(v3, v2);

            switch (l)
            {
            case 1:
                switch (d)
                {
                case 3:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 12, 3);
                case 2:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 12, 3);
                case 1:
                    __lsx_vstelm_w(v4, pix_dst, 12, 3);
                    break;
                default:
                    break;
                }
                break;
            case 2:
                switch (d)
                {
                case 3:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 8, 2);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 12, 3);
                case 2:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 8, 2);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 12, 3);
                case 1:
                    __lsx_vstelm_w(v4, pix_dst, 8, 2);
                    __lsx_vstelm_w(v4, pix_dst, 12, 3);
                    break;
                default:
                    break;
                }
                break;
            case 3:
                switch (d)
                {
                case 3:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 4, 1);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 8, 2);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 12, 3);
                case 2:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 4, 1);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 8, 2);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 12, 3);
                case 1:
                    __lsx_vstelm_w(v4, pix_dst, 4, 1);
                    __lsx_vstelm_w(v4, pix_dst, 8, 2);
                    __lsx_vstelm_w(v4, pix_dst, 12, 3);
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }
        if (r)
        {
            /* happens if the width of the window can not be devide by 4 */
            j = width + src_x;
            pix_src = (uint8_t *)src_bits + i * src_stride + (j << 4);
            pix_dst = (uint8_t *)dst_bits + i * dst_stride + (j << 2);

            v0 = __lsx_vld(pix_src, 0);
            v1 = __lsx_vld(pix_src, 16);
            v2 = __lsx_vld(pix_src, 32);
            v3 = __lsx_vld(pix_src, 48);

            v4 = __lsx_vilvl_d(v1, v0);
            v5 = __lsx_vilvh_d(v1, v0);
            v6 = __lsx_vilvl_d(v3, v2);
            v7 = __lsx_vilvh_d(v3, v2);

            switch (r)
            {
            case 1:
                switch (d)
                {
                case 3:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 0, 0);
                case 2:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 0, 0);
                case 1:
                    __lsx_vstelm_w(v4, pix_dst, 0, 0);
                    break;
                default:
                    break;
                }
                break;
            case 2:
                switch (d)
                {
                case 3:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 0, 0);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 4, 1);
                case 2:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 0, 0);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 4, 1);
                case 1:
                    __lsx_vstelm_w(v4, pix_dst, 0, 0);
                    __lsx_vstelm_w(v4, pix_dst, 4, 1);
                    break;

                default:
                    break;
                }
                break;
            case 3:
                switch (d)
                {
                case 3:
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 0, 0);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 4, 1);
                    __lsx_vstelm_w(v6, pix_dst + dst_stride * 2, 8, 2);
                case 2:
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 0, 0);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 4, 1);
                    __lsx_vstelm_w(v5, pix_dst + dst_stride, 8, 2);
                case 1:
                    __lsx_vstelm_w(v4, pix_dst, 0, 0);
                    __lsx_vstelm_w(v4, pix_dst, 4, 1);
                    __lsx_vstelm_w(v4, pix_dst, 8, 2);
                    break;

                default:
                    break;
                }
                break;
            }
        }
    }
#endif

    return TRUE;
}
