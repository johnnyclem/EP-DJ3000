# EP-DJ3000 hardware: status, what is missing, builder traps

**Nothing here has been printed, wired or measured, and nothing has run on a chip.** Shell facts are read from
`field-jog/field-jog.scad` at commit 15417b6 (`main`); re-check them if the shell changes again. The current build is a **free-spinning platter on a bearing with an
AS5600 magnetic encoder** under it, **one touch pad** (the hold pad) for touch/hold detection, a slide pot, a knob, CUE and
REV, an optional PCM5102A DAC and a TRS MIDI jack, all on an ESP32-S3 DevKitC-1. The mechanical design in the repo is the
Field Jog shell (`field-jog/`), **redrawn to 16 mm** with a bearing sleeve, a battery bay under a removable faceplate, a jack
row on the top edge and a headphone jack on the fader end. It was drawn for the platter half of this (bearing, axle, magnet,
AS5600 pad) and for different electronics around it. The controller bay is now a lip for a headerless ESP32-S3-DevKitC-1 (about 70 × 25.5 mm; measure yours). Factory pin headers do not fit the 16 mm shell. The segmented touch ring is kept as an experiment
([`PLATTER.md`](PLATTER.md), last sections); it is not the path to follow now. Its bench coupon is
[`platter-test/`](platter-test/README.md): not a shell, and nobody has printed it.

**The board is decided:** the owner chose the ESP32-S3-DevKitC-1, which is what the firmware targets. (History, one line: the
bay was Pico-sized until `e04c912`, then a lip for an ESP32-Audio-Kit V2.2 until `b06055e`; that board is a classic ESP32 with no
USB device mode and was dropped.) The pin plan was also checked on paper against Espressif's DevKitC-1 header tables: every
planned pin is on a header and none is USB, console, strapping or flash/PSRAM
([`WIRING.md`](WIRING.md#checked-against-espressifs-header-tables)). Not checked on a board.

| File | What |
|---|---|
| [`WIRING.md`](WIRING.md) | Pin table (the source of truth is `board_pins.h`) and its check against Espressif's header tables, power, **AS5600 wiring, its filter configuration, the 3.3 V / 5 V strap trap and the bring-up log lines**, DAC, TRS MIDI, LED |
| [`TWIN.md`](TWIN.md) | The two-platter shell: MCP23017 buttons, ADS1115 pitch faders, second MT6701 on its own I2C bus |
| [`PLATTER.md`](PLATTER.md) | The AS5600 platter and its mechanics; the two ways to get a hold signal off a spinning conductive-PLA platter (both unvalidated); what you lose without a hold pad; then the experimental touch ring |
| [`platter-test/`](platter-test/README.md) | Printable bench coupon for the experimental touch ring: carrier, 8 wedges, numbered stand. Not a shell; nobody has printed it |
| [`BOM.md`](BOM.md) | What changes from the Field Jog bill of materials |

## The mechanical delta against `field-jog/field-jog.scad` (the 16 mm redraw)

Each line below was read from the **current** `field-jog/field-jog.scad` (variable names in code font), not from older text.
When `field-jog/README.md` and the SCAD disagree, this file follows the SCAD. `field-jog/README.md` says what the printed
parts and the bill of materials are. The SCAD and the STLs were not edited for any of this.

### Already drawn, usable as it is

| What | In the SCAD |
|---|---|
| Body 88 x 240 x **16** mm (the EP-133 / EP-136 thickness, so the cases sit flush), 4.2 mm top plate, 11.8 mm tray with a 1.8 mm floor (10.0 mm of cavity), 2.6 mm walls, four M2.5 screw bosses, Technic pin holes (positions unchecked against real EP units, per `field-jog/README.md`), feet | `W`, `H`, `body_h`, `skin`, `tray_h`, `floor_t`, `wall`, `screw_positions()`, `technic_ys` |
| 76 mm platter, 3.8 mm thick, in a 0.7 mm well (0.55 mm diametral clearance), centred at x = 44, y = 148 mm; it stands 3.1 mm proud of the deck; `platter.stl` exists | `platter_d`, `platter_t`, `well_d`, `platter_clear`, `platter_cx/cy` |
| **683ZZ sleeve** hanging below the deck: a 13.55 mm tube from the well floor (z = 15.3) down to **z = 10.7**, 1.1 mm below the top plate's underside, so a 3 mm bearing fits a 16 mm case. 7.15 mm pocket for the 7.00 mm OD, race at z = 12.0 to 15.0 (its top 0.3 mm below the platter), a 1.3 mm ledge under it with a 5.6 mm bore for the M3 shank | `bearing_d`, `bearing_h`, `bearing_drop`, `ledge`, `ledge_z`, `shaft_hole` |
| M3 button-head pocket in the platter's underside (6.8 mm wide, 2.15 mm deep, opening downward) | `platter()` |
| AS5600 pad: 26 x 20 mm, its top (the board seat) at **z = 3.5 mm**, two blind 2.1 mm holes for M2, 16 mm apart, from z = 0.7 up through the pad, under the platter centre | `encoder_pad_*` |
| The magnet's target zone: 6 mm x 2.5 mm at **z = 8.1 .. 10.6 mm** (a ghost in the preview) | `ghosts()` |
| **Five round caps**, each a 4.4 mm stem through a 5.2 mm hole in the skin onto a 6 x 6 mm tactile switch glued underneath: PLAY (16 mm cap, hole at x = 17, y = 36), CUE (16 mm, 17, 60), LOOP IN (10 mm, 17, 86), LOOP OUT (10 mm, 17, 102), REV (10 mm, 71, 48). **Two 3.1 mm LED holes** beside LOOP IN and LOOP OUT (x = 28, y = 86 and 102). What the firmware does with them: "Shell controls vs firmware" below | `play_*`, `cue_*`, `in_*`, `out_*`, `rev_*`, `play_d`, `loop_d`, `led_d` |
| Knob: a 7.3 mm bushing hole for a vertical pot (the orange knob, the DevKit's ADC pin GPIO2) | `knob_x/y` |
| **Battery bay and faceplate**: a flush 74 x 45 x 1.6 mm plate over the top end (with a 23.2 x 6.4 mm window for the 0.91" SSD1306 OLED and a clearance hole for the knob), two M2 screws, and under it a 52.8 x 34.8 mm opening through the top plate into the tray. "A flat LiPo fits. There is no cell holder." | `plate_*`, `plate_screws`, the "Bay" cut |
| **Jack row on the top edge**, left to right: MIDI in, MIDI out, the USB window, line in, line out. Four 6.3 mm holes at z = 7.4 and x = 20.1, 31.6, 56.4, 67.9 mm; the USB window is 12.5 x 6.2 mm at z = 4.3 .. 10.5, centred at x = 44 | `jack_xs`, `jack_d`, `jack_z`, `usb_w/usb_h/usb_z0` |
| **Headphone jack** hole, 6.3 mm at z = 7.4, centred on the fader end (x = 44) | `hp_x` |

### The AS5600 stack-up, after the pad was lowered

The seat used to be at z = 6.2, which left 1.9 mm under the magnet and put the chip inside it. `encoder_pad_top` is now **3.5 mm**, and the M2 pilots start at z = 0.7 so they stay in the floor. The bill of materials asks for an **M3×6**, not an M3×12. A 12 mm shank from the platter underside ends in the floor.

This is still a guess until the breakout in your hand is measured. A 1.6 mm board plus a 1.5 mm chip on the 3.5 mm pad puts the chip face near z = 6.6. The ghost magnet is z = 8.1 to 10.6, about 1.5 mm of air. An M3×6 lands a little lower than that ghost. Shim with tape if the module is thinner. If it is thicker, the pad has to go lower again.

### Stack-up cross-check on paper (shell 15417b6, nothing measured)

The two paragraphs above (and the shell README's "roughly 2 mm") put the magnet where the SCAD draws its ghost. The real height
depends on where the bolt head seats, which the SCAD does not fix, so the same SCAD gives a different answer.

From the SCAD: the platter's underside is z = 15.3 (`body_h` 16 - `well_d` 0.7); its M3 head pocket is 2.15 mm deep, so the
pocket ceiling is z = 17.45; the 683ZZ race top is z = 15.0; the sleeve bottom is z = 10.7, and the 6 mm magnet is wider than the
5.6 mm bore, so **the magnet top must stay at or below z = 10.7**. From memory, **measure yours**: an M3 button head is 1.65 mm
tall; the breakout is a 1.6 mm board plus a 1.5 mm chip on the 3.5 mm seat, so the chip face is at z = 6.6.

The shank starts at z = 15.0 if the head rests on the bearing race, or z = 17.45 - 1.65 = 15.8 if it is pressed against the pocket
ceiling. The 6 x 2.5 mm magnet is glued on the shank end, so magnet bottom = shank start - bolt length - 2.5, and air gap = magnet
bottom - 6.6.

| Axle | Head on the race (shank from 15.0): gap, magnet top | Head at the pocket ceiling (from 15.8): gap, magnet top |
|---|---|---|
| M3x6 | **-0.1 mm** (the magnet touches the chip), 9.0 | +0.7 mm, 9.8 |
| M3x5 | +0.9 mm, 10.0 | +1.7 mm, **10.8** (0.1 mm into the ledge) |
| M3x4 | not viable: magnet top 11.0, inside the sleeve | not viable: 11.8 |

The AS5600 wants a gap of roughly 1 to 3 mm (the original author's figure in `field-jog/README.md`; the datasheet's range is from
memory). On paper only the M3x5 seated against the pocket ceiling lands inside it, with no margin against the ledge; the M3x6 sits
between touching the chip and 0.7 mm. **Paper cannot settle this.** Buy both an M3x5 and an M3x6, test-fit with the real breakout,
and let the boot log judge: `magnet detected`, `magnet too weak`, `magnet too strong`, `magnet not detected` or `magnet unknown
(status register unreadable)` on the `platter:` line, plus a WARN `AS5600 magnet <status> (STATUS 0x..)` for anything but
detected (`WIRING.md`, bring-up table). Too much air: tape shims under the breakout. Too little: shave the pad, i.e. lower
`encoder_pad_top`. Nothing has run on a chip, so none of those log lines has been seen from a real one.

### Not drawn, or drawn for other electronics: what this build still needs

| Missing | Why, from the SCAD |
|---|---|
| **Controller bay for the DevKitC-1** (drawn, headerless boards only) | A lip on the tray floor, `devkit_w` 25.5 by `devkit_l` 70, origin x = 31.25, y = 165.4, USB toward the top wall. Drawn for a **headerless** board. Factory pin headers do not fit the 10 mm cavity. Measure the board in your hand before printing. |
| **USB window for two ports** | The window is `usb_w` x `usb_h` = 12.5 x 6.2 mm, one connector, centred on the jack row. The DevKitC-1 has two USB-C ports on one end (native USB for the computer, UART for flashing); a finished unit needs at least the native one reachable, and about 30 mm of window to expose both (an estimate: measure your board). |
| **Slide-pot slot** | The 8 x 64 mm slot (`fader_slot_w`, `fader_slot_l`; 51 mm of travel for the 13 mm stem) is for the loose hall-sensor fader block: its stem, `fader_lock` with the magnet, and two hall posts (`hall_x`, `hall_z_top` = 7.0). A slide pot (about 45-60 mm travel, 10 k linear) needs a mounting pad under the slot with screw holes for its body, and a cap or adapter that clamps its lever. None of that is drawn; the hall posts are not used. |
| **Hold pad mount** | Nothing in the SCAD: it has no foil recess, no contact holder and no wire path. For the foil-under-the-platter option the platter's underside sits **on** the well floor (z = 15.3 mm, no designed gap), so there is no room for foil without a recess, and the 4.2 mm top plate has no wire passage from the well to the cavity. For the brush option the only opening around the axle is the sleeve's 5.6 mm bore (z 10.7 .. 12.0), between the bearing's underside (z = 12.0) and the magnet zone (top at 10.6): nothing is drawn there either. See `PLATTER.md`. |
| **A pocket for the PCM5102A board**, if the DAC is fitted | Nothing. |
| **A window or light pipe for the status LED** | Nothing: the DevKitC-1's WS2812 sits inside the shell. |

The conductive-PLA platter itself needs no new drawing: print `platter.stl` in conductive filament (hardened or stainless
nozzle) instead of black PLA. Whether such a platter gives a usable hold signal is the open question
([`PLATTER.md`](PLATTER.md)). The segmented ring is a different print: the bench coupon is
[`platter-test/`](platter-test/README.md).

### Shell controls vs firmware

The shell has openings and caps for more than this design implements. Honestly, per control (shell at 15417b6; firmware as in
this repo; nothing has run on a chip):

| Control or jack | In the SCAD | Firmware / MIDI today | Smallest next step |
|---|---|---|---|
| Platter and AS5600 pad | 76 mm platter over the 683ZZ sleeve, M3 head pocket, AS5600 pad (seat z = 3.5) | Implemented: jog CC 22 from the AS5600. Simulation-tested only | Print, then test-fit an M3x5 and an M3x6 (stack-up cross-check above) |
| Hold pad (touch, hold, tap) | Nothing: no contact, foil recess or wire path | Implemented for one pad on GPIO4: touch note 38, tap -> play note 39. Simulation-tested only | Choose and draw the rotating contact (`PLATTER.md`) |
| **PLAY** cap | 16 mm cap, stem hole | **No button.** Play note 39 exists only as the platter-tap pulse (and as the host's feedback in), so while the hold pad does not work nothing can send play | Proposal below |
| **CUE** and **REV** caps | 16 mm and 10 mm caps, stem holes | Implemented: CUE note 36 on GPIO12, REV note 37 on GPIO13 | Glue a 6 x 6 mm tactile under each stem |
| **LOOP IN** and **LOOP OUT** caps | 10 mm caps, stem holes | **None.** No pin, no note | Proposal below |
| Two **LEDs** beside LOOP IN / LOOP OUT | Two 3.1 mm holes; no LED part chosen | **None.** The only LED the firmware drives is the DevKit's own WS2812, inside the shell | Proposal below (host echo) |
| Knob | 7.3 mm bushing hole | Implemented: CC 24, wiper on GPIO2 | Fit a 9 mm vertical 10 k pot |
| Scratch fader | Loose-fader slot, 8 x 64 mm (51 mm of travel), two hall posts; **no slide-pot mount** | Reads a 10 k slide pot on GPIO1 (default role PITCH, CC 25/57). **No hall-sensor input** | Draw a pot pad and a lever cap (firmware unchanged), or decide to build the hall fader and design its input (not designed) |
| **MIDI in** / **MIDI out** (3.5 mm TRS) | Two 6.3 mm holes | Implemented: UART1 RX on GPIO41 through an H11L1 optocoupler, UART1 TX on GPIO40, 3.3 V Type A (`WIRING.md`). Never run on hardware | Label the jacks (builder trap 8) |
| **USB-C window** | 12.5 x 6.2 mm, native port only | The DevKit's native USB port carries MIDI and, with a DAC, USB audio. The UART port stays inside; flash with the deck open | Flash over UART before closing; prove `REBOOT(bootloader)` first (`docs/BRINGUP.md`) |
| **Line out** (3.5 mm TRS) | 6.3 mm hole | Implemented with the optional PCM5102A: I2S, USB Audio Class 1.0 (topology A, `docs/ARCHITECTURE.md` section 1). Never run on hardware | Fit the DAC; draw a pocket for its board |
| **Line in** (3.5 mm TRS) | 6.3 mm hole | **No firmware and no wiring.** The PCM5102A is output-only; a line input needs an ADC or an I2S codec, an input path in the firmware and a USB audio *input* endpoint (the USB audio function here is host-to-device only). Not in the contract, not in the bill of materials | Drop the hole, or design the input |
| **Headphone jack** | 6.3 mm hole, fader end | **No firmware and no wiring.** A headphone amplifier has to be designed (the PCM5102A's line-level output is not a headphone driver), and nothing in the firmware produces a cue or monitor mix to send it | Drop the hole, or design the amp and the mix |
| **OLED** (0.91" SSD1306, 128x32, I2C 0x3C) | 23.2 x 6.4 mm window in the faceplate | **No driver** and nothing in the contract to show | Proposal below (shares the AS5600's I2C bus) |
| microSD playback, varispeed | Nothing drawn | **None** (named in `field-jog/README.md` only) | Out of scope for this repo |
| **Battery bay** | 52.8 x 34.8 mm opening under the faceplate, no cell holder | **No firmware and no wiring.** No charger, protection circuit, power path, switch or battery-level sense anywhere in the design: the unit is bus powered (`WIRING.md`, power) | Leave it empty |

The bay is drawn for a headerless DevKitC-1. Still open: how the **hold pad** reaches the spinning platter (or whether to build
without one and live without scratch mode: `PLATTER.md`).

#### Proposal: PLAY, LOOP IN, LOOP OUT and their LEDs (NOT IMPLEMENTED)

Needs the owner's go-ahead and a contract revision. Nothing below exists in the firmware, the tests, the macOS app, the
mappings or `protocol/`.

* **PLAY cap** -> the existing `play` note 39, sent as the same pulse (127, then 0) a platter tap sends. No new note.
* **LOOP IN / LOOP OUT caps** -> two new notes (36-39 and 41 are taken; numbers to be assigned in the contract), velocity 127 down
  and 0 up like CUE and REV. The host echoes each loop's state back on the same note (velocity above 0 = lit), and the unit drives
  the two LEDs from that echo, the same pattern as the contract's `cue` note. Mixxx has `loop_in` and `loop_out` controls to bind
  them to; the other three apps' equivalents have not been looked up.
* **Pins.** Three buttons and two LEDs are five GPIOs. Free and not special: GPIO18, 39 and 42 (three), then GPIO5-11, which are
  the experimental touch ring's sectors 1-7: idle with the AS5600 platter, **taken if the ring is used**, and reserved for it by
  `board_pins.h` today, so using them needs a pin-plan change and retires the ring option. Each LED needs a series resistor
  (value from the LED chosen).
* **OLED.** The SSD1306 shares the AS5600's I2C bus (GPIO21 / GPIO47; AS5600 at 0x36, SSD1306 at 0x3C), so no new pins. It needs a
  driver and a contract message for what to show. A full 128x32 frame is 512 bytes, about 12 ms of bus time at 400 kHz (arithmetic,
  not measured), so display traffic must be kept out of the 1 ms loop that reads the AS5600.

## Builder traps

1. **AS5600 breakouts are often strapped for 5 V.** Powering one from the DevKit's 3V3 without changing the strap runs the
   chip below spec (and a 5 V supply would pull the I2C lines to 5 V, which the S3 does not tolerate). Set the strap for
   3.3 V and measure the chip's 3.3 V pin (`WIRING.md`).
2. **The magnet must be diametrically magnetised** (poles on opposite sides of the round face), centred under the chip, at
   1 to 3 mm. The boot log says `magnet detected` / `too weak` / `too strong` / `not detected` (`WIRING.md`). The pad is at
   3.5 mm for a headerless breakout and the margin is thin on paper: measure the module, and buy an M3x5 and an M3x6 (the
   cross-check above).
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
7. **Two USB-C cables, and only one of them reaches the window.** After the first flash the native port belongs to TinyUSB
   (the unit is a MIDI device, not a serial port, and not the ROM's download interface), so reflashing and logs need the UART
   port, or BOOT held while pressing reset. The shell's window exposes only the native port. So: flash over the UART port with the
   deck open; **prove `REBOOT(bootloader)` from the app before closing the deck for good** (`docs/BRINGUP.md`, step 2: it is
   implemented and has never been tried on a chip); until it is proven, a closed deck can only be updated by opening it for
   BOOT + RESET.
8. **Jacks that look alike.** The MIDI in, MIDI out, line in and line out holes are all 6.3 mm for 3.5 mm TRS. The MIDI jacks carry
   3.3 V on the ring (through 33 ohm) and the line jacks carry audio. Plugging a mixer cable into a MIDI jack, or a MIDI cable into a
   mixer input, puts DC on the wrong input. Label them (the faceplate does not cover the top-edge jacks), or use a different
   connector for MIDI. Only MIDI in, MIDI out and line out are wired in this design; the line in and headphone holes lead nowhere.
9. **GPIO47 and 48 sit in the module's VDD_SPI power domain.** GPIO47 is I2C SCL here. Fine on the usual 3.3 V modules; a
   module with 1.8 V flash would drive them at 1.8 V (`WIRING.md`; this is from memory of the datasheet: check your module
   variant in its datasheet and avoid 1.8 V modules).
10. **A stale `sdkconfig.esp32s3`** from before contract v1.3 keeps the touch ring and eight pads as the build's defaults
    (`firmware/esp32s3/README.md`); delete it, or pick the AS5600 platter and 1 pad in `menuconfig`.
11. **Experimental ring only:** printing the sectors (conductive PLA is abrasive and brittle; adjacent wedges must read open
    on a multimeter) and the eight thin wires from the platter to GPIO4..11, which must stay short (under 10 cm) and away
    from the ADC, DAC and MIDI wiring (`PLATTER.md`, last sections).
12. **Setting build options under PlatformIO**: `pio run -d firmware/esp32s3 -t menuconfig` (for the first-boot deck, the
    platter, the touch options and `EPDJ_AS5600_FILTERED_ANGLE`); see `firmware/esp32s3/README.md`.
