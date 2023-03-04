/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright (C) 2022 Loongson Corporation
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 * Authors: Alan Hourihane <alanh@tungstengraphics.com>
 *          Sui Jingfeng <suijingfeng@loongson.cn>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <drm.h>
#include <xf86drm.h>
#include <xf86Crtc.h>
#include <damage.h>
#include <shadow.h>

#if HAVE_LIBDRM_ETNAVIV
#include "etnaviv_device.h"
#endif

#ifdef GLAMOR_HAS_GBM
#define GLAMOR_FOR_XORG 1
#include <glamor.h>
#include <gbm.h>
#endif

#include "drmmode_display.h"

struct LoongsonRec {
    int fd;

    unsigned int vendor_id;
    unsigned int device_id;
    unsigned int revision;

    EntityInfoPtr pEnt;
    struct pci_device *PciInfo;

#if HAVE_LIBDRM_ETNAVIV
    struct EtnavivRec etnaviv;
#endif

#if HAVE_LIBDRM_GSGPU
    struct gsgpu_device *gsgpu;
#endif

    Bool is_gsgpu;
    Bool is_lsdc;
    Bool is_loongson_drm;
    Bool is_loongson;
    Bool is_prime_supported;
    Bool has_etnaviv;

    CloseScreenProcPtr CloseScreen;
    CreateWindowProcPtr CreateWindow;

    CreateScreenResourcesProcPtr createScreenResources;
    ScreenBlockHandlerProcPtr BlockHandler;
    miPointerSpriteFuncPtr SpriteFuncs;
    void *driver;
    char *render_node;
    struct drmmode_rec drmmode;

    drmEventContext event_context;

    /**
     * Page flipping stuff.
     *  @{
     */
    Bool atomic_modeset;
    Bool pending_modeset;
    /** @} */

    DamagePtr damage;
    Bool dirty_enabled;
    Bool shadow_present;

    uint32_t cursor_width, cursor_height;

    Bool has_queue_sequence;
    Bool tried_queue_sequence;

    Bool kms_has_modifiers;


    /* EXA API */
    ExaDriverPtr exaDrvPtr;

    /* shadow API */
    struct ShadowAPI {
        Bool (*Setup)(ScreenPtr);
        Bool (*Add)(ScreenPtr, PixmapPtr, ShadowUpdateProc, ShadowWindowProc, int, void *);
        void (*Remove)(ScreenPtr, PixmapPtr);
        void (*Update32to24)(ScreenPtr, shadowBufPtr);
        void (*UpdatePacked)(ScreenPtr, shadowBufPtr);
        void (*Update32)(ScreenPtr, PixmapPtr pShadow, RegionPtr pDamage);
    } shadow;

#ifdef GLAMOR_HAS_GBM
    /* glamor API */
    struct GlamorAPI {
        Bool (*back_pixmap_from_fd)(PixmapPtr, int, CARD16, CARD16, CARD16,
                                    CARD8, CARD8);
        void (*block_handler)(ScreenPtr);
        void (*clear_pixmap)(PixmapPtr);
        Bool (*egl_create_textured_pixmap)(PixmapPtr, int, int);
        Bool (*egl_create_textured_pixmap_from_gbm_bo)(PixmapPtr,
                                                       struct gbm_bo *,
                                                       Bool);
        void (*egl_exchange_buffers)(PixmapPtr, PixmapPtr);
        struct gbm_device *(*egl_get_gbm_device)(ScreenPtr);
        Bool (*egl_init)(ScrnInfoPtr, int);
        void (*finish)(ScreenPtr);
        struct gbm_bo *(*gbm_bo_from_pixmap)(ScreenPtr, PixmapPtr);
        Bool (*init)(ScreenPtr, unsigned int);
        int (*name_from_pixmap)(PixmapPtr, CARD16 *, CARD32 *);
        void (*set_drawable_modifiers_func)(ScreenPtr,
                                            GetDrawableModifiersFuncPtr);
        int (*shareable_fd_from_pixmap)(ScreenPtr, PixmapPtr, CARD16 *,
                                        CARD32 *);
        Bool (*supports_pixmap_import_export)(ScreenPtr);
        XF86VideoAdaptorPtr (*xv_init)(ScreenPtr, int);
        const char *(*egl_get_driver_name)(ScreenPtr);
    } glamor;
#endif
};


typedef struct LoongsonRec  loongsonRec;
typedef struct LoongsonRec *loongsonPtr;

#define loongsonPTR(p)    ((loongsonPtr)((p)->driverPrivate))

xf86CrtcPtr ms_dri2_crtc_covering_drawable(DrawablePtr pDraw);

int ms_get_crtc_ust_msc(xf86CrtcPtr crtc, CARD64 *ust, CARD64 *msc);

uint64_t ms_kernel_msc_to_crtc_msc(xf86CrtcPtr crtc, uint64_t sequence, Bool is64bit);

Bool ms_vblank_screen_init(ScreenPtr screen);
void ms_vblank_close_screen(ScreenPtr screen);

Bool ms_present_screen_init(ScreenPtr screen);

int ms_flush_drm_events(ScreenPtr screen);


void LS_SetupScrnHooks(ScrnInfoPtr scrn, Bool (* pFnProbe)(DriverPtr, int));
