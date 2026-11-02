# Field Jog audio

Class-compliant USB audio for the deck. USB Audio Class 1.0, full speed,
stereo, 16-bit, 48 kHz. No driver on macOS, Windows, Linux, or iOS.

The RP2040 only has a full-speed USB controller, so this is UAC1 on purpose.
UAC2 wants high speed, and an RP2040 cannot do that.

The host lists the deck as an input named Field Jog. That is the USB name
for audio leaving the device. Spin the platter and the host hears it.
The same samples go out a UART, if you want a DAC on the other end of a wire.

The Pro Micro sketch in `../field_jog.ino` is still the MIDI-only build.
This one does not speak MIDI yet.

## Board

A Raspberry Pi Pico pinout with a USB-C receptacle, 21 × 51 mm, so it sits
on the rails already in the shell. The official Pico is micro-USB. A
USB-C Pico-shaped board is the one this is written for. The window in the
top wall is 12.5 × 7.5 mm, which a USB-C plug fits through.

| Signal | Pin | |
|---|---|---|
| AS5600 SDA | GP4 | I2C0, 400 kHz |
| AS5600 SCL | GP5 | |
| Hall, fader low end | GP26 | ADC0 |
| Hall, fader high end | GP27 | ADC1 |
| Knob | GP28 | ADC2, scales the jog |
| CUE | GP6 | other side to GND |
| REV | GP7 | other side to GND, flips scratch direction |
| UART DAC TX | GP0 | UART0, 3.125 Mbaud, 8N1 |
| UART DAC disable | GP22 | ground this at boot to leave GP0 idle |

3.3 V and ground to the encoder, both halls, and the pot. Diametric magnet
on the platter axle, same as the MIDI sketch.

## What you hear

A short loop with a click at the start. Playback rate follows the platter.
Stopped is silence. The fader is the level. REV reverses the rate. The knob
scales it from half to double. CUE mixes one click on the press, even if
the platter is still.

`FADER_LO` and `FADER_HI` in `src/controls.c` are guesses for a 12-bit ADC.
Trim them the first time the fader doesn't cover the whole throw.

## UART DAC frame

Only if GP22 is not grounded. One frame each millisecond:

```
A5 5A
uint16 length      192, little-endian
uint16 sequence
int16 L, int16 R   48 times, little-endian
uint8  crc8        poly 0x07, over length through the last sample
```

199 bytes per frame. 3.125 Mbaud is 125 MHz / 40, so the RP2040 hits it
on an integer divisor, and 8N1 still has room for the frame. Nothing is
listening if you leave GP0 empty. That is the optional part.

## Build

Pico SDK 2.1 or newer, on `PICO_SDK_PATH`. The SDK's TinyUSB has to be new
enough to have `TUD_AUDIO10_DESC_STD_AC` and the rest of the UAC1 templates.

```
cmake -S firmware/rp2040 -B firmware/rp2040/build -DPICO_BOARD=pico
cmake --build firmware/rp2040/build
```

Flash `field_jog_audio.uf2`. The LED blinks fast until the host enumerates,
then slow.

Record it as a normal input. 48 kHz, stereo, 16-bit. macOS: Audio MIDI
Setup. Linux: `arecord -D hw:CARD=FieldJog -c 2 -f S16_LE -r 48000 out.wav`.
