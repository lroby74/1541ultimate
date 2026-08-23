# Ultimate-II+ — full C1530 tape support, Magic Desk 2, and what changed

A fork by **Lroby74 & Claude Opus 5.0 Code**, based on the
[official Ultimate firmware](README.txt) by Gideon Zweijtzer, version 3.14e,
built **for the Ultimate-II+ only** (Cyclone IV `EP4CE22F17C8`).

Everything here was measured on the hardware or on a test bench; where
something could not be measured, it says so.

---

> ## Read this before you flash anything
>
> **This firmware is for the Ultimate-II+ with the Cyclone IV FPGA, and for
> nothing else.** It must **not** be used on:
>
> * the **Ultimate-II** (the one without the plus);
> * the **Ultimate-II+L**, the newer board built around a **Lattice** FPGA;
> * the **Ultimate 64**;
> * the **C64 Ultimate**.
>
> Those are different machines with different FPGAs and different processors.
> The bitstream in this image is compiled for an `EP4CE22F17C8` and means
> nothing to any of them.
>
> **There is a safety net, and one way to defeat it.** Each product only looks
> for its own update file extension: `.U2P` here, `.U2L` on the Lattice
> Ultimate-II+L, `.U64` on the Ultimate 64, `.UE2` on the C64 Ultimate, `.U2R`
> and `.U2U` on the Ultimate-II. Leave the extension alone and the wrong
> machine will simply ignore the file. **Do not rename it to make it fit.**
>
> The pair that gets confused is **Ultimate-II+ and Ultimate-II+L**: the names
> differ by one letter, the hardware does not. If your cartridge takes `.u2l`
> files, this is not your firmware.

---

## Before anything else: the Tape Adapter is mandatory

**To use a `.TAP` file in any way — play, record, counter, anything — you need
the Tape Adapter.** It is a small board that plugs into the **Datassette port**
of the C64 and connects to the cartridge with a cable that *looks* like USB but
is not: the connector on the cartridge carries the C2N signals, not USB.

Without that adapter the Ultimate-II+ **cannot read or write a TAP at all**, no
matter which firmware is loaded. This is how the product has always been — it is
a hardware requirement of the cartridge, and it has nothing to do with this
fork. `.T64` archives are a different thing and do not need it.

The adapter is sold by the manufacturer as the
[Tape Adapter Set for Ultimate-II+](https://ultimate64.com/Tape_Adapter_Set_for_Ultimate-II-1),
and several cartridge bundles already include it.

---

## What this fork adds

* **Full Commodore 1530 (Datassette) behaviour**, with a **real tape counter**
  that follows the tape, not the clock. The model comes from the C1530
  subsystem of the MiSTer C64 core (see *Credits*); it is not a re-invention.
  Two things make it accurate, and both were measured:
  * the **motor inertia** — the tape keeps moving for about 89 ms after the
    motor is switched off, once per block. On a real tape that is 79% of a
    counter click;
  * the **lead-in silence** the player emits before the first byte:
    2,463,125 cycles, **91% of a click**, which used to be counted as tape.
* **One-screen tape menu**, with the same names as the MiSTer C64 core:
  `PLAY TAPE`, `STOP TAPE`, `REWIND TAPE`, `FF TAPE`, `GO TO`, `RESET COUNTER`,
  `UNMOUNT TAPE`. ENTER repeats the same entry, so twenty rewinds are twenty
  keypresses and nothing else.
* **Three ways in**: `F8` from the file browser, **C= + the menu button** while
  a program is running, and *Mount TAP*, which drops you straight into the tape
  controls.
* **Magic Desk 2** (CRT type 85) **up to 2 MB**, 128 banks of 16K — enough for
  the large modern cartridge conversions.
* **The cartridge reset button restarts a cartridge from scratch**, instead of
  letting a game resume from where it was left in RAM.
* **Mounting a big TAP is cheap now**: building the counter map used to cost 592
  reads and 594 seeks over USB; it costs **38 reads and 3 seeks**.

---

## What is different from the official firmware — please read this first

If something that used to work no longer does, it is most likely in this list.
Every item is a deliberate decision, with what it cost written next to it.

### The Nios II JTAG debug module is removed

>>> **This is the one to know about.** The Ultimate-II+ FPGA was **97% full**,
and the tape counter, the 2 MB cartridge window and everything else did not fit.
The Nios II **JTAG debug module (OCI)** and the **SLD hub** that exists only to
serve it were switched off, which gave back **377 logic elements** (21,911 →
21,534 LE). <<<

* **What you lose:** you cannot attach a Nios II debugger over JTAG to this
  bitstream. If you develop firmware with `nios2-gdb-server`, this build is not
  for you — use the official one.
* **What still works:** programming the FPGA and the flash with the Quartus
  programmer — that talks to the Cyclone IV's **own JTAG TAP**, which is
  hardware inside the chip and has nothing to do with the SLD hub — and every
  recovery path the cartridge has. Those are spelled out, with the file each
  claim comes from, in *[If an update goes wrong](#if-an-update-goes-wrong)*
  below. **Read that section before worrying about this one.**

### The RAM disk is 2 MB smaller

The cartridge ROM window had to grow from 1 MB to **2 MB**, and it must be 2 MB
aligned, so it moved to `0x2000000`. The 2 MB came from the RAM disk, which is
now **14 MB instead of 16 MB** (`0x2200000`–`0x3000000`). The REU area is
untouched.

### Tape entries in the file browser

Pressing ENTER on a `.TAP` used to offer four entries; it now offers two:

```
Mount TAP        <- mounts the tape and opens the 1530 controls
Write to Tape    <- record (types SAVE on the C64)
```

* **`Play Tape` and `Run Tape` are gone.** `Run Tape` in particular did not
  "run the tape": it typed `LOAD` and `RUN` on the C64 for you. On a real 1530
  you type `SHIFT + RUN/STOP` yourself, and that is what the MiSTer C64 core
  does too. Mount the tape, press `PLAY TAPE`, go back to BASIC and type it.
* **`.IDX` index files are no longer supported at all** — the entry list inside
  a tape, the `Play/Run/Write From Here` entries and the code that read those
  files have been removed. Use the tape counter and `GO TO` instead.

### Two behaviours changed, both switchable

* **The tape is no longer unmounted when it reaches the end.** The tape stays
  in and so does the counter, which is what a real datassette does. The
  official behaviour is one setting away: *Unload Tape at End* in
  **Tape Settings**. This mirrors *Tape Auto Unload* in the MiSTer OSD.
* **The reset button clears the C64 RAM when a cartridge is mounted**, so the
  cartridge starts cold. Some games keep their state in RAM and would otherwise
  resume in the middle of a level. If you prefer the old warm reset — for
  instance to recover a program in RAM with a freezer cartridge — turn off
  *Reset Button Clears RAM* in the C64 settings.

---

## The Commodore 128

**Nothing in this fork touches C128 behaviour, and there are no plans to.**

The Ultimate-II+ is a **C64 cartridge**. The manufacturer supports the C128
"with some limitations", and rather than list them the official FAQ points at a
third-party page. Those limitations are pre-existing and well documented — the
cartridge menu only appears on the 40-column screen, a selected cartridge image
forces the machine into 64 mode, the internal 1571 of a 128D/DCR fights with the
virtual 1541, the timing settings need adjusting on a DCR, and the alternate
kernal does not work at all.

If you use a C128, read these instead of opening an issue here:

* [Official Ultimate FAQ](https://1541u-documentation.readthedocs.io/en/latest/faq.html)
* [Bart's Place — The Commodore 128 DCR with a 1541 Ultimate II+](https://www.bartsplace.net/content/publications/1541ultimate128.shtml)
* [Lemon64 — Ultimate-II+ with a Commodore 128 in 128-mode, doable or not?](https://www.lemon64.com/forum/viewtopic.php?t=69500)

---

## Building

>>> **This fork was built with Quartus Prime Lite 17.0.2, not 18.1.** <<<

The [original README](README.txt) lists *"Altera Quartus (tested with 18.1 Lite
Edition)"* for the U2+. Everything published here — bitstream, firmware and the
`.u2p` image — was produced with **Quartus Prime Lite 17.0.2** instead, and it
was verified before anything else that 17.0.2 supports the `EP4CE22F17C8`
device. If you rebuild with 18.1 you should get a working image too, but the
numbers below are the ones measured with 17.0.2, on this tree, and those are the
only ones that were checked on the hardware.

What that means in practice:

* the **Nios II system in the tree is Qsys 15.1** and **does not need
  regenerating** — the Nios II core, the BSP and the memory map are the ones
  already committed here. Regenerating it with a newer Qsys only adds risk;
* **ModelSim ASE**, the one bundled with the 17.0 installation, is what runs the
  hardware test benches;
* the Nios II compiler used for the firmware is the **GCC 5.3.0** that ships
  with 17.0 (`nios2eds`).

The result of the fit, for anyone reproducing the build:

| | |
|---|---|
| Logic elements | **21,606 of 22,320 — 97%**, 714 free |
| Timing | no negative slack |
| Errors | none (the usual 314 warnings of this project) |

The test benches live in [`banco/`](banco/LEGGIMI.md) — ModelSim for the
cartridge mapper and the tape hardware, plain C++ for the counter arithmetic,
the menu logic and the keyboard. `banco/setup-ambiente.sh` rebuilds the WSL
wrappers that let `make` run under WSL while calling the Windows Nios II
compiler.

---

## If an update goes wrong

Firmware for a device you cannot open is a fair thing to be careful about, so
here is exactly what an update touches, what it cannot touch, and how the
cartridge gets back on its feet. Every claim is followed by the file it comes
from, in this repository, so none of it has to be taken on trust.

### The Ultimate-II+ carries two flash chips, and the recovery one boots first

They are selected by a hardware line the firmware drives, `REMOTE_FLASHSEL`.
**Flash 0** holds a *recovery* FPGA image and a *recovery* application;
**flash 1** holds the runtime FPGA image and the Ultimate application
(`flash_addresses_u2p[]` in `software/io/flash/w25q_flash.cc`).

At power-on the FPGA always comes up from the **recovery** flash. The bootloader
in it is built from `software/portable/nios/bootloader.c` with `RECOVERY=1`
(`target/u2plus/nios/boot_recovery/Makefile`), and the first thing it does is
read the buttons:

```c
    uint8_t buttons = ioRead8(ITU_BUTTON_REG) & ITU_BUTTONS;
    if ((buttons & ITU_BUTTON1) == 0) {   // middle button not pressed
        REMOTE_FLASHSEL_1;                // ...switch to the runtime flash
        REMOTE_RECONFIG = 0xBE;           // ...and reconfigure the FPGA from it
    }
```

So:

* **button not held** — it switches to the runtime flash and the FPGA
  reconfigures from it. That is the normal boot, and it is the only thing you
  ever see.
* **middle button held at power-on** — it stays on the recovery flash, loads the
  recovery application and runs it. `ITU_BUTTON1` is the same button the running
  firmware uses to open the menu (`C64::checkButton()` in
  `software/io/c64/c64.cc` tests the same bit).

The recovery application is a small file browser, and `filetype_u2p.cc` is
linked into it (`target/u2plus/nios/recovery/Makefile`), so **from there you can
select a `.u2p` on a USB stick and write the runtime flash again**.

There is a second, smaller net in the same file: hold the **right** button
(`ITU_BUTTON2`) and the bootloader loads the image but stops before running it —
it prints `Lock` instead of jumping.

### An update writes the runtime flash, and only the runtime flash

`do_update()` in `software/application/update_u2p/update.cc` begins by selecting
flash 1 (`REMOTE_FLASHSEL_1`) and then writes two things:

| | |
|---|---|
| `FLASH_ID_BOOTFPGA` | the runtime FPGA bitstream |
| `FLASH_ID_APPL` | the Ultimate application |

The block that would write the **recovery** flash — the one that starts with
`popup("Flash Recovery?", ...)` — is inside a `/* */` comment in the upstream
source, and is commented out here too. Nothing in this firmware can write to
flash 0.

**That is the whole answer.** The image that runs first at every power-on lives
on a chip that no update touches, and one button gets you into it. A failed or
bad update costs you a power cycle with a button held down, not a programmer.

### "You removed JTAG, so a brick cannot be recovered"

This is worth answering properly, because it confuses two different things that
share a name.

* The **Cyclone IV's own JTAG TAP** is hardware inside the FPGA. It is what a
  USB-Blaster and the Quartus Programmer talk to; it works with a blank or a
  corrupt flash; and **no bitstream can remove it**, including this one. It is
  untouched.
* What was removed is the **Nios II JTAG debug module (OCI)** and the **SLD
  hub**: soft logic *inside the design*, whose only jobs are to let
  `nios2-gdb-server` attach to the soft CPU and to carry the JTAG UART console.
  That is a development convenience.

And the decisive point: whatever you load over JTAG to revive a board, **you
load it over JTAG** — the design sitting in the broken flash is not running and
is therefore irrelevant to the operation. A debug module that is not executing
can neither help you nor stand in your way.

The one thing genuinely lost is this: if you develop firmware and want to attach
a Nios II debugger to *this* bitstream, you cannot. Use the official build for
that. It is said plainly in *What is different from the official firmware*,
above, because it is the honest cost of the 377 logic elements that made the
rest of this fork fit.

> **Correction.** An earlier revision of this section described a recovery file
> on an SD card (`recover.u2u`). That is the **Ultimate-II**'s mechanism —
> `software/application/2nd_boot/boot.cc`, which is built only under
> `target/u2/`. The Ultimate-II+ has no SD card slot and does not use `.u2u`
> files, and its bootloader is the one quoted above. Thanks to those who pointed
> it out.

---

## Downloads, and what lives where

* **The flashable image is not in the repository.** It is attached to a
  **Release tagged `U2+`**, as a `.u2p` file. Source code lives in the tree,
  binaries live in the releases; that way the tree stays a fork you can
  actually diff against the original.
* **The original sources are all here.** This repository is a real fork of
  [GideonZ/1541ultimate](https://github.com/GideonZ/1541ultimate), with the
  upstream history intact and nothing removed, so there is no need for a
  separate archive of the original code: `git diff` against upstream shows
  exactly what this fork changed, and nothing more.
* **To install**: copy the `.u2p` file to a USB stick, put the stick in the
  cartridge and power the machine on. The updater takes it from there.
* **Keep the previous `.u2p`.** If an update ever misbehaves, the earlier
  image is the quickest way back — see *[If an update goes
  wrong](#if-an-update-goes-wrong)*.

---

## Credits

* **Gideon Zweijtzer**, for the Ultimate hardware and the firmware this is
  forked from.
* **Daniel Kahlin**, for the original tape code in the official firmware.
* **The C1530 subsystem of the MiSTer C64 core** — `tape_subsystem.sv`,
  `tap_scanner.sv`, `tap_time_map.sv`, `tape_counter.sv`, `c1530.sv`, the
  *Tape Auto Unload* option and the on-screen counter — written by **Lroby74**
  and a friend. It is not one example among many here: it is the
  specification. Where the Ultimate-II+ had to behave like a C1530, the answer
  was taken from there.
* **[Claude Code](https://claude.com/claude-code) (Max plan), used inside VS
  Code**, which wrote the code in this fork.

---

## On the use of AI

The code in this fork was written with Claude Code, and it could not have been
done without it. That is stated here in the first place so that nobody has to go
looking for it.

What matters is not who typed it, but what was checked. **Every number in this
README was measured**, on the hardware or on a test bench, and the benches are
in [`banco/`](banco/LEGGIMI.md) so you can run them yourself. Two examples: the
tape counter was validated against a **real TAP file** — the four positions used
on Turrican II (005, 012, 044, 067) land on clicks 5, 11, 44 and 67 of the real
tape, **maximum error one click** — and Magic Desk 2 was verified in ModelSim
for **all 128 banks** and then against **three real 2 MB CRT files**, byte by
byte, before it ever reached the hardware.

The two objections this kind of note usually gets deserve an answer.

*"We tried AI and the results were a disaster."* — The difference is not the
model, it is the environment. In a chat window you paste a file in and get a
file out that nobody runs. Here the assistant had a shell: it compiled, it ran
ModelSim, it read the real TAP files, and it got contradicted by the bench
repeatedly. Ideas that looked perfect on paper were measured and thrown away;
that is what the benches are for.

*"AI code is unmaintainable and unreadable."* — Read the diff. Every non-obvious
change has the reason written next to it, in the code, at the point where
somebody would ask "why is this like that?".
