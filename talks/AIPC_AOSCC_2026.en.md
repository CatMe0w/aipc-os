# No Datasheet, No Problem: Porting Linux to the (Legendary) AIPC

AOSCC 2026, August 2026. Speaker: catme0w.

### Slide 1

[TN: The title parodies a well-known Chinese ad meme] [booming voice] No datasheet, how do you grow Linux! [normal tone] Porting Linux to the legendary AIPC.

### Slide 2

There was supposed to be a video here, but the original ad is really too loud, so here is a screenshot instead. I believe most of you have seen it; please use your imagination for the sound. This is the AIPC TV commercial, the origin of a meme that lives on to this day.

### Slide 3

But for all these years, AIPC has only been a legend. Almost nobody has ever seen the real device. So, the bizarre marketing claims like "Samsung genuine CPU" are even harder to trace.

### Slide 4

That same year, there was another kind of TV ad, one that sounds completely unrelated to AIPC, called "Buy a wireless network adapter, get a laptop for free." This ad was almost never remembered by anyone. I could not even find a single screenshot of it, only this one news article debunking it.

### Slide 5

Are they related? You have probably already guessed.

### Slide 6

AIPC and this "wireless network adapter" are the same thing. No matter which one you called in to buy, what you received was the exact same knockoff netbook.

### Slide 7

But if all this information has been lost for so many years, how can I be sure they are the same device?

### Slide 8

Because... I was that wireless network adapter buyer.

### Slide 9

This is the device. About seven inches, incredibly thick, but also very light thanks to its cheap materials. You can try the real device at the exhibition area.

### Slide 10

Its OS is Windows CE. Not real Windows, but it can connect to Wi-Fi, browse the web, and even has quite a few games to play.

### Slide 11

I have always had a wish: could I replace WinCE with Linux? As for why, honestly, I have forgotten the reason, but the idea has always been there.

### Slide 12

For this wish, I searched everywhere for information. Overseas, a netbook with the exact same chassis was sold under the Sylvania brand, and someone had already successfully ported Linux to it.

### Slide 13

But Sylvania used a VIA or WonderMedia processor that the mainline kernel already supports. Our AIPC uses a processor I had never heard of, completely different from Sylvania. Their approach is useless for AIPC.

### Slide 14

This is AIPC's chip: Anyka AK7802. The company is from Shenzhen, and back then they made processors for cheap MP3 and MP4 players. The AK7802 is a chip designed for budget MP4 players, with an ARM9 core. Beyond that, I knew nothing about it.

### Slide 15

No useful documentation to be found. No chip manual, no datasheet, let alone an evaluation board.

### Slide 16

As for the AIPC device itself, a bit more information was available: I found what appeared to be this device's motherboard schematic on a skeptical document-sharing website. It looked correct, but there was no way to verify it.

### Slide 17

With only these to go on, there is absolutely no way to port Linux. So, what now?

### Slide 18

In the world of WinCE, there is a ready-made tool called HaRET. Back in the day, people used it to install Android on Windows Mobile phones like the HTC HD2. It boots a Linux kernel directly from WinCE userspace, no reflashing needed. As long as WinCE runs, HaRET works.

### Slide 19

For someone who knows almost nothing about the hardware, this might be the only way in.

### Slide 20

But having the tool alone is still useless. Where does the kernel come from? Where do the boot parameters come from? I had no clue, and my wish entered a long period of stagnation.

### Slide 21

Until this year. This year, I did one small thing: I searched GitHub for "AK7802."

### Slide 22

Of course, no miracle happened. After all these years, the world still knows nothing about this chip. No direct information on AK7802 in the search results. But observe that there is something else: its direct successor, the AK98 series, had Linux 2.6 kernel source code available.

### Slide 23

But if I am just going to recompile the same old 2.6, where is the fun in that!

### Slide 24

Now that there is hope, it has to be Linux 7.0, the latest mainline kernel, still in RC at the time!

### Slide 25

HaRET can do more than boot kernels. It can also directly read and write physical memory. Using this ability, I found the physical memory address range.

### Slide 26

With no UART or JTAG available, the screen is my only debugging tool. So, the next thing to find immediately is the framebuffer, or loosely speaking, the video memory.

### Slide 27

But this was a real roundabout process: I could only use HaRET to manually write pixel data to various memory addresses and see whether the device would crash or display colors on the screen.

### Slide 28

Ta-da!

### Slide 29

After finding the first splash of color, I used binary search to narrow it down until the screen was perfectly covered. The framebuffer parameters were now in hand.

### Slide 30

With the memory range and framebuffer, two critically important pieces of data, the remaining work for porting Linux became a clear path: copy drivers and register tables from the AK98 2.6 kernel source, write them into a device tree, compile them into 7.0, and hand it to HaRET to boot.

### Slide 31

But it is not that easy. 2.6 and 7.0 are worlds apart. Before the Linux kernel takes over the screen, there are countless obstacles. But before the screen driver comes up, if anything goes wrong during boot, nothing appears. You get no useful feedback at all.

### Slide 32

Observe that since HaRET performs a warm boot, our kernel directly inherits the hardware state that WinCE had already initialized, including the LCD. That means even after the CPU has been handed over to Linux, the paint-the-screen trick still works.

### Slide 33

All I need to do is patch the Linux source code: insert a small piece of color-painting code before and after each key function in the boot sequence, each function painting a different color. This way, I can tell exactly where things get stuck.

### Slide 34

After overcoming tremendous difficulties... truly tremendous. Countless reboots, launching HaRET, staring at which color this round would freeze on, over and over, until finally...

### Slide 35

Welcome to Linux!

### Slide 36

But this is not the finish line yet. The shell cursor is frozen, I cannot type anything, and the kernel log timestamps are all zeros. The clock is broken. This is expected, since I never wrote a clock driver. At this moment, the hardest part is over, and the rest really should be smooth sailing.

### Slide 37

But I was impatient. The first version of the clock driver was too rushed, and kernel log timestamps were still mostly all zeros. But the shell cursor's blinking looked about right. Fine, mark it as a TODO for now. Getting the keyboard working and achieving basic input is the top priority.

### Slide 38

AIPC's keyboard is a real nightmare. This keyboard is essentially a USB keyboard, but it is not connected to the SoC's USB pins. Instead, it connects to an external USB host controller, which in turn connects to the SoC via SPI bus, and that SPI is bit-banged through GPIO pin muxing. What a detour. To get the keyboard working, three subsystems must be solved at once.

### Slide 39

Still, with the power of the 2.6 source code and the color-painting technique, the fog over the SoC register map has cleared. No more surprises ahead.

### Slide 40

...right?

### Slide 41

[mock-solemn narration] One month later, as he faced the dead-silent SPI bus, catme0w was to remember that distant afternoon when Linux 7.0 first lit up on the AIPC.

### Slide 42

Something went wrong. I had no clue what. I had ruled out every possibility, but the SPI bus had no response whatsoever, as if it were completely disconnected.

### Slide 43

Regardless, AK98 is not AK78. That 2.6 source code is useful, but it ultimately cannot be treated as a reliable source. There is only one place to find a reliable source: the original WinCE firmware on the AIPC itself. If I can just see how WinCE's SPI driver works, I can find out where the problem is.

### Slide 44

But looking at WinCE's drivers is not that simple. WinCE's system files are protected; even copying them out will fail. I need a tool to dump the entire firmware.

### Slide 45

After searching, I found a tool called "grabit" on XDA Developers, shared by someone long ago. Just copy it to a USB drive, run it on WinCE, and it automatically dumps the firmware. Excellent. I had zero interest in figuring out how to write programs for WinCE.

### Slide 46

So let's try it. It worked! grabit produced a plausible-looking dump, but when I opened it...

### Slide 47

I was dumbfounded! The entropy distribution of the dump file looks like a wall. There is no way this is firmware. Looking closely, it is the same tiny bit of data repeated thousands upon thousands of times.

### Slide 48

After stripping away the repeated data, only 19 KB remains. But I was still curious: what on earth did I just extract?

### Slide 49

I had a strong feeling that this 19 KB must be something significant. Just from eyeballing the hex, I guessed it must, absolutely must, be executable code. I tried decompiling it, just to see.

### Slide 50

I think... I accidentally extracted the AIPC's Boot ROM.

### Slide 51

How is that even possible? grabit had a bug. It was actually reading from a null pointer, which is physical address zero. On most systems, reading address zero would crash immediately, but WinCE has no protection on address zero. And the AK7802 chip happens to place its Boot ROM, the very first code that runs when the chip powers on, at address zero. Three conditions, three coincidences. Without any one of them, I would have gotten nothing, and would have had to keep guessing with code I did not even know was correct, perhaps spending another month with no progress.

### Slide 52

But at this moment, I finally had the first fully reliable source of truth.

### Slide 53

After fully decompiling the Boot ROM, observe that it has a built-in bare-metal mode, internally named "USB Boot": from a PC over USB, you can arbitrarily read and write memory, and jump to execute any address.

### Slide 54

With the Boot ROM's logic understood, the next target is clear: continue down the boot chain. First, I need to get EBOOT, the WinCE bootloader.

### Slide 55

Because EBOOT itself contains the keyboard driver code, and extracting EBOOT is more practical than extracting the entire firmware. EBOOT is much smaller, and I do not need to deal with WinCE's driver model.

### Slide 56

With USB Boot's read-write capability, I built a NAND extraction tool and dumped the entire flash.

### Slide 57

The second stage of the boot chain, nboot, sits between the Boot ROM and EBOOT and serves only as a bridge. It is 4 KB, decompilation is a breeze, and I can find the logic for loading EBOOT in it. That is it. nboot has nothing we care about.

### Slide 58

Victory is in sight. Extracting EBOOT from the NAND dump seems as easy as breathing now.

### Slide 59

But the decompilation result of EBOOT looks like this. The decompilation output is incomprehensible. Even the function entry points are irregular.

### Slide 60

I suspected non-standard calling conventions, obfuscation, even self-modifying code, one after another, but none of them can explain this degree of chaos.

### Slide 61

Once again, I hit a dead end with no feedback. I do not know where the problem is. I do not know where to start.

### Slide 62

But it is not a total standstill. I have USB Boot. I can load arbitrary code onto this device. Since that is the case, I can choose not to go all-in on the WinCE path. I need a bare-metal program that bypasses WinCE and directly validates my understanding of the hardware.

### Slide 63

This program must have graphics and interaction. What should it be?

### Slide 64

Obviously, it can only be DOOM.

### Slide 65

Porting DOOM to new platforms is well-trodden territory; just follow the established approach. But DOOM also needs debugging, and debugging needs logs. Without UART or JTAG, I need a method, an unprecedented method, to get data back from this device. The problem is, the only way to get data out of this device seems to be USB Boot.

### Slide 66

Let's think step by step. USB Boot can read memory and send it back to the PC, but USB Boot can only be entered at power-on.

### Slide 67

DOOM can write to memory, but memory data vanishes when the power goes off. Is there a way to make data in memory survive a power cycle? Yes, there is.

### Slide 68

If the gap between power-off and power-on is short enough, the data in memory survives.

### Slide 69

This is a cold boot attack.

### Slide 70

Here is the concrete approach: implement DOOM's logging function as writes to a specific memory address. When DOOM gets stuck, pull the power, immediately plug it back in, enter USB Boot, and dump that block of memory. Now you can see the logs.

### Slide 71

It's super effective! This technique became the primary debugging method for the DOOM sidequest.

### Slide 72

Then I noticed that the same method can be applied to our main storyline: Linux.

### Slide 73

The idea is the same. Boot WinCE, let it start up normally, pull the power, plug it back in, enter USB Boot. But this time, dump all of memory.

### Slide 74

This is a complete memory snapshot of WinCE in normal operation. I originally planned to look for WinCE's SPI driver directly in the memory, but also observe that EBOOT was still there in memory, not overwritten. I cut it out and sent it to the decompiler. I did not expect the result to be any different, but since I already had it, why not try.

### Slide 75

But this time, the decompiler gave an extremely clean result.

### Slide 76

One truth prevails! The EBOOT from the NAND dump was corrupted.

### Slide 77

Why is the same EBOOT corrupted when read from NAND but fine when taken from memory? Because of ECC, error checking and correction. This NAND flash has 2112 bytes per page, but normally it is 2048 bytes of data plus 64 bytes of check area. When reading, you just take the first 2048 bytes.

### Slide 78

But AIPC does not use that layout. It places 16 bytes of ECC right after every 512 bytes of data, repeated four times within a single page.

### Slide 79

The Boot ROM's NAND read function does not perform error correction. It just takes the first 2048 bytes of each page. As a result, three segments of ECC check bytes get mixed right into the data.

### Slide 80

And my extraction tool used the Boot ROM's exact read method. Each read returned 2048 bytes, which happens to be the nominal data size of a page, so I never suspected the read itself was wrong. I thought the hardware had already done the correction for me.

### Slide 81

Moreover, nboot itself does not use ECC, so the Boot ROM's read method works perfectly for nboot, and its decompilation looked fine.

### Slide 82

But ironically, one of the things nboot does is load EBOOT into memory using ECC-corrected reads. I said earlier that nboot had nothing we care about, but it had contained the answer to this puzzle all along.

### Slide 83

Finally, with the clean EBOOT in hand, I got what I had been dreaming of: the keyboard driver and the LCD initialization sequence.

### Slide 84

I ported these hard-won findings into DOOM, and the game finally had graphics and input.

### Slide 85

However...

### Slide 86

Entering the game, the screen freezes on the first frame. The world stands still.

### Slide 87

Clock trouble. The clock again.

### Slide 88

But unlike a month ago, I now have USB Boot. I can run experiments in an ideal bare-metal environment like doing physics experiments: form a hypothesis, design an experiment, verify. I can write countless programs to run in bare metal, probing this chip's behavior. Most of these experiments were run automatically by AI: generate a test program, load it into bare metal, read back results, then generate the next round, and repeat. Exploring hardware logic can now iterate automatically.

### Slide 89

Under this highly efficient new workflow, the problem was found quickly. In fact, it had already been visible in this log from earlier.

### Slide 90

Look at the timestamps. At first, all zeros, then a sudden jump to 33 seconds. Time is... discrete.

### Slide 91

Here is what happened. Each timer on the AK7802 has two registers. One holds the current running time, the other holds a control parameter that we write ourselves. My driver had been reading the latter, so that number never changed.

### Slide 92

So the kernel could only sense time through overflow interrupts. The timer runs through one full cycle, an interrupt fires once, and only then does time advance by one step.

### Slide 93

One month ago, the keyboard did not work, the SPI bus seemed disconnected, and I kept thinking it was a problem with my SPI driver.

### Slide 94

Because the kernel's time only advances every few seconds. No bus communication can possibly work under such timing.

### Slide 95

In fact, the SPI driver was never wrong. They were just waiting for a correct clock.

### Slide 96

The clock is fixed. DOOM works.

### Slide 97

Back to the main storyline. After fixing the clock alongside DOOM, the Linux keyboard finally works too. All those drivers written a month ago are all working properly.

### Slide 98

Everything accumulated from the DOOM sidequest, together with the LCD initialization sequence extracted from EBOOT, is all ported back into the Linux kernel. And there is a bonus: Linux can now boot directly from the Boot ROM in bare metal. No more WinCE, no more HaRET.

### Slide 99

At first, Linux was right there, but there was no way to interact with it.

### Slide 100

Now, everything is here.

### Slide 101

The wish from over a decade ago is finally fulfilled.

### Slide 102

And that is the end of this story. In fact, both Linux and DOOM still have much more work ahead, but that is a story for another time. Thank you for listening to the end, and feel free to come try the real device during the break.
