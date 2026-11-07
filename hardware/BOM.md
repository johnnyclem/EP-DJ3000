# EP-DJ3000 bill of materials: what changes from the Field Jog

Baseline: the BOM in [`field-jog/README.md`](../field-jog/README.md) (Pro Micro/Pico + AS5600 + bearing + magnetic
loose crossfader), for the shell as **redrawn to 16 mm** (read at shell commit 15417b6). This lists only the **delta**. Nothing here has been ordered or fitted;
part choices are suggestions, and the mechanical consequences for the printed shell are called out because they are not small
(`README.md` has the full list of what is not drawn, and which of the jack holes the design wires).

## Kept from the Field Jog (the platter is the AS5600 build)

| Part | Qty | Notes |
|---|---|---|
| 683ZZ bearing, 3 x 7 x 3 mm | 1 | pressed into the deck, as in the Field Jog README |
| M3 button-head screw | 1 | the axle; head in the platter's pocket, shank down through the bearing. Shell commit 15417b6 moved the shell's list from M3 x 12 to **M3 x 6** and lowered the AS5600 pad to 3.5 mm. The margin is thin on paper (a gap of -0.1 to +0.7 mm depending on where the head seats; the head height used is from memory), so **buy an M3 x 5 as well as an M3 x 6** and test-fit: [cross-check in `README.md`](README.md#stack-up-cross-check-on-paper-shell-15417b6-nothing-measured) |
| 6 x 2.5 mm **diametric** magnet | 1 | glued to the shank's end. **Diametrically** magnetised (poles on opposite sides of the round face); an axial magnet (poles on the flat faces) gives the AS5600 nothing to measure. Field Jog's own list says "diametric" for the AS5600 and "axial is fine" for the fader lock: do not mix the two up |
| MT6701 breakout | 1 | MagnTek, I2C address 0x06 (the protocol still names the platter source AS5600). VDD, GND, SDA GPIO21, SCL GPIO47, Analog/PWM open. 3.3 V, no pin header. Four M2 holes on a 17.5 mm square; the SCAD pad is 26 mm square with its top at 3.5 mm. Board measured at 23 mm square from the photo next to the old pad |
| M2 screws | 4 | the MT6701 board to its pad |
| 2 x 6 x 6 mm tactile switches, vertical 9 mm 10 k pot for the knob | 2, 1 | CUE/REV and the orange knob, as in the Field Jog. The shell has three more stems (PLAY, LOOP IN, LOOP OUT) that nothing reads: see below |

## Replaced

| Field Jog | EP-DJ3000 | Notes |
|---|---|---|
| Raspberry Pi Pico or Pro Micro | **ESP32-S3-DevKitC-1, headerless** (the owner's choice; the firmware targets it) | The pin plan is in GPIO numbers: 21 of them (GPIO1, 2, 4-17, 21, 38, 40, 41, 47; GPIO48 instead of 38 on a v1.0 board), all on the DevKitC-1's headers (checked on paper in `WIRING.md`). The shell's lip (15417b6) is about 70 x 25.5 mm for a **headerless** board: factory pin headers do not fit the 16 mm shell, so buy a board without headers or clip them, and measure yours. No other S3 board has been checked against the pin plan (many small ones break out far fewer pins). |
| 2x hall sensors + magnet in a loose fader block | **1x slide potentiometer**, 10 kΩ linear, ~45-60 mm travel | The shell does cut an 8 x 64 mm slot (for the loose fader's magnet stem, with hall posts), which a slide pot could reuse. **Missing: a mounting pad for the pot and a lever/cap adapter.** Not drawn. |
| Platter printed in (black) PLA | The same `platter.stl` printed in **conductive PLA** | Still the platter, now with two jobs: it spins, and it has to carry the finger's hold signal to a fixed touch pad. Not wired to the pad directly (it rotates); `PLATTER.md` lists the two ways to try, both unvalidated. |

## Added

| Part | Qty | Why |
|---|---|---|
| Conductive PLA filament (carbon-loaded; Protopasta-type) | ~25 g | the platter: a 76 mm x 3.8 mm disc is about 17 cm³, about 20-25 g at 100 % infill (an estimate from volume and a density near 1.2-1.3 g/cm³). Abrasive: use a hardened or stainless nozzle. See [`PLATTER.md`](PLATTER.md). |
| Copper foil tape (or a thin copper sheet) | a few cm² | **hold pad option (a)**: a stationary pad or ring under the platter, wired to GPIO4. Unvalidated. |
| Spring finger, brush or pogo pin | 1 | **hold pad option (b)**: a contact on the axle. Unvalidated. Buy either (a) or (b), or both to try. |
| Thin hook-up wire, 28-30 AWG, ~10 cm | 1 | hold pad to GPIO4 |
| Resistor 330 Ω-1 kΩ | 1 (optional) | series resistor at the hold-pad input, ESD/EMI |
| Resistors 4.7 kΩ | 2 (only if the AS5600 breakout has no pull-ups) | SDA and SCL to 3V3 |
| 100 nF ceramic capacitor | 2-4 | wiper filters (fader, knob); optionally across CUE/REV |
| **PCM5102A breakout** ("GY-PCM5102" type) | 1 | USB audio output, I2S, no I2C, no MCLK. Only when you want audio. |
| 3.5 mm TRS jack (stereo, panel mount) | 1 | line out to the EP-136 / any mixer (the rightmost of the top-edge holes). Only for topology A (`docs/ARCHITECTURE.md` section 1). **Looks identical to the MIDI jacks and must not be confused with them** (see `README.md`) |
| Pin header/jumper for the DAC strap | 1 | GPIO14 to GND on the DAC board |
| 100 Ω resistor | 2 | series with each line-out channel |
| **TRS MIDI**: 3.5 mm TRS jack | 2 | MIDI in and MIDI out, the two leftmost holes of the top edge. Type A wiring. |
| H11L1 optocoupler (3.3 V capable) | 1 | MIDI IN isolation. Not 6N137/6N138 (5 V parts). |
| 1N4148 diode | 1 | MIDI IN LED reverse protection |
| 220 Ω, 1 kΩ resistors | 1 each | MIDI IN |
| 33 Ω (0.5 W), 10 Ω resistors | 1 each | MIDI OUT at 3.3 V (see WIRING.md) |
| 74LVC1G34 (or any 3.3 V buffer) | 1 (optional) | MIDI OUT driver |
| WS2812 / "NeoPixel" LED | 0-1 | already on the DevKitC-1: **GPIO38 on v1.1 boards (the default build), GPIO48 on v1.0**; choose the revision in `menuconfig` (`EPDJ_BOARD_REV`). On another board a WS2812 has to be wired to the pin `kStatusLed` names |

## In the shell, not in this design (no part is listed because nothing supports them)

The redrawn shell has holes and space for more than the firmware does anything with (`README.md`, "Shell controls vs firmware").
There is deliberately **no bill-of-materials line** for:

| Drawn in the shell | Why there is no part |
|---|---|
| a **line in** jack (top edge) | the PCM5102A is output-only; a line input needs an ADC or an I2S codec, firmware and a USB audio input path |
| a **headphone** jack (fader end) | a headphone amplifier and a cue/monitor mix are not designed |
| a flat **LiPo** in the battery bay | no cell holder, charger, protection, power path, switch or battery sense is designed; the unit is bus powered |
| the **PLAY, LOOP IN, LOOP OUT** caps and the two LEDs beside the loop caps | no firmware reads the buttons or drives the LEDs; a proposal is in `README.md` (not implemented). Leave the stems unfitted |
| the 0.91" **SSD1306 OLED** | no driver and nothing in the contract to show |

## Only for the experimental touch ring (not the current path)

| Part | Qty | Why |
|---|---|---|
| Conductive PLA, ~30 g | | the eight sector wedges (instead of the solid disc), printed multi-material or separately and pressed into an insulating disc. See [`PLATTER.md`](PLATTER.md), last sections |
| M2 x 4 screws or brass heat-set inserts (or copper-tape tabs) | 8-9 | one contact per sector, plus the optional ground plane |
| Hook-up wire, thin, 28-30 AWG, ~15 cm each | 8 | sector to GPIO4..11 |
| Resistor 330-1 kΩ, 0603/THT | 8 (optional) | series resistor at each touch input, ESD/EMI |

The ring has no bearing, magnet or encoder: a ring build drops the 683ZZ, the axle, the magnet and the AS5600.

## Cabling

* USB-C cable, data capable, to the DJ computer (native port). A second cable to the DevKit's UART port is only for
  flashing/logging, but you need it for as long as you are developing: once the firmware runs, the native port belongs
  to TinyUSB. In the shell only the native port reaches the window, so the UART cable needs the deck open
  (`README.md`, builder trap 7).
* For two decks there are two audio topologies (`docs/ARCHITECTURE.md` section 1), and the parts differ:
  * **A, a sound card per deck**: two complete units with a DAC and a line-out jack each, **each with its own USB data
    cable to the computer**, each line out into one mixer channel. A deck on a USB charger has no USB audio stream, so
    its DAC is silent: one-cable mode does not apply.
  * **B, MIDI-only decks**: no DAC or line-out jack. One USB cable per deck, or one in total with the second deck
    powered from any charger and bridged over the radio (`docs/LINK.md`). Audio comes from another interface.
* Neither has been tried.
