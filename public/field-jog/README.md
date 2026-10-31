# FIELD JOG

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
| Width | 88 mm | 176 mm | **120 mm** |
| Thickness | 16 mm | 16 mm | **26 mm** |

The height matches so the top and bottom edges line up with the EPs.
The width does not match a Sidekick, because a real 76 mm platter does not
fit in 88 mm with a wall and a knob. The body is 26 mm thick because a
683ZZ bearing, a magnet, and a fader lock have to live under the deck.
16 mm is not enough. Don't sand it down to match.

A 256 mm bed (Bambu X1 / P1 / A1) fits the 240 mm length. A 220 mm bed
does not. Open `field-jog.scad` and set `H = 210` if you have to; the
nameplate just gets shorter. The Technic holes stay on the 8 mm grid
as long as `H` stays a multiple of 8 and you don't move `technic_y0`.

## Technic pins

Four holes on the left edge, four on the right. Same idea as the EP-133:
round LEGO Technic pin holes on an 8 mm pitch, so a real pin joins this
shell to a K.O. II, a Sidekick, or another Field Jog.

| | |
|---|---|
| Count | 4 per long side |
| Pitch | 8 mm |
| Centers, from the bottom edge | 108, 116, 124, 132 mm |
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

- 4× M2.5×12 self-tapping screws (pan head). Pilots in the bosses are 2.15 mm. Heads sit in the four corner counterbores.
- 1× 683ZZ bearing, 3×7×3 mm. Press into the platter well from above. It should sit about 0.4 mm below the floor of the well. If it stands proud, it will scrape.
- 1× M3×12 button head. This is the axle. Head hides in the pocket on the underside of the platter. Shank points down through the bearing.
- 1× 6×2.5 mm **diametric** magnet, glued to the end of that M3, for the AS5600.
- 1× AS5600 breakout. Solder wires, not tall pin headers. Two M2 holes, 16 mm apart, on the pad under the platter. The chip wants to be 1–3 mm from the magnet. Shim with tape if the gap is wrong.
- 1× 6×2.5 mm magnet (axial is fine) in the fader lock.
- 2× hall sensors, SS49E or A1302. Glue one to the top of each post beside the fader. Flat face toward the lock.
- 2× 6×6 mm tactile switches. Glue them under the button holes so the actuator passes into the stem.
- 1× vertical 9 mm pot, 10k, if you want the orange knob to do something. Bushing is the 7.3 mm hole. The knob is a friction fit on a 6 mm shaft.
- 1× Raspberry Pi Pico, or a Pro Micro. Rails are sized for a Pico (21×51 mm) with the USB connector toward the top edge. A Pro Micro fits in the same bay with a bit of foam.
- 1× 3.5 mm TRS jack, optional, in the hole on the right wall. Labeled MIDI. Not wired by the sketch; it's a hole for a sync/MIDI jack if you want one later.

No slide pot. The crossfader is a loose block with a magnet, same idea as the red one, so it can be hit. The two halls read where it is. The curve is monotonic, not linear. Trim `FADER_LO` and `FADER_HI` in the sketch.

## Assembly

1. Press the 683ZZ into the deck.
2. Drop the M3 button head into the platter pocket, shank down, through the bearing. A tiny dot of threadlocker on the head so it stays in the platter and spins with it. Glue the diametric magnet to the shank end.
3. Screw the AS5600 to the pad, chip up, under the magnet.
4. Fader: push the stem down through the slot. Press the lock onto the stem from inside (0.2 mm interference; file it if it won't start). Glue the magnet into the lock, facing the posts.
5. Glue the two halls to the posts. Glue the two tactiles under the button holes. Push the caps on.
6. Pot in the corner hole, knob on the shaft.
7. Pico or Pro Micro between the rails, USB at the window in the top wall. The window is 12.5×7.5 mm so a micro-USB plug can pass through and seat on the board.
8. Four screws from the face into the bosses.
9. Glue the orange plus, the bar, and the USB tab. Press the feet in.

## Firmware

`firmware/field_jog.ino` targets a **Pro Micro** and the MIDIUSB library.
It has not been run on hardware.

| Control | Message |
|---|---|
| Jog | CC 22, relative, 64 = still |
| Fader | CC 23, 0–127 |
| Knob | CC 24 |
| CUE | note 36 |
| REV | note 37 |

Channel 1. In djay, learn CC 22 as a relative jog. If the scratch is
backwards, set `JOG_SIGN` to `-1`.

Wiring for the Pro Micro is in the top of the sketch. A Pico will run the
same logic on the Arduino-Pico core, but the pin numbers change (Wire is
GPIO 4/5, not D2/D3). I didn't ship a second sketch.

## Files

- `field-jog.scad` — the model. `part` selects one solid for export.
- `build.sh` — `openscad` on your PATH, writes `stl/`.
- `stl/` — already exported.
- `preview/top.png`, `preview/angle.png` — generated from the model, not a render of a photograph.

## Knobs I did not turn

- No fake snap latch for the EP rail. I don't have the Sidekick's clip in front of me, and a wrong one is worse than a straight edge you can tape or print a clip for later.
- No mechanical jog brake. The corner knob is a pot. A set-screw brake is easy to add once the platter is spinning and you know where you want the drag.
- CUE and REV are just buttons. The names are suggestions.
