/*
 * Copyright (C) 2020 Loongson Corporation
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
#ifndef LOONGSON_SCANOUT_H_
#define LOONGSON_SCANOUT_H_

#include <xf86str.h>

struct DrmModeBO *
LS_CreateFrontBO(ScrnInfoPtr pScrn,
                 int drm_fd,
                 int width,
                 int height,
                 int bpp);

void *LS_MapFrontBO(ScrnInfoPtr pScrn,
                    int drm_fd,
                    struct DrmModeBO *pFrontBO);

void LS_FreeFrontBO(ScrnInfoPtr pScrn,
                    int drm_fd,
                    uint32_t fb_id,
                    struct DrmModeBO *pFBO);

Bool loongson_crtc_get_fb_id(xf86CrtcPtr crtc,
                             uint32_t *fb_id,
                             int *x,
                             int *y);

Bool loongson_pixmap_get_fb_id(PixmapPtr pPixmap, uint32_t *fb_id);

Bool loongson_create_scanout_pixmap(ScrnInfoPtr pScrn,
                                    int width,
                                    int height,
                                    PixmapPtr *ppScanoutPix);

int drmmode_bo_import(drmmode_ptr drmmode,
                      drmmode_bo *bo,
                      uint32_t *fb_id);

uint32_t drmmode_bo_get_pitch(drmmode_bo *bo);
uint32_t drmmode_bo_get_handle(drmmode_bo *bo);
void *drmmode_bo_get_cpu_addr(drmmode_bo *bo);

int drmmode_bo_destroy(drmmode_ptr drmmode, drmmode_bo *bo);

#endif
