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
| `touch_ring.h` | Segmented conductive-PLA ring: touch detection and finger angle. |
| `jog.h` | `JogTracker` (angle to 1024/rev ticks, touch edges, tap), `JogEmitter` (500 Hz relative CC). |
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
| `step(RawInputs)` | `faderRaw`/`knobRaw` 12-bit ADC (values above 4095 are clamped); `angle12` + `angleValid` from the AS5600; `touchRaw[8]` + `touchCount` (larger = more touched, `touchCount` = pads wired, 0 = no touch hardware); `cue`/`rev` true = pressed; `usbMounted`, `audioStreaming`, `dacPresent`. |
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
* **Fader role default is `CHANNEL_VOLUME`** (CC 7 / 39; contract v1.1), so two units do not fight over one
  crossfader. A unit that saved its config under v1.0 keeps the `faderRole` it stored.
* **Radio group** = the `linkGroup` config key, applied live (`Link::setGroup`): the unit leaves its old rig at
  once (peers forgotten, a held bridged note released) and announces itself in the new one without a reboot.
  The config value 0 means "use `Identity::linkGroup`", the platform's build-time default.
* **Host feedback** is accepted on the unit's own channel only (cable 0, or relayed over the radio from the
  bridging deck's cable 2). The `play` note (velocity > 0) is LED feedback in DJ and SYNC_FOLLOW and never starts or
  stops a SYNC_MASTER clock: only the `transport` CC (and the local gestures) command a master. A velocity-0 release
  changes nothing in a master, so a pulse echoed back by a mapping cannot pause it either. **Known deviation from
  `PROTOCOL.md` (E2E-3, open):** in SYNC_MASTER the note is dropped, so it does not light the play LED, which shows the
  unit's own transport (`Engine::handleHostMessage`; `docs/E2E.md`).
* **SysEx answers are bounded and counted.** A `GET_CONFIG` dump (`0xFF`) is one `CONFIG` per key followed by
  `ACK[GET_CONFIG]` as the end marker (34 frames, 506 bytes on the wire). If the output queue refuses a frame the
  dump stops there with no ACK, so a dump that ends with the ACK is complete; the loss is counted in
  `stats().outDropped` (and in `ConfigServer::repliesDropped()` for a standalone server). `ReplyQueue` is sized
  for a full dump and a `static_assert` fails the build when a contract with more keys no longer fits.
* **Motion with no listener is dropped**, not queued: nothing is sent while no host is mounted and no peer
  bridges, and a host that appears later gets the current fader/knob/tempo state, not old jog movement.
* **Calibration** (CAL_BEGIN/END) takes a 3-sample median to reject ADC spikes, so a sweep must dwell for a
  few ms at each end stop (a hand does); a pass that turns around instantly loses the last sample.
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

Engine tests (all in simulated time, no wall clock; `test_engine_sim.h` has the shared simulators: a synthetic
touch ring, a fake platform, a lossy two-unit radio, queries over the recorded output):

| File | Covers |
|---|---|
| `test_engine_core.cpp` | Ring sweep -> jog CCs on the right channel with exact conservation (both decks, both directions); AS5600 spin exactness and `jogInvert`; touch/tap notes and the play pulse; fader sweeps per role (MSB first) and the CHANNEL_VOLUME default, no chatter, 1-step latency; knob roles and the BPM-knob pickup (saved tempo survives boot, 2% threshold both ways, re-arm on role/calibration change, re-plug does not override); button notes, bounce, chords (mode cycle, deck swap); DJ mode emits no clock; Deck B on channel 2; channel change mid-hold; host feedback; USB unplug/replug; TRS pass-through. |
| `test_engine_sync.cpp` | Master clock over 10 s at 1 kHz and at 40 kHz stepping, 25 random tempo changes against the generator's schedule, scheduled tick not moved by a tempo change, `clockOutMask` port gating, the 32-bit wrap; start/pause/continue/stop/rewind byte sequences; platter tap; CUE tap tempo to 128 BPM; platter nudge (exact with AS5600, approximate with the ring); MTC quarter frames and full frames; leaving master; SYNC_FOLLOW locking to a jittered 24 PPQN input on USB and TRS. |
| `test_engine_sysex.cpp` | Input before the first step, GET_INFO, config round trip (the dump and its ACK end marker), deck identity (LOAD_DEFAULTS keeps it, SET_CONFIG of it is refused, SET_DECK is the only way), live application to fader and platter, STREAM telemetry (rate, fields, expiry, clamp), calibration (apply, no apply, failures), SET_DECK/REBOOT/LINK_STATUS/TEST_TONE/IDENTIFY, SysEx split across USB packets with real-time bytes, corrupt and hostile input, `encodeUsb` round trip, queue bounds. |
| `test_engine_link.cpp` | Two engines over a radio with 20% loss, 10% duplication and reordering: Deck B's jog, fader, knob, buttons and touch arrive on Deck A's cable 2 on channel 2 with exact jog conservation; 5 ms bridge latency on a clean simulated air (a model, never measured on a radio); feedback relayed back; master transport/tempo/beat reaching a follower; peer loss releasing held notes; route changes with USB host changes; link disable and deck-id conflict; `linkGroup` changed live over SysEx (leave, find each other again, bridge resumes) and its precedence over the build-time default; determinism. |
| `test_engine_stability.cpp` | One hour of simulated time on two linked engines (both clocks cross the 2^32 us wrap): random sensors with garbage readings, random host MIDI, random SysEx of every command, random config changes, hostile radio packets, a lossy air, stalls. Every output event well formed, work per step bounded, queue within capacity, all losses counted, engine alive afterwards. |

Run time on the host (4 cores): the five engine test binaries take about 38 s under ASan/UBSan, of which the one-hour
stability test is 31 s, and about 6 s in Release. A bare `step()` costs about 0.3 us on a desktop CPU
(Release). `engine.cpp` also compiles clean (same warning flags, `-Werror`) with the ESP-IDF Xtensa
ESP32-S3 GCC and with clang; its only external references are `memcpy`, `memset` and the 64-bit division helpers.
Nothing has been run on the chip: step time and stack use there are unmeasured.
