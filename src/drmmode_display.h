/*
 * Copyright © 2007 Red Hat, Inc.
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
 *
 */
#ifndef __DRMMODE_DISPLAY_H__
#define __DRMMODE_DISPLAY_H__

#include <xf86drmMode.h>
#ifdef HAVE_LIBUDEV
#include <libudev.h>
#endif

#include <xf86str.h>
#include <xf86Crtc.h>
#include <xf86drm.h>

#include <exa.h>
#include "dumb_bo.h"
#include "loongson_exa.h"

struct gbm_device;

enum drmmode_plane_property {
    DRMMODE_PLANE_TYPE = 0,
    DRMMODE_PLANE_FB_ID,
    DRMMODE_PLANE_IN_FORMATS,
    DRMMODE_PLANE_CRTC_ID,
    DRMMODE_PLANE_SRC_X,
    DRMMODE_PLANE_SRC_Y,
    DRMMODE_PLANE_SRC_W,
    DRMMODE_PLANE_SRC_H,
    DRMMODE_PLANE_CRTC_X,
    DRMMODE_PLANE_CRTC_Y,
    DRMMODE_PLANE_CRTC_W,
    DRMMODE_PLANE_CRTC_H,
    DRMMODE_PLANE__COUNT
};

enum drmmode_plane_type {
    DRMMODE_PLANE_TYPE_PRIMARY = 0,
    DRMMODE_PLANE_TYPE_CURSOR,
    DRMMODE_PLANE_TYPE_OVERLAY,
    DRMMODE_PLANE_TYPE__COUNT
};

enum drmmode_connector_property {
    DRMMODE_CONNECTOR_CRTC_ID,
    DRMMODE_CONNECTOR__COUNT
};

enum drmmode_crtc_property {
    DRMMODE_CRTC_ACTIVE,
    DRMMODE_CRTC_MODE_ID,
    DRMMODE_CRTC__COUNT
};

struct DrmModeBO {
    uint32_t width;
    uint32_t height;
    struct dumb_bo *dumb;
#ifdef GLAMOR_HAS_GBM
    Bool used_modifiers;
    struct gbm_bo *gbm;
#endif
};

typedef struct DrmModeBO drmmode_bo;

struct drmmode_rec {
    int fd;
    unsigned fb_id;
    drmModeFBPtr mode_fb;
    int cpp;
    int kbpp;
    ScrnInfoPtr scrn;

    struct gbm_device *gbm;

#ifdef HAVE_LIBUDEV
    struct udev_monitor *uevent_monitor;
    InputHandlerProc uevent_handler;
#endif
    drmEventContext event_context;
    drmmode_bo front_bo;
    Bool sw_cursor;

    /* Broken-out options. */
    OptionInfoPtr Options;

    Bool glamor_enabled;
    Bool exa_enabled;
    enum ExaAccelType exa_acc_type;
    Bool shadow_enable;
    Bool shadow_enable2;


    /** Is Option "PageFlip" enabled? */
    Bool pageflip;
    void *shadow_fb;
    void *shadow_fb2;
    /* SCREEN SPECIFIC_PRIVATE_KEYS */
    DevPrivateKeyRec pixmapPrivateKeyRec;
    DevScreenPrivateKeyRec spritePrivateKeyRec;
    /* Number of SW cursors currently visible on this screen */
    int sprites_visible;

    Bool is_secondary;
    Bool is_lsdc;

    PixmapPtr fbcon_pixmap;

#ifdef DRI3
    char *dri3_device_name;
#endif

    Bool dri2_flipping;
    Bool present_flipping;
    Bool flip_bo_import_failed;

    Bool dri2_enable;
    Bool present_enable;
};

typedef struct drmmode_rec * drmmode_ptr;

struct drmmode_prop_enum_info_rec {
    const char *name;
    Bool valid;
    uint64_t value;
};

typedef struct drmmode_prop_enum_info_rec * drmmode_prop_enum_info_ptr;

struct drmmode_prop_info_rec {
    const char *name;
    uint32_t prop_id;
    unsigned int num_enum_values;
    struct drmmode_prop_enum_info_rec *enum_values;
};
typedef struct drmmode_prop_info_rec *drmmode_prop_info_ptr;

typedef struct {
    drmModeModeInfo mode_info;
    uint32_t blob_id;
    struct xorg_list entry;
} drmmode_mode_rec, *drmmode_mode_ptr;

typedef struct {
    uint32_t format;
    uint32_t num_modifiers;
    uint64_t *modifiers;
} drmmode_format_rec, *drmmode_format_ptr;

struct drmmode_crtc_private_rec {
    drmmode_ptr drmmode;
    drmModeCrtcPtr mode_crtc;
    uint32_t vblank_pipe;
    int dpms_mode;
    struct dumb_bo *cursor_bo;
    Bool cursor_up;
    uint16_t lut_r[256], lut_g[256], lut_b[256];

    struct drmmode_prop_info_rec props[DRMMODE_CRTC__COUNT];
    struct drmmode_prop_info_rec props_plane[DRMMODE_PLANE__COUNT];
    uint32_t plane_id;
    drmmode_mode_ptr current_mode;
    uint32_t num_formats;
    drmmode_format_rec *formats;

    drmmode_bo rotate_bo;
    unsigned rotate_fb_id;

    PixmapPtr prime_pixmap;
    PixmapPtr prime_pixmap_back;
    unsigned prime_pixmap_x;

    /**
     * @{ MSC (vblank count) handling for the PRESENT extension.
     *
     * The kernel's vblank counters are 32 bits and apparently full of
     * lies, and we need to give a reliable 64-bit msc for GL, so we
     * have to track and convert to a userland-tracked 64-bit msc.
     */
    uint32_t msc_prev;
    uint64_t msc_high;
    /** @} */

    Bool need_modeset;
    struct xorg_list mode_list;

    Bool enable_flipping;
    Bool flipping_active;
};

typedef struct drmmode_crtc_private_rec * drmmode_crtc_private_ptr;

typedef struct {
    drmModePropertyPtr mode_prop;
    uint64_t value;
    int num_atoms;              /* if range prop, num_atoms == 1; if enum prop, num_atoms == num_enums + 1 */
    Atom *atoms;
} drmmode_prop_rec, *drmmode_prop_ptr;

typedef struct {
    drmmode_ptr drmmode;
    int output_id;
    drmModeConnectorPtr mode_output;
    drmModeEncoderPtr *mode_encoders;
    drmModePropertyBlobPtr edid_blob;
    drmModePropertyBlobPtr tile_blob;
    int dpms_enum_id;
    int dpms;
    int num_props;
    drmmode_prop_ptr props;
    struct drmmode_prop_info_rec props_connector[DRMMODE_CONNECTOR__COUNT];
    int enc_mask;
    int enc_clone_mask;
    xf86CrtcPtr current_crtc;
} drmmode_output_private_rec, *drmmode_output_private_ptr;

typedef struct {
    uint32_t    lessee_id;
} drmmode_lease_private_rec, *drmmode_lease_private_ptr;


#define msGetPixmapPriv(drmmode, p) \
    ((msPixmapPrivPtr)dixGetPrivateAddr(&(p)->devPrivates, &(drmmode)->pixmapPrivateKeyRec))

typedef struct _msSpritePriv {
    CursorPtr cursor;
    Bool sprite_visible;
} msSpritePrivRec, *msSpritePrivPtr;


#define msGetSpritePriv(dev, ms, screen) \
    dixLookupScreenPrivate(&(dev)->devPrivates, &(ms)->drmmode.spritePrivateKeyRec, screen)

Bool drmmode_is_format_supported(ScrnInfoPtr scrn, uint32_t format,
                                 uint64_t modifier);
int drmmode_bo_import(drmmode_ptr drmmode, drmmode_bo *bo,
                      uint32_t *fb_id);
int drmmode_bo_destroy(drmmode_ptr drmmode, drmmode_bo *bo);
uint32_t drmmode_bo_get_pitch(drmmode_bo *bo);
uint32_t drmmode_bo_get_handle(drmmode_bo *bo);


Bool drmmode_SharedPixmapPresentOnVBlank(PixmapPtr frontTarget, xf86CrtcPtr crtc,
                                         drmmode_ptr drmmode);
Bool drmmode_SharedPixmapFlip(PixmapPtr frontTarget, xf86CrtcPtr crtc,
                              drmmode_ptr drmmode);

extern Bool drmmode_pre_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int cpp);

void drmmode_adjust_frame(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int x, int y);
extern Bool drmmode_set_desired_modes(ScrnInfoPtr pScrn, drmmode_ptr drmmode, Bool set_hw);
extern Bool drmmode_setup_colormap(ScreenPtr pScreen, ScrnInfoPtr pScrn);

extern void drmmode_uevent_init(ScrnInfoPtr scrn, drmmode_ptr drmmode);
extern void drmmode_uevent_fini(ScrnInfoPtr scrn, drmmode_ptr drmmode);

void drmmode_get_default_bpp(ScrnInfoPtr pScrn, drmmode_ptr drmmmode,
                             int *depth, int *bpp);

void drmmode_copy_fb(ScrnInfoPtr pScrn, drmmode_ptr drmmode);

int drmmode_crtc_flip(xf86CrtcPtr crtc, uint32_t fb_id, uint32_t flags, void *data);

void drmmode_set_dpms(ScrnInfoPtr scrn, int PowerManagementMode, int flags);

xf86OutputStatus drmmode_output_detect(xf86OutputPtr output);

Bool drmmode_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
                       Rotation rotation, int x, int y);

void drmmode_validate_leases(ScrnInfoPtr scrn);


Bool drmmode_prop_info_copy(struct drmmode_prop_info_rec *dst,
                            const struct drmmode_prop_info_rec *src,
                            unsigned int num_props,
                            Bool copy_prop_id);

uint32_t drmmode_prop_info_update(drmmode_ptr drmmode,
                                  drmmode_prop_info_ptr info,
                                  unsigned int num_infos,
                                  drmModeObjectProperties *props);

void drmmode_output_create_resources(xf86OutputPtr output);

int connector_add_prop(drmModeAtomicReq *req,
                       drmmode_output_private_ptr drmmode_output,
                       enum drmmode_connector_property prop,
                       uint64_t val);

int crtc_add_dpms_props(drmModeAtomicReq *req,
                        xf86CrtcPtr crtc,
                        int new_dpms,
                        Bool *active);

Bool drmmode_InitSharedPixmapFlipping(xf86CrtcPtr crtc,
                                      drmmode_ptr drmmode);

#endif
