/*
 * Copyright (C) 2007 Red Hat, Inc.
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
 *     Dave Airlie <airlied@redhat.com>
 *     Sui Jingfeng <suijingfeng@loongson.cn>
 */
#ifndef DUMB_BO_H
#define DUMB_BO_H

#include <stdint.h>

struct dumb_bo;

struct dumb_bo *dumb_bo_create(int fd,
                               unsigned int width,
                               unsigned int height,
                               unsigned int bpp);
int dumb_bo_map(int fd, struct dumb_bo * const bo);
void dumb_bo_unmap(struct dumb_bo * const bo);
int dumb_bo_destroy(int fd, struct dumb_bo * const bo);
uint32_t dumb_bo_pitch(struct dumb_bo * const bo);
uint32_t dumb_bo_handle(struct dumb_bo * const bo);
uint32_t dumb_bo_size(struct dumb_bo * const bo);
void *dumb_bo_cpu_addr(struct dumb_bo * const bo);

struct dumb_bo *dumb_get_bo_from_fd(int fd, int handle, int pitch, int size);

#endif
