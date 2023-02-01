/*
 * Copyright (C) 2022 Loongson Corporation
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdint.h>

#include "driver.h"
#include "drmmode_display.h"
#include "loongson_modeset.h"

static char *outputs_for_crtc(xf86CrtcPtr crtc, char *outputs, int max)
{
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(crtc->scrn);
    int len, i;

    for (i = len = 0; i < config->num_output; i++)
    {
        xf86OutputPtr output = config->output[i];

        if (output->crtc != crtc)
            continue;

        len += snprintf(outputs+len, max-len, "%s, ", output->name);
    }

    assert(len >= 2);
    outputs[len-2] = '\0';

    return outputs;
}

Bool loongson_set_desired_modes(ScrnInfoPtr pScrn,
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

            xf86Msg(X_INFO, "%s: CRTC-%d is not enabled\n", __func__, c);

            continue;
        }

        if (config->output[config->compat_output]->crtc == crtc)
        {
            output = config->output[config->compat_output];

            xf86Msg(X_INFO, "%s: config->compat_output=%d\n",
                            __func__, config->compat_output);
        }
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
        {
            xf86Msg(X_INFO, "%s: no output for CRTC-%d\n", __func__, c);
            continue;
        }


        /* Mark that we'll need to re-set the mode for sure */
        memset(&crtc->mode, 0, sizeof(crtc->mode));
        if (!crtc->desiredMode.CrtcHDisplay)
        {
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
            if (!crtc->funcs->set_mode_major(crtc,
                                             &crtc->desiredMode,
                                             crtc->desiredRotation,
                                             crtc->desiredX,
                                             crtc->desiredY))
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
