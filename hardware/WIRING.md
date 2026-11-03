# EP-DJ3000 wiring

Everything here is **design guidance that has not been built or measured**. The pin numbers are the ones in
[`firmware/esp32s3/components/epdj_hal/include/epdj_hal/board_pins.h`](../firmware/esp32s3/components/epdj_hal/include/epdj_hal/board_pins.h),
which refuses to compile if a pin is used twice or lands on a reserved one. Change a pin there, then here.

Target: **ESP32-S3-DevKitC-1** (any ESP32-S3 module with native USB on GPIO19/20 works with the same GPIO numbers).
The board's two USB-C ports: **USB** (native, GPIO19/20) goes to the DJ computer and carries MIDI and audio;
**UART** (CP210x on UART0) is only for flashing and the log. Both can be connected at once.
VBUS from the native port is **not sensed** by any GPIO, so the firmware cannot tell a pulled USB cable from a quiet bus
except by the bus going idle (`docs/BRINGUP.md` step 5 and risk 9). If that proves too slow or unreliable, add a divider from the
connector's VBUS (5 V) to a spare GPIO, e.g. 100 k to VBUS and 150 k to GND (about 3.0 V, 3.15 V at 5.25 V; check the real value
with a multimeter), and set `self_powered` / `vbus_monitor_io` in `usb_device.cpp`. Not built, not on the pin table.

## Pin table

| GPIO | Function | Notes |
|---|---|---|
| 1 | Fader wiper | ADC1_CH0. 12-bit, ~3.1 V full scale at 12 dB attenuation. |
| 2 | Knob wiper | ADC1_CH1. |
| 4 | **Hold pad** (default build) | The ONE touch pad of the AS5600 build: touch/hold and tap detection. How it reaches a spinning platter is undecided: `PLATTER.md`. Also sector 0 of the experimental ring. |
| 5 .. 11 | Experimental touch ring only: sectors 1 .. 7 | Touch pad N is GPIO N on the S3. Sector 0 = GPIO4 = 12 o'clock, increasing clockwise seen from above. Leave unconnected in the default build. |
| 12 | CUE button | to GND, internal pull-up, pressed = low |
| 13 | REV button | same |
| 14 | DAC-present strap | internal pull-up; a jumper to GND on the DAC board means "a DAC is attached" |
| 15 | I2S BCK | to PCM5102A BCK |
| 16 | I2S WS (LRCK) | to PCM5102A LCK |
| 17 | I2S DOUT | to PCM5102A DIN |
| 21 | I2C SDA | **AS5600** (the default platter's encoder; also the optional codec probe) |
| 47 | I2C SCL | same. In the VDD_SPI power domain: see the header-table check below. |
| 40 | TRS MIDI TX | UART1, 31250 baud, through the output stage below |
| 41 | TRS MIDI RX | UART1, from the optocoupler |
| 38 (v1.1) or 48 (v1.0) | Status LED | the DevKitC-1's on-board WS2812. **v1.1 boards, the current ones, have it on GPIO38; v1.0 on GPIO48.** Pick the revision in `menuconfig` (EP-DJ3000 > board revision, `EPDJ_BOARD_REV`); the default is v1.1. A wrong choice leaves the LED dark and nothing else changes. |

Why these: fader and knob are on ADC1 because ADC2 cannot be used while the radio is on. GPIO4..11 are the S3's
touch pads 4..11 (contiguous, away from the strapping pins). Left alone on purpose: GPIO0 (BOOT/strapping),
GPIO3 and 45/46 (strapping), GPIO19/20 (USB), GPIO35..37 (octal flash/PSRAM on octal modules; GPIO22..34 are not on the
DevKitC-1's headers at all, and `board_pins.h` reserves 26..37 regardless), GPIO43/44 (UART0 log).

## Checked against Espressif's header tables

The pin plan was checked on paper, not on a board, against the J1 and J3 header tables of Espressif's
[ESP32-S3-DevKitC-1 user guide (v1.0)](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html).
The headers expose GPIO 0-21 and 35-48; **GPIO 22-34 are not broken out.** Every planned pin is on a header:

| Function | GPIO | On a header | Note |
|---|---|---|---|
| Fader, knob wipers | 1, 2 | yes | ADC1_CH0, ADC1_CH1 |
| Hold pad | 4 | yes | TOUCH4 |
| CUE, REV | 12, 13 | yes | |
| DAC-present strap | 14 | yes | |
| I2S BCK, WS, DOUT | 15, 16, 17 | yes | |
| AS5600 SDA, SCL | 21, 47 | yes | 47 is in the flash/PSRAM supply domain (caveat 2) |
| TRS MIDI TX, RX | 40, 41 | yes | JTAG pins (caveat 1) |
| Status LED | 38 (v1.1) or 48 (v1.0) | yes | the guide lists the RGB LED on GPIO48 for v1.0; 48 is in the supply domain too |

None of them is USB (GPIO19 D-, GPIO20 D+), the UART0 console (GPIO43, 44), a strapping pin (GPIO0, 3, 45, 46) or one of the
octal flash/PSRAM pins (GPIO35, 36, 37, which the guide says are used internally on octal modules).

**Still free and not special:** GPIO18, 39, 42, and GPIO5-11. GPIO5-11 are the experimental touch ring's sectors 1-7: idle with
the AS5600 platter, taken if the ring is used.

Two caveats:

1. **GPIO39-42 are the JTAG pins.** They are plain GPIO unless an external JTAG probe is used (the built-in USB-Serial/JTAG is
   not affected). We use 40 and 41 for TRS MIDI.
2. **GPIO47 and 48 are in the flash/PSRAM supply domain.** Fine on 3.3 V WROOM-1 modules, but that is from memory of the
   datasheet: check your module variant in its datasheet, and avoid 1.8 V modules (the "V" parts such as N32R8V would drive
   I2C SCL and the v1.0 LED at 1.8 V). GPIO38, the v1.1 LED pin, is not in that domain as far as I remember: the same advice
   applies.

Ports and BOOT, from the same guide: the board has two USB ports. **UART** is the USB-to-UART bridge (CP2102-type), the default
for flashing and logs. **USB** is the ESP32-S3's native full-speed OTG port, the one this firmware uses as the MIDI/audio
device. Download mode: hold BOOT, then press RESET. (The v1.0 guide calls the UART connector Micro-USB; the USB-C connectors
named elsewhere in this repo are what v1.1 boards have, from memory: look at your board.)

## Power

* Bus powered over the native USB port. Rough budget from datasheet ballparks, **not measured**: ESP32-S3 with the
  radio always on about 120 mA average with peaks above 300 mA, plus the DAC (~25 mA), LED and sensors. The
  descriptor requests 250 mA. A host port that only gives 100 mA until configured is fine: the unit enumerates first.
* Everything is 3.3 V logic. Feed the PCM5102A board and the AS5600 from the DevKit's **3V3** pin, not 5 V, **and check the
  AS5600 breakout's 3.3 V / 5 V strap (below): many are strapped for 5 V.**
  The pots (fader, knob) are wired **between 3V3 and GND** with the wiper to the ADC pin; a 100 nF capacitor from
  each wiper to GND removes most of the ADC noise (and keep that wiring away from the touch traces).
* One ground. Star it at the DevKit: touch-pad shielding, DAC, jacks and the TRS MIDI sleeve all return there.

## Buttons and pots

* CUE (GPIO12) and REV (GPIO13): one terminal to the pin, the other to GND. Debounce is in firmware
  (`buttonDebounceMs`, default 4 ms). Optionally 100 nF across each switch.
* Slide pot: 10 kΩ linear, ends to 3V3 and GND, wiper to GPIO1. Which end is "up" does not matter: `faderInvert` and
  the calibration take care of it. A wiper that breaks contact for a moment reads 0 or 4095 for a sample: the
  engine's filter rejects single-sample spikes, but do not use a scratchy pot.
* Knob pot: 10 kΩ linear, same wiring, wiper to GPIO2.

## DAC: PCM5102A board (I2S, no I2C, no MCLK)

Wire a PCM5102A breakout (the common "GY-PCM5102" style) like this:

| PCM5102A pin | Connect to | Why |
|---|---|---|
| VIN / 3V3 | DevKit 3V3 | Check your breakout: some have a 5 V input with an LDO. Use 3.3 V. |
| GND | GND | |
| BCK | GPIO15 | bit clock |
| LCK (LRCK) | GPIO16 | word clock |
| DIN | GPIO17 | data |
| **SCK** | **GND** | No master clock is supplied. With SCK held low the chip generates its own system clock from BCK with its internal PLL (TI datasheet: after 16 LRCK periods of SCK low). Many breakouts have a solder bridge for this; make sure it is closed. |
| FMT | GND | low = I2S, high = left-justified. The firmware sends I2S (Philips). |
| FLT | GND | low = normal-latency filter. (High = low-latency filter; either works.) |
| DEMP | GND | de-emphasis off |
| XSMT | 3V3 | soft mute **released**. Low mutes the output; if the breakout does not tie it high, no sound. |
| L / R out | the line-out jack | 2.1 Vrms ground-centred, no DC-blocking capacitor needed (charge pump inside the chip). |

On the usual breakout these four pins are the solder jumpers H1..H4 (FLT, DEMP, XSMT, FMT): bridge to the "L"
(low) pads for FLT/DEMP/FMT and the "H" pad for XSMT.

**DAC-present strap:** a jumper (or a wire on the DAC board) from **GPIO14** to **GND**. With the jumper in,
`dacMode = AUTO` (the default) enumerates the USB audio function; without it the unit is MIDI-only. The decision is
taken at boot because the USB descriptors cannot change afterwards. The pin is sampled 16 times over 16 ms and counts
only if every sample reads low, so a loose or noisy strap means "no DAC" (the boot log says `UNSTABLE`).

**Line out:** from the DAC board's L, R and GND to a 3.5 mm TRS jack, with 100 Ω in series with each output and a
100 pF to ground after it if the cable picks up noise. The EP-136's inputs are line level. Keep the line-out jack
physically far from the MIDI jack and the touch wires. **The line-out jack and the MIDI jacks are all 3.5 mm TRS and the
MIDI ring carries 3.3 V through 33 Ω: label them or use a different connector for one of them**, or a swapped cable puts
DC on a mixer input.

**The redrawn shell's other jack holes** (`README.md`): the top edge has holes for MIDI in, MIDI out, USB, **line in** and line out, and
the fader end one for **headphones**. MIDI in, MIDI out and line out are wired as described in this file. **Line in and headphones are
not wired and not supported**: the PCM5102A is output-only, there is no input path in the firmware or in the USB audio function
(host-to-device only), and no headphone amplifier or cue mix is designed.

`dacMode = AUTO` (the default) means "the strap is fitted". It does not detect a DAC: the optional I2C codec probe
(`EPDJ_DAC_PROBE_I2C`) is compiled out by default and the PCM5102A has no I2C anyway.

**Bit clock accuracy:** BCK = 48 kHz x 32 = 1.536 MHz is derived from the S3's 160 MHz PLL with a fractional
divider. It is not exactly 48 kHz; the USB feedback loop absorbs the difference (that is what it is for).

## TRS MIDI jack (3.5 mm, Type A)

MIDI Association Type A: **tip = DIN pin 5, ring = DIN pin 4, sleeve = DIN pin 2**. Ring is the current source (+V),
tip is the sink/data side. Type B jacks (some Korg and Novation units) swap tip and ring. **Check what the EP-133
K.O. II actually has before wiring**: if it is Type B, swap the tip and ring wires (or ask for a one-line change here).

MIDI OUT (GPIO40), 3.3 V transmitter. Resistors per the MIDI Association's 3.3 V electrical update as published by
Elektor (33 Ω source, 10 Ω sink); 5 V designs use 220 Ω/220 Ω.

```
           3V3 ──[ 33 Ω ]────────────────────────────── ring  (DIN 4)
                                                          │   cable
  GPIO40 ─(74LVC1G34)─[ 10 Ω ]────────────────────────── tip   (DIN 5)
                                                         sleeve (DIN 2) ── GND (optional shield)
```

UART idle is high, so with GPIO40 high there is no current in the loop; a start bit or a 0 data bit pulls the tip
to ground and ~5-8 mA flows through the receiver's LED. The 74LVC1G34 (any 3.3 V non-inverting buffer) is there
so that an ESD hit on the jack reaches a 20 cent part and not the S3; it can be left out on a bench build.

MIDI IN (GPIO41), through an optocoupler so the two grounds stay apart:

```
  ring (DIN 4) ──[ 220 Ω ]──┬── LED anode (H11L1 pin 1)
                            │         ┌─ 1N4148 across the LED, cathode to the anode side (protects the LED
                            │         │   from reverse voltage)
  tip  (DIN 5) ─────────────┴── LED cathode (H11L1 pin 2)

  H11L1:  pin 6 (VCC) ── 3V3      pin 4 (output, open collector, Schmitt) ──┬── GPIO41
          pin 5 (GND) ── GND                                            [ 1 kΩ ] to 3V3
                                                                           (pin 3 not connected)
```

Use a 3.3 V-capable optocoupler: **H11L1** (or PC900V-class). The classic 6N138/6N137 are specified for 5 V and
are not drop-in at 3.3 V. The H11L1's LED needs ~1.6 mA to switch, so 220 Ω is plenty. The sleeve of the MIDI
jack is not connected to the receiver; ground it only through the transmitter shield if at all.

Not built, not scope-checked: before the first connection to the EP-133 verify the idle level on GPIO41 (high),
the loop current (~5 mA, measured across the 33 Ω resistor), and the 31250 baud timing on a logic analyser.

## AS5600 (the default platter: free-spinning on a bearing)

Mechanics are `field-jog/README.md` and `field-jog/field-jog.scad`: 683ZZ bearing, an M3x6 axle, a 6 x 2.5 mm **diametric** magnet
on the shank's end, the AS5600 breakout on the pad under the platter, chip up, **1 to 3 mm** from the magnet (`PLATTER.md`). Shell
commit 15417b6 lowered the pad to 3.5 mm and moved the BOM from M3x12 to M3x6. **The margin is thin:** on paper the gap is -0.1 to
+0.7 mm for an M3x6 and +0.9 to +1.7 mm for an M3x5, depending on where the bolt head seats, so buy both and let the boot log's
magnet status judge ([the cross-check in `README.md`](README.md#stack-up-cross-check-on-paper-shell-15417b6-nothing-measured)).

| AS5600 breakout | Connect to | Note |
|---|---|---|
| VCC / 3V3 | DevKit 3V3 | **Not 5 V, and check the strap below.** |
| GND | GND | |
| SDA | GPIO21 | |
| SCL | GPIO47 | |
| DIR | GND | Do not leave it floating. With DIR low the angle rises with clockwise rotation (datasheet, from memory). If clockwise reads backwards, set `jogInvert = 1` (the software twin of this pin) instead of moving the wire. |
| OUT | not connected | analog/PWM output, not used |
| PGO | not connected | programming option, not used (leave it as the breakout ships it) |

**The 3.3 V / 5 V strap (a trap).** From memory of the datasheet, the AS5600 has two supply pins, VDD5V (pin 1) and VDD3V3
(pin 2), and an internal regulator. At 5 V you feed VDD5V and the regulator produces 3.3 V on the VDD3V3 pin; at 3.3 V you
tie the two pins together and feed 3.3 V, bypassing the regulator. **Many breakouts are strapped (a solder bridge or a
zero-ohm link) for 5 V.** Feed 3.3 V into one of those and the regulator, which wants roughly 4.5 V or more in that mode,
has no headroom: the chip then runs below spec. It may still answer on the bus and give plausible angles, so **the
firmware cannot tell**: the boot line says `answered at 0x36` either way.

* Before powering: read the silkscreen or the seller's schematic for the 3V3 / 5V jumper and set it for 3.3 V.
* After powering from 3V3: measure the chip's VDD3V3 pin (pin 2) with a multimeter; it must read 3.3 V.
* The other half of the same trap: on most breakouts the SDA/SCL pull-ups go to the board's VCC rail. Powering a board at 5 V
  would pull SDA/SCL up to 5 V, and **the ESP32-S3's pins are not 5 V tolerant**. 3.3 V supply, 3.3 V strap, pull-ups to 3.3 V.

**I2C.**

* Address **0x36** (7-bit). The firmware probes only that address. The AS5600L is a different part that answers 0x40 by
  default (from memory) and is not supported.
* The bus runs at 400 kHz. **Pull-ups:** the firmware enables the S3's internal ones (about 45 k, from memory), which keep an
  unpopulated bus quiet but are far too weak for 400 kHz. Breakouts usually carry 4.7 k (some 10 k) to VCC; if yours has
  none, add 4.7 k from SDA and from SCL to 3V3. One set of pull-ups is enough; do not add more in parallel.
* Keep the two wires short (well under 20 cm), together, and away from the hold-pad wire and from the DAC and MIDI wiring.
* GPIO47 (SCL) is in the module's VDD_SPI power domain: see caveat 2 under "Checked against Espressif's header tables".

**Hold pad (GPIO4).** One thin wire, as short as you can make it (target under 10 cm), from the hold pad to GPIO4, optionally
with 330 ohm to 1 k in series at the chip end, away from the I2C, DAC and MIDI wiring. What the other end of that wire
touches is the unsolved part: `PLATTER.md` ("The conductive-PLA platter has two jobs"). `EPDJ_TOUCH_PAD_COUNT = 1` (the
default) is this pad. Without it jog still works, but the unit never sends a touch note (`PLATTER.md`).

**Bring-up: what the boot log should say** (`pio device monitor`, UART port; the format the firmware prints, not output from a
chip, which this firmware has never run on):

```
platter: source AS5600 | AS5600 answered at 0x36, magnet detected, conf=0x1C00, angle reg 0x0E (filtered), read <N> us | touch pads wired: 1 (GPIO4 = hold pad)
```

| It says | It means | Do |
|---|---|---|
| `magnet detected` | the chip's AGC has the magnet in range | turn the platter: the `sens:` line's `angle=` changes |
| `magnet too weak` (WARN) | too far, too small, off-centre, or axially magnetised | closer (1 to 3 mm), centre it on the chip, check it is diametric |
| `magnet too strong` (WARN) | too close | further away |
| `magnet not detected` (WARN) | nothing in range | as for too weak; check the magnet is there at all |
| `AS5600 NOT answering at 0x36 (...), asking again every 500 ms` and `!!! platterSource is AS5600 but no encoder answered ... JOG IS SILENT until one does ... !!!` (WARN) | nothing ACKed on the bus | SDA GPIO21, SCL GPIO47, 3V3, GND, the strap, pull-ups, the breakout's soldering. **No reboot needed**: the chip is asked again every 500 ms and the log says `as5600: encoder appeared at 0x36 after boot ...` when it answers |
| `conf=0x1C00` | the chip took the filter configuration (below) | nothing |
| `conf=0x0000 (wrote 0x1C00)` and a WARN, or `conf=?` | the chip did not take CONF (a burned configuration, or the write/read-back failed on the bus) | check the bus; jog still works with whatever the chip does |
| `as5600: encoder stopped answering at 0x36 ...` / `... answering again at 0x36 ...` (while running) | two failed reads in a row, then the first good trial read (one per 500 ms) | a loose wire; jog resumes from the platter's present position with no jump |
| `sens: ... angle=NNNN(bad)` | the last read failed | a loose wire; the diagnostics also warn when the count of failed reads grows |
| `an AS5600 answered but platterSource is ...` (WARN) | a config saved before contract v1.3 kept TOUCH_RING | `LOAD_DEFAULTS` then `SAVE_CONFIG`, or set `platterSource` 0 |

The unit reports the same thing to the host: `Info.caps` has the AS5600 bit once the chip has answered (at boot, or later: the
bit is set when the re-probe finds it), and the telemetry flag `ANGLE_VALID` follows each read. With no chip the unit sends no jog
at all, rather than something made up.

**Chip configuration and which register the angle comes from.** At start-up, and again whenever the chip starts answering, the
firmware writes the AS5600's CONF register (`0x07`, 16 bits, high byte first) and reads it back:

| Field (CONF bits) | Value written | Why |
|---|---|---|
| WD, watchdog (13) | 0, off | the watchdog drops the chip into low-power mode after a minute without movement |
| HYST, hysteresis (3:2) | 0, off | no dead band of its own: the engine's 4-counts-per-tick quantisation already is one |
| SF, slow filter (9:8) | 0 = 16x | the lowest-noise setting: quietens a platter at rest (at the price of lag at low speed) |
| FTH, fast-filter threshold (12:10) | 7 = **10 LSB** | a step larger than the threshold switches to the fast filter, so a scratch is not delayed by the slow one. Code 7 is out of order in the table: 1..7 are 6, 7, 9, 18, 21, 24, **10** LSB |
| PM, power mode (1:0) | 0, nominal | |
| OUTS, PWMF (5:4, 7:6) | 0 | nothing is connected to the OUT pin |

= `0x1C00`, the word printed as `conf=` in the boot line. These are the settings of the Arduino sketch the shell was modelled on (its
code is not copied; its licence is unknown). CONF is volatile RAM; the firmware never sends the one-time BURN command. The register
layout was checked against four independent implementations (Rob Tillaart's Arduino library, the Linux IIO driver, TinyGo, Adafruit
CircuitPython); the ams-OSRAM datasheet PDF itself could not be fetched, so nothing was read from it (`as5600_regs.h` has the details).

The angle is read from **ANGLE (`0x0E`)**, the scaled output and, as far as the sketch and the datasheet's wording imply, the one the
filters act on, instead of **RAW ANGLE (`0x0C`)**, which is documented as "unscaled and unmodified". That is what the original
sketch reads, and what our HAL read before this change was `0x0C`. **What is not established by anything we could read is that the
filters leave RAW ANGLE alone; and ANGLE is also scaled by ZPOS/MPOS/MANG** (a chip that someone burned with a limited range gives
fewer than 1024 ticks a lap; the encoder check's "one lap = 4096 counts" would show it). So the choice is a build switch,
`EPDJ_AS5600_FILTERED_ANGLE` (menuconfig, default **on**): build once each way and compare, on the bench, (1) the idle `angle=` in the
`sens:` line with the platter still (peak to peak over a few seconds), and (2) how a fast scratch feels. The expected cost of the
filter is lag (the datasheet excerpts we saw quote roughly 2 ms for the slow filter, 0.3 ms for the fast one; unchecked). A model of
what the filter buys at rest is in [`firmware/core/README.md`](../firmware/core/README.md); the real chip's noise has not been
measured.

**The hold pad is not read like the original's.** The original sketch reads its touch plate with an ADC-based capacitive technique on
one analog pin and raised a series resistor to 1 M ohm to make it more sensitive. The ESP32-S3 has a dedicated capacitive touch
peripheral, which the HAL uses, and it measures differently. **Whether a series resistor helps, hurts or is irrelevant on the S3 with a
platter behind a rotating contact is untested**: the 330 ohm to 1 k ohm optional resistor above is for ESD and EMI, not for
sensitivity, and no value has been tried.

**I2C transaction cost** (what the 1 kHz loop assumes; measured at boot and printed in the `adc:` line, never on a chip yet):
a register read at 400 kHz is about 45 clocks, 112 us on the wire plus driver overhead, so about 150 to 200 us of every
millisecond. It comes out of the same 400 us sensor budget as the ADC conversions (`firmware/esp32s3/README.md`). If the chip did not
answer at boot, 200 us are reserved for it anyway so that finding it later cannot overspend the budget. A chip that is absent or lost
costs one failed read (timeout 2 ms) per 500 ms.

## Touch pads

The default build has ONE: the hold pad on GPIO4 (above). See [`PLATTER.md`](PLATTER.md) for how it may reach the spinning
platter (both ways are unvalidated) and, further down that file, for the **experimental** conductive-PLA ring: sector layout,
how each sector is wired to GPIO4..11, shielding, and what counts to expect.

## Status LED

One WS2812 (GRB, 800 kHz), the DevKitC-1's own: GPIO38 on v1.1 boards (the default build), GPIO48 on v1.0 (see the pin
table; the boot log prints the pin it drives). Colours: deck A blue, deck B orange; dim while the platter is untouched;
slow blink while searching for the other deck; alternates with red on a deck-id conflict; white flash on the
beat in clock modes; white blink for `IDENTIFY`; amber/red while CUE+REV is held toward the mode-change/deck-swap
thresholds. `ledBrightness` scales it.
