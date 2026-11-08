# Twin deck wiring

The 176 × 240.5 mm shell with two 76 mm platters. One ESP32-S3-DevKitC-1 runs both decks. The 88 mm deck and the single 6 inch deck do not use this page.

Build it with `EPDJ_TWIN` on (`pio run -d firmware/esp32s3 -t menuconfig`, EP-DJ3000 > Twin wide deck). Leave that off for the other shells. The code is `firmware/core/include/epdj/twin.h` and `firmware/esp32s3/components/epdj_hal/src/twin_io.cpp`.

Two extra chips, because the DevKit does not have the pins:

| Chip | Why | Bus |
|---|---|---|
| MCP23017 | 16 buttons. The header has seven free GPIOs and the twin has sixteen switches. | I2C 0x20, same pair as the OLED |
| ADS1115 | Two pitch faders. GPIO1 and GPIO2 are the only fast ADC pins left, and GPIO1 is the crossfader. | I2C 0x48, same pair |

No analog multiplexer. The pitch faders update about every 4 ms, which is enough for a tempo slider. The crossfader stays on the DevKit's own ADC and is read every millisecond.

## What is already on the single-deck circuit

Keep these exactly as in [WIRING.md](WIRING.md). Nothing below moves them.

| Function | GPIO | Twin use |
|---|---|---|
| Crossfader wiper | 1 | Was the only fader. On the twin it is the center crossfader. |
| Knob wiper | 2 | The one orange knob, above the right platter. |
| Hold pad, left platter | 4 | Deck A touch. |
| DAC strap | 14 | Unchanged. |
| I2S BCK, WS, DIN | 15, 16, 17 | One PCM5102A for the whole unit. |
| microSD CLK, CMD, D0 | 18, 39, 42 | Unchanged. |
| I2C SDA, SCL | 21, 47 | OLED, left MT6701, and now the MCP23017 and the ADS1115. |
| Status LED | 38 | The DevKit's own WS2812. |
| MIDI TX, RX | 40, 41 | Unchanged. |

## What the twin adds

| Function | Where | Notes |
|---|---|---|
| Hold pad, right platter | GPIO5 | Second touch pad. The firmware scans exactly GPIO4 and GPIO5. |
| Right MT6701 | GPIO6 SDA, GPIO7 SCL | Its own I2C bus. Address 0x06, same as the left chip, which is why it cannot share the display bus. |
| IN / OUT LEDs | GPIO8, 9, 10, 11 | Sunk. Low = lit. 330 Ω from 3V3 to the anode, cathode to the pin. |
| Left MT6701 | GPIO21 / GPIO47 | Already there, spliced onto the OLED. |
| 16 buttons | MCP23017 | Internal pull-ups. Switch to GND. |
| Pitch A, pitch B | ADS1115 A0, A1 | Ends of each pot on 3V3 and GND. 100 nF from each wiper to GND. |

GPIO12 and GPIO13, CUE and REV on the narrow deck, are not used. Do not hang the twin's buttons on them.

## I2C map

Bus 0, GPIO21 SDA and GPIO47 SCL, 400 kHz. One set of pull-ups to 3V3 (the OLED's or the left encoder's, 4.7 kΩ). Do not add another pair.

| Address | Part |
|---|---|
| 0x3C | SSD1306, the one display |
| 0x06 | Left MT6701, under the left platter |
| 0x20 | MCP23017. A0, A1 and A2 tied to GND. RESET tied to 3V3. A floating RESET holds the chip in reset and every button reads open. |
| 0x48 | ADS1115. ADDR tied to GND. ALERT left open. |

Bus 1, GPIO6 SDA and GPIO7 SCL. Only the right MT6701. Use the pull-ups on that breakout. VDD is 3.3 V. Analog and PWM stay open, same as the left chip.

## Buttons

MCP23017 port A is the left deck, port B the right deck. Bit 0 is GPA0.

| Pin | Button | MIDI |
|---|---|---|
| GPA0 | Left CUE | note 36, channel 1, the existing engine |
| GPA1 | Left PLAY | note 39, channel 1, a pulse on release |
| GPA2 | Left IN | note 42, channel 1 |
| GPA3 | Left OUT | note 43, channel 1 |
| GPA4..7 | Left pads 1..4 | notes 44..47, channel 1 |
| GPB0 | Right CUE | note 36, channel 2 |
| GPB1 | Right PLAY | note 39, channel 2, a pulse on release |
| GPB2 | Right IN | note 42, channel 2 |
| GPB3 | Right OUT | note 43, channel 2 |
| GPB4..7 | Right pads 1..4 | notes 44..47, channel 2 |

Notes 42..47 are not in contract v1.4. The DJ-app mappings do not bind them yet. The bytes on the wire are stable.

PLAY does not fire on the press. A tap shorter than 600 ms sends 127, then 0 twelve milliseconds later. Holding left PLAY for about a second hands CUE and PLAY to the SD browser instead of sending play. In the browser, left CUE is play/pause and left PLAY is next / stop, the job REV has on the narrow deck.

There is no REV button and no deck-swap chord. This unit is both decks. Channel 1 is the left platter, channel 2 is the right. A saved "deck B" identity is ignored at boot.

IN and OUT light their LEDs while held. A host can also latch the LED by sending note 42 or 43 back on that deck's channel, velocity above 0 to light, 0 to clear.

## Faders and knob

| Pot | Wire | Message |
|---|---|---|
| Left pitch | ADS1115 A0, AIN0 to GND | CC 25 / 57, channel 1 |
| Right pitch | ADS1115 A1 | CC 25 / 57, channel 2 |
| Crossfader | GPIO1 | CC 23 / 55, channel 1 |
| Knob | GPIO2 | CC 24 / 56, channel 1 |

ADS1115 A2 and A3 are free. PGA is ±4.096 V, so a 3.3 V wiper is in range. `faderInvert` flips both pitch faders. If the crossfader is backwards, swap its end wires.

VDD of the ADS1115 is 3.3 V, not 5 V.

## Platters

Each platter is the same stem, diametric 6 × 2.5 mm magnet, and MT6701 on a 26 mm pad as the narrow deck. Left chip on bus 0, right chip on bus 1. The firmware keeps the top 12 of the 14-bit angle. Jog is CC 22, 1024 ticks a turn, channel per deck. Touch is note 38 from that deck's hold pad.

Right-hand coasting follows the same rule as the left: sent until that pad has registered a touch since power-up, then only while the hand is on it.

## What is still not in this circuit

Line in, the headphone jack, and a battery. One DAC, one line out, one USB device. The twin does not need a second DevKit and it does not bridge deck B over the radio: both channels come out of USB cable 0.

## Boot log

```
twin: MCP23017 at 0x20, 16 buttons, pull-ups on
twin: ADS1115 at 0x48, pitch A on A0, pitch B on A1
twin: right encoder answered at 0x06
```

A missing MCP means no buttons. A missing ADS means both pitch faders sit at center. A missing right encoder means the right platter sends no jog. The left deck still comes up on the original path.
