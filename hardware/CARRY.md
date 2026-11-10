# Carry deck

The original Field Jog, held in one hand. Not the 88 mm Sidekick shell and not the 176 mm twin.

168 × 228 × 26 mm. A 6 inch ring (152.4 mm) with a round screen in the hole. The screen is bolted to the case. The ring spins around it. Grey deck, black ring, orange knob, orange letters printed into pockets. No vinyl, no engraving. The first print tore engraved type out of the skin.

## Why this shape

A 152 mm disc sets the width. 8 mm of deck on each side is enough because you pinch the ring, which stands proud of the skin, not the cheek. 168 mm is narrower than a K.O. II. The length is the disc plus a thumb row: CUE, PLAY, four hot cues, one orange knob.

It is 26 mm thick so a 5000 mAh pouch (about 7 mm) can lie flat under the buttons, next to the board, not stacked under the platter. An SP-404 is this footprint and nearly twice as thick.

The screen is the 1.28 inch GC9A01 you bought. Round board, blue, 240×240, four-wire SPI. The names on the header say SDA and SCL. They are SPI data and clock, not the I2C bus. Pins, as printed on the face: RST, CS, DC, SDA, SCL, GND, VCC. The back of the board says CS and RST can be left off. Wire them anyway.

The glass is 32.4 mm across. The round board is 38 mm, and the pin tab makes the whole module 45.5 mm long. Two 2 mm holes are on the tab. The hole in the ring is 35 mm, so the glass shows and the blue board does not. The tab points at the buttons and sits in a pocket under the vinyl. Screw it down there. Cut the pin header off and solder wires flat. A header left on makes the module 11 mm tall and it will not sit under the ring.

That tab is why the wires do not come out the middle. They leave at six o'clock. The ring still cannot put the MT6701 on the platter axis, because the board is sitting there. The tooth ring and the gear stay.

## The screen does not spin

The module screws to the top shell by the two holes on its tab. The tab points at the buttons, in a pocket, and the wires leave there. The ring is an annulus. Its hole is 35 mm. A skirt on the underside rides a rail outside the tab, so the spinning part never touches the board or the pins. The glass sits 0.4 mm above the ring.

This module has no touch layer. The four pads mark the cues. The screen only shows them.

The MT6701 still cannot sit on the platter axis. The round board is there, and it has no hole through the middle. Under the ring, a tooth ring turns a small gear. The diametric magnet is on the gear, the MT6701 is under the gear, and the firmware divides the ratio back to 1024 ticks a turn. Nothing in the center rotates.

## MCU

Stay on the ESP32-S3. This panel is SPI, 240 by 240, and the S3 drives it on a free SPI bus. A P4 is for a MIPI panel. This is not one.

The 5000 mAh cell is the point of the thickness. With the screen dimmed for a train, the S3 is a long day, not an hour.
