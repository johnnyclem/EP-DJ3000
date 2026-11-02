# End-to-end conformance (firmware x Swift x Mixxx)

Every other test in the repo checks one implementation against the spec or against a fake written from the spec: the
Swift tests use a fake device, the driver tests use a mock engine, the firmware tests use the firmware. Nothing proved
that the three agree with each other. This harness does: the **real firmware `Engine`** runs behind a text pipe, fed
by simulated hardware, and the **real Swift client libraries** and the **real Mixxx script** talk to it.

```
                       simulated hands                                    your code under test
  hold pad (1 touch pad)─┐                                    ┌─────────────────────────────────────────────┐
  fader / knob ADC      ─┤  sensor model   ┌────────────────┐ │ Swift (macos/Tests/EPDJ3000E2ETests)        │
  AS5600 angle          ─┼──────────────▶  │ epdj_engine_sim│◀┤  DeviceClient, ControllerState, wizards,    │
  CUE / REV buttons     ─┘  (1 kHz,        │  1 or 2 real   │ │  DeckModel + ScratchPlayerCore, UsbMidi     │
                          virtual time)  │  Engine objects│    ├─────────────────────────────────────────────┤
  host MIDI / SysEx ───────────────────▶ │  + lossy radio │ ◀──┤ Node (drivers/test/e2e)                     │
  (USB cables 0/1/2, TRS)                └────────────────┘    │  drivers/mixxx/EP-DJ3000-scripts.js + XML   │
                                      stdin: text commands     │  on a strict mock Mixxx engine              │
                                      stdout: JSON events      └─────────────────────────────────────────────┘
```

## Running it

```sh
tools/e2e.sh            # build the simulator, run the Node and the Swift suites
tools/e2e.sh node       # one suite
tools/e2e.sh swift
tools/e2e.sh build      # only build the simulator
```

About 25 s on a 4-core laptop (33 Node tests, 69 Swift tests). Environment: `EPDJ_E2E_BUILD` (build directory, default `firmware/core/build-e2e`, git-ignored),
`EPDJ_E2E_SANITIZE=ON|OFF` (default ON: the simulator is built with ASan + UBSan, and a sanitizer report fails the test
that provoked it), `EPDJ_SWIFT_SCRATCH` (`--scratch-path` for SwiftPM). Needs cmake and a C++17 compiler, Node 18+
and a Swift toolchain (5.9 is the manifest's tools version; it was run with 6.0.3); a missing Node or Swift is printed as SKIP, never counted as a pass.

Without the simulator binary both suites **skip** with a message (they do not fail), so a plain `swift test` or
`node drivers/test/run-all.mjs` on a machine that never built it stays green. The binary is found through
`EPDJ_ENGINE_SIM`, then `firmware/core/build-e2e/epdj_engine_sim`.

`-DEPDJ_BUILD_TOOLS=ON` adds the `epdj_engine_sim` target to `firmware/core/CMakeLists.txt` (default OFF; the unit-test
globbing is untouched). The simulator shares its sensor and radio models (`Rng`, `RingSim`, `Air`) with the engine
unit tests through `firmware/core/test/sim_models.h`.

## The simulator (`firmware/core/tools/engine_sim.cpp`)

One or two `Engine` instances ("units"), stepped at 1 kHz of **virtual** time in the same order as the platform loop
(`firmware/esp32s3/main/engine_task.cpp`): host input, due radio packets, sensors, `step()`, drain `popOut()`. Nothing
reads a clock; time moves only on `run` and `ramp`, so a run is deterministic and a 60 s session takes ~1 s.

* **Sensor model**: the default unit is the shipping build (contract v1.3): the platter is an **AS5600** (an angle in
  revolutions, unbounded, wrapped to 12 bits like the chip; optional noise in counts; it can be made to fail its reads or
  never to answer) and the **one touch pad** (GPIO4, the hold pad) reads a finger with `pressure` 0..1 (the engine tests'
  model: a 40 per mille peak, +-3 per mille uniform noise). The **experimental touch ring** is `init platter=ring`: eight
  pads, a finger at an angle in revolutions with a 1.5-sector footprint. Both platters have fader and knob ADC with +-2
  counts of noise, CUE and REV buttons, USB mount, DAC.
* **Platform**: persistence (`stored` = what NVS holds: written by SAVE_CONFIG and SET_DECK, read at boot), restart after
  `SET_DECK` / `REBOOT` / the CUE+REV 5 s swap with a new radio epoch and a 300 ms USB re-enumeration, injectable
  flash failure.
* **Radio** (two units): the `Air` model with loss, duplication, delay and jitter (reordering), per-unit clock offset.
* **Output**: every `OutEvent` with its port mask, cable, bytes and virtual time, plus the 4-byte USB-MIDI packets that
  the firmware's `encodeUsb` produced, so USB framing is exercised in both directions (Swift `UsbMidi.encode` into
  `Engine::onUsbPacket`, `encodeUsb` out through Swift `UsbMidi.decode`).

Protocol: one command per line on stdin, `verb key=value ...`. Every command is answered by event lines and exactly one
terminator (`{"e":"done",...}` or `{"e":"error",...}`).

| Command | Effect |
|---|---|
| `init units= deck0= deck1= usb0= usb1= offset1= seed= loss= dup= delay= jitter= group= platter= as5600= touch= dac=` | (re)create the rig; also done at start with defaults. `platter=as5600\|ring\|hold` picks the hardware build: `as5600` (the default) is the AS5600 plus one hold pad, `ring` is the experimental touch ring (eight pads, no encoder, and `platterSource` is stored as TOUCH_RING), `hold` is one pad and no encoder. `as5600=0\|1` and `touch=0..8` override a piece of it (`as5600=0`: no chip ever answers, so `angleValid` stays false and `Info.caps` has no AS5600; `touch=0`: no hold pad). Without `platter=` the unit boots on the contract defaults, which is what a factory unit does |
| `run ms=N` | advance N milliseconds |
| `ramp what=fader\|knob\|finger\|pressure\|as5600 from= to= ms= [u=]` | move one control linearly while time runs |
| `finger rev= pressure=` / `fader raw= noise=` / `knob raw= noise=` / `buttons cue= rev=` / `as5600 rev= noise= valid=` / `ring peak= width= noise=` | set the sensors. `finger pressure=` is the hold pad (with one pad `rev` means nothing); `as5600 valid=0` makes the chip's reads fail while the platter keeps turning |
| `midi port=0\|1\|2\|trs hex=` | host bytes (split into messages, framed as USB-MIDI by the firmware's `midi::encodePacket/encodeSysex`) |
| `usbpkt hex=` | host USB-MIDI event packets, as the Swift `UsbMidi.encode` makes them |
| `usb mounted=` / `peripherals dac= audio=` / `platform saveok= toneok=` / `air loss= dup= partition= delay= jitter=` / `reboot` / `trace link=` | environment |
| `status` | state only. Every per-unit command ends with the unit's state in its `done` line: `jogTotal`, `transport`, `tempoX10`, `led{...}` (the `LedStatus` the status LED would show), `cfg[]` (live), `stored[]` (persisted), link state, queue statistics |

`out` events: `{"e":"out","t":<virtual us>,"u":<unit>,"k":"msg|sysex|bytes|link","ports":<clockPorts mask>,"cable":<n>,"hex":"..","usb":".."}`.

## What the suites check

**Swift** (`macos/Tests/EPDJ3000E2ETests`, XCTest; the real `DeviceClient` runs on the simulator's virtual clock, so
request timeouts and the 2 s stream keep-alive happen in virtual time):

* `DeviceClient`: `getInfo` (a factory unit says `platterSource == AS5600` and `Caps` has AS5600; a ring build says
  TOUCH_RING and not AS5600; `LOAD_DEFAULTS` returns a unit to the encoder); broadcast vs deck-addressed requests and the virtual timeout; `dumpConfig` (all 33 keys,
  closed by `ACK[GET_CONFIG]`) against the contract's defaults; set/get; every key's range ends accepted and one step
  beyond rejected with `OUT_OF_RANGE`; `SET_CONFIG deckId` -> `NOT_SUPPORTED`; `SAVE_CONFIG` persists (and `FLASH_ERROR`
  surfaces), `LOAD_DEFAULTS` keeps the deck, a restart brings back what was saved; `SET_DECK` -> restart -> the client
  follows the new id; `IDENTIFY`, `TEST_TONE`, `REBOOT`.
* Telemetry: field offsets and signs against the simulated sensors (idle hold pad reading in `touchRaw[0]`, the seven
  unwired pads 0, `angle` the encoder's 12-bit reading), `jogTotal` exact (-1536 for 1.5 counter-clockwise revolutions),
  seq continuity, the 100 Hz clamp, the keep-alive across 12 virtual seconds, the device's own 5 s timeout.
* Calibration: device `CAL_BEGIN/END` for the fader (full travel then reaches 0 and 16383), `apply = 0` reporting the
  measured extremes and writing nothing against `apply = 1` reporting what it wrote (the 1 % inset), `CAL_FAILED` /
  `CAL_NOT_ACTIVE`, the host-side `SweepCalibration` against the device's answer and its reversed-pot detection, the
  **touch-ring wizard driven by simulated fingers** (`platter=ring`; the thresholds it produces are then used: 16 finger
  positions, on and between sectors, are detected within 5 ms and released within 4 ms, and 6 s of idle noise produces no
  touch), a too-light finger, and the **platter-direction wizard** with a correct and a reversed platter on the AS5600
  (the default: which way it counts depends on how the magnet and breakout were mounted) and on the ring.
* `ControllerState`: a one-revolution spin of the AS5600 platter with a hand on the hold pad folds into **exactly 1024**
  ticks (both decks, both directions, equal to `jogTotal`), and the same spin with no hand on it is the same 1024 ticks of
  motion and no touch note; the ring's one-revolution sweep folds into the ticks the firmware counted (1020..1024 of 1024
  in the runs so far, exact equality with `jogTotal`); 5 revolutions, wire rules (never 0 or 64, >= 2 ms apart), fader and
  knob equal to telemetry `faderOut/knobOut`, role changes, buttons, tap pulses and the tap rules (`tapMaxMs`,
  `tapMoveTicks`, `tapPlay`, pulse >= 10 ms), debounce, the CUE+REV chord (mode cycle, deck swap), SYNC_MASTER transport /
  clock (no drift over 10 s, `clockOutMask`) / MTC / tempo, the AS5600 path (exact across the wrap and with encoder
  noise), reads that fail while the platter keeps turning (no ticks, no jump afterwards), an encoder that never answers
  (`as5600=0`: silent on jog, `ANGLE_VALID` down, `Info.caps` says so, while the hold pad, buttons and fader work), no hold
  pad at all (`touch=0`: motion but never a touch note), `TOUCH_HOLD_ONLY`, a flick faster than the wire can carry (late,
  never lost).
* `DeckModel` + `ScratchPlayerCore` (on the default AS5600 platter; relative scratching, because an encoder cannot teleport
  between gestures): tap plays and pauses and the play LED note goes back in and lights the unit; a resting finger holds
  a playing track; the **scratch law** (a revolution moves the track by 1.8 s) at four speeds;
  forwards-and-back scratching returns to where it started; CUE preview/return and the cue LED; REV; fader as volume,
  knob as trim, PITCH as tempo; in SYNC_MASTER a tap toggles once, and a host play note lights the play LED without ever starting the unit's clock.
* Radio: the one-cable bridge under 20 % loss and 5 % duplication (exact tick conservation on cable 2, channel 2 kept),
  LINK_STATUS (peer, flags, rssi, rtt, **clock offset: -123 456 us set, 7 us off after 6 s**), `linkGroup` splitting and re-joining a rig live,
  deck-id conflict, `SET_DECK` on one unit of a rig, a follower taking tempo and transport over the radio.
* Ports: TRS pass-through both ways and `trsMidiEnable`, the channel filter on every cable, `midiChannel` override,
  transport CC vs play note, host tempo / nudge / tap, hot-plug re-send, fader curves (the firmware's integer curves
  against Swift's `FaderCurve.gain`), a follower locking to a host clock.
* Every test also asserts, over everything the units sent, that **every USB packet the firmware produced decodes with
  the Swift `UsbMidi` to the same bytes and re-encodes to the same packets**, and the **wire rules** of PROTOCOL.md: a
  unit's own 14-bit pairs go MSB before LSB within a tick, jog is never 0 or 64 and at most every 2 ms per channel,
  control notes go on with velocity 127 and are never turned on or off twice in a row, and every EP-DJ3000 SysEx frame
  decodes strictly with the Swift codec (the Node suite applies the same rules to its log).

**Node** (`drivers/test/e2e/run.mjs`): the unmodified `drivers/mixxx` XML bindings and script in a `vm` context against
a strict mock Mixxx engine (unknown controls/API calls throw, values are range-checked), fed by the real firmware:

* `scratchTick` conservation over 2-revolution sweeps, both decks and directions, on the AS5600 (exactly 2048 ticks) and
  on the experimental ring (`platter=ring`, within 4 %): the sum Mixxx receives equals the wire total equals the
  firmware's `jogTotal`; `scratchEnable` once per touch with 1024 intervals; nothing leaks into pitch bend; `jogInvert`
  (exactly -1024 for a clockwise revolution).
* The default hardware: a **full one-revolution spin of the untouched platter reaches the script as exactly 1024 ticks** of
  `[ChannelN],jog` pitch bend (the mock reads and zeroes the accumulator every 2 ms like Mixxx does per audio callback); a
  **hold-pad touch switches Mixxx into scratch mode** (`scratchEnable`, ticks, `scratchDisable`) whether or not the platter
  moves; a hand that lets go of a spinning platter scratches until the release and the coasting ticks become pitch bend,
  none lost; a flick released at once leaves no `scratchTick` after `scratchDisable` (the ring, unlike the encoder, also
  leaves no pitch bend); failing reads give no ticks and no jump when they recover; an encoder that never answers gives
  Mixxx no jog at all while the pad still scratches.
* Fader: 0..1 over the whole travel, monotonic, ends exactly at the wire value / 16383, MSB before LSB in one engine
  tick, no chatter from +-3 counts of noise for 5 s, on the wire in the same tick as the ADC change; `faderRole` selects
  volume / crossfader / rate; knob -> Quick Effect; `knobRole = BPM` -> deck BPM.
* Buttons (`cue_default`, `reverseroll`, toggle mode), the closed loop **platter tap -> `play` -> LED note -> unit**, LED
  feedback bytes accepted by the engine (`cue_indicator`, `play`), and each unit ignoring the other deck's channel.
* SYNC_MASTER (tap tempo -> `tempo_tap`, platter tap -> Start + play note), the one-cable bridge under 15 % loss (Deck 2
  scratch and fader through cable 2, LED back through the radio), `SET_DECK` -> channel 2 -> `[Channel2]`.

## Findings

Things the harness found that are not in the files it owns. This table is the source of truth: update it first when one
changes. An open finding is a `knownIssue` in the suites, which prints `KNOWN ISSUE` and **fails the test as soon as the
issue stops reproducing**, so the marker is promoted to a plain assertion instead of rotting; a fixed one is a plain
assertion. Fixed findings keep one line here for history.

| Id | Where | What | Status |
|---|---|---|---|
| E2E-1 | firmware vs `PROTOCOL.md` 3 (CAL_END) | `CAL_END` with `apply = 0` returned the extremes inset by 1 % like `apply = 1`, where the spec says "the measured extremes". | **Fixed.** `apply = 0` reports the measured extremes and changes nothing; `apply = 1` reports (and writes) the 1 % inset range. Pinned by `TelemetryCalibrationE2ETests` (a knob sweep 100..3900 ends as 100..3900 without apply and 138..3862 with it) and by core tests in `test_analog`, `test_config_server` and `test_engine_sysex`. |
| E2E-2 | Swift `ControllerState` / `Cc14Assembler` | `faderRole` is only updated when a 14-bit pair's *value changes*. Switching the role away and back with the pot untouched makes the unit re-send the pair (that is why it does), the value equals what the fold already holds for that pair, nothing is reported, and `faderRole` (and `DeckModel`'s idea of what the pot is) stays on the previous role until the pot moves. | **Fixed in the Swift library.** `Cc14Assembler.observe()` also reports unchanged pairs and `ControllerState` takes the role from any arriving fader pair. Pinned by pure `ControllerState` tests, a `DeckModel` test and the Swift e2e. The old reproducer was: `cc14(.channelVolume, 16383)`, `cc14(.pitch, 16383)`, `cc14(.channelVolume, 16383)` into one `ControllerState` -> `faderRole == .pitch`. |
| E2E-3 | `PROTOCOL.md` 2.6 vs `Engine` | In SYNC_MASTER the engine dropped the host's inbound `play` note, so `LedStatus.transport` showed only the unit's own clock and a host that starts its deck could not light a master unit's play LED. | **Fixed.** The engine remembers the host's play note in every mode; SYNC_MASTER shows `Playing` on the LED while either its own transport or the host's deck plays. The note still never touches the master's transport, clock or MTC. Rule in `firmware/core/README.md` ("Play LED rule"); pinned by `DeckE2ETests` and by `test_engine_sync`. |

Observations (working as designed, but worth knowing):

* **14-bit transients.** MSB first then LSB, an unchanged half skipped (PROTOCOL.md 2.1), means a receiver that applies
  every message sees the new MSB with the old LSB for a moment: up to 127/16383 = 0.78 % of travel off for the length of
  one USB frame. The Mixxx script applies per message, so `[ChannelN],volume` glitches by that much at each MSB step
  of a fast fader move (measured: worst 127 counts in an 800 ms sweep; `ControllerState` does the same and documents
  it). The values settle exactly. 562 of 816 CC messages in that sweep were LSB-only updates.
* **`ControllerState.bpmX10` stays `nil`** until the unit announces a change. The unit announces tempo changes and on
  USB plug-in, not when it enters SYNC_MASTER or when a tap lands on the tempo it already has; a host that wants the
  value any time reads `Telemetry.bpmX10`.
* **Pause inside the first beat** is `Stop` + `SPP 0` on the wire, which a follower folds as STOPPED while the unit
  itself reports PAUSED (the documented edge case of PROTOCOL.md 2.6).
* **Scratch law accuracy** (the AS5600 platter, a hand on the hold pad). One clockwise revolution at 33 1/3 rpm moves the
  track by 1.804 s against the law's 1.800 s (+0.2 %); +0.2 %, +0.6 % and +0.8 % at 0.56, 1.1 and 2.2 rev/s, -1.6 % at
  0.28 rev/s. With the touch ring it was +2.2 .. +2.6 % at 0.56 and 1.1 rev/s: that was the ring's angle filter, which
  the encoder does not have, so the encoder needs no tuning here. Four 0.15-rev forward-and-back strokes end 98 frames
  (2 ms) of audio from the start.
* **Resting finger**: with the encoder and the hold pad a still finger is exactly 0 ticks over any time; the ring's
  noise made it net -2..+3 ticks in 10 s over six noise seeds. A finger landing on a record playing at 1x holds it
  (speed under 0.05) 12 ms after it touches down, on the hold pad.
* **A platter that keeps turning after the hand leaves** (only an encoder can do this): the touch-up note comes when the
  finger leaves the pad, whatever the platter is doing, and the ticks that follow reach Mixxx as pitch bend, not as
  `scratchTick` (apart from the few ms before the release is detected). In the Node test 512 ticks pushed by hand and 512
  coasting after the release all reach Mixxx: the wire total equals the firmware's `jogTotal`, 1024.
* **Wizard thresholds** for the simulated touch ring (`platter=ring`): rises 42..43 per mille, idle noise 6.0 per mille
  peak to peak -> `touchOnPermille 21`, `touchOffPermille 18` (the 3x-noise floor dominates, so hysteresis is 3 per
  mille). A finger that peaks at 14 per mille makes the wizard give up with `noFingerDetected(sector: 0)` after its 10 s
  timeout. The hold pad of the default build has no wizard: its thresholds are the factory 12 / 7 per mille against a
  40 per mille finger.
* **Bridge**: under 20 % loss + 5 % duplication Deck B's 1536 ticks arrive as 1536 ticks on cable 2.
* **Pacing**: 30 revolutions in 0.5 s (twice what 63 ticks per 2 ms can carry) are delivered complete, the last
  message 0.47 s after the spin ended.

## How we know the tests can fail

The harness was run against nine deliberately broken copies of the firmware (built in a scratch directory, not part of the
repo): jog sign flipped, 14-bit pair order swapped (LSB first), Deck B on channel 1, no 1 % calibration inset, SHARP_CUT
knee moved, 2 ms instead of 10 ms play pulse, the touch note replaced by the REV note, telemetry `jogTotal` negated, and
no ACK at the end of a GET_CONFIG dump. Every one failed at least one suite (jog sign, channel and touch note: both; the
inset, knee, pulse width, `jogTotal` and the missing ACK: Swift; the pair order: both, through the wire-rule check that
runs after every test). A missing reply fails quickly: when a test sits idle, the virtual request timeout fires on its own.

For the AS5600 default two more mutants were built in a scratch directory and run against the new tests: the engine
ignoring `angleValid` (caught by three core tests and by the Swift and Node "reads that fail" tests; **not** by the
"encoder never answers" tests, because the simulator, like the HAL, leaves `angle12` at 0 when nobody answers, so a
constant angle is silent either way: that is why the failing-reads test keeps the platter turning), and the angle
difference without wrap handling (caught by the wrap cases and by the one-lap tests).

## What this does NOT prove

* **Real hardware.** The sensor models are the engine tests' models. The AS5600 is ideal: the angle is exact, there is no
  chip latency (the AS5600's slow filter and its 12-bit noise are not modelled; `as5600 noise=` adds noise in counts but
  it is white), no magnet misalignment or eccentricity, no I2C failures other than the ones a test asks for. The hold pad
  is a clean 40 per mille finger with uniform noise, **and nothing in here says a conductive platter that spins can give
  such a signal**: how the hold pad is coupled to a rotating platter is undecided and unvalidated (`hardware/PLATTER.md`).
  The ring model is the same finger with no coupling between sectors beyond the footprint, no drift, no EMI. Printed
  conductive PLA will differ; the thresholds above are the first things to re-check.
* **The platform layer.** USB enumeration, TinyUSB, ESP-NOW latency and loss, I2S/audio, NVS, timing jitter of the real
  1 ms tick, the 1 kHz loop's overrun behaviour. The simulator calls the engine in exactly the order the task loop does,
  and with perfectly even 1 ms steps.
* **Real Mixxx, VirtualDJ, djay Pro, rekordbox.** The Node mock models the control names, ranges, the (status, midino)
  dispatch and control-change connections. It does not model Mixxx's scratch physics, the ramp after `scratchDisable`
  (the mock ends scratching immediately), soft takeover (recorded, not enforced), the real set of controls present in
  your Mixxx version (`tempo_tap` needs 2.5), controller thread timing, or one script instance per MIDI port when
  Mixxx opens the three USB-MIDI cables as separate devices (the tests that matter model it with one instance per cable).
  VirtualDJ, djay and rekordbox are not exercised at all.
* **CoreMIDI.** The Swift transport hands the client the bytes of USB-MIDI packets one packet at a time; CoreMIDI's
  chunking, timestamps and port naming are not involved. `CoreMidiTransport`, `DeviceManager`, the audio output and the
  SwiftUI app are macOS-only and not compiled here.
* **Audio quality.** `ScratchPlayerCore` is rendered offline in 96-frame blocks and its playhead and gain are checked;
  nobody listens to it.
* **Long-term behaviour.** The longest run is a minute of virtual time with a scratch every second; the engine's own
  `test_engine_stability` covers hours.
* **Anything the simulator was not asked to do**: a second host on the same cable, cable 2 SysEx (not bridged by design),
  USB suspend, a host that stops reading.

## Adding a test

Swift: subclass `E2ETestCase`, `try await withWorld("units=1 deck0=0") { world in ... }`, create clients with
`world.client()`, move time with `world.run(ms:)` / `world.ramp(...)` / `world.gesture(...)` (they let every client
consume what the unit sent before time moves on), read what the unit sent with `world.midiEvents(...)`, the unit's
state with `world.status()`. Node: `withWorld({...}, async ({ sim, engine, rig, script }) => ...)` in `run.mjs`; `rig.run`,
`rig.ramp`, `rig.cmd("finger rev=0.3 pressure=1")`, `engine.take()` for what the script did to the mock Mixxx.
A disagreement you find goes through `knownIssue(...)` / the `observe(...)` helpers and into the table above, with a
minimal reproducer.
