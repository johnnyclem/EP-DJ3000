# Carry deck

The original Field Jog face, held in one hand. Not the 88 mm Sidekick shell and not the 176 mm twin.

214 × 300 × 22 mm. A 6 inch ring (152.4 mm) with a round screen in the hole. The screen is bolted to the case. The ring spins around it. Lighter stone body, black ring, orange knob and rule, cyan screen arc. Letters are pockets for a second color. No vinyl, no engraving.

## Why this shape

A 152 mm disc with the original's margins does not fit in 168. The cheeks and the knob lane are why. 214 is the ring, a cheek for the ticks, and a lane for the 125 mm fader on the right. The length is the wordmark, the disc, and a centered row of six flush switches.

It is 22 mm thick so a 5000 mAh pouch (about 7 mm) can lie flat under the button row, next to the board, not stacked under the platter.

The screen is the 1.28 inch GC9A01. Round board, 240×240, four-wire SPI. The names on the header say SDA and SCL. They are SPI data and clock, not the I2C bus. Pins, as printed: RST, CS, DC, SDA, SCL, GND, VCC. Wire CS and RST anyway.

The glass is 32.4 mm across. The round board is 38 mm, and the pin tab makes the module 45.5 mm long. The hole in the ring is 35 mm, so the glass shows and the blue board does not. The tab points at the buttons and sits in a pocket under the ring. Cut the pin header off and solder wires flat. A header left on makes the module 11 mm tall and it will not sit under the ring.

## Controls

Six 12 × 12 × 7.3 mm latching switches, flush. The Ø6.9 cap sits in the plane of the deck. The LED is in the cap, not a separate pip. The row is CUE, REV, and four cues, centered across the face.

The pitch fader is the 125 mm body, in the right lane, the same place as the pitch fader on the six. The encoder is upper right, clear of the corner screw.

## The screen does not spin

The module screws to the top shell by the two holes on its tab. The tab points at the buttons, in a pocket, and the wires leave there. The ring is an annulus. Its hole is 35 mm. A skirt on the underside rides a rail outside the tab, so the spinning part never touches the board or the pins.

This module has no touch layer. The six switches mark the cues. The screen only shows them.

The MT6701 still cannot sit on the platter axis. The round board is there, and it has no hole through the middle. Under the ring, a tooth ring turns a small gear. The diametric magnet is on the gear, the MT6701 is under the gear, and the firmware divides the ratio back to 1024 ticks a turn.

## MCU

Stay on the ESP32-S3. This panel is SPI, 240 by 240, and the S3 drives it on a free SPI bus. A P4 is for a MIPI panel. This is not one.

The 5000 mAh cell is the point of the thickness. With the screen dimmed for a train, the S3 is a long day, not an hour.

## Platter retention and the magnet

The LCD occupies the axis, so the magnet cannot sit there. The ring has a skirt that clears the 38 mm board, and a small flange at the bottom of the skirt. A lip on the underside of the lid catches that flange. The ring drops in and cannot lift off. It spins on the lip.

The encoder is off-axis. A gear on the underside of the platter, outside the LCD, turns a small pinion. The magnet is 6 mm, on the pinion, not on the platter. The MT6701 sits under the pinion, clear of the board. The firmware divides the ratio back to 1024 ticks a turn.

## Copper tape

A shallow annular groove on the underside of the lid, just outside the LCD pocket, is the seat for the copper tape. It is 0.4 mm deep so the tape sits flush and the ring does not rub it. A short channel runs from that ring toward the right edge for the lead. The tape is the stationary electrode. The conductive ring is the moving one.
