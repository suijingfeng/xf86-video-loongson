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

#ifdef HAVE_LSX
#include <lsxintrin.h>
#endif

#include "loongson_debug.h"
#include "etnaviv_resolve.h"
#include "write_bmp.h"

/* TODO: provide a none simd version */

Bool lsx_resolve_etnaviv_tile_4x4(uint32_t *src_bits,
                                  uint32_t *dst_bits,
                                  int src_stride,
                                  int dst_stride,
                                  int src_x,
                                  int src_y,
                                  int dest_x,
                                  int dest_y,
                                  int width,
                                  int height)
{
    TRACE_ENTER();

#ifdef HAVE_LSX
    int src_stride_tiled = src_stride * 4;
    int dst_stride_tiled = dst_stride * 4;
    int i, j;

    dst_bits += dst_stride * dest_y + dest_x;
    src_bits += src_stride * src_y + src_x;

    DEBUG_MSG("%s: src stride=%d, dst stride=%d, src addr: %p, dst addr: %p\n",
            __func__, src_stride, dst_stride, src_bits, dst_bits);

    for (i = 0; i < height; i += 4)
    {
        uint32_t *pSrc = src_bits;
        uint32_t *pDst0 = dst_bits;
        uint32_t *pDst1 = pDst0 + dst_stride;
        uint32_t *pDst2 = pDst1 + dst_stride;
        uint32_t *pDst3 = pDst2 + dst_stride;
        int offset = 0;

        for (j = 0; j < width; j += 4)
        {
            __m128i v0, v1, v2, v3;

            /* load one tile, tile is stored in continues row */
            v0 = __lsx_vld(pSrc, 0);
            v1 = __lsx_vld(pSrc, 16);
            v2 = __lsx_vld(pSrc, 32);
            v3 = __lsx_vld(pSrc, 48);

            __lsx_vstx(v0, pDst0, offset);
            __lsx_vstx(v1, pDst1, offset);
            __lsx_vstx(v2, pDst2, offset);
            __lsx_vstx(v3, pDst3, offset);

            offset += 16;
            pSrc += 16;
        }

        src_bits += src_stride_tiled;
        dst_bits += dst_stride_tiled;
    }
#endif

    TRACE_EXIT();

    return TRUE;
}


/*
    from: 4x32 pixel

    000 001  002 003  004 005  006 007

    to: 16x8 pixel

    000 001
    002 003
    004 005
    006 007
*/
static void lsx_resolve_4x2_tile(uint8_t *pSrc,
                                 uint8_t *pDst,
                                 int dst_stride)
{

    uint8_t *pDst0 = pDst;
    uint8_t *pDst1 = pDst0 + dst_stride;
    uint8_t *pDst2 = pDst1 + dst_stride;
    uint8_t *pDst3 = pDst2 + dst_stride;
    int dst_stride_x4 = dst_stride * 4;
    int j;

    for (j = 0; j < 4; ++j)
    {
        __m128i v0, v1, v2, v3;
        __m128i v4, v5, v6, v7;

        // T_000
        v0 = __lsx_vld(pSrc, 0);
        v1 = __lsx_vld(pSrc, 16);
        v2 = __lsx_vld(pSrc, 32);
        v3 = __lsx_vld(pSrc, 48);

        // T_001
        v4 = __lsx_vld(pSrc, 64);
        v5 = __lsx_vld(pSrc, 80);
        v6 = __lsx_vld(pSrc, 96);
        v7 = __lsx_vld(pSrc, 112);

        __lsx_vst(v0, pDst0, 0);  __lsx_vst(v4, pDst0, 16);
        __lsx_vst(v1, pDst1, 0);  __lsx_vst(v5, pDst1, 16);
        __lsx_vst(v2, pDst2, 0);  __lsx_vst(v6, pDst2, 16);
        __lsx_vst(v3, pDst3, 0);  __lsx_vst(v7, pDst3, 16);

        pSrc += 128;
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
static void etnaviv_resolve_supertile(uint8_t *pSrc,
                                      uint8_t *pDst,
                                      int dst_stride)
{
    int i, j;

    // each supertile have 4x8 groups
    for (j = 0; j < 4; ++j)
    {
        uint8_t *pDstGroup = pDst;
        uint8_t *pSrcGroup = pSrc;

        for (i = 0; i < 8; ++i)
        {
            // each group have 4x2 tiles
            lsx_resolve_4x2_tile(pSrcGroup, pDstGroup, dst_stride);
            // 512 byte (8 tiles)
            pSrcGroup += 8 * 4 * 4 * 4;
            // 32 byte (step 8 pixel)
            pDstGroup += 2 * 4 * 4;
        }

        pSrc += 64 * 64; /* byte */
        pDst += dst_stride * 16;
    }
}


/*
    from: 4x32 pixel

    000 001  002 003  004 005  006 007

    to: 16x8 pixel

    000 001
    002 003
    004 005
    006 007
*/
static void lsx_resolve_tail_tile_row(uint8_t *pSrc,
                                      uint8_t *pDst,
                                      int dst_stride,
                                      int remain_x)
{
    uint8_t *pDst0 = pDst;
    uint8_t *pDst1 = pDst0 + dst_stride;
    uint8_t *pDst2 = pDst1 + dst_stride;
    uint8_t *pDst3 = pDst2 + dst_stride;
    int dst_stride_x4 = dst_stride * 4;
    int j;

    for (j = 0; j < 4; ++j)
    {
        __m128i v0, v1, v2, v3;
        __m128i v4, v5, v6, v7;

        // T_000
        v0 = __lsx_vld(pSrc, 0);
        v1 = __lsx_vld(pSrc, 16);
        v2 = __lsx_vld(pSrc, 32);
        v3 = __lsx_vld(pSrc, 48);

        if (remain_x > 4)
        {
            // T_001
            v4 = __lsx_vld(pSrc, 64);
            v5 = __lsx_vld(pSrc, 80);
            v6 = __lsx_vld(pSrc, 96);
            v7 = __lsx_vld(pSrc, 112);
        }

        switch (remain_x)
        {
            case 1:
            {
                __lsx_vstelm_w(v0, pDst0, 0, 0);
                __lsx_vstelm_w(v1, pDst1, 0, 0);
                __lsx_vstelm_w(v2, pDst2, 0, 0);
                __lsx_vstelm_w(v3, pDst3, 0, 0);
            } break;

            case 2:
            {
                __lsx_vstelm_d(v0, pDst0, 0, 0);
                __lsx_vstelm_d(v1, pDst1, 0, 0);
                __lsx_vstelm_d(v2, pDst2, 0, 0);
                __lsx_vstelm_d(v3, pDst3, 0, 0);
            } break;

            case 3:
            {
                __lsx_vstelm_d(v0, pDst0, 0, 0); __lsx_vstelm_w(v0, pDst0, 8, 2);
                __lsx_vstelm_d(v1, pDst1, 0, 0); __lsx_vstelm_w(v1, pDst1, 8, 2);
                __lsx_vstelm_d(v2, pDst2, 0, 0); __lsx_vstelm_w(v2, pDst2, 8, 2);
                __lsx_vstelm_d(v3, pDst3, 0, 0); __lsx_vstelm_w(v3, pDst3, 8, 2);
            } break;

            case 4:
            {
                __lsx_vst(v0, pDst0, 0);
                __lsx_vst(v1, pDst1, 0);
                __lsx_vst(v2, pDst2, 0);
                __lsx_vst(v3, pDst3, 0);
            } break;

            case 5:
            {
                __lsx_vst(v0, pDst0, 0); __lsx_vstelm_w(v4, pDst0, 16, 0);
                __lsx_vst(v1, pDst1, 0); __lsx_vstelm_w(v5, pDst1, 16, 0);
                __lsx_vst(v2, pDst2, 0); __lsx_vstelm_w(v6, pDst2, 16, 0);
                __lsx_vst(v3, pDst3, 0); __lsx_vstelm_w(v7, pDst3, 16, 0);
            } break;

            case 6:
            {
                __lsx_vst(v0, pDst0, 0); __lsx_vstelm_d(v4, pDst0, 16, 0);
                __lsx_vst(v1, pDst1, 0); __lsx_vstelm_d(v5, pDst1, 16, 0);
                __lsx_vst(v2, pDst2, 0); __lsx_vstelm_d(v6, pDst2, 16, 0);
                __lsx_vst(v3, pDst3, 0); __lsx_vstelm_d(v7, pDst3, 16, 0);
            } break;

            case 7:
            {
                __lsx_vst(v0, pDst0, 0); __lsx_vstelm_d(v4, pDst0, 16, 0); __lsx_vstelm_w(v4, pDst0, 24, 2);
                __lsx_vst(v1, pDst1, 0); __lsx_vstelm_d(v5, pDst1, 16, 0); __lsx_vstelm_w(v5, pDst1, 24, 2);
                __lsx_vst(v2, pDst2, 0); __lsx_vstelm_d(v6, pDst2, 16, 0); __lsx_vstelm_w(v6, pDst2, 24, 2);
                __lsx_vst(v3, pDst3, 0); __lsx_vstelm_d(v7, pDst3, 16, 0); __lsx_vstelm_w(v7, pDst3, 24, 2);
            } break;

            default:
                return;
        }

        pSrc += 128;
        pDst0 += dst_stride_x4;
        pDst1 += dst_stride_x4;
        pDst2 += dst_stride_x4;
        pDst3 += dst_stride_x4;
    }
}



/* supertile is stored in continues row
 *
 * supertile : 64x64 pixel, each pixel is 4 byte, remain_x < 64
 */
static void etnaviv_resolve_supertile_row_tail(uint8_t *pSrc,
                                               uint8_t *pDst,
                                               int dst_stride,
                                               int remain_x)
{
    int j;
    int x;

    // each supertile have 4x8 groups
    for (j = 0; j < 4; ++j)
    {
        uint8_t *pDstGroup = pDst;
        uint8_t *pSrcGroup = pSrc;

        /* Currently, we store 8 pixel once a time */
        for (x = remain_x; x >= 8; x -= 8)
        {
            // each group have 4x2 tiles, each tile is 64 byte
            lsx_resolve_4x2_tile(pSrcGroup, pDstGroup, dst_stride);
            // 512 byte
            pSrcGroup += 8 * 64;
            // 32 byte
            pDstGroup += 8 * 4;
        }

        if (x)
        {
            /* remian_x less than 8 */
            lsx_resolve_tail_tile_row(pSrcGroup, pDstGroup, dst_stride, x);
        }

        pSrc += 64 * 64; /* byte */
        pDst += dst_stride * 16;
    }
}


/*
    from: 4x32 pixel

        000 001  002 003  004 005  006 007

    to: 16x8 pixel

        000 001
        002 003
        004 005
        006 007

    resolve bottom tile which less than one group, remain_y < 16
*/
static void lsx_resolve_tail_tile_col(uint8_t *pSrc,
                                      uint8_t *pDst,
                                      int dst_stride,
                                      int remain_y)
{

    uint8_t *pDst0 = pDst;
    uint8_t *pDst1 = pDst0 + dst_stride;
    uint8_t *pDst2 = pDst1 + dst_stride;
    uint8_t *pDst3 = pDst2 + dst_stride;
    int dst_stride_x4 = dst_stride * 4;

    for ( ; remain_y >= 4; remain_y -= 4)
    {
        __m128i v0, v1, v2, v3;
        __m128i v4, v5, v6, v7;

        // T_000
        v0 = __lsx_vld(pSrc, 0);
        v1 = __lsx_vld(pSrc, 16);
        v2 = __lsx_vld(pSrc, 32);
        v3 = __lsx_vld(pSrc, 48);

        // T_001
        v4 = __lsx_vld(pSrc, 64);
        v5 = __lsx_vld(pSrc, 80);
        v6 = __lsx_vld(pSrc, 96);
        v7 = __lsx_vld(pSrc, 112);

        __lsx_vst(v0, pDst0, 0); __lsx_vst(v4, pDst0, 16);
        __lsx_vst(v1, pDst1, 0); __lsx_vst(v5, pDst1, 16);
        __lsx_vst(v2, pDst2, 0); __lsx_vst(v6, pDst2, 16);
        __lsx_vst(v3, pDst3, 0); __lsx_vst(v7, pDst3, 16);

        pSrc += 128;
        pDst0 += dst_stride_x4;
        pDst1 += dst_stride_x4;
        pDst2 += dst_stride_x4;
        pDst3 += dst_stride_x4;
    }

    if (remain_y)
    {
        __m128i v0, v1, v2;
        __m128i v4, v5, v6;

        // T_000
        v0 = __lsx_vld(pSrc, 0);
        v1 = __lsx_vld(pSrc, 16);
        v2 = __lsx_vld(pSrc, 32);

        // T_001
        v4 = __lsx_vld(pSrc, 64);
        v5 = __lsx_vld(pSrc, 80);
        v6 = __lsx_vld(pSrc, 96);

        if (remain_y == 3)
        {
            __lsx_vst(v0, pDst0, 0); __lsx_vst(v4, pDst0, 16);
            __lsx_vst(v1, pDst1, 0); __lsx_vst(v5, pDst1, 16);
            __lsx_vst(v2, pDst2, 0); __lsx_vst(v6, pDst2, 16);
            return;
        }

        if (remain_y == 2)
        {
            __lsx_vst(v0, pDst0, 0); __lsx_vst(v4, pDst0, 16);
            __lsx_vst(v1, pDst1, 0); __lsx_vst(v5, pDst1, 16);
            return;
        }

        if (remain_y == 1)
        {
            __lsx_vst(v0, pDst0, 0); __lsx_vst(v4, pDst0, 16);
            return;
        }
    }
}

/*
 *    remain_y is less than 64 pixel
 */
static void etnaviv_resolve_supertile_col_tail(uint8_t *pSrc,
                                               uint8_t *pDst,
                                               int dst_stride,
                                               int remain_y)
{
    int i;
    uint8_t *pDstGroup;
    uint8_t *pSrcGroup;

    // each supertile have 4x8 groups
    while (remain_y >= 16)
    {
        pDstGroup = pDst;
        pSrcGroup = pSrc;

        for (i = 0; i < 8; ++i)
        {
            // each group have 4x2 tiles
            lsx_resolve_4x2_tile(pSrcGroup, pDstGroup, dst_stride);
            // 512 byte
            pSrcGroup += 4 * 2 * 4 * 4 * 4;
            // 32 byte
            pDstGroup += 2 * 4 * 4;
        }

        pSrc += 64 * 64; /* byte */
        pDst += dst_stride * 16;
        remain_y -= 16;
    }

    if (remain_y)
    {
        pDstGroup = pDst;
        pSrcGroup = pSrc;

        for (i = 0; i < 8; ++i)
        {
            // each group have 4x2 tiles
            lsx_resolve_tail_tile_col(pSrcGroup, pDstGroup, dst_stride, remain_y);
            // 512 byte
            pSrcGroup += 4 * 2 * 4 * 4 * 4;
            // 32 byte
            pDstGroup += 2 * 4 * 4;
        }
    }
}

static void lsx_resolve_4x2_tile_row_col_tail(uint8_t *pSrc,
                                              uint8_t *pDst,
                                              int dst_stride,
                                              int remain_x,
                                              int remain_y)
{
    uint8_t *pDst0 = pDst;
    uint8_t *pDst1 = pDst0 + dst_stride;
    uint8_t *pDst2 = pDst1 + dst_stride;
    uint8_t *pDst3 = pDst2 + dst_stride;
    int dst_stride_x4 = dst_stride * 4;
    int j;

    for (j = 0; j < 4; ++j)
    {
        __m128i v0, v1, v2, v3;
        __m128i v4, v5, v6, v7;

        // T_000
        v0 = __lsx_vld(pSrc, 0);
        v1 = __lsx_vld(pSrc, 16);
        v2 = __lsx_vld(pSrc, 32);
        v3 = __lsx_vld(pSrc, 48);

        if (remain_x > 4)
        {
            // T_001
            v4 = __lsx_vld(pSrc, 64);
            v5 = __lsx_vld(pSrc, 80);
            v6 = __lsx_vld(pSrc, 96);
            v7 = __lsx_vld(pSrc, 112);
        }

        if (remain_y == 0)
        {
            return;
        }
        else if (remain_y >= 4)
        {
            switch (remain_x)
            {
                case 1:
                {
                    __lsx_vstelm_w(v0, pDst0, 0, 0);
                    __lsx_vstelm_w(v1, pDst1, 0, 0);
                    __lsx_vstelm_w(v2, pDst2, 0, 0);
                    __lsx_vstelm_w(v3, pDst3, 0, 0);
                } break;

                case 2:
                {
                    __lsx_vstelm_d(v0, pDst0, 0, 0);
                    __lsx_vstelm_d(v1, pDst1, 0, 0);
                    __lsx_vstelm_d(v2, pDst2, 0, 0);
                    __lsx_vstelm_d(v3, pDst3, 0, 0);
                } break;

                case 3:
                {
                    __lsx_vstelm_d(v0, pDst0, 0, 0); __lsx_vstelm_w(v0, pDst0, 8, 2);
                    __lsx_vstelm_d(v1, pDst1, 0, 0); __lsx_vstelm_w(v1, pDst1, 8, 2);
                    __lsx_vstelm_d(v2, pDst2, 0, 0); __lsx_vstelm_w(v2, pDst2, 8, 2);
                    __lsx_vstelm_d(v3, pDst3, 0, 0); __lsx_vstelm_w(v3, pDst3, 8, 2);
                } break;

                case 4:
                {
                    __lsx_vst(v0, pDst0, 0);
                    __lsx_vst(v1, pDst1, 0);
                    __lsx_vst(v2, pDst2, 0);
                    __lsx_vst(v3, pDst3, 0);
                } break;

                case 5:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_w(v4, pDst0, 16, 0);
                    __lsx_vst(v1, pDst1, 0); __lsx_vstelm_w(v5, pDst1, 16, 0);
                    __lsx_vst(v2, pDst2, 0); __lsx_vstelm_w(v6, pDst2, 16, 0);
                    __lsx_vst(v3, pDst3, 0); __lsx_vstelm_w(v7, pDst3, 16, 0);
                } break;

                case 6:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_d(v4, pDst0, 16, 0);
                    __lsx_vst(v1, pDst1, 0); __lsx_vstelm_d(v5, pDst1, 16, 0);
                    __lsx_vst(v2, pDst2, 0); __lsx_vstelm_d(v6, pDst2, 16, 0);
                    __lsx_vst(v3, pDst3, 0); __lsx_vstelm_d(v7, pDst3, 16, 0);
                } break;

                case 7:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_d(v4, pDst0, 16, 0); __lsx_vstelm_w(v4, pDst0, 24, 2);
                    __lsx_vst(v1, pDst1, 0); __lsx_vstelm_d(v5, pDst1, 16, 0); __lsx_vstelm_w(v5, pDst1, 24, 2);
                    __lsx_vst(v2, pDst2, 0); __lsx_vstelm_d(v6, pDst2, 16, 0); __lsx_vstelm_w(v6, pDst2, 24, 2);
                    __lsx_vst(v3, pDst3, 0); __lsx_vstelm_d(v7, pDst3, 16, 0); __lsx_vstelm_w(v7, pDst3, 24, 2);
                } break;
            }
            remain_y -= 4;
        }
        else if (remain_y == 3)
        {
            switch (remain_x)
            {
                case 1:
                {
                    __lsx_vstelm_w(v0, pDst0, 0, 0);
                    __lsx_vstelm_w(v1, pDst1, 0, 0);
                    __lsx_vstelm_w(v2, pDst2, 0, 0);
                } break;

                case 2:
                {
                    __lsx_vstelm_d(v0, pDst0, 0, 0);
                    __lsx_vstelm_d(v1, pDst1, 0, 0);
                    __lsx_vstelm_d(v2, pDst2, 0, 0);
                } break;

                case 3:
                {
                    __lsx_vstelm_d(v0, pDst0, 0, 0); __lsx_vstelm_w(v0, pDst0, 8, 2);
                    __lsx_vstelm_d(v1, pDst1, 0, 0); __lsx_vstelm_w(v1, pDst1, 8, 2);
                    __lsx_vstelm_d(v2, pDst2, 0, 0); __lsx_vstelm_w(v2, pDst2, 8, 2);
                } break;

                case 4:
                {
                    __lsx_vst(v0, pDst0, 0);
                    __lsx_vst(v1, pDst1, 0);
                    __lsx_vst(v2, pDst2, 0);
                } break;

                case 5:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_w(v4, pDst0, 16, 0);
                    __lsx_vst(v1, pDst1, 0); __lsx_vstelm_w(v5, pDst1, 16, 0);
                    __lsx_vst(v2, pDst2, 0); __lsx_vstelm_w(v6, pDst2, 16, 0);
                } break;

                case 6:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_d(v4, pDst0, 16, 0);
                    __lsx_vst(v1, pDst1, 0); __lsx_vstelm_d(v5, pDst1, 16, 0);
                    __lsx_vst(v2, pDst2, 0); __lsx_vstelm_d(v6, pDst2, 16, 0);
                } break;

                case 7:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_d(v4, pDst0, 16, 0);  __lsx_vstelm_w(v4, pDst0, 24, 2);
                    __lsx_vst(v1, pDst1, 0); __lsx_vstelm_d(v5, pDst1, 16, 0);  __lsx_vstelm_w(v5, pDst1, 24, 2);
                    __lsx_vst(v2, pDst2, 0); __lsx_vstelm_d(v6, pDst2, 16, 0);  __lsx_vstelm_w(v6, pDst2, 24, 2);
                } break;
            }

            return;
        }
        else if (remain_y == 2)
        {
            switch (remain_x)
            {
                case 1:
                {
                    __lsx_vstelm_w(v0, pDst0, 0, 0);
                    __lsx_vstelm_w(v1, pDst1, 0, 0);
                } break;

                case 2:
                {
                    __lsx_vstelm_d(v0, pDst0, 0, 0);
                    __lsx_vstelm_d(v1, pDst1, 0, 0);
                } break;

                case 3:
                {
                    __lsx_vstelm_d(v0, pDst0, 0, 0); __lsx_vstelm_w(v0, pDst0, 8, 2);
                    __lsx_vstelm_d(v1, pDst1, 0, 0); __lsx_vstelm_w(v1, pDst1, 8, 2);
                } break;

                case 4:
                {
                    __lsx_vst(v0, pDst0, 0);
                    __lsx_vst(v1, pDst1, 0);
                } break;

                case 5:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_w(v4, pDst0, 16, 0);
                    __lsx_vst(v1, pDst1, 0); __lsx_vstelm_w(v5, pDst1, 16, 0);
                } break;

                case 6:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_d(v4, pDst0, 16, 0);
                    __lsx_vst(v1, pDst1, 0); __lsx_vstelm_d(v5, pDst1, 16, 0);
                } break;

                case 7:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_d(v4, pDst0, 16, 0);  __lsx_vstelm_w(v4, pDst0, 24, 2);
                    __lsx_vst(v1, pDst1, 0); __lsx_vstelm_d(v5, pDst1, 16, 0);  __lsx_vstelm_w(v5, pDst1, 24, 2);
                } break;
            }

            return;
        }
        else if (remain_y == 1)
        {
            switch (remain_x)
            {
                case 1:
                {
                    __lsx_vstelm_w(v0, pDst0, 0, 0);
                } break;

                case 2:
                {
                    __lsx_vstelm_d(v0, pDst0, 0, 0);
                } break;

                case 3:
                {
                    __lsx_vstelm_d(v0, pDst0, 0, 0); __lsx_vstelm_w(v0, pDst0, 8, 2);
                } break;

                case 4:
                {
                    __lsx_vst(v0, pDst0, 0);
                } break;

                case 5:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_w(v4, pDst0, 16, 0);
                } break;

                case 6:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_d(v4, pDst0, 16, 0);
                } break;

                case 7:
                {
                    __lsx_vst(v0, pDst0, 0); __lsx_vstelm_d(v4, pDst0, 16, 0); __lsx_vstelm_w(v4, pDst0, 24, 2);
                } break;
            }
            return;
        }

        pSrc += 128;
        pDst0 += dst_stride_x4;
        pDst1 += dst_stride_x4;
        pDst2 += dst_stride_x4;
        pDst3 += dst_stride_x4;
    }
}


static void etnaviv_resolve_supertile_row_col_tail(uint8_t *pSrc,
                                                   uint8_t *pDst,
                                                   int dst_stride,
                                                   int remain_x,
                                                   int remain_y)
{
    /* Currently, we store 8 pixel once a time */
    int M = remain_x / 8;
    int N = remain_x - M * 8;
    int i, j;

    // each supertile have 4x8 groups
    for (j = 0; j < 4; ++j)
    {
        uint8_t *pDstGroup = pDst;
        uint8_t *pSrcGroup = pSrc;
        if (remain_y >= 16)
        {
            for (i = 0; i < M; ++i)
            {
                // each group have 4x2 tiles
                lsx_resolve_4x2_tile(pSrcGroup, pDstGroup, dst_stride);
                // 512 byte
                pSrcGroup += 4 * 2 * 4 * 4 * 4;
                // 32 byte
                pDstGroup += 2 * 4 * 4;
            }

            if (N)
            {
                lsx_resolve_tail_tile_row(pSrcGroup, pDstGroup, dst_stride, N);
            }

            remain_y -= 16;
        }
        else
        {
            for (i = 0; i < M; ++i)
            {
                // each group have 4x2 tiles
                lsx_resolve_tail_tile_col(pSrcGroup, pDstGroup, dst_stride, remain_y);
                // 512 byte
                pSrcGroup += 4 * 2 * 4 * 4 * 4;
                // 32 byte
                pDstGroup += 2 * 4 * 4;
            }

            if (N)
            {
                lsx_resolve_4x2_tile_row_col_tail(pSrcGroup, pDstGroup, dst_stride, N, remain_y);
            }
            return;
        }
        pSrc += 64 * 64; /* byte */
        pDst += dst_stride * 16;
    }
}

/*
 * Vivante 64x64 super-tiling layout
 *
 * This is a tiled layout using 64x64 pixel super-tiles,
 * where each super-tile contains 8x4 groups of 2x4 tiles
 * of 4x4 pixels (like above) each, all in row-major layout.
 *
 * It appears that the blob always pads render buffers pixel
 * sizes to a multiple of 64, ie, a width of 400 becomes 448
 * and 800 becomes 832. This is because the render buffer is
 * also tiled, albeit differently than the 4x4 tiling format
 * of the textures. On a fine level, every tile is the same
 * as for normal tiled surfaces:
 *
 *  0  1   2  3
 *  4  5   6  7
 *  8  9  10  11
 *  12 13 14  15
 *
 * However, as the name 'supertiled' implies, the tiles themselves
 * are also tiled, to be specific in this pattern:
 *
 *  000 001  008 009  016 017  024 025  032 033  040 041  048 049  056 057
 *  002 003  010 011  018 019  026 027  034 035  042 043  050 051  058 059
 *  004 005  012 013  020 021  028 029  036 037  044 045  052 053  060 061
 *  006 007  014 015  022 023  030 031  038 039  046 047  054 055  062 063
 *
 *  064 065  072 073  080 081  088 089  096 097  104 105  112 113  120 121
 *  066 067  074 075  082 083  090 091  098 099  106 107  114 115  122 123
 *  068 069  076 077  084 085  092 093  100 101  108 109  116 117  124 125
 *  070 071  078 079  086 087  094 095  102 103  110 111  118 119  126 127
 *
 *  128 129  136 137  144 145  152 153  160 161  168 169  176 177  184 185
 *  130 131  138 139  146 147  154 155  162 163  170 171  178 179  186 187
 *  132 133  140 141  148 149  156 157  164 165  172 173  180 181  188 189
 *  134 135  142 143  150 151  158 159  166 167  174 175  182 183  190 191
 *
 *  192 193  200 201  208 209  216 217  224 225  232 233  240 241  248 249
 *  194 195  202 203  210 211  218 219  226 227  234 235  242 243  250 251
 *  196 197  204 205  212 213  220 221  228 229  236 237  244 245  252 253
 *  198 199  206 207  214 215  222 223  230 231  238 239  246 247  254 255
 *
 * This is one of the Vivante supertiling layouts. Every number is a tile
 * number in the supertile for the tile at that x,y.
 *
 * This has some similarity to a Z-order_curve, but is only nested one level,
 * in total this results in 64x64 sized tiles. The GPU can render to normal
 * tiled surfaces (such as used by textures) as well as supertiled surfaces.
 * However, rendering to supertiled surfaces is likely faster due to better
 * cache locality. Stride, as used for resolve operations, is for a row of
 * tiles not a row of pixels; 0x1c00 for width 448 (originally 400), 0x3400
 * for width 832 (originally 800).
 */

/* src_stride : num of pixels one row */
/* dst_stride : num of pixels one row */

Bool etnaviv_supertile_to_linear_lsx(uint32_t *src_bits,
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
    // width / 64; height / 64;
    int num_supertile_x = width >> 6;
    int num_supertile_y = height >> 6;
    int remain_x = width & 63;
    int remain_y = height & 63;
    int dst_stride_bytes = dst_stride * 4;
    int i, j;

    dst_bits += dst_stride * dst_y + dst_x;
    src_bits += src_stride * src_y + src_x;

    for (j = 0; j < num_supertile_y; j++)
    {
        uint8_t *pSrc = (uint8_t *)src_bits;
        uint8_t *pDst = (uint8_t *)dst_bits;

        for (i = 0; i < num_supertile_x; i++)
        {
            etnaviv_resolve_supertile(pSrc, pDst, dst_stride_bytes);
            pSrc += 64 * 64 * 4; /* byte */
            pDst += 64 * 4;
        }

        if (remain_x)
        {
            etnaviv_resolve_supertile_row_tail(pSrc, pDst, dst_stride_bytes, remain_x);
        }

        src_bits += src_stride * 64;
        dst_bits += dst_stride * 64;
    }

    if (remain_y)
    {
        uint8_t *pSrc = (uint8_t *)src_bits;
        uint8_t *pDst = (uint8_t *)dst_bits;

        for (i = 0; i < num_supertile_x; i++)
        {
            // 16 x 16 in pixel, 16 x 64 in bytes
            etnaviv_resolve_supertile_col_tail(pSrc, pDst, dst_stride_bytes, remain_y);

            pSrc += 64 * 64 * 4; /* byte */
            pDst += 64 * 4;
        }

        if (remain_x)
        {
            etnaviv_resolve_supertile_row_col_tail(pSrc, pDst, dst_stride_bytes, remain_x, remain_y);
        }
    }

    return TRUE;
}
