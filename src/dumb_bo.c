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
 *    Dave Airlie <airlied@redhat.com>
 *    Sui Jingfeng <suijingfeng@loongson.cn>
 */

#ifdef HAVE_DIX_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>

#include "dumb_bo.h"

struct dumb_bo
{
    uint32_t handle;
    uint32_t size;
    void *ptr;
    uint32_t pitch;
};

struct dumb_bo *dumb_bo_create(int fd,
                               unsigned int width,
                               unsigned int height,
                               unsigned int bpp)
{
    struct drm_mode_create_dumb arg;
    struct dumb_bo *bo;
    int ret;

    bo = calloc(1, sizeof(*bo));
    if (bo == NULL)
    {
        return NULL;
    }

    memset(&arg, 0, sizeof(arg));
    arg.width = width;
    arg.height = height;
    arg.bpp = bpp;

    ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &arg);
    if (ret)
    {
        free(bo);
        return NULL;
    }

    bo->handle = arg.handle;
    bo->size = arg.size;
    bo->pitch = arg.pitch;

    return bo;
}


int dumb_bo_map(int fd, struct dumb_bo * const bo)
{
    struct drm_mode_map_dumb arg;
    int ret;
    void *map;

    if (bo->ptr)
    {
        return 0;
    }

    memset(&arg, 0, sizeof(arg));
    arg.handle = bo->handle;

    ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &arg);
    if (ret)
    {
        return ret;
    }

    map = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, arg.offset);
    if (map == MAP_FAILED)
    {
        return -errno;
    }

    bo->ptr = map;
    return 0;
}

void dumb_bo_unmap(struct dumb_bo * const bo)
{
    if (bo->ptr)
    {
        munmap(bo->ptr, bo->size);
        bo->ptr = NULL;
    }
}

int dumb_bo_destroy(int fd, struct dumb_bo * const bo)
{
    struct drm_mode_destroy_dumb arg;
    int ret;

    if (bo->ptr)
    {
        munmap(bo->ptr, bo->size);
        bo->ptr = NULL;
    }

    memset(&arg, 0, sizeof(arg));
    arg.handle = bo->handle;
    ret = drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &arg);
    if (ret)
    {
        return -errno;
    }

    free(bo);
    return 0;
}

uint32_t dumb_bo_pitch(struct dumb_bo * const bo)
{
    return bo->pitch;
}

void *dumb_bo_cpu_addr(struct dumb_bo * const bo)
{
    return bo->ptr;
}

uint32_t dumb_bo_handle(struct dumb_bo * const bo)
{
    return bo->handle;
}

uint32_t dumb_bo_size(struct dumb_bo * const bo)
{
    return bo->size;
}

/* OUTPUT SLAVE SUPPORT */
struct dumb_bo *dumb_get_bo_from_fd(int fd, int handle, int pitch, int size)
{
    struct dumb_bo *bo;
    int ret;

    bo = calloc(1, sizeof(*bo));
    if (bo == NULL)
    {
        return NULL;
    }

    ret = drmPrimeFDToHandle(fd, handle, &bo->handle);
    if (ret)
    {
        free(bo);
        return NULL;
    }

    bo->pitch = pitch;
    bo->size = size;

    return bo;
}
