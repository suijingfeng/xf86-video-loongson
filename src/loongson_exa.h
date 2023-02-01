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


#ifndef LOONGSON_EXA_H_
#define LOONGSON_EXA_H_

#include "dumb_bo.h"

enum ExaAccelType {
    EXA_ACCEL_TYPE_NONE = 0,
    EXA_ACCEL_TYPE_FAKE = 1,
    EXA_ACCEL_TYPE_SOFTWARE = 2,
    EXA_ACCEL_TYPE_VIVANTE = 3,
    EXA_ACCEL_TYPE_ETNAVIV = 4,
    EXA_ACCEL_TYPE_GSGPU = 5
};

struct ms_exa_prepare_args {
    struct {
        int alu;
        Pixel planemask;
        Pixel fg;
    } solid;

    struct {
        PixmapPtr pSrcPixmap;
        int alu;
        Pixel planemask;
    } copy;

    struct {
        int op;
        PicturePtr pSrcPicture;
        PicturePtr pMaskPicture;
        PicturePtr pDstPicture;
        PixmapPtr pSrc;
        PixmapPtr pMask;
        PixmapPtr pDst;

        int rotate;
        Bool reflect_y;
    } composite;
};

struct DrmModeBO {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    struct dumb_bo *dumb;
    struct gsgpu_bo *gbo;
#ifdef GLAMOR_HAS_GBM
    Bool used_modifiers;
    struct gbm_bo *gbm;
#endif
};

typedef struct DrmModeBO drmmode_bo;

Bool LS_InitExaLayer(ScreenPtr pScreen);
Bool LS_DestroyExaLayer(ScreenPtr pScreen);

Bool try_enable_exa(ScrnInfoPtr pScrn);

struct dumb_bo *dumb_bo_from_pixmap(ScreenPtr screen,
                                    PixmapPtr pixmap);

Bool loongson_set_pixmap_dumb_bo(ScrnInfoPtr pScrn,
                                 PixmapPtr pPixmap,
                                 struct dumb_bo *dumb,
                                 int usage_hint,
                                 int prime_fd);

int loongson_exa_shareable_fd_from_pixmap(ScreenPtr pScreen,
                                          PixmapPtr pixmap,
                                          CARD16 *stride,
                                          CARD32 *size);

void ms_exa_exchange_buffers(PixmapPtr front, PixmapPtr back);
void print_pixmap_info(PixmapPtr pPixmap);

#endif
