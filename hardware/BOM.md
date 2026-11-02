# EP-DJ3000 bill of materials: what changes from the Field Jog

Baseline: the BOM in [`field-jog/README.md`](../field-jog/README.md) (Pro Micro/Pico + AS5600 + bearing + magnetic
loose crossfader). This lists only the **delta**. Nothing here has been ordered or fitted; part choices are
suggestions, and the mechanical consequences for the printed shell are called out because they are not small.

## Replaced

| Field Jog | EP-DJ3000 | Notes |
|---|---|---|
| Raspberry Pi Pico or Pro Micro | **ESP32-S3 board with native USB**: ESP32-S3-DevKitC-1 for bring-up | The pin plan is in GPIO numbers, so any S3 board that exposes all 21 of GPIO1, 2, 4-17, 21, 38, 40, 41, 47 works (GPIO48 instead of 38 on a DevKitC-1 v1.0). **The shell's controller bay is sized for a Pico (21 x 51 mm). A DevKitC-1 is about 70 x 25 mm and does not fit.** Bring up on the bench. No smaller S3 board has been checked against the pin plan (many small ones break out far fewer pins), so for an enclosure either commit to the DevKitC-1 and draw the shell around it, or pick a board and rework `board_pins.h`: see [`README.md`](README.md). |
| 2x hall sensors + magnet in a loose fader block | **1x slide potentiometer**, 10 kΩ linear, ~45-60 mm travel | The shell does cut an 8 x 64 mm slot (for the loose fader's magnet stem, with hall posts), which a slide pot could reuse. **Missing: a mounting pad for the pot and a lever/cap adapter.** Not drawn. |
| 683ZZ bearing, M3 axle, diametric magnet, AS5600 (free-spinning platter) | **Conductive-PLA platter in 8 isolated sectors**, fixed to the deck (no bearing) | Default `platterSource = TOUCH_RING`. The bearing/axle/magnet/AS5600 stay as a supported **option**; see below. **There is no printable file for the sector platter** (sectors, gaps, contact bosses, wire passages): the SCAD platter is a plain 76 mm disc. |

## Added

| Part | Qty | Why |
|---|---|---|
| Conductive PLA filament (carbon-loaded; Protopasta-type) | ~30 g | platter sectors. Abrasive: use a hardened or stainless nozzle. See [`PLATTER.md`](PLATTER.md). |
| M2 x 4 screws or brass heat-set inserts (or copper-tape tabs) | 8-9 | one contact per sector, plus the optional ground plane |
| Hook-up wire, thin, 28-30 AWG, ~15 cm each | 8 | sector to GPIO4..11 |
| 100 nF ceramic capacitor | 2-4 | wiper filters (fader, knob); optionally across CUE/REV |
| Resistor 330-1 kΩ, 0603/THT | 8 (optional) | series resistor at each touch input, ESD/EMI |
| **PCM5102A breakout** ("GY-PCM5102" type) | 1 | USB audio output, I2S, no I2C, no MCLK. Only when you want audio. |
| 3.5 mm TRS jack (stereo, panel mount) | 1 | line out to the EP-136 / any mixer. Only for topology A (`docs/ARCHITECTURE.md` section 1). **Looks identical to the MIDI jack and must not be confused with it** (see `README.md`) |
| Pin header/jumper for the DAC strap | 1 | GPIO14 to GND on the DAC board |
| 100 Ω resistor | 2 | series with each line-out channel |
| **TRS MIDI**: 3.5 mm TRS jack | 1 | the Field Jog has a blank hole for it. Type A wiring. |
| H11L1 optocoupler (3.3 V capable) | 1 | MIDI IN isolation. Not 6N137/6N138 (5 V parts). |
| 1N4148 diode | 1 | MIDI IN LED reverse protection |
| 220 Ω, 1 kΩ resistors | 1 each | MIDI IN |
| 33 Ω (0.5 W), 10 Ω resistors | 1 each | MIDI OUT at 3.3 V (see WIRING.md) |
| 74LVC1G34 (or any 3.3 V buffer) | 1 (optional) | MIDI OUT driver |
| WS2812 / "NeoPixel" LED | 0-1 | already on the DevKitC-1: **GPIO38 on v1.1 boards (the default build), GPIO48 on v1.0**; choose the revision in `menuconfig` (`EPDJ_BOARD_REV`). On another board a WS2812 has to be wired to the pin `kStatusLed` names |

## Kept as an option (AS5600 platter)

AS5600 breakout, 683ZZ bearing, M3x12 axle, 6x2.5 mm diametric magnet: exactly as in the Field Jog README. Wire
GPIO21/GPIO47 and a single hold pad on GPIO4 (see WIRING.md). If you will never build this variant, delete it later;
it is about one class in the firmware.

## Cabling

* USB-C cable, data capable, to the DJ computer (native port). A second cable to the DevKit's UART port is only for
  flashing/logging, but you need it for as long as you are developing: once the firmware runs, the native port belongs
  to TinyUSB.
* For two decks there are two audio topologies (`docs/ARCHITECTURE.md` section 1), and the parts differ:
  * **A, a sound card per deck**: two complete units with a DAC and a line-out jack each, **each with its own USB data
    cable to the computer**, each line out into one mixer channel. A deck on a USB charger has no USB audio stream, so
    its DAC is silent: one-cable mode does not apply.
  * **B, MIDI-only decks**: no DAC or line-out jack. One USB cable per deck, or one in total with the second deck
    powered from any charger and bridged over the radio (`docs/LINK.md`). Audio comes from another interface.
* Neither has been tried.
