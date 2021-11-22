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

#include "driver.h"
#include "loongson_buffer.h"


void LS_AllocBuf(int width, int height,
        int depth, int bpp, int usage_hint, struct LoongsonBuf *pBuf)
{
    //
    // depth and bpp is useless
    //
    unsigned int pitch = ((width * bpp + FB_MASK) >> FB_SHIFT) * sizeof(FbBits);
    unsigned int size;
    // suijingfeng: make sure this align value reasonable
    pitch = (pitch + 15) & ~(15);
    size = pitch * height;

    pBuf->pDat = malloc(size);
    pBuf->pitch = pitch;
    pBuf->size = size;
    pBuf->width = width;
    pBuf->height = height;
}


void LS_FreeBuf(struct LoongsonBuf *pBuf)
{
    //	NULL_DBG_MSG("FreeBuf pDat:%p", pBuf->pDat);
    if (pBuf->pDat != NULL)
    {
        free(pBuf->pDat);

        pBuf->pDat = NULL;
        pBuf->pitch = 0;
        pBuf->size = 0;
        pBuf->width = 0;
        pBuf->height = 0;
    }
}
