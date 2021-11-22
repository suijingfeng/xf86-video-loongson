#ifndef DRMMODE_OUTPUT_H_
#define DRMMODE_OUTPUT_H_

#include <unistd.h>
#include <xf86str.h>
#include <xf86Crtc.h>


unsigned int drmmode_output_init(ScrnInfoPtr pScrn, drmmode_ptr drmmode,
	drmModeResPtr mode_res, int num, Bool dynamic, int crtcshift);

int drmmode_output_disable(xf86OutputPtr output);
#endif
