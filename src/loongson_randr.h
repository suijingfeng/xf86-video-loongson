#ifndef LOONGSON_RANDR_H_
#define LOONGSON_RANDR_H_


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

void LS_InitRandR(ScreenPtr pScreen);
Bool drmmode_set_target_scanout_pixmap(xf86CrtcPtr crtc,
                                       PixmapPtr ppix,
                                       PixmapPtr *target);

void drmmode_FiniSharedPixmapFlipping(xf86CrtcPtr crtc, drmmode_ptr drmmode);
#endif
