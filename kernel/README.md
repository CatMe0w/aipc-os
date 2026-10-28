# Linux kernel

Patches against Linux v7.3 (`c03c1a14507dfaf8b856c2608f9ac216021fc222`).

## Build

Run `kernel/build.sh` to build the kernel. The result is `kernel/build/zImage`.

That file is what both boot paths load. Put it in the root of the FAT partition.

## Work on the patches

```sh
# Fetch the firmware
./kernel/build.sh --firmware-only

# Fetch the Linux source
cd kernel/build
git clone --depth 1 --branch v7.3 --single-branch https://github.com/torvalds/linux
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
git format-patch --no-numbered --zero-commit --no-signature -o ../../patches v7.3..HEAD
```

## Caveats

### There are no loadable modules

`CONFIG_MODULES` is off. The machine has 64 MB of RAM, and the module tables cost about 370 kB. Every driver is built in, so a new driver needs a full kernel build and a new `zImage` on the card.

## License

GPLv2. See [COPYING](COPYING).

The firmware is not under the GPL. See [LICENCE.atheros_firmware](LICENCE.atheros_firmware) for its license.
