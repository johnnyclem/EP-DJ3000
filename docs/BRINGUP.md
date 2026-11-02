# EP-DJ3000 bring-up checklist

The order of [`ARCHITECTURE.md`](ARCHITECTURE.md) section 6, with what to expect in the log at each step. **Nothing in
the firmware has run on a chip yet.** It compiles and links for the ESP32-S3 (PlatformIO and `idf.py`, ESP-IDF 6.1.0 with
esp_tinyusb 3.0.0 = TinyUSB 0.21.0), and the portable logic is covered by host tests (the USB audio class driver too: see
the last risks), so this checklist is where reality gets to disagree. Write down every
deviation: the "Risks" at the end say which ones are most likely.

Hardware: [`hardware/WIRING.md`](../hardware/WIRING.md), [`hardware/PLATTER.md`](../hardware/PLATTER.md). There is no
enclosure for this design yet ([`hardware/README.md`](../hardware/README.md)): bring it up on the bench.
Firmware build and flash: [`firmware/esp32s3/README.md`](../firmware/esp32s3/README.md).

**The platter is the AS5600 for now** (`platterSource = AS5600`, the default since contract v1.3; the owner's decision).
The first session proves three things, in this order, before anything else:

1. **The AS5600 answers and its magnet status is "detected"** (section 1: the boot log's platter line, then section 2's
   Encoder check).
2. **The direction is right**: spinning clockwise raises `jogTotal`, otherwise `jogInvert` flips (section 2).
3. **A hold pad actually detects a hand on the spinning platter** (section 2). It is the biggest unknown left: the pad's
   signal has to cross a rotating joint that nobody has designed yet, and DJ software enters scratch mode on the touch
   note this pad produces.

**What `jogUntouched` does between steps 1-2 and step 3 (contract v1.4).** The default setting (`jogUntouched = AUTO`) sends
platter motion as jog **until the hold pad has registered one touch since power-up; after that it sends jog only while the pad
reports a touch**, because a free-spinning platter coasts for seconds after the hand leaves and would bend the pitch the whole
time. So with the pad not connected or not working yet, spinning the platter **does** produce jog: a MIDI monitor on CC 22 shows
it and a DJ app sees a pitch nudge. The encoder works before the pad does, with nothing to set. What you will see once the pad
has registered a hand:

* a coasting platter is **silent by design**: no CC 22, nothing in a DJ app, until the next reboot (the proof is kept in RAM and
  starts over at every power-up and whenever `platterSource` changes). The unit still *counts* the motion: telemetry's `jogTotal`
  and the app's Encoder check and Direction steps work, because they read telemetry, not CC 22;
* to see untouched motion again on a unit whose pad has worked, set **`jogUntouched = REPORT`** (SysEx below, section 2b: always
  sent, a pitch nudge) or reboot; **`SUPPRESS`** is "never", from the first tick;
* the unit cannot tell a hand from noise: a floating or chattering pad that reports a phantom touch note counts as proof too
  (risk 11). Watch note 38 on the monitor.

Nothing of this has run on hardware; what is established is the firmware's behaviour in the simulator (`docs/E2E.md`). The boot log
states the policy in force, once, at boot, so it cannot say whether the pad has been proven since:
`jog: platter motion with no hand on it is AUTO: sent until the hold pad registers a touch, then NOT sent`.

Then USB and audio (section 4). The touch ring (finger-slide scratching on a fixed conductive-PLA disc) is the experimental
alternative; its bring-up is [Appendix A](#appendix-a-the-experimental-touch-ring) and only matters if you return to it.

**The five things most likely to invalidate part of the design, in order of damage.** Everything else in this file is
routine next to them; prove these first and write the numbers down.

1. **A hold pad that detects a hand on the spinning platter** (step 2, risks 11, 5 and 10, `PLATTER.md`). The platter is the
   headline feature, DJ software scratches on the touch note this pad produces, and nobody has seen a real count from a pad
   behind a rotating contact. (If you return to the ring, the same question is its signal-to-noise on real conductive PLA:
   Appendix A.)
2. **USB Audio 1.0 with a feedback endpoint on a real host** (step 4, risks 1-3). Try macOS Sound settings first, then
   Linux `aplay`, then Windows.
3. **ESP-NOW latency, jitter and loss** under the bridge and time sync (step 5). Every figure in `LINK.md` is simulated.
4. **`REBOOT(bootloader)`** (step 2), never tried. It is not the supported update path: that is the DevKit's UART port, or
   BOOT held while pressing reset. Find out whether the PHY really is handed back to the ROM before anyone relies on it.
5. **Noticing a pulled USB cable without VBUS sense**, which one-cable mode depends on (step 5, risk 9). Decide early
   whether to add the VBUS divider to the pin plan (`WIRING.md`) rather than rely on an untested idle-detect heuristic.

## 0. Before power

* [ ] Continuity: no short 3V3-GND; the hold pad wire (GPIO4) open to GND; CUE/REV open until pressed.
* [ ] AS5600 breakout: SDA to GPIO21, SCL to GPIO47, 3V3, GND ([`WIRING.md`](../hardware/WIRING.md)); its 3.3 V / 5 V strap or
  jumper on **3.3 V**; the diametric magnet on the end of the axle, 1 to 3 mm from the chip (`field-jog/README.md`; **in the 16 mm
  shell as drawn that gap does not exist and the M3 x 12 axle is too long**: [`hardware/README.md`](../hardware/README.md)); the
  platter turns freely without scraping.
* [ ] Hold pad: the pad wire (GPIO4) reaches the platter one of the two ways `PLATTER.md` lists, both untried. A spring or brush
  contact on the axle (option b): continuity from the platter's touch surface to the pad wire **with the platter still, then
  turned slowly by hand** (a multimeter across the contact: steady, no flicker). Capacitive coupling to a stationary pad
  under the platter (option a) has no DC path to measure: the check is the `touch=[...]` count in section 1. A contact that
  opens, or a count that wanders, while the platter turns is risk 11 before it is anything else.
* [ ] Fader and knob ends on 3V3 and GND, wipers to GPIO1 / GPIO2.
* [ ] If a DAC is fitted: SCK to GND, XSMT high, FMT/FLT/DEMP low, strap GPIO14-GND **removed** for now (step 4 adds it).

## 1. Flash and watch the log

Connect the DevKit's **UART** port, flash, open the monitor at **921600** baud (`pio device monitor` is preset;
`idf.py monitor` follows the build's console baud rate).

Expected, in this order (the numbers are illustrations, not measurements: nothing has run on a chip; yours will differ). The
platter line is the one that matters first; the firmware prints the platter source, whether the AS5600 answered, its magnet
status, the CONF word it wrote and read back (`conf=`), which angle register it reads (`0x0E (filtered)` or `0x0C (raw)`), and how
many touch pads are wired:

```
main: EP-DJ3000 core 0.1.0, protocol v1, ESP-IDF v6.1.0
main: config: nothing saved; deck A
adc: fader GPIO1 raw 2031, knob GPIO2 raw 1180; 24 us per conversion, x6 oversampling; sensor time per ms: ADC 288 us + AS5600 read 110 us = 398 us of 400
touch: scan 1220 us at 500 cycles, 318 us at 125: target 800 us -> 325 cycles
touch: 1 pad(s) on GPIO4..4, 325 cycles per pad, idle peak count 13890, shift 0
touch: full scan of 1 pads takes 800 us (idle)
main: platter: source AS5600 | AS5600 answered at 0x36, magnet detected, conf=0x1C00, angle reg 0x0E (filtered), read 110 us | touch pads wired: 1 (GPIO4 = hold pad)
main: jog: platter motion with no hand on it is AUTO: sent until the hold pad registers a touch, then NOT sent
main: dac: mode AUTO, strap low 0/16 samples, codec none -> absent (MIDI only)
usb: "EP-DJ3000 Deck A" serial 7CDFA1xxxxxx, config 173 bytes, VID:PID 1209:0001, audio off
radio: ESP-NOW on channel 1, station 7C:DF:A1:xx:xx:xx, broadcast peer
trs: UART1 31250 baud, TX GPIO40 RX GPIO41
main: link group 0 (build default)
main: ready: caps 0x0059, serial A1xxxxxx, MIDI channel 1
```

What to look for:

* [ ] **The `platter:` line says `source AS5600`, `AS5600 answered ... magnet detected, conf=0x1C00, angle reg 0x0E (filtered)` and
  `touch pads wired: 1`.** That is step 1 of the first session. The other outcomes, each with its own warning line from the firmware:
  * `AS5600 NOT answering ... asking again every 500 ms` plus a warning that **jog is silent until one does**: the encoder is not
    on the bus. Check SDA (GPIO21) and SCL (GPIO47) are not swapped, 3V3 and GND, the breakout's 3.3 V / 5 V strap. **No reboot
    needed**: the chip is asked again every 500 ms, and the log prints `as5600: encoder appeared at 0x36 after boot (magnet ...,
    conf=0x1C00, angle reg 0x0E): jog is live, no reboot needed` when it answers (a `magnet` WARN follows if it is not "detected").
  * `conf=0x0000 (wrote 0x1C00)` with a WARN, or `conf=?`: the chip answers but did not take the filter configuration, or the write
    or read-back failed on the bus. Jog works with whatever the chip does; look at the wiring (a marginal bus often shows here
    first), at a chip someone burned, and read `hardware/WIRING.md`, "Chip configuration".
  * `angle reg 0x0C (raw)`: the build has `EPDJ_AS5600_FILTERED_ANGLE` off (the bench A/B, below).
  * the chip answers but the magnet is `too weak`, `too strong`, `not detected` or `unknown`: the gap (1 to 3 mm), the magnet
    centred on the axle, and that it is **diametrically** magnetised (`field-jog/README.md` asks for a diametric magnet for the AS5600).
  * `source TOUCH_RING (experimental)` on a unit that saved its configuration before contract v1.3: it keeps what it saved.
    Put it on the new default with `SET_CONFIG platterSource = 0` (section 2b) or Settings > *Load defaults*, then save.
  * `touch pads wired: 0 (no hold pad)` with a warning: the unit never sends a touch note, so DJ apps never enter scratch mode.
  * the `jog:` line after it says whether untouched motion goes out (`AUTO: sent until the hold pad registers a touch, then NOT
    sent`, `AUTO: sent, no hold pad configured`, `REPORT: always sent`, `SUPPRESS: never sent`): see "What `jogUntouched` does" above.
* [ ] `ready` appears and the LED turns on (deck A blue, dim while the platter is untouched). The default build drives
  the WS2812 on GPIO38 (DevKitC-1 v1.1); for a v1.0 board (GPIO48) choose the revision in `menuconfig`
  (`EPDJ_BOARD_REV`, `WIRING.md`). The boot log prints the pin it drives.
* [ ] `adc:` reports the measured cost of one conversion and, now, the AS5600 read. The engine does 2 x oversampling of
  conversions every millisecond with interrupts off, plus the I2C read, so the firmware lowers the oversampling (and warns)
  when the two together would exceed 400 us. Write both figures down.
* [ ] `touch:` timed one full scan of the pads at two settings and chose the charge/discharge cycles that make it take
  about 0.8 ms (`EPDJ_TOUCH_SCAN_TARGET_US`). With the one hold pad that 0.8 ms goes to a single pad, so it gets more cycles
  (and higher counts) than it would in an eight-pad ring, and a fresh value on every read. `full scan of N pads takes ...` must be under 1.2 ms; the warning `scan is slower than the 1 ms engine tick` and
  `could not time the scan` (the scan register did not move: 48 cycles are used as a guess) both mean look here first.
  `limit reached: target not met` means even the shortest measurement (8 cycles) is too slow, or the pad is so small that
  500 cycles already scan faster than the target.
* [ ] `idle peak count` is in the thousands (it scales with the cycles chosen, so it is higher with one pad than with eight). The warning `idle counts are tiny:
  pads not connected` means an open pad or too few charge/discharge cycles. **Check the hand's rise against the idle noise
  at this count** (step 2's Hold pad, risk 5): nobody has seen the signal-to-noise of a pad behind a rotating contact yet.
* [ ] `shift` is 0 unless the counts exceed 32768; then they are halved to fit 16 bits (harmless).
* [ ] `dac:` says how the DAC decision was made. In AUTO the strap pin (GPIO14) is sampled 16 times over 16 ms and counts
  only if every sample is low; a flickering strap prints `(UNSTABLE: wire or contact?)` and counts as absent.
* [ ] `link group` names the radio rig id in force: the `linkGroup` config key when it is non-zero, otherwise the build
  default (`EPDJ_LINK_GROUP`, 0).
* [ ] `caps 0x0059` = AS5600 (0x01) + TRS MIDI (0x08) + link (0x10) + LED (0x40): the AS5600 bit means the chip has answered, at
  boot or (since the runtime re-probe) later: a unit whose chip appeared after boot starts reporting the bit from that moment.
  `0x0020` is added with the USB audio function (the engine adds DAC `0x0004` in `Info`). The touch ring bit (`0x0002`) is set
  only when three or more pads are wired, which the default build does not do.
* [ ] The reset loop with `Guru Meditation` or `assert failed`: note the line, and run the backtrace through
  `pio device monitor` (decoder preset) or `idf.py monitor`.
* [ ] Nothing printed after `ready` except one `diag: sens:` line per second. The engine task itself never logs.

Once a second:

```
diag: sens: fader=2031 knob=1180 angle=2210 cue=0 rev=0 touch=[1452]
```

* [ ] **Turn the platter by hand: `angle` changes smoothly through 0..4095 and wraps.** `angle=0(none)` means no encoder has
  answered since boot; `(bad)` after the number means it answered but a read just failed. A `diag:` warning counts failed I2C
  reads while it was answering (a loose wire; each costs the 1 kHz loop up to 2 ms).
* [ ] **The re-probe** (a minute, and it tells you whether the wiring is solid): with the unit running, pull the SDA wire. Within
  a second the log prints `as5600: encoder stopped answering at 0x36 (two failed reads in a row): jog is silent until it does; asking
  again every 500 ms` and `angle=` shows `(bad)`. Put it back: `as5600: encoder answering again at 0x36 (conf=0x1C00): jog resumed
  from the platter's present position, no jump` within 500 ms, and `jogTotal` has not moved by the distance the platter turned while
  it was out. The loop statistics must stay clean during the outage (a dead chip costs one failed 2 ms read per 500 ms). If `conf=`
  differs after it comes back, the chip lost power and took the default configuration.
* [ ] Move the fader and knob end to end: both span most of 0..4095 and move smoothly. A fader that does not reach
  both ends is calibrated in step 2, not fixed in wiring.
* [ ] Press CUE, REV: `cue=1` / `rev=1`.
* [ ] Put a flat hand on the platter: the `touch=[...]` count rises. Write down the idle noise (peak-to-peak) and the rise
  with the platter still, **and again with it turning under the hand** (risk 11).
* [ ] Every 10 s: `diag: loop: ... N over 1 ms (max M us), K late wake-ups, R resyncs`. A healthy loop has 0 overruns and
  a max well under 1000 us. If it does not: the ADC conversions are the biggest single cost (the `adc:` line says how much
  budget they take), then the AS5600 read and a stuck I2C bus (up to 4 ms per 100 ms), then what shares core 1. `resyncs`
  counts the times the loop was more than two ticks behind and dropped the lost ticks instead of running flat out; it must
  stay 0.

## 2. macOS app: sensors and calibration

No Mac, or the app does not build: do the same with SysEx by hand (section 2b).

Connect the native **USB** port to the Mac. In the log:

```
usb: mounted by host
```

* [ ] The Mac lists "EP-DJ3000 Deck A" under Audio MIDI Setup -> MIDI Studio with three ports ("Deck", "TRS MIDI",
  "Linked deck"). `system_profiler SPUSBDataType` shows VID 0x1209, PID 0x0001 and the serial.
* [ ] App Test tab: telemetry streams (`STREAM`). Fader and knob bars follow the controls. Buttons light.
* [ ] App Calibrate tab: with `platterSource = AS5600` the steps are *Encoder check*, *Platter direction*, *Hold pad*,
  *Slide pot*, *Knob*, in that order. Do them in that order.
* [ ] **Encoder check** (first session, step 1 again): spin the platter at least one full turn. The app says the AS5600
  follows the platter and how many turns it measured. If not it says which of four things it found: no chip has answered yet
  (wiring, 3.3 V strap), the chip answered but no valid angle arrives, the angle does not move a full turn (no magnet, too
  far, not on the axle), or the angle jumps around (magnet off-centre, too weak or too strong). After fixing the wiring
  the unit finds the chip within 500 ms by itself (and sets the AS5600 bit in `Info.caps`), but the app reads `Info` when the
  check starts: run the check again, or reconnect, rather than restarting the unit.
* [ ] **Direction** (first session, step 2): spin the platter clockwise as seen from above for a quarter turn. `jogTotal`
  must rise (it counts untouched motion whatever `jogUntouched` says, so this works with a pad that does not work yet); if it falls the app writes `jogInvert = 1 - current`. Without the app: stream telemetry, spin, and if
  `jogTotal` falls send the `jogInvert` frame in section 2b.
* [ ] **Hold pad** (first session, step 3): hands off for a second, then a flat hand on the platter, held until the app says
  release. It writes `touchOnPermille` / `touchOffPermille` (50% and 30% of the hand's rise, never under 3 times the idle
  noise: [`PROTOCOL.md`](PROTOCOL.md) section 3.2). A failure names the pad: reads zero (its wire or the rotating contact is
  open), no hand seen, too noisy at idle, or a hand that rises too little for the noise.
* [ ] **What the wizard cannot see: the pad while the platter turns.** After applying, put a flat hand on the platter and
  turn it slowly, then fast, with the Decks tab open and the MIDI monitor on the Deck port. The TOUCH light must come on once
  and stay on, and note 38 must show one on and one off for the whole gesture. Then let the platter coast with no hand for 30
  s: note 38 must stay silent. A touch that flickers off and on under a steady hand is the rotating contact dropping out (risk
  11); write down how often, and at what speed. (A dropout of up to a few tens of milliseconds in the middle of a touch longer than
  7 s is bridged and must not show; risk 13.)
* [ ] Calibrate fader and knob (sweep end to end); `CAL_END(apply)` reports a span above 1500 counts.
* [ ] *Save to unit* (`SAVE_CONFIG`). On the monitor nothing new appears, but the next boot prints `config: ok` instead of
  `nothing saved`.
* [ ] `IDENTIFY` makes the LED blink white.
* [ ] Settings tab (or any `GET_CONFIG 0xFF`): 34 keys (ids 0-33) arrive, then `ACK[GET_CONFIG]` as the end marker; a dump
  that stops without the ACK is incomplete. The dump is 35 SysEx frames = 174 USB-MIDI packets = 696 bytes; the MIDI TX FIFO
  is 1024 bytes, so the whole dump fits in one go. The next `usb:` stats line should show `tx stalls` and `drops` unchanged.
  If they climb, the host is not reading the port fast enough (drops mean the rest of that frame was abandoned after
  100 ms; once that has happened every later event is dropped at its first refused packet until the host reads again).
* [ ] **Nobody reading the MIDI port.** Close every MIDI application, then move the platter and fader for 30 s while
  watching a second terminal with the TRS MIDI output (or the other deck's log): TRS and the link must keep their full rate.
  The `usb:` line shows `drops` climbing and `tx stalls` stopping at about 100 (one give-up window); open a MIDI monitor
  afterwards and note how much stale traffic it receives at first (up to 256 packets sit in the FIFO).
* [ ] **`REBOOT(bootloader)`** (app: Settings -> Reboot to bootloader). Never tried on a chip. Expected: the Mac's "EP-DJ3000"
  disappears, then a "USB JTAG/serial debug unit" (303a:1001) appears and `esptool --before no-reset --port <it> chip-id`
  answers. If the Mac sees nothing, the PHY was not handed back (`usb_dev::releaseForRomDownload`): use the UART port or
  BOOT + reset to flash, and tell the owner. Also try it on a unit with a DAC (audio function mounted).
* [ ] A flash write happens on `SAVE_CONFIG`: expect a brief hiccup in the engine tick (a `late wake-up` in the
  stats line is normal at that moment).

## 2b. Without the macOS app

The Mac app has never been compiled ([`macos/README.md`](../macos/README.md)), and Windows and Linux have no app at all.
Everything bring-up needs from it is plain SysEx ([`PROTOCOL.md`](PROTOCOL.md) section 3), so any program that can send
raw SysEx to the unit's **Deck port (USB-MIDI cable 0**, not "TRS MIDI" or "Linked deck") and show what comes back will
do: SysEx Librarian or MIDI Monitor on macOS, MIDI-OX on Windows, `amidi -l` then `amidi -p hw:N,0 -S 'F0 7D ...'` on
Linux, or `sendmidi` anywhere. **None of those programs was run here and nothing was sent to a unit.** What was run: every
frame below, built with the snippet below, was fed to the real firmware `Engine` through the end-to-end simulator
(`docs/E2E.md`) and answered as the third column says, and the snippet reproduces `protocol/test-vectors.json` exactly.

The frames are addressed to deck `7F` (any), so every unit on that port acts on them. With two units on USB, address one:
change the deck argument (0 = A, 1 = B); the checksum depends on it, so build the frame with the snippet instead of editing
the bytes. Frames are shown as the bytes a SysEx tool sends, `F0 ... F7`.

| What | Frame | Reply |
|---|---|---|
| Identify the unit (GET_INFO) | `F0 7D 45 44 01 7F 01 7F F7` | `INFO` (cmd `41`) |
| Read all 34 config values (GET_CONFIG) | `F0 7D 45 44 01 7F 02 01 7F 7E F7` | 34 x `CONFIG` (cmd `42`), then `ACK` for cmd 2 |
| Start telemetry at 20 Hz (STREAM) | `F0 7D 45 44 01 7F 06 00 14 66 F7` | `ACK`, then `TELEMETRY` (cmd `47`) frames for 5 s; send it again to keep it going |
| Stop telemetry | `F0 7D 45 44 01 7F 06 00 00 7A F7` | `ACK` |
| Fader calibration: start (CAL_BEGIN fader) | `F0 7D 45 44 01 7F 07 00 00 79 F7` | `ACK` |
| Fader calibration: finish and apply (CAL_END fader) | `F0 7D 45 44 01 7F 08 00 00 01 77 F7` | `CAL_RESULT` (cmd `48`), or `NAK` (cmd `7F`) with error 9 if the sweep was under 1500 counts |
| Knob calibration: start | `F0 7D 45 44 01 7F 07 00 01 78 F7` | `ACK` |
| Knob calibration: finish and apply | `F0 7D 45 44 01 7F 08 00 01 01 76 F7` | `CAL_RESULT` or `NAK` |
| Slide pot as channel volume (SET_CONFIG faderRole = CHANNEL_VOLUME): a per-deck scratch cut, audible in Mixxx only | `F0 7D 45 44 01 7F 03 00 0B 01 00 00 00 71 F7` | `ACK` |
| Slide pot as crossfader (faderRole = CROSSFADER): one unit into a single software mixer | `F0 7D 45 44 01 7F 03 00 0B 00 00 00 00 72 F7` | `ACK` |
| Slide pot as tempo fader (faderRole = PITCH): the factory default, so this is "back to normal" | `F0 7D 45 44 01 7F 03 00 0B 02 00 00 00 70 F7` | `ACK` |
| Flip the jog direction (jogInvert = 1) | `F0 7D 45 44 01 7F 03 00 04 01 00 00 00 78 F7` | `ACK` |
| **Always send untouched platter motion as jog (jogUntouched = REPORT)**: a coasting platter becomes a pitch nudge even after the hold pad has worked (see the top of this file). Live; `SAVE_CONFIG` keeps it | `F0 7D 45 44 01 7F 03 00 21 01 00 00 00 5B F7` | `ACK` |
| Back to the default (jogUntouched = AUTO): untouched motion is sent until the hold pad has registered a touch, then not | `F0 7D 45 44 01 7F 03 00 21 00 00 00 00 5C F7` | `ACK` |
| Jog only while touched (jogUntouched = SUPPRESS), from the first tick | `F0 7D 45 44 01 7F 03 00 21 02 00 00 00 5A F7` | `ACK` |
| Platter source AS5600 (platterSource = 0): the factory default since contract v1.3. A unit that saved its config under v1.2 or earlier keeps `TOUCH_RING` until it gets this or `LOAD_DEFAULTS` + `SAVE_CONFIG` | `F0 7D 45 44 01 7F 03 00 03 00 00 00 00 7A F7` | `ACK` |
| Platter source TOUCH_RING (platterSource = 1): experimental, Appendix A | `F0 7D 45 44 01 7F 03 00 03 01 00 00 00 79 F7` | `ACK` |
| Platter source TOUCH_HOLD_ONLY (platterSource = 2): touch only, no rotation | `F0 7D 45 44 01 7F 03 00 03 02 00 00 00 78 F7` | `ACK` |
| Clock master mode (mode = SYNC_MASTER) | `F0 7D 45 44 01 7F 03 00 02 01 00 00 00 7A F7` | `ACK` |
| Back to DJ controller (mode = DJ) | `F0 7D 45 44 01 7F 03 00 02 00 00 00 00 7B F7` | `ACK` |
| Emit MIDI Time Code in SYNC_MASTER (mtcEnable = 1) | `F0 7D 45 44 01 7F 03 00 18 01 00 00 00 64 F7` | `ACK` |
| Keep what you set (SAVE_CONFIG) | `F0 7D 45 44 01 7F 04 7C F7` | `ACK`, or `NAK` error 10 if flash failed |
| Factory defaults, keeping the deck (LOAD_DEFAULTS) | `F0 7D 45 44 01 7F 05 7B F7` | `ACK`; then `SAVE_CONFIG` to persist |
| Blink the LED for 5 s (IDENTIFY) | `F0 7D 45 44 01 7F 0D 00 05 6E F7` | `ACK` |
| Who is on the radio (LINK_STATUS) | `F0 7D 45 44 01 7F 0B 75 F7` | `LINK` (cmd `4B`) |
| Make the unit that answers to deck A become deck B (SET_DECK 1) | `F0 7D 45 44 01 00 09 00 01 75 F7` | `ACK`, then the unit restarts as Deck B |

The slide pot's role (`faderRole`, key 11) is **PITCH out of the box** (contract v1.2): CC 25/57, a CDJ-style tempo slider, which
VirtualDJ, djay Pro and rekordbox act on even behind an external mixer, and which two units never fight over. Change it only
for a reason: `CHANNEL_VOLUME` (CC 7/39) is a per-deck scratch cut that does anything audible only in Mixxx (its deck outputs
are post-fader); `CROSSFADER` (CC 23/55) is for one unit into a single software mixer. The change is live; `SAVE_CONFIG`
keeps it. A unit that saved its config under an earlier contract keeps the role it stored (v1.1 defaulted to
`CHANNEL_VOLUME`): `LOAD_DEFAULTS` then `SAVE_CONFIG` (it keeps the deck id), or the PITCH frame above, gets it onto the new default.

Replies are `F0 7D 45 44 01 <deck> <cmd> ... F7`: `7E` is an ACK (payload = the command it answers), `7F` a NAK (command,
then error code: `PROTOCOL.md` has the table). For the calibration frames, the sweep happens between the two frames: move
the control end to end, slowly, and dwell at each end.

**Building other frames, and reading telemetry.** Python 3, standard library only. `frame` builds any request;
`telemetry` decodes one `TELEMETRY` frame captured as hex: `flags` (bit 1 = ANGLE_VALID, bit 0 = touched), `angle`, `jogTotal`
and `touchRaw[0]` are what the encoder check, the direction check and the hold-pad recipe in `PROTOCOL.md` section 3.2 need
(`touchRaw` for the ring recipe too: `PLATTER.md` "Tuning").

```python
import struct

def frame(deck, cmd, payload=()):
    raw, sept = list(payload), []
    for i in range(0, len(raw), 7):                      # 8 -> 7 packing
        grp = raw[i:i + 7]
        sept.append(sum((b >> 7) << j for j, b in enumerate(grp)))
        sept += [b & 0x7F for b in grp]
    body = [1, deck, cmd] + sept
    return [0xF0, 0x7D, 0x45, 0x44] + body + [(-sum(body)) & 0x7F, 0xF7]

def set_config(deck, key, value):                        # key ids: PROTOCOL.md section 3.1
    return frame(deck, 0x03, [key] + list(value.to_bytes(4, "little", signed=True)))

def unpack(sept):
    out = []
    for i in range(0, len(sept), 8):
        hdr, grp = sept[i], sept[i + 1:i + 8]
        out += [b | ((hdr >> j) & 1) << 7 for j, b in enumerate(grp)]
    return bytes(out)

def telemetry(hexstr):                                   # one 0x47 frame, e.g. "F0 7D 45 44 01 00 47 ..."
    f = bytes.fromhex(hexstr)
    assert f[6] == 0x47, "not a TELEMETRY frame"
    t = struct.unpack("<4B6Hi8H", unpack(f[7:-2]))
    names = ("seq", "buttons", "flags", "transport", "faderRaw", "knobRaw", "faderOut", "knobOut", "angle", "bpmX10", "jogTotal")
    return dict(zip(names, t), touchRaw=t[11:])

print(" ".join(f"{b:02X}" for b in set_config(0x7F, 6, 20)))   # touchOnPermille = 20 for every unit
```

Typical use, for step 2 of this file, by hand:

* **Encoder check.** `GET_INFO` and read `caps` (bit 0 = a chip has answered, at boot or since). Send `STREAM`, spin the platter a full turn
  by hand and decode a dozen frames: `flags & 2` must be set in every one and `angle` must move smoothly through 0..4095
  and wrap. All `flags & 2` clear means the reads fail; a constant `angle` means no usable magnet.
* **Direction.** Spin clockwise as seen from above: `jogTotal` must rise. If it falls, send the `jogInvert = 1` frame (or 0 if
  it is already 1).
* **Hold pad.** With hands off, decode a second's worth of frames and note `touchRaw[0]` (its mean and its peak-to-peak noise);
  put a flat hand on the platter and note the peak. The rise is `(peak - mean) / mean x 1000` per-mille. Set
  `touchOnPermille` = 50% of the rise and `touchOffPermille` = 60% of that, neither under 3 times the noise
  (`set_config(deck, 6, on)` and `set_config(deck, 7, off)`), then `SAVE_CONFIG`. It is slower than the app's wizard and has
  the same result. Then do the spinning-platter check of step 2 above, which no wizard does.

## 3. MIDI

* [ ] MIDI monitor on the "Deck" port, channel 1: fader -> CC 25 (+57) (the default `faderRole` is PITCH; CC 7 (+39) after
  `faderRole = CHANNEL_VOLUME`, CC 23 (+55) after `faderRole = CROSSFADER`), knob -> CC 24 (+56), CUE -> note 36,
  REV -> note 37, hand on the platter down/up -> note 38 (the hold pad), platter motion -> CC 22 around 64 (with the AS5600
  platter and the default `jogUntouched = AUTO`: also with no hand on it until the pad has registered its first touch since power-up,
  then **only while a hand is on the pad**; with `REPORT` (or with no pad configured) always, also while it coasts; with `SUPPRESS`
  only under a hand; the tail of a scratch can follow the touch-off note by a tick or two, because the 500 Hz emitter lags the release).
* [ ] A quick tap on the platter sends note 39 (play pulse).
* [ ] The app's built-in player: load a track, hold the platter and scratch. Direction and feel are the things to judge;
  note the platter dead zone (tick threshold) for later tuning. The player only follows the platter while the touch note is
  on, so a spin with no hand moves nothing in the player; and once the pad has worked, with the default `jogUntouched = AUTO` a
  coasting platter does not count in the Decks tab's "jog sent" either (no jog is sent for it; the drawing still turns, it follows the
  chip's angle from telemetry).
* [ ] SYNC_MASTER (hold CUE+REV 2-5 s, release): MIDI clock 0xF8 at the set tempo on the Deck port, start/stop with a
  platter tap. Back to DJ the same way.
* [ ] TRS MIDI: loop a cable from the TRS jack to a MIDI interface or the EP-133. Check the idle level first
  (WIRING.md). Bytes sent on the USB "TRS MIDI" port come out of the jack and vice versa. Log: `trs: rx overflows 0,
  tx dropped 0`.

## 4. DAC and USB audio

1. Power off, fit the DAC strap (GPIO14 to GND), power on. Log changes to:

```
main: dac: mode AUTO, strap low 16/16 samples, codec none -> present
audio: I2S BCK GPIO15 WS GPIO16 DOUT GPIO17, 48000 Hz 16-bit stereo, ring 2048 frames (42 ms), prime at 1024
usb: "EP-DJ3000 Deck A" serial ..., config 294 bytes, VID:PID 1209:0001, audio UAC1 on
main: ready: caps 0x007A ...
```

* [ ] The Mac shows an audio output named after the USB product, "EP-DJ3000 Deck A" (Sound settings: 2 channels, 48 kHz,
  volume slider); the function's own string is "EP-DJ3000 Audio". No new device, or MIDI vanished: the host rejected
  the configuration descriptor. Capture `log show` / `dmesg` / USBPcap, and see Risks.
* [ ] A loose strap: remove the jumper halfway, or touch the pin with a finger: a partial count such as `strap low 7/16 ... (UNSTABLE ...)` and
  no audio function (MIDI only) is the intended outcome. `dacMode = ON` forces the audio function, OFF removes it; both
  take effect after a reboot.
* [ ] `TEST_TONE` from the app (Test tab): a clean sine on the line out, no clicks at start/stop. Log:
  nothing; the audio task needs no prints. Scope the I2S lines if silent: BCK 1.536 MHz, LRCK 48 kHz.
* [ ] Play music to the unit's output ("EP-DJ3000 Deck A") from any app, 48 kHz. Log (each 10 s):

```
diag: audio: streaming running fill 1030 frames, usb frames 480000, over 0 under 0, reprimes 0, drift 31.2 ppm, fb 48.0015 smp/frame, slips -0 +0
```

* [ ] `fill` hovers near 1024 (half the 2048-frame ring) and stays there for minutes; `over`/`under` stay 0;
  `drift` settles to a stable value (tens of ppm); `slips` stays 0.
* [ ] `fill` runs away to 0 or 2048 and `slips` count up: the host is ignoring the feedback endpoint. That is what
  the slip compensator is for, but it should not be needed; note which host.
* [ ] Windows/Linux, if available: same checks. Linux: `aplay -l`, `speaker-test -c2 -r48000 -D hw:...`.
* [ ] Volume slider and mute on the host change the level (USB audio 1.0 feature unit: master range -80 dB..0 in 1 dB steps).
* [ ] Unplug and replug while streaming. The firmware cannot see VBUS (bus-powered PHY, no sense pin), so a pulled cable is
  noticed only as the bus going idle: `usb: bus idle (suspended, or the cable was pulled)` within a few ms, and about a
  second later the stats line says `suspended (no host)` and the engine stops sending to USB. On replug the host
  re-enumerates (`usb: mounted by host`), the audio state is cleared (`audio: idle`), and `fill` re-primes when the stream
  starts again. Expected without a reboot; not verified. A host that suspends an idle port (Windows selective suspend)
  produces the same messages and `bus active again` on resume.

## 5. Second unit and the link

Flash the second unit with `EPDJ_FIRST_BOOT_DECK_B` (`pio run -d firmware/esp32s3 -t menuconfig` -> EP-DJ3000; or hold
CUE+REV 5 s on a unit to swap A/B), no USB host needed on it beyond power.

* [ ] Both logs show `radio: ESP-NOW on channel 1` and, within about a second, in the 10 s stats:
  `link: up, peers 1, radio rx ... tx ...` on both. LED stops the slow "searching" blink.
* [ ] Both units claim the same deck: the LED alternates with red and `LINK_STATUS` reports the conflict (fix with
  `SET_DECK`).
* [ ] **One-cable mode**: unplug the data cable from deck B (power it from a charger). Deck B's log shows
  `usb: bus idle (...)`, and about a second later (the debounce) its `usb:` line says `suspended (no host)` and its link flags
  drop USB_HOST. On deck A's log `link: up, bridging`; the DJ software sees deck B's controls on the "Linked deck" port
  (cable 2) on MIDI channel 2. If B keeps power through its UART port instead, nothing changes in how it is noticed.
  If B never reports `bus idle`, the idle detection does not work without VBUS and one-cable mode needs the VBUS divider
  (below, risk 9).
* [ ] Latency: round-trip from a jog on B to a MIDI event at the host should be a few ms (docs/LINK.md target
  <= 5 ms typical). Measure it; it is a prediction.
* [ ] SYNC: deck A in SYNC_MASTER, deck B SYNC_FOLLOW: B's beat flash follows A's.
* [ ] Check `radio ... tx ... (queue full 0, failed 0)` stays at 0 under motion.
* [ ] Rig ids: `SET_CONFIG linkGroup = 1` on deck B only. Within about 1.5 s both units show `searching` and the
  `other-group` counters in their `link:` lines climb; set 1 on deck A as well and they link again, no reboot needed
  (`SAVE_CONFIG` keeps it, and after a reboot the log says `link group 1 (linkGroup config key)`). A unit built with a non-zero
  `EPDJ_LINK_GROUP` cannot be put back on group 0 over SysEx.

## 6. DJ software

* [ ] With a track playing, put a hand on the platter: the deck holds (that is the touch note, note 38, from the hold pad).
  Spin it: it scratches. Lift the hand: it plays on. If the deck never holds, the hold pad is not reporting (step 2); Mixxx then
  only ever bends the pitch with the platter. Scratching in these apps depends on the touch note, not on the spin
  ([`drivers/README.md`](../drivers/README.md)).
* [ ] Load the Mixxx mapping first (`drivers/mixxx`), then the others (`drivers/README.md`). The slide pot is the tempo
  fader by default (`faderRole` = PITCH), which works in all four apps. Switch it to `CHANNEL_VOLUME` (a scratch cut, audible in
  Mixxx only) or `CROSSFADER` (a single software mixer) with the frames in section 2b if you want that instead, and
  back to PITCH afterwards.
* [ ] Audio, whichever topology you chose (`ARCHITECTURE.md` section 1). **A**: two cables, route deck 1 to "EP-DJ3000 Deck
  A" and deck 2 to "EP-DJ3000 Deck B", each line out to a mixer channel; whether the app can drive two USB sound cards at
  once is the thing to find out. **B**: units MIDI-only, deck 1 and deck 2 to two outputs of one multi-channel interface.

## Risks, in the order I would check them

Risk 11 was added with the AS5600 decision and is the first to check; 12 and 13 came with contract v1.4. The others keep their
numbers because other documents point at them.

1. **USB audio on a real host (UAC1, asynchronous, explicit feedback).** The unit is USB Audio Class 1.0 because the
   ESP32-S3 is full-speed only and TinyUSB's UAC1 driver is the one that sends the feedback value the way a full-speed
   host expects (3 bytes, 10.14); its UAC2 driver sends 4 bytes 16.16 at any speed (ARCHITECTURE.md decision 5). The
   descriptor is byte-identical to TinyUSB's own `TUD_AUDIO10_*` macros in the layout of its full-speed speaker example,
   and `firmware/esp32s3/tools/crosscheck_usb_desc/run.sh` drives TinyUSB's real audio driver on the host with the firmware's callbacks,
   but that example, not this unit, is what its authors tested on Windows, macOS and Linux. Not checked on any host:
   how each treats the feedback endpoint (`bRefresh` 0 and `bmAttributes` 0x01, as in the example), and iPadOS at all.
   Symptoms: the device enumerates but audio is garbled, plays at the wrong speed, or the buffer fill walks away
   (`slips` count up in the `audio:` line; the software slip hides a host that ignores feedback). Where to look: the
   descriptor is `core/src/usb_desc.cpp`, the requests are `components/epdj_usb/src/usb_audio.cpp`. If a host refuses the
   feedback endpoint, the two descriptor fields to try are `bmAttributes` 0x11 (usage = feedback) and `bRefresh` 1..9
   (audio 1.0 asks for 1..9; TinyUSB's example uses 0 so the DWC2 controller serves every endpoint at 1 ms).
2. **Stream start.** TinyUSB queues the first feedback packet while it handles SET_INTERFACE, before any callback can run,
   so it carries 0 (the host driver check shows `00 00 00`). The firmware's set-interface callback then sets 48.0 and
   every later packet carries at least that, until the audio task has a measurement. Hosts ignore out-of-range
   feedback values; if one mis-sets its rate at the start of a stream, this is why.
3. **A zero-length packet on the audio endpoint.** TinyUSB's `audiod_rx_xfer_isr` fails its FIFO-write check on a
   0-byte packet *before* it re-arms the endpoint, so one ZLP from the host during streaming would stop the stream until
   the host re-selects the interface. The build compiles a patched copy of that driver file (one `if`, see
   `firmware/esp32s3/README.md`); the host check runs a ZLP through the patched driver and sees the endpoint re-armed and the
   next packet arrive (and shows the unpatched file failing). Whether any host sends one is unknown. If audio dies mid-play
   with `usb frames` frozen in the stats line anyway, look at the endpoint, not at this.
4. **ADC noise.** The S3's ADC is noisy; the fader is oversampled 8x and the engine filters it, but check that a
   still fader sends nothing (MIDI monitor: no CC 25/57 traffic at rest; CC 7/39 with the channel-volume role, CC 23/55
   with the crossfader role).
5. **Touch counts and scan period.** Everything about PLATTER.md, plus the `touch:` boot lines (step 1): the scan period decides
   how stale a pad value is, the cycle count decides how coarse it is. With the one hold pad of the default build the 0.8 ms
   target is spent on that pad alone.
6. **Flash writes versus the USB interrupt.** The USB interrupt handler runs from flash. `SAVE_CONFIG` and `SET_DECK`
   erase and write flash with the cache off for milliseconds: expect an audio glitch at that moment.
7. **Bandwidth of the engine tick.** `loop: ... over 1 ms` in the stats line. The ADC cost is a measurement at boot, not an
   assumption (`adc:` line), and the loop drops lost ticks instead of running flat out (`resyncs`).
   The ADC conversions run **with interrupts off**, up to 400 us of every millisecond, on core 1, the core that also runs
   the TinyUSB task and the USB OTG interrupt (`main.cpp` header comment). They may delay USB and I2S service; nobody has
   measured it. With the fader moving, watch `over`/`under` and `late wake-ups` in the `audio:` and `loop:` lines; if they
   climb, lower the budget (`AdcInputs::kBudgetUs`) or move the read to core 0.
8. **The 3.3 V TRS MIDI stage** (WIRING.md): measure before connecting expensive gear.
9. **No VBUS sense.** The USB PHY is configured bus-powered with no sense pin, which makes TinyUSB believe VBUS is always
   present: it never reports a detach, and a pulled cable shows up only as the bus going idle (step 5). If that proves
   unreliable on the bench, the supported fix is a 5 V -> 3.3 V resistor divider from the USB connector's VBUS to a spare GPIO,
   `self_powered = true` and `vbus_monitor_io = <that GPIO>` in `usb_device.cpp`. That needs a wiring change, so it was not made
   blind.
10. **Touch signal at the short measurement.** Auto-ranging trades counts for scan speed (risk 5 and step 1). If the hand's
    (or, on the ring, the finger's) rise is lost in the noise, the alternative is a slower scan (raise
    `EPDJ_TOUCH_SCAN_TARGET_US`) or summing a few consecutive scans in the engine; neither is built.
11. **The hold pad on a spinning platter (the AS5600 default).** The encoder supplies the angle, but touch, hold and tap come
    from one capacitive pad, and the hand is on a platter that turns, so the pad's signal has to cross a rotating joint:
    capacitive coupling to a stationary pad under the platter (option a) or a spring or brush contact on the axle (option b),
    the two ways `PLATTER.md` lists, both unvalidated and neither drawn in the SCAD. What can go wrong, from reasoning and not from
    measurement: the contact opens or chatters while the platter turns (the touch note flickers under a steady hand, taps and
    holds misfire); contact resistance and wire length cut the hand's rise below 3 times the idle noise (the Hold pad step
    fails with "rises too little"); a contact whose resistance changes with speed makes a coasting platter look touched.
    Symptoms in a DJ app: the deck does not hold when you touch it, scratch mode starts and stops by itself, Mixxx only bends
    the pitch. Look at the `touch=[...]` count in the `diag:` line while turning the platter, at note 38 in the MIDI monitor
    (step 2's spinning-platter check), and at `touchOnPermille` / `touchOffPermille`. If no contact works, `TOUCH_RING`
    (Appendix A) is the kept alternative, with its own unproven signal.
12. **The filtered ANGLE register is a bet, and the shell does not leave room for the chip.** By default the firmware writes the
    chip's CONF (`0x1C00`: 16x slow filter, fast threshold setting 7 = 10 LSB, hysteresis and watchdog off, the original Field Jog
    firmware's settings) and reads ANGLE (`0x0E`). Not established by anything read while writing this: that the filters leave
    RAW ANGLE (`0x0C`) alone, how much lag the slow filter adds (excerpts say about 2 ms), and how noisy the chip is at rest; ANGLE is
    also scaled by ZPOS/MPOS/MANG, so a chip someone burned gives fewer than 1024 ticks a lap. The A/B: build with
    `EPDJ_AS5600_FILTERED_ANGLE` on and off, and compare the idle `angle=` jitter in the `sens:` line (platter still, peak to peak
    over several seconds) and a fast scratch. The simulated answer (`firmware/core/README.md`) is that the filter wins at rest if the
    real chip is noisy and costs only lag if it is not. Separately, the 16 mm shell as drawn leaves 1.9 mm for the breakout and the air
    gap, which cannot fit: `hardware/README.md` has the arithmetic and the one-line SCAD fix, and the M3 x 12 axle is too long.
13. **A long touch followed by a hand lifted off a spinning platter.** After a touch of more than 7 s the pad's release is only
    believed once the platter has made no tick for 80 ms (so a dropout of the pad's signal in the middle of a long scratch does not
    end scratch mode). The price is by design: let go of a platter that is still turning, after such a touch, and note 38 stays on
    (and the coast goes out as scratch ticks) until the platter stops. If that is a nuisance in a real set, the constants are
    `StickyTouch::kStickyAfterMs` and `kStickyMotionMs` in `firmware/core/include/epdj/jog.h`; they are not config keys.

## Appendix A: the experimental touch ring

Only if you return to `platterSource = TOUCH_RING`: a fixed platter printed in 3 to 8 conductive-PLA sectors, scratched by
sliding a finger. Set it with the frame in section 2b, or build with `EPDJ_PLATTER_TOUCH_RING` and `EPDJ_TOUCH_PAD_COUNT` 3 to 8.
Nothing here has been done on real conductive PLA. These are the checks the earlier default used; they replace the AS5600
checks of sections 0 to 2.

* [ ] Continuity: each of GPIO4..11 open to GND and to each other (sectors isolated); adjacent platter sectors read open on a
  multimeter.
* [ ] The boot log with eight pads (illustrative numbers, nothing has run on a chip). The platter line names the ring, the
  `touch:` lines time eight pads, and `caps 0x005A` = touch ring (0x02) + TRS MIDI (0x08) + link (0x10) + LED (0x40):

  ```
  touch: scan 9760 us at 500 cycles, 2540 us at 125: target 800 us -> 34 cycles
  touch: 8 pad(s) on GPIO4..11, 34 cycles per pad, idle peak count 1452, shift 0
  touch: full scan of 8 pads takes 790 us (idle)
  main: platter: source TOUCH_RING (experimental) | AS5600 NOT answering at 0x36 (SDA GPIO21, SCL GPIO47) | touch pads wired: 8
  main: ready: caps 0x005A, serial A1xxxxxx, MIDI channel 1
  diag: sens: fader=2031 knob=1180 angle=0(none) cue=0 rev=0 touch=[1452 1456 1427 1435 1455 1446 1424 1441]
  ```

* [ ] The pads are measured one after another, so the scan time is how often a pad gets a new value: the default of 500 cycles
  would be roughly 10 ms for eight pads, i.e. each pad stale for ten 1 ms reads, which is why the firmware auto-ranges to about
  0.8 ms. `idle peak count` is in the low thousands at that setting. **Check the finger rise against the idle noise at this
  count** (`PLATTER.md` "Tuning"): the shorter measurement is the price of a fresh value on every read, and nobody has seen the
  signal-to-noise on real conductive PLA yet. If it is too noisy, raise `EPDJ_TOUCH_SCAN_TARGET_US` (every pad then repeats its
  value for a few reads, the engine's 1 kHz tick still works) rather than the cycle count (risks 5 and 10).
* [ ] Put a finger on each sector in turn: that sector's count rises, a neighbour's a little. Write down the idle noise
  (peak-to-peak) and the finger rise for each sector (`PLATTER.md` "Tuning").
* [ ] App Calibrate tab with the ring selected: *Slide pot*, *Knob*, *Touch ring* (idle noise, then a finger on each sector in
  turn, with live per-sector bars), *Platter direction* (slide a finger clockwise). The ring wizard is the one written for
  this platter and has not changed; `SAVE_CONFIG` afterwards.
* [ ] Spin a finger around clockwise: `jogTotal` increases. If not, `jogInvert = 1`.
* [ ] Without the app: the touch-ring recipe in `PROTOCOL.md` section 3.2 and the `telemetry()` snippet in section 2b
  (`touchRaw[0..7]`).

