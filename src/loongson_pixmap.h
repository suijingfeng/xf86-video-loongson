/*
 * Copyright (C) 2021 Loongson Corporation
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

#ifndef LOONGSON_PIXMAP_H_
#define LOONGSON_PIXMAP_H_

/*
 * A pixmap is a three-dimensional array of bits stored somewhere offscreen,
 * rather than in the visible portion of the screen’s display frame buffer.
 * It can be used as a source or destination in graphics operations.
 * There is no implied interpretation of the pixel values in a pixmap,
 * because it has no associated visual or colormap. There is only a depth
 * that indicates the number of significant bits per pixel. Also, there is
 * no implied physical size for each pixel; all graphic units are in numbers
 * of pixels. Therefore, a pixmap alone does not constitute a complete image;
 * it represents only a rectangular array of pixel values.
 */

#define CREATE_PIXMAP_USAGE_SCANOUT 0x80000000
#define CREATE_PIXMAP_USAGE_DRI3    0x40000000

#define LOONGSON_ALIGN(x,bytes)     (((x) + ((bytes) - 1)) & ~((bytes) - 1))
#define LOONGSON_DUMB_BO_ALIGN      256

#define GSGPU_SURF_MODE_MASK        0x03

enum gsgpu_surf_mode
{
    GSGPU_SURF_MODE_LINEAR = 1,
    GSGPU_SURF_MODE_TILED4 = 2,
    GSGPU_SURF_MODE_TILED8 = 3,
};

struct drmmode_fb
{
    int refcnt;
    /* fb_id get from the kernel */
    uint32_t id;
};

struct exa_pixmap_priv {
    struct dumb_bo *bo;
    struct etna_bo *etna_bo;
    struct gsgpu_bo *gbo;
    /* CPU side local buffer backing by malloc */
    struct LoongsonBuf *pBuf;

    struct drmmode_fb *fb;

    uint64_t tiling_info;

    /* GEM handle for pixmaps shared via DRI2/3 */
    int fd;
    int ref_count;
    int usage_hint;
    unsigned int pitch;
    uint16_t width;
    uint16_t height;

    /* rename to ref_count ? */
    Bool is_dumb;
    Bool is_gtt;
    Bool is_mapped;
};

/* OUTPUT SLAVE SUPPORT */
typedef struct LoongsonPixmapPriv
{
    uint32_t fb_id;
    /* if this pixmap is backed by a dumb bo */
    struct dumb_bo *backing_bo;
    /* OUTPUT SLAVE SUPPORT */
    DamagePtr slave_damage;

    /** Sink fields for flipping shared pixmaps */
    int flip_seq; /* seq of current page flip event handler */
    Bool wait_for_damage; /* if we have requested damage notification from source */

    /** Source fields for flipping shared pixmaps */
    Bool defer_dirty_update; /* if we want to manually update */
    PixmapDirtyUpdatePtr dirty; /* cached dirty ent to avoid searching list */
    DrawablePtr slave_src; /* if we exported shared pixmap, dirty tracking src */
    Bool notify_on_damage; /* if sink has requested damage notification */
} msPixmapPrivRec, *msPixmapPrivPtr;

void *LS_CreateExaPixmap(ScreenPtr pScreen,
        int width, int height, int depth,
        int usage_hint, int bitsPerPixel,
        int *new_fb_pitch);

void LS_DestroyExaPixmap(ScreenPtr pScreen, void *driverPriv);


void *LS_CreateDumbPixmap(ScreenPtr pScreen,
        int width, int height, int depth,
        int usage_hint, int bitsPerPixel,
        int *new_fb_pitch);

void LS_DestroyDumbPixmap(ScreenPtr pScreen, void *driverPriv);

PixmapPtr loongson_pixmap_create_header(ScreenPtr pScreen,
        int width, int height, int depth, int bitsPerPixel,
        int devKind, void *pPixData);

uint64_t loongson_pixmap_get_tiling_info(PixmapPtr pPixmap);
#endif
