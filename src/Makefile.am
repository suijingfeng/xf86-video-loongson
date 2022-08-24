#  Copyright © 2020 Loongson Corporation.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


# This is obnoxious:
# -module lets us name the module exactly how we want
# -avoid-version prevents gratuitous .0.0.0 version numbers on the end
# _ladir passes a dummy rpath to libtool so the thing will actually link
# TODO: -nostdlib/-Bstatic/-lgcc platform magic, not installing the .a, etc.

loongson_drv_la_LIBADD = $(LIBDRM_LIBS)

AM_CFLAGS = $(DIX_CFLAGS) $(XORG_CFLAGS) $(LIBDRM_CFLAGS) $(LIBUDEV_CFLAGS) $(CWARNFLAGS)

if LIBUDEV
loongson_drv_la_LIBADD += $(LIBUDEV_LIBS)
endif

loongson_drv_la_LTLIBRARIES = loongson_drv.la
loongson_drv_la_LDFLAGS = -module -avoid-version
loongson_drv_ladir = @moduledir@/drivers

loongson_drv_la_SOURCES = \
	 dri2.c \
	 driver.c \
	 driver.h \
	 drmmode_display.c \
	 drmmode_display.h \
	 drmmode_output.h \
	 drmmode_output.c \
	 drmmode_uevent.c \
	 drmmode_crtc_config.h \
	 drmmode_crtc_config.c \
	 dumb_bo.c \
	 dumb_bo.h \
	 box.h \
	 box.c \
	 present.c \
	 vblank.h \
	 vblank.c \
	 pageflip.c \
	 sprite.h \
	 sprite.c \
	 dri3.c \
	 etnaviv_dri3.c \
	 gsgpu_dri3.c \
	 fake_exa.c \
	 fake_exa.h \
	 loongson_exa.c \
	 loongson_exa.h \
	 loongson_buffer.h \
	 loongson_buffer.c \
	 loongson_pixmap.h \
	 loongson_pixmap.c \
	 loongson_helpers.h \
	 loongson_helpers.c \
	 loongson_probe.h \
	 loongson_probe.c \
	 loongson_cursor.h \
	 loongson_cursor.c \
	 loongson_glamor.h \
	 loongson_glamor.c \
	 loongson_shadow.h \
	 loongson_shadow.c \
	 loongson_entity.h \
	 loongson_entity.c \
	 loongson_options.h \
	 loongson_options.c \
	 loongson_debug.h \
	 loongson_debug.c \
	 loongson_scanout.h \
	 loongson_scanout.c \
	 loongson_prime.h \
	 loongson_prime.c \
	 loongson_randr.h \
	 loongson_randr.c \
	 loongson_module.c
	 $(NULL)
