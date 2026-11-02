# EP-DJ3000 hardware: status, what is missing, builder traps

**Nothing here has been printed, wired or measured, and there is no enclosure for this design.** The firmware targets an
ESP32-S3 with a segmented conductive-PLA platter, a slide pot, a knob, CUE and REV, an optional PCM5102A DAC and a TRS
MIDI jack. The only mechanical design in the repo is the earlier Field Jog shell (`field-jog/`), which was drawn for a
different set of parts.

| File | What |
|---|---|
| [`WIRING.md`](WIRING.md) | Pin table (the source of truth is `board_pins.h`), power, DAC, TRS MIDI, AS5600 option, LED |
| [`PLATTER.md`](PLATTER.md) | Conductive-PLA sector platter: geometry, contacts, noise, tuning; the open question of a spinning platter |
| [`BOM.md`](BOM.md) | What changes from the Field Jog bill of materials |

## What has to be drawn before this can be built into a deck

The Field Jog SCAD (`field-jog/field-jog.scad`) was checked against this firmware. It does **not** provide:

1. **The sector platter.** The platter module is a plain 76 mm disc with an M3 pocket. Needed: 8 conductive wedges with
   1.0 to 1.5 mm non-conductive gaps (`PLATTER.md`), one M2 screw or heat-set-insert boss per wedge, and wire passages
   through the skin. No STL for it exists.
2. **A bay for the board actually used.** The rails are sized for a Pico (21 x 51 mm, `pico_w` and `pico_l`). A
   DevKitC-1 is about 70 x 25 mm (measure yours), so the rails and the bay have to move.
3. **A USB window for two ports.** The window is 12.5 x 7.5 mm, one micro-USB plug. The DevKitC-1 has two USB-C ports
   (native USB for the computer, UART for flashing); a finished unit needs at least the native one reachable, and about
   30 mm of window to expose both (an estimate: measure your board).
4. **A slide-pot mount and lever adapter.** The shell does cut an 8 x 64 mm slot (for the loose fader's magnet stem),
   with hall posts. A pot needs a mounting pad under that slot and a cap that engages its lever.
5. **A second 3.5 mm hole.** There is one 6.3 mm hole, labelled MIDI. Topology A (a DAC per deck) also needs a line-out
   jack.
6. **A pocket for the PCM5102A board**, if the DAC is fitted.

Two decisions come first (also in the [root README](../README.md)): whether the **board** really is the DevKitC-1, and
whether the **platter spins**. If it spins, the electrical path from a rotating conductive platter to a fixed pad also has
to be designed (`PLATTER.md`, "If the platter should spin").

## Builder traps

1. **Status LED pin.** The WS2812 is on GPIO38 on DevKitC-1 v1.1 boards (the default build) and GPIO48 on v1.0; pick
   the revision in `menuconfig` (`EPDJ_BOARD_REV`). On any other board the LED stays dark until you set the pin.
2. **Two USB-C cables.** After the first flash the native port belongs to TinyUSB (the unit is a MIDI device, not a serial
   port), so reflashing and logs need the UART port, or BOOT held while pressing reset. Development uses both cables.
3. **Two 3.5 mm TRS jacks that look alike.** The MIDI jack carries 3.3 V on its ring (through 33 ohm) and the line-out
   jack carries audio. Plugging a mixer cable into the MIDI jack, or a MIDI cable into a mixer input, puts DC on the wrong
   input. Label them, put them far apart, or use a different connector for MIDI.
4. **Printing the sectors.** Conductive PLA is abrasive and brittle: use a hardened or stainless nozzle. The wedges need a
   multi-material print or to be printed separately and pressed into an insulating disc, and adjacent wedges must read
   open on a multimeter.
5. **GPIO47 and 48 sit in the module's VDD_SPI power domain.** Fine on the usual 3.3 V modules; a module with 1.8 V flash
   would drive them at 1.8 V (`WIRING.md`; this is from memory of the datasheet, verify for your module).
6. **Eight thin wires from the platter** to GPIO4..11 must stay short (under 10 cm) and away from the ADC, DAC and MIDI
   wiring, or the touch counts get noisy (`PLATTER.md`).
7. **Setting build options under PlatformIO**: `pio run -d firmware/esp32s3 -t menuconfig` (for the first-boot deck, the
   touch options); see `firmware/esp32s3/README.md`.
