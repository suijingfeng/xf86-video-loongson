#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <malloc.h>

#include <drm_fourcc.h>
#include <drm_mode.h>

#include <xf86drm.h>
#include <xf86Crtc.h>
#include "drmmode_display.h"
#include <present.h>

#include "driver.h"

#include "loongson_options.h"
#include "loongson_entity.h"
#include "drmmode_output.h"

#ifdef HAVE_LIBUDEV

#define DRM_MODE_LINK_STATUS_GOOD       0
#define DRM_MODE_LINK_STATUS_BAD        1

static void drmmode_handle_uevents(int fd, void *closure)
{
    drmmode_ptr drmmode = closure;
    ScrnInfoPtr scrn = drmmode->scrn;
    struct udev_device *dev;
    drmModeResPtr mode_res;
    xf86CrtcConfigPtr  config = XF86_CRTC_CONFIG_PTR(scrn);
    int i, j;
    Bool found = FALSE;
    Bool changed = FALSE;

    while ((dev = udev_monitor_receive_device(drmmode->uevent_monitor))) {
        udev_device_unref(dev);
        found = TRUE;
    }
    if (!found)
        return;

    /* Try to re-set the mode on all the connectors with a BAD link-state:
     * This may happen if a link degrades and a new modeset is necessary, using
     * different link-training parameters. If the kernel found that the current
     * mode is not achievable anymore, it should have pruned the mode before
     * sending the hotplug event. Try to re-set the currently-set mode to keep
     * the display alive, this will fail if the mode has been pruned.
     * In any case, we will send randr events for the Desktop Environment to
     * deal with it, if it wants to.
     */
    for (i = 0; i < config->num_output; i++) {
        xf86OutputPtr output = config->output[i];
        drmmode_output_private_ptr drmmode_output = output->driver_private;

        drmmode_output_detect(output);

        /* Get an updated view of the properties for the current connector and
         * look for the link-status property
         */
        for (j = 0; j < drmmode_output->num_props; j++) {
            drmmode_prop_ptr p = &drmmode_output->props[j];

            if (!strcmp(p->mode_prop->name, "link-status")) {
                if (p->value == DRM_MODE_LINK_STATUS_BAD) {
                    xf86CrtcPtr crtc = output->crtc;
                    if (!crtc)
                        continue;

                    /* the connector got a link failure, re-set the current mode */
                    drmmode_set_mode_major(crtc, &crtc->mode, crtc->rotation,
                                           crtc->x, crtc->y);

                    xf86DrvMsg(scrn->scrnIndex, X_WARNING,
                               "hotplug event: connector %u's link-state is BAD, "
                               "tried resetting the current mode. You may be left"
                               "with a black screen if this fails...\n",
                               drmmode_output->mode_output->connector_id);
                }
                break;
            }
        }
    }

    mode_res = drmModeGetResources(drmmode->fd);
    if (!mode_res)
        goto out;

    if (mode_res->count_crtcs != config->num_crtc) {
        /* this triggers with Zaphod mode where we don't currently support connector hotplug or MST. */
        goto out_free_res;
    }

    /* figure out if we have gotten rid of any connectors
       traverse old output list looking for outputs */
    for (i = 0; i < config->num_output; i++) {
        xf86OutputPtr output = config->output[i];
        drmmode_output_private_ptr drmmode_output;

        drmmode_output = output->driver_private;
        found = FALSE;
        for (j = 0; j < mode_res->count_connectors; j++) {
            if (mode_res->connectors[j] == drmmode_output->output_id) {
                found = TRUE;
                break;
            }
        }
        if (found)
            continue;

        drmModeFreeConnector(drmmode_output->mode_output);
        drmmode_output->mode_output = NULL;
        drmmode_output->output_id = -1;

        changed = TRUE;
    }

    /* find new output ids we don't have outputs for */
    for (i = 0; i < mode_res->count_connectors; i++) {
        found = FALSE;

        for (j = 0; j < config->num_output; j++) {
            xf86OutputPtr output = config->output[j];
            drmmode_output_private_ptr drmmode_output;

            drmmode_output = output->driver_private;
            if (mode_res->connectors[i] == drmmode_output->output_id) {
                found = TRUE;
                break;
            }
        }
        if (found)
            continue;

        changed = TRUE;
        drmmode_output_init(scrn, drmmode, mode_res, i, TRUE, 0);
    }

    if (changed) {
        RRSetChanged(xf86ScrnToScreen(scrn));
        RRTellChanged(xf86ScrnToScreen(scrn));
    }

out_free_res:

    /* Check to see if a lessee has disappeared */
    drmmode_validate_leases(scrn);

    drmModeFreeResources(mode_res);
out:
    RRGetInfo(xf86ScrnToScreen(scrn), TRUE);
}

#undef DRM_MODE_LINK_STATUS_BAD
#undef DRM_MODE_LINK_STATUS_GOOD

#endif


void drmmode_uevent_init(ScrnInfoPtr scrn, drmmode_ptr drmmode)
{
#ifdef HAVE_LIBUDEV
    struct udev *u;
    struct udev_monitor *mon;

    u = udev_new();
    if (!u)
        return;
    mon = udev_monitor_new_from_netlink(u, "udev");
    if (!mon) {
        udev_unref(u);
        return;
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(mon,
                                                        "drm",
                                                        "drm_minor") < 0 ||
        udev_monitor_enable_receiving(mon) < 0)
    {
        udev_monitor_unref(mon);
        udev_unref(u);
        return;
    }

    drmmode->uevent_handler =
        xf86AddGeneralHandler(udev_monitor_get_fd(mon),
                              drmmode_handle_uevents, drmmode);

    drmmode->uevent_monitor = mon;
#endif
}



void drmmode_uevent_fini(ScrnInfoPtr scrn, drmmode_ptr drmmode)
{
#ifdef HAVE_LIBUDEV
    if (drmmode->uevent_handler)
    {
        struct udev *u = udev_monitor_get_udev(drmmode->uevent_monitor);

        xf86RemoveGeneralHandler(drmmode->uevent_handler);

        udev_monitor_unref(drmmode->uevent_monitor);
        udev_unref(u);
    }
#endif
}
