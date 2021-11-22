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
 *    Dave Airlie <airlied@redhat.com>
 *    Sui Jingfeng <suijingfeng@loongson.cn>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <malloc.h>
#include <inputstr.h>
// XA_ATOM
#include <X11/Xatom.h>
#include <mi.h>
#include <micmap.h>
#include <xf86cmap.h>
#include <xf86DDC.h>
#include <drm_fourcc.h>
#include <drm_mode.h>

#include <xf86drm.h>
#include <present.h>
#include <cursorstr.h>

#include <X11/extensions/dpmsconst.h>

#include "driver.h"
#include "drmmode_display.h"
#include "dumb_bo.h"
#include "vblank.h"
#include "loongson_options.h"
#include "loongson_entity.h"
#include "loongson_glamor.h"
#include "loongson_prime.h"
#include "loongson_randr.h"
#include "loongson_scanout.h"

#include "drmmode_crtc_config.h"
#include "drmmode_output.h"

static PixmapPtr drmmode_create_pixmap_header(ScreenPtr pScreen,
        int width, int height, int depth, int bitsPerPixel,
        int devKind, void *pPixData);


static inline uint32_t * formats_ptr(struct drm_format_modifier_blob *blob)
{
    return (uint32_t *)(((char *)blob) + blob->formats_offset);
}


static inline struct drm_format_modifier *
modifiers_ptr(struct drm_format_modifier_blob *blob)
{
    return (struct drm_format_modifier *)(((char *)blob) + blob->modifiers_offset);
}


Bool drmmode_is_format_supported(ScrnInfoPtr scrn, uint32_t format, uint64_t modifier)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int c, i, j;

    /* BO are imported as opaque surface, so let's pretend there is no alpha */
    format = get_opaque_format(format);

    for (c = 0; c < xf86_config->num_crtc; c++) {
        xf86CrtcPtr crtc = xf86_config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
        Bool found = FALSE;

        if (!crtc->enabled)
            continue;

        if (drmmode_crtc->num_formats == 0)
            continue;

        for (i = 0; i < drmmode_crtc->num_formats; i++) {
            drmmode_format_ptr iter = &drmmode_crtc->formats[i];

            if (iter->format != format)
                continue;

            if (modifier == DRM_FORMAT_MOD_INVALID ||
                iter->num_modifiers == 0) {
                found = TRUE;
                break;
            }

            for (j = 0; j < iter->num_modifiers; j++) {
                if (iter->modifiers[j] == modifier) {
                    found = TRUE;
                    break;
                }
            }

            break;
        }

        if (!found)
            return FALSE;
    }

    return TRUE;
}


static uint64_t
drmmode_prop_get_value(drmmode_prop_info_ptr info,
                       drmModeObjectPropertiesPtr props,
                       uint64_t def)
{
    unsigned int i;

    if (info->prop_id == 0)
        return def;

    for (i = 0; i < props->count_props; i++) {
        unsigned int j;

        if (props->props[i] != info->prop_id)
            continue;

        /* Simple (non-enum) types can return the value directly */
        if (info->num_enum_values == 0)
            return props->prop_values[i];

        /* Map from raw value to enum value */
        for (j = 0; j < info->num_enum_values; j++) {
            if (!info->enum_values[j].valid)
                continue;
            if (info->enum_values[j].value != props->prop_values[i])
                continue;

            return j;
        }
    }

    return def;
}

uint32_t drmmode_prop_info_update(drmmode_ptr drmmode,
                                  drmmode_prop_info_ptr info,
                                  unsigned int num_infos,
                                  drmModeObjectProperties *props)
{
    drmModePropertyRes *prop;
    uint32_t valid_mask = 0;
    unsigned i, j;

    assert(num_infos <= 32 && "update return type");

    for (i = 0; i < props->count_props; i++) {
        Bool props_incomplete = FALSE;
        unsigned int k;

        for (j = 0; j < num_infos; j++) {
            if (info[j].prop_id == props->props[i])
                break;
            if (!info[j].prop_id)
                props_incomplete = TRUE;
        }

        /* We've already discovered this property. */
        if (j != num_infos)
            continue;

        /* We haven't found this property ID, but as we've already
         * found all known properties, we don't need to look any
         * further. */
        if (!props_incomplete)
            break;

        prop = drmModeGetProperty(drmmode->fd, props->props[i]);
        if (!prop)
            continue;

        for (j = 0; j < num_infos; j++) {
            if (!strcmp(prop->name, info[j].name))
                break;
        }

        /* We don't know/care about this property. */
        if (j == num_infos) {
            drmModeFreeProperty(prop);
            continue;
        }

        info[j].prop_id = props->props[i];
        valid_mask |= 1U << j;

        if (info[j].num_enum_values == 0) {
            drmModeFreeProperty(prop);
            continue;
        }

        if (!(prop->flags & DRM_MODE_PROP_ENUM)) {
            xf86DrvMsg(drmmode->scrn->scrnIndex, X_WARNING,
                       "expected property %s to be an enum,"
                       " but it is not; ignoring\n", prop->name);
            drmModeFreeProperty(prop);
            continue;
        }

        for (k = 0; k < info[j].num_enum_values; k++) {
            int l;

            if (info[j].enum_values[k].valid)
                continue;

            for (l = 0; l < prop->count_enums; l++) {
                if (!strcmp(prop->enums[l].name,
                            info[j].enum_values[k].name))
                    break;
            }

            if (l == prop->count_enums)
                continue;

            info[j].enum_values[k].valid = TRUE;
            info[j].enum_values[k].value = prop->enums[l].value;
        }

        drmModeFreeProperty(prop);
    }

    return valid_mask;
}

Bool drmmode_prop_info_copy(struct drmmode_prop_info_rec *dst,
			    const struct drmmode_prop_info_rec *src,
			    unsigned int num_props,
			    Bool copy_prop_id)
{
    unsigned int i;

    xf86Msg(X_INFO, "%s: %u Props, copy prop_id : %s.\n",
                   __func__, num_props, copy_prop_id ? "TRUE" : "FALSE");

    memcpy(dst, src, num_props * sizeof(*dst));

    for (i = 0; i < num_props; ++i)
    {
        unsigned int j;
        struct drmmode_prop_enum_info_rec * pEnumVal;
        const unsigned int nEnumVal = src[i].num_enum_values;
        const unsigned int szBytes = nEnumVal * sizeof(struct drmmode_prop_enum_info_rec);

        if (copy_prop_id)
            dst[i].prop_id = src[i].prop_id;
        else
            dst[i].prop_id = 0;

        if (nEnumVal == 0)
            continue;

        // dst[i].enum_values = malloc(nEnumVal * sizeof(*dst[i].enum_values));
        pEnumVal = malloc(szBytes);
        if (pEnumVal == NULL)
            goto err;

        memcpy(pEnumVal, src[i].enum_values, szBytes);

        dst[i].enum_values = pEnumVal;
        for (j = 0; j < nEnumVal; ++j)
            dst[i].enum_values[j].valid = FALSE;
    }

    return TRUE;

err:
    while (i--)
        free(dst[i].enum_values);
    return FALSE;
}

static void
drmmode_prop_info_free(drmmode_prop_info_ptr info, int num_props)
{
    int i;

    for (i = 0; i < num_props; i++)
        free(info[i].enum_values);
}

static void
drmmode_ConvertToKMode(ScrnInfoPtr scrn,
                       drmModeModeInfo * kmode, DisplayModePtr mode);


static int
plane_add_prop(drmModeAtomicReq *req, drmmode_crtc_private_ptr drmmode_crtc,
               enum drmmode_plane_property prop, uint64_t val)
{
    drmmode_prop_info_ptr info = &drmmode_crtc->props_plane[prop];
    int ret;

    if (!info)
        return -1;

    ret = drmModeAtomicAddProperty(req, drmmode_crtc->plane_id,
                                   info->prop_id, val);
    return (ret <= 0) ? -1 : 0;
}

static int
plane_add_props(drmModeAtomicReq *req, xf86CrtcPtr crtc,
                uint32_t fb_id, int x, int y)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    int ret = 0;

    ret |= plane_add_prop(req, drmmode_crtc, DRMMODE_PLANE_FB_ID,
                          fb_id);
    ret |= plane_add_prop(req, drmmode_crtc, DRMMODE_PLANE_CRTC_ID,
                          fb_id ? drmmode_crtc->mode_crtc->crtc_id : 0);
    ret |= plane_add_prop(req, drmmode_crtc, DRMMODE_PLANE_SRC_X, x << 16);
    ret |= plane_add_prop(req, drmmode_crtc, DRMMODE_PLANE_SRC_Y, y << 16);
    ret |= plane_add_prop(req, drmmode_crtc, DRMMODE_PLANE_SRC_W,
                          crtc->mode.HDisplay << 16);
    ret |= plane_add_prop(req, drmmode_crtc, DRMMODE_PLANE_SRC_H,
                          crtc->mode.VDisplay << 16);
    ret |= plane_add_prop(req, drmmode_crtc, DRMMODE_PLANE_CRTC_X, 0);
    ret |= plane_add_prop(req, drmmode_crtc, DRMMODE_PLANE_CRTC_Y, 0);
    ret |= plane_add_prop(req, drmmode_crtc, DRMMODE_PLANE_CRTC_W,
                          crtc->mode.HDisplay);
    ret |= plane_add_prop(req, drmmode_crtc, DRMMODE_PLANE_CRTC_H,
                          crtc->mode.VDisplay);

    return ret;
}

static int
crtc_add_prop(drmModeAtomicReq *req, drmmode_crtc_private_ptr drmmode_crtc,
              enum drmmode_crtc_property prop, uint64_t val)
{
    drmmode_prop_info_ptr info = &drmmode_crtc->props[prop];
    int ret;

    if (!info)
        return -1;

    ret = drmModeAtomicAddProperty(req, drmmode_crtc->mode_crtc->crtc_id,
                                   info->prop_id, val);
    return (ret <= 0) ? -1 : 0;
}

int connector_add_prop(drmModeAtomicReq *req,
                       drmmode_output_private_ptr drmmode_output,
                       enum drmmode_connector_property prop,
                       uint64_t val)
{
    drmmode_prop_info_ptr info = &drmmode_output->props_connector[prop];
    int ret;

    if (!info)
        return -1;

    ret = drmModeAtomicAddProperty(req, drmmode_output->output_id,
                                   info->prop_id, val);
    return (ret <= 0) ? -1 : 0;
}

static int
drmmode_CompareKModes(drmModeModeInfo * kmode, drmModeModeInfo * other)
{
    return memcmp(kmode, other, sizeof(*kmode));
}

static int
drm_mode_ensure_blob(xf86CrtcPtr crtc, drmModeModeInfo mode_info)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_mode_ptr mode;
    int ret;

    if (drmmode_crtc->current_mode &&
        drmmode_CompareKModes(&drmmode_crtc->current_mode->mode_info, &mode_info) == 0)
        return 0;

    mode = calloc(sizeof(drmmode_mode_rec), 1);
    if (!mode)
        return -1;

    mode->mode_info = mode_info;
    ret = drmModeCreatePropertyBlob(ms->fd,
                                    &mode->mode_info,
                                    sizeof(mode->mode_info),
                                    &mode->blob_id);
    drmmode_crtc->current_mode = mode;
    xorg_list_add(&mode->entry, &drmmode_crtc->mode_list);

    return ret;
}

int crtc_add_dpms_props(drmModeAtomicReq *req, xf86CrtcPtr crtc,
                        int new_dpms, Bool *active)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    Bool crtc_active = FALSE;
    int i;
    int ret = 0;

    for (i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];
        drmmode_output_private_ptr drmmode_output = output->driver_private;

        if (output->crtc != crtc) {
            if (drmmode_output->current_crtc == crtc) {
                ret |= connector_add_prop(req, drmmode_output,
                                          DRMMODE_CONNECTOR_CRTC_ID, 0);
            }
            continue;
        }

        if (drmmode_output->output_id == -1)
            continue;

        if (new_dpms == DPMSModeOn)
            crtc_active = TRUE;

        ret |= connector_add_prop(req, drmmode_output,
                                  DRMMODE_CONNECTOR_CRTC_ID,
                                  crtc_active ?
                                      drmmode_crtc->mode_crtc->crtc_id : 0);
    }

    if (crtc_active) {
        drmModeModeInfo kmode;

        drmmode_ConvertToKMode(crtc->scrn, &kmode, &crtc->mode);
        ret |= drm_mode_ensure_blob(crtc, kmode);

        ret |= crtc_add_prop(req, drmmode_crtc,
                             DRMMODE_CRTC_ACTIVE, 1);
        ret |= crtc_add_prop(req, drmmode_crtc,
                             DRMMODE_CRTC_MODE_ID,
                             drmmode_crtc->current_mode->blob_id);
    } else {
        ret |= crtc_add_prop(req, drmmode_crtc,
                             DRMMODE_CRTC_ACTIVE, 0);
        ret |= crtc_add_prop(req, drmmode_crtc,
                             DRMMODE_CRTC_MODE_ID, 0);
    }

    if (active)
        *active = crtc_active;

    return ret;
}

static void
drm_mode_destroy(xf86CrtcPtr crtc, drmmode_mode_ptr mode)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    if (mode->blob_id)
        drmModeDestroyPropertyBlob(ms->fd, mode->blob_id);
    xorg_list_del(&mode->entry);
    free(mode);
}

static int
drmmode_crtc_can_test_mode(xf86CrtcPtr crtc)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);

    return ms->atomic_modeset;
}

/*
 * Return TRUE if success
 */
static Bool drmmode_crtc_get_fb_id(xf86CrtcPtr crtc,
                                   uint32_t *fb_id,
                                   int *x,
                                   int *y)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    int ret;

    *fb_id = 0;

    if (drmmode_crtc->prime_pixmap)
    {
        if (1 /* !drmmode->reverse_prime_offload_mode */)
        {
            msPixmapPrivPtr ppriv =
                msGetPixmapPriv(drmmode, drmmode_crtc->prime_pixmap);
            *fb_id = ppriv->fb_id;
            *x = 0;
        } else
            *x = drmmode_crtc->prime_pixmap_x;
        *y = 0;
    }
    else if (drmmode_crtc->rotate_fb_id)
    {
        *fb_id = drmmode_crtc->rotate_fb_id;
        *x = *y = 0;
    }
    else
    {
        *fb_id = drmmode->fb_id;
        *x = crtc->x;
        *y = crtc->y;
    }

    if (*fb_id == 0)
    {
        ret = drmmode_bo_import(drmmode, &drmmode->front_bo, &drmmode->fb_id);
        if (ret < 0)
        {
            ErrorF("failed to add fb %d\n", ret);
            return FALSE;
        }
        *fb_id = drmmode->fb_id;
    }

    return TRUE;
}

void
drmmode_set_dpms(ScrnInfoPtr scrn, int dpms, int flags)
{
    loongsonPtr ms = loongsonPTR(scrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    uint32_t mode_flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    int ret = 0;
    int i;

    assert(ms->atomic_modeset);

    if (!req)
        return;

    for (i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];
        drmmode_output_private_ptr drmmode_output = output->driver_private;

        if (output->crtc != NULL)
            continue;

        ret = connector_add_prop(req, drmmode_output,
                                 DRMMODE_CONNECTOR_CRTC_ID, 0);
    }

    for (i = 0; i < xf86_config->num_crtc; i++) {
        xf86CrtcPtr crtc = xf86_config->crtc[i];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
        Bool active = FALSE;

        ret |= crtc_add_dpms_props(req, crtc, dpms, &active);

        if (dpms == DPMSModeOn && active && drmmode_crtc->need_modeset) {
            uint32_t fb_id;
            int x, y;

            if (!drmmode_crtc_get_fb_id(crtc, &fb_id, &x, &y))
                continue;
            ret |= plane_add_props(req, crtc, fb_id, x, y);
            drmmode_crtc->need_modeset = FALSE;
        }
    }

    if (ret == 0)
        drmModeAtomicCommit(ms->fd, req, mode_flags, NULL);
    drmModeAtomicFree(req);

    ms->pending_modeset = TRUE;
    xf86DPMSSet(scrn, dpms, flags);
    ms->pending_modeset = FALSE;
}


static int drmmode_crtc_disable(xf86CrtcPtr crtc)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    int ret = 0;

    assert(ms->atomic_modeset);

    if (!req)
        return 1;

    ret |= crtc_add_prop(req, drmmode_crtc,
                         DRMMODE_CRTC_ACTIVE, 0);
    ret |= crtc_add_prop(req, drmmode_crtc,
                         DRMMODE_CRTC_MODE_ID, 0);

    if (ret == 0)
        ret = drmModeAtomicCommit(ms->fd, req, flags, NULL);

    drmModeAtomicFree(req);
    return ret;
}

static int drmmode_crtc_set_mode(xf86CrtcPtr crtc, Bool test_only)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    loongsonPtr ms = loongsonPTR(pScrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    drmModeModeInfo kmode;
    int output_count = 0;
    uint32_t *output_ids = NULL;
    uint32_t fb_id;
    int x, y;
    int i, ret = 0;

    if (drmmode_crtc_get_fb_id(crtc, &fb_id, &x, &y) == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "%s: failed get fb id from crtc\n", __func__);
        return 1;
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "%s: fb_id=%u, x=%d, y=%d\n", __func__, fb_id, x, y);
    }

#ifdef GLAMOR_HAS_GBM
    /* Make sure any pending drawing will be visible in a new scanout buffer */
    if (drmmode->glamor_enabled)
        glamor_finish(crtc->scrn->pScreen);
#endif

    if (ms->atomic_modeset)
    {
        drmModeAtomicReq *req = drmModeAtomicAlloc();
        Bool active;
        uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;

        if (!req)
            return 1;

        ret |= crtc_add_dpms_props(req, crtc, DPMSModeOn, &active);
        ret |= plane_add_props(req, crtc, active ? fb_id : 0, x, y);

        /* Orphaned CRTCs need to be disabled right now in atomic mode */
        for (i = 0; i < xf86_config->num_crtc; i++) {
            xf86CrtcPtr other_crtc = xf86_config->crtc[i];
            drmmode_crtc_private_ptr other_drmmode_crtc = other_crtc->driver_private;
            int lost_outputs = 0;
            int remaining_outputs = 0;
            int j;

            if (other_crtc == crtc)
                continue;

            for (j = 0; j < xf86_config->num_output; j++) {
                xf86OutputPtr output = xf86_config->output[j];
                drmmode_output_private_ptr drmmode_output = output->driver_private;

                if (drmmode_output->current_crtc == other_crtc) {
                    if (output->crtc == crtc)
                        lost_outputs++;
                    else
                        remaining_outputs++;
                }
            }

            if (lost_outputs > 0 && remaining_outputs == 0) {
                ret |= crtc_add_prop(req, other_drmmode_crtc,
                                     DRMMODE_CRTC_ACTIVE, 0);
                ret |= crtc_add_prop(req, other_drmmode_crtc,
                                     DRMMODE_CRTC_MODE_ID, 0);
            }
        }

        if (test_only)
            flags |= DRM_MODE_ATOMIC_TEST_ONLY;

        if (ret == 0)
            ret = drmModeAtomicCommit(ms->fd, req, flags, NULL);

        if (ret == 0 && !test_only) {
            for (i = 0; i < xf86_config->num_output; i++) {
                xf86OutputPtr output = xf86_config->output[i];
                drmmode_output_private_ptr drmmode_output = output->driver_private;

                if (output->crtc == crtc)
                    drmmode_output->current_crtc = crtc;
                else if (drmmode_output->current_crtc == crtc)
                    drmmode_output->current_crtc = NULL;
            }
        }

        drmModeAtomicFree(req);
        return ret;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "%s: number of output=%d\n",
                    __func__, xf86_config->num_output);

    output_ids = calloc(sizeof(uint32_t), xf86_config->num_output);
    if (!output_ids)
        return -1;

    for (i = 0; i < xf86_config->num_output; i++)
    {
        xf86OutputPtr output = xf86_config->output[i];
        drmmode_output_private_ptr drmmode_output;

        if (output->crtc != crtc)
            continue;

        drmmode_output = output->driver_private;
        if (drmmode_output->output_id == -1)
            continue;


        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "%s: output-%d id=%d\n",
                    __func__, i, drmmode_output->output_id);

        output_ids[output_count] = drmmode_output->output_id;
        output_count++;
    }

    drmmode_ConvertToKMode(crtc->scrn, &kmode, &crtc->mode);
    ret = drmModeSetCrtc(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                         fb_id, x, y, output_ids, output_count, &kmode);

    free(output_ids);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: finished\n", __func__);

    return ret;
}


int drmmode_crtc_flip(xf86CrtcPtr crtc,
                      uint32_t fb_id,
                      uint32_t flags,
                      void *data)
{
    loongsonPtr lsp = loongsonPTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    if (lsp->atomic_modeset)
    {
        int ret;

        drmModeAtomicReq *req = drmModeAtomicAlloc();

        if (!req)
            return 1;

        ret = plane_add_props(req, crtc, fb_id, crtc->x, crtc->y);
        flags |= DRM_MODE_ATOMIC_NONBLOCK;
        if (ret == 0)
            ret = drmModeAtomicCommit(lsp->fd, req, flags, data);
        drmModeAtomicFree(req);
        return ret;
    }

    return drmModePageFlip(lsp->fd,
                           drmmode_crtc->mode_crtc->crtc_id,
                           fb_id, flags, data);
}

int drmmode_bo_destroy(struct drmmode_rec * const pDrmMode,
                       struct DrmModeBO * const pBO)
{
    int ret;

#ifdef GLAMOR_HAS_GBM
    if (pBO->gbm)
    {
        gbm_bo_destroy(pBO->gbm);
        pBO->gbm = NULL;
    }
#endif

    if (pBO->dumb)
    {
        ret = dumb_bo_destroy(pDrmMode->fd, pBO->dumb);
        if (ret == 0)
            pBO->dumb = NULL;
    }

    return 0;
}

uint32_t drmmode_bo_get_pitch(struct DrmModeBO * const pBO)
{
#ifdef GLAMOR_HAS_GBM
    if (pBO->gbm)
        return gbm_bo_get_stride(pBO->gbm);
#endif

    return pBO->dumb->pitch;
}

static Bool drmmode_bo_has_bo(struct DrmModeBO * const pBO)
{
#ifdef GLAMOR_HAS_GBM
    if (pBO->gbm)
        return TRUE;
#endif

    return pBO->dumb != NULL;
}

uint32_t drmmode_bo_get_handle(drmmode_bo *bo)
{
#ifdef GLAMOR_HAS_GBM
    if (bo->gbm)
        return gbm_bo_get_handle(bo->gbm).u32;
#endif

    return bo->dumb->handle;
}

static void * drmmode_bo_map(drmmode_ptr drmmode, drmmode_bo *bo)
{
    int ret;

#ifdef GLAMOR_HAS_GBM
    if (bo->gbm)
        return NULL;
#endif

    if (bo->dumb->ptr)
        return bo->dumb->ptr;

    ret = dumb_bo_map(drmmode->fd, bo->dumb);
    if (ret)
        return NULL;

    return bo->dumb->ptr;
}

int drmmode_bo_import(drmmode_ptr drmmode, drmmode_bo *bo, uint32_t *fb_id)
{
#ifdef GBM_BO_WITH_MODIFIERS
    modesettingPtr ms = modesettingPTR(drmmode->scrn);
    if (bo->gbm && ms->kms_has_modifiers &&
        gbm_bo_get_modifier(bo->gbm) != DRM_FORMAT_MOD_INVALID)
    {
        int num_fds;

        num_fds = gbm_bo_get_plane_count(bo->gbm);
        if (num_fds > 0) {
            int i;
            uint32_t format;
            uint32_t handles[4];
            uint32_t strides[4];
            uint32_t offsets[4];
            uint64_t modifiers[4];

            memset(handles, 0, sizeof(handles));
            memset(strides, 0, sizeof(strides));
            memset(offsets, 0, sizeof(offsets));
            memset(modifiers, 0, sizeof(modifiers));

            format = gbm_bo_get_format(bo->gbm);
            format = get_opaque_format(format);
            for (i = 0; i < num_fds; i++) {
                handles[i] = gbm_bo_get_handle_for_plane(bo->gbm, i).u32;
                strides[i] = gbm_bo_get_stride_for_plane(bo->gbm, i);
                offsets[i] = gbm_bo_get_offset(bo->gbm, i);
                modifiers[i] = gbm_bo_get_modifier(bo->gbm);
            }

            return drmModeAddFB2WithModifiers(drmmode->fd, bo->width, bo->height,
                                              format, handles, strides,
                                              offsets, modifiers, fb_id,
                                              DRM_MODE_FB_MODIFIERS);
        }
    }
#endif
    return drmModeAddFB(drmmode->fd, bo->width, bo->height,
                        drmmode->scrn->depth, drmmode->kbpp,
                        drmmode_bo_get_pitch(bo),
                        drmmode_bo_get_handle(bo), fb_id);
}


static Bool
drmmode_SharedPixmapPresent(PixmapPtr ppix, xf86CrtcPtr crtc,
                            drmmode_ptr drmmode)
{
    ScreenPtr master = crtc->randr_crtc->pScreen->current_master;

    if (master->PresentSharedPixmap(ppix)) {
        /* Success, queue flip to back target */
        if (drmmode_SharedPixmapFlip(ppix, crtc, drmmode))
            return TRUE;

        xf86DrvMsg(drmmode->scrn->scrnIndex, X_WARNING,
                   "drmmode_SharedPixmapFlip() failed, trying again next vblank\n");

        return drmmode_SharedPixmapPresentOnVBlank(ppix, crtc, drmmode);
    }

    /* Failed to present, try again on next vblank after damage */
    if (master->RequestSharedPixmapNotifyDamage) {
        msPixmapPrivPtr ppriv = msGetPixmapPriv(drmmode, ppix);

        /* Set flag first in case we are immediately notified */
        ppriv->wait_for_damage = TRUE;

        if (master->RequestSharedPixmapNotifyDamage(ppix))
            return TRUE;
        else
            ppriv->wait_for_damage = FALSE;
    }

    /* Damage notification not available, just try again on vblank */
    return drmmode_SharedPixmapPresentOnVBlank(ppix, crtc, drmmode);
}

struct vblank_event_args {
    PixmapPtr frontTarget;
    PixmapPtr backTarget;
    xf86CrtcPtr crtc;
    drmmode_ptr drmmode;
    Bool flip;
};


static void
drmmode_SharedPixmapVBlankEventHandler(uint64_t frame, uint64_t usec,
                                       void *data)
{
    struct vblank_event_args *args = data;

    drmmode_crtc_private_ptr drmmode_crtc = args->crtc->driver_private;

    if (args->flip) {
        /* frontTarget is being displayed, update crtc to reflect */
        drmmode_crtc->prime_pixmap = args->frontTarget;
        drmmode_crtc->prime_pixmap_back = args->backTarget;

        /* Safe to present on backTarget, no longer displayed */
        drmmode_SharedPixmapPresent(args->backTarget, args->crtc, args->drmmode);
    } else {
        /* backTarget is still being displayed, present on frontTarget */
        drmmode_SharedPixmapPresent(args->frontTarget, args->crtc, args->drmmode);
    }

    free(args);
}

static void
drmmode_SharedPixmapVBlankEventAbort(void *data)
{
    struct vblank_event_args *args = data;

    msGetPixmapPriv(args->drmmode, args->frontTarget)->flip_seq = 0;

    free(args);
}


Bool drmmode_SharedPixmapPresentOnVBlank(PixmapPtr ppix,
                                         xf86CrtcPtr crtc,
                                         drmmode_ptr drmmode)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    msPixmapPrivPtr ppriv = msGetPixmapPriv(drmmode, ppix);
    struct vblank_event_args *event_args;

    if (ppix == drmmode_crtc->prime_pixmap)
        return FALSE; /* Already flipped to this pixmap */
    if (ppix != drmmode_crtc->prime_pixmap_back)
        return FALSE; /* Pixmap is not a scanout pixmap for CRTC */

    event_args = calloc(1, sizeof(*event_args));
    if (!event_args)
        return FALSE;

    event_args->frontTarget = ppix;
    event_args->backTarget = drmmode_crtc->prime_pixmap;
    event_args->crtc = crtc;
    event_args->drmmode = drmmode;
    event_args->flip = FALSE;

    ppriv->flip_seq = ms_drm_queue_alloc(crtc, event_args,
                           drmmode_SharedPixmapVBlankEventHandler,
                           drmmode_SharedPixmapVBlankEventAbort);

    return ms_queue_vblank(crtc, MS_QUEUE_RELATIVE, 1, NULL, ppriv->flip_seq);
}


Bool
drmmode_SharedPixmapFlip(PixmapPtr frontTarget, xf86CrtcPtr crtc,
                         drmmode_ptr drmmode)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    msPixmapPrivPtr ppriv_front = msGetPixmapPriv(drmmode, frontTarget);

    struct vblank_event_args *event_args;

    event_args = calloc(1, sizeof(*event_args));
    if (!event_args)
        return FALSE;

    event_args->frontTarget = frontTarget;
    event_args->backTarget = drmmode_crtc->prime_pixmap;
    event_args->crtc = crtc;
    event_args->drmmode = drmmode;
    event_args->flip = TRUE;

    ppriv_front->flip_seq =
        ms_drm_queue_alloc(crtc, event_args,
                           drmmode_SharedPixmapVBlankEventHandler,
                           drmmode_SharedPixmapVBlankEventAbort);

    if (drmModePageFlip(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                        ppriv_front->fb_id, DRM_MODE_PAGE_FLIP_EVENT,
                        (void *)(intptr_t) ppriv_front->flip_seq) < 0) {
        ms_drm_abort_seq(crtc->scrn, ppriv_front->flip_seq);
        return FALSE;
    }

    return TRUE;
}

static Bool
drmmode_InitSharedPixmapFlipping(xf86CrtcPtr crtc, drmmode_ptr drmmode)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    if (!drmmode_crtc->enable_flipping)
        return FALSE;

    if (drmmode_crtc->flipping_active)
        return TRUE;

    drmmode_crtc->flipping_active =
        drmmode_SharedPixmapPresent(drmmode_crtc->prime_pixmap_back,
                                    crtc, drmmode);

    return drmmode_crtc->flipping_active;
}


static void
drmmode_ConvertFromKMode(ScrnInfoPtr scrn,
                         drmModeModeInfo * kmode, DisplayModePtr mode)
{
    memset(mode, 0, sizeof(DisplayModeRec));
    mode->status = MODE_OK;

    mode->Clock = kmode->clock;

    mode->HDisplay = kmode->hdisplay;
    mode->HSyncStart = kmode->hsync_start;
    mode->HSyncEnd = kmode->hsync_end;
    mode->HTotal = kmode->htotal;
    mode->HSkew = kmode->hskew;

    mode->VDisplay = kmode->vdisplay;
    mode->VSyncStart = kmode->vsync_start;
    mode->VSyncEnd = kmode->vsync_end;
    mode->VTotal = kmode->vtotal;
    mode->VScan = kmode->vscan;

    mode->Flags = kmode->flags; //& FLAG_BITS;
    mode->name = strdup(kmode->name);

    if (kmode->type & DRM_MODE_TYPE_DRIVER)
        mode->type = M_T_DRIVER;
    if (kmode->type & DRM_MODE_TYPE_PREFERRED)
        mode->type |= M_T_PREFERRED;
    xf86SetModeCrtc(mode, scrn->adjustFlags);
}

static void
drmmode_ConvertToKMode(ScrnInfoPtr scrn,
                       drmModeModeInfo * kmode, DisplayModePtr mode)
{
    memset(kmode, 0, sizeof(*kmode));

    kmode->clock = mode->Clock;
    kmode->hdisplay = mode->HDisplay;
    kmode->hsync_start = mode->HSyncStart;
    kmode->hsync_end = mode->HSyncEnd;
    kmode->htotal = mode->HTotal;
    kmode->hskew = mode->HSkew;

    kmode->vdisplay = mode->VDisplay;
    kmode->vsync_start = mode->VSyncStart;
    kmode->vsync_end = mode->VSyncEnd;
    kmode->vtotal = mode->VTotal;
    kmode->vscan = mode->VScan;

    kmode->flags = mode->Flags; //& FLAG_BITS;
    if (mode->name)
        strncpy(kmode->name, mode->name, DRM_DISPLAY_MODE_LEN);
    kmode->name[DRM_DISPLAY_MODE_LEN - 1] = 0;

}

static void
drmmode_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    /* XXX Check if DPMS mode is already the right one */

    drmmode_crtc->dpms_mode = mode;

    if (ms->atomic_modeset) {
        if (mode != DPMSModeOn && !ms->pending_modeset)
            drmmode_crtc_disable(crtc);
    } else if (crtc->enabled == FALSE) {
        drmModeSetCrtc(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                       0, 0, 0, NULL, 0, NULL);
    }
}

#ifdef GLAMOR_HAS_GBM
static PixmapPtr
create_pixmap_for_fbcon(drmmode_ptr drmmode, ScrnInfoPtr pScrn, int fbcon_id)
{
    PixmapPtr pixmap = drmmode->fbcon_pixmap;
    drmModeFBPtr fbcon;
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    modesettingPtr ms = modesettingPTR(pScrn);
    Bool ret;

    if (pixmap)
        return pixmap;

    fbcon = drmModeGetFB(drmmode->fd, fbcon_id);
    if (fbcon == NULL)
        return NULL;

    if (fbcon->depth != pScrn->depth ||
        fbcon->width != pScrn->virtualX ||
        fbcon->height != pScrn->virtualY)
        goto out_free_fb;

    pixmap = drmmode_create_pixmap_header(pScreen, fbcon->width,
                                          fbcon->height, fbcon->depth,
                                          fbcon->bpp, fbcon->pitch, NULL);
    if (!pixmap)
        goto out_free_fb;

    ret = ms->glamor.egl_create_textured_pixmap(pixmap, fbcon->handle,
                                                fbcon->pitch);
    if (!ret) {
      FreePixmap(pixmap);
      pixmap = NULL;
    }

    drmmode->fbcon_pixmap = pixmap;
out_free_fb:
    drmModeFreeFB(fbcon);
    return pixmap;
}
#endif

void
drmmode_copy_fb(ScrnInfoPtr pScrn, drmmode_ptr drmmode)
{
#ifdef GLAMOR_HAS_GBM
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    PixmapPtr src, dst;
    int fbcon_id = 0;
    GCPtr gc;
    int i;

    for (i = 0; i < xf86_config->num_crtc; i++) {
        drmmode_crtc_private_ptr drmmode_crtc = xf86_config->crtc[i]->driver_private;
        if (drmmode_crtc->mode_crtc->buffer_id)
            fbcon_id = drmmode_crtc->mode_crtc->buffer_id;
    }

    if (!fbcon_id)
        return;

    if (fbcon_id == drmmode->fb_id) {
        /* in some rare case there might be no fbcon and we might already
         * be the one with the current fb to avoid a false deadlck in
         * kernel ttm code just do nothing as anyway there is nothing
         * to do
         */
        return;
    }

    src = create_pixmap_for_fbcon(drmmode, pScrn, fbcon_id);
    if (!src)
        return;

    dst = pScreen->GetScreenPixmap(pScreen);

    gc = GetScratchGC(pScrn->depth, pScreen);
    ValidateGC(&dst->drawable, gc);

    (*gc->ops->CopyArea)(&src->drawable, &dst->drawable, gc, 0, 0,
                         pScrn->virtualX, pScrn->virtualY, 0, 0);

    FreeScratchGC(gc);

    pScreen->canDoBGNoneRoot = TRUE;

    if (drmmode->fbcon_pixmap)
        pScrn->pScreen->DestroyPixmap(drmmode->fbcon_pixmap);
    drmmode->fbcon_pixmap = NULL;
#endif
}



Bool drmmode_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
                       Rotation rotation, int x, int y)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    int saved_x, saved_y;
    Rotation saved_rotation;
    DisplayModeRec saved_mode;
    Bool ret = TRUE;
    Bool can_test;
    int i;

    saved_mode = crtc->mode;
    saved_x = crtc->x;
    saved_y = crtc->y;
    saved_rotation = crtc->rotation;

    xf86Msg(X_INFO,
               "%s: saved_x = %d, saved_y = %d\n",
               __func__, saved_x, saved_y);

    if (mode) {
        crtc->mode = *mode;
        crtc->x = x;
        crtc->y = y;
        crtc->rotation = rotation;

        if (!xf86CrtcRotate(crtc)) {
            goto done;
        }

        crtc->funcs->gamma_set(crtc, crtc->gamma_red, crtc->gamma_green,
                               crtc->gamma_blue, crtc->gamma_size);

        can_test = drmmode_crtc_can_test_mode(crtc);
        if (drmmode_crtc_set_mode(crtc, can_test)) {
            xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
                       "failed to set mode: %s\n", strerror(errno));
            ret = FALSE;
            goto done;
        } else
            ret = TRUE;

        if (crtc->scrn->pScreen)
            xf86CrtcSetScreenSubpixelOrder(crtc->scrn->pScreen);

        ms->pending_modeset = TRUE;
        drmmode_crtc->need_modeset = FALSE;
        crtc->funcs->dpms(crtc, DPMSModeOn);

        if (drmmode_crtc->prime_pixmap_back)
            drmmode_InitSharedPixmapFlipping(crtc, drmmode);

        /* go through all the outputs and force DPMS them back on? */
        for (i = 0; i < xf86_config->num_output; i++) {
            xf86OutputPtr output = xf86_config->output[i];
            drmmode_output_private_ptr drmmode_output;

            if (output->crtc != crtc)
                continue;

            drmmode_output = output->driver_private;
            if (drmmode_output->output_id == -1)
                continue;
            output->funcs->dpms(output, DPMSModeOn);
        }

        /* if we only tested the mode previously, really set it now */
        if (can_test)
            drmmode_crtc_set_mode(crtc, FALSE);
        ms->pending_modeset = FALSE;
    }

 done:
    if (!ret) {
        crtc->x = saved_x;
        crtc->y = saved_y;
        crtc->rotation = saved_rotation;
        crtc->mode = saved_mode;
    } else
        crtc->active = TRUE;

    return ret;
}


static void drmmode_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg)
{

}


static void
drmmode_set_cursor_position(xf86CrtcPtr crtc, int x, int y)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    drmModeMoveCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, x, y);
}


static Bool
drmmode_set_cursor(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    uint32_t handle = drmmode_crtc->cursor_bo->handle;
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    CursorPtr cursor = xf86CurrentCursor(crtc->scrn->pScreen);
    int ret = -EINVAL;

    if (cursor == NullCursor) {
        return TRUE;
    }

    ret = drmModeSetCursor2(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                            handle, ms->cursor_width, ms->cursor_height,
                            cursor->bits->xhot, cursor->bits->yhot);

    /* -EINVAL can mean that an old kernel supports drmModeSetCursor but
     * not drmModeSetCursor2, though it can mean other things too. */
    if (ret == -EINVAL)
        ret = drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                               handle, ms->cursor_width, ms->cursor_height);

    /* -ENXIO normally means that the current drm driver supports neither
     * cursor_set nor cursor_set2.  Disable hardware cursor support for
     * the rest of the session in that case. */
    if (ret == -ENXIO) {
        xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
        xf86CursorInfoPtr cursor_info = xf86_config->cursor_info;

        cursor_info->MaxWidth = cursor_info->MaxHeight = 0;
        drmmode_crtc->drmmode->sw_cursor = TRUE;
    }

    if (ret)
        /* fallback to swcursor */
        return FALSE;
    return TRUE;
}

static void drmmode_hide_cursor(xf86CrtcPtr crtc);

/*
 * The load_cursor_argb_check driver hook.
 *
 * Sets the hardware cursor by calling the drmModeSetCursor2 ioctl.
 * On failure, returns FALSE indicating that the X server should fall
 * back to software cursors.
 */
static Bool
drmmode_load_cursor_argb_check(xf86CrtcPtr crtc, CARD32 *image)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    int i;
    uint32_t *ptr;

    /* cursor should be mapped already */
    ptr = (uint32_t *) (drmmode_crtc->cursor_bo->ptr);

    for (i = 0; i < ms->cursor_width * ms->cursor_height; i++)
        ptr[i] = image[i];      // cpu_to_le32(image[i]);

    if (drmmode_crtc->cursor_up)
        return drmmode_set_cursor(crtc);
    return TRUE;
}

static void
drmmode_hide_cursor(xf86CrtcPtr crtc)
{
    modesettingPtr ms = modesettingPTR(crtc->scrn);
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    drmmode_crtc->cursor_up = FALSE;
    drmModeSetCursor(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id, 0,
                     ms->cursor_width, ms->cursor_height);
}

static Bool drmmode_show_cursor(xf86CrtcPtr crtc)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_crtc->cursor_up = TRUE;
    return drmmode_set_cursor(crtc);
}

static void
drmmode_crtc_gamma_set(xf86CrtcPtr crtc, uint16_t * red, uint16_t * green,
                       uint16_t * blue, int size)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;

    drmModeCrtcSetGamma(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                        size, red, green, blue);
}

/* OUTPUT SLAVE SUPPORT */
static Bool drmmode_set_scanout_pixmap(xf86CrtcPtr crtc, PixmapPtr ppix)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

    /* Use DisableSharedPixmapFlipping before switching to single buf */
    if (drmmode_crtc->enable_flipping)
        return FALSE;

    return drmmode_set_target_scanout_pixmap(crtc, ppix,
                                             &drmmode_crtc->prime_pixmap);
}

static void *drmmode_shadow_allocate(xf86CrtcPtr crtc, int width, int height)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    struct drmmode_crtc_private_rec * const drmmode_crtc = crtc->driver_private;
    struct drmmode_rec * const pDrmMode = drmmode_crtc->drmmode;
    int ret;
    Bool bo_create_res;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: %dx%d\n",
                                 __func__, width, height);

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled)
    {
        bo_create_res = ls_glamor_create_gbm_bo(pScrn,
                                                &drmmode_crtc->rotate_bo,
                                                width,
                                                height,
                                                pDrmMode->kbpp);
    }
    else
#endif
    {
        struct DrmModeBO * const pRotateBO = &drmmode_crtc->rotate_bo;

         pRotateBO->width = width;
         pRotateBO->height = height;

         pRotateBO->dumb = dumb_bo_create(pDrmMode->fd,
                                          width,
                                          height,
                                          pDrmMode->kbpp);
         if (pRotateBO->dumb == NULL)
         {
             xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                        "%s: Create Rotated Dumb BO(%dx%d, bpp=%d) failed\n",
                        __func__, width, height, pDrmMode->kbpp);

             bo_create_res = FALSE;
         }
         else
         {
             xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                        "%s: Rotated Dumb BO(%dx%d, bpp=%d) created\n",
                        __func__, width, height, pDrmMode->kbpp);

             bo_create_res = TRUE;
         }
    }

    if (bo_create_res == FALSE)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
               "Couldn't allocate shadow memory for rotated CRTC\n");
        return NULL;
    }

    ret = drmmode_bo_import(pDrmMode,
                            &drmmode_crtc->rotate_bo,
                            &drmmode_crtc->rotate_fb_id);

    if (ret)
    {
        ErrorF("failed to add rotate fb\n");
        drmmode_bo_destroy(pDrmMode, &drmmode_crtc->rotate_bo);
        return NULL;
    }

#ifdef GLAMOR_HAS_GBM
    if (pDrmMode->glamor_enabled && pDrmMode->gbm)
        return drmmode_crtc->rotate_bo.gbm;
#endif
    return drmmode_crtc->rotate_bo.dumb;
}



static PixmapPtr drmmode_create_pixmap_header(ScreenPtr pScreen,
        int width, int height, int depth, int bitsPerPixel,
        int devKind, void *pPixData)
{
    PixmapPtr pixmap;

    /* width and height of 0 means don't allocate any pixmap data */
    pixmap = (*pScreen->CreatePixmap)(pScreen, 0, 0, depth, 0);

    if (pixmap)
    {
        if ((*pScreen->ModifyPixmapHeader)(pixmap, width, height, depth,
                                           bitsPerPixel, devKind, pPixData))
        {
            return pixmap;
        }

        (*pScreen->DestroyPixmap)(pixmap);
    }

    return NullPixmap;
}


static PixmapPtr drmmode_shadow_create(xf86CrtcPtr crtc,
                                       void *data,
                                       int width,
                                       int height)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    uint32_t rotate_pitch;
    PixmapPtr rotate_pixmap;
    void *pPixData = NULL;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: %dx%d\n",
                                 __func__, width, height);

    if (!data)
    {
        data = drmmode_shadow_allocate(crtc, width, height);
        if (!data)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Couldn't allocate shadow pixmap for rotated CRTC\n");
            return NULL;
        }
    }

    if (!drmmode_bo_has_bo(&drmmode_crtc->rotate_bo))
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Couldn't allocate shadow pixmap for rotated CRTC\n");
        return NULL;
    }

    pPixData = drmmode_bo_map(drmmode, &drmmode_crtc->rotate_bo);
    rotate_pitch = drmmode_bo_get_pitch(&drmmode_crtc->rotate_bo);

    rotate_pixmap = drmmode_create_pixmap_header(pScrn->pScreen,
                                                 width, height,
                                                 pScrn->depth,
                                                 drmmode->kbpp,
                                                 rotate_pitch,
                                                 pPixData);

    if (rotate_pixmap == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Couldn't allocate shadow pixmap for rotated CRTC\n");
        return NULL;
    }

    if (drmmode->exa_enabled)
    {
        drmmode_bo *rotate_bo = &drmmode_crtc->rotate_bo;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "exa: %s\n", __func__);
        ms_exa_set_pixmap_bo(pScrn, rotate_pixmap, rotate_bo->dumb, FALSE);
    }

#ifdef GLAMOR_HAS_GBM
    if (drmmode->glamor_enabled)
    {
        glamor_set_pixmap_bo(pScrn, rotate_pixmap, &drmmode_crtc->rotate_bo);
    }
#endif

    return rotate_pixmap;
}

static void
drmmode_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr rotate_pixmap, void *data)
{
    struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
    struct drmmode_rec *drmmode = drmmode_crtc->drmmode;

    if (rotate_pixmap)
    {
        rotate_pixmap->drawable.pScreen->DestroyPixmap(rotate_pixmap);
    }

    if (data) {
        drmModeRmFB(drmmode->fd, drmmode_crtc->rotate_fb_id);
        drmmode_crtc->rotate_fb_id = 0;

        drmmode_bo_destroy(drmmode, &drmmode_crtc->rotate_bo);
        memset(&drmmode_crtc->rotate_bo, 0, sizeof drmmode_crtc->rotate_bo);
    }
}

static void
drmmode_crtc_destroy(xf86CrtcPtr crtc)
{
    drmmode_mode_ptr iterator, next;
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    modesettingPtr ms = modesettingPTR(crtc->scrn);

    if (!ms->atomic_modeset)
        return;

    drmmode_prop_info_free(drmmode_crtc->props_plane, DRMMODE_PLANE__COUNT);
    xorg_list_for_each_entry_safe(iterator, next, &drmmode_crtc->mode_list, entry) {
        drm_mode_destroy(crtc, iterator);
    }
}

static const xf86CrtcFuncsRec drmmode_crtc_funcs = {
    .dpms = drmmode_crtc_dpms,
    .set_mode_major = drmmode_set_mode_major,
    .set_cursor_colors = drmmode_set_cursor_colors,
    .set_cursor_position = drmmode_set_cursor_position,
    .show_cursor_check = drmmode_show_cursor,
    .hide_cursor = drmmode_hide_cursor,
    .load_cursor_argb_check = drmmode_load_cursor_argb_check,

    .gamma_set = drmmode_crtc_gamma_set,
    .destroy = drmmode_crtc_destroy,
     /* MODESETTING OUTPUT SLAVE SUPPORT */
    .set_scanout_pixmap = drmmode_set_scanout_pixmap,
    .shadow_allocate = drmmode_shadow_allocate,
    .shadow_create = drmmode_shadow_create,
    .shadow_destroy = drmmode_shadow_destroy,
};

static uint32_t drmmode_crtc_vblank_pipe(int crtc_id)
{
    if (crtc_id > 1)
        return crtc_id << DRM_VBLANK_HIGH_CRTC_SHIFT;
    else if (crtc_id > 0)
        return DRM_VBLANK_SECONDARY;
    else
        return 0;
}

static Bool
is_plane_assigned(ScrnInfoPtr scrn, int plane_id)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int c;

    for (c = 0; c < xf86_config->num_crtc; c++) {
        xf86CrtcPtr iter = xf86_config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = iter->driver_private;
        if (drmmode_crtc->plane_id == plane_id)
            return TRUE;
    }

    return FALSE;
}

/**
 * Populates the formats array, and the modifiers of each format for a drm_plane.
 */
static Bool
populate_format_modifiers(xf86CrtcPtr crtc, const drmModePlane *kplane,
                          uint32_t blob_id)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    unsigned i, j;
    drmModePropertyBlobRes *blob;
    struct drm_format_modifier_blob *fmt_mod_blob;
    uint32_t *blob_formats;
    struct drm_format_modifier *blob_modifiers;

    if (!blob_id)
        return FALSE;

    blob = drmModeGetPropertyBlob(drmmode->fd, blob_id);
    if (!blob)
        return FALSE;

    fmt_mod_blob = blob->data;
    blob_formats = formats_ptr(fmt_mod_blob);
    blob_modifiers = modifiers_ptr(fmt_mod_blob);

    assert(drmmode_crtc->num_formats == fmt_mod_blob->count_formats);

    for (i = 0; i < fmt_mod_blob->count_formats; i++) {
        uint32_t num_modifiers = 0;
        uint64_t *modifiers = NULL;
        uint64_t *tmp;
        for (j = 0; j < fmt_mod_blob->count_modifiers; j++) {
            struct drm_format_modifier *mod = &blob_modifiers[j];

            if ((i < mod->offset) || (i > mod->offset + 63))
                continue;
            if (!(mod->formats & (1 << (i - mod->offset))))
                continue;

            num_modifiers++;
            tmp = realloc(modifiers, num_modifiers * sizeof(modifiers[0]));
            if (!tmp) {
                free(modifiers);
                drmModeFreePropertyBlob(blob);
                return FALSE;
            }
            modifiers = tmp;
            modifiers[num_modifiers - 1] = mod->modifier;
        }

        drmmode_crtc->formats[i].format = blob_formats[i];
        drmmode_crtc->formats[i].modifiers = modifiers;
        drmmode_crtc->formats[i].num_modifiers = num_modifiers;
    }

    drmModeFreePropertyBlob(blob);

    return TRUE;
}

static void drmmode_crtc_create_planes(xf86CrtcPtr crtc, int num)
{
    drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
    drmmode_ptr drmmode = drmmode_crtc->drmmode;
    drmModePlaneRes *kplane_res;
    drmModePlane *kplane, *best_kplane = NULL;
    drmModeObjectProperties *props;
    uint32_t i, type, blob_id;
    int current_crtc, best_plane = 0;

    static struct drmmode_prop_enum_info_rec plane_type_enums[] = {
        [DRMMODE_PLANE_TYPE_PRIMARY] = {
            .name = "Primary",
        },
        [DRMMODE_PLANE_TYPE_OVERLAY] = {
            .name = "Overlay",
        },
        [DRMMODE_PLANE_TYPE_CURSOR] = {
            .name = "Cursor",
        },
    };

    static const struct drmmode_prop_info_rec plane_props[] = {
        [DRMMODE_PLANE_TYPE] = {
            .name = "type",
            .enum_values = plane_type_enums,
            .num_enum_values = DRMMODE_PLANE_TYPE__COUNT,
        },
        [DRMMODE_PLANE_FB_ID] = { .name = "FB_ID", },
        [DRMMODE_PLANE_CRTC_ID] = { .name = "CRTC_ID", },
        [DRMMODE_PLANE_IN_FORMATS] = { .name = "IN_FORMATS", },
        [DRMMODE_PLANE_SRC_X] = { .name = "SRC_X", },
        [DRMMODE_PLANE_SRC_Y] = { .name = "SRC_Y", },
        [DRMMODE_PLANE_SRC_W] = { .name = "SRC_W", },
        [DRMMODE_PLANE_SRC_H] = { .name = "SRC_H", },
        [DRMMODE_PLANE_CRTC_X] = { .name = "CRTC_X", },
        [DRMMODE_PLANE_CRTC_Y] = { .name = "CRTC_Y", },
        [DRMMODE_PLANE_CRTC_W] = { .name = "CRTC_W", },
        [DRMMODE_PLANE_CRTC_H] = { .name = "CRTC_H", },
    };

    struct drmmode_prop_info_rec tmp_props[DRMMODE_PLANE__COUNT];

    if (!drmmode_prop_info_copy(tmp_props, plane_props, DRMMODE_PLANE__COUNT, 0))
    {
        xf86DrvMsg(drmmode->scrn->scrnIndex, X_ERROR,
                   "failed to copy plane property info\n");
        drmmode_prop_info_free(tmp_props, DRMMODE_PLANE__COUNT);
        return;
    }

    kplane_res = drmModeGetPlaneResources(drmmode->fd);
    if (!kplane_res) {
        xf86DrvMsg(drmmode->scrn->scrnIndex, X_ERROR,
                   "failed to get plane resources: %s\n", strerror(errno));
        drmmode_prop_info_free(tmp_props, DRMMODE_PLANE__COUNT);
        return;
    }

    for (i = 0; i < kplane_res->count_planes; i++) {
        int plane_id;

        kplane = drmModeGetPlane(drmmode->fd, kplane_res->planes[i]);
        if (!kplane)
            continue;

        if (!(kplane->possible_crtcs & (1 << num)) ||
            is_plane_assigned(drmmode->scrn, kplane->plane_id)) {
            drmModeFreePlane(kplane);
            continue;
        }

        plane_id = kplane->plane_id;

        props = drmModeObjectGetProperties(drmmode->fd, plane_id,
                                           DRM_MODE_OBJECT_PLANE);
        if (!props) {
            xf86DrvMsg(drmmode->scrn->scrnIndex, X_ERROR,
                    "couldn't get plane properties\n");
            drmModeFreePlane(kplane);
            continue;
        }

        drmmode_prop_info_update(drmmode, tmp_props, DRMMODE_PLANE__COUNT, props);

        /* Only primary planes are important for atomic page-flipping */
        type = drmmode_prop_get_value(&tmp_props[DRMMODE_PLANE_TYPE],
                                      props, DRMMODE_PLANE_TYPE__COUNT);
        if (type != DRMMODE_PLANE_TYPE_PRIMARY) {
            drmModeFreePlane(kplane);
            drmModeFreeObjectProperties(props);
            continue;
        }

        /* Check if plane is already on this CRTC */
        current_crtc = drmmode_prop_get_value(&tmp_props[DRMMODE_PLANE_CRTC_ID],
                                              props, 0);
        if (current_crtc == drmmode_crtc->mode_crtc->crtc_id) {
            if (best_plane) {
                drmModeFreePlane(best_kplane);
                drmmode_prop_info_free(drmmode_crtc->props_plane, DRMMODE_PLANE__COUNT);
            }
            best_plane = plane_id;
            best_kplane = kplane;
            blob_id = drmmode_prop_get_value(&tmp_props[DRMMODE_PLANE_IN_FORMATS],
                                             props, 0);
            drmmode_prop_info_copy(drmmode_crtc->props_plane, tmp_props,
                                   DRMMODE_PLANE__COUNT, 1);
            drmModeFreeObjectProperties(props);
            break;
        }

        if (!best_plane) {
            best_plane = plane_id;
            best_kplane = kplane;
            blob_id = drmmode_prop_get_value(&tmp_props[DRMMODE_PLANE_IN_FORMATS],
                                             props, 0);
            drmmode_prop_info_copy(drmmode_crtc->props_plane, tmp_props,
                                   DRMMODE_PLANE__COUNT, 1);
        } else {
            drmModeFreePlane(kplane);
        }

        drmModeFreeObjectProperties(props);
    }

    drmmode_crtc->plane_id = best_plane;
    if (best_kplane) {
        drmmode_crtc->num_formats = best_kplane->count_formats;
        drmmode_crtc->formats = calloc(sizeof(drmmode_format_rec),
                                       best_kplane->count_formats);
        if (!populate_format_modifiers(crtc, best_kplane, blob_id)) {
            for (i = 0; i < best_kplane->count_formats; i++)
                drmmode_crtc->formats[i].format = best_kplane->formats[i];
        }
        drmModeFreePlane(best_kplane);
    }

    drmmode_prop_info_free(tmp_props, DRMMODE_PLANE__COUNT);
    drmModeFreePlaneResources(kplane_res);
}

static unsigned int drmmode_crtc_init(ScrnInfoPtr pScrn,
                                      struct drmmode_rec * pDrmMode,
                                      drmModeResPtr mode_res,
                                      int num)
{
    xf86CrtcPtr pCrtc;
    struct drmmode_crtc_private_rec * drmmode_crtc;
    modesettingPtr ms = modesettingPTR(pScrn);
    drmModeObjectPropertiesPtr props;
    int devFD = pDrmMode->fd;
    uint32_t crtcID = mode_res->crtcs[num];
    int ret;

    pCrtc = xf86CrtcCreate(pScrn, &drmmode_crtc_funcs);
    if (pCrtc == NULL)
    {
        return 0;
    }

    xf86Msg(X_INFO, "\n");

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: mode_res->crtcs[%d] = %d\n",
                   __func__, num, crtcID);

    // the first parameter is num, the second parameter is size
    drmmode_crtc = xnfcalloc(1, sizeof(struct drmmode_crtc_private_rec));
    drmmode_crtc->mode_crtc = drmModeGetCrtc(devFD, crtcID);
    drmmode_crtc->drmmode = pDrmMode;
    drmmode_crtc->vblank_pipe = drmmode_crtc_vblank_pipe(num);
    pCrtc->driver_private = drmmode_crtc;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: vblank pipe: %d\n",
                   __func__, drmmode_crtc->vblank_pipe);

    xorg_list_init(&drmmode_crtc->mode_list);

    if (ms->atomic_modeset)
    {
        static const struct drmmode_prop_info_rec crtc_props[] = {
            [DRMMODE_CRTC_ACTIVE] = { .name = "ACTIVE" },
            [DRMMODE_CRTC_MODE_ID] = { .name = "MODE_ID" },
        };
        Bool res;

        props = drmModeObjectGetProperties(devFD, crtcID, DRM_MODE_OBJECT_CRTC);
        if (props == NULL)
        {
            xf86CrtcDestroy(pCrtc);
            return 0;
        }
        else
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: %u Properties\n",
                   __func__, props->count_props);
        }

        res = drmmode_prop_info_copy(drmmode_crtc->props, crtc_props,
                                     DRMMODE_CRTC__COUNT, FALSE);
        if (res == FALSE)
        {
            xf86CrtcDestroy(pCrtc);
            return 0;
        }
        else
        {

        }

        drmmode_prop_info_update(pDrmMode, drmmode_crtc->props,
                                 DRMMODE_CRTC__COUNT, props);
        drmModeFreeObjectProperties(props);
        drmmode_crtc_create_planes(pCrtc, num);
    }

    /* Hide any cursors which may be active from previous users */
    ret = drmModeSetCursor(devFD, drmmode_crtc->mode_crtc->crtc_id, 0, 0, 0);
    if (ret == 0)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "%s: Hide cursors success.\n", __func__);
    }

    /* Mark num'th crtc as in use on this device. */
    LS_MarkCrtcInUse(pScrn, num);

    xf86Msg(X_INFO, "\n");

    return 1;
}

/*
 * Update all of the property values for an output
 */
static void
drmmode_output_update_properties(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    int i, j, k;
    int err;
    drmModeConnectorPtr koutput;

    /* Use the most recently fetched values from the kernel */
    koutput = drmmode_output->mode_output;

    if (!koutput)
        return;

    for (i = 0; i < drmmode_output->num_props; i++) {
        drmmode_prop_ptr p = &drmmode_output->props[i];

        for (j = 0; koutput && j < koutput->count_props; j++) {
            if (koutput->props[j] == p->mode_prop->prop_id) {

                /* Check to see if the property value has changed */
                if (koutput->prop_values[j] != p->value) {

                    p->value = koutput->prop_values[j];

                    if (p->mode_prop->flags & DRM_MODE_PROP_RANGE) {
                        INT32 value = p->value;

                        err = RRChangeOutputProperty(output->randr_output, p->atoms[0],
                                                     XA_INTEGER, 32, PropModeReplace, 1,
                                                     &value, FALSE, TRUE);

                        if (err != 0) {
                            xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                                       "RRChangeOutputProperty error, %d\n", err);
                        }
                    }
                    else if (p->mode_prop->flags & DRM_MODE_PROP_ENUM) {
                        for (k = 0; k < p->mode_prop->count_enums; k++)
                            if (p->mode_prop->enums[k].value == p->value)
                                break;
                        if (k < p->mode_prop->count_enums) {
                            err = RRChangeOutputProperty(output->randr_output, p->atoms[0],
                                                         XA_ATOM, 32, PropModeReplace, 1,
                                                         &p->atoms[k + 1], FALSE, TRUE);
                            if (err != 0) {
                                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                                           "RRChangeOutputProperty error, %d\n", err);
                            }
                        }
                    }
                }
                break;
            }
        }
    }
}


xf86OutputStatus drmmode_output_detect(xf86OutputPtr output)
{
    /* go to the hw and retrieve a new output struct */
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    xf86OutputStatus status;

    if (drmmode_output->output_id == -1)
        return XF86OutputStatusDisconnected;

    drmModeFreeConnector(drmmode_output->mode_output);

    drmmode_output->mode_output =
        drmModeGetConnector(drmmode->fd, drmmode_output->output_id);

    if (!drmmode_output->mode_output) {
        drmmode_output->output_id = -1;
        return XF86OutputStatusDisconnected;
    }

    drmmode_output_update_properties(output);

    switch (drmmode_output->mode_output->connection) {
    case DRM_MODE_CONNECTED:
        status = XF86OutputStatusConnected;
        break;
    case DRM_MODE_DISCONNECTED:
        status = XF86OutputStatusDisconnected;
        break;
    default:
    case DRM_MODE_UNKNOWNCONNECTION:
        status = XF86OutputStatusUnknown;
        break;
    }
    return status;
}

static Bool
drmmode_output_mode_valid(xf86OutputPtr output, DisplayModePtr pModes)
{
    return MODE_OK;
}

int koutput_get_prop_idx(int fd, drmModeConnectorPtr koutput,
                         int type, const char *name)
{
    int idx = -1;
    unsigned int i;
    unsigned int nProps = koutput->count_props;

    for (i = 0; i < nProps; ++i)
    {
        uint32_t property_id = koutput->props[i];
        drmModePropertyPtr prop = drmModeGetProperty(fd, property_id);

        if (!prop)
            continue;

        if (drm_property_type_is(prop, type) && !strcmp(prop->name, name))
            idx = i;

        drmModeFreeProperty(prop);

        if (idx > -1)
            break;
    }

    return idx;
}


drmModePropertyBlobPtr koutput_get_prop_blob(int fd,
                                             drmModeConnectorPtr koutput,
                                             const char *name)
{
    drmModePropertyBlobPtr blob = NULL;
    int idx = koutput_get_prop_idx(fd, koutput, DRM_MODE_PROP_BLOB, name);

    if (idx > -1)
        blob = drmModeGetPropertyBlob(fd, koutput->prop_values[idx]);

    return blob;
}

static void drmmode_output_attach_tile(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmModeConnectorPtr koutput = drmmode_output->mode_output;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    struct xf86CrtcTileInfo tile_info, *set = NULL;

    if (!koutput) {
        xf86OutputSetTile(output, NULL);
        return;
    }

    drmModeFreePropertyBlob(drmmode_output->tile_blob);

    /* look for a TILE property */
    drmmode_output->tile_blob =
        koutput_get_prop_blob(drmmode->fd, koutput, "TILE");

    if (drmmode_output->tile_blob)
    {
        if (xf86OutputParseKMSTile(drmmode_output->tile_blob->data, 
               drmmode_output->tile_blob->length, &tile_info) == TRUE)
        {
            set = &tile_info;
        }
    }
    xf86OutputSetTile(output, set);
}

static Bool has_panel_fitter(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmModeConnectorPtr koutput = drmmode_output->mode_output;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    int idx;

    /* Presume that if the output supports scaling, then we have a
     * panel fitter capable of adjust any mode to suit.
     */
    idx = koutput_get_prop_idx(drmmode->fd, koutput,
            DRM_MODE_PROP_ENUM, "scaling mode");

    return (idx > -1);
}

static DisplayModePtr
drmmode_output_add_gtf_modes(xf86OutputPtr output, DisplayModePtr Modes)
{
    xf86MonPtr mon = output->MonInfo;
    DisplayModePtr i, m, preferred = NULL;
    int max_x = 0, max_y = 0;
    float max_vrefresh = 0.0;

    if (mon && GTF_SUPPORTED(mon->features.msc))
        return Modes;

    if (!has_panel_fitter(output))
        return Modes;

    for (m = Modes; m; m = m->next)
    {
        if (m->type & M_T_PREFERRED)
            preferred = m;
        max_x = max(max_x, m->HDisplay);
        max_y = max(max_y, m->VDisplay);
        max_vrefresh = max(max_vrefresh, xf86ModeVRefresh(m));
    }

    max_vrefresh = max(max_vrefresh, 60.0);
    max_vrefresh *= (1 + SYNC_TOLERANCE);

    m = xf86GetDefaultModes();
    xf86ValidateModesSize(output->scrn, m, max_x, max_y, 0);

    for (i = m; i; i = i->next) {
        if (xf86ModeVRefresh(i) > max_vrefresh)
            i->status = MODE_VSYNC;
        if (preferred &&
            i->HDisplay >= preferred->HDisplay &&
            i->VDisplay >= preferred->VDisplay &&
            xf86ModeVRefresh(i) >= xf86ModeVRefresh(preferred))
            i->status = MODE_VSYNC;
    }

    xf86PruneInvalidModes(output->scrn, &m, FALSE);

    return xf86ModesAdd(Modes, m);
}

static DisplayModePtr
drmmode_output_get_modes(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmModeConnectorPtr koutput = drmmode_output->mode_output;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    int i;
    DisplayModePtr Modes = NULL, Mode;
    xf86MonPtr mon = NULL;

    if (!koutput)
        return NULL;

    drmModeFreePropertyBlob(drmmode_output->edid_blob);

    /* look for an EDID property */
    drmmode_output->edid_blob =
        koutput_get_prop_blob(drmmode->fd, koutput, "EDID");

    if (drmmode_output->edid_blob) {
        mon = xf86InterpretEDID(output->scrn->scrnIndex,
                                drmmode_output->edid_blob->data);
        if (mon && drmmode_output->edid_blob->length > 128)
            mon->flags |= MONITOR_EDID_COMPLETE_RAWDATA;
    }
    xf86OutputSetEDID(output, mon);

    drmmode_output_attach_tile(output);

    /* modes should already be available */
    for (i = 0; i < koutput->count_modes; i++) {
        Mode = xnfalloc(sizeof(DisplayModeRec));

        drmmode_ConvertFromKMode(output->scrn, &koutput->modes[i], Mode);
        Modes = xf86ModesAdd(Modes, Mode);

    }

    return drmmode_output_add_gtf_modes(output, Modes);
}

static void
drmmode_output_destroy(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    int i;

    drmModeFreePropertyBlob(drmmode_output->edid_blob);
    drmModeFreePropertyBlob(drmmode_output->tile_blob);

    for (i = 0; i < drmmode_output->num_props; i++) {
        drmModeFreeProperty(drmmode_output->props[i].mode_prop);
        free(drmmode_output->props[i].atoms);
    }
    free(drmmode_output->props);
    if (drmmode_output->mode_output) {
        for (i = 0; i < drmmode_output->mode_output->count_encoders; i++) {
            drmModeFreeEncoder(drmmode_output->mode_encoders[i]);
        }
        drmModeFreeConnector(drmmode_output->mode_output);
    }
    free(drmmode_output->mode_encoders);
    free(drmmode_output);
    output->driver_private = NULL;
}

static void
drmmode_output_dpms(xf86OutputPtr output, int mode)
{
    modesettingPtr ms = modesettingPTR(output->scrn);
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    xf86CrtcPtr crtc = output->crtc;
    drmModeConnectorPtr koutput = drmmode_output->mode_output;

    if (!koutput)
        return;

    /* XXX Check if DPMS mode is already the right one */

    drmmode_output->dpms = mode;

    if (ms->atomic_modeset) {
        if (mode != DPMSModeOn && !ms->pending_modeset)
            drmmode_output_disable(output);
    } else {
        drmModeConnectorSetProperty(drmmode->fd, koutput->connector_id,
                                    drmmode_output->dpms_enum_id, mode);
    }

    if (crtc) {
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        if (mode == DPMSModeOn) {
            if (drmmode_crtc->need_modeset)
                drmmode_set_mode_major(crtc, &crtc->mode, crtc->rotation,
                                       crtc->x, crtc->y);

            if (drmmode_crtc->enable_flipping)
                drmmode_InitSharedPixmapFlipping(crtc, drmmode_crtc->drmmode);
        } else {
            if (drmmode_crtc->enable_flipping)
                drmmode_FiniSharedPixmapFlipping(crtc, drmmode_crtc->drmmode);
        }
    }

    return;
}

static Bool
drmmode_property_ignore(drmModePropertyPtr prop)
{
    if (!prop)
        return TRUE;
    /* ignore blob prop */
    if (prop->flags & DRM_MODE_PROP_BLOB)
        return TRUE;
    /* ignore standard property */
    if (!strcmp(prop->name, "EDID") || !strcmp(prop->name, "DPMS") ||
        !strcmp(prop->name, "CRTC_ID"))
        return TRUE;

    return FALSE;
}

void drmmode_output_create_resources(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmModeConnectorPtr mode_output = drmmode_output->mode_output;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    drmModePropertyPtr drmmode_prop;
    int i, j, err;

    drmmode_output->props =
        calloc(mode_output->count_props, sizeof(drmmode_prop_rec));
    if (!drmmode_output->props)
        return;

    drmmode_output->num_props = 0;
    for (i = 0, j = 0; i < mode_output->count_props; i++) {
        drmmode_prop = drmModeGetProperty(drmmode->fd, mode_output->props[i]);
        if (drmmode_property_ignore(drmmode_prop)) {
            drmModeFreeProperty(drmmode_prop);
            continue;
        }
        drmmode_output->props[j].mode_prop = drmmode_prop;
        drmmode_output->props[j].value = mode_output->prop_values[i];
        drmmode_output->num_props++;
        j++;
    }

    /* Create CONNECTOR_ID property */
    {
        Atom    name = MakeAtom("CONNECTOR_ID", 12, TRUE);
        INT32   value = mode_output->connector_id;

        if (name != BAD_RESOURCE) {
            err = RRConfigureOutputProperty(output->randr_output, name,
                                            FALSE, FALSE, TRUE,
                                            1, &value);
            if (err != 0) {
                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                           "RRConfigureOutputProperty error, %d\n", err);
            }
            err = RRChangeOutputProperty(output->randr_output, name,
                                         XA_INTEGER, 32, PropModeReplace, 1,
                                         &value, FALSE, FALSE);
            if (err != 0) {
                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                           "RRChangeOutputProperty error, %d\n", err);
            }
        }
    }

    for (i = 0; i < drmmode_output->num_props; i++) {
        drmmode_prop_ptr p = &drmmode_output->props[i];

        drmmode_prop = p->mode_prop;

        if (drmmode_prop->flags & DRM_MODE_PROP_RANGE) {
            INT32 prop_range[2];
            INT32 value = p->value;

            p->num_atoms = 1;
            p->atoms = calloc(p->num_atoms, sizeof(Atom));
            if (!p->atoms)
                continue;
            p->atoms[0] =
                MakeAtom(drmmode_prop->name, strlen(drmmode_prop->name), TRUE);
            prop_range[0] = drmmode_prop->values[0];
            prop_range[1] = drmmode_prop->values[1];
            err = RRConfigureOutputProperty(output->randr_output, p->atoms[0],
                                            FALSE, TRUE,
                                            drmmode_prop->
                                            flags & DRM_MODE_PROP_IMMUTABLE ?
                                            TRUE : FALSE, 2, prop_range);
            if (err != 0) {
                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                           "RRConfigureOutputProperty error, %d\n", err);
            }
            err = RRChangeOutputProperty(output->randr_output, p->atoms[0],
                                         XA_INTEGER, 32, PropModeReplace, 1,
                                         &value, FALSE, TRUE);
            if (err != 0) {
                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                           "RRChangeOutputProperty error, %d\n", err);
            }
        }
        else if (drmmode_prop->flags & DRM_MODE_PROP_ENUM) {
            p->num_atoms = drmmode_prop->count_enums + 1;
            p->atoms = calloc(p->num_atoms, sizeof(Atom));
            if (!p->atoms)
                continue;
            p->atoms[0] =
                MakeAtom(drmmode_prop->name, strlen(drmmode_prop->name), TRUE);
            for (j = 1; j <= drmmode_prop->count_enums; j++) {
                struct drm_mode_property_enum *e = &drmmode_prop->enums[j - 1];

                p->atoms[j] = MakeAtom(e->name, strlen(e->name), TRUE);
            }
            err = RRConfigureOutputProperty(output->randr_output, p->atoms[0],
                                            FALSE, FALSE,
                                            drmmode_prop->
                                            flags & DRM_MODE_PROP_IMMUTABLE ?
                                            TRUE : FALSE, p->num_atoms - 1,
                                            (INT32 *) &p->atoms[1]);
            if (err != 0) {
                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                           "RRConfigureOutputProperty error, %d\n", err);
            }
            for (j = 0; j < drmmode_prop->count_enums; j++)
                if (drmmode_prop->enums[j].value == p->value)
                    break;
            /* there's always a matching value */
            err = RRChangeOutputProperty(output->randr_output, p->atoms[0],
                                         XA_ATOM, 32, PropModeReplace, 1,
                                         &p->atoms[j + 1], FALSE, TRUE);
            if (err != 0) {
                xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                           "RRChangeOutputProperty error, %d\n", err);
            }
        }
    }
}

static Bool
drmmode_output_set_property(xf86OutputPtr output, Atom property,
                            RRPropertyValuePtr value)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    int i;

    for (i = 0; i < drmmode_output->num_props; i++) {
        drmmode_prop_ptr p = &drmmode_output->props[i];

        if (p->atoms[0] != property)
            continue;

        if (p->mode_prop->flags & DRM_MODE_PROP_RANGE) {
            uint32_t val;

            if (value->type != XA_INTEGER || value->format != 32 ||
                value->size != 1)
                return FALSE;
            val = *(uint32_t *) value->data;

            drmModeConnectorSetProperty(drmmode->fd, drmmode_output->output_id,
                                        p->mode_prop->prop_id, (uint64_t) val);
            return TRUE;
        }
        else if (p->mode_prop->flags & DRM_MODE_PROP_ENUM) {
            Atom atom;
            const char *name;
            int j;

            if (value->type != XA_ATOM || value->format != 32 ||
                value->size != 1)
                return FALSE;
            memcpy(&atom, value->data, 4);
            if (!(name = NameForAtom(atom)))
                return FALSE;

            /* search for matching name string, then set its value down */
            for (j = 0; j < p->mode_prop->count_enums; j++) {
                if (!strcmp(p->mode_prop->enums[j].name, name)) {
                    drmModeConnectorSetProperty(drmmode->fd,
                                                drmmode_output->output_id,
                                                p->mode_prop->prop_id,
                                                p->mode_prop->enums[j].value);
                    return TRUE;
                }
            }
        }
    }

    return TRUE;
}

static Bool
drmmode_output_get_property(xf86OutputPtr output, Atom property)
{
    return TRUE;
}


static uint32_t
find_clones(ScrnInfoPtr scrn, xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output =
        output->driver_private, clone_drmout;
    int i;
    xf86OutputPtr clone_output;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int index_mask = 0;

    if (drmmode_output->enc_clone_mask == 0)
        return index_mask;

    for (i = 0; i < xf86_config->num_output; i++) {
        clone_output = xf86_config->output[i];
        clone_drmout = clone_output->driver_private;
        if (output == clone_output)
            continue;

        if (clone_drmout->enc_mask == 0)
            continue;
        if (drmmode_output->enc_clone_mask == clone_drmout->enc_mask)
            index_mask |= (1 << i);
    }
    return index_mask;
}

static void
drmmode_clones_init(ScrnInfoPtr scrn, drmmode_ptr drmmode, drmModeResPtr mode_res)
{
    int i, j;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);

    for (i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];
        drmmode_output_private_ptr drmmode_output;

        drmmode_output = output->driver_private;
        drmmode_output->enc_clone_mask = 0xff;
        /* and all the possible encoder clones for this output together */
        for (j = 0; j < drmmode_output->mode_output->count_encoders; j++) {
            int k;

            for (k = 0; k < mode_res->count_encoders; k++) {
                if (mode_res->encoders[k] ==
                    drmmode_output->mode_encoders[j]->encoder_id)
                    drmmode_output->enc_mask |= (1 << k);
            }

            drmmode_output->enc_clone_mask &=
                drmmode_output->mode_encoders[j]->possible_clones;
        }
    }

    for (i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr output = xf86_config->output[i];

        output->possible_clones = find_clones(scrn, output);
    }
}

const xf86OutputFuncsRec drmmode_output_funcs = {
    .dpms = drmmode_output_dpms,
    .create_resources = drmmode_output_create_resources,
    .set_property = drmmode_output_set_property,
    .get_property = drmmode_output_get_property,
    .detect = drmmode_output_detect,
    .mode_valid = drmmode_output_mode_valid,

    .get_modes = drmmode_output_get_modes,
    .destroy = drmmode_output_destroy
};


void drmmode_validate_leases(ScrnInfoPtr scrn)
{
    ScreenPtr screen = scrn->pScreen;
    rrScrPrivPtr scr_priv;
    modesettingPtr ms = modesettingPTR(scrn);
    drmmode_ptr drmmode = &ms->drmmode;
    drmModeLesseeListPtr lessees;
    RRLeasePtr lease, next;
    int l;

    /* Bail out if RandR wasn't initialized. */
    if (!dixPrivateKeyRegistered(rrPrivKey))
        return;

    scr_priv = rrGetScrPriv(screen);

    /* We can't talk to the kernel about leases when VT switched */
    if (!scrn->vtSema)
        return;

    lessees = drmModeListLessees(drmmode->fd);
    if (!lessees)
        return;

    xorg_list_for_each_entry_safe(lease, next, &scr_priv->leases, list) {
        drmmode_lease_private_ptr lease_private = lease->devPrivate;

        for (l = 0; l < lessees->count; l++) {
            if (lessees->lessees[l] == lease_private->lessee_id)
                break;
        }

        /* check to see if the lease has gone away */
        if (l == lessees->count) {
            free(lease_private);
            lease->devPrivate = NULL;
            xf86CrtcLeaseTerminated(lease);
        }
    }

    free(lessees);
}


Bool drmmode_pre_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int cpp)
{
    int i;
    int ret;
    uint64_t value = 0;
    unsigned int crtcs_needed = 0;
    drmModeResPtr mode_res;
    int crtcshift;

    /* check for dumb capability */
    ret = drmGetCap(drmmode->fd, DRM_CAP_DUMB_BUFFER, &value);
    if (ret > 0 || value != 1)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "KMS doesn't support dumb interface\n");
        return FALSE;
    }

    xf86CrtcConfigInit(pScrn, &ls_xf86crtc_config_funcs);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "PreInit: byte per pixel = %d.\n", cpp);

    drmmode->scrn = pScrn;
    drmmode->cpp = cpp;

    mode_res = drmModeGetResources(drmmode->fd);
    if (NULL == mode_res)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "drmModeGetResources failed.\n");
        return FALSE;
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, " ----------------------------\n");

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, " Got KMS resources.\n");
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  %d Connectors, %d Encoders.\n",
                mode_res->count_connectors, mode_res->count_encoders);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  %d CRTCs, %d FBs.\n",
                mode_res->count_crtcs, mode_res->count_fbs);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  %dx%d minimum resolution.\n",
                mode_res->min_width, mode_res->min_height);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  %dx%d maximum resolution.\n",
                mode_res->max_width, mode_res->max_height);

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, " ----------------------------\n");
    }

    crtcshift = ffs( LS_GetAssignedCrtc(pScrn) ^ 0xffffffff ) - 1;

    for (i = 0; i < mode_res->count_connectors; i++)
    {
        crtcs_needed += drmmode_output_init(pScrn, drmmode, mode_res, i, FALSE,
                                            crtcshift);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "Up to %d crtcs needed for screen.\n", crtcs_needed);

    xf86CrtcSetSizeRange(pScrn, 320, 200, mode_res->max_width,
                         mode_res->max_height);

    for (i = 0; i < mode_res->count_crtcs; i++)
    {
        if (!xf86IsEntityShared(pScrn->entityList[0]) ||
            (crtcs_needed && !(LS_GetAssignedCrtc(pScrn) & (1 << i))))
            crtcs_needed -= drmmode_crtc_init(pScrn, drmmode, mode_res, i);
    }

    /* All ZaphodHeads outputs provided with matching crtcs? */
    if (xf86IsEntityShared(pScrn->entityList[0]) && (crtcs_needed > 0))
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "%d ZaphodHeads crtcs unavailable. Some outputs will stay off.\n",
                   crtcs_needed);

    /* workout clones */
    drmmode_clones_init(pScrn, drmmode, mode_res);

    drmModeFreeResources(mode_res);
    /* XF86_CRTC_VERSION >= 5 */
    xf86ProviderSetup(pScrn, NULL, "loongson");

    xf86InitialConfiguration(pScrn, TRUE);

    return TRUE;
}



void drmmode_adjust_frame(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int x, int y)
{
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
    xf86OutputPtr output = config->output[config->compat_output];
    xf86CrtcPtr crtc = output->crtc;

    xf86Msg(X_INFO, "%s: x = %d, y = %d\n", __func__, x, y);

    if (crtc && crtc->enabled)
    {
        drmmode_set_mode_major(crtc, &crtc->mode, crtc->rotation, x, y);
    }
}


Bool drmmode_set_desired_modes(ScrnInfoPtr pScrn,
                               drmmode_ptr drmmode,
                               Bool set_hw)
{
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
    int nCrtc = config->num_crtc;
    int c;

    xf86Msg(X_INFO, "\n");
    xf86Msg(X_INFO, "%s: %d crtc\n", __func__, nCrtc);

    for (c = 0; c < nCrtc; ++c)
    {
        xf86CrtcPtr crtc = config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;
        xf86OutputPtr output = NULL;
        int o;

        /* Skip disabled CRTCs */
        if (!crtc->enabled)
        {
            if (set_hw)
            {
                drmModeSetCrtc(drmmode->fd, drmmode_crtc->mode_crtc->crtc_id,
                               0, 0, 0, NULL, 0, NULL);
            }
            continue;
        }

        if (config->output[config->compat_output]->crtc == crtc)
            output = config->output[config->compat_output];
        else
        {
            for (o = 0; o < config->num_output; o++)
                if (config->output[o]->crtc == crtc)
                {
                    output = config->output[o];
                    break;
                }
        }
        /* paranoia */
        if (!output)
            continue;

        /* Mark that we'll need to re-set the mode for sure */
        memset(&crtc->mode, 0, sizeof(crtc->mode));
        if (!crtc->desiredMode.CrtcHDisplay) {
            DisplayModePtr mode =
                xf86OutputFindClosestMode(output, pScrn->currentMode);

            if (!mode)
                return FALSE;
            crtc->desiredMode = *mode;
            crtc->desiredRotation = RR_Rotate_0;
            crtc->desiredX = 0;
            crtc->desiredY = 0;
        }

        if (set_hw)
        {
            if (!crtc->funcs->
                set_mode_major(crtc, &crtc->desiredMode, crtc->desiredRotation,
                               crtc->desiredX, crtc->desiredY))
                return FALSE;
        }
        else
        {
            crtc->mode = crtc->desiredMode;
            crtc->rotation = crtc->desiredRotation;
            crtc->x = crtc->desiredX;
            crtc->y = crtc->desiredY;
            if (!xf86CrtcRotate(crtc))
                return FALSE;
        }
    }

    /* Validate leases on VT re-entry */
    drmmode_validate_leases(pScrn);

    xf86Msg(X_INFO, "\n");

    return TRUE;
}

static void drmmode_load_palette(ScrnInfoPtr pScrn, int numColors,
                     int *indices, LOCO * colors, VisualPtr pVisual)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    uint16_t lut_r[256], lut_g[256], lut_b[256];
    int index, j, i;
    int c;

    for (c = 0; c < xf86_config->num_crtc; c++) {
        xf86CrtcPtr crtc = xf86_config->crtc[c];
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        for (i = 0; i < 256; i++) {
            lut_r[i] = drmmode_crtc->lut_r[i] << 6;
            lut_g[i] = drmmode_crtc->lut_g[i] << 6;
            lut_b[i] = drmmode_crtc->lut_b[i] << 6;
        }

        switch (pScrn->depth) {
        case 15:
            for (i = 0; i < numColors; i++) {
                index = indices[i];
                for (j = 0; j < 8; j++) {
                    lut_r[index * 8 + j] = colors[index].red << 6;
                    lut_g[index * 8 + j] = colors[index].green << 6;
                    lut_b[index * 8 + j] = colors[index].blue << 6;
                }
            }
            break;
        case 16:
            for (i = 0; i < numColors; i++) {
                index = indices[i];

                if (i <= 31) {
                    for (j = 0; j < 8; j++) {
                        lut_r[index * 8 + j] = colors[index].red << 6;
                        lut_b[index * 8 + j] = colors[index].blue << 6;
                    }
                }

                for (j = 0; j < 4; j++) {
                    lut_g[index * 4 + j] = colors[index].green << 6;
                }
            }
            break;
        default:
            for (i = 0; i < numColors; i++) {
                index = indices[i];
                lut_r[index] = colors[index].red << 6;
                lut_g[index] = colors[index].green << 6;
                lut_b[index] = colors[index].blue << 6;
            }
            break;
        }

        /* Make the change through RandR */
        if (crtc->randr_crtc)
            RRCrtcGammaSet(crtc->randr_crtc, lut_r, lut_g, lut_b);
        else
            crtc->funcs->gamma_set(crtc, lut_r, lut_g, lut_b, 256);
    }
}

Bool drmmode_setup_colormap(ScreenPtr pScreen, ScrnInfoPtr pScrn)
{
    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
              "Initializing kms color map for depth %d, %d bpc.\n",
              pScrn->depth, pScrn->rgbBits);
    if (!miCreateDefColormap(pScreen))
    {
        return FALSE;
    }

    /* Adapt color map size and depth to color depth of screen. */
    if (!xf86HandleColormaps(pScreen, 1 << pScrn->rgbBits, 10,
                             drmmode_load_palette, NULL,
                             CMAP_PALETTED_TRUECOLOR |
                             CMAP_RELOAD_ON_MODE_SWITCH))
    {
        return FALSE;
    }

    return TRUE;
}



/* ugly workaround to see if we can create 32bpp */
void drmmode_get_default_bpp(ScrnInfoPtr pScrn, drmmode_ptr drmmode, int *depth,
                        int *bpp)
{
    drmModeResPtr mode_res;
    uint64_t value;
    struct dumb_bo *bo;
    uint32_t fb_id;
    int ret;

    /* 16 is fine */
    ret = drmGetCap(drmmode->fd, DRM_CAP_DUMB_PREFERRED_DEPTH, &value);
    if (!ret && (value == 16 || value == 8))
    {
        *depth = value;
        *bpp = value;
        return;
    }

    *depth = 24;
    mode_res = drmModeGetResources(drmmode->fd);
    if (!mode_res)
        return;

    // workaround kernel bug reporting 0x0 as valid mins
    // It reports these but then you can't create a 0 sized bo.
    if (mode_res->min_width == 0)
        mode_res->min_width = 1;
    if (mode_res->min_height == 0)
        mode_res->min_height = 1;
    /*create a bo */
    bo = dumb_bo_create(drmmode->fd, mode_res->min_width, mode_res->min_height,
                        32);
    if (!bo)
    {
        *bpp = 24;
        goto out;
    }

    ret = drmModeAddFB(drmmode->fd, mode_res->min_width, mode_res->min_height,
                       24, 32, bo->pitch, bo->handle, &fb_id);

    if (ret)
    {
        *bpp = 24;
        dumb_bo_destroy(drmmode->fd, bo);
        goto out;
    }

    drmModeRmFB(drmmode->fd, fb_id);
    *bpp = 32;

    dumb_bo_destroy(drmmode->fd, bo);
 out:
    drmModeFreeResources(mode_res);
    return;
}
