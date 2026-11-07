# FIELD JOG

> **This folder is the original Field Jog shell and Pro Micro sketch, redrawn to 16 mm by the owner's other tool for a headerless
> ESP32-S3-DevKitC-1** (shell commit 15417b6: the 683ZZ in a sleeve under the deck, a battery bay under a removable faceplate, a
> jack row on the top edge, a headphone jack on the fader end, five round caps).
>
> **The ESP32-S3 deck is what runs in it:** `firmware/`, `macos/`, `drivers/` and `docs/` at the repo root; start at the
> [root README](../README.md). Its MIDI map is its own ([`docs/PROTOCOL.md`](../docs/PROTOCOL.md)); its platter is the
> free-spinning AS5600 one drawn here.
>
> **What still does not match** (table: [`hardware/README.md`](../hardware/README.md#shell-controls-vs-firmware)): no slide-pot
> mount (the fader slot is for the hall-sensor block), no firmware for PLAY / LOOP IN / LOOP OUT, the two LEDs, the OLED, line in or
> the headphone jack, no hold-pad contact drawn, and the UART port is reachable only with the deck open (the window shows the
> native USB port). The AS5600 stack-up margin is thin
> ([cross-check](../hardware/README.md#stack-up-cross-check-on-paper-shell-15417b6-nothing-measured)). Nothing has been printed,
> fitted or run.
>
> `firmware/field_jog.ino` is the old MIDI-only Pro Micro build, not the ESP32-S3 firmware. `firmware/rp2040/` is a separate
> experiment (USB Audio, no MIDI), **not covered by this repo's tests**. The Technic-pin hole positions below are the original
> author's and have not been checked against real EP hardware.

A scratch deck shell that sits in the same row as a Teenage Engineering
EP-133 K.O. II and an EP-136 K.O. Sidekick.

This is not a Teenage Engineering product, and it is not a remix of the
Frisk Fader / Scratch Ninja STL. Same job (3 inch jog, loose crossfader,
corner knob), different body. If you send the original STL I can reopen
the cavities so parts you already printed drop in.

## Why these dimensions

| | EP-136 Sidekick | EP-133 K.O. II | FIELD JOG |
|---|---|---|---|
| Height | 240 mm | 240 mm | **240 mm** |
| Width | 88 mm | 176 mm | **88 mm** |
| Thickness | 16 mm | 16 mm | **16 mm** |

The long edge matches the EPs, and the width matches a Sidekick. The 76 mm
platter is centered, with 6 mm of deck on each side. The knob moves above
the platter, because a 16 mm knob will not sit beside a 3 inch jog in 88 mm.
The body is 16 mm thick, same as the EPs, so the cases sit flush on a desk.
A 683ZZ is 3 mm tall. It lives in a sleeve that hangs under the deck,
and the fader lock rides in the cavity beside it. The platter stands
about 3 mm proud of the case, the way a K.O. knob does. Don't expect
the spinning disc to be flush. The shell is.

A 256 mm bed (Bambu X1 / P1 / A1) fits the 240 mm length. A 220 mm bed
does not. The pin holes are measured from the bottom (fader) end.

## Lettering

Don't engrave the deck. The first print tore out everywhere the type was
cut into the top skin. FDM will not hold a shallow indent on a face this
thin.

The top 52 mm of the deck rises from 16 mm to 22 mm, and that rise is
part of the top shell. There is no separate battery door. A leaded 2×AA
holder is 59 × 33 × 15 mm, and a 16 mm shell cannot hold it, so the cells
sit in a pocket that opens downward. Pull the knob off, then lift the
top. Four 5 × 20 × 2 mm magnets in the tray meet four in the lid.

The pocket is 60 × 34.5 mm. The cells hang into the tray and the roof
over them is 4 mm of the top shell, so the springs are not crushed. It
stops about 10 mm short of the inside of the end wall. A jack body
longer than that will hit the cells. The screen tapes to the underside
of the roof, in the air above the cells. The DevKit moves to the left,
under the platter, and its USB reaches the back-panel window on a short
lead. There is still no charger.

The roof has one opening, cut to the glass: 30.5 × 11.85 mm. The module
behind it is 40 × 12.85 mm, so about half a millimeter of board shows past
the glass on the short sides and that lip is what keeps it from falling
through. Tape it to the underside, pins bent down into the bay. It is the
128×32 yellow-over-blue I2C panel. It shares I2C with the AS5600 (display
`0x3C`, encoder `0x36`).

## One board

Standalone playback (SD card, headphones, the screen, jog as a pitch
wheel) stays on the ESP32-S3 that already runs MIDI, the link, and USB
audio. Do not add a Teensy or a Daisy until that chip actually drops
samples.

- Line out is the I2S DAC already in the firmware (PCM5102A or the board's DAC).
- Headphones are a small amp on that same I2S stream, into the jack on the fader end.
- The card is a microSD on SDMMC, 1-bit is enough. First files are WAV. Varispeed is a read pointer, not a decoder. MP3 is a later problem.
- The screen is a view, not a controller. Yellow row: track name. Blue rows: BPM, time, cue.
- MIDI clock stays where it is, on the ESP, out the TRS jacks.

A second MCU is two firmwares and a private protocol in a 10 mm cavity.
The only reason to split later is a measurement: Wi-Fi link plus USB audio
plus a scratch voice underruns the S3. Until then, one chip.

Under the plate is a bay beside the board. A flat LiPo fits next to the
display module. There is no
cell holder. The long-side pin holes are unchanged.

## DevKit

The brain is an ESP32-S3-DevKitC-1, the board the firmware is written for.
The audio kit is out. It is a classic ESP32 and it cannot be the USB device.

The DevKit seat is a lip at the top of the tray, about 70 × 25.5 mm, native
USB toward the window in the top wall. Measure your board before trusting
that. Factory pin headers do not fit a 16 mm shell. Clip them, or use a
board without headers. The UART port stays inside. Flash with the deck
open. The window is the native USB port only.

## Encoder

The AS5600 pad used to sit at 6.2 mm. That left 1.9 mm under the magnet,
which is where the chip is, so the magnet hit the board. The pad is now
3.5 mm, for a headerless breakout. Measure yours. A thicker module needs
the pad lower still, or it hits the magnet again.

The magnet is no longer glued to a screw. The platter stem is the axle. It snaps through an 8.2 mm bore, and the 6 × 2.5 mm magnet clicks into a lipped cup on the end of that stem, about 2.7 mm above the pad. The 683ZZ does not fit this stem. The bore is the bearing. PETG will hold the barb better than PLA.

## Technic pins

Five holes on the left edge, five on the right. Round LEGO Technic pin
holes, so a real pin joins this shell to a K.O. II, a Sidekick, or another
Field Jog.

| | |
|---|---|
| Count | 5 per long side |
| Centers, from the bottom (fader) end | 16, 25, 48, 216, 224 mm |
| Height off the desk face | 8 mm (centered on a 16 mm EP side) |
| Diameter | 5.0 mm (a 4.8 mm pin, after PLA shrink) |
| Depth | 8.2 mm, blind, with a rib behind the wall |

They are in the tray (`bottom.stl`), not the deck. A friction pin's
gripping half is about 8 mm, so it seats and stops. If a pin is too
tight, set `technic_d = 5.15` and reprint the tray. If it rattles,
`4.85`.

The short edges are left solid. The top edge is the USB window.

Everything that matters is a variable at the top of the scad file.
OpenSCAD's customizer exposes them.

## What you are printing

Grey body, black controls, orange marks. Three filaments, no paint required.

| STL | Material | Count | Notes |
|---|---|---|---|
| `top.stl` | grey PLA/PETG | 1 | face up, no supports |
| `bottom.stl` | grey | 1 | cavity up, no supports |
| `platter.stl` | black | 1 | face up. Center pocket bridges on the bed |
| `fader.stl` | black | 1 | stem up |
| `fader_lock.stl` | black | 1 | socket up, magnet pocket on the bed |
| `knob.stl` | orange | 1 | |
| `button.stl` | black | 2 | stem up |
| `plus.stl` | orange | 1 | glue into the platter |
| `tab.stl` | orange | 1 | glue into the USB pocket |
| `bar.stl` | orange | 1 | glue into the short slot under the title |
| `foot.stl` | TPU or black | 4 | press into the recesses |

0.2 mm layers, 4 walls, 15% infill. The deck is a 6.2 mm skin, so 4 walls
matter more than infill. PLA is fine. PETG if it lives in a hot car.

The orange pieces are 0.6 mm. Print them slow, on a clean plate.

## Bill of materials

- 8× 5 × 20 × 2 mm magnets. Four in the tray, four in the lid, at the same four corners. The pocket is 21 × 6 × 2.5 mm. The extra 0.5 mm on each side is wiggle room. The extra 0.5 mm of depth is glue behind the magnet. Press each one flush with the mating face. Opposite poles face each other, or the case will push apart.
- 1× 6×2.5 mm **diametric** magnet. Press it into the cup on the underside of the platter until it clicks past the lip. It should sit about 2.7 mm above the encoder pad. To get it out, flex one of the three fingers and push from the lip. The old 683ZZ is not used. The stem runs in the 8.2 mm bore.
- 1× AS5600 breakout, headerless. Two M2 holes, 16 mm apart, on the pad under the platter. The pad top is at 3.5 mm. The chip wants 1–3 mm of air under the magnet. Measure the module before printing. A tall one will not fit.
- 1× dual-gang slide pot, 75 × 9 mm, 60 mm travel, B103 (10 k linear). One gang is the crossfader. Bend the pins flat.
- 2× 6×6 mm tactile switches, under CUE and PLAY. Glue them so the actuator passes into the stem.
- 3× 12×12×7.3 mm tactile switches, round cap Ø6.9, for IN, OUT and REV. The ears run left to right. The cap comes through the 7.6 mm hole and sits about 1 mm proud. No printed cap on these.
- 1× vertical 9 mm pot, 10k, if you want the orange knob to do something. Bushing is the 7.3 mm hole. The knob is a friction fit on a 6 mm shaft.
- 1× ESP32-S3-DevKitC-1, headerless. The lip is 70 × 25.5 mm, USB toward the top window. Measure yours. The firmware targets this board.
- 4× 3.5 mm TRS jacks on the top end, beside the USB-C window. Left pair is MIDI in and MIDI out. Right pair is line in and line out.
- 1× 3.5 mm TRS headphone jack, centered on the fader end. The long sides are pin holes only.

The crossfader is a real slide pot, not the printed magnet block. The one that fits is 75 mm long, 9 mm wide, 60 mm of travel, dual-gang, marked B103 (10 k linear). The slot is 66 mm so the metal tang can finish its travel. Two rails under the deck pinch the body. Bend the eight pins flat into the channel in the floor. The M2 holes on the pot are 67 mm apart and sit on the same line as the slot, so they are not drilled through the skin. The printed cap pushes onto the tang. Do not print the magnet lock.

## Assembly

1. Press the platter stem through the hole until the barb snaps open under the deck. If it will not start, run an 8.5 mm drill through the bore. PETG flexes. PLA may crack. In that case, shave the barb down and use a dot of glue. Then press the diametric magnet into the cup until it clicks.
2. Screw the AS5600 to the pad, chip up, under the magnet.
3. Slide pot into the cradle, lever up through the slot. Bend the pins flat into the floor channel. Push the printed cap onto the tang.
4. Glue the two 6 mm tactiles under CUE and PLAY, then push the printed caps on. Seat the three 12 mm toggles up into the pockets, cap through the hole.
5. Pot in the corner hole, knob on the shaft.
6. DevKit in the lip, native USB at the window. Flash over the UART port before the deck is closed.
7. Four screws from the face into the bosses.
8. Glue the orange plus, the bar, and the USB tab. Press the feet in.

## Firmware

Two builds.

`firmware/field_jog.ino` is USB MIDI on a Pro Micro (ATmega32U4). It has
not been run on hardware. Channel 1: jog CC 22 (relative, 64 = still),
fader CC 23, knob CC 24, CUE note 36, REV note 37. Wiring is at the top
of the sketch. If the scratch is backwards, set `JOG_SIGN` to `-1`.

`firmware/rp2040/` is the audio build. A Pico-shaped RP2040 with a USB-C
plug. USB Audio Class 1.0, stereo, 16-bit, 48 kHz, so a phone or a
laptop sees it as a normal input with no driver. The same samples go out
UART0 (GP0) at 3.125 Mbaud for an optional DAC. Ground GP22 at boot to
turn that off. The Pro Micro cannot do this. The RP2040 is full-speed
only, so the class is UAC1, not UAC2. Pins, the UART frame, and the build
are in `firmware/rp2040/README.md`. Not run on hardware yet.

## Files

- `field-jog.scad` — the model. `part` selects one solid for export.
- `build.sh` — `openscad` on your PATH, writes `stl/`.
- `stl/` — already exported.
- `preview/top.png`, `preview/angle.png` — generated from the model, not a render of a photograph.

## Knobs I did not turn

- No fake snap latch for the EP rail. I don't have the Sidekick's clip in front of me, and a wrong one is worse than a straight edge you can tape or print a clip for later.
- No mechanical jog brake. The corner knob is a pot. A set-screw brake is easy to add once the platter is spinning and you know where you want the drag.
- CUE and REV are just buttons. The names are suggestions.
