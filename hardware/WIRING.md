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
| 4 .. 11 | Platter touch pads, sectors 0 .. 7 | Touch pad N is GPIO N on the S3. Sector 0 = GPIO4 = 12 o'clock, increasing clockwise seen from above. In AS5600 mode only GPIO4 is wired (hold pad). |
| 12 | CUE button | to GND, internal pull-up, pressed = low |
| 13 | REV button | same |
| 14 | DAC-present strap | internal pull-up; a jumper to GND on the DAC board means "a DAC is attached" |
| 15 | I2S BCK | to PCM5102A BCK |
| 16 | I2S WS (LRCK) | to PCM5102A LCK |
| 17 | I2S DOUT | to PCM5102A DIN |
| 21 | I2C SDA | AS5600 (and the optional codec probe) |
| 47 | I2C SCL | same |
| 40 | TRS MIDI TX | UART1, 31250 baud, through the output stage below |
| 41 | TRS MIDI RX | UART1, from the optocoupler |
| 48 | Status LED | the DevKitC-1 v1.0 WS2812. **v1.1 moved it to GPIO38**: change `kStatusLed`. |

Why these: fader and knob are on ADC1 because ADC2 cannot be used while the radio is on. GPIO4..11 are the S3's
touch pads 4..11 (contiguous, away from the strapping pins). Left alone on purpose: GPIO0 (BOOT/strapping),
GPIO3 and 45/46 (strapping), GPIO19/20 (USB), GPIO26..37 (SPI flash and octal PSRAM/flash on R8 modules),
GPIO43/44 (UART0 log).

Notes on the pins that are used: GPIO39-42 are the S3's external-JTAG pins (GPIO40/41 carry TRS MIDI here); they
are plain GPIO unless you debug with an external probe, and the built-in USB-Serial/JTAG is not affected.
GPIO47/48 sit in the module's VDD_SPI power domain: fine on the usual 3.3 V modules (N4/N8/N16, R2/R8), but a module
variant with 1.8 V flash (the "V" parts, e.g. N32R8V) would drive them at 1.8 V. From memory of the datasheet,
**verify for your module** before relying on I2C or the LED there.

Unused pins you could take for something later: GPIO18, 38, 39, 42 (check your module: some boards put an LED or
a flash/PSRAM line on them).

## Power

* Bus powered over the native USB port. Rough budget from datasheet ballparks, **not measured**: ESP32-S3 with the
  radio always on about 120 mA average with peaks above 300 mA, plus the DAC (~25 mA), LED and sensors. The
  descriptor requests 250 mA. A host port that only gives 100 mA until configured is fine: the unit enumerates first.
* Everything is 3.3 V logic. Feed the PCM5102A board and the AS5600 from the DevKit's **3V3** pin, not 5 V.
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
physically far from the MIDI jack and the touch wires. **The line-out jack and the MIDI jack are both 3.5 mm TRS and the
MIDI ring carries 3.3 V through 33 Ω: label them or use a different connector for one of them**, or a swapped cable puts
DC on a mixer input.

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

## AS5600 option (free-spinning platter)

Per `field-jog/README.md`: AS5600 breakout under the platter, diametric 6 x 2.5 mm magnet on the axle, 1-3 mm gap.

| AS5600 | Connect to |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO47 |
| DIR | GND (the firmware has `jogInvert`) |
| OUT, PGO | not connected |

Most breakouts carry 4.7 kΩ pull-ups; the firmware also enables the S3's weak internal ones, which is enough to
keep an unpopulated bus quiet but not for 400 kHz on its own. At boot the firmware probes address 0x36; if it
answers, `Info.caps` reports AS5600 and the macOS app can suggest it. The first-boot platter source comes from
`idf.py menuconfig` (under PlatformIO: `pio run -d firmware/esp32s3 -t menuconfig`) -> EP-DJ3000 -> Platter and sensors
(`EPDJ_PLATTER_*`); it is a normal config key afterwards.

In AS5600 mode wire **only pad GPIO4** (sector 0) to the platter as the hold pad (set `EPDJ_TOUCH_PAD_COUNT = 1`):
the engine repeats a single pad to get touch/hold and tap detection, and takes rotation from the encoder. **A platter
that rotates cannot be wired to that pad with a plain wire**: it needs a slip contact, spring pin or brush, which is not
designed (`PLATTER.md`, "If the platter should spin").

## Touch pads

See [`PLATTER.md`](PLATTER.md) for the conductive-PLA ring: sector layout, how each sector is wired to GPIO4..11,
shielding, and what counts to expect.

## Status LED

One WS2812 (GRB, 800 kHz) on GPIO48 (DevKitC-1 v1.0; v1.1 has it on GPIO38, see the pin table). Colours: deck A blue, deck B orange; dim while the platter is untouched;
slow blink while searching for the other deck; alternates with red on a deck-id conflict; white flash on the
beat in clock modes; white blink for `IDENTIFY`; amber/red while CUE+REV is held toward the mode-change/deck-swap
thresholds. `ledBrightness` scales it.
