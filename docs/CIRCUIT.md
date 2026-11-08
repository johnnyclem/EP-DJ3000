# EP-DJ3000 / Field Jog — circuit and operation

This document describes one deck: the electrical circuit, the parts on it, and what the firmware does with them. The three shells (narrow 88 × 240 mm, twin 3-inch, single 6-inch) share this circuit. Only the platter count and the jack positions change.

Pin numbers are the ones in `firmware/esp32s3/components/epdj_hal/include/epdj_hal/board_pins.h`. The firmware will not compile if a pin is used twice or lands on a reserved GPIO. The wiring sheet is `public/field-jog/wiring.png`.

Status of the words below: the circuit and the firmware agree with each other. Power numbers, magnet gap, and USB behavior are design figures, not bench measurements. `docs/BRINGUP.md` is the order to prove them.

## 1. What the unit is

A deck is a USB peripheral and, optionally, a small standalone player.

- To a computer it is a full-speed USB composite device: always a MIDI controller, and a stereo sound card when a DAC is fitted.
- To another deck it is an ESP-NOW peer. Two decks share tempo and, if one of them has no USB host, that deck's MIDI.
- To a 3.5 mm TRS jack it is a Type A MIDI port for external gear (an EP-133, a DAW, a drum machine).
- On its own, with a microSD card, it plays 16-bit WAV out of the line jack and shows the track on the OLED. That path does not need a computer.

It does not mix. EQ, channel faders, and cue belong to the mixer the line out is plugged into. The deck tells the DJ software what the hand did, and plays the audio the software (or the card) sends.

## 2. Block diagram

```
                         USB-C native (GPIO19/20)
                    MIDI always, audio when DAC is fitted
                                    |
   hold pad --GPIO4--+              |              +-- GPIO40 -- buffer -- MIDI OUT
                     |         ESP32-S3            +-- GPIO41 -- H11L1  -- MIDI IN
   CUE ------GPIO12--+         DevKitC-1          |
   REV ------GPIO13--+              |             +-- GPIO15 BCK --+
                     |              |             +-- GPIO16 WS  --+-- PCM5102A -- line out
   fader wiper GPIO1-+              |             +-- GPIO17 DIN --+     |
   knob wiper  GPIO2-+              |             GPIO14 strap ----------+  (to GND = DAC fitted)
                     |              |
                     |         I2C  GPIO21 SDA ----+---- OLED SSD1306 0x3C
                     |              GPIO47 SCL ----+---- MT6701     0x06
                     |                              (one pair of wires; encoder splices the display)
                     |
                     +-- GPIO18 CLK, GPIO39 CMD, GPIO42 D0 -- microSD (1-bit)
                     |
                     +-- Wi-Fi radio -- ESP-NOW to the other deck
                     |
                     +-- GPIO38 (v1.1) WS2812 status LED, on the DevKit
```

One ground. Every return (DevKit, DAC, jacks, encoder, display, card, pot ends, MIDI sleeve on the transmitter side) meets at the DevKit ground pin. Do not run a second ground through a cable shield and call it isolated.

## 3. Power

The deck is bus-powered from the native USB-C port. There is no battery, charger, switch, or power-path circuit. The faceplate bay is empty on purpose.

| Rail | Where it comes from | What uses it |
|---|---|---|
| 5 V (VBUS) | The host, into the DevKit only | The DevKit's own regulator. Nothing else is wired to 5 V. |
| 3.3 V | DevKit 3V3 pin | ESP32-S3 I/O, MT6701, OLED, PCM5102A, microSD, pot ends, MIDI OUT current source, optocoupler. |

Everything the firmware talks to is 3.3 V logic. The ESP32-S3 pins are not 5 V tolerant. A module whose I2C pull-ups go to 5 V will damage GPIO21 and GPIO47. Feed VDD of the encoder, the display, the DAC, and the card from 3V3.

Budget, from datasheet ballparks, not measured: the S3 with the radio on is about 120 mA average and can peak above 300 mA; the DAC is about 25 mA; the LED and the sensors are small. The USB descriptor asks the host for 250 mA. A port that only offers 100 mA until the device is configured is acceptable, because the radio and the DAC come up after enumeration.

VBUS is not sensed. The firmware cannot tell a pulled cable from a host that has gone quiet except by the USB bus going idle. A divider from VBUS onto a spare GPIO is described in `hardware/WIRING.md` and is not fitted.

The UART USB-C port on the DevKit (the CP210x) is for flashing and the log. It is not a power source the design relies on, and the shell window does not reach it. Flash with the deck open.

## 4. The controller

ESP32-S3-DevKitC-1, headerless. Factory pin headers do not fit the 10 mm cavity. The seat is a lip about 70 × 25.5 mm, native USB toward the rear window.

Pins the module keeps for itself, and that this circuit does not use:

| GPIO | Why it is left alone |
|---|---|
| 0 | BOOT, strapping |
| 3, 45, 46 | strapping |
| 19, 20 | USB D− / D+ |
| 26–37 | flash and PSRAM; 22–34 are not even on the DevKit headers |
| 43, 44 | UART0, the log |

GPIO47 (I2C SCL) sits in the flash/PSRAM supply domain. On a normal 3.3 V WROOM module that domain is 3.3 V. A 1.8 V flash module ("V" parts) would drive SCL at 1.8 V. Do not use one.

### 4.1 Tasks

After boot, `app_main` exits. Six tasks run the deck.

| Task | Core | Priority | Period | Job |
|---|---|---|---|---|
| engine | 1 | 24 | 1 ms | Read sensors, run `Engine::step`, emit MIDI, update the LED |
| TinyUSB | 1 | 22 | as needed | Enumeration, audio volume, MIDI endpoints |
| audio | 0 | 22 | 1 ms (48 frames) | USB ring or SD file → volume → I2S |
| link | 0 | 21 | paced by send-done | ESP-NOW, one packet in flight |
| TRS RX | 0 | 20 | UART events | Bytes from the MIDI IN optocoupler |
| diag | 0 | 3 | slow | The only task that prints once the deck is up |

The engine task is the only caller of the portable core. It does not log, and every queue it touches is non-blocking. If a tick runs more than 2 ms late, the missed ticks are dropped rather than caught up, so a slow sensor read cannot starve USB on the same core.

Sensor time is budgeted at 400 µs of each millisecond. ADC conversions give way to the I2C read. A missing encoder still reserves its slot, so finding the chip later cannot blow the budget. One failed I2C read costs a 2 ms timeout, and that probe happens only once per 500 ms.

A flash write (saving config) disables the cache. USB audio can glitch for that write. Saves happen only when the user asks.

## 5. Pin table

| GPIO | Net | Direction | Circuit |
|---|---|---|---|
| 1 | Fader wiper | ADC1 in | 10 kΩ linear gang, ends on 3V3 and GND. 100 nF wiper to GND. |
| 2 | Knob wiper | ADC1 in | Same, vertical 10 kΩ pot. |
| 4 | Hold pad | touch in | One wire to the platter contact. Optional 330 Ω–1 kΩ at the pin. |
| 5–11 | — | — | Touch-ring experiment only. Open in this build. |
| 12 | CUE | in, pull-up | Switch to GND. Pressed = low. |
| 13 | REV | in, pull-up | Same. |
| 14 | DAC strap | in, pull-up | Jumper to GND on the DAC board means "a DAC is fitted." |
| 15 | I2S BCK | out | PCM5102A BCK. 1.536 MHz. |
| 16 | I2S WS | out | PCM5102A LCK. 48 kHz. |
| 17 | I2S DIN | out | PCM5102A DIN. Philips I2S, 16-bit stereo. |
| 18 | SD CLK | out | 10 kΩ to 3V3. |
| 21 | I2C SDA | OD | To the OLED, then spliced onward to the MT6701. |
| 38 or 48 | Status LED | out | On-board WS2812. v1.1 is GPIO38 (the build default). v1.0 is GPIO48. |
| 39 | SD CMD | OD | 10 kΩ to 3V3. Also a JTAG pin; fine unless a probe is attached. |
| 40 | MIDI TX | out | UART1, 31250 baud, through the 3.3 V driver below. |
| 41 | MIDI RX | in | UART1, from the H11L1. Also a JTAG pin. |
| 42 | SD D0 | OD | 10 kΩ to 3V3. Also a JTAG pin. |
| 47 | I2C SCL | OD | Same pair as GPIO21. |

Internal pull-ups on SDA and SCL are about 45 kΩ. They hold an empty bus idle. They are too weak for 400 kHz. The OLED or the encoder breakout must provide the real pull-ups (typically 4.7 kΩ to 3V3). One set is enough.

## 6. The I2C bus

One bus, 400 kHz, two devices, two addresses.

| Device | Address | What it is |
|---|---|---|
| SSD1306 OLED, 128×32, yellow over blue | 0x3C (0x3D accepted) | The glass in the roof. A view, not a control. |
| MagnTek MT6701 | 0x06 | 14-bit magnetic angle under the platter. |

GPIO21 and GPIO47 are wired to the OLED first. The encoder taps those same two wires. There is no second pair. Short jumpers from the display's SDA and SCL to the encoder's SDA and SCL are the whole splice.

The protocol still calls the platter source `AS5600`. That name means "free-spinning platter with a magnetic encoder." The part that answers is the MT6701. The old AS5600 (address 0x36, CONF register, magnet-status nibble) is not on this board. Do not look for `conf=0x1C00` or `magnet detected` in the log.

Boot line when the wiring is right:

```
platter: source AS5600 | MT6701 answered at 0x06, 14-bit angle reg 0x03 (top 12 bits), read <N> us | touch pads wired: 1 (GPIO4 = hold pad)
```

If nothing ACKs, the firmware asks again every 500 ms. Fixing the wires is enough. No reboot.

### 6.1 OLED

128×32, two 16-pixel bands. The driver paints a 512-byte framebuffer and ships it when the line changes. A full redraw holds the bus for on the order of 50 ms, so the platter misses a few 1 ms reads at that moment. The engine re-anchors when the angle comes back, so the gap is not reported as a spin.

At boot the glass shows the deck letter and whether the encoder answered. In card-browser mode the top line is the track name and the bottom line is index and state (`3/12`). Outside that mode it reads `DECK` / `REV HOLD = CARD`.

### 6.2 MT6701

Header, as used: VDD, GND, Analog, PWM, SCL, SDA. Wire VDD, GND, SCL, SDA. Leave Analog and PWM open. There is no DIR pin and no 3.3/5 V strap on this board. The chip's MODE strap on the breakout selects I2C. Do not fit a pin header; the shell has no room for one, and the header would close the magnet gap.

The board is 23 mm square. Four M2 holes sit on a 17.5 mm square. The printed pad is 26 mm square with its top at 3.5 mm, so the chip face (1.6 mm board plus the SOP-8) lands about 1.75 mm above the pad. The magnet bottom is about 2.7 mm above the pad. Air gap is about 1 mm, inside the 0.5–3 mm window. A taller module means a lower pad, not a longer axle.

Angle is 14 bits in two registers:

- `0x03` = ANGLE[13:6]
- `0x04` bits 7:2 = ANGLE[5:0]
- `angle14 = ((reg03 << 8) | reg04) >> 2`
- the engine keeps the top 12: `angle12 = angle14 >> 2`, so one turn is 0..4095

The firmware does not write a configuration register. This chip has no AS5600 CONF, and it does not report a magnet-strength nibble. A missing, axial, or far magnet shows up as an angle that barely moves, not as a status warning. Clockwise that reads backwards is the config key `jogInvert`, not a wire.

Jog resolution on the wire is 1024 ticks per revolution. The engine quantizes the 4096-count angle by four. Positive is clockwise from above when `jogInvert` is 0.

## 7. Platter mechanics, as they meet the circuit

The platter stem is the axle. It snaps through an 8.2 mm bore in the deck. A barb under the skin keeps it from pulling out. A 6 × 2.5 mm diametric magnet clicks into a lipped cup on the end of the stem, centered on the chip. The 683ZZ bearing from the earlier shell is not used. The bore is the bearing. PETG holds the barb; PLA may crack it.

Diametric means the poles are on opposite sides of the round face. An axial magnet (poles on the flat faces) gives the MT6701 nothing to measure.

The hold signal is a separate problem from the angle. GPIO4 is a capacitive touch pad. The hand is on a part that turns, and the pad does not. Two ways are drawn as options and neither is proven:

1. A stationary copper pad or ring under the platter, close enough to couple.
2. A spring, brush, or pogo on the stem.

Without that wire the encoder still produces jog. The deck never sends the touch note, so DJ software never enters scratch mode and the platter behaves as a pitch wheel. `jogUntouched = AUTO` sends coasting motion until the pad has reported one touch since power-up, then stops sending it. That is so a bench spin works before the pad does, and so a free-spinning coast does not bend the pitch for seconds after the hand leaves.

A touch longer than 7 s is released only after the platter has been still for 80 ms. A pad that drops out mid-scratch would otherwise end scratch mode and start it again. Short touches release immediately.

A tap (down and up inside `tapMaxMs`, almost no motion, `tapPlay` on) sends play note 39 as a pulse, 127 then 0. It is not a scratch.

## 8. Buttons and pots

### 8.1 Switches

CUE (GPIO12) and REV (GPIO13) are the only switches the firmware reads. One side of the switch to the GPIO, the other to GND. Internal pull-up. Debounce default is 4 ms. A 100 nF across the switch is optional.

Mechanical fit:

- CUE and PLAY are 6 × 6 mm tactiles under printed stems.
- IN, OUT, and REV are 12 × 12 × 7.3 mm tactiles. Their own caps come through a 7.6 mm hole and sit about 1 mm proud. No printed cap.

PLAY, IN, and OUT are in the shell and not in this circuit. Nothing reads them. While the card browser owns the buttons (section 11), CUE and REV are not sent as MIDI either.

| Gesture | DJ mode and SYNC_FOLLOW | SYNC_MASTER |
|---|---|---|
| CUE | note 36, velocity 127 down / 0 up | tap tempo |
| REV | note 37, same | stop and rewind |
| CUE+REV held 2–5 s, then released | cycle DJ → SYNC_MASTER → SYNC_FOLLOW → DJ | same |
| CUE+REV held 5 s or more | swap Deck A ↔ Deck B, save, reboot | same |

A single press is not delayed to see if a chord is coming. When a chord completes, the engine sends note-off for any button it had reported down.

### 8.2 Fader

Dual-gang slide pot, 75 mm body, 60 mm travel, marked B103 (10 kΩ linear). Use one gang. Ends of that gang to 3V3 and GND, wiper to GPIO1, 100 nF from wiper to GND. Leave the second gang open. Do not tie the wipers together.

Which end is "up" does not matter. `faderInvert` and the calibration swap it. A wiper that breaks contact reads 0 or 4095 for one sample. The filter rejects a single spike. A scratchy pot will not be rejected.

Default role is PITCH: CC 25 (MSB) and CC 57 (LSB), a tempo slider. That is audible in the DJ apps even when a hardware mixer owns the channel fader. The role can be changed to channel volume (CC 7/39) or crossfader (CC 23/55). Output is 14-bit. The fader must move on the wire within 2 ms of a real motion, and ±3 ADC counts of noise must not chatter.

ADC is 12-bit, about 3.1 V full scale at 12 dB attenuation. Both pots are on ADC1. ADC2 cannot be used while the radio is on.

### 8.3 Knob

Vertical 9 mm, 10 kΩ linear, bushing in the 7.3 mm hole. Same three-wire circuit, wiper on GPIO2. Default role sends CC 24/56. The BPM role uses the knob as a 60–180 tempo knob instead.

## 9. Audio

### 9.1 DAC

PCM5102A breakout, I2S, no I2C, no MCLK.

| PCM5102 pin | Connect | Why |
|---|---|---|
| VIN | 3V3 | Some boards want 5 V into an LDO. This one is fed 3.3 V. Confirm the silkscreen. |
| GND | GND | |
| BCK | GPIO15 | |
| LCK | GPIO16 | |
| DIN | GPIO17 | |
| SCK | GND | Held low, the chip PLLs its system clock from BCK. Close the solder bridge if the board has one. |
| FMT | GND | I2S, not left-justified. |
| FLT | GND | Normal-latency filter. |
| DEMP | GND | De-emphasis off. |
| XSMT | 3V3 | Mute released. Low = silence. |
| L, R | line-out jack | 2.1 Vrms, ground-centered. No DC block. 100 Ω in series with each leg. |

On the usual GY-PCM5102 the solder jumpers are FLT, DEMP, XSMT, FMT. Bridge FLT, DEMP, and FMT low, XSMT high.

GPIO14 is the "DAC is here" strap, not a detector. The pin is sampled 16 times over 16 ms at boot. Every sample must read low. A noisy strap means no DAC, and the log says `UNSTABLE`. USB descriptors cannot change after enumeration, so fitting the strap requires a reboot. `dacMode` AUTO follows the strap. ON forces the audio function. OFF is MIDI only.

Line out is a 3.5 mm TRS jack. It looks like the MIDI jacks. The MIDI ring carries 3.3 V through 33 Ω. A mixer cable in a MIDI jack, or a MIDI cable into a mixer, puts DC on the wrong input. Label them.

Line in is a hole and nothing else. The PCM5102A is output-only. There is no ADC and no USB audio input.

Headphones are a hole and nothing else. Line level is not a headphone driver, and there is no cue mix.

### 9.2 What comes out of the DAC

The audio task moves 48 stereo frames every millisecond.

1. If the SD player returns samples, those samples are the block. The USB ring is left alone, and a host that keeps sending will overrun it.
2. Otherwise the block comes from the USB audio ring: prime to half a 42 ms buffer, then run. The feedback endpoint tells the host the rate the DAC is actually consuming (3 bytes, 10.14). Software slips a sample when a host ignores the feedback.
3. A test tone replaces either source.

USB audio is UAC1, 48 kHz, 16-bit, asynchronous, with mute and volume. UAC2 is not used because the S3 is full-speed only, and a full-speed host expects the 10.14 feedback format that TinyUSB's UAC1 driver sends. BCK is 1.536 MHz from a fractional divider off the 160 MHz PLL. It is not a perfect 48 kHz. The feedback loop is what absorbs that.

The product string is `EP-DJ3000 Deck A` or `Deck B`. The audio function's string is `EP-DJ3000 Audio`. No vendor driver is required for class-compliant hosts. That has not been proven on a machine. Windows playback should work with the in-box driver; low-latency Windows audio would want ASIO, which this device does not provide.

### 9.3 Two ways to get sound to a mixer

| | A. A sound card per deck | B. MIDI-only decks |
|---|---|---|
| Hardware | PCM5102A, strap fitted, line-out jack | No DAC, or `dacMode` OFF |
| Cables | One USB data cable per deck | One per deck, or one total with the radio bridge |
| App setup | Deck 1 → "EP-DJ3000 Deck A", deck 2 → Deck B. Two sound cards at once. Untested in the DJ apps. | Deck 1 and deck 2 → two outputs of one interface. Documented by the apps, untested here. |
| One-cable mode | Impossible. A deck with no USB host has no audio stream, so its DAC is silent. | Works. Audio comes from the other interface. |

Start with B. A is the only setup that exercises the DAC.

## 10. TRS MIDI

Two 3.5 mm jacks, Type A: tip = DIN pin 5 (data), ring = DIN pin 4 (current source), sleeve = DIN pin 2. Type B (some Korg and Novation) swaps tip and ring. Confirm the other device before soldering. UART1, 31250 baud, 8N1. Idle is high, which means no current in the loop.

MIDI OUT, GPIO40, 3.3 V electrical spec (not the old 220 Ω / 5 V circuit):

```
3V3 --[ 33 Ω ]------------------------------- ring
GPIO40 -- (74LVC1G34) --[ 10 Ω ]------------- tip
                                              sleeve -- GND (optional)
```

A start bit or a zero pulls the tip down and about 5–8 mA flows in the receiver LED. The buffer is so an ESD hit on the jack kills a small part instead of the S3. A bench build can omit it.

MIDI IN, GPIO41, isolated:

```
ring --[ 220 Ω ]--+-- H11L1 LED anode
                  |     1N4148 across the LED, cathode toward the anode
tip --------------+-- H11L1 LED cathode

H11L1 pin 6 VCC -- 3V3
pin 5 GND -- GND
pin 4 (open collector) -- GPIO41, and 1 kΩ to 3V3
pin 3 open
```

Use an H11L1 or a PC900-class part. A 6N138 or 6N137 is a 5 V device and is not a drop-in at 3.3 V. The receiver sleeve is not tied to deck ground.

USB cable 1 is a pipe to this jack. Bytes the host sends on the "TRS MIDI" port come out of the tip, and bytes from the optocoupler appear on that port. Cable 0 is the deck's own controls and is not the jack.

## 11. microSD

1-bit SDMMC, not SPI. DAT3 is pulled to 3V3 and is not driven, which keeps the card in SD mode.

| Socket | Net |
|---|---|
| VDD | 3V3 |
| GND | GND |
| CLK | GPIO18, 10 kΩ to 3V3 |
| CMD | GPIO39, 10 kΩ to 3V3 |
| DAT0 | GPIO42, 10 kΩ to 3V3 |
| DAT1, DAT2 | open |
| DAT3 | 10 kΩ to 3V3 only |
| CD, WP | open |

Clock is 20 MHz. The wires are short but they are not a controlled pair. The socket is a low push-push (about 1.8 mm) on the inside of the bottom skin, mouth down, clear of the magnet posts and the jack bodies.

At boot the firmware mounts `/sd` and lists up to 48 `*.wav` names in the root. Anything else is ignored. Playback is 16-bit PCM, mono or stereo, 8 kHz to 96 kHz. 48 kHz is copied. Any other rate is held up to 48 kHz with no low-pass filter. MP3 is not decoded.

The browser task runs every 20 ms and watches the same two buttons:

- Hold REV about 0.8 s: take the buttons away from MIDI. The glass shows the current file.
- CUE: play, or pause if it is already playing.
- REV, short, while stopped: next file.
- REV, short, while playing: stop.
- Hold REV again, and release: give the buttons back to the deck.

The file read happens on the audio task, inside the pull callback, so it does not run from an interrupt. While the browser owns the buttons, the engine is handed CUE = up and REV = up, so a card session cannot also send notes or complete a mode chord.

## 12. Link between two decks

ESP-NOW, not BLE-MIDI. A BLE connection interval would add 7–15 ms of jitter, which is too much for a scratch.

Units with the same `linkGroup` find each other, pick roles, and share tempo and transport. A deck with no USB host forwards its notes and CCs through the deck that has a host. They come out of that deck's USB cable 2, on the original MIDI channel, so Deck B stays on channel 2. The host's notes and CCs on cable 2 go back. SysEx, MIDI clock, and MTC are not bridged. Jog is forwarded as a running 16-bit counter so a lost radio packet does not lose motion.

There is no audio on the radio. ESP-NOW will not carry a stereo 48 kHz stream. A bridged deck's DAC is silent.

Deck identity: hold CUE+REV for 5 seconds. The unit saves, reboots, and enumerates under the other name and channel. Deck A is MIDI channel 1, Deck B is channel 2, unless `midiChannel` is set by hand.

## 13. Status LED

The DevKit's own WS2812. GRB, 800 kHz. Pick the board revision in menuconfig. A wrong choice leaves the LED dark and changes nothing else.

| Color | Meaning |
|---|---|
| Blue, dim when the platter is idle | Deck A |
| Orange, same | Deck B |
| Slow blink | Looking for the other deck |
| Alternating with red | Both decks claim the same id |
| White flash on the beat | SYNC_MASTER or SYNC_FOLLOW |
| White blink | IDENTIFY |
| Amber, then red, while CUE+REV is held | Approaching the mode change, then the deck swap |

`ledBrightness` scales it. The LED is inside the shell. There is no light pipe.

## 14. Operation, end to end

### 14.1 Boot

1. Load config from NVS. A bad CRC or a version mismatch is logged and repaired or replaced. Defaults: Deck A, platter source AS5600, fader role PITCH, `jogUntouched` AUTO, `dacMode` AUTO.
2. Start I2C. Probe 0x3C/0x3D and 0x06.
3. Paint the OLED. Probe the MT6701 and time the read.
4. Start the touch pad, the ADC, the two buttons.
5. Sample GPIO14. Decide whether USB audio will be in the descriptor.
6. Start USB. The host sees MIDI, and audio if the strap said so.
7. Start I2S, the radio, the TRS UART, the SD mount, and the engine task.
8. Print the platter line and `ready`, with the capability bits.

From then on the 1 ms loop is the product.

### 14.2 One millisecond, DJ mode

1. Pop whatever MIDI arrived from USB, the TRS jack, and the radio. Time-stamped at receipt.
2. Read the MT6701 (12-bit angle), both ADC channels, the hold pad, and the buttons (unless the card owns them).
3. `Engine::step` turns those into events:
   - angle delta, quantized to 1024 ticks/turn, emitted as CC 22 at most every 2 ms, offset-64, remainder kept so nothing is lost;
   - touch note 38 on a confirmed finger, with the sticky release after a long touch;
   - play pulse on a tap;
   - fader and knob as 14-bit pairs, filtered so a still pot is silent;
   - CUE and REV notes, or a chord if both are held.
4. Events go to USB cable 0 (dropped, not queued forever, if the host is not reading), to the TRS TX ring, and to the radio if a peer wants them.
5. The LED is updated from the engine's color, at a slower rate.

USB cable 0 is "Deck." Cable 1 is the jack. Cable 2 is the other deck, and it is silent unless that deck is bridged.

### 14.3 SYNC_MASTER

Entered by the 2–5 s chord, or by `SET_CONFIG mode = 1`. The deck stops being a controller and becomes a clock.

- MIDI clock `0xF8` at 24 PPQN runs continuously, including while stopped, so a follower can lock tempo.
- CUE is tap tempo. REV is stop and rewind (Stop, then song position 0).
- A hand on the platter nudges tempo by 0.1 BPM per 32 ticks.
- A platter tap is play/pause.
- Start, continue, stop, and song position follow the transport state.
- MTC quarter-frames are optional (`mtcEnable`). This is not vinyl timecode.

The chord only changes the live mode. `SAVE_CONFIG` makes the next boot come up in it.

### 14.4 SYNC_FOLLOW

The deck locks its beat LED to a master's transport packet on the radio, or to MIDI clock on USB or the TRS jack. It still sends its own controls. It does not generate clock.

### 14.5 Standalone card

No host required for sound, provided the DAC is fitted and something is providing 5 V (a charger enumerates nothing, so this path is the one that still makes noise). Hold REV, pick a file, press CUE. Samples replace the USB stream until the file stops. Hold REV again to go back to being a controller.

## 15. MIDI map (deck channel)

Deck A channel 1, Deck B channel 2, unless overridden.

| Control | Message | Direction |
|---|---|---|
| Jog | CC 22, relative, 64 = still, 1024 ticks/rev | out |
| Pitch fader (default) | CC 25 / 57 | out |
| Knob | CC 24 / 56 | out |
| CUE | note 36 | both (in = LED, and there is no LED on that button) |
| REV | note 37 | both |
| Touch | note 38 | out |
| Play (tap) | note 39, pulse | both |
| Tempo | CC 20 / 52, value = BPM × 10 | both, master mode |
| Tempo nudge | CC 21 | both |
| Transport | CC 29: 0 stop, 1 play, 2 pause | both |
| Tap | note 41 | both |

Channel voice for the other channel is ignored, including on the bridge, so a host can spray both decks' LED feedback at every port.

Config, calibration, and identity travel as SysEx (`F0 7D 45 44 …`). The contract is `docs/PROTOCOL.md`. There is no network service and no cloud.

## 16. What is drawn and not wired

| In the shell | In this circuit |
|---|---|
| MIDI in, MIDI out, USB, line out | Yes |
| Platter, MT6701, fader, knob, CUE, REV, OLED, microSD | Yes |
| Hold pad | The GPIO is assigned. The rotating contact is not drawn. |
| PLAY, LOOP IN, LOOP OUT, two LED holes | No. No pins, no notes for the loops. |
| Line in | No. No ADC. |
| Headphone jack | No. No amplifier. |
| Battery bay | No. No cell, charger, or switch. |
| Second encoder on the twin-platter shell | The pad is in the print. The firmware has one I2C encoder. A second MT6701 at 0x06 would collide. |
| Touch ring, GPIO5–11 | Compiled as an option. Not this build. |

## 17. Failures and what they look like

| Symptom | Cause | What to do |
|---|---|---|
| `MT6701 NOT answering at 0x06` | SDA/SCL swapped, no 3V3, missing pull-up, Analog/PWM shorted into the bus | Fix the four wires. It retries every 500 ms. |
| Angle barely moves through a full turn | Axial magnet, magnet missing, or gap far outside 0.5–3 mm | Diametric 6 × 2.5, about 1 mm off the SOP-8, centered. |
| Jog silent, encoder fine | `platterSource` is not AS5600 (an old saved config) | `LOAD_DEFAULTS`, then `SAVE_CONFIG`. |
| Jog works, scratch mode never starts | No hold pad, or the pad never reports a touch | GPIO4. Until it has touched once, coasting jog is still sent. |
| Coasting suddenly goes quiet | The pad registered a touch, including a phantom | Expected under AUTO. `jogUntouched = REPORT` to always send it. |
| No USB audio device | Strap open, or not low for all 16 samples | GPIO14 to GND, reboot. |
| DAC silent, device present | XSMT low, SCK not grounded, FMT high, or no host stream and no card | Jumpers in section 9.1. |
| OLED redraw, platter hiccups | The 512-byte frame owns the bus | Normal. A few ticks, then re-anchor. |
| MIDI into a mixer channel | Jacks look the same | Label them. Ring of MIDI OUT is 3.3 V. |
| Deck will not reflash once closed | The window only reaches native USB, which is a MIDI device, not the ROM download port | BOOT+RESET with the deck open, or prove `REBOOT(bootloader)` before closing it. |
| I2C at 1.8 V | 1.8 V flash module | Wrong module. SCL is in the VDD_SPI domain. |

## 18. Where the detail lives

| Topic | File |
|---|---|
| Pins, MIDI electrical, SD socket, encoder bring-up | `hardware/WIRING.md` |
| This sheet, as a picture | `public/field-jog/wiring.png` |
| Parts | `hardware/BOM.md`, `field-jog/README.md` |
| Shell, stem, pad | `field-jog/field-jog.scad`, `field-jog/wide.scad` |
| Wire contract | `docs/PROTOCOL.md`, `protocol/epdj3000.json` |
| Tasks, USB, DAC decision | `docs/ARCHITECTURE.md` |
| Radio | `docs/LINK.md` |
| Order to power it on | `docs/BRINGUP.md` |
| Angle packing | `firmware/esp32s3/components/epdj_hal/include/epdj_hal/mt6701_regs.h` |
