# EP-DJ3000 firmware core

Portable C++17 firmware logic. No ESP-IDF, no heap, no exceptions, no RTTI, no clock reads, no I/O: every
class is a state machine driven by a microsecond timestamp the caller passes in. That is what makes the whole
unit testable on a laptop, and it is why the ESP-IDF layer (`firmware/esp32s3/`) can stay thin: it moves
bytes and timestamps and does what the engine asks.

The wire contract (`protocol.h`, generated) is defined in [`docs/PROTOCOL.md`](../../docs/PROTOCOL.md); the
radio link in [`docs/LINK.md`](../../docs/LINK.md).

## Module map

| Header (`include/epdj/`) | What it is |
|---|---|
| `protocol.h` | GENERATED from `protocol/epdj3000.json`: constants, enums, config table, payload structs. Never edit. |
| `util.h` | `clamp`, 12-bit angle math, filter helpers. |
| `config.h` | `Config` (one int32 per key, validated against the contract), NVS blob form, `ConfigStore`. |
| `midi.h` | `Message`, builders, 14-bit / relative encodings, USB-MIDI event packets, streaming `Parser`. |
| `sysex.h` | 8-to-7 packing, checksum, frame encode/decode, reply builders. |
| `config_server.h` | Device side of the SysEx protocol: `ConfigServer`, `ReplySink`, `DeviceActions`, `CalibrationHost`. |
| `analog.h` | `AnalogControl` (fader, knob: filter, gate, dead zones, curve, 14-bit), `CalibrationTracker`. |
| `touch_ring.h` | Touch detection (the hold pad of the default AS5600 build is one pad fed through it) and, for the experimental segmented conductive-PLA ring, finger angle. |
| `jog.h` | `JogTracker` (angle to 1024/rev ticks, touch edges, tap; the AS5600 path is exact integer arithmetic), `StickyTouch` (the hold pad's motion-sticky release), `JogEmitter` (500 Hz relative CC). |
| `buttons.h` | `ButtonDebouncer`, `ButtonGestures` (CUE+REV chords). |
| `sync.h` | `ClockGenerator`, `TransportFsm`, `MtcGenerator`, `ClockFollower`, `TapTempo`. |
| `link.h` | ESP-NOW link: framing, peers, election, bridge, reliable messages, time sync. |
| `audio_dsp.h`, `audio_rate.h` | USB audio helpers (test tone, volume, feedback, drift). Used by the audio task, not by the engine. |
| **`engine.h`** | **Integrates all of the above into one deterministic object. Section below.** |

`src/` has one `.cpp` per header. `test/` has one `test_*.cpp` per area; `test_harness.h` is the
dependency-free harness, `vectors.generated.h` the byte-exact oracle from `protocol/test-vectors.json`.

## The engine

```
              onMidiIn / onUsbPacket / onSysexIn / onLinkPacket
  inputs  ->  step(RawInputs)                                      ->  popOut(OutEvent)   ->  USB / TRS / radio
              (sensors, 1 kHz)                                         led()  stats()
```

`Engine(Config&, EnginePlatform&, Identity)` owns every sensor pipeline, the mode logic (DJ, SYNC_MASTER,
SYNC_FOLLOW), the clock / transport / MTC, the SysEx server, calibration and the radio `Link`. Given the same
calls with the same timestamps it produces the same output, byte for byte (`engine_pair_session_is_deterministic`).
`sizeof(Engine)` is about 7.7 kB (host, 64-bit), of which 4 kB is the output queue; `OutEvent` is 256 bytes.

### Platform contract

**Threading.** Not thread-safe. **No call is ISR-safe.** One task (the engine task) makes every call,
including `popOut()`. ISRs and driver callbacks only push into their own FreeRTOS queues, stamping
`esp_timer_get_time()` at receipt; the engine task drains those queues into the engine.

**One iteration of the engine task, every 1 ms** (`vTaskDelayUntil` or an `esp_timer` wake):

1. For everything received since the last iteration, in arrival order:
   `onUsbPacket(pkt, rxUs)` (or `onMidiIn(MidiPort::Usb0/1/2, bytes, n, rxUs)`), `onMidiIn(MidiPort::Trs, ...)`
   for UART bytes, `onLinkPacket(data, len, rssi, rxUs, nowUs)` for radio packets.
2. Sample the hardware into `RawInputs` and call `step(in)`.
3. `while (engine.popOut(&ev)) route(ev);` (see below). Then update the LED from `led()`.

`step()` may be called faster than 1 kHz; it must not be called less often than about every 2 ms (the radio
link needs a poll every 2 ms at least).

**Time.** Every timestamp is a monotonic 64-bit microsecond count (`esp_timer_get_time()`). A timestamp
older than one the engine has already seen is treated as that one. The 32-bit microsecond wrap inside the
clock, MTC and link is handled (tested across it). Timestamps passed to the input calls must be taken at
receipt, not when the task gets around to them: MIDI clock estimation and radio time sync depend on it.

**Inputs.**

| Call | Notes |
|---|---|
| `step(RawInputs)` | `faderRaw`/`knobRaw` 12-bit ADC (values above 4095 are clamped); `angle12` + `angleValid` from the AS5600 (the default platter: `angleValid = false` means nobody answered, this read failed, or the platform is between two re-probes of a chip that is missing, and then `angle12` is ignored and the unit sends no jog; the first valid read after a gap longer than 3 ms re-anchors, so a chip that turns up while running, or answers again, never produces a jump); `touchRaw[8]` + `touchCount` (larger = more touched, `touchCount` = pads wired: 1 in the default build, the hold pad; 0 = no touch hardware, so no touch note ever; 3 to 8 for the experimental ring); `cue`/`rev` true = pressed; `usbMounted`, `audioStreaming`, `dacPresent`. |
| `onUsbPacket(UsbPacket, rxUs)` / `onMidiIn(port, bytes, n, rxUs)` | Cable 0: the deck (notes/CC/real-time from the host, and SysEx, assembled across packets even with real-time bytes in between). Cable 1: passed raw to the TRS jack. Cable 2: host feedback for the linked deck, relayed over the radio. TRS: parsed, passed to USB cable 1, and its real-time bytes feed the clock follower. |
| `onSysexIn(frame, n, rxUs)` | One complete F0..F7 message, for a HAL that assembles SysEx itself. `onMidiIn` on cable 0 ends up here. |
| `onLinkPacket(data, len, rssi, rxUs, nowUs)` | `rxUs` is the radio's receive timestamp (take it in the callback). A shorter overload takes just `nowUs`. |

Calls made before the first `step()` are fine: the engine initialises itself on the first call of any kind.

**Outputs: `popOut(OutEvent*)`**, oldest first, never blocks, false when empty. Each event has `kind`,
`portMask` (`proto::clockPorts` bits), `cable`, `len`, `data`.

| `kind` | `portMask` | HAL does |
|---|---|---|
| `Message` (1..3 bytes) | `kUsb` and/or `kTrs` | USB: `encodeUsb(ev, pkts, n)` then write the 4-byte packets on `ev.cable`. TRS: write the bytes to the UART. |
| `SysEx` (F0..F7) | `kUsb` and/or `kTrs` | USB: `encodeUsb` (CIN 4/5/6/7 chunks). TRS: write the bytes. |
| `Bytes` (1..3 raw bytes) | `kTrs` | Write the bytes to the UART (USB cable 1 -> TRS pass-through). |
| `Link` (<= 250 bytes) | `kLink` | Broadcast it (ESP-NOW). A refused send is just packet loss. |

If the USB endpoint is full, keep one event in hand and stop popping; the queue (4 kB) absorbs a few ms.
When it is full the newest event is dropped and counted in `stats().outDropped`. The HAL must keep draining
even with no host attached; the engine already omits USB events while `usbMounted` is false.

`led()` is a semantic snapshot (deck, mode, link state, beat flash, identify blink, cue lit, touched,
transport, chord progress 0/1/2, brightness). Colours and animation belong to the HAL.

**`EnginePlatform`** (called from inside the engine's own calls, on the engine task):
`reboot(mode)`, `setDeck(deck)` (the config already holds the new deck: persist and restart), `testTone(on,
freq, atten)` (the engine refuses without `dacPresent` and calls `testTone(false,0,0)` itself after 10 s),
`saveConfig(cfg)`.

**Identity** (fixed per boot): `hwRev`, `serial`, `caps` (hardware capability bits; the engine adds `kDac`
while a DAC is present), station `mac`, random per-boot `epoch`, `linkGroup` (the build-time rig id: it applies
only while the `linkGroup` config key is 0, a non-zero config value wins and is applied live, see
[`docs/LINK.md`](../../docs/LINK.md) section 11). The platform loads the `Config` from NVS (`loadConfig`)
before constructing the engine.

`setCaps(caps)` replaces `Identity::caps` while running, for a capability that appears after boot: the platform calls it when an
AS5600 that did not answer at power-up is found by its re-probe, so `Info.caps` stops saying the chip is absent (the Mac app's
encoder check reads that bit and would otherwise go on calling a fixed wire a wiring fault).

**What stays outside the engine:** USB descriptors and enumeration, the audio path (I2S, USB Audio 1.0 feedback, tone
synthesis, volume), NVS, Wi-Fi/ESP-NOW setup and channel, the LED driver, sensor sampling and pad
normalisation, ADC and I2C access.

### Behaviour worth knowing

* **Output order within a step:** config changes, buttons, platter (touch notes, jog, tap), jog CC, fader,
  knob, tempo announce, clock/MTC, radio. Controls leave in the step that saw them (fader within 2 ms:
  the same step).
* **MIDI clock placement** is quantised to the call interval: a tick leaves in the first `step()` at or after
  its ideal time (so at 1 kHz it is 0..1 ms late, never early by more than the generator's 1 us integer
  rounding), and the schedule itself is the exact `ClockGenerator` one, so nothing accumulates. At a 25 us
  call interval the error is under 25 us. After a stall longer than a beat the generator emits at most one
  beat of clocks (24) in a single step and re-anchors.
* **Controls go to USB cable 0 and, when a peer bridges for us, over the radio.** Clock, transport and MTC go
  only to `clockOutMask` ports (USB cable 0, TRS), only in SYNC_MASTER. The LINK bit does not send 0xF8 over
  the radio (the link refuses system messages); it sends reliable `TRANSPORT` packets (state, tempo,
  position, send time) on every state change, tempo change (at most 20 Hz), mode entry and every 10 s.
* **SYNC_FOLLOW** mirrors; it never outputs clock. Tempo and transport come from external MIDI clock / Start /
  Stop / Continue / SPP (USB cable 0 or TRS; locks after 24 ticks, drops after 500 ms) or, with no clock,
  from the partner's `TRANSPORT`. The beat LED follows the clocks or the `TRANSPORT` phase (time-synced).
* **Gestures:** single presses are never delayed, so a CUE+REV chord started in SYNC_MASTER also taps a tempo
  and rewinds the transport on the way. Platter tap in SYNC_MASTER toggles play/pause and still sends the
  `play` pulse (PROTOCOL 2.3); in DJ / SYNC_FOLLOW it only sends the pulse. Mode cycling is live and is
  not saved (use `SAVE_CONFIG`); the deck swap calls `EnginePlatform::setDeck`.
* **Live config:** `SET_CONFIG` re-parameterises only what changed and keeps filter state. A fader role
  change re-sends the position on the new CC pair at once. A platter-source change closes a held touch note.
  The MIDI channel of every held note is remembered, so changing it cannot strand a note.
* **Deck identity is fixed at boot.** `deckId` changes only through `SET_DECK` (or the CUE+REV deck swap),
  which store the new deck and restart the unit; `SET_CONFIG` of it is `NAK NOT_SUPPORTED` and `LOAD_DEFAULTS`
  keeps it, so a unit never stops answering to its deck by accident. In the short window between `SET_DECK` and
  the restart `ConfigServer` (which addresses by the config's `deckId`) already answers to the new deck, while the
  engine still acts as the deck it booted as: the MIDI channel in "auto", the LED deck and the radio deck id do
  not move. A fixed `midiChannel` still applies live.
* **Knob as tempo (`knobRole = BPM`) has pickup.** The knob does not touch the tempo at boot: it takes over only
  once it has moved more than ~2% of its travel (328 of 16383 units of the calibrated output) from where it was
  at boot, then sets `60.0 + 120.0 x position` as before. The same pickup restarts when `knobRole`, the knob
  calibration or `knobInvert` changes, so switching the role live, or calibrating, never makes the tempo jump.
  A knob nobody turned never replaces a tempo that the host, a tap or the platter set since (a USB re-plug that
  re-sends controls does not count as a turn).
* **Fader role default is `PITCH`** (CC 25 / 57; contract v1.2): the CDJ-style tempo slider, which every DJ app acts
  on even behind an external mixer, and two units never fight over one control. `CHANNEL_VOLUME` (CC 7 / 39, a
  per-deck scratch cut) and `CROSSFADER` (CC 23 / 55) stay selectable. A unit that saved its config under an earlier
  contract keeps the `faderRole` it stored.
* **Radio group** = the `linkGroup` config key, applied live (`Link::setGroup`): the unit leaves its old rig at
  once (peers forgotten, a held bridged note released) and announces itself in the new one without a reboot.
  The config value 0 means "use `Identity::linkGroup`", the platform's build-time default.
* **Host feedback** is accepted on the unit's own channel only (cable 0, or relayed over the radio from the
  bridging deck's cable 2). The `play` note (velocity > 0) is feedback for the LED in every mode and never starts or
  stops a SYNC_MASTER clock: only the `transport` CC (and the local gestures) command a master. A velocity-0 release
  changes nothing in a master, so a pulse echoed back by a mapping cannot pause it either.
* **Play LED rule** (`LedStatus::transport`). The engine remembers the host's last play note in every mode.
  DJ and SYNC_FOLLOW show the mirrored transport, which that note feeds (together with the `transport` CC, an external
  clock and the linked master). SYNC_MASTER shows its own transport, and `Playing` as well while the host says its deck
  plays, so the indicator is lit while either plays; the host's "not playing" never dims a running master, and the
  note never reaches the transport, the clock or MTC. A mapping that echoes the unit's own tap pulse back therefore
  makes a paused master's indicator flash for the length of the pulse. Mode changes re-derive the mirrored state from
  the clock follower, so feedback given before a switch to DJ or SYNC_FOLLOW is not carried over (a switch to
  SYNC_MASTER keeps it).
* **SysEx answers are bounded and counted.** A `GET_CONFIG` dump (`0xFF`) is one `CONFIG` per key followed by
  `ACK[GET_CONFIG]` as the end marker (35 frames, 521 bytes on the wire with the 34 keys of v1.4). If the output queue refuses a frame the
  dump stops there with no ACK, so a dump that ends with the ACK is complete; the loss is counted in
  `stats().outDropped` (and in `ConfigServer::repliesDropped()` for a standalone server). `ReplyQueue` is sized
  for a full dump and a `static_assert` fails the build when a contract with more keys no longer fits.
* **`jogUntouched` (contract v1.4) decides whether platter motion with no hand on it is sent as jog.** It is applied where ticks
  enter the `JogEmitter`, not in the `JogTracker`: `jogTotal` (telemetry, the direction wizard) always counts all motion, and a
  tick the policy refuses is discarded, not queued, so nothing bursts out at the next touch-down. AUTO (the default) refuses
  untouched motion when `platterSource` is AS5600, at least one hold pad is configured (`RawInputs.touchCount > 0`) **and the pad
  has registered a touch** since power-up (`padProven_`, set at the first touch-down, cleared when `platterSource` changes, never
  stored), because a free-spinning platter coasts for seconds after the hand leaves and would bend the pitch the whole time (the
  original Field Jog firmware also sends jog only while touched). Until that first touch, and with no pad at all, AUTO reports, so the
  encoder works before the pad does: `touchCount` is a build setting (`EPDJ_TOUCH_PAD_COUNT`), it says a pad is configured, not that
  anything is connected or works, and a phantom touch from a floating pad counts as a proof. REPORT always sends, SUPPRESS never
  sends untouched motion (with no pad that is no jog at all). The ring only ever moves while touched, so the
  policy cannot matter there. A tick made in the sample that sees the touch-up still counts as touched, but leaves after the
  touch-off note (the emitter is paced at 2 ms): a tick or two can reach the host just after the release. Changed live over SysEx it
  takes effect at the next sample, with nothing queued from before. A config saved before v1.4 has no such key and loads as AUTO.
* **Motion-sticky touch release (the hold pad; AS5600 and hold-only sources).** Once a touch has lasted longer than
  `StickyTouch::kStickyAfterMs` = 7000 ms, a release reported by the pad is only believed after the platter has made no jog tick for
  `kStickyMotionMs` = 80 ms. Shorter touches release at once; a platter that is still releases at once whatever the length. This
  keeps a dropout of the pad's signal in the middle of a long scratch from sending a touch-off and a touch-on (the DJ app would leave
  and re-enter scratch mode). The price, by design: a hand lifted off a platter that is still **spinning**, after a touch of more than 7 s,
  stays "touched" until the platter stops (seconds of coast on a good bearing), and the coast ticks are sent as touched ticks. The
  hold-only source produces no ticks, so for it the rule changes nothing. The idea is the original firmware's; the implementation and
  the numbers' tests are ours (`test_jog.cpp` for the state machine, `test_engine_jog_policy.cpp` through the Engine).
* **Motion with no listener is dropped**, not queued: nothing is sent while no host is mounted and no peer
  bridges, and a host that appears later gets the current fader/knob/tempo state, not old jog movement.
* **Calibration** (CAL_BEGIN/END) takes a 3-sample median to reject ADC spikes, so a sweep must dwell for a
  few ms at each end stop (a hand does); a pass that turns around instantly loses the last sample. `CAL_END` with
  `apply = 1` writes the measured extremes moved in by 1 % of the span and reports those values; with `apply = 0` it
  reports the measured extremes themselves and changes nothing (`CalibrationTracker::Result` carries both).
* A SysEx answer is queued even when `usbMounted` is false; only a host can have asked.

## Tests

```sh
./run_tests.sh              # configure, build, run everything (Debug)
./run_tests.sh --sanitize   # the same under AddressSanitizer + UBSan (builds in build-san; what a CI job should run: none is configured yet)
# or by hand:
cmake -S . -B build -DEPDJ_SANITIZE=ON && cmake --build build -j2 && ctest --test-dir build --output-on-failure
```

The library builds with `-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Werror -fno-exceptions
-fno-rtti`. `nm` shows no reference to `operator new`, `malloc`, `printf` or `__cxa_*` anywhere in the
library.

Engine tests (all in simulated time, no wall clock; `test_engine_sim.h` has the shared simulators: a `Deck` that is
the unit the firmware ships by default (contract defaults, platterSource AS5600, the encoder and ONE hold pad), a
synthetic touch ring that a test asks for explicitly with `Deck::useRing()`, a fake platform, a lossy two-unit radio,
queries over the recorded output):

| File | Covers |
|---|---|
| `test_engine_as5600.cpp` | The default build through the Engine, on a Deck with nothing configured: one lap is exactly 1024 ticks both ways on both decks (untouched spins say `jogUntouched = REPORT` first); 4 counts = 1 tick across the 4095 -> 0 wrap in both directions with `jogInvert` mirroring every case, the sub-tick carry between steps and directions, a quarter turn in one step still motion and more a glitch; 200 laps out and back with no drift; rest noise never drifts (uniform and, with a finger down, **filtered-chip Gaussian noise**: the measurement below); slow motion is passed exactly where a 3-count-per-sample threshold would drop it; `angleValid = false` gives no ticks, no garbage and a cleared `ANGLE_VALID`, the first good read re-anchors, up to 3 ms of dropout loses nothing and a longer one loses only what could not be seen; **an encoder that turns up 2.5 s after boot is picked up with no jump**; a unit whose encoder never answers is silent on jog while the hold pad, buttons and fader work (and does not move the tempo in SYNC_MASTER); one hold pad gives the touch notes and taps while the encoder gives the motion; an untouched spin never makes a touch note; no hold pad (`touchCount = 0`) means jog but no touch note; a live switch to the ring does not strand the touch note; `LOAD_DEFAULTS` puts a unit back on the encoder. |
| `test_engine_jog_policy.cpp` | **`jogUntouched`**: the default is AUTO; AUTO reports an untouched spin until the hold pad has registered one touch and then refuses the coast (`padProven_`), and a change of `platterSource` starts that proof over; AUTO on a proven pad sends exactly the ticks counted while touched (the ledger is kept from the LED's own touched flag, independent of the emission code) and nothing of the coast; AUTO with no pad, and REPORT, send everything; SUPPRESS never sends untouched motion, pad or not; telemetry's `jogTotal` is identical under all six policy/pad combinations; a 3000-tick suppressed coast leaves no backlog to burst at the next touch-down; live SET_CONFIG changes in every direction (mid-touch, mid-coast) lose and duplicate nothing; the ring ignores the policy. **The sticky release**: a 8 s scratch rides out a 40 ms pad dropout with one touch-on and one touch-off (a 3 s one does not: two pairs); the 7 s window opens between 6.8 and 7.2 s; a long touch released over a still platter lets go as fast as a short one; released over a spinning platter it holds until the platter has been still 80 ms (and the coast ticks go out as touched ticks); hold-only is unaffected; a live platter change during a sticky hold closes the touch and starts no second one. |
| `test_engine_core.cpp` | Ring sweep -> jog CCs on the right channel with exact conservation (both decks, both directions; the ring selected explicitly); AS5600 spin exactness and `jogInvert`; touch/tap notes and the play pulse (default platter and ring); fader sweeps per role (MSB first) and the PITCH default, no chatter, 1-step latency; knob roles and the BPM-knob pickup (saved tempo survives boot, 2% threshold both ways, re-arm on role/calibration change, re-plug does not override); button notes, bounce, chords (mode cycle, deck swap); DJ mode emits no clock; Deck B on channel 2; channel change mid-hold; host feedback; USB unplug/replug; TRS pass-through. |
| `test_engine_sync.cpp` | Master clock over 10 s at 1 kHz and at 40 kHz stepping, 25 random tempo changes against the generator's schedule, scheduled tick not moved by a tempo change, `clockOutMask` port gating, the 32-bit wrap; start/pause/continue/stop/rewind byte sequences; platter tap (hold pad and ring); the host's play note in every mode (LED only: no transport, clock or MTC output); CUE tap tempo to 128 BPM; platter nudge (exact with the AS5600 default, approximate with the ring); MTC quarter frames and full frames; leaving master; SYNC_FOLLOW locking to a jittered 24 PPQN input on USB and TRS. |
| `test_engine_sysex.cpp` | Input before the first step, GET_INFO, config round trip (the dump and its ACK end marker), deck identity (LOAD_DEFAULTS keeps it, SET_CONFIG of it is refused, SET_DECK is the only way), live application to fader and platter, STREAM telemetry (rate, fields, expiry, clamp), calibration (apply reports the inset range it wrote, no apply reports the measured extremes, failures), SET_DECK/REBOOT/LINK_STATUS/TEST_TONE/IDENTIFY, SysEx split across USB packets with real-time bytes, corrupt and hostile input, `encodeUsb` round trip, queue bounds. |
| `test_engine_link.cpp` | Two engines over a radio with 20% loss, 10% duplication and reordering: Deck B's jog, fader, knob, buttons and touch arrive on Deck A's cable 2 on channel 2 with exact jog conservation; 5 ms bridge latency on a clean simulated air (a model, never measured on a radio); feedback relayed back; master transport/tempo/beat reaching a follower; peer loss releasing held notes; route changes with USB host changes; link disable and deck-id conflict; `linkGroup` changed live over SysEx (leave, find each other again, bridge resumes) and its precedence over the build-time default; determinism. |
| `test_engine_stability.cpp` | One hour of simulated time on two linked engines (both clocks cross the 2^32 us wrap): random sensors with garbage readings, random host MIDI, random SysEx of every command, random config changes (the units boot on the AS5600 default; the chaos must also spend over a minute on each platter source), hostile radio packets, a lossy air, stalls. Every output event well formed, work per step bounded, queue within capacity, all losses counted, engine alive afterwards. |

### Mutation check (contract v1.4 work)

**Scope.** This run is of the first version of the policy, where AUTO refused untouched motion whenever a hold pad was configured.
It predates the pad-proof rule (`padProven_`) that replaced it and is not repeated here; the mutants named below that ignore or
always apply the pad condition therefore describe the old rule. The end-to-end suites pin the new rule with their own mutants
(`docs/E2E.md`).

Thirty-one deliberately broken copies of the sources were built in a scratch directory (nothing in the repository was touched) and
run against `test_jog`, `test_engine_jog_policy`, `test_engine_as5600` and the host checks of the ESP32-S3 layer's pure headers.
**30 were caught, 1 survived, and the survivor is an equivalent mutant.**

| Area | Mutants (each caught by at least one test) |
|---|---|
| Sticky release (13) | no stickiness at all; the long-touch boundary `>` as `>=`; the motion window `<` as `<=`; `kStickyAfterMs` 6000 and 8000; `kStickyMotionMs` 60 and 100; motion never noted (in the class, and in the engine); a new touch keeping the old start time; the engine not feeding the filter; no reset on a live platter change; the filter applied to the ring as well |
| `jogUntouched` (6 of 7) | AUTO ignoring the hold pad (always suppress); AUTO always reporting; REPORT behaving as SUPPRESS and the reverse; the touch-up step's ticks refused; the policy applied before the tracker, so refused motion stops being counted in `jogTotal` |
| CONF word and register choice (6) | the fast-filter code 7 changed; the slow-filter, fast-filter and watchdog bit positions moved; the read-back mask widened to the unused bits; filtered and raw swapped |
| Re-probe policy and budget (5) | interval 500 ms as 250 ms; one failed read enough to lose the chip; a failed trial not restarting the wait; a chip absent at boot asked at once; no budget reserve for an encoder that may turn up |

**The survivor** makes AUTO suppress untouched motion on the touch ring too (`!(touchCount > 0)` instead of the AS5600 test). It
cannot be told apart by any test because it changes nothing: the ring only ever produces ticks while touched, and a tick in
the touch-up sample is sent whatever the policy says, so the policy is never consulted for a ring tick. The AS5600 condition is kept
because it is what the contract says (at the time of this run: "suppress iff `platterSource` is AS5600 and at least one hold pad is
wired"; the pad-proof rule adds "and the pad has registered a touch").
Two mutants first failed to build (an unused variable under `-Werror`) and were rewritten to compile; the runner is not in the
repository.

### Why the jog path has no per-sample threshold (measured in simulation)

The original Field Jog firmware drops any per-sample change smaller than 3 encoder counts while touched, because after it made
its touch sensor more sensitive, tiny vibrations on touch-down produced false jog. We do not copy that, for two reasons that the
engine's own tests measure (`test_engine_as5600.cpp`, printed when the binary runs):

1. **A threshold throws away slow motion.** Applied to our 1 kHz samples, a 3-count rule delivers nothing below about 0.5 rev/s and
   next to nothing up to 0.73 rev/s (3 counts per millisecond); the engine, which keeps every count and carries the remainder
   (4 counts = 1 tick), delivers all of it. From the same angle samples, 10 s each, finger down:

   | platter speed | engine ticks / true ticks | what a 3-count-per-sample rule would deliver |
   |---|---|---|
   | 0.05 rev/s | 511 / 512 | 0 % |
   | 0.1 rev/s | 1023 / 1024 | 0 % |
   | 0.2 rev/s | 2047 / 2048 | 0 % |
   | 0.5 rev/s | 5119 / 5120 | 7 % |
   | 1 rev/s | 10239 / 10240 | 100 % |
   | 2 rev/s | 20479 / 20480 | 100 % |

   (The brief this was written from puts the original's cut-off at about 0.2 rev/s, which would mean a loop of roughly 270 Hz; we
   do not know its rate. The point is the same at any rate: the rule is a dead band in motion, not in noise.)

2. **A touched but still platter is already quiet without it.** The tracker's total is (position - first position) / 4, truncated,
   so noise cannot accumulate; it can only flicker the total between neighbouring ticks when the platter rests next to a tick boundary
   and the noise crosses it. With a finger on the pad (so AUTO sends everything), a still platter for 8 resting positions x 15 s per
   row, noise modelled as white chip noise of `sigma` counts rms, read through the AS5600's slow filter (a 1-in-16 average at the
   chip's roughly 7 conversions per millisecond, the ANGLE register) or raw (the RAW ANGLE register):

   | raw chip noise | filtered ANGLE: worst net ticks / flicker events per min / jog CCs in 2 min | raw register: same |
   |---|---|---|
   | 0.5 counts rms | 0 / 0 / 0 | 0 / 0 / 0 |
   | 1 | 0 / 0 / 0 | 1 / 80 / 160 |
   | 2 | 0 / 0 / 0 | 2 / 8857 / 16162 |
   | 3 | 0 / 0 / 0 | 3 / 21923 / 34689 |
   | 4 | 1 / 1 / 2 | 4 / 30261 / 43530 |

   So with the filter the guarantee asked for holds across the whole range modelled: no net motion and at most single-tick flicker,
   silent up to 3 counts rms. The raw register holds it only to about 1 count rms; beyond that it flickers by several ticks, over a
   hundred times a second. **This is a model, not a measurement**: neither the chip's real rest noise nor its internal conversion rate
   was measured, and the model assumes the filter acts on the ANGLE register and not on RAW ANGLE, which no source we could reach
   states (`hardware/WIRING.md`). It does say what to look for on the bench: the idle `angle=` in the once-a-second `sens:` line, with
   `EPDJ_AS5600_FILTERED_ANGLE` on and off. If the real chip is quiet in raw mode too, the filter is lag with no benefit.

Run time on the host (4 cores): the seven engine test binaries take about a minute under ASan/UBSan, of which the one-hour
stability test is about 50 s and `test_engine_as5600` (which now includes the rest-noise measurement) about 10 s; the whole
suite is 29 binaries. A bare `step()` costs about 0.3 us on a desktop CPU
(Release). `engine.cpp` also compiles clean (same warning flags, `-Werror`) with the ESP-IDF Xtensa
ESP32-S3 GCC and with clang; its only external references are `memcpy`, `memset` and the 64-bit division helpers.
Nothing has been run on the chip: step time and stack use there are unmeasured.
