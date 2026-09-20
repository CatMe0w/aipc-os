# Unimplemented-Address RNG

A read of the unimplemented addresses of the L2 buffer SRAM returns a value that changes between reads. This document reports what that value does, how much of it is unpredictable, what changes it, and the driver built on it. The physical cause of the change is not known.

## The device has almost no other entropy source

Without this source the kernel generator waits 32 to 252 seconds for its seed, depending on the root file system. With it the wait is 4.3 seconds. The kernel prints `random: crng init done` when the seed is ready. The figures below are boot times measured to that line, not a benchmark of the source.

| Root file system | This source | `crng init done` |
| --- | --- | ---: |
| systemd | yes | 4.3255 s |
| systemd | no | 32.1638 s |
| busybox | no | ~251.6582 s |

The first row is the mean of eight boots, which agree to the millisecond. The other two rows come from one boot each.

The wait is long because of what this device does not have. Gutterman, Pinkas and Reinman examined that case in 2006 and named four properties of an OpenWRT router:

- no keyboard, mouse or hard drive
- a flash file system that gives the generator nothing
- no generator state saved between reboots
- network interrupts as the only source left, which an adversary of a wireless router can observe

The last three hold here as they stand, and the network of this device is wireless as well.

The first needs one correction. This device has a keyboard and a touchpad, and the kernel mixes input events into the pool. They give nothing during a boot, because the generator needs its seed before anybody touches the machine. An unattended boot produces no input event at all.

The paper also quantifies the source this device does not have. Hard disk events carried 1.03 bit of entropy each, or 0.5 bit for an event given the one before it. This device has no disk. Its root file system is on an SD card, and it saves no random seed across boots.

One thing has changed since the paper, and it does not help here. Linux 5.18 replaced the SHA-1 pool with a ChaCha20 construction, which removed the forward security attack the paper describes along with the other flaws it found in the extraction path. That work changed how the kernel holds and extracts entropy, not how much it collects. A device with few sources to collect from still has few.

The paper asks a disk-less system to give the generator more entropy, or at the least to save its state at shutdown. The driver in the Linux port does the first.

## The source

The source is the address space above the end of L2 memory. [The memory map](../bootrom/memory-map.md) gives the ranges. L2 memory is nineteen buffers, 5504 bytes in total, and it ends at 0x4800157F. The address decode covers 8 KB, because accesses from 0x48002000 upward alias back with a period of 0x2000. Addresses 0x48001580 to 0x48001FFF are therefore decoded, but they lie above the last buffer. A read there returns noise and a write has no effect. This document calls that range the unimplemented window. It holds 672 aligned words.

All measurements below come from the v1.58.2 device, except where the text names the v1.88 device.

## Bit structure

Most bits of a read are fixed. Only a few change, and which ones change depends on the address.

At 0x48001580 on the v1.58.2 device, 21 bits are always 0 or always 1, and six bits change. At 0x48001584, four bytes away, 19 bits are fixed and eight change. The two sets share bits 30 and 31 and nothing else. This document calls a bit that changes an active bit.

Bit 2 of the address divides the window into two classes. Every address with bit 2 clear gives the same frequent value, and every address with bit 2 set gives a different frequent value. The two classes share no frequent value. On the v1.58.2 device each class has exactly one frequent value across all 336 of its addresses.

Two addresses of the same class behave alike, even when they are far apart. 0x48001580 and 0x48001800 are 0x280 bytes apart. Their active bit sets are identical, and their bit rates agree to 0.007 at every sample point of an 88 minute run.

**The active bits and the frequent values are different on each device.** The v1.88 device has active bits 11, 16, 28 and 30 at 0x48001580, against 12, 13, 14, 26, 30 and 31 on the v1.58.2 device. None of the frequent values match either. A driver that names specific bits works on one device only.

## How to measure it

Read one fixed address. Do not sweep consecutive addresses.

A sweep inflates the result, because address bit 2 alternates on every step and carries the value with it. Under a most-common-value estimate the three best bits of a sweep score 0.92 to 0.97 bit. Under a first order Markov model the same three bits score 0.061 to 0.097 bit, a factor of ten lower. Those bits follow the public address phase. They do not carry entropy.

At a fixed address the two estimates agree: 0.5519 against 0.5449 for the strongest bit, a ratio of 1.01. A fixed address has no detectable first order structure.

A most-common-value estimate overstates the entropy. Use the non-IID estimators of NIST SP 800-90B. On the same capture the most-common-value figure is 0.8531 bit per read and the SP 800-90B figure is 0.7859 bit per read.

## Entropy

The table gives the SP 800-90B non-IID result for one million consecutive reads. The final estimate is `min(H_original, 32 * H_bitstring)`.

| Address | H_original | 32 x H_bitstring | Final |
| --- | ---: | ---: | ---: |
| 0x48001580 | 0.785923 | 0.830944 | 0.785923 |
| 0x48001584 | 0.668506 | 0.669728 | 0.668506 |

`H_bitstring` must come from the real 32 bit words. A label file, which maps each distinct word to a byte in order of first appearance, gives a bit pattern that does not correspond to the hardware.

Bus traffic lowers these numbers, so the state of the bus belongs with any number quoted from them. The table above comes from a capture taken while the USB link was active. A capture taken before the USB controller starts, with the same address and the same read rate, gives 2.0742 bit per read against 0.8553 bit per read, a factor of 2.4. Both figures use the most-common-value estimate over 1000 samples, so the sample count does not explain the difference.

## The samples are close to independent on average

A read repeats the previous value 38 to 41 percent of the time. That rate is the collision probability of a skewed distribution, not serial correlation. An independent source with the same histogram repeats at `sum(p^2)` for every lag. The measured excess over that value is 0.003, which is 0.8 percent of the rate.

| Address | sum(p^2) | Measured lag 1 | Excess |
| --- | ---: | ---: | ---: |
| 0x48001580 | 0.380600 | 0.383792 | +0.003193 |
| 0x48001584 | 0.408887 | 0.411141 | +0.002254 |

The repeat rate stays flat across lags 1 to 64 and beyond, so the window holds no period.

The tail is not independent. A capture of 52428800 samples taken while the SD card was busy holds a run of 119 equal values. Under an independent model the expected count of runs that long is `N * r^118`, which is 4.7e-42 at the measured repeat rate of 0.3838. A repeat rate of 0.860 would be necessary to explain one. Those long runs come from the episodes of strong bias that DMA traffic produces, reported under [What changes the source](#what-changes-the-source). They are rare enough to leave the average where the table above puts it.

At a fixed address the active bits are also close to independent of each other. The sum of the per-bit estimates is 0.847846 at 0x48001580, against 0.853110 for the whole word. Independence predicts equality.

**The value does not copy the last word on the bus.** A test that writes alternating all-zero and all-one words to a connected buffer before each read finds no exact match in one million reads. The agreement per bit is 0.4986. The v1.88 device gives 0 and 0.4989. Three separate runs give the same answer.

## The source drifts

The estimate moves on three time scales.

### Inside one run

Split a capture into blocks of 20000 samples and estimate each block. The spread between blocks is 3 to 7.6 times the spread of a shuffled control, which keeps the histogram and destroys the time order. The control matches its binomial prediction, so the control is sound.

| Address | Block spread | Shuffled control | Ratio | Block minimum | Whole run |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0x48001580 | 0.047914 | 0.008606 | 5.57 | 0.733723 | 0.853110 |
| 0x48001584 | 0.061965 | 0.008192 | 7.56 | 0.611645 | 0.735050 |

A conservative bound must use the block minimum, not the whole run figure.

### Across minutes

Runs separated by tens of seconds differ by 0.17 to 0.21 bit.

### Across weeks, and per address

Five weeks apart, 0x48001584 moved from 0.668126 to 0.668506, a difference of 0.0004. Over the same interval 0x48001580 moved from 1.007970 to 0.785923, a difference of 0.22. Two addresses four bytes apart, measured in the same captures, differ by a factor of 600 in stability. Stability is a property of the address.

## What changes the source

The table separates two effects. A change of bias moves the value toward one rail. A change of randomness moves how much each read is decided by chance. Only bus traffic does the second.

| Variable | Range tested | Bias | Randomness |
| --- | --- | --- | --- |
| Time between reads | 195 ns to 264 us | Moves, +0.031 to +0.048 | No effect |
| CPU clock source | 124 and 248 MHz | No effect | No effect |
| ASIC clock | 124 and 160 MHz | Not established | Not established |
| L2 traffic | 0 to 6 writes per read, and DMA | Moves | **Lowers, in episodes** |
| USB controller activity | off and on | Moves | **Lowers** |
| Temperature | -18 C to room | Moves per active bit | No effect |

Notes on single rows:

### Time between reads

Four independent series agree on the direction and the size, including one that reverses the order of the delays. The flip rate stays between 0.488 and 0.493 across a factor of 1350 in float time. A separate figure, the whole-word estimate, appeared to follow the delay in one series and to follow it backwards in two others, so that figure carries no delay effect.

#### CPU clock source

A change from 124 MHz to 248 MHz moves a two-instruction spin loop by 0.03 percent. The read path does not follow the core clock, so the CPU clock is not an attack surface for this source.

#### ASIC clock

Seventeen interleaved runs give a difference of 0.105 bit in the whole-word estimate, at t = 2.24 on 14.5 degrees of freedom. The bias and the flip rate give t = 1.31 and t = 1.04. The drift between runs is as large as the effect. This test does not establish an effect.

#### L2 traffic, in single writes

Six writes to connected buffers before each read take the flip rate from 0.4565 to 0.3846 and the whole-word estimate from 1.1377 to 0.9459. A control that writes to DDR instead of to L2 gives 0.4529 and 1.0991, which matches the quiet case. The effect belongs to the L2 bus, not to memory traffic in general. Distance inside L2 does not matter: a write to the last connected word before the window and a write 0x1100 bytes away differ by 0.016 bit.

All six conditions of that test ran at a common inter-read interval. The probe times each condition on the board first and then pads the faster ones, because the interval alone moves the bias.

#### L2 traffic, in episodes

DMA traffic does more than lower the average. It drives the source into episodes in which one value takes most of the reads, and an episode outlasts 1024 samples. This is the fine-grained form of the block drift reported above.

The measurement runs the SD card at its limit, which puts its DMA on the same bus as the window, and reads the window while that runs. Across two captures of 102400 sample windows, 512 samples each, the worst sample window holds one value 490 times, or 95.7 percent, against a whole-capture rate near 33 percent. The longest run of equal values is 119.

An episode outlasts a sample window because the proportion does not fall when the sample window grows. At 1024 samples it reads 0.951, against 0.957 at 512. An event shorter than the sample window would be diluted by the longer one.

An idle bus produces no episodes. Over 76800 sample windows with the SD card quiet, the worst holds one value 289 times, or 56.4 percent, and the longest run is 14.

#### Temperature

A cold start and 88 minutes of warming move every bias, but not in one direction. Expressed as `|z| = offset / sigma`, the seven active bits move by +52.8, -13.9, -7.4, +38.6, -1.6, +52.6 and +3.9 percent. A rise in noise amplitude would lower every `|z|` by the same fraction. The observed signs disagree, so the movement comes from per-bit offsets. The gap between the measured flip rate and `2p(1-p)` moves by 0.0002, against a value of about 0.0009, so temperature does not change the randomness.

## The source re-randomizes at every power-on

A restart test under SP 800-90B section 3.1.4 passed. The test takes the first 1000 samples after each of 1000 power cycles and compares the rows against the columns.

```
H_r: 1.937975
H_c: 2.085610
H_I: 0.669000
Validation Test Passed...
```

The columns score higher than the rows. Each estimator agrees: 2.1018 against 2.1014 for MultiMMC, 2.1053 against 2.1073 for LZ78Y. A source whose output is a function of its power-on state gives columns that collapse. These do not.

Two further figures support this. The chance that column j equals its value in row 0 is 0.1272, and the lag 1 repeat rate inside a row is 0.1298. The two are the same, so a sample at the same position on another boot tells no more than a neighbor on the same boot. The first sample after power-on takes 34 distinct values over 1000 restarts, and the most frequent takes 20.2 percent of them.

The samples come from before the USB controller starts, so they carry the idle-bus rate.

## Read timing follows the ASIC clock

A read of the window costs 193.4 ns with the ASIC clock at 124 MHz and 150.4 ns at 160 MHz. The ratio is 1.2857 against an expected 1.2903. A calibrated spin loop gives 64.59 ns and 50.08 ns for a ratio of 1.2897. Both agree with the clock ratio to 0.4 percent, and neither varies between runs at the same clock.

The read path and the delay loop run on the ASIC clock alone. The v1.88 device gives 64.57 ns for the same spin loop, so both devices run their bus at the same rate.

## The mechanism is not known

No experiment so far identifies what makes the value change.

The temperature result rules out one candidate: a large change in noise amplitude that acts on the whole device. It does not rule out a small one. A swing from -18 C to room temperature changes the amplitude of thermal noise by 8 percent, which would lower every `|z|` by 7.4 percent. The scatter between active bits is about 28 percent, or four times the size of that signal. The temperature result is a null result with known low power, not a refutation.

What the evidence does support is narrow. The window is not an open bus, because the value does not copy the last word. The randomness does not depend on how long the bus floats. Bus traffic lowers it, and DMA traffic drives it into episodes. Each active bit has its own offset, and that offset moves with temperature.

The offsets belong to the read path, not to storage. Two addresses of the same class agree on every active bit even when they are far apart, so nothing between them holds a value of its own.

The episodes are consistent with a read that returns a held value while DMA holds the bus, but no experiment separates that from a noise source that DMA disturbs.

## Driver

`anyka-l2rng` registers the window with the kernel `hw_random` subsystem. This section states the choices that the measurements above force, and what the driver cannot do. The evidence for each one is in the sections above.

### It takes the first live address, and does not rank them

Which bits are active differs between devices, so the driver cannot name an address. At probe it reads each address 256 times and takes the first one whose most active bit changes in at least 1 percent of the reads.

It does not rank the addresses and take the best. A rank would rest on one snapshot of a quantity that drifts per address. It would also buy nothing: in a scan of all 672 addresses the weakest one changes 16 times more often than the threshold requires.

### It returns raw reads

The driver applies no hash and no other conditioning. Conditioning would leave the health tests reading their own output, which passes whatever the source does. Raw output is also the stated contract of the subsystem: the kernel documentation says the data of the character device is not checked by any fitness test.

Expect one consequence. The raw output does not pass `rngtest`, because the distribution is skewed and the driver does not hide that. A user who needs conditioned output reads it from the kernel random device, which mixes this source with the others.

### It declares one bit of entropy per 1024 bits of output

The measurement is near 21 bits per 1024. The driver claims 1 because the spread between devices has no measurement behind it, and this is the only value that covers it. The claim has to be set explicitly, because `hw_random` reads 0 as 1024.

### The health test cutoffs come from one sample window, not from the long run

The driver runs the repetition count test and the adaptive proportion test of SP 800-90B over a sample window of 512. The two cutoffs, 401 and 510, follow the formulas of sections 4.4.1 and 4.4.2 for H = 0.1 bit per read at a false alarm rate of 2^-40.

H = 0.1 is far below the 0.669 bit the source delivers over a long run, and that is deliberate. Each test judges one sample window at a time, and an episode takes a sample window to 95.7 percent of one value. Cutoffs derived from the long run figure fire on a working device: over 102400 sample windows measured under SD card traffic, the pair for H = 0.5, which is 41 and 410, fired 75 and 226 times. The pair in use fired on none of them.

The false alarm rate of 2^-40 holds for a source that is IID at H = 0.1, which this one is not, so the measured statement is the one to rely on. No sample window of those 102400 reached either cutoff. The driver itself then ran ten times under the same load, 5242880 samples each time, and no test failed.

### One failure stops a call, two stop the driver

On a failure the driver discards the whole call, returns `-EIO`, resets the test state and continues. It stops for good only when a second failure arrives within 4096 samples of the first.

The rule separates the two cases that a single test result cannot. A stuck source fails again one cutoff later, every time, so it stops the driver after about 800 samples. A working source under bus traffic can fail once and recover. An earlier version stopped on the first failure, and that version stopped for the rest of the boot on a machine that was merely busy.

The driver does not look for another address after a failure. A search for an address that passes the test would turn the health test into a filter.

A stuck source still delivers about 400 samples before the first test fires, because the repetition count test needs that many equal values to reach its cutoff. At the declared rate those samples carry 12.5 bits of claimed entropy, and the driver stops after two such rounds, so the bound is about 25 bits for one boot.

### The startup test runs before the driver registers

SP 800-90B 4.3 requires 1024 consecutive samples to pass both tests before any output. A device on which every address in the window is stuck therefore does not register at all. A machine with no usable source must look like a machine without the device, not like one with a broken source.

### What the driver cannot detect

A source degraded to about 96 percent of one value is indistinguishable from the worst behavior of a healthy source on this device, so neither test fires on it. This is a property of the source, not a choice of cutoff: the healthy worst case reaches 95.7 percent and a dead address reaches 100 percent, which leaves almost nothing between them. The repetition count test still catches an address that freezes outright.

## Unresolved

- The v1.88 device has data from one boot only. It shows that the source exists on a second device. It does not show that the source is stable there.
- Bit 2 of the address divides the window cleanly on the v1.58.2 device, with one frequent value per class. On the v1.88 device the class with bit 2 set has seven frequent values across its 336 addresses. The extra variation has no explanation.
- The idle-bus rate has a most-common-value estimate only. It has no SP 800-90B figure.
- USB activity lowers the estimate by a factor of 2.4, and six CPU writes per read lower it by 17 percent. The gap between those two figures has no explanation.
- No experiment separates a held read from a disturbed noise source as the cause of the episodes.

## Reference

Zvi Gutterman, Benny Pinkas and Tzachy Reinman. Analysis of the Linux Random Number Generator. Cryptology ePrint Archive, Paper 2006/086, 2006. https://eprint.iacr.org/2006/086
