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


#ifndef LOONGSON_GLAMOR_H_
#define LOONGSON_GLAMOR_H_

#include <xf86str.h>

#include "drmmode_display.h"

Bool try_enable_glamor(ScrnInfoPtr pScrn);

Bool ls_glamor_create_gbm_bo(ScrnInfoPtr pScrn,
                             drmmode_bo *bo,
                             unsigned width,
                             unsigned height,
                             unsigned bpp);

Bool ls_glamor_init(ScrnInfoPtr pScrn);

Bool glamor_set_pixmap_bo(ScrnInfoPtr pScrn,
                          PixmapPtr pixmap,
                          drmmode_bo *bo);


uint32_t get_opaque_format(uint32_t format);

Bool ls_glamor_handle_new_screen_pixmap(ScrnInfoPtr pScrn,
                          struct DrmModeBO * const pFrontBO);
#endif
