#  Copyright 2005 Adam Jackson.
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
#
# Process this file with autoconf to produce a configure script

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([xf86-video-loongson],
        [0.2.0],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
        [xf86-video-loongson])

AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(.)

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])

XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

AM_MAINTAINER_MODE

# Initialize libtool
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_SYS_LARGEFILE

AC_CHECK_HEADERS([sys/ioctl.h])
AC_CHECK_HEADERS([stdint.h])

AH_TOP([#include "xorg-server.h"])

# Are we in a git checkout?
dot_git=no
if test -e .git; then
	AC_DEFINE(HAVE_DOT_GIT, 1, [Are we in a git checkout?])
	dot_git=yes
fi
AM_CONDITIONAL(HAVE_DOT_GIT, test "x$dot_git" = "xyes")

# If so, we include the git description in our logs for sanity checking.
#
# However, for people not running their own drivers this is just noise.
# So we copy the xserver's builderstring idiom to allow for this to be
# overridden and perhaps replaced with something more useful.
AC_ARG_WITH(builderstring,
	    AS_HELP_STRING([--with-builderstring=BUILDERSTRING],
	                   [Additional builder string (default: use git describe)]),
	    [BUILDERSTRING="$withval"],
	    [BUILDERSTRING="x-magic-git-describe"])

if test "x$BUILDERSTRING" = "xx-magic-git-describe" -a "x$dot_git" = "xyes"; then
	AC_DEFINE(USE_GIT_DESCRIBE, 1, [Use automagic builder description])
else
  if test "x$BUILDERSTRING" != x -a "x$BUILDERSTRING" != "xno" -a "x$BUILDERSTRING" != xx-magic-git-describe; then
	  AC_DEFINE_UNQUOTED(BUILDER_DESCRIPTION, ["$BUILDERSTRING"], [Builder description])
  fi
fi

dnl ===========================================================================
dnl Check for MIPS SIMD Architecture(MSA)

if test "x$MSA_CFLAGS" = "x" ; then
    MSA_CFLAGS="-mmsa"
fi

have_mips_msa=no
AC_MSG_CHECKING(whether to enable Mips MSA assembler)

msa_save_CFLAGS=$CFLAGS
CFLAGS=" $MSA_CFLAGS $CFLAGS -I$srcdir"
AC_LINK_IFELSE([AC_LANG_SOURCE([[
#if !(defined(__mips__) &&  __mips_isa_rev >= 2)
#error "MIPS SIMD Architecture are only available on MIPS"
#endif
#if defined(__GNUC__) && (__GNUC__ < 7 )
#error "Need GCC >= 7 for MSA compilation"
#endif
#include <msa.h>
int
main ()
{
    v16i8 a = {0}, b = {0};
    a = __msa_addvi_b(b,1);
    return 0;
}]])], have_mips_msa=yes)
CFLAGS=$msa_save_CFLAGS

AC_ARG_ENABLE(msa,
   [AC_HELP_STRING([--disable-msa],
                   [disable use Mips MSA SIMD])],
                   [enable_mips_msa=$enableval],
                   [enable_mips_msa=auto])

if test $enable_mips_msa = no ; then
   have_mips_msa=disabled
fi

if test $have_mips_msa = yes ; then
   AC_DEFINE(HAVE_MSA, 1, [have MIPS SIMD])
else
   MSA_CFLAGS=
fi

AC_MSG_RESULT($have_mips_msa)
if test $enable_mips_msa = yes && test $have_mips_msa = no ; then
   AC_MSG_ERROR([Mips MSA not detected])
fi

AM_CONDITIONAL(HAVE_MSA, test $have_mips_msa = yes)

AC_SUBST(MSA_CFLAGS)
AC_SUBST(MSA_LDFLAGS)

dnl ===========================================================================
# Check for LoongArch LASX SIMD

if test "x$LASX_CFLAGS" = "x" ; then
    LASX_CFLAGS="-mlasx"
fi

have_lasx=no
AC_MSG_CHECKING(whether to enable LoongArch LASX assembler)

lasx_save_CFLAGS=$CFLAGS
CFLAGS=" $LASX_CFLAGS $CFLAGS -I$srcdir"
AC_LINK_IFELSE([AC_LANG_SOURCE([[
#if !(defined(__loongarch__))
#error "LASX are only available on loongson architecture."
#endif
int main ()
{
    asm volatile("xvadd.w \$xr0, \$xr1, \$xr2");
    return 0;
}]])], have_lasx=yes)
CFLAGS=$lasx_save_CFLAGS

AC_ARG_ENABLE(lasx,
   [AC_HELP_STRING([--disable-lasx],
                   [disable LoongArch LASX SIMD])],
   [enable_lasx=$enableval], [enable_lasx=auto])

if test $enable_lasx = no ; then
   have_lasx=disabled
fi

if test $have_lasx = yes ; then
   AC_DEFINE(HAVE_LASX, 1, [HAVE LoongArch LASX SIMD Intrinsic])
else
   LASX_CFLAGS=
fi

AC_MSG_RESULT($have_lasx)
if test $enable_lasx = yes && test $have_lasx = no ; then
   AC_MSG_ERROR([Loongson LASX SIMD is not supported on you machine])
fi

AM_CONDITIONAL(HAVE_LASX, test $have_lasx = yes)

AC_SUBST(LASX_CFLAGS)
AC_SUBST(LASX_LDFLAGS)


dnl ===========================================================================
# check for LoongArch LSX SIMD

if test "x$LSX_CFLAGS" = "x" ; then
    LSX_CFLAGS="-mlsx"
fi

have_lsx=no
AC_MSG_CHECKING(whether to enable LoongArch LSX assembler)

lsx_save_CFLAGS=$CFLAGS
CFLAGS=" $LSX_CFLAGS $CFLAGS -I$srcdir"
AC_LINK_IFELSE([AC_LANG_SOURCE([[
#if !(defined(__loongarch__))
#error "LSX are only available on LoongArch architecture."
#endif
int main ()
{
    asm volatile("vadd.w \$vr0, \$vr1, \$vr2");
    return 0;
}]])], have_lsx=yes)
CFLAGS=$lsx_save_CFLAGS

AC_ARG_ENABLE(lsx,
   [AC_HELP_STRING([--disable-lsx],
                   [disable LoongArch LSX SIMD])],
   [enable_lsx=$enableval], [enable_lsx=auto])

if test $enable_lsx = no ; then
   have_lsx=disabled
fi

if test $have_lsx = yes ; then
   AC_DEFINE(HAVE_LSX, 1, [HAVE LOONGARCH LSX SIMD])
else
   LSX_CFLAGS=
fi

AC_MSG_RESULT($have_lsx)
if test $enable_lsx = yes && test $have_lsx = no ; then
   AC_MSG_ERROR([Loongson LSX SIMD is not supported on you machine])
fi

AM_CONDITIONAL(HAVE_LSX, test $have_lsx = yes)

if test "x$GCC" = "xyes"; then
    CFLAGS="$CFLAGS -Wall"
fi

AC_SUBST(LSX_CFLAGS)
AC_SUBST(LSX_LDFLAGS)



# Define a configure option for an alternate module directory
AC_ARG_WITH(xorg-module-dir,
            AC_HELP_STRING([--with-xorg-module-dir=DIR],
                           [Default xorg module directory [[default=$libdir/xorg/modules]]]),
            [moduledir="$withval"],
            [moduledir="$libdir/xorg/modules"])

# Store the list of server defined optional extensions in REQUIRED_MODULES
XORG_DRIVER_CHECK_EXT(RANDR, randrproto)
XORG_DRIVER_CHECK_EXT(RENDER, renderproto)
XORG_DRIVER_CHECK_EXT(XV, videoproto)
XORG_DRIVER_CHECK_EXT(DPMSExtension, xextproto)

# Checks for libraries.
PKG_CHECK_MODULES(LIBDRM, [libdrm >= 2.4.89])
PKG_CHECK_MODULES(GBM, [gbm])

# ETNAVIV
AC_ARG_ENABLE([etnaviv],
              AS_HELP_STRING([--disable-etnaviv], [Disable etnaviv backend support [default=auto]]),
              [enable_etnaviv="$enableval"],
              [enable_etnaviv=auto])

if test "x$enable_etnaviv" != "xno"; then
    PKG_CHECK_MODULES(LIBDRM_ETNAVIV, [libdrm_etnaviv >= 2.4.91], [LIBDRM_ETNAVIV=yes], [LIBDRM_ETNAVIV=no])
    if test "x$LIBDRM_ETNAVIV" = xyes; then
        AC_DEFINE(HAVE_LIBDRM_ETNAVIV, 1, [libdrm_etnaviv support])
    elif test "x$LIBDRM_ETNAVIV" != "xauto"; then
        AC_MSG_ERROR([Building with etnaviv backend requested but libdrm_etnaviv not found])
    fi
fi
AM_CONDITIONAL(HAVE_LIBDRM_ETNAVIV, test x$LIBDRM_ETNAVIV = xyes)

# GSGPU
AC_ARG_ENABLE([gsgpu],
              AS_HELP_STRING([--disable-gsgpu], [Disable gsgpu backend support [default=auto]]),
              [enable_gsgpu="$enableval"],
              [enable_gsgpu=auto])

if test "x$enable_gsgpu" != "xno"; then
    PKG_CHECK_MODULES(LIBDRM_GSGPU, [libdrm_gsgpu >= 2.4.91], [LIBDRM_GSGPU=yes], [LIBDRM_GSGPU=no])
    if test "x$LIBDRM_GSGPU" = xyes; then
        AC_DEFINE(HAVE_LIBDRM_GSGPU, 1, [libdrm_gsgpu support])
    elif test "x$LIBDRM_GSGPU" != "xauto"; then
        AC_MSG_ERROR([Building with gsgpu backend requested but libdrm_gsgpu not found])
    fi
fi
AM_CONDITIONAL(HAVE_LIBDRM_GSGPU, test x$LIBDRM_GSGPU = xyes)


# Obtain compiler/linker options for the driver dependencies
PKG_CHECK_MODULES(XORG, [xorg-server >= 1.13 xproto fontsproto xf86driproto damageproto pixman-1 $REQUIRED_MODULES])
PKG_CHECK_MODULES(XEXT, [xextproto >= 7.0.99.1],
                  HAVE_XEXTPROTO_71="yes"; AC_DEFINE(HAVE_XEXTPROTO_71, 1, [xextproto 7.1 available]),
                  HAVE_XEXTPROTO_71="no")
AM_CONDITIONAL(HAVE_XEXTPROTO_71, [ test "$HAVE_XEXTPROTO_71" = "yes" ])

# Section "OutputClass" is only supported as of xserver 1.16
PKG_CHECK_EXISTS([xorg-server >= 1.16],
		 [sysconfigdir=`$PKG_CONFIG --variable=sysconfigdir xorg-server`],
		 [sysconfigdir=""])
AM_CONDITIONAL(HAS_XORG_CONF_DIR, [test "x$sysconfigdir" != "x"])

# Define a configure option for an alternate X Server configuration directory
AC_ARG_WITH(xorg-conf-dir,
            AS_HELP_STRING([--with-xorg-conf-dir=DIR],
                           [Default xorg.conf.d directory [[default=${prefix}/share/X11/xorg.conf.d]]]),
            [configdir="$withval"],
            [configdir='${prefix}/share/X11/xorg.conf.d'])
AC_SUBST(configdir)

AC_ARG_ENABLE([udev],
		AS_HELP_STRING([--disable-udev], [Disable libudev support [default=auto]]),
		[enable_udev="$enableval"],
		[enable_udev=auto])
if test "x$enable_udev" != "xno"; then
	PKG_CHECK_MODULES(LIBUDEV, [libudev], [LIBUDEV=yes], [LIBUDEV=no])
	if test "x$LIBUDEV" = xyes; then
		AC_DEFINE(HAVE_LIBUDEV, 1, [Enable udev-based monitor hotplug detection])
	elif test "x$enable_udev" != "xauto"; then
		AC_MSG_ERROR([Building with udev requested but libudev not found])
	fi
fi
AM_CONDITIONAL(LIBUDEV, test x$LIBUDEV = xyes)


ABI_VERSION=`$PKG_CONFIG --variable=abi_videodrv xorg-server`
XSERVER_VERSION=`$PKG_CONFIG --modversion xorg-server`
PIXMAN_VERSION=`$PKG_CONFIG --modversion pixman-1`

debug_msg=""
have_valgrind="no"
AC_ARG_ENABLE(debug,
              AS_HELP_STRING([--enable-debug],
                             [Enables internal debugging [default=no]]),
              [DEBUG="$enableval"],
              [DEBUG="no"])

AC_ARG_ENABLE(valgrind,
              AS_HELP_STRING([--enable-valgrind],
                             [Enables valgrindified ioctls for debugging [default=no]]),
              [VG="$enableval"],
              [VG="no"])

AM_CONDITIONAL(DEBUG, test "x$DEBUG" != "xno")
AM_CONDITIONAL(FULL_DEBUG, test "x$DEBUG" = "xfull")

if test "x$DEBUG" = "xno"; then
        AC_DEFINE(NDEBUG,1,[Disable internal debugging])
else
        if test "x$VG" != "xyes"; then
                VG=auto
        fi
fi

if test "x$DEBUG" = "xsync"; then
	AC_DEFINE(DEBUG_SYNC,1,[Enable synchronous rendering for debugging])
	debug_msg="$debug_msg sync"
fi
if test "x$DEBUG" = "xmemory"; then
	AC_DEFINE(DEBUG_MEMORY,1,[Enable memory debugging])
	debug_msg="$debug_msg memory"
fi
if test "x$DEBUG" = "xpixmap"; then
	AC_DEFINE(DEBUG_PIXMAP,1,[Enable pixmap debugging])
	debug_msg="$debug_msg pixmaps"
fi
if test "x$DEBUG" = "xfull"; then
	AC_DEFINE(DEBUG_MEMORY,1,[Enable memory debugging])
	AC_DEFINE(DEBUG_PIXMAP,1,[Enable pixmap debugging])
	AC_DEFINE(HAS_DEBUG_FULL,1,[Enable all debugging])
        CFLAGS="$CFLAGS -O0 -ggdb3"
	debug_msg=" full"
fi

if test "x$VG" != "xno"; then
	PKG_CHECK_MODULES(VALGRIND, [valgrind], have_valgrind="yes", have_valgrind="no")
	AC_MSG_CHECKING([whether to include valgrind support])
	if test "x$have_valgrind" = "xyes"; then
		AC_DEFINE([HAVE_VALGRIND], 1, [Use valgrind intrinsics to suppress false warnings])
	else
		if test "x$VG" = "xyes"; then
			AC_MSG_ERROR([valgrind support requested, but valgrind-dev headers not found])
		fi
	fi
	AC_MSG_RESULT([$have_valgrind ($VG)])
fi

AM_CONDITIONAL(VALGRIND, test "x$have_valgrind" = "xyes")
if test "x$have_valgrind" = "xyes"; then
        debug_msg="$debug_msg valgrind"
fi

if test "x$DEBUG" = "xfull"; then
        AC_DEFINE(DEBUG_MEMORY,1,[Enable memory debugging])
        AC_DEFINE(DEBUG_PIXMAP,1,[Enable pixmap debugging])
        AC_DEFINE(HAS_DEBUG_FULL,1,[Enable all debugging])
        CFLAGS="$CFLAGS -O0 -ggdb3"
        debug_msg=" full"
fi

if test "x$debug_msg" = "x"; then
        debug_msg=" none"
fi

SAVE_CFLAGS=$CFLAGS
CFLAGS=$LIBDRM_CFLAGS $XORG_CFLAGS

# Checks for header files.
AC_HEADER_STDC

PKG_CHECK_MODULES([PCIACCESS], [pciaccess >= 0.10])

SAVE_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $XORG_CFLAGS"

AC_MSG_CHECKING([whether to include GLAMOR support])
AC_ARG_ENABLE(glamor,
          AS_HELP_STRING([--disable-glamor],
          [Disable glamor, a new GL-based acceleration [default=enabled]]),
          [GLAMOR="$enableval"],
          [GLAMOR=yes])

LIBGBM="gbm >= 10.2.0"

if test "x$GLAMOR" != "xno"; then
    AC_CHECK_HEADERS([glamor.h], [GLAMOR_H="yes"], [GLAMOR_H="no"],
                     [#include "xorg-server.h"])
                      #include "glamor.h"])

    PKG_CHECK_EXISTS(epoxy >= 1.4.4,
             [AC_DEFINE(GLAMOR_HAS_EGL_QUERY_DMABUF, 1, [Have GLAMOR_HAS_EGL_QUERY_DMABUF])],
             [])

    PKG_CHECK_MODULES(GBM, "$LIBGBM", [GBM=yes], [GBM=no])
    if test "x$GBM" = xyes; then
        AC_DEFINE(GLAMOR_HAS_GBM, 1,
            [Build glamor with GBM-based EGL support])
        AC_CHECK_DECL(GBM_BO_USE_LINEAR,
            [AC_DEFINE(GLAMOR_HAS_GBM_LINEAR, 1, [Have GBM_BO_USE_LINEAR])], [],
            [#include <stdlib.h>
             #include <gbm.h>])
        dnl 17.1.0 is required for gbm_bo_create_with_modifiers
        PKG_CHECK_EXISTS(gbm >= 17.1.0,
            [AC_DEFINE(GBM_BO_WITH_MODIFIERS, 1, [Have gbm_bo_create_with_modifiers])],
            [])
    else
        if test "x$XORG" = xyes; then
            AC_MSG_ERROR([Glamor for Xorg requires $LIBGBM])
        fi
    fi

    AC_DEFINE(USE_GLAMOR, 1, [Enable glamor acceleration])
else
    AC_MSG_RESULT([$GLAMOR])
fi
AM_CONDITIONAL(GLAMOR, test x$GLAMOR != xno)


AC_CHECK_DECL(RegionDuplicate,
	      [AC_DEFINE(HAVE_REGIONDUPLICATE, 1,
	      [Have RegionDuplicate API])], [],
	      [#include <xorg-server.h>
	       #include <regionstr.h>])

AC_CHECK_DECL(fbGlyphs,
	      [AC_DEFINE(HAVE_FBGLYPHS, 1, [Have fbGlyphs API])], [],
	      [#include <X11/Xmd.h>
	       #include <X11/Xfuncproto.h>
	       #include <X11/extensions/renderproto.h>
	       #include <xorg-server.h>
	       #include <picture.h>
	       #include <glyphstr.h>
	       #include <fbpict.h>])

AC_CHECK_FUNCS([drmPrimeFDToHandle])



AC_CHECK_HEADERS([misyncshm.h], [], [],
		 [#include <X11/Xdefs.h>
		 #include <X11/Xfuncproto.h>
		 #include <xorg-server.h>
		 #include <screenint.h>])

AC_CHECK_HEADERS([present.h], [], [],
		 [#include <X11/Xmd.h>
		 #include <X11/Xproto.h>
		 #include "xorg-server.h"
		 #include <X11/X.h>])

AC_CHECK_HEADERS([dri3.h], [], [],
		 [#include <X11/Xmd.h>
		 #include <xorg-server.h>])

CFLAGS=$SAVE_CFLAGS

DRIVER_NAME=loongson
AC_SUBST([DRIVER_NAME])
AC_SUBST([moduledir])

AC_CONFIG_FILES([
                Makefile
                src/Makefile
                man/Makefile
                conf/Makefile
])
AC_OUTPUT

dnl
dnl Output some configuration info for the user
dnl
echo ""
echo "AC_PACKAGE_STRING will be compiled with:"
echo "        Xorg ABI version:        $ABI_VERSION (xorg-server-$XSERVER_VERSION)"
echo "        Building string:         $BUILDERSTRING"
echo "        pixman version:          pixman-1-$PIXMAN_VERSION"
echo "        prefix:                  $prefix"
echo "        exec_prefix:             $exec_prefix"
echo "        libdir:                  $libdir"
echo "        includedir:              $includedir"
echo "        configdir:               $configdir"

echo ""
echo "        CFLAGS:                  $CFLAGS"
echo "        LASX_CFLAGS:             $LASX_CFLAGS"
echo "        LSX_CFLAGS:              $LSX_CFLAGS"
echo "        MSA_CFLAGS:              $MSA_CFLAGS"
echo ""
echo "        LIBDRM_GSGPU_CFLAGS:     $LIBDRM_GSGPU_CFLAGS"
echo "        LIBDRM_GSGPU_LDFLAGS:    $LIBDRM_GSGPU_LDFLAGS"
echo "        LIBDRM_GSGPU_LIBS:       $LIBDRM_GSGPU_LIBS"
echo ""
echo "        LIBDRM_ETNAVIV_CFLAGS:   $LIBDRM_ETNAVIV_CFLAGS"
echo "        LIBDRM_ETNAVIV_LDFLAGS:  $LIBDRM_ETNAVIVU_LDFLAGS"
echo "        LIBDRM_ETNAVIV_LIBS:     $LIBDRM_ETNAVIV_LIBS"
echo ""
echo "        Macros:                  $DEFINES"
echo "        CXXFLAGS:                $CXXFLAGS"
echo "        Debugging support:       $debug_msg"
echo ""
echo "        Run '${MAKE-make}' to build xf86-video-loongson"
echo ""
