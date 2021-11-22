#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <xf86str.h>
#include <xf86Crtc.h>

#include "driver.h"
#include "loongson_options.h"
#include "drmmode_display.h"
#include "drmmode_output.h"

extern const xf86OutputFuncsRec drmmode_output_funcs;

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

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "%s finished.\n", __func__);

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

static int koutput_get_prop_id(int fd, drmModeConnectorPtr koutput,
                               int type, const char *name)
{
    int idx = koutput_get_prop_idx(fd, koutput, type, name);

    return (idx > -1) ? koutput->props[idx] : -1;
}


unsigned int drmmode_output_init(ScrnInfoPtr pScrn,
                                 struct drmmode_rec * const drmmode,
                                 drmModeResPtr mode_res,
                                 int num,
                                 Bool dynamic,
                                 int crtcshift)
{
    xf86OutputPtr output;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    modesettingPtr ms = modesettingPTR(pScrn);
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

    output = xf86OutputCreate(pScrn, &drmmode_output_funcs, name);
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
    modesettingPtr ms = modesettingPTR(output->scrn);
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
