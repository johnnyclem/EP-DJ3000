# EP-DJ3000 hardware: status, what is missing, builder traps

**Nothing here has been printed, wired or measured.** The current build is a **free-spinning platter on a bearing with an
AS5600 magnetic encoder** under it, **one touch pad** (the hold pad) for touch/hold detection, a slide pot, a knob, CUE and
REV, an optional PCM5102A DAC and a TRS MIDI jack, all on an ESP32-S3 DevKitC-1. The mechanical design in the repo is the
Field Jog shell (`field-jog/`), **redrawn to 16 mm** with a bearing sleeve, a battery bay under a removable faceplate, a jack
row on the top edge and a headphone jack on the fader end. It was drawn for the platter half of this (bearing, axle, magnet,
AS5600 pad) and for different electronics around it. The controller bay is now a lip for a headerless ESP32-S3-DevKitC-1 (about 70 × 25.5 mm; measure yours). Factory pin headers do not fit the 16 mm shell. The segmented touch ring is kept as an experiment
([`PLATTER.md`](PLATTER.md), last sections); it is not the path to follow now.

| File | What |
|---|---|
| [`WIRING.md`](WIRING.md) | Pin table (the source of truth is `board_pins.h`), power, **AS5600 wiring, its filter configuration, the 3.3 V / 5 V strap trap and the bring-up log lines**, DAC, TRS MIDI, LED |
| [`PLATTER.md`](PLATTER.md) | The AS5600 platter and its mechanics; the two ways to get a hold signal off a spinning conductive-PLA platter (both unvalidated); what you lose without a hold pad; then the experimental touch ring |
| [`BOM.md`](BOM.md) | What changes from the Field Jog bill of materials |

## The mechanical delta against `field-jog/field-jog.scad` (the 16 mm redraw)

Each line below was read from the **current** `field-jog/field-jog.scad` (variable names in code font), not from older text.
`field-jog/README.md` still carries a few numbers from before the redraw (its USB window is 12.5 x 7.5 mm; the SCAD says
12.5 x 6.2); when the two disagree this file follows the SCAD. `field-jog/README.md` says what the printed parts and the bill of
materials are. The SCAD and the STLs were not edited for any of this.

### Already drawn, usable as it is

| What | In the SCAD |
|---|---|
| Body 88 x 240 x **16** mm (the EP-133 / EP-136 thickness, so the cases sit flush), 4.2 mm top plate, 11.8 mm tray with a 1.8 mm floor (10.0 mm of cavity), 2.6 mm walls, four M2.5 screw bosses, Technic pin holes (positions unchecked against real EP units, per `field-jog/README.md`), feet | `W`, `H`, `body_h`, `skin`, `tray_h`, `floor_t`, `wall`, `screw_positions()`, `technic_ys` |
| 76 mm platter, 3.8 mm thick, in a 0.7 mm well (0.55 mm diametral clearance), centred at x = 44, y = 148 mm; it stands 3.1 mm proud of the deck; `platter.stl` exists | `platter_d`, `platter_t`, `well_d`, `platter_clear`, `platter_cx/cy` |
| **683ZZ sleeve** hanging below the deck: a 13.55 mm tube from the well floor (z = 15.3) down to **z = 10.7**, 1.1 mm below the top plate's underside, so a 3 mm bearing fits a 16 mm case. 7.15 mm pocket for the 7.00 mm OD, race at z = 12.0 to 15.0 (its top 0.3 mm below the platter), a 1.3 mm ledge under it with a 5.6 mm bore for the M3 shank | `bearing_d`, `bearing_h`, `bearing_drop`, `ledge`, `ledge_z`, `shaft_hole` |
| M3 button-head pocket in the platter's underside (6.8 mm wide, 2.15 mm deep, opening downward) | `platter()` |
| AS5600 pad: 26 x 20 mm, its top (the board seat) at **z = 3.5 mm**, two blind 2.1 mm holes for M2, 16 mm apart, from z = 0.7 up through the pad, under the platter centre | `encoder_pad_*` |
| The magnet's target zone: 6 mm x 2.5 mm at **z = 8.1 .. 10.6 mm** (a ghost in the preview) | `ghosts()` |
| CUE and REV: two 6 mm holes in 13.2 mm wells for caps over 6 x 6 mm tactile switches | `btn_*` |
| Knob: a 7.3 mm bushing hole for a vertical pot (the orange knob, the DevKit's ADC pin GPIO2) | `knob_x/y` |
| **Battery bay and faceplate**: a flush 74 x 45 x 1.6 mm blank plate over the top end, two M2 screws, and under it a 52.8 x 34.8 mm opening through the top plate into the tray. "A flat LiPo fits. There is no cell holder." | `plate_*`, `plate_screws`, the "Bay" cut |
| **Jack row on the top edge**, left to right: MIDI in, MIDI out, the USB window, line in, line out. Four 6.3 mm holes at z = 7.4 and x = 20.1, 31.6, 56.4, 67.9 mm; the USB window is 12.5 x 6.2 mm at z = 4.3 .. 10.5, centred at x = 44 | `jack_xs`, `jack_d`, `jack_z`, `usb_w/usb_h/usb_z0` |
| **Headphone jack** hole, 6.3 mm at z = 7.4, centred on the fader end (x = 44) | `hp_x` |

### The AS5600 stack-up, after the pad was lowered

The seat used to be at z = 6.2, which left 1.9 mm under the magnet and put the chip inside it. `encoder_pad_top` is now **3.5 mm**, and the M2 pilots start at z = 0.7 so they stay in the floor. The bill of materials asks for an **M3×6**, not an M3×12. A 12 mm shank from the platter underside ends in the floor.

This is still a guess until the breakout in your hand is measured. A 1.6 mm board plus a 1.5 mm chip on the 3.5 mm pad puts the chip face near z = 6.6. The ghost magnet is z = 8.1 to 10.6, about 1.5 mm of air. An M3×6 lands a little lower than that ghost. Shim with tape if the module is thinner. If it is thicker, the pad has to go lower again.

### Not drawn, or drawn for other electronics: what this build still needs

| Missing | Why, from the SCAD |
|---|---|
| **Controller bay for the DevKitC-1** | A lip on the tray floor, `devkit_w` 25.5 by `devkit_l` 70, origin x = 31.25, y = 165.4, USB toward the top wall. Drawn for a **headerless** board. Factory pin headers do not fit the 10 mm cavity. Measure the board in your hand before printing. |
| **USB window for two ports** | The window is `usb_w` x `usb_h` = 12.5 x 6.2 mm, one connector, centred on the jack row. The DevKitC-1 has two USB-C ports on one end (native USB for the computer, UART for flashing); a finished unit needs at least the native one reachable, and about 30 mm of window to expose both (an estimate: measure your board). |
| **Slide-pot slot** | The 8 x 64 mm slot (`fader_slot_w`, `fader_slot_l`; 51 mm of travel for the 13 mm stem) is for the loose hall-sensor fader block: its stem, `fader_lock` with the magnet, and two hall posts (`hall_x`, `hall_z_top` = 7.0). A slide pot (about 45-60 mm travel, 10 k linear) needs a mounting pad under the slot with screw holes for its body, and a cap or adapter that clamps its lever. None of that is drawn; the hall posts are not used. |
| **Hold pad mount** | Nothing in the SCAD: it has no foil recess, no contact holder and no wire path. For the foil-under-the-platter option the platter's underside sits **on** the well floor (z = 15.3 mm, no designed gap), so there is no room for foil without a recess, and the 4.2 mm top plate has no wire passage from the well to the cavity. For the brush option the only opening around the axle is the sleeve's 5.6 mm bore (z 10.7 .. 12.0), between the bearing's underside (z = 12.0) and the magnet zone (top at 10.6): nothing is drawn there either. See `PLATTER.md`. |
| **A pocket for the PCM5102A board**, if the DAC is fitted | Nothing. |
| **A window or light pipe for the status LED** | Nothing: the DevKitC-1's WS2812 sits inside the shell. |

The conductive-PLA platter itself needs no new drawing: print `platter.stl` in conductive filament (hardened or stainless
nozzle) instead of black PLA. Whether such a platter gives a usable hold signal is the open question
([`PLATTER.md`](PLATTER.md)).

### What the redrawn shell's new features have in the firmware and the wiring

The shell now has openings for more than this design implements. Honestly, per feature:

| Drawn in the shell | Firmware and wiring in this repo |
|---|---|
| **MIDI in** (3.5 mm TRS) | Implemented: UART1 RX on GPIO41 through an H11L1 optocoupler, Type A (`WIRING.md`). Never run on hardware. |
| **MIDI out** (3.5 mm TRS) | Implemented: UART1 TX on GPIO40, 3.3 V Type A (`WIRING.md`). Never run on hardware. |
| **USB-C window** | The DevKit's native USB port carries MIDI and, with a DAC, USB audio. One connector fits the window. The UART port stays inside; flash with the deck open. |
| **Line out** (3.5 mm TRS) | Implemented with the optional PCM5102A: I2S, USB Audio Class 1.0 (topology A, `docs/ARCHITECTURE.md` section 1). Never run on hardware. |
| **Line in** (3.5 mm TRS) | **No firmware and no wiring.** The PCM5102A is output-only; a line input needs an ADC or an I2S codec, an input path in the firmware and a USB audio *input* endpoint (the USB audio function here is host-to-device only). Not in the contract, not in the bill of materials. |
| **Headphone jack** | **No firmware and no wiring.** A headphone amplifier has to be designed (the PCM5102A's line-level output is not a headphone driver), and nothing in the firmware produces a cue or monitor mix to send it. |
| **Battery bay and faceplate** | **No firmware and no wiring.** There is no cell holder in the SCAD, and no charger, protection circuit, power path, switch or battery-level sense anywhere in the design: the unit is bus powered (`WIRING.md`, power). |
| 16 mm body, bearing sleeve | Mechanics only (the stack-up above). |

The bay is drawn for a headerless DevKitC-1. Still open: how the **hold pad** reaches the spinning platter (or whether to build
without one and live without scratch mode: `PLATTER.md`).

## Builder traps

1. **AS5600 breakouts are often strapped for 5 V.** Powering one from the DevKit's 3V3 without changing the strap runs the
   chip below spec (and a 5 V supply would pull the I2C lines to 5 V, which the S3 does not tolerate). Set the strap for
   3.3 V and measure the chip's 3.3 V pin (`WIRING.md`).
2. **The magnet must be diametrically magnetised** (poles on opposite sides of the round face), centred under the chip, at
   1 to 3 mm. The boot log says `magnet detected` / `too weak` / `too strong` / `not detected` (`WIRING.md`). The pad is at 3.5 mm for a headerless breakout. Measure the module before gluing.
3. **No answer from the AS5600 means no jog at all, until it answers.** The firmware says so loudly at boot (`JOG IS SILENT until
   one does`) and then asks again every 500 ms, so fixing the wiring is enough: no reboot (`WIRING.md`).
4. **Without a hold pad there is no touch note**, so DJ apps never enter scratch mode and the platter is a pitch-bend wheel
   (`PLATTER.md`).
5. **Before the hold pad works, a spinning platter shows jog; after it has worked once, a coasting platter is silent** (contract
   v1.4, `jogUntouched` AUTO: untouched motion is sent until the pad has registered a touch since power-up, then not). So the
   encoder can be brought up first with nothing to set, and a coast that suddenly goes quiet means the pad has seen a hand (a
   phantom touch counts too). `jogUntouched` = REPORT always sends it, SUPPRESS never; the proof resets at reboot
   (`docs/BRINGUP.md`).
6. **Status LED pin.** The WS2812 is on GPIO38 on DevKitC-1 v1.1 boards (the default build) and GPIO48 on v1.0; pick
   the revision in `menuconfig` (`EPDJ_BOARD_REV`). On any other board the LED stays dark until you set the pin.
7. **Two USB-C cables.** After the first flash the native port belongs to TinyUSB (the unit is a MIDI device, not a serial
   port), so reflashing and logs need the UART port, or BOOT held while pressing reset. Development uses both cables.
8. **Jacks that look alike.** The MIDI in, MIDI out, line in and line out holes are all 6.3 mm for 3.5 mm TRS. The MIDI jacks carry
   3.3 V on the ring (through 33 ohm) and the line jacks carry audio. Plugging a mixer cable into a MIDI jack, or a MIDI cable into a
   mixer input, puts DC on the wrong input. Label them (the blank faceplate does not cover the top-edge jacks), or use a different
   connector for MIDI. Only MIDI in, MIDI out and line out are wired in this design; the line in and headphone holes lead nowhere.
9. **GPIO47 and 48 sit in the module's VDD_SPI power domain.** GPIO47 is I2C SCL here. Fine on the usual 3.3 V modules; a
   module with 1.8 V flash would drive them at 1.8 V (`WIRING.md`; this is from memory of the datasheet, verify for your
   module).
10. **A stale `sdkconfig.esp32s3`** from before contract v1.3 keeps the touch ring and eight pads as the build's defaults
    (`firmware/esp32s3/README.md`); delete it, or pick the AS5600 platter and 1 pad in `menuconfig`.
11. **Experimental ring only:** printing the sectors (conductive PLA is abrasive and brittle; adjacent wedges must read open
    on a multimeter) and the eight thin wires from the platter to GPIO4..11, which must stay short (under 10 cm) and away
    from the ADC, DAC and MIDI wiring (`PLATTER.md`, last sections).
12. **Setting build options under PlatformIO**: `pio run -d firmware/esp32s3 -t menuconfig` (for the first-boot deck, the
    platter, the touch options and `EPDJ_AS5600_FILTERED_ANGLE`); see `firmware/esp32s3/README.md`.
