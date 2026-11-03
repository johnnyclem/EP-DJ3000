# What the original Field Jog firmware teaches us

The `field-jog/` shell was modelled on a unit with Arduino firmware: a Pro Micro class board (ATmega32U4), the Control_Surface
USB-MIDI library, Rob Tillaart's AS5600 library, a bearing platter over an AS5600, six trigger buttons, a shift button, a
reverse switch, an encoder with a button and a 3-state crossfader. The owner pasted the sketch; this page is what we took from
it. We did not run it and have no hardware, so **nothing here was re-tested**.

**Licensing.** The sketch has no licence we know of, so **no code and no comments were copied** (or translated). We took ideas
and interoperability facts (a register setting, a time constant, a CC number) and wrote our own code. Not legal advice.

## Idea by idea

| Idea in the original | Verdict | Where, or why not |
|---|---|---|
| Write the AS5600 configuration at start-up: watchdog off, hysteresis 0, slow filter 16x, fast-filter threshold 7 | adopted | `firmware/esp32s3/components/epdj_hal/` (`as5600.cpp`, `as5600_regs.h`); written again whenever the chip starts answering, since the chip forgets it at power-off |
| Read the chip's **filtered** angle register (0x0E), not the raw one (0x0C) | adopted, unverified | same files; `Telemetry.angle` and the jog come from 0x0E, with the build switch `EPDJ_AS5600_FILTERED_ANGLE` for an A/B on the bench. That the filters act on 0x0E is what the original relied on; nobody here read it in the datasheet |
| I2C at 400 kHz | already equivalent | the HAL already ran at 400 kHz |
| Chip absent at boot: re-probe every 500 ms, beep when it appears | adopted, no beep | `encoder_retry.h` in the same HAL; `Engine::setCaps` keeps `Info.caps` truthful; the status LED replaces the buzzer |
| **Sticky touch release**: after a touch over 7 s, accept a release only once the platter has been still for 80 ms | adopted | `StickyTouch` in `firmware/core/include/epdj/jog.h`, used by `Engine::runPlatter`; [`PROTOCOL.md`](PROTOCOL.md) 2.3 |
| Jog is sent **only while touched** | adopted as a policy | config key `jogUntouched` (33): `AUTO` sends untouched motion until the hold pad has registered a touch since power-up and suppresses it after that (the grace period is ours: this build's pad may not be connected yet), `REPORT` always sends it, `SUPPRESS` never does; `jogTotal` counts everything ([`PROTOCOL.md`](PROTOCOL.md) 2.2) |
| Touch reference = 500-sample average at boot; ON above it by a threshold (40 to 80 of 1024); 10-sample average per read | already equivalent | a slowly tracking baseline with on/off thresholds in per-mille, set by the app's Hold pad step. Another sensor: the numbers do not transfer |
| Asymmetric debounce: ON at once, OFF after a few ms | already equivalent | hysteresis between the thresholds, plus the sticky release |
| Per-sample dead band: while touched, drop steps under 3 counts (of 4096) | **not adopted** | it loses slow motion (roughly everything under 0.2 rev/s at a loop of a few ms; the loop rate is our estimate). We carry the remainder of 4 counts = 1 tick, so nothing is lost; the chip filter keeps rest noise under one tick |
| Read the jog twice per loop, around the MIDI flush | not needed | the engine ticks at 1 ms whatever USB is doing; the accumulator keeps every tick |
| Buzzer; a test mode whose tones follow the platter, no computer needed | not adopted | no buzzer; the app's Calibrate steps and the telemetry stream test the platter, the LED shows state |
| Shift (+20 on every CC), six triggers, reverse switch, encoder click, 3-state crossfader | not applicable | this deck has CUE, REV, an analogue slide pot and a knob; chords replace a shift layer |

## What it says about the hold pad

The original senses touch on **one analog pin of the spinning platter** (an ADCTouch-style read; the author raised a series
resistor to 1 Mohm for sensitivity) and needed the sticky release because the signal **dropped out during long contact** while
the platter was still being scratched. So a touch signal from a platter that turns has been made to work, which makes our
biggest hardware unknown **solvable in principle**, and the dropouts are real enough that the author coded around them. What the
code cannot show is the **mechanical path**: a pin gets read, and nothing says how the platter's surface reaches it (brush,
coupling, conductive bearing). That stays undesigned here ([`hardware/PLATTER.md`](../hardware/PLATTER.md)), and the ESP32-S3's
touch peripheral is not an ADCTouch pin, so signal and noise are ours to measure.

## Its CC scheme, as a field-tested reference for djay

As described to us; **these are not our numbers** (ours: [`PROTOCOL.md`](PROTOCOL.md)).

| Control | Original | Ours |
|---|---|---|
| Jog | CC 16, relative, **two's complement** (negative = 128 + value), 2.5 per AS5600 count, 4096 counts per revolution, clamped to +/-63 | CC 22, offset-64, 1024 ticks per revolution |
| Platter touch | CC 24, 127 down / 0 up | note 38 |
| Crossfader | CC 30 (0 / 64 / 127 from a 3-state switch) | slide pot, 14-bit pairs |
| Triggers | CC 17 to 21 and 26; CC 21 is play/pause (the pin that toggles the play state) | notes 36, 37, 39 |
| Encoder | CC 25 click, CC 28 turn (127 / 1) | none |
| Shift | adds 20 to every CC | none |

**Caveats.** We did **not** see the author's djay mapping, so which djay action each CC drives, and which control type djay uses
for the jog, is unknown (djay's `3Fh/41h` family is our offset-64, its `01h/7Fh` family is two's complement). The original jog is
about **10x finer per revolution** (10240 CC units against our 1024 ticks), so djay's Speed and Reaction settings may need
adjusting. All of this is the sketch as described, not a djay session.

**Offer.** If the owner supplies the original djay mapping, a "Frisk compatibility" output profile (their CC numbers and
encoding, so that mapping loads unchanged) is a small addition: a second table for the same events plus a config key to pick
it, which is a contract change. Not started.
