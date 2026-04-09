# AIPC OS: Warmboot Flavor

The Warmboot flavor is the most accessible entry point for testing and running AIPC OS. It leverages HaRET to hijack the native Windows CE environment, allowing you to boot the Linux kernel directly from an SD card without hardware modifications.

## Prerequisites

To perform a warmboot, you need the following files located in the same directory on a FAT32-formatted SD card:

1.  `haret.exe`: The boot utility (found in `third_party/`).
2.  `startup.txt`: The HaRET configuration script, containing physical memory maps and kernel command-line arguments.
3.  `zImage`: The compiled AIPC OS Linux kernel.

## Usage

1.  Insert the prepared SD card into the device.
2.  Boot the device into the default Windows CE environment.
3.  Use the file explorer to navigate to the SD card directory.
4.  Execute `haret.exe`.

The script will automatically parse `startup.txt`, load the kernel into RAM, and initiate the boot sequence.
