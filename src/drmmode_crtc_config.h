#ifndef DRMMODE_CRTC_CONFIG_H_
#define DRMMODE_CRTC_CONFIG_H_

#include <xf86drmMode.h>

extern const xf86CrtcConfigFuncsRec ls_xf86crtc_config_funcs;

Bool drmmode_xf86crtc_resize(ScrnInfoPtr pScrn, int width, int height);

#endif
