This is a generic modesetting driver.
The idea is to piggy-back the X driver on top of the DRM and Gallium3D
drivers (DRM for modesetting and Gallium3D for Exa acceleration)

All questions regarding this software should be directed at the
Xorg mailing list:

        http://lists.freedesktop.org/mailman/listinfo/xorg

Please submit bug reports to the Xorg bugzilla:

        https://bugs.freedesktop.org/enter_bug.cgi?product=xorg

The master development code repository can be found at:

        git://anongit.freedesktop.org/git/xorg/driver/xf86-video-modesetting

        http://cgit.freedesktop.org/xorg/driver/xf86-video-modesetting

For patch submission instructions, see:

        http://www.x.org/wiki/Development/Documentation/SubmittingPatches

For more information on the git code manager, see:

        http://wiki.x.org/wiki/GitPage


### Dependency, Build and Install

# On fedora/centos

```
$ sudo yum install git automake xorg-x11-util-macros libXrandr-devel libdrm-devel xorg-x11-server-devel
$ ./autogen.sh --prefix=/usr --libdir=/usr/lib64/
$ make -j4
$ sudo make install
```

# On debian/ubuntu/uos

```
$ sudo apt install automake xserver-xorg-dev libdrm-dev libudev-dev libgbm-dev xutils-dev xorg-dev libtool
$ ./autogen.sh --prefix=/usr
$ make -j4
$ sudo make install
```

# build if you don't have libdrm-gsgpu

```
$ ./autogen.sh --prefix=/usr --disable-gsgpu
$ make -j4
$ sudo make install
```

On x86 platform if you don't have libdrm-etnaviv and libdrm-gsgpu, build is still possible
for example on ubuntu-20.04

```
$ ./autogen.sh --prefix=/usr --disable-etnaviv --disable-gsgpu
$ make -j4
$ sudo make install
```

### Documention

使用 exa + etnaviv 后端

在 ls7a1000 + 3A4000/3A5000平台上， 内核用 loongosn-drm + etnaviv 驱动也可以
使用etnaviv驱动。 xf86-video-loongson 始化了 X server的dri3层，
可以实现用etanviv共享buffer，加速3D客户端app(demo version).

目前还只能在X server 1.20.x 版本编译通过

![driver_framework](https://github.com/suijingfeng/xf86-video-loongson/blob/master/doc/driver_framework.png)
