# EP-DJ3000 bill of materials: what changes from the Field Jog

Baseline: the BOM in [`field-jog/README.md`](../field-jog/README.md) (Pro Micro/Pico + AS5600 + bearing + magnetic
loose crossfader). This lists only the **delta**. Nothing here has been ordered or fitted; part choices are
suggestions, and the mechanical consequences for the printed shell are called out because they are not small.

## Replaced

| Field Jog | EP-DJ3000 | Notes |
|---|---|---|
| Raspberry Pi Pico or Pro Micro | **ESP32-S3 board with native USB**: ESP32-S3-DevKitC-1 for bring-up | The pin plan is in GPIO numbers, so any S3 board that exposes GPIO1, 2, 4-17, 21, 40, 41, 47, 48 works. **The shell's controller bay is sized for a Pico (21 x 51 mm). A DevKitC-1 is about 70 x 25 mm and does not fit.** Bring up on the bench; for the enclosure use a smaller S3 board with those pins or a bare module on a small PCB. |
| 2x hall sensors + magnet in a loose fader block | **1x slide potentiometer**, 10 kΩ linear, ~45-60 mm travel | The shell has a magnetic loose-fader cavity, not a slider slot. Needs a new slot and mounting pad in `field-jog.scad`. Not done. |
| 683ZZ bearing, M3 axle, diametric magnet, AS5600 (free-spinning platter) | **Conductive-PLA platter in 8 isolated sectors**, fixed to the deck (no bearing) | Default `platterSource = TOUCH_RING`. The bearing/axle/magnet/AS5600 stay as a supported **option**; see below. |

## Added

| Part | Qty | Why |
|---|---|---|
| Conductive PLA filament (carbon-loaded; Protopasta-type) | ~30 g | platter sectors. Abrasive: use a hardened or stainless nozzle. See [`PLATTER.md`](PLATTER.md). |
| M2 x 4 screws or brass heat-set inserts (or copper-tape tabs) | 8-9 | one contact per sector, plus the optional ground plane |
| Hook-up wire, thin, 28-30 AWG, ~15 cm each | 8 | sector to GPIO4..11 |
| 100 nF ceramic capacitor | 2-4 | wiper filters (fader, knob); optionally across CUE/REV |
| Resistor 330-1 kΩ, 0603/THT | 8 (optional) | series resistor at each touch input, ESD/EMI |
| **PCM5102A breakout** ("GY-PCM5102" type) | 1 | USB audio output, I2S, no I2C, no MCLK. Only when you want audio. |
| 3.5 mm TRS jack (stereo, panel mount) | 1 | line out to the EP-136 / any mixer |
| Pin header/jumper for the DAC strap | 1 | GPIO14 to GND on the DAC board |
| 100 Ω resistor | 2 | series with each line-out channel |
| **TRS MIDI**: 3.5 mm TRS jack | 1 | the Field Jog has a blank hole for it. Type A wiring. |
| H11L1 optocoupler (3.3 V capable) | 1 | MIDI IN isolation. Not 6N137/6N138 (5 V parts). |
| 1N4148 diode | 1 | MIDI IN LED reverse protection |
| 220 Ω, 1 kΩ resistors | 1 each | MIDI IN |
| 33 Ω (0.5 W), 10 Ω resistors | 1 each | MIDI OUT at 3.3 V (see WIRING.md) |
| 74LVC1G34 (or any 3.3 V buffer) | 1 (optional) | MIDI OUT driver |
| WS2812 / "NeoPixel" LED | 0-1 | on DevKitC-1 v1.0 it is already on the board (GPIO48) |

## Kept as an option (AS5600 platter)

AS5600 breakout, 683ZZ bearing, M3x12 axle, 6x2.5 mm diametric magnet: exactly as in the Field Jog README. Wire
GPIO21/GPIO47 and a single hold pad on GPIO4 (see WIRING.md). If you will never build this variant, delete it later;
it is about one class in the firmware.

## Cabling

* USB-C cable, data capable, to the DJ computer (native port). A second cable to the DevKit's UART port is only for
  flashing/logging.
* For two decks: two complete units, each with its own USB cable to a host (or one deck powered by any USB charger
  and bridged through the other over the radio, see `docs/LINK.md`), and each deck's DAC line out into one channel of
  the mixer.
