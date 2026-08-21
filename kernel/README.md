# Linux kernel

Patches against Linux v7.2 (`8d3ae59288f1e7d58d76558a6ee96d533bc5019f`).

Choose one of the two methods below to build the kernel. Run every command in this file from the `kernel/` directory.

## Source: tarball

Use this method if you only want a `zImage`. The CI build uses it.

```
mkdir -p build && cd build
curl -fSLO https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.2.tar.xz
tar -xf linux-7.2.tar.xz
cd linux-7.2
for p in ../../patches/v1-*.patch; do patch -p1 --forward < "$p"; done
```

Then proceed to the build section below.

## Source: git

Use this method if you want to change the patches. `git am` keeps each patch as a commit, so you can edit the series and export it again with `git format-patch`.

```
mkdir -p build && cd build
git clone --depth 1 --branch v7.2 --single-branch https://github.com/torvalds/linux
cd linux
git am ../../patches/v1-*.patch
```

## Build

```
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- aipc_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -j"$(nproc)"
cat arch/arm/boot/zImage arch/arm/boot/dts/anyka/ak7802-netbook.dtb > zImage
```

That file is what both boot paths load. Put it in the root of the FAT partition.

## Caveats

### The kernel command line is compiled in

`CONFIG_ARM_ATAG_DTB_COMPAT` is off and the device tree carries no `bootargs`, so a bootloader cannot pass a command line to this kernel. It always uses:

```
console=tty0 root=/dev/mmcblk0p2 rootfstype=ext4 rootwait rw init=/sbin/init
```

That matches the SD card image built by [sdcard](../sdcard/README.md), where partition 2 is the root filesystem. To change it, edit `CONFIG_CMDLINE` in `aipc_defconfig` and rebuild.

## License

GPLv2. See [COPYING](COPYING).
