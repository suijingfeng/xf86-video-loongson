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

#include <msa.h>
#include "etnaviv_resolve.h"


/*
    from: 4x32 pixel

    000 001  002 003  004 005  006 007

    to: 16x8 pixel

    000 001
    002 003
    004 005
    006 007

    remain_x is less than 8
*/
static void msa_resolve_tail_tile_row(uint32_t *pSrc,
                                      uint32_t *pDst,
                                      int dst_stride,
                                      int remain_x)
{
    uint32_t *pDst0 = pDst;
    uint32_t *pDst1 = pDst0 + dst_stride;
    uint32_t *pDst2 = pDst1 + dst_stride;
    uint32_t *pDst3 = pDst2 + dst_stride;
    int dst_stride_x4 = dst_stride * 4;
    int i, j;

    /* 000 001 */
    for (j = 0; j < 4; ++j)
    {
        uint32_t tmp[4][8];
        v4i32 v0, v1, v2, v3;
        v4i32 v4, v5, v6, v7;

        // T_000
        v0 = __msa_ld_w(pSrc, 0);
        v1 = __msa_ld_w(pSrc, 16);
        v2 = __msa_ld_w(pSrc, 32);
        v3 = __msa_ld_w(pSrc, 48);

        // T_001
        v4 = __msa_ld_w(pSrc, 64);
        v5 = __msa_ld_w(pSrc, 80);
        v6 = __msa_ld_w(pSrc, 96);
        v7 = __msa_ld_w(pSrc, 112);

        // save to temp buffer first
        __msa_st_w(v0, &tmp[0][0], 0); __msa_st_w(v4, &tmp[0][4], 0);
        __msa_st_w(v1, &tmp[1][0], 0); __msa_st_w(v5, &tmp[1][4], 0);
        __msa_st_w(v2, &tmp[2][0], 0); __msa_st_w(v6, &tmp[2][4], 0);
        __msa_st_w(v3, &tmp[3][0], 0); __msa_st_w(v7, &tmp[3][4], 0);

        for (i = 0; i < remain_x; ++i)
        {
            pDst0[i] = tmp[0][i];
            pDst1[i] = tmp[1][i];
            pDst2[i] = tmp[2][i];
            pDst3[i] = tmp[3][i];
        }

        // 2 tiles = 4x4x2 pixel
        pSrc += 32;
        pDst0 += dst_stride_x4;
        pDst1 += dst_stride_x4;
        pDst2 += dst_stride_x4;
        pDst3 += dst_stride_x4;
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
static void msa_resolve_tail_tile_col(uint32_t *pSrc,
                                      uint32_t *pDst,
                                      int dst_stride,
                                      int remain_y)
{

    uint32_t *pDst0 = pDst;
    uint32_t *pDst1 = pDst0 + dst_stride;
    uint32_t *pDst2 = pDst1 + dst_stride;
    uint32_t *pDst3 = pDst2 + dst_stride;
    int dst_stride_x4 = dst_stride * 4;

    v4i32 v0, v1, v2, v3;
    v4i32 v4, v5, v6, v7;

    while (remain_y >= 4)
    {
        // T_000
        v0 = __msa_ld_w(pSrc, 0);
        v1 = __msa_ld_w(pSrc, 16);
        v2 = __msa_ld_w(pSrc, 32);
        v3 = __msa_ld_w(pSrc, 48);

        // T_001
        v4 = __msa_ld_w(pSrc, 64);
        v5 = __msa_ld_w(pSrc, 80);
        v6 = __msa_ld_w(pSrc, 96);
        v7 = __msa_ld_w(pSrc, 112);

        __msa_st_w(v0, pDst0, 0); __msa_st_w(v4, pDst0, 16);
        __msa_st_w(v1, pDst1, 0); __msa_st_w(v5, pDst1, 16);
        __msa_st_w(v2, pDst2, 0); __msa_st_w(v6, pDst2, 16);
        __msa_st_w(v3, pDst3, 0); __msa_st_w(v7, pDst3, 16);

        // 2 tiles = 4x4x2 pixel
        pSrc += 32;
        pDst0 += dst_stride_x4;
        pDst1 += dst_stride_x4;
        pDst2 += dst_stride_x4;
        pDst3 += dst_stride_x4;
        remain_y -= 4;
    }

    if (remain_y == 3)
    {
        // T_000
        v0 = __msa_ld_w(pSrc, 0);
        v1 = __msa_ld_w(pSrc, 16);
        v2 = __msa_ld_w(pSrc, 32);

        // T_001
        v4 = __msa_ld_w(pSrc, 64);
        v5 = __msa_ld_w(pSrc, 80);
        v6 = __msa_ld_w(pSrc, 96);

        __msa_st_w(v0, pDst0, 0); __msa_st_w(v4, pDst0, 16);
        __msa_st_w(v1, pDst1, 0); __msa_st_w(v5, pDst1, 16);
        __msa_st_w(v2, pDst2, 0); __msa_st_w(v6, pDst2, 16);
        return;
    }

    if (remain_y == 2)
    {
        // T_000
        v0 = __msa_ld_w(pSrc, 0);
        v1 = __msa_ld_w(pSrc, 16);
        // T_001
        v4 = __msa_ld_w(pSrc, 64);
        v5 = __msa_ld_w(pSrc, 80);

        __msa_st_w(v0, pDst0, 0); __msa_st_w(v4, pDst0, 16);
        __msa_st_w(v1, pDst1, 0); __msa_st_w(v5, pDst1, 16);
        return;
    }

    if (remain_y == 1)
    {
        // T_000
        v0 = __msa_ld_w(pSrc, 0);
        // T_001
        v4 = __msa_ld_w(pSrc, 64);

        __msa_st_w(v0, pDst0, 0); __msa_st_w(v4, pDst0, 16);
        return;
    }
}

//////////////////////////////////////////////////////////////////////////////

/*
    from: 4x32 pixel

    000 001  002 003  004 005  006 007

    to: 16x8 pixel

    000 001
    002 003
    004 005
    006 007
*/
static void msa_resolve_4x2_tile(uint32_t *pSrc,
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
        v4i32 v0, v1, v2, v3;
        v4i32 v4, v5, v6, v7;

        // T_000
        v0 = __msa_ld_w(pSrc, 0);
        v1 = __msa_ld_w(pSrc, 16);
        v2 = __msa_ld_w(pSrc, 32);
        v3 = __msa_ld_w(pSrc, 48);

        // T_001
        v4 = __msa_ld_w(pSrc, 64);
        v5 = __msa_ld_w(pSrc, 80);
        v6 = __msa_ld_w(pSrc, 96);
        v7 = __msa_ld_w(pSrc, 112);

        __msa_st_w(v0, pDst0, 0);  __msa_st_w(v4, pDst0, 16);
        __msa_st_w(v1, pDst1, 0);  __msa_st_w(v5, pDst1, 16);
        __msa_st_w(v2, pDst2, 0);  __msa_st_w(v6, pDst2, 16);
        __msa_st_w(v3, pDst3, 0);  __msa_st_w(v7, pDst3, 16);

        pSrc += 32;    /* 32 pixel */
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
            msa_resolve_4x2_tile(pSrcGroup, pDstGroup, dst_stride);
            // 512 byte (8 tiles, each tile is 16 pixel)
            pSrcGroup += 8 * 16;
            // 32 byte (step 8 pixel)
            pDstGroup += 8;
        }

        pSrc += 64 * 16; /* 64x16 pixel */
        pDst += dst_stride * 16;
    }
}

/*
 * supertile is stored in continues row
 *
 * supertile : 64x64 pixel, each pixel is 4 byte
 * remain_x < 64
 */
static void etnaviv_resolve_supertile_row_tail(uint32_t *pSrc,
                                               uint32_t *pDst,
                                               int dst_stride,
                                               int remain_x)
{
    int j;

    // each supertile have 4x8 groups
    for (j = 0; j < 4; ++j)
    {
        uint32_t *pSrcGroup = pSrc;
        uint32_t *pDstGroup = pDst;
        int x = remain_x;

        // we store 8 pixel once a time
        while (x >= 8)
        {
            // each group have 4x2 tiles, each tile is 4x4 pixel
            msa_resolve_4x2_tile(pSrcGroup, pDstGroup, dst_stride);
            // 128 pixel, 512 byte
            pSrcGroup += 8 * 16;
            // step 8 pixel
            pDstGroup += 8;
            x -= 8;
        }

        // less than 8 pixel case
        if (x)
        {
            msa_resolve_tail_tile_row(pSrcGroup, pDstGroup, dst_stride, x);
        }

        pSrc += 64 * 16;
        pDst += dst_stride * 16;
    }
}


/*
 *    remain_y is less than 64 pixel
 */
static void etnaviv_resolve_supertile_col_tail(uint32_t *pSrc,
                                               uint32_t *pDst,
                                               int dst_stride,
                                               int remain_y)
{
    int i;
    uint32_t *pDstGroup;
    uint32_t *pSrcGroup;

    // each supertile have 4x8 groups
    while (remain_y >= 16)
    {
        pDstGroup = pDst;
        pSrcGroup = pSrc;

        for (i = 0; i < 8; ++i)
        {
            // each group have 4x2 tiles
            msa_resolve_4x2_tile(pSrcGroup, pDstGroup, dst_stride);
            // 512 byte (8 tiles, each tile is 16 pixel)
            pSrcGroup += 8 * 16;
            // 32 byte (step 8 pixel)
            pDstGroup += 8;
        }

        pSrc += 64 * 16; /* pixel */
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
            msa_resolve_tail_tile_col(pSrcGroup, pDstGroup, dst_stride, remain_y);
            // 512 byte (8 tiles, each tile is 16 pixel)
            pSrcGroup += 8 * 16;
            // 32 byte (step 8 pixel)
            pDstGroup += 8;
        }
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

// remain_x is less than 8
// remain_y is less than 16
*/
static void msa_resolve_tile_row_col_tail(uint32_t *pSrc,
                                          uint32_t *pDst,
                                          int dst_stride,
                                          int remain_x,
                                          int remain_y)
{
    uint32_t *pDst0 = pDst;
    uint32_t *pDst1 = pDst0 + dst_stride;
    uint32_t *pDst2 = pDst1 + dst_stride;
    uint32_t *pDst3 = pDst2 + dst_stride;
    int dst_stride_x4 = dst_stride * 4;
    uint32_t tmp[4][8];
    v4i32 v0, v1, v2, v3;
    v4i32 v4, v5, v6, v7;
    int i;

    while (remain_y >= 4)
    {
        // T_000
        v0 = __msa_ld_w(pSrc, 0);
        v1 = __msa_ld_w(pSrc, 16);
        v2 = __msa_ld_w(pSrc, 32);
        v3 = __msa_ld_w(pSrc, 48);

        // T_001
        v4 = __msa_ld_w(pSrc, 64);
        v5 = __msa_ld_w(pSrc, 80);
        v6 = __msa_ld_w(pSrc, 96);
        v7 = __msa_ld_w(pSrc, 112);

        // save to temp buffer first
        __msa_st_w(v0, &tmp[0][0], 0); __msa_st_w(v4, &tmp[0][4], 0);
        __msa_st_w(v1, &tmp[1][0], 0); __msa_st_w(v5, &tmp[1][4], 0);
        __msa_st_w(v2, &tmp[2][0], 0); __msa_st_w(v6, &tmp[2][4], 0);
        __msa_st_w(v3, &tmp[3][0], 0); __msa_st_w(v7, &tmp[3][4], 0);

        for (i = 0; i < remain_x; ++i)
        {
            pDst0[i] = tmp[0][i];
            pDst1[i] = tmp[1][i];
            pDst2[i] = tmp[2][i];
            pDst3[i] = tmp[3][i];
        }

        // step 2 tiles = 32 pixel
        pSrc += 32;
        pDst0 += dst_stride_x4;
        pDst1 += dst_stride_x4;
        pDst2 += dst_stride_x4;
        pDst3 += dst_stride_x4;
        remain_y -= 4;
    }

    if (remain_y == 3)
    {
        // T_000
        v0 = __msa_ld_w(pSrc, 0);
        v1 = __msa_ld_w(pSrc, 16);
        v2 = __msa_ld_w(pSrc, 32);

        // T_001
        v4 = __msa_ld_w(pSrc, 64);
        v5 = __msa_ld_w(pSrc, 80);
        v6 = __msa_ld_w(pSrc, 96);

        // save to temp buffer first
        __msa_st_w(v0, &tmp[0][0], 0); __msa_st_w(v4, &tmp[0][4], 0);
        __msa_st_w(v1, &tmp[1][0], 0); __msa_st_w(v5, &tmp[1][4], 0);
        __msa_st_w(v2, &tmp[2][0], 0); __msa_st_w(v6, &tmp[2][4], 0);

        for (i = 0; i < remain_x; ++i)
        {
            pDst0[i] = tmp[0][i];
            pDst1[i] = tmp[1][i];
            pDst2[i] = tmp[2][i];
        }

        return;
    }

    if (remain_y == 2)
    {
        // T_000
        v0 = __msa_ld_w(pSrc, 0);
        v1 = __msa_ld_w(pSrc, 16);

        // T_001
        v4 = __msa_ld_w(pSrc, 64);
        v5 = __msa_ld_w(pSrc, 80);

        // save to temp buffer first
        __msa_st_w(v0, &tmp[0][0], 0); __msa_st_w(v4, &tmp[0][4], 0);
        __msa_st_w(v1, &tmp[1][0], 0); __msa_st_w(v5, &tmp[1][4], 0);

        for (i = 0; i < remain_x; ++i)
        {
            pDst0[i] = tmp[0][i];
            pDst1[i] = tmp[1][i];
        }

        return;
    }

    if (remain_y == 1)
    {
        // T_000
        v0 = __msa_ld_w(pSrc, 0);
        // T_001
        v4 = __msa_ld_w(pSrc, 64);

        // save to temp buffer first
        __msa_st_w(v0, &tmp[0][0], 0); __msa_st_w(v4, &tmp[0][4], 0);

        for (i = 0; i < remain_x; ++i)
        {
            pDst0[i] = tmp[0][i];
        }

        return;
    }
}


/*
 *    remain_x is less than 64 pixel
 *    remain_y is less than 64 pixel
 */
static void etnaviv_resolve_supertile_row_col_tail(uint32_t *pSrc,
                                                   uint32_t *pDst,
                                                   int dst_stride,
                                                   int remain_x,
                                                   int remain_y)
{
    /* Currently, we store 8 pixel once a time */
    int M = remain_x / 8;
    remain_x -= M * 8;

    // each supertile have 4x8 groups
    while (remain_y >= 16)
    {
        uint32_t *pDstGroup = pDst;
        uint32_t *pSrcGroup = pSrc;
        int i;

        for (i = 0; i < M; ++i)
        {
            // each group have 4x2 tiles
            msa_resolve_4x2_tile(pSrcGroup, pDstGroup, dst_stride);
            // 512 byte (8 tiles, each tile is 16 pixel)
            pSrcGroup += 8 * 16;
            // 32 byte
            pDstGroup += 8;
        }

        // remain_x < 8
        if (remain_x)
        {
            msa_resolve_tail_tile_row(pSrcGroup, pDstGroup, dst_stride, remain_x);
        }

        pSrc += 64 * 16; /* pixel */
        pDst += dst_stride * 16;
        remain_y -= 16;
    }

    // remain_y is less than 16
    if (remain_y)
    {
        uint32_t *pDstGroup = pDst;
        uint32_t *pSrcGroup = pSrc;
        int i;

        for (i = 0; i < M; ++i)
        {
            // each group have 4x2 tiles
            msa_resolve_tail_tile_col(pSrcGroup, pDstGroup, dst_stride, remain_y);
            // 512 byte (8 tiles, each tile is 16 pixel)
            pSrcGroup += 8 * 16;
            // 32 byte
            pDstGroup += 8;
        }

        // remain_x is less than 8
        if (remain_x)
        {
            msa_resolve_tile_row_col_tail(pSrcGroup,
                                          pDstGroup,
                                          dst_stride,
                                          remain_x,
                                          remain_y);
        }
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
Bool etnaviv_supertile_to_linear_msa(uint32_t *src_bits,
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
            etnaviv_resolve_supertile_row_tail(pSrc, pDst, dst_stride, remain_x);
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
            etnaviv_resolve_supertile_col_tail(pSrc, pDst, dst_stride, remain_y);

            // step 64x64 pixel
            pSrc += 64 * 64;
            // step 64 pxiel in row derection
            pDst += 64;
        }

        // remain_x < 64
        if (remain_x)
        {
            etnaviv_resolve_supertile_row_col_tail(pSrc, pDst, dst_stride,
                                                   remain_x, remain_y);
        }
    }

    return TRUE;
}
