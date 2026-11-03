# The platter: AS5600 spinning platter, hold pad, and the experimental touch ring

**Status: design guidance only. Nothing here has been printed, wired or measured.** The current build is a **free-spinning
platter on a bearing with an AS5600 magnetic encoder under it** (`platterSource = AS5600`, the contract default since v1.3),
plus **one touch pad** (GPIO4) for hold/touch detection. The earlier segmented touch ring is kept as an experiment, further
down, and is not the path to follow now.

Two things are verified by tests in simulation only (`firmware/core`, `docs/E2E.md`): the firmware turns AS5600 angles into
jog (1024 ticks per revolution, exact) and turns a pad reading into touch/tap. Two things are **not validated by anyone**:
that a conductive-PLA platter that spins can give a usable hold signal (this file lists two ways to try, both untested),
and that the mechanics below produce the 1 to 3 mm magnet gap (the paper check against the 16 mm SCAD at shell commit 15417b6 is in
[`README.md`](README.md#stack-up-cross-check-on-paper-shell-15417b6-nothing-measured): the margin is thin and paper cannot settle it;
buy an M3x5 and an M3x6 and let the boot log's magnet status judge).

## The default build

```
                 conductive-PLA platter (76 mm disc, 3.8 mm)          spins on the 683ZZ
                 +----------------------------------------+
                 |  M3x6  button-head axle, head in the   |   hold pad: ONE touch pad, GPIO4,
                 |  platter's pocket, shank down through  |   reaches the platter by capacitive coupling
                 |  the bearing                           |   or a brush (options a and b below)
                 +--------------------+-------------------+
                                      | 683ZZ bearing (3x7x3 mm) in the deck
                          6x2.5 mm DIAMETRIC magnet glued to the shank end
                                      |  gap 1 .. 3 mm
                          AS5600 breakout, chip up, on the encoder pad   --- I2C ---> GPIO21 SDA / GPIO47 SCL
```

Mechanics are `field-jog/field-jog.scad` and `field-jog/README.md`; nothing is invented here. From the README: **one 683ZZ
bearing (3 x 7 x 3 mm) pressed into the deck, one M3 x 6 button-head screw as the axle** (head in a pocket on the
underside of the platter, shank pointing down through the bearing, a dot of threadlocker so it spins with the platter; the list
said M3 x 12 before shell commit 15417b6),
**one 6 x 2.5 mm diametric magnet glued to the shank's end**, and **the AS5600 breakout screwed to the pad under the
platter, chip up, 1 to 3 mm from the magnet** (tape shims if the gap is wrong). The magnet must be **diametrically**
magnetised (north and south on opposite sides of the round face, not on the two flat faces): an axial magnet gives the
AS5600 nothing to measure. Wiring and the 3.3 V / 5 V strap trap: [`WIRING.md`](WIRING.md). What the SCAD already draws and
what it does not: [`README.md`](README.md).

**What the firmware does with it.** The encoder gives the angle: 4096 counts per revolution, 4 counts per jog tick, so
1024 ticks per revolution. The firmware **counts** all motion, touched or not (telemetry's `jogTotal`), but whether motion with no
hand on the platter is **sent** is the `jogUntouched` setting (contract v1.4). The default, AUTO, sends it **until the hold pad has
registered a touch since power-up** (so the encoder works before the pad does) and then stops: a free-spinning platter coasts for
seconds after the hand leaves, and sent as jog that coast would bend the pitch the whole time (the original Field Jog firmware
sends jog only while touched). After that proof, jog goes out only while the pad says touched; REPORT always sends untouched
motion as a pitch nudge, SUPPRESS never does. `jogInvert` is the software twin of the
AS5600's DIR pin. The pad gives "a finger is on the platter": touch-down and touch-up notes, tap-play, the tempo nudge in
SYNC_MASTER, and now also **whether the jog is sent at all**. It has no say in the angle, so a noisy pad hurts hold/tap detection and
the jog gate, never the jog count. After a touch longer than 7 s the pad's release is only believed once the platter has made no tick for
80 ms (a dropout in the middle of a long scratch must not end scratch mode); the flip side is that a hand lifted off a platter that is
still spinning, after such a long touch, keeps the touch on until the platter stops.

## The conductive-PLA platter has two jobs, and the second one is unsolved

It is still the platter, printed in conductive PLA (the same disc as the SCAD's `platter` module, which is printed in black
PLA in `field-jog/`; a plain disc, not sectors). In this build it has to:

1. **Spin.** It sits on an axle and a bearing, so it cannot be wired to the pad with a wire: the wire would wind up.
2. **Carry the hold signal across a rotating joint** to a pad that stays put on the DevKit.

The firmware's input is one touch-pad reading (larger = more touched, `touchRaw[0]` in telemetry). Anything that makes a
fixed pad see a change when a finger rests on the spinning disc will do. Two ways to try; **both are UNVALIDATED**: nobody
has built, measured or even simulated them beyond "the firmware accepts a pad reading".

### Option (a): capacitive coupling to a stationary pad under the platter (UNVALIDATED)

A fixed copper-foil pad (or ring) on the deck, directly under the platter and as close to its underside as the mechanics
allow, wired to **touch pad GPIO4**. The conductive platter floats above it as a plate: a finger on the platter couples to
ground through the person, and the platter couples to the foil through the small gap, so the pad sees the finger as a
capacitance in series with the platter-to-foil capacitance.

* The signal is that series combination, so it is **smaller than a direct touch** and shrinks with the gap and grows with
  the overlap area. Expect a much smaller per-mille rise than the 40 per mille the simulation uses. If the rise is lost in
  the idle noise nothing in firmware recovers it (see the counts section below).
* A **ring** of foil (an annulus, concentric with the axle) keeps the coupling the same at every platter angle; a partial
  pad would modulate it as the platter turns. The platter's own asymmetries (grip rings, the plus pocket) will add some
  modulation anyway.
* The platter must **not** be grounded anywhere, and the steel axle and bearing must not connect it to a ground: a floating
  plate is the idea. Whether the bearing and the M3 screw make the platter float or ground it is not known.
* Mechanics, checked against the current SCAD (see `README.md`): the platter's underside sits at the well floor (z = 15.3 mm), so
  there is **no designed air gap** and no recess for foil. A foil pad on the well floor would be rubbed by the spinning
  platter; a recess, and a wire path through the 4.2 mm top plate to the DevKit, are not drawn.
* A non-conductive layer between foil and platter (a 0.1 mm sheet, or the foil's own adhesive) keeps them from shorting; a
  short would be a galvanic contact, which is option (b) by accident.

### Option (b): a spring or brush contact on the axle (UNVALIDATED)

A spring finger, brush or pogo pin that touches the rotating axle (the M3 shank between the bearing and the magnet is the
only bare metal that turns with the platter) and carries it to GPIO4. The idea: the conductive platter touches the screw
head in its pocket, and the shank carries the finger's signal down to the contact.

* Contact resistance and noise of a sliding metal contact on a 3 mm shank are unknown; a dry sliding contact makes
  intermittent readings, which the touch detector would read as touches and releases. The pad signal only says "a finger is
  on it", so a bad contact breaks hold and tap detection, and (once the pad has registered a touch, under `jogUntouched` AUTO) the
  jog gate (jog goes out only while touched), not the jog angle the firmware counts.
* Whether the conductive PLA makes a reliable connection with the screw head is unknown. The M3 head and the magnet at the
  shank's end leave little room: the SCAD's magnet zone is z = 8.1 to 10.6 mm and the contact would have to sit between the
  bearing's underside (z = 12.0 mm) and the magnet, around the shank, through the sleeve's 5.6 mm bore (z 10.7 to 12.0). Nothing is
  drawn for it.
* Wear: a spring rubbing a rotating shaft wears both.
* The bearing itself is **not** a contact (balls on grease): do not count on the bearing's inner and outer race being
  connected through it.

### Without a hold pad (`EPDJ_TOUCH_PAD_COUNT = 0`, or nothing wired to GPIO4)

Jog works (with `jogUntouched` AUTO, untouched motion **is** sent until a pad has registered a touch, which with no pad is
always: the encoder works before the pad does); **nothing that depends on a finger being on the platter does:**

* **No touch note is ever sent**, so DJ apps never enter scratch mode. The platter is a pitch-bend wheel: motion goes to
  the app's jog/pitch-bend input, not to its scratch engine. (Mixxx: untouched motion -> `[ChannelN],jog`; touched motion
  -> `scratchEnable`/`scratchTick`.) A pad that is configured but not connected or not working behaves the same way: it never
  registers a touch, so AUTO keeps sending the untouched jog and the platter stays a pitch wheel (a floating pad that reports a
  phantom touch counts as proof, and silences coasting from then on: `docs/BRINGUP.md`).
* **No tap-play** (touch and release without moving toggles play/pause).
* In **SYNC_MASTER** the platter is not a tempo wheel (the nudge counts ticks only while touched).
* The status LED never shows "touched".

The boot log says so: `no hold pad wired (EPDJ_TOUCH_PAD_COUNT = 0)`. With the pad wired, SysEx telemetry carries its
count in `touchRaw[0]`; untouched it should sit on a steady idle count.

### What to measure first on a built platter

1. Idle `touchRaw[0]` with the platter spinning slowly and nobody touching it: is it steady (within about 0.3 % over a
   second), and does it stay steady through a whole revolution?
2. A finger on the platter: the per-mille rise of `touchRaw[0]`, still and while spinning. Target at least three times the
   idle noise; the factory thresholds are `touchOnPermille` 12 and `touchOffPermille` 7.
3. Set the thresholds as in `docs/PROTOCOL.md` 3.2 (the recipe is per pad: `touchOnPermille` = 50 % of the rise,
   `touchOffPermille` = 60 % of that, never below 3x the idle noise): with the Mac app's hold-pad wizard, which reads
   `touchRaw[0]` (new, never run against a real pad), or by hand over `STREAM` telemetry (`docs/BRINGUP.md` section 2b).
4. Touching it and letting the platter coast: touch-up must fire when the hand leaves, not when the platter stops.

With ONE pad nothing auto-ranges the touch measurement (`touch_pads.cpp` does that only for two or more pads) and the
firmware uses `EPDJ_TOUCH_CHARGE_DISCHARGE_TIMES` = 500 cycles as it stands. By the formula in the ring section below
that gives idle counts roughly ten times the ring's (tens of thousands; the HAL shifts them to fit 16 bits). That is an
estimate, not a measurement; the boot log prints the real `idle peak count`.

## The encoder: what to expect (from memory of the datasheet; verify against the part you hold)

* **Which register, and the chip's filters.** By default the firmware writes the chip's CONF register at start-up (watchdog off,
  hysteresis 0, slow filter 16x, fast-filter threshold setting 7 = 10 LSB, the original Field Jog firmware's settings) and reads the
  filtered ANGLE register (0x0E). `EPDJ_AS5600_FILTERED_ANGLE` off reads RAW ANGLE (0x0C), documented as unscaled and unmodified,
  which is what this firmware read before. The slow filter costs lag (the datasheet excerpts we saw quote roughly 2 ms; unchecked)
  and buys quiet at rest; whether the buy is worth the lag on the real chip is the bench A/B described in `WIRING.md`. Neither the
  chip's noise nor its delay has been measured here.
* **Noise at rest.** The tracker needs four counts of net movement for one tick and carries the remainder, so jitter cannot
  accumulate into drift. In simulation (`firmware/core/test/test_engine_as5600.cpp`, results in `firmware/core/README.md`): with a
  finger down and the platter still, the filtered register stays within one tick of where it started for up to 4 counts rms of
  raw chip noise and is silent up to 3; the raw register is quiet only up to about 1 count rms and flickers by several ticks, over a
  hundred times a second, beyond that. Those noise figures are a model, not a measurement. If the idle `angle` in telemetry wanders by
  more than a count or two, fix the mechanics (gap, magnet centring, a wobbling axle) before blaming the firmware, and A/B the
  register switch.
* **Resolution.** 12 bits is 0.088 degrees per count; a tick (4 counts) is 0.35 degrees.
* **Alignment.** The magnet centred under the chip to within about a millimetre; an off-centre or wobbling magnet shows up as
  angle error that repeats every revolution.

---

## Experimental: the segmented touch ring (`platterSource = TOUCH_RING`)

**Not the current path.** Kept because the owner may come back to it: a fixed conductive-PLA disc in N isolated sectors,
each wired to its own touch pad (GPIO4..11 for sectors 0..7), scratched by sliding a finger on it. No bearing, no magnet,
no encoder. The firmware side is fully supported and tested (against a simulated ring); the hardware has never been
printed. Everything in the rest of this file is about that ring; set `EPDJ_PLATTER_TOUCH_RING` and `EPDJ_TOUCH_PAD_COUNT` to
3..8 in `menuconfig` to try it.

### What the firmware expects

The platter is a disc of **N isolated conductive sectors** (default N = 8, allowed 3..8, config key `touchSectors`).
Each sector is wired to one ESP32-S3 touch input. Sector *i* is centred at *i*/N of a turn, clockwise seen from
above, with sector 0 at 12 o'clock. The firmware computes:

* **touched** = the strongest sector's count has risen more than `touchOnPermille` (default 12 = 1.2 %) over its own
  slowly tracking baseline (hysteresis: off below `touchOffPermille`, default 7);
* **finger angle** = a phasor sum over all sectors of their rise above a floor, so a finger between two sectors
  reads between them;
* **jog** = change of that angle between samples while touched, as relative CC at 1024 ticks per revolution.

The pads are read as the ESP32-S3 touch sensor's raw count: **larger = more touched** (a finger adds capacitance, so
each charge/discharge cycle takes longer). Unequal sectors are tolerated: sensitivity per sector is learned at run time
within a 0.5 .. 2x range.

### Geometry

Field Jog platter: 76 mm diameter. Start with:

| | Value | Why |
|---|---|---|
| Sectors | 8 (6 is a good fallback) | resolution of the angle comes from interpolating between neighbours |
| Gap between sectors | 1.0 .. 1.5 mm, filled with non-conductive filament (flush) | small enough that a fingertip always overlaps two sectors near a boundary, large enough to stay isolated through print tolerance (a 0.4 mm gap bridges easily) |
| Sector shape | wedges, optionally cut to a ring (hole in the middle for the centre mark) | equal areas keep the counts comparable |
| Outer radius | 36 mm | leave 2 mm of rim |

**Sector size versus fingertip.** The estimate is best when a fingertip (about 12-18 mm wide) covers 1-1.5 sector
widths. The arc of a sector at mean radius *r* is `2πr / N`:

| N | arc at r = 30 mm | arc at r = 22 mm |
|---|---|---|
| 8 | 23.6 mm | 17.3 mm |
| 6 | 31.4 mm | 23.0 mm |

On a 76 mm platter with 8 sectors, a finger on the outer part covers well under one sector. If angle tracking is
lumpy there, try a **ring** (inner radius ~12 mm, outer ~30 mm) so the finger lives at r ≈ 20-24 mm, or drop to 6
sectors. That is a trade the printed result will decide; the firmware does not care.

### Thickness and stack-up

| Layer | Suggestion |
|---|---|
| Conductive sector | 1.2 .. 2.0 mm, 100 % infill, 0.2 mm layers. Carbon-loaded PLA is brittle and abrasive: hardened or stainless nozzle, no thin unsupported walls. |
| Top surface | bare conductive PLA first (largest signal). If it feels gritty, add a 0.4 .. 0.6 mm non-conductive skin: the finger then couples through the skin like a touchscreen overlay and the count rise drops, perhaps by half. Re-tune thresholds after. |
| Air or foam gap below | 3 mm or more before any ground (see below) |

Print the sectors in a **multi-material** plate (conductive + insulating), or print the conductive wedges separately and
press/glue them into a pocketed insulating disc. Check isolation with a multimeter before assembly: adjacent sectors
should read open (megohms), not tens of kΩ.

### Contacts and wiring

* One contact per sector on its underside: an M2 screw into a brass heat-set insert pressed into the sector, a
  copper-tape tab, or a spring pin. **Do not use a printed conductive trace as wiring**: resistance is
  `R = ρ·L/A`; with ρ of tens of Ω·cm, a 1.5 x 1.5 mm trace 50 mm long is several kΩ and is also an antenna. A
  touch input tolerates some kΩ of series resistance, but the useful signal shrinks and noise rises. Keep the
  conductive path from any point of a sector to its contact under ~2 kΩ (check with the multimeter).
* Wire to the pads with thin hook-up wire, **as short as you can (target under 10 cm)** and not bundled with the DAC,
  MIDI or ADC wires. GPIO4..11 are sectors 0..7 (see WIRING.md). Optional 330 Ω .. 1 kΩ in series at the chip end.
* Wires add capacitance. A pad that already has a lot of baseline capacitance gives a smaller per-mille rise for the
  same finger. If counts barely move, shorten wires before touching any firmware setting.

### Ground, shielding, noise

* The S3 touch sensor measures capacitance to ground, and the user's hand couples to ground through the case or the
  USB cable. Battery-only or floating setups behave differently from USB-connected ones; test connected to the host.
* A grounded copper plane **under** the platter (not touching it, 3+ mm below) shields the pads from the electronics
  underneath (USB, radio, DAC clocks) but adds baseline capacitance, which lowers the relative signal. Try without
  first; add it if idle counts jitter by more than ~0.3 % of baseline.
* Ground-pour a guard ring around the wiring on any PCB, not under the pad connections.
* Keep the Wi-Fi antenna (module end of the DevKit) away from the pad wiring; ESP-NOW bursts show up as periodic
  noise on touch counts.

### Expected counts (what to look for on the monitor)

The firmware logs `touch: ... C cycles per pad, idle peak count N, shift S` and `full scan of N pads takes T us` at boot,
and a `sens:` line each second with every pad's count. Typical shape, to be confirmed on the real thing:

* The sensor measures the pads **one after another**, and the engine reads them every millisecond. At boot the firmware
  shortens the measurement (`C`, from 500 down) until one scan of all pads takes about 0.8 ms
  (`EPDJ_TOUCH_SCAN_TARGET_US`), so each read carries a new value for every pad. The cost is a smaller count: the idle
  count is roughly C x (a few tens of clock cycles per charge/discharge), so **low thousands** is normal, not a fault.
  Larger pads or longer wires make each cycle slower and push C down.
* Idle counts: **stable to within a fraction of a percent** (peak-to-peak over a second; at low-thousand counts that is a
  few counts). Similar sectors read similar counts (within ~±20 %).
* Finger on a sector: that sector's count rises by roughly **3 .. 30 %**, a neighbour by less. A palm raises
  everything.
* Counts of ~0 or ~full scale: wiring fault (open or shorted pad).

If the rise is smaller than the idle noise, nothing in firmware can recover it: fix the hardware (shorter wires,
bigger sectors, thinner skin).

### Tuning (uses the calibration recipe in PROTOCOL.md 3.2)

1. `STREAM` telemetry from the macOS app's Test tab (or by hand: `docs/BRINGUP.md` section 2b), finger off for 1 s: note each pad's idle noise as a per-mille of its
   count.
2. Put a finger on each sector in turn: note the per-mille rise of that sector.
3. `touchOnPermille` = 50 % of the **weakest** sector's rise; `touchOffPermille` = 60 % of that; never below 3x the idle
   noise. `SET_CONFIG`, check, `SAVE_CONFIG`.
4. Spin a finger around clockwise while streaming; if `jogTotal` decreases set `jogInvert = 1`.
5. Scan period and resolution pull against each other. The boot log's `full scan ... takes T us` must be under 1.2 ms
   (above that the log warns: pad values are stale for several reads and the pads are sampled at different instants, which
   smears the finger angle). If the finger rise is lost in the idle noise at that count, raise `EPDJ_TOUCH_SCAN_TARGET_US`
   (a longer measurement, fewer fresh values), and shorten the wiring first; neither the setting nor the threshold advice
   above has been tried on real conductive PLA.

### Behaviour to expect, from the firmware design

* A finger resting on the platter at power-up is not seen as a touch until it is lifted once.
* The baseline follows slow drift (5 s time constant). A very long hold (over 120 s) is re-learned as untouched.
* Tap (touch and release within `tapMaxMs`, default 180 ms, with little movement) sends the play pulse.
* No bearing, no magnet, no encoder in this mode: scratching is a finger sliding on a fixed disc.
