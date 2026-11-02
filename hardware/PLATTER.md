# Conductive-PLA platter: design guidance

**Status: design guidance only. Nothing here has been printed, wired or measured.** The firmware side (touch
detection, finger angle from a segmented ring, tap, calibration) is tested against a simulated ring only
(`firmware/core`). Treat every number below as the first thing to try and expect to change it.

## What the firmware expects

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

## Geometry

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

## Thickness and stack-up

| Layer | Suggestion |
|---|---|
| Conductive sector | 1.2 .. 2.0 mm, 100 % infill, 0.2 mm layers. Carbon-loaded PLA is brittle and abrasive: hardened or stainless nozzle, no thin unsupported walls. |
| Top surface | bare conductive PLA first (largest signal). If it feels gritty, add a 0.4 .. 0.6 mm non-conductive skin: the finger then couples through the skin like a touchscreen overlay and the count rise drops, perhaps by half. Re-tune thresholds after. |
| Air or foam gap below | 3 mm or more before any ground (see below) |

Print the sectors in a **multi-material** plate (conductive + insulating), or print the conductive wedges separately and
press/glue them into a pocketed insulating disc. Check isolation with a multimeter before assembly: adjacent sectors
should read open (megohms), not tens of kΩ.

## Contacts and wiring

* One contact per sector on its underside: an M2 screw into a brass heat-set insert pressed into the sector, a
  copper-tape tab, or a spring pin. **Do not use a printed conductive trace as wiring**: resistance is
  `R = ρ·L/A`; with ρ of tens of Ω·cm, a 1.5 x 1.5 mm trace 50 mm long is several kΩ and is also an antenna. A
  touch input tolerates some kΩ of series resistance, but the useful signal shrinks and noise rises. Keep the
  conductive path from any point of a sector to its contact under ~2 kΩ (check with the multimeter).
* Wire to the pads with thin hook-up wire, **as short as you can (target under 10 cm)** and not bundled with the DAC,
  MIDI or ADC wires. GPIO4..11 are sectors 0..7 (see WIRING.md). Optional 330 Ω .. 1 kΩ in series at the chip end.
* Wires add capacitance. A pad that already has a lot of baseline capacitance gives a smaller per-mille rise for the
  same finger. If counts barely move, shorten wires before touching any firmware setting.

## Ground, shielding, noise

* The S3 touch sensor measures capacitance to ground, and the user's hand couples to ground through the case or the
  USB cable. Battery-only or floating setups behave differently from USB-connected ones; test connected to the host.
* A grounded copper plane **under** the platter (not touching it, 3+ mm below) shields the pads from the electronics
  underneath (USB, radio, DAC clocks) but adds baseline capacitance, which lowers the relative signal. Try without
  first; add it if idle counts jitter by more than ~0.3 % of baseline.
* Ground-pour a guard ring around the wiring on any PCB, not under the pad connections.
* Keep the Wi-Fi antenna (module end of the DevKit) away from the pad wiring; ESP-NOW bursts show up as periodic
  noise on touch counts.

## Expected counts (what to look for on the monitor)

The firmware logs `touch: ... idle peak count N, shift S` at boot and a `sens:` line each second with every pad's
count. Typical shape, to be confirmed on the real thing:

* Idle counts: a few thousand to a few tens of thousands per pad, **stable to within a fraction of a percent**
  (peak-to-peak over a second). Similar sectors read similar counts (within ~±20 %).
* Finger on a sector: that sector's count rises by roughly **3 .. 30 %**, a neighbour by less. A palm raises
  everything.
* Counts of ~0 or ~full scale: wiring fault (open or shorted pad).

If the rise is smaller than the idle noise, nothing in firmware can recover it: fix the hardware (shorter wires,
bigger sectors, thinner skin).

## Tuning (uses the calibration recipe in PROTOCOL.md 3.2)

1. `STREAM` telemetry from the macOS app's Test tab, finger off for 1 s: note each pad's idle noise as a per-mille of its
   count.
2. Put a finger on each sector in turn: note the per-mille rise of that sector.
3. `touchOnPermille` = 50 % of the **weakest** sector's rise; `touchOffPermille` = 60 % of that; never below 3x the idle
   noise. `SET_CONFIG`, check, `SAVE_CONFIG`.
4. Spin a finger around clockwise while streaming; if `jogTotal` decreases set `jogInvert = 1`.
5. If the scan is too slow (`touch ... fresh=` well below 100 % on the monitor line), lower
   `EPDJ_TOUCH_CHARGE_DISCHARGE_TIMES` (menuconfig); if counts are coarse, raise it.

## Behaviour to expect, from the firmware design

* A finger resting on the platter at power-up is not seen as a touch until it is lifted once.
* The baseline follows slow drift (5 s time constant). A very long hold (over 120 s) is re-learned as untouched.
* Tap (touch and release within `tapMaxMs`, default 180 ms, with little movement) sends the play pulse.
* No bearing, no magnet, no encoder in this mode: scratching is a finger sliding on a fixed disc.
