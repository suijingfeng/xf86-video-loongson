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

#ifndef LOONGSON_PRIME_H_
#define LOONGSON_PRIME_H_

#include <xf86drmMode.h>
#include <xf86str.h>
#include <xf86Crtc.h>
#include <damage.h>

#include "drmmode_display.h"

/* OUTPUT SLAVE SUPPORT */
typedef struct _msPixmapPriv {
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


void *drmmode_map_slave_bo(drmmode_ptr drmmode, msPixmapPrivPtr ppriv);

Bool LS_SharePixmapBacking(PixmapPtr pPix, ScreenPtr slave, void **handle);
Bool LS_SetSharedPixmapBacking(PixmapPtr pPix, void *fd_handle);
void LS_DispatchSlaveDirty(ScreenPtr pScreen);
void LS_DispatchDirty(ScreenPtr pScreen);

#endif
