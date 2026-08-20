# Linux kernel

Patches against Linux v7.0, plus the config they are built with.

## Build

```
git submodule update --init --depth 1 kernel/linux
cd kernel/linux
git am ../patches/v1-*.patch
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
