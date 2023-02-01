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


#ifndef GSGPU_BO_HELPER_H_
#define GSGPU_BO_HELPER_H_

#ifdef HAVE_LIBDRM_GSGPU

#include <gsgpu_drm.h>
#include <gsgpu.h>

struct gsgpu_bo *gsgpu_bo_create(struct gsgpu_device *gdev,
                                 uint32_t alloc_size,
                                 uint32_t phys_alignment,
                                 uint32_t domains);

struct gsgpu_bo *gsgpu_get_pixmap_bo(PixmapPtr pPix);

Bool gsgpu_set_pixmap_bo(ScrnInfoPtr pScrn,
                         PixmapPtr pPixmap,
                         struct gsgpu_bo *gbo,
                         int prime_fd);

#endif

#endif
