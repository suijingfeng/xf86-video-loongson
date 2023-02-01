/*
 * Copyright (C) 2023 Loongson Corporation
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

#include "etnaviv_resolve.h"

/*
    from: 4x32 pixel

    000 001  002 003  004 005  006 007

    to: 16x8 pixel

    000 001
    002 003
    004 005
    006 007
*/
static void generic_resolve_4x2_tile(uint32_t *pSrc,
                                     uint32_t *pDst,
                                     int dst_stride)
{
    uint32_t *pDst0 = pDst;
    uint32_t *pDst1 = pDst0 + dst_stride;
    uint32_t *pDst2 = pDst1 + dst_stride;
    uint32_t *pDst3 = pDst2 + dst_stride;
    int dst_stride_x4 = dst_stride * 4;
    int j;

    for (j = 0; j < 4; ++j)
    {
        /*
          from: 4x8 pixel matrix

            000 001 002 003  004 005 006 007  ...  024 025 026 027  028 029 030 031

          to: 4x8 pixel matrix

            000 001 002 003  016 017 018 019
            004 005 006 007  020 021 022 023
            008 009 010 011  024 025 026 027
            012 013 014 015  028 029 030 031
        */
        memcpy(pDst0, &pSrc[0],  16); memcpy(&pDst0[16], &pSrc[64],  16);
        memcpy(pDst1, &pSrc[16], 16); memcpy(&pDst1[16], &pSrc[80],  16);
        memcpy(pDst2, &pSrc[32], 16); memcpy(&pDst2[16], &pSrc[96],  16);
        memcpy(pDst3, &pSrc[48], 16); memcpy(&pDst3[16], &pSrc[112], 16);

        pSrc += 32;
        pDst0 += dst_stride_x4;
        pDst1 += dst_stride_x4;
        pDst2 += dst_stride_x4;
        pDst3 += dst_stride_x4;
    }
}


/* tile is stored in continues row
 *
 * supertile : 64x64 pixel, each pixel is 4 byte
 */
static void etnaviv_resolve_supertile_impl(uint32_t *pSrc,
                                           uint32_t *pDst,
                                           int dst_stride)
{
    int i, j;

    // each supertile have 4x8 groups
    for (j = 0; j < 4; ++j)
    {
        uint32_t *pSrcGroup = pSrc;
        uint32_t *pDstGroup = pDst;

        for (i = 0; i < 8; ++i)
        {
            // each group have 4x2 tiles, each tile is 16 pixel
            generic_resolve_4x2_tile(pSrcGroup, pDstGroup, dst_stride);
            // 512 byte (8 tiles, each tile is 16 pixel)
            pSrcGroup += 8 * 16;
            // 32 byte (step 8 pixel)
            pDstGroup += 8;
        }

        pSrc += 64 * 16; /* 64x16 pixel */
        pDst += dst_stride * 16;
    }
}

Bool etnaviv_supertile_to_linear_generic(uint32_t *src_bits,
                                         uint32_t *dst_bits,
                                         int src_stride,
                                         int dst_stride,
                                         int src_x,
                                         int src_y,
                                         int dst_x,
                                         int dst_y,
                                         int width,
                                         int height)
{
    // width / 64
    int num_supertile_x = width >> 6;
    // height / 64
    int num_supertile_y = height >> 6;
    int remain_x = width & 63;
    int remain_y = height & 63;
    int i, j;

    dst_bits += dst_stride * dst_y + dst_x;
    src_bits += src_stride * src_y + src_x;

    for (j = 0; j < num_supertile_y; j++)
    {
        uint32_t *pSrc = src_bits;
        uint32_t *pDst = dst_bits;

        for (i = 0; i < num_supertile_x; i++)
        {
            etnaviv_resolve_supertile_impl(pSrc, pDst, dst_stride);
            pSrc += 64 * 64; // step 64x64 pixel
            pDst += 64; // step 64 pxiel
        }

        if (remain_x)
        {
            //// etnaviv_resolve_supertile_row_tail(pSrc, pDst, dst_stride, remain_x);
        }

        src_bits += src_stride * 64;
        dst_bits += dst_stride * 64;
    }

    // remain_y < 64
    if (remain_y)
    {
        uint32_t *pSrc = src_bits;
        uint32_t *pDst = dst_bits;

        for (i = 0; i < num_supertile_x; i++)
        {
            // 16 x 16 in pixel, 16 x 64 in bytes
            //// etnaviv_resolve_supertile_col_tail(pSrc, pDst, dst_stride, remain_y);

            // step 64x64 pixel
            pSrc += 64 * 64;
            // step 64 pxiel in row derection
            pDst += 64;
        }

        // remain_x < 64
        if (remain_x)
        {
            //// etnaviv_resolve_supertile_row_col_tail(pSrc, pDst, dst_stride, remain_x, remain_y);
        }
    }

    return TRUE;
}
