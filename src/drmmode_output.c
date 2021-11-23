#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <xf86str.h>
#include <xf86Crtc.h>

#include <X11/Xatom.h>
#include <xf86DDC.h>

#include "driver.h"
#include "loongson_options.h"
#include "drmmode_display.h"
#include "drmmode_output.h"


static int subpixel_conv_table[7] = {
    0,
    SubPixelUnknown,
    SubPixelHorizontalRGB,
    SubPixelHorizontalBGR,
    SubPixelVerticalRGB,
    SubPixelVerticalBGR,
    SubPixelNone
};


static const char *const output_names[] = {
    "None",
    "VGA",
    "DVI-I",
    "DVI-D",
    "DVI-A",
    "Composite",
    "SVIDEO",
    "LVDS",
    "Component",
    "DIN",
    "DP",
    "HDMI",
    "HDMI-B",
    "TV",
    "eDP",
    "Virtual",
    "DSI",
    "DPI",
};


static xf86OutputPtr find_output(ScrnInfoPtr pScrn, int id)
{
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    const int nOutput = xf86_config->num_output;
    int i;

    for (i = 0; i < nOutput; ++i)
    {
        xf86OutputPtr pOutput = xf86_config->output[i];
        drmmode_output_private_ptr drmmode_output = pOutput->driver_private;

        if (drmmode_output->output_id == id)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "%s: matched output(id=%d) found.\n",
                       __func__, id);
            return pOutput;
        }
    }

    return NULL;
}


static int parse_path_blob(drmModePropertyBlobPtr path_blob,
                           int *conn_base_id,
                           char **path)
{
    char *conn;
    char conn_id[5];
    int id, len;
    char *blob_data;

    xf86Msg(X_INFO, "%s,%d: Entered.\n", __func__, __LINE__);

    if (!path_blob)
        return -1;

    blob_data = path_blob->data;
    /* we only handle MST paths for now */
    if (strncmp(blob_data, "mst:", 4))
        return -1;

    conn = strchr(blob_data + 4, '-');
    if (!conn)
        return -1;
    len = conn - (blob_data + 4);
    if (len + 1> 5)
        return -1;
    memcpy(conn_id, blob_data + 4, len);
    conn_id[len] = '\0';
    id = strtoul(conn_id, NULL, 10);

    *conn_base_id = id;

    *path = conn + 1;

    xf86Msg(X_INFO, "%s,%d: finished.\n", __func__, __LINE__);
    return 0;
}


static void drmmode_create_name(ScrnInfoPtr pScrn,
                                drmModeConnectorPtr koutput,
                                char *name,
                                drmModePropertyBlobPtr path_blob)
{
    int ret;
    char *extra_path;
    int conn_id;
    xf86OutputPtr output;

    ret = parse_path_blob(path_blob, &conn_id, &extra_path);
    if (ret == -1)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Parse path blob failed, will fallback.\n");

        goto fallback;
    }

    output = find_output(pScrn, conn_id);
    if (output == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Can not find matched output(id=%d), will fallback.\n",
                    conn_id);
        goto fallback;
    }

    snprintf(name, 32, "%s-%s", output->name, extra_path);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s: name=%s.\n", __func__, name);

    return;

 fallback:
    if (koutput->connector_type >= ARRAY_SIZE(output_names))
        snprintf(name, 32, "Unknown%d-%d", koutput->connector_type, koutput->connector_type_id);
    else if (pScrn->is_gpu)
        snprintf(name, 32, "%s-%d-%d", output_names[koutput->connector_type], pScrn->scrnIndex - GPU_SCREEN_OFFSET + 1, koutput->connector_type_id);
    else
        snprintf(name, 32, "%s-%d", output_names[koutput->connector_type], koutput->connector_type_id);
}



static Bool drmmode_zaphod_string_matches(ScrnInfoPtr scrn,
                                          const char *s,
                                          char *output_name)
{
    char **token = xstrtokenize(s, ", \t\n\r");
    Bool ret = FALSE;
    int i = 0;

    if (!token)
        return FALSE;

    for (i = 0; token[i]; ++i)
    {
        if (strcmp(token[i], output_name) == 0)
            ret = TRUE;

        free(token[i]);
    }

    free(token);

    return ret;
}


static int koutput_get_prop_idx(int fd,
                               drmModeConnectorPtr koutput,
                               int type,
                               const char *name)
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


static int koutput_get_prop_id(int fd,
                               drmModeConnectorPtr koutput,
                               int type,
                               const char *name)
{
    int idx = koutput_get_prop_idx(fd, koutput, type, name);

    return (idx > -1) ? koutput->props[idx] : -1;
}


static void drmmode_output_dpms(xf86OutputPtr output, int mode)
{
    loongsonPtr lsp = loongsonPTR(output->scrn);
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    xf86CrtcPtr crtc = output->crtc;
    drmModeConnectorPtr koutput = drmmode_output->mode_output;

    if (!koutput)
        return;

    /* XXX Check if DPMS mode is already the right one */

    drmmode_output->dpms = mode;

    if (lsp->atomic_modeset)
    {
        if (mode != DPMSModeOn && !lsp->pending_modeset)
            drmmode_output_disable(output);
    }
    else
    {
        drmModeConnectorSetProperty(drmmode->fd, koutput->connector_id,
                                    drmmode_output->dpms_enum_id, mode);
    }

    if (crtc)
    {
        drmmode_crtc_private_ptr drmmode_crtc = crtc->driver_private;

        if (mode == DPMSModeOn) {
            if (drmmode_crtc->need_modeset)
                drmmode_set_mode_major(crtc, &crtc->mode, crtc->rotation,
                                       crtc->x, crtc->y);
        }

        if (drmmode_crtc->enable_flipping)
            drmmode_InitSharedPixmapFlipping(crtc, drmmode_crtc->drmmode);
    }

    return;
}



static Bool drmmode_property_ignore(drmModePropertyPtr prop)
{
    if (!prop)
        return TRUE;
    /* ignore blob prop */
    if (prop->flags & DRM_MODE_PROP_BLOB)
        return TRUE;
    /* ignore standard property */
    if (!strcmp(prop->name, "EDID") ||
        !strcmp(prop->name, "DPMS") ||
        !strcmp(prop->name, "CRTC_ID"))
    {
        return TRUE;
    }

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


static Bool drmmode_output_set_property(xf86OutputPtr output,
                                        Atom property,
                                        RRPropertyValuePtr value)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    drmmode_ptr drmmode = drmmode_output->drmmode;
    int i;

    for (i = 0; i < drmmode_output->num_props; i++)
    {
        drmmode_prop_ptr p = &drmmode_output->props[i];

        if (p->atoms[0] != property)
            continue;

        if (p->mode_prop->flags & DRM_MODE_PROP_RANGE)
        {
            uint32_t val;

            if (value->type != XA_INTEGER || value->format != 32 ||
                value->size != 1)
                return FALSE;
            val = *(uint32_t *) value->data;

            drmModeConnectorSetProperty(drmmode->fd, drmmode_output->output_id,
                                        p->mode_prop->prop_id, (uint64_t) val);
            return TRUE;
        }
        else if (p->mode_prop->flags & DRM_MODE_PROP_ENUM)
        {
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


static Bool drmmode_output_get_property(xf86OutputPtr output, Atom property)
{
    return TRUE;
}


/*
 * Update all of the property values for an output
 */
static void drmmode_output_update_properties(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    int i, j, k;
    int err;
    drmModeConnectorPtr koutput;

    /* Use the most recently fetched values from the kernel */
    koutput = drmmode_output->mode_output;

    if (!koutput)
        return;

    for (i = 0; i < drmmode_output->num_props; i++)
    {
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

    if (!drmmode_output->mode_output)
    {
        drmmode_output->output_id = -1;
        return XF86OutputStatusDisconnected;
    }

    drmmode_output_update_properties(output);

    switch (drmmode_output->mode_output->connection)
    {
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


static Bool drmmode_output_mode_valid(xf86OutputPtr output,
                                      DisplayModePtr pModes)
{
    return MODE_OK;
}


static void drmmode_ConvertFromKMode(ScrnInfoPtr scrn,
                                     drmModeModeInfo * kmode,
                                     DisplayModePtr mode)
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

static DisplayModePtr drmmode_output_add_gtf_modes(xf86OutputPtr output,
                                                   DisplayModePtr Modes)
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



static drmModePropertyBlobPtr koutput_get_prop_blob(int fd,
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
        xf86Msg(X_INFO, "HAVE TILE BLOB\n");

        if (xf86OutputParseKMSTile(drmmode_output->tile_blob->data,
               drmmode_output->tile_blob->length, &tile_info) == TRUE)
        {
            set = &tile_info;
        }
    }
    xf86OutputSetTile(output, set);
}


static DisplayModePtr drmmode_output_get_modes(xf86OutputPtr output)
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

    if (drmmode_output->edid_blob)
    {
        xf86Msg(X_INFO, "\n");
        xf86Msg(X_INFO, "HAVE EDID BLOB, SCREEN-%d\n",
                        output->scrn->scrnIndex);

        mon = xf86InterpretEDID(output->scrn->scrnIndex,
                                drmmode_output->edid_blob->data);
        if (mon && drmmode_output->edid_blob->length > 128)
            mon->flags |= MONITOR_EDID_COMPLETE_RAWDATA;
    }

    xf86OutputSetEDID(output, mon);

    drmmode_output_attach_tile(output);

    /* modes should already be available */
    for (i = 0; i < koutput->count_modes; i++)
    {
        Mode = xnfalloc(sizeof(DisplayModeRec));

        drmmode_ConvertFromKMode(output->scrn, &koutput->modes[i], Mode);
        Modes = xf86ModesAdd(Modes, Mode);

    }

    return drmmode_output_add_gtf_modes(output, Modes);
}



static void drmmode_output_destroy(xf86OutputPtr output)
{
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    int i;

    drmModeFreePropertyBlob(drmmode_output->edid_blob);
    drmModeFreePropertyBlob(drmmode_output->tile_blob);

    for (i = 0; i < drmmode_output->num_props; i++)
    {
        drmModeFreeProperty(drmmode_output->props[i].mode_prop);
        free(drmmode_output->props[i].atoms);
    }

    free(drmmode_output->props);

    if (drmmode_output->mode_output)
    {
        for (i = 0; i < drmmode_output->mode_output->count_encoders; i++)
        {
            drmModeFreeEncoder(drmmode_output->mode_encoders[i]);
        }
        drmModeFreeConnector(drmmode_output->mode_output);
    }

    free(drmmode_output->mode_encoders);
    free(drmmode_output);
    output->driver_private = NULL;
}



static const xf86OutputFuncsRec loongson_output_funcs = {
    .dpms = drmmode_output_dpms,
    .create_resources = drmmode_output_create_resources,
    .set_property = drmmode_output_set_property,
    .get_property = drmmode_output_get_property,
    .detect = drmmode_output_detect,
    .mode_valid = drmmode_output_mode_valid,

    .get_modes = drmmode_output_get_modes,
    .destroy = drmmode_output_destroy
};



unsigned int drmmode_output_init(ScrnInfoPtr pScrn,
                                 struct drmmode_rec * const drmmode,
                                 drmModeResPtr mode_res,
                                 int num,
                                 Bool dynamic,
                                 int crtcshift)
{
    xf86OutputPtr output;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    loongsonPtr ms = loongsonPTR(pScrn);
    drmModeConnectorPtr koutput;
    drmModeEncoderPtr *kencoders = NULL;
    drmmode_output_private_ptr drmmode_output;
    char name[32];
    int i;
    Bool nonDesktop = FALSE;
    drmModePropertyBlobPtr path_blob = NULL;
    const char *s;
    drmModeObjectPropertiesPtr props;

    static const struct drmmode_prop_info_rec connector_props[] =
    {
        [DRMMODE_CONNECTOR_CRTC_ID] = { .name = "CRTC_ID", },
    };

    xf86Msg(X_INFO, "\n");
    xf86Msg(X_INFO, "------------- output %d ----------\n", num);

    xf86Msg(X_INFO, "is dynamic: %s\n", dynamic ? "Yes" : "No");

    xf86Msg(X_INFO, "connector id: %u\n", mode_res->connectors[num]);

    koutput = drmModeGetConnector(drmmode->fd, mode_res->connectors[num]);
    if (!koutput)
        return 0;

    path_blob = koutput_get_prop_blob(drmmode->fd, koutput, "PATH");
    i = koutput_get_prop_idx(drmmode->fd, koutput, DRM_MODE_PROP_RANGE, RR_PROPERTY_NON_DESKTOP);
    if (i >= 0)
        nonDesktop = koutput->prop_values[i] != 0;

    xf86Msg(X_INFO, "Non Desktop: %s\n", nonDesktop ? "Yes" : "No");

    drmmode_create_name(pScrn, koutput, name, path_blob);

    if (path_blob)
        drmModeFreePropertyBlob(path_blob);

    if (path_blob && dynamic)
    {
        /* see if we have an output with this name already
           and hook stuff up */
        for (i = 0; i < xf86_config->num_output; i++)
        {
            output = xf86_config->output[i];

            if (strncmp(output->name, name, 32))
                continue;

            drmmode_output = output->driver_private;
            drmmode_output->output_id = mode_res->connectors[num];
            drmmode_output->mode_output = koutput;
            output->non_desktop = nonDesktop;
            return 1;
        }
    }

    kencoders = calloc(sizeof(drmModeEncoderPtr), koutput->count_encoders);
    if (!kencoders)
    {
        goto out_free_encoders;
    }

    for (i = 0; i < koutput->count_encoders; i++)
    {
        kencoders[i] = drmModeGetEncoder(drmmode->fd, koutput->encoders[i]);
        if (!kencoders[i])
        {
            goto out_free_encoders;
        }
    }

    if (xf86IsEntityShared(pScrn->entityList[0])) {
        if ((s = xf86GetOptValString(drmmode->Options, OPTION_ZAPHOD_HEADS))) {
            if (!drmmode_zaphod_string_matches(pScrn, s, name))
                goto out_free_encoders;
        } else {
            if (!drmmode->is_secondary && (num != 0))
                goto out_free_encoders;
            else if (drmmode->is_secondary && (num != 1))
                goto out_free_encoders;
        }
    }

    output = xf86OutputCreate(pScrn, &loongson_output_funcs, name);
    if (!output) {
        goto out_free_encoders;
    }

    drmmode_output = calloc(sizeof(drmmode_output_private_rec), 1);
    if (!drmmode_output) {
        xf86OutputDestroy(output);
        goto out_free_encoders;
    }

    drmmode_output->output_id = mode_res->connectors[num];
    drmmode_output->mode_output = koutput;
    drmmode_output->mode_encoders = kencoders;
    drmmode_output->drmmode = drmmode;
    output->mm_width = koutput->mmWidth;
    output->mm_height = koutput->mmHeight;

    output->subpixel_order = subpixel_conv_table[koutput->subpixel];
    output->interlaceAllowed = TRUE;
    output->doubleScanAllowed = TRUE;
    output->driver_private = drmmode_output;
    output->non_desktop = nonDesktop;

    output->possible_crtcs = 0;
    for (i = 0; i < koutput->count_encoders; i++)
    {
        output->possible_crtcs |= (kencoders[i]->possible_crtcs >> crtcshift) & 0x7f;
    }
    /* work out the possible clones later */
    output->possible_clones = 0;

    if (ms->atomic_modeset)
    {
        if (!drmmode_prop_info_copy(drmmode_output->props_connector,
                                    connector_props, DRMMODE_CONNECTOR__COUNT,
                                    0))
        {
            goto out_free_encoders;
        }

        props = drmModeObjectGetProperties(drmmode->fd,
                                           drmmode_output->output_id,
                                           DRM_MODE_OBJECT_CONNECTOR);

        drmmode_prop_info_update(drmmode, drmmode_output->props_connector,
                                 DRMMODE_CONNECTOR__COUNT, props);
    }
    else
    {
        drmmode_output->dpms_enum_id =
            koutput_get_prop_id(drmmode->fd, koutput, DRM_MODE_PROP_ENUM,
                                "DPMS");

         xf86Msg(X_INFO, "dpms enum id = %d\n", drmmode_output->dpms_enum_id);
    }

    if (dynamic)
    {
        output->randr_output = RROutputCreate(xf86ScrnToScreen(pScrn),
                                 output->name, strlen(output->name), output);
        if (output->randr_output)
        {
            drmmode_output_create_resources(output);
            RRPostPendingProperties(output->randr_output);
        }
    }

    xf86Msg(X_INFO, "-------------- -------- ------------\n");
    xf86Msg(X_INFO, "\n");

    return 1;

 out_free_encoders:
    if (kencoders) {
        for (i = 0; i < koutput->count_encoders; i++)
            drmModeFreeEncoder(kencoders[i]);
        free(kencoders);
    }
    drmModeFreeConnector(koutput);

    xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s aborted.\n", __func__);
    return 0;
}

int drmmode_output_disable(xf86OutputPtr output)
{
    loongsonPtr ms = loongsonPTR(output->scrn);
    drmmode_output_private_ptr drmmode_output = output->driver_private;
    xf86CrtcPtr crtc = drmmode_output->current_crtc;
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    int ret = 0;

    assert(ms->atomic_modeset);

    if (!req)
        return 1;

    ret |= connector_add_prop(req, drmmode_output,
                              DRMMODE_CONNECTOR_CRTC_ID, 0);
    if (crtc)
        ret |= crtc_add_dpms_props(req, crtc, DPMSModeOff, NULL);

    if (ret == 0)
        ret = drmModeAtomicCommit(ms->fd, req, flags, NULL);

    if (ret == 0)
        drmmode_output->current_crtc = NULL;

    drmModeAtomicFree(req);
    return ret;
}
