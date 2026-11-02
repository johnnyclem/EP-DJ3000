# EP-DJ3000 hardware: status, what is missing, builder traps

**Nothing here has been printed, wired or measured.** The current build is a **free-spinning platter on a bearing with an
AS5600 magnetic encoder** under it, **one touch pad** (the hold pad) for touch/hold detection, a slide pot, a knob, CUE and
REV, an optional PCM5102A DAC and a TRS MIDI jack, all on an ESP32-S3 DevKitC-1. The mechanical design in the repo is the
earlier Field Jog shell (`field-jog/`), which was drawn for exactly the platter half of this (bearing, axle, magnet, AS5600
pad) and for different electronics around it. The segmented touch ring is kept as an **experiment**
([`PLATTER.md`](PLATTER.md), last sections); it is not the path to follow now.

| File | What |
|---|---|
| [`WIRING.md`](WIRING.md) | Pin table (the source of truth is `board_pins.h`), power, **AS5600 wiring, the 3.3 V / 5 V strap trap and the bring-up log lines**, DAC, TRS MIDI, LED |
| [`PLATTER.md`](PLATTER.md) | The AS5600 platter and its mechanics; the two ways to get a hold signal off a spinning conductive-PLA platter (both unvalidated); what you lose without a hold pad; then the experimental touch ring |
| [`BOM.md`](BOM.md) | What changes from the Field Jog bill of materials |

## The mechanical delta against `field-jog/field-jog.scad`

Each line below was checked against the SCAD (variable names in code font); nothing is guessed. `field-jog/README.md` says what
the printed parts and the bill of materials are.

### Already drawn, usable as it is

| What | In the SCAD |
|---|---|
| Body 88 x 240 x 26 mm (the Sidekick footprint), 6.2 mm top plate, 2.6 mm walls and floor, four M2.5 screw bosses, Technic pin holes (positions unchecked against real EP units, per `field-jog/README.md`), feet | `W`, `H`, `body_h`, `skin`, `wall`, `floor_t`, `technic_ys` |
| 76 mm platter, 3.8 mm thick, in a 1.1 mm well (0.55 mm diametral clearance), centred at x = 44, y = 148 mm; `platter.stl` exists | `platter_d`, `platter_t`, `well_d`, `platter_clear`, `platter_cx/cy` |
| 683ZZ pocket (7.15 mm for the 7.00 mm OD, 3.0 mm deep), its top 0.4 mm below the well floor so the race does not touch the platter; a 5.6 mm ledge bore under it for the M3 shank | `bearing_d`, `bearing_h`, `bearing_drop`, `shaft_hole` |
| M3 button-head pocket in the platter's underside (6.8 mm wide, 2.15 mm deep, opening downward) | `platter()` |
| AS5600 pad: 26 x 20 mm, top (the board seat) at z = 8.8 mm, two blind 2.1 mm holes for M2, 16 mm apart, under the platter centre | `encoder_pad_*` |
| The magnet's target zone: 6 mm x 2.5 mm at z = 12.2 .. 14.7 mm (a ghost in the preview) | `ghosts()` |
| CUE and REV: two 6 mm holes in 13.2 mm wells for caps over 6 x 6 mm tactile switches | `btn_*` |
| Knob: a 7.3 mm bushing hole for a vertical pot (the orange knob, the DevKit's ADC pin GPIO2) | `knob_x/y` |
| A 6.3 mm hole in the right wall, labelled MIDI, for the TRS MIDI jack | `midi_*` |

**One mismatch to check on the first assembly.** The pad's seat is at z = 8.8 mm and the magnet zone starts at z = 12.2 mm:
**3.4 mm** for the breakout's PCB, the chip and the gap. A common 1.6 mm PCB with a roughly 1.5 mm package (both from memory,
measure yours) leaves a few tenths of a millimetre, not the 1 to 3 mm `field-jog/README.md` asks for; a 1.0 mm PCB leaves
about 0.9 mm. The README's remedy, "shim with tape", can only raise the board and shrink the gap further. Nothing in the
repo computes where an M3 x 12 puts the magnet (the screw head sits in the platter pocket, the magnet on the shank's end),
so build it, measure it, and move the magnet (a shorter screw, a spacer between the shank and the magnet) before gluing.

### Not drawn: what this build still needs

| Missing | Why it is missing, from the SCAD |
|---|---|
| **Controller bay for the DevKitC-1** | The rails are sized for a Pico: `pico_w` = 21 mm by `pico_l` = 51 mm, 1.6 mm wide and 2.4 mm high, at the top end. A DevKitC-1 is about 70 x 25 mm (measure yours). The rails and the bay have to move or be redrawn. |
| **USB window for two ports** | The window is `usb_w` x `usb_h` = 12.5 x 7.5 mm, one micro-USB plug, at z = 5.2 mm. The DevKitC-1 has two USB-C ports on one end (native USB for the computer, UART for flashing); a finished unit needs at least the native one reachable, and about 30 mm of window to expose both (an estimate: measure your board). |
| **Slide-pot slot** | The 8 x 64 mm slot (`fader_slot_w`, `fader_slot_l`; 51 mm of travel for the 13 mm stem) is for the loose hall-sensor fader block: its stem, `fader_lock` with the magnet, and two hall posts (`hall_x`, `hall_z_top`). A slide pot (about 45-60 mm travel, 10 k linear) needs a mounting pad under the slot with screw holes for its body, and a cap or adapter that clamps its lever. None of that is drawn; the hall posts are not used. |
| **Hold pad mount** | Nothing in the SCAD: it has no foil recess, no contact holder and no wire path. For the foil-under-the-platter option the platter's underside sits **on** the well floor (z = 24.9 mm, no designed gap), so there is no room for foil without a recess, and the 6.2 mm top plate has no wire passage from the well to the cavity. For the brush option nothing is drawn around the axle between the bearing (underside z = 21.5 mm) and the magnet. See `PLATTER.md`. |
| **A second 3.5 mm hole** | One hole exists (MIDI). Topology A (a DAC per deck) also needs a line-out jack. |
| **A pocket for the PCM5102A board**, if the DAC is fitted | Nothing. |
| **A window or light pipe for the status LED** | Nothing: the DevKitC-1's WS2812 sits inside the shell. |

The conductive-PLA platter itself needs no new drawing: print `platter.stl` in conductive filament (hardened or stainless
nozzle) instead of black PLA. Whether such a platter gives a usable hold signal is the open question
([`PLATTER.md`](PLATTER.md)).

Two decisions come first (also in the [root README](../README.md)): whether the **board** really is the DevKitC-1, and how
the **hold pad** reaches the spinning platter (or whether to build without one and live without scratch mode: `PLATTER.md`).

## Builder traps

1. **AS5600 breakouts are often strapped for 5 V.** Powering one from the DevKit's 3V3 without changing the strap runs the
   chip below spec (and a 5 V supply would pull the I2C lines to 5 V, which the S3 does not tolerate). Set the strap for
   3.3 V and measure the chip's 3.3 V pin (`WIRING.md`).
2. **The magnet must be diametrically magnetised** (poles on opposite sides of the round face), centred under the chip, at
   1 to 3 mm. The boot log says `magnet detected` / `too weak` / `too strong` / `not detected` (`WIRING.md`).
3. **No answer from the AS5600 means no jog at all.** The firmware says so loudly at boot (`JOG WILL BE SILENT`) and does not
   retry while running: fix the wiring and reboot.
4. **Without a hold pad there is no touch note**, so DJ apps never enter scratch mode and the platter is a pitch-bend wheel
   (`PLATTER.md`).
5. **Status LED pin.** The WS2812 is on GPIO38 on DevKitC-1 v1.1 boards (the default build) and GPIO48 on v1.0; pick
   the revision in `menuconfig` (`EPDJ_BOARD_REV`). On any other board the LED stays dark until you set the pin.
6. **Two USB-C cables.** After the first flash the native port belongs to TinyUSB (the unit is a MIDI device, not a serial
   port), so reflashing and logs need the UART port, or BOOT held while pressing reset. Development uses both cables.
7. **Two 3.5 mm TRS jacks that look alike.** The MIDI jack carries 3.3 V on its ring (through 33 ohm) and the line-out
   jack carries audio. Plugging a mixer cable into the MIDI jack, or a MIDI cable into a mixer input, puts DC on the wrong
   input. Label them, put them far apart, or use a different connector for MIDI.
8. **GPIO47 and 48 sit in the module's VDD_SPI power domain.** GPIO47 is I2C SCL here. Fine on the usual 3.3 V modules; a
   module with 1.8 V flash would drive them at 1.8 V (`WIRING.md`; this is from memory of the datasheet, verify for your
   module).
9. **A stale `sdkconfig.esp32s3`** from before contract v1.3 keeps the touch ring and eight pads as the build's defaults
   (`firmware/esp32s3/README.md`); delete it, or pick the AS5600 platter and 1 pad in `menuconfig`.
10. **Experimental ring only:** printing the sectors (conductive PLA is abrasive and brittle; adjacent wedges must read open
    on a multimeter) and the eight thin wires from the platter to GPIO4..11, which must stay short (under 10 cm) and away
    from the ADC, DAC and MIDI wiring (`PLATTER.md`, last sections).
11. **Setting build options under PlatformIO**: `pio run -d firmware/esp32s3 -t menuconfig` (for the first-boot deck, the
    platter and the touch options); see `firmware/esp32s3/README.md`.
