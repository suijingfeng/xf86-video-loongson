#  Copyright (C) 2020 Loongson Corporation.
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

loongson_drv_la_LIBADD = $(LIBDRM_LIBS) $(LIBDRM_ETNAVIV_LIBS) $(LIBDRM_GSGPU_LIBS)

AM_CFLAGS = @XORG_CFLAGS@ \
            @LIBDRM_CFLAGS@ \
            @LIBDRM_ETNAVIV_CFLAGS@ \
            @LIBDRM_GSGPU_CFLAGS@ \
            @LIBUDEV_CFLAGS@ \
            @CWARNFLAGS@

if LIBUDEV
loongson_drv_la_LIBADD += $(LIBUDEV_LIBS)
endif


loongson_drv_la_LTLIBRARIES = loongson_drv.la
loongson_drv_la_LDFLAGS = -module -avoid-version -no-undefined
loongson_drv_ladir = @moduledir@/drivers

noinst_LTLIBRARIES =

loongson_drv_la_SOURCES = \
	 loongson_dri2.h \
	 loongson_dri2.c \
	 loongson_damage.h \
	 loongson_damage.c \
	 driver.c \
	 driver.h \
	 drmmode_display.c \
	 drmmode_display.h \
	 drmmode_output.h \
	 drmmode_output.c \
	 drmmode_uevent.c \
	 drmmode_crtc_config.h \
	 drmmode_crtc_config.c \
	 loongson_modeset.h \
	 loongson_modeset.c \
	 loongson_rotation.h \
	 loongson_rotation.c \
	 dumb_bo.c \
	 dumb_bo.h \
	 box.h \
	 box.c \
	 write_bmp.c \
	 write_bmp.h \
	 present.c \
	 vblank.h \
	 vblank.c \
	 pageflip.c \
	 sprite.h \
	 sprite.c \
	 loongson_dri3.h \
	 loongson_dri3.c \
	 fake_exa.c \
	 fake_exa.h \
	 common.xml.h \
	 loongson_blt.c \
	 loongson_blt.h \
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
	 loongson_module.c \
	 $(NULL)

if HAVE_LIBDRM_ETNAVIV
loongson_drv_la_SOURCES += \
	 etnaviv_device.c \
	 etnaviv_device.h \
	 etnaviv_dri3.c \
	 etnaviv_dri3.h \
	 etnaviv_exa.c \
	 etnaviv_exa.h \
	 etnaviv_resolve_generic.c \
	 etnaviv_resolve.h
	 $(NULL)
endif

if HAVE_LIBDRM_GSGPU
loongson_drv_la_SOURCES += \
	 gsgpu_device.c \
	 gsgpu_device.h \
	 gsgpu_dri2.c \
	 gsgpu_dri2.h \
	 gsgpu_dri3.c \
	 gsgpu_dri3.h \
	 gsgpu_exa.c \
	 gsgpu_exa.h \
	 gsgpu_bo_helper.c \
	 gsgpu_bo_helper.h \
	 $(NULL)
endif

libloongson_drv_lsx_la_SOURCES =

# loongarch lsx common code
if HAVE_LSX
noinst_LTLIBRARIES += libloongson_drv_lsx.la
libloongson_drv_lsx_la_SOURCES += lsx_blt.c lsx_blt.h
endif

if HAVE_LIBDRM_GSGPU
libloongson_drv_lsx_la_SOURCES += gsgpu_resolve.c gsgpu_resolve.h
endif

if HAVE_LIBDRM_ETNAVIV
libloongson_drv_lsx_la_SOURCES += etnaviv_resolve_lsx.c
endif

if HAVE_LSX
libloongson_drv_lsx_la_CFLAGS = $(LSX_CFLAGS)
loongson_drv_la_LDFLAGS += $(LSX_LDFLAGS)
loongson_drv_la_LIBADD += libloongson_drv_lsx.la
endif

# loongarch lasx code
if HAVE_LASX
noinst_LTLIBRARIES += libloongson_drv_lasx.la
libloongson_drv_lasx_la_SOURCES = lasx_blt.c lasx_blt.h
libloongson_drv_lasx_la_CFLAGS = $(LASX_CFLAGS)
loongson_drv_la_LDFLAGS += $(LASX_LDFLAGS)
loongson_drv_la_LIBADD += libloongson_drv_lasx.la
endif

# mips msa code
if HAVE_MSA
noinst_LTLIBRARIES += libloongson_drv_msa.la
libloongson_drv_msa_la_SOURCES = etnaviv_resolve_msa.c
libloongson_drv_msa_la_CFLAGS = $(MSA_CFLAGS)
loongson_drv_la_LDFLAGS += $(MSA_LDFLAGS)
loongson_drv_la_LIBADD += libloongson_drv_msa.la
endif


if HAVE_DOT_GIT
.PHONY: git_version.h
git_version.h:
	echo static const char git_version[] = \"$(shell git --git-dir=$(top_srcdir)/.git rev-parse HEAD)\"\; > git_version.h

driver.c: git_version.h
endif
