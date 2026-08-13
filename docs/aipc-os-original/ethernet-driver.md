# Ethernet Driver

The Linux driver for the DM9000A on this board. [docs/nk/dm9000-driver.md](../nk/dm9000-driver.md) records the board wiring and the bus protocol. This document records our backend for it.

## Why the Mainline Driver Does Not Fit

The unmodified mainline `dm9000.c` needs two MMIO resources, one for the index port and one for the data port, and it reaches them with direct `readb` and `writeb` throughout. Its platform-data callbacks override block data transfers only. They cannot replace index access or single-byte data access.

This board has neither MMIO port. Software generates every bus cycle on thirteen GPIO pins. A fabricated `reg` range would only point those `readb` and `writeb` calls at an unrelated or unmapped address. The driver therefore needs a core access abstraction, which is what `dm9000_bus_ops` is.

## GPIO Backend

The backend cuts the software cost per cycle where the protocol allows it. It reaches all eight data pins through one grouped GPIO operation, caches the data-bus direction and reprograms it only on a change, and holds `CS#` for a whole block instead of per byte. `IOR#` or `IOW#` still has to toggle once per byte, because the bus protocol gives no way around it.

The `INT` pin is configured active high. The chip keeps its power-on polarity, and the `RLAN_INT#` name on the schematic does not describe the signal as the board runs it.

## Offloads

The driver reads the chip revision, finds `0x19`, and enables `NETIF_F_IP_CSUM` and `NETIF_F_RXCSUM`. The DM9000A computes and checks IP, TCP and UDP checksums itself. The MAC, the PHY, the packet SRAM, the CRC and padding logic and the flow control are all inside the chip as well. Only the host bus cycles are software.

## Status

The interface works. The chip identifies, the link comes up, DHCP completes, and ordinary traffic passes.

Sustained throughput has no measurement yet. The Linux and WinCE paths do the same asymptotic amount of bus work, but nothing compares the constant-factor cost of gpiolib calls against the `KernelIoControl` dispatch that the WinCE miniport uses.
