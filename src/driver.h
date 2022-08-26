/*
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
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


#ifdef GLAMOR_HAS_GBM
#define GLAMOR_FOR_XORG 1
#include <glamor.h>
#include <gbm.h>
#endif

#include "drmmode_display.h"
#include "etnaviv_drmif.h"

/* Enum with indices for each of the feature words */
enum viv_features_word {
   viv_chipFeatures = 0,
   viv_chipMinorFeatures0 = 1,
   viv_chipMinorFeatures1 = 2,
   viv_chipMinorFeatures2 = 3,
   viv_chipMinorFeatures3 = 4,
   viv_chipMinorFeatures4 = 5,
   viv_chipMinorFeatures5 = 6,
   VIV_FEATURES_WORD_COUNT /* Must be last */
};

struct EtnavivRec {
    int fd;
    char *render_node;
    struct etna_device *dev;
    struct etna_gpu *gpu;
    struct etna_pipe *pipe;
    struct etna_cmd_stream *stream;
    struct etna_bo *bo;

    uint32_t model;
    uint32_t revision;
    uint32_t features[VIV_FEATURES_WORD_COUNT];
};

struct LoongsonRec {
    int fd;

    int Chipset;
    EntityInfoPtr pEnt;

    Bool noAccel;
    CloseScreenProcPtr CloseScreen;
    CreateWindowProcPtr CreateWindow;

    CreateScreenResourcesProcPtr createScreenResources;
    ScreenBlockHandlerProcPtr BlockHandler;
    miPointerSpriteFuncPtr SpriteFuncs;
    void *driver;

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

    uint32_t cursor_width, cursor_height;

    Bool has_queue_sequence;
    Bool tried_queue_sequence;

    Bool kms_has_modifiers;


    /* EXA API */
    ExaDriverPtr exaDrvPtr;
    struct EtnavivRec etna;

    /* shadow API */
    struct ShadowAPI {
        Bool (*Setup)(ScreenPtr);
        Bool (*Add)(ScreenPtr, PixmapPtr, ShadowUpdateProc, ShadowWindowProc, int, void *);
        void (*Remove)(ScreenPtr, PixmapPtr);
        void (*Update32to24)(ScreenPtr, shadowBufPtr);
        void (*UpdatePacked)(ScreenPtr, shadowBufPtr);
        void (*Update32)(ScreenPtr, shadowBufPtr);
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


Bool ms_dri2_screen_init(ScreenPtr screen);
void ms_dri2_close_screen(ScreenPtr screen);

Bool ms_vblank_screen_init(ScreenPtr screen);
void ms_vblank_close_screen(ScreenPtr screen);

Bool ms_present_screen_init(ScreenPtr screen);


int ms_flush_drm_events(ScreenPtr screen);


void LS_SetupScrnHooks(ScrnInfoPtr scrn, Bool (* pFnProbe)(DriverPtr, int));
