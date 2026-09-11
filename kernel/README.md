# Linux kernel

Patches against Linux v7.2 (`8d3ae59288f1e7d58d76558a6ee96d533bc5019f`).

## Build

Run `kernel/build.sh` to build the kernel. The result is `kernel/build/zImage`.

That file is what both boot paths load. Put it in the root of the FAT partition.

## Work on the patches

```sh
# Fetch the firmware
./kernel/build.sh --firmware-only

# Fetch the Linux source
cd kernel/build
git clone --depth 1 --branch v7.2 --single-branch https://github.com/torvalds/linux
cd linux
git am ../../patches/*.patch

# Build the kernel
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- aipc_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- -j"$(nproc)"
cat arch/arm/boot/zImage arch/arm/boot/dts/anyka/ak7802-netbook.dtb > ../zImage
```

`--firmware-only` puts the firmware in `kernel/build/firmware`. That directory must stay next to the Linux source tree, so copy it there too if you clone Linux somewhere else.

To export the patches again:

```sh
rm -f ../../patches/*.patch
git format-patch --no-numbered --zero-commit --no-signature -o ../../patches v7.2..HEAD
```

## Caveats

### The kernel command line is compiled in

`CONFIG_ARM_ATAG_DTB_COMPAT` is off and the device tree carries no `bootargs`, so a bootloader cannot pass a command line to this kernel. It always uses:

```
console=tty0 root=/dev/mmcblk0p2 rootfstype=ext4 rootwait rw
```

That matches the SD card image built by [sdcard](../sdcard/README.md), where partition 2 is the root filesystem. To change it, edit `CONFIG_CMDLINE` in `aipc_defconfig` and rebuild.

### There are no loadable modules

`CONFIG_MODULES` is off. The machine has 64 MB of RAM, and the module tables cost about 370 kB. Every driver is built in, so a new driver needs a full kernel build and a new `zImage` on the card.

## License

GPLv2. See [COPYING](COPYING).

The firmware is not under the GPL. See [LICENCE.atheros_firmware](LICENCE.atheros_firmware) for its license.
