/*
 * Copyright © 2021 Loongson Corporation
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

struct ms_exa_pixmap_priv {
    struct dumb_bo *bo;
    int fd;
    int pitch;
    Bool owned;
    Bool is_dumb;
    struct LoongsonBuf *pBuf;
    int usage_hint;
};


Bool LS_IsDumbPixmap(int usage_hint);

void * LS_CreateExaPixmap(ScreenPtr pScreen,
        int width, int height, int depth,
        int usage_hint, int bitsPerPixel,
        int *new_fb_pitch);

void LS_DestroyExaPixmap(ScreenPtr pScreen, void *driverPriv);


void * LS_CreateDumbPixmap(ScreenPtr pScreen,
        int width, int height, int depth,
        int usage_hint, int bitsPerPixel,
        int *new_fb_pitch);

void LS_DestroyDumbPixmap(ScreenPtr pScreen, void *driverPriv);

PixmapPtr drmmode_create_pixmap_header(ScreenPtr pScreen,
        int width, int height, int depth, int bitsPerPixel,
        int devKind, void *pPixData);

/*

Bool LS_ModifyDumbPixmapHeader( PixmapPtr pPixmap,
        int width, int height, int depth, int bitsPerPixel,
        int devKind, pointer pPixData );

Bool LS_ModifyExaPixmapHeader( PixmapPtr pPixmap,
        int width, int height, int depth,
        int bitsPerPixel, int devKind, pointer pPixData );

*/

#endif
