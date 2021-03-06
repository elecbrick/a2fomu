# A2Fomu: Apple II Retrocomputing inside a USB port

The A2Fomu project started as a simple challenge to fit an Apple II emulator
inside the most portable FPGA development environment,
[Fomu](https://tomu.im/fomu.html). It has evolved into a Sytem on a Chip (SoC)
that integrates a custom operating system. The hardware was simple enough to
create but testing it and having someting useful took the project through a
sequence of steps that added mosre code ouput and then serial communications
over USB and ended with crash recovery and logging to a persistent filesystem.

A secondary challenge was to create a stand-alone Apple II computer that fits
inside a USB power adapter or power bank. Since Fomu does not have any wireless
capabilities, the input and output must be via the single on-board LED and the
four touch buttons. How useful is a single pixel display? The US Navy finds it
so useful that that they see it as the [Future of
Communication](https://www.engineering.com/story/why-the-navy-sees-morse-code-as-the-future-of-communication).
In stand-alone mode, the Apple keyboard is emulated by a program on the Risc-V
processor that times button presses and releases, interprets them as Morse Code
dots and dashes and translates the results into ASCII that is delivered to the
standard Apple keyboard controller. In a similar manner, characters written to
the Apple frame buffer are converted to Morse Code and cause the LED to blink in
the traditional pattern used by signal lights for almost 200 years.

[Fomu](https://tomu.im/fomu.html) uses the Lattice iCE40 UltraPlus UP5K FPGA
which contains 5280 logic elements and 143kB RAM.  The device is partitioned
into a 6502 with 64kB RAM dedicated to the Apple and an addtional Vex Rixc-V
control processor with its own 64kB RAM. Additionally, the USB subsystem needs
some hardware and RAM dedicated to buffers as it is necessary to send
instantaneous responses to the host computer. This leaves a little bit of logic
and memory inside FOMU for keyboard, video and disk drive emulation, all of
which are made accessible over the USB bus via standard protocols.

As was previously stated, this started as an experiment to see how small an
Apple II could be made today. The answer is very small especially since the USB
hardare and the Risc-V processor that runs the USB software stack are each
several times larger than the 6502 in terms of chip real estate.

A2Fomu works like a USB memory stick / flash drive for file transfer and like a
serial port for text programs. Displaying the Apple Graphic modes require a
special program to transfer the screen buffer.

## Features

* Drag and drop file transfer that works like any USB drive.
* Text mode works with standard terminal emulator, not requiring any driver.
* Text mode is via standard USB communication addapter and terminal emulation.
* Graphics modes are rendered by transfering the frame buffer to an external
program.
* Floppy disks may be placed in a virtual drive by copying them to the
  built-in flash drive.
* Floppy disks can also be kept on host and accessed via the second COM port.

## Limitations

Many limitations exists since this project is very tight on space. No attempt
has been made to guarantee accuracy in timing or experience. There are many
software emulators that provide a more faithful recreation than this attempt
does.
* Clock speed is based on 48.0 MHz USB rather than 14.31818 MHz NTSC.
* Audio is not attempted due to USB 1.1 speed limitations and the lack of
  High-Speed availability in Fomu.
* Lo-Res and Hi-Res graphics require a program on the host computer to interpret
the screen buffer that is sent over the serial port. Unfortunately, the lack of
space in Fomu coupled with the low bandwidth of USB 1.1 speeds
precludes webcam emulation.
* Video scaling is not performed on-chip due to bandwidth limitations. 
  The full-speed USB bus can barely support color Hi-Res graphics at 30fps.
* No cassette tape or game controllers.
* No "undocumented" 6502 instruction compatability.
* No Apple IIe, 80 column, lower case, language card, or other extensions.
* No write protect of the ROM. DOS 3.3 needs to be patched to avoid reloading
the language card it thinks is installed.

## Quick Start

1. Install the [Fomu
toolchain](https://github.com/im-tomu/fomu-toolchain/releases/latest) and
ensure the executable directory is in your PATH.
2. Clone this project to grab pre-built binaries.
3. Insert a new Fomu into a USB port on your laptop, desktop or Raspberry Pi.
4. From this project directory, type:
```
$ make RELEASE=1.0 flash
$ screen /dev/ttyACM0
```
5. You should see the familiar ']' prompt of the Basic interpreter. At this
   point it is possible to copy emulator disk images onto the new flash drive
   that was just installed.

This will first build the required software for the runtime executable as well
as the second and third stage boot loaders. After this, it will build the FPGA
image which will take several minutes. Once this is done, the image will be
loaded onto the Fomu and the Fomu will show up as a USB drive.

At this point, two new COM ports will show up. Connect to the first (Ubuntu:
/dev/ttyACM0) with a terminal emulator 

## Hardware Architecture

The iCE40UP5K contains:
* 128kB single port RAM (SPRAM) available as four independent 16-bit wide banks.
* 15kB dual port embedded block RAM (EBR/BRAM) as 30 independent arrays, each 16-bits wide.
* 5280 logic elements
* 2 oscillators: 48MHz, 10kHz (neither of which are used in A2Fomu)
* 1 PLL (fed by external 38MHz crystal)

The PLL must be fed by an external oscillator for proper USB timing as the
internal oscilator has too much jitter for reliable communications.  A 48MHz
frequency allows 4x oversampling to recover the USB signal so this is the value
chosen for the external crystal as well. The ValentyUSB core relies on this
frequency.

Only the USB clock recovery runs at full speed. Everything else in A2Fomu runs
at 12MHz or less including the Risc-V control CPU. This is for two reasons. The
first is that the power draw would be too high if everything ran full speed. The
second is that routing a large design is difficult at high speeds and A2Fomu
uses every single logic element available.

The 6502 clock is also derrived from the PLL generated one via a clock divider.
This clock divider is programable allowing the Apple to run up to 12MHz but by
default, the divider is set to produce the nominal 1MHz clock of a 1978
computer.

Half of the SPRAM is dedicated to each processor. The Risc-V control processor
uses its half for USB and floppy disk buffers as well as keeping most of its
operating system and control program. The Apple RAM is mapped to the entire 6502
address space from 0000 to FFFF. Even the Apple ROM is loaded into RAM which
sometimes makes it look like a language card because it can be written to. A ROM
file is loaded into addresses between D000 and FFFF by the control processor at
boot time. Addresses C100-CFFF are also RAM in this architecture with only
addresses in the soft-switch range C000-C0FF being special. The disk controller
ROM is also loaded into RAM by the configuration script at boot time.

### Scroll and Repeat detection

The most sophisticated of the custom hardware in A2Fomu is the scroll detection
circuit that simply detects a read of screen memory immediately followed by a
write to the line above. It hides a series of these writes and sends a single
command to the control processor indicating that a scroll has occurred. The
control processor replaces this token with a new-line character which
significantly simplifies the streaming of textual output of Morse Code. It also
plays nicely with modern terminal emulators that keep scroll back history.
Without a feature like this, the display keeps updating the same 24 lines and no
history is kept. Detecting the read of screen memory is the key to making any
program on the Apple output Morse Code. It would be possible to change the
CHARGET routine to send to a custom printer, such as PR#1 would do, but the
chosen method even works with programs that bypass the Apple BIOS and write
directly to screen memory.

Repeat detection is similar to scroll detection but is mostly useful for
clearing the screen or clearing to the end of a line. These operations
are critical for full screen applications but are unnecessary for scrolling
applications. They are also easily detected by software so this hardware is
disabled by default simply because there is no room for this unnecessary
circutry.

### Video Buffer

All writes to screen memory enter a FIFO (software terminology: queue) to
guarantee characters are displayed by the Morse Code transmitter in the same
order in which they were placed on the screen. The FIFO also guarantees no
characters are lost should the Apple write to a screen location twice between
consecutive scans by software. One consequence of this is that the startup code
now writes "[] ellpA" via Morse Code because the ROM code actually prints the
initial string from right to left.

### Keyboard

The keyboard is two memory mapped locations in the Apple memory space. These two
registers are made available to the control processor along with a flag stating
that a character is being requested. This lets the control processor buffer a
large paste buffer that is fed via the emulated keyboard one key at a time.
Software on the control processor takes care of translating ASCII to key codes.

### Floppy Disk Drive

The Disk II controller card has 16 memory mapped hardware registers, half of
which control movement of the arm by stepper motors for track selection. The
other half determine which drive is active, whether the disk is spinning or not
and the actual data being read from or written to the disk. Modified versions of
the hardware registers are made available to the control processor. For one, the
eight phase registers that control arm movement are reduced to four bits.
Writing to disk is not currently supported simply because the device is full.
Some parts of the device were optimized (eliminated) in order to make the read
function work. Write will be added when this emulator moves to a larger device.

### Buttons

Fomu has four touch pads that are intended for capacitive touch but, due to the
lack of external circuitry, end up being better used as a pair of switches that
are activating by shorting two adjacent pads with each other. The operating
system treats one switch as the Morse Code key and the other as a shortcut key
that alows quick input of a macro or as a timing key that makes learning to type
easier.

### LED

Fomu has one RGB LED that is available to the host processor. The A2Fomu
operating system uses this for Morse Code transmission. It would be possible
to place the USB D+ and D- directlu on two colors, say the R and G channels, but
this would achieve marginal speedup and make it harder for humans to interpret
the output directly. Instead, this would force the need of a computer to
translate the NRZI encoded differential signal back into readable text. Morse
Code, on the other hand, can easily be learned and A2Fomu is a great device for
that.

### LiteX

[LiteX](https://github.com/enjoy-digital/litex) provides a build environment
that integrates a Wishbone BUS, VexRiscv CPU, ValentyUSB, SPI Flash, Timer

### 6502 CPU

There are many published implementations of the venerable 6502. I planned to
write my own but ended up using one of many that were available. The project 
[verilog-6502](https://github.com/Arlet/verilog-6502) implements a simplified
model that interfaces directly with synchronous memory and avoids the complexity
of asynchronous conversion. It uses a single input clock and the RDY signal can
be pulsed to gate the 12MHz Fomu system clock down to the target 1MHz 6502 clock
speed.

### Risc-V CPU

The [VexRiscv](https://github.com/SpinalHDL/VexRiscv) CPU is highly configurable
and offers great performance in a small a gate count. The chosen configuration
implements the RV32I instruction set with a four stage pipeline, a 4kB
instruction cache, interrupt handling, and single cycle shift. There is no
data cache, hardware multiply, or MMU.

## Software Architecture

The emulated Apple II runs any software written for the Apple II. However, since
there is no game port, speaker, light pen, etc, programs using those input or
output devices will not function as intended. Also, writes to disk are not
currently supported. 

The Risc-V software is modular and has a permisive license allowing pieces to be
mixed with other projects. One goal was to create a device layer that is not
present in FreeRTOS or Zephyr. One goal is to make I/O layers available for easy
use with these established operating systems.

### Initial Power-on Foboot

Fomu ships with a fail-safe boot loader that is not touched by A2Fomu. This boot
loader is responsible for loading the A2Fomu gateware into Flash memory both
when first transforming the standard Fomu into an A2Fomu and whenever a software
update is required for the main A2Fomu executable. Foboot always checks to see
if a different executable, called Foboot Main, is available and if so, transfers
control to that program. This project installs fbms in that slot to allow the
Apple emulator to start as soon as the device is plugged in.

### ROM

A2Fomu contains a small ROM that only acts as a third stage loader, after the
standard Foboot and FBM have flipped to this gateware.
Software consists of a small 1kB ROM containing a third stage loader
that brings
the critical parts of the Risc-V OS from flash into RAM.

that fits into 64kB RAM including
code, data, buffers and persistent storage for logging that survives a crash.

## Implementation

100% Free and open tool chain and IP.

## Requirements

Fomu or another iCE40UP5K board. 
https://tomu.im/fomu.html

Building the hardware image has the following dependancies:

* Python 3.5+
* Migen
* LiteX
* Yosys
* Nextpnr

Building the software requires:

* Risc-V toolchain

## Flash layout

- Locked blocks are unchaged. Official foboot failsafe image and coldboot
vectors are used.
- Flash is erased in 4kB chunks so all updates are in units of 4kB.
- 25% of the flash is kept for the failsafe and A2Fomu gateware as well as the
runtime, secondary bootstrap and reserved spaces for libraries.
- 75% of the flash is formatted as a FAT filesystem allowing A2Fomu to serve as
a USB flash drive. Disk images may be dragged and dropped into this
device and then mounted for use by the emulator and scripts may be copied that
can execute CLI command on the control processor.

## Troubleshooting

If the build fails with a message like:
```
ERROR: Max frequency for clock 'clk48_$glb_clk': 45.41 MHz (FAIL at 48.00 MHz)
```
It may build if a different seed is used. Please enter the 'hw' directory and
follow the instructions there for building with a different seed.

If the build fails with a message like:
```
ERROR: Could not place ...
```
It is likely that one of the dependencies changed. Please ensure the correct
versions of tools and LiteX are used. If the versions are correct, please try
using a different seed as per the Max frequency error as it could also be that a
tool changed without increasing its version and now produces output that
requires a chip with more gates than Fomu offers. Normally tool improvements
optimize and reduce the size required but this is not always the case.

### Apple II ROM

Apple still enforces the copyright on the ROM but there are open source
alternatives that are compatable. One option is to use the source code that 
Microsoft released and compile a version of BASIC that has most of the features
of Applesoft. Here are some links I used as reference:
[Microsoft BASIC for 6502]<https://github.com/mist64/msbasic>

### Alternatives
[Apple2fpga: Reconstructing an Apple II+ on an
FPGA]<http://www.cs.columbia.edu/~sedwards/apple2fpga/>

### Usage

Plug it into a computer USB port and place some files on it. Edit the HELLO
startup script to load the desired ROMs and mount the desired disks. Use screen
(Linux), PuTTY (Windows) or your favorite terminal emulator to connect to the
COM port and start typing.

#### Creating A2Fomu on a new Fomu

The Makefile in the hw directory takes care of building and flashing the bios
but it requires the main software be built first. Both these steps are performed
by the Makefile in the project root. All you should need is:
```make
```
which will first
```make -C sw/src
make -C hw flash
```

## Credits / Disclaimers

Fomu:

Foboot:

TinyUSB:

ValentyUSB:

LiteX:

Migen:

Apple, Apple II, and Applesoft are registered trademarks of Apple Computer.
