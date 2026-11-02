# EP-DJ3000 wire protocol (contract v1.3)

The contract between the **firmware**, the **macOS app** and the **DJ-software
mappings**. The machine-readable source of truth is
[`protocol/epdj3000.json`](../protocol/epdj3000.json); tables below between
`GENERATED` markers are produced from it by `python3 tools/gen_protocol.py`
(never edit them by hand). Byte-exact behavior is pinned by
[`protocol/test-vectors.json`](../protocol/test-vectors.json) (produced by the
independent reference in `tools/gen_vectors.py`): every implementation must
reproduce those vectors.

Normative words: **MUST**, **SHOULD**, **MAY** as in RFC 2119.

**Revision.** This is contract revision 1.3; the wire version byte (`Info.protocolVersion`, SysEx `ver`) is still 1, and no
message layout changed after 1.1. The revision label exists only in these documents, not in the JSON.

* **1.1** renamed `Peer.reserved` to `Peer.status` (a `PeerStatus` bit field), added the `linkGroup` config key, put the USB
  identity in the contract, and settled the `deckId`, `LOAD_DEFAULTS`, `GET_CONFIG`-dump and play-note rules below.
* **1.2** changed the default `faderRole` from `CHANNEL_VOLUME` to `PITCH` (the CDJ-style tempo slider: audible in every DJ app
  even with an external mixer).
* **1.3** changed the default `platterSource` from `TOUCH_RING` to `AS5600` (a free-spinning platter over a magnetic encoder; the
  touch ring is kept as an experimental option). A unit that saved its config under an older revision keeps its old values
  until `LOAD_DEFAULTS` + `SAVE_CONFIG`.


## 1. USB topology

The unit is a USB **composite** device (full speed, ESP32-S3 native USB):

| Function | Class | When present |
|---|---|---|
| MIDI | USB-MIDI 1.0 streaming, **3 cables** each way | always |
| Audio out | USB Audio Class 1.0, stereo, 48 kHz, 16-bit, asynchronous with a feedback endpoint (3 bytes, 10.14 samples per frame), mute and volume | only when a DAC is present (`dacMode` AUTO finds one, or ON) |

Both functions are meant to be class-compliant, i.e. to need no driver on macOS, iPadOS, Linux or Windows; **that has not
been tested on any host** (Windows would need a vendor ASIO driver for low-latency audio; its in-box drivers cover MIDI and
plain playback). Audio is UAC1 rather than UAC2 because the ESP32-S3 is a full-speed device: see
[`ARCHITECTURE.md`](ARCHITECTURE.md) decision 5.
Descriptor changes need re-enumeration, so `dacMode` takes effect after reboot.
Product string: `EP-DJ3000 Deck A` / `EP-DJ3000 Deck B` (from `deckId`); the serial
number string is derived from the chip MAC so two units never collide.

USB-MIDI cables:

<!-- BEGIN GENERATED:cables -->
| Cable | Name | Purpose |
|---|---|---|
| 0 | Deck | This unit's own controls, clock and transport. Always present. |
| 1 | TRS MIDI | Pass-through to/from the 3.5 mm TRS MIDI jack (external gear, e.g. EP-133 K.O. II). |
| 2 | Linked deck | The peer deck, bridged over the radio link when the peer has no USB host of its own. Silent otherwise. |
<!-- END GENERATED:cables -->

Jack strings SHOULD carry the cable names so hosts label the ports.

## 2. MIDI implementation

Channel: `midiChannel` (0-15), or **auto** (16) = Deck A -> channel 1, Deck B -> channel 2
(zero-based 0 and 1). All controls below use that channel. Real-time messages have no channel.

**Channel filter:** a unit MUST ignore channel-voice messages whose channel is not its own effective channel, including
messages relayed on cable 2 by the bridge. Host software may therefore send LED feedback for both decks to every port without
one deck's LEDs following the other's.

<!-- BEGIN GENERATED:midi -->
| Control | Message | Kind | Direction | Meaning |
|---|---|---|---|---|
| `jog` | CC 22 | relative (offset-64) | out | Platter motion, relative offset-64. 1024 ticks = one full revolution. |
| `crossfader` | CC 23 / 55 (MSB/LSB) | 14-bit | out | Slide pot when faderRole = CROSSFADER. 14-bit: MSB CC 23, LSB CC 55. Always sent MSB first, then LSB; an unchanged half may be skipped. |
| `channelVolume` | CC 7 / 39 (MSB/LSB) | 14-bit | out | Slide pot when faderRole = CHANNEL_VOLUME. 14-bit: MSB CC 7, LSB CC 39. |
| `pitch` | CC 25 / 57 (MSB/LSB) | 14-bit | out | Slide pot when faderRole = PITCH (tempo fader). 14-bit: MSB CC 25, LSB CC 57. |
| `knob` | CC 24 / 56 (MSB/LSB) | 14-bit | out | Orange knob. 14-bit: MSB CC 24, LSB CC 56. |
| `bpm` | CC 20 / 52 (MSB/LSB) | 14-bit | both | Tempo, value = BPM x 10 (14-bit, 20.0..300.0 valid). Host->device sets tempo; device->host reports tempo changes in SYNC_MASTER mode. |
| `bpmNudge` | CC 21 | relative (offset-64) | both | Tempo change, relative offset-64, one step = 0.1 BPM. |
| `transport` | CC 29 | 7-bit | both | Absolute transport state: 0 = stop, 1 = play, 2 = pause. Unambiguous alternative to toggle notes. |
| `cue` | Note 36 | note | both | CUE button. Out: vel 127 down / 0 up. In: vel > 0 lights the button (deck cue state). |
| `rev` | Note 37 | note | both | REV button. Out: vel 127 down / 0 up. |
| `touch` | Note 38 | note | out | Platter touched (vel 127) / released (vel 0). Enter/leave scratch mode. |
| `play` | Note 39 | note | both | Play/pause toggle. Out: pulse (127 then 0) on a platter tap when tapPlay = 1. In: vel > 0 = deck is playing (LED feedback). |
| `tap` | Note 41 | note | both | Tap tempo. In: host taps tempo. Out: sent when a tap is detected in SYNC_MASTER mode. |
<!-- END GENERATED:midi -->

### 2.1 Encodings

* **Relative (offset-64):** `value = 64 + clamp(delta, -63, +63)`; value 64 means no change; value 0 is never sent.
* **14-bit:** `value14 = (msb << 7) | lsb`. Sent MSB first then LSB; either half MAY be skipped if unchanged.
  `bpm` carries BPM x 10 (`128.5 BPM = 1285`).
* **Notes:** velocity 127 on press, velocity 0 (as Note Off or Note On vel 0 - receivers MUST accept both) on release.

### 2.2 Jog (platter motion)

* Resolution is fixed at **1024 ticks per revolution** regardless of sensor.
* Positive delta = **clockwise as seen from above** when `jogInvert = 0`.
* The device accumulates motion and emits **at most one jog message every 2 ms (500 Hz)**.
  Each message carries `clamp(accum, -63, 63)`; the unsent remainder carries to the next message, so no motion is lost.
* While the platter is untouched (`platterSource = AS5600`, the default: a free-spinning platter) motion is still reported;
  with `TOUCH_RING` it is reported only while a finger is down.
* On touch-down the engine MUST re-anchor its angle reference so the first sample after touching never produces a jump.

### 2.3 Touch and tap-play

* `touch` note on when a finger is confirmed down, off when confirmed up.
  Thresholds are `touchOnPermille` / `touchOffPermille` (per-mille rise over a slowly-tracking baseline, with hysteresis).
  With `AS5600` and `TOUCH_HOLD_ONLY` the touch comes from the one hold pad (`touchRaw[0]`; the other seven entries are 0), whose
  reading the engine repeats across its ring detector; with `TOUCH_RING` it is the strongest of the 3-8 sectors. DJ software
  enters scratch mode on this note, so on the default platter it is as important as the jog.
* **Tap**: touch down -> up within `tapMaxMs` with total movement <= `tapMoveTicks`, and `tapPlay = 1`:
  emit a `play` note pulse (127, then 0 after >= 10 ms). In SYNC_MASTER this also toggles the local transport
  (SYNC_FOLLOW only mirrors a transport, and DJ mode has none).
  A tap MUST NOT also be reported as scratching (jog messages produced during the tap window are still sent; the host ignores sub-threshold motion).

### 2.4 Fader and knob

* Raw input is a 12-bit ADC reading. Calibrated position = `(raw - rawMin) / (rawMax - rawMin)` (swapped if min > max),
  clamped, passed through the dead zones (`faderDeadLo/HiPermille`), the `faderCurve`, then `faderInvert`.
* Output is 14-bit. The fader MUST emit within **2 ms** of a real movement and MUST NOT chatter when the pot is still
  (ADC noise of +/-3 counts must not produce output). Suggested filter: One-Euro style (heavy smoothing at rest, none when moving fast).
* The CC pair depends on `faderRole`: CROSSFADER -> `crossfader`, CHANNEL_VOLUME -> `channelVolume`, PITCH -> `pitch`.
* The knob sends `knob` (14-bit) when `knobRole = GENERIC`. When `knobRole = BPM` it instead sets the tempo
  (`60.0 + 120.0 x position` BPM, i.e. 60-180) and sends `bpm`.

### 2.5 Buttons and gestures

| Gesture | DJ mode / SYNC_FOLLOW | SYNC_MASTER |
|---|---|---|
| CUE press/release | `cue` note | **tap tempo** (also emits `tap` note) |
| REV press/release | `rev` note | **stop + rewind** (Stop, SPP 0) |
| Platter tap | `play` pulse | play / pause |
| Platter spin while touched | `jog` | **BPM nudge**: +0.1 BPM per 32 ticks clockwise (emits `bpm`) |
| CUE + REV held 2-5 s, then released | cycle mode DJ -> SYNC_MASTER -> SYNC_FOLLOW -> DJ | same |
| CUE + REV held >= 5 s | swap deck A <-> B, save, reboot | same |

Buttons are debounced by `buttonDebounceMs`. Single presses MUST NOT be delayed to wait for a chord.
When a chord completes, the engine sends note-offs for any button it had reported as down.

### 2.6 Transport, clock, timecode and BPM

Transport state (`TransportState`): STOPPED, PLAYING, PAUSED.

**Output** (only in `mode = SYNC_MASTER`, only to ports in `clockOutMask`; cable 0 = USB, cable 1 = TRS, LINK = radio):

* **Clock** `0xF8` at 24 PPQN, **continuously** while in SYNC_MASTER (so followers can lock tempo while stopped).
  Period = `60e6 / (bpm x 24)` microseconds; tempo changes take effect on the next tick with no phase jump.
* **Start** `0xFA`: STOPPED -> PLAYING. **Continue** `0xFB`: PAUSED -> PLAYING.
  **Stop** `0xFC`: PLAYING -> PAUSED, immediately followed by **Song Position** `0xF2 lsb msb`
  (position in MIDI beats = 6 clocks) so a later Continue resumes exactly there.
  **Stop + rewind**: Stop (if playing) then SPP 0, state STOPPED.
* **MTC** (when `mtcEnable = 1`): quarter-frame messages `0xF1 nd` (4 per frame at `mtcFps`, pieces 0..7 in order, piece 0 on a frame boundary)
  while PLAYING; a full-frame SysEx (`F0 7F 7F 01 01 hr mn sc fr F7`, `hr = (fpsType << 5) | hours`) on start, pause, stop and rewind.
  Time = elapsed PLAYING time since the last rewind (pause holds it).
* **BPM**: tempo changes are announced with `bpm` (14-bit) on change, at most 20 Hz.
* **LINK port bit**: the radio never carries `0xF8` bytes. Instead it sends reliable TRANSPORT packets (state, tempo, position, send time)
  to the linked deck on every state change, tempo change (<= 20 Hz), mode entry and every 10 s.
* **Edge semantics**: Pause rounds the position down to the announced MIDI beat. `Stop` + `SPP 0` on the wire means both
  pause-at-zero and stop-and-rewind, so followers mirror STOPPED. Rewind while PLAYING emits Stop, SPP 0, Start and keeps playing;
  while PAUSED it emits SPP 0 and ends STOPPED.

**Input** (always accepted on cable 0):

* `0xF8/0xFA/0xFB/0xFC` update the follower: BPM estimate (robust over >= 24 ticks; invalid after 500 ms without clock) and mirrored transport state.
* The `transport` CC (0/1/2) is the **command**: in SYNC_MASTER it drives the local transport; elsewhere it only updates the mirrored state.
  The `play` note with velocity > 0 is **feedback only, in every mode** (the host telling the unit its deck is playing, for the LED);
  it never starts the unit's clock, so a DJ app can mirror its play state to the unit regardless of mode.
  In `SYNC_MASTER` the LED shows Playing while either the unit's own transport or the host's deck is playing; the note still
  never touches the master's transport, clock or MTC.
* `bpm` / `bpmNudge` / `tap` set tempo (SYNC_MASTER applies it; otherwise it is stored for display).
* Tempo is clamped to `bpmMinX10..bpmMaxX10`. Tap tempo averages the last 4 intervals and resets after a 2 s gap.

In `DJ` mode the unit emits **no** clock/transport/MTC.

## 3. SysEx configuration protocol

Frame (manufacturer ID `7D` = non-commercial/prototype; `45 44` = "ED"):

```
F0 7D 45 44 <ver> <deck> <cmd> <packed payload ...> <checksum> F7
```

* `ver` = 1. `deck` = target deck 0-15, or `127` (any/broadcast); a unit answers only if `deck` matches its deck id or is 127.
* **Packing (8 -> 7):** payload bytes are split into groups of up to 7 bytes. Each group emits one header septet whose bit *i* is bit 7 of byte *i*,
  followed by the 7 low bits of each byte. Empty payload = no septets. Decoding reverses it.
* **Checksum:** `(-(sum of every byte from <ver> through the last payload septet)) & 0x7F`.
* **Replies:** `cmd | 0x40` carrying data; `0x7E` ACK with payload `[cmd]`; `0x7F` NAK with payload `[cmd, errorCode]`.
  The deck byte in a reply is the replying unit's deck id.
* All multi-byte integers inside payloads are little-endian; `i32` is two's complement.
* **Command rules** (every command): the payload length must match exactly, else `NAK BAD_LENGTH` with no side effects (a payload that
  cannot be unpacked, or exceeds 96 bytes, is also `BAD_LENGTH`). Boolean bytes above 1, `SET_DECK` with a deck above 1 and `REBOOT` with a
  mode above 1 are `OUT_OF_RANGE`. `CAL_BEGIN` with an unknown kind is `NOT_SUPPORTED`; `CAL_END` with a kind that is not active is
  `CAL_NOT_ACTIVE`. `TEST_TONE` needs `on` in {0,1}; when on, `freqHz` 20..20000 and `attenDb` <= 96, else `OUT_OF_RANGE`.
* `SET_CONFIG` of `deckId` is `NAK NOT_SUPPORTED`: the deck changes only through `SET_DECK`. `LOAD_DEFAULTS` keeps `deckId`.
* A `GET_CONFIG` dump (`0xFF`) answers one `CONFIG` frame per key the device knows, then `ACK[GET_CONFIG]` as the end marker, so a host and a
  firmware with different key counts still agree on when the dump is over. The device needs about 512 bytes of reply queue for a dump.
* Receivers MUST ignore frames with a bad checksum, wrong header or wrong version (NAK `BAD_CHECKSUM` is optional), and MUST tolerate
  SysEx split across USB packets and interleaved with real-time bytes.
* USB-MIDI framing of SysEx follows the USB-MIDI 1.0 CIN rules (4 = continue, 5/6/7 = end with 1/2/3 bytes).

<!-- BEGIN GENERATED:sysex -->
| Cmd | Name | Request payload | Response | Purpose |
|---|---|---|---|---|
| 0x01 | `GET_INFO` | (empty) | INFO (0x41): struct Info | Identify the unit, firmware, capabilities. |
| 0x02 | `GET_CONFIG` | [key u8] (0xFF = dump all) | CONFIG (0x42): [key u8, value i32le]; a dump (0xFF) sends one per key then ACK[GET_CONFIG] | Read config value(s). |
| 0x03 | `SET_CONFIG` | [key u8, value i32le] | ACK \| NAK(BAD_KEY/OUT_OF_RANGE/NOT_SUPPORTED for deckId: use SET_DECK) | Apply a config value live (not persisted until SAVE_CONFIG). |
| 0x04 | `SAVE_CONFIG` | (empty) | ACK \| NAK(FLASH_ERROR) | Persist the live config to flash. |
| 0x05 | `LOAD_DEFAULTS` | (empty) | ACK | Reset live config to defaults, KEEPING deckId (call SAVE_CONFIG to persist). |
| 0x06 | `STREAM` | [rateHz u8] | ACK, then TELEMETRY (0x47) frames | Start/stop raw telemetry (0 = off; rates above 100 Hz are clamped to 100). Auto-stops after 5 s without a refresh; repeating the request refreshes it. |
| 0x07 | `CAL_BEGIN` | [kind u8] | ACK \| NAK(NOT_SUPPORTED) | Begin recording extremes of a control (kind = CalKind). |
| 0x08 | `CAL_END` | [kind u8, apply u8] | CAL_RESULT (0x48): struct CalResult \| NAK(CAL_NOT_ACTIVE/CAL_FAILED) | Finish; apply=1 writes the measured range into the live config. CalResult carries the values written (with inset) when applied=1, else the measured extremes. |
| 0x09 | `SET_DECK` | [deck u8] | ACK | Set deck A/B, persist, and re-enumerate USB (reboot). The ACK carries the deck the unit had when the request arrived. |
| 0x0A | `REBOOT` | [mode u8] 0 = normal, 1 = ROM bootloader (firmware update) | ACK, then reboot | Restart the unit. |
| 0x0B | `LINK_STATUS` | (empty) | LINK (0x4B): [count u8] + count x struct Peer | Peers visible on the radio link. |
| 0x0C | `TEST_TONE` | [on u8 (0/1), freqHz u16le (20..20000), attenDb u8 (0..96)] | ACK \| NAK(NOT_SUPPORTED) | Play a sine on the DAC output (audio path test). Auto-stops after 10 s. |
| 0x0D | `IDENTIFY` | [seconds u8] | ACK | Flash the status LED so you can tell which physical unit this is. |
<!-- END GENERATED:sysex -->

Error codes:

<!-- BEGIN GENERATED:errors -->
| Code | Name |
|---|---|
| 1 | `UNKNOWN_CMD` |
| 2 | `BAD_LENGTH` |
| 3 | `BAD_CHECKSUM` |
| 4 | `BAD_KEY` |
| 5 | `OUT_OF_RANGE` |
| 6 | `BUSY` |
| 7 | `NOT_SUPPORTED` |
| 8 | `CAL_NOT_ACTIVE` |
| 9 | `CAL_FAILED` |
| 10 | `FLASH_ERROR` |
<!-- END GENERATED:errors -->

Enums and bit fields:

<!-- BEGIN GENERATED:enums -->
| Enum | Values |
|---|---|
| `Deck` | A = 0, B = 1 |
| `Mode` | DJ = 0, SYNC_MASTER = 1, SYNC_FOLLOW = 2 |
| `PlatterSource` | AS5600 = 0, TOUCH_RING = 1, TOUCH_HOLD_ONLY = 2 |
| `FaderRole` | CROSSFADER = 0, CHANNEL_VOLUME = 1, PITCH = 2 |
| `FaderCurve` | LINEAR = 0, SHARP_CUT = 1, SMOOTH = 2 |
| `KnobRole` | GENERIC = 0, BPM = 1 |
| `DacMode` | AUTO = 0, OFF = 1, ON = 2 |
| `LinkBridge` | AUTO = 0, OFF = 1, FORCE = 2 |
| `CalKind` | FADER = 0, KNOB = 1 |
| `TransportState` | STOPPED = 0, PLAYING = 1, PAUSED = 2 |
| `RebootMode` | NORMAL = 0, BOOTLOADER = 1 |
<!-- END GENERATED:enums -->

<!-- BEGIN GENERATED:bitfields -->
| Bit field | Bits |
|---|---|
| `Caps` | AS5600 = bit 0, TOUCH_RING = bit 1, DAC = bit 2, TRS_MIDI = bit 3, LINK = bit 4, USB_AUDIO = bit 5, STATUS_LED = bit 6 |
| `StateFlags` | TOUCHED = bit 0, ANGLE_VALID = bit 1, LINK_UP = bit 2, USB_MOUNTED = bit 3, AUDIO_STREAMING = bit 4, DAC_PRESENT = bit 5, BRIDGING = bit 6 |
| `Buttons` | CUE = bit 0, REV = bit 1 |
| `ClockPorts` | USB = bit 0, TRS = bit 1, LINK = bit 2 |
| `PeerFlags` | USB_HOST = bit 0, FORWARDING = bit 1, IS_MASTER = bit 2 |
| `PeerStatus` | CONFLICT = bit 0 |
<!-- END GENERATED:bitfields -->

Payload structs. `Telemetry.jogTotal` is the cumulative sum of the jog ticks the unit has emitted, **after** `jogInvert` is applied,
wrapping as an `i32`. `CalResult.rawMin/rawMax` are the values written to the config (with the 1% inset) when `applied = 1`, otherwise the
measured extremes. `Peer.status` bit 0 (`PeerStatus.CONFLICT`) means two units claim
the same deck id.

<!-- BEGIN GENERATED:structs -->
| Struct | Bytes | Fields (little-endian, packed) |
|---|---|---|
| `Info` | 18 | protocolVersion:u8, fwMajor:u8, fwMinor:u8, fwPatch:u8, hwRev:u8, deck:u8, mode:u8, platterSource:u8, caps:u16, serial:u32, uptimeS:u32 |
| `Telemetry` | 36 | seq:u8, buttons:u8, flags:u8, transport:u8, faderRaw:u16, knobRaw:u16, faderOut:u16, knobOut:u16, angle:u16, bpmX10:u16, jogTotal:i32, touchRaw:u16[8] |
| `CalResult` | 6 | kind:u8, applied:u8, rawMin:u16, rawMax:u16 |
| `Peer` | 10 | deck:u8, flags:u8, rssi:i8, status:u8, rttUs:u16, offsetUs:i32 |
<!-- END GENERATED:structs -->

### 3.1 Config keys

Config changes apply **live** (`SET_CONFIG`) and persist only on `SAVE_CONFIG`. Out-of-range values are rejected with `OUT_OF_RANGE`.
Exceptions that need a reboot to take effect: `dacMode`, `deckId` (via `SET_DECK`).

<!-- BEGIN GENERATED:config -->
| Id | Key | Type | Range | Default | Meaning |
|---|---|---|---|---|---|
| 0 | `deckId` | enum `Deck` | 0..1 | 0 | Which deck this unit is (A/B). Sets default MIDI channel and USB name. |
| 1 | `midiChannel` | u8 | 0..16 | 16 | Zero-based MIDI channel 0..15, or 16 = auto (follow deckId). |
| 2 | `mode` | enum `Mode` | 0..2 | 0 | DJ controller, standalone clock master, or clock follower. |
| 3 | `platterSource` | enum `PlatterSource` | 0..2 | 0 | How platter rotation is sensed. AS5600 (default): a free-spinning platter on a bearing with a diametric magnet over an AS5600 magnetic encoder; touch/hold comes from one touch pad. TOUCH_RING: a segmented conductive-PLA ring on touch pads, finger-slide scratching (experimental, kept for later). TOUCH_HOLD_ONLY: touch detect only, no rotation. |
| 4 | `jogInvert` | bool | 0..1 | 0 | Flip jog direction. |
| 5 | `touchSectors` | u8 | 3..8 | 8 | Number of capacitive sectors in the platter ring. |
| 6 | `touchOnPermille` | u16 | 1..1000 | 12 | Finger-down threshold, per-mille rise over baseline. |
| 7 | `touchOffPermille` | u16 | 1..1000 | 7 | Finger-up threshold (hysteresis), per-mille rise over baseline. |
| 8 | `tapPlay` | bool | 0..1 | 1 | A quick touch with no movement toggles play/pause. |
| 9 | `tapMaxMs` | u16 | 60..500 | 180 | Longest touch that still counts as a tap. |
| 10 | `tapMoveTicks` | u8 | 0..64 | 8 | Platter movement (ticks of 1/1024 rev) allowed during a tap. |
| 11 | `faderRole` | enum `FaderRole` | 0..2 | 2 | What the slide pot controls (changes the CC it sends). Default PITCH: the CDJ-style tempo slider, which is audible in every DJ app even with an external mixer, and two units never fight over one control. CHANNEL_VOLUME is a per-deck scratch cut (only audible where the app's deck outputs are post-fader, e.g. Mixxx); CROSSFADER suits a single software mixer. |
| 12 | `faderRawMin` | u16 | 0..4095 | 40 | 12-bit ADC reading at one end of travel. |
| 13 | `faderRawMax` | u16 | 0..4095 | 4055 | 12-bit ADC reading at the other end of travel. |
| 14 | `faderInvert` | bool | 0..1 | 0 | Flip fader direction. |
| 15 | `faderDeadLoPermille` | u16 | 0..400 | 15 | Dead zone at the low end (per-mille of travel). |
| 16 | `faderDeadHiPermille` | u16 | 0..400 | 15 | Dead zone at the high end (per-mille of travel). |
| 17 | `faderCurve` | enum `FaderCurve` | 0..2 | 0 | Response curve; SHARP_CUT gives a scratch-style fast cut. |
| 18 | `knobRole` | enum `KnobRole` | 0..1 | 0 | GENERIC sends CC 24; BPM turns the knob into a tempo control. |
| 19 | `knobRawMin` | u16 | 0..4095 | 0 | 12-bit ADC reading at one end of the knob. |
| 20 | `knobRawMax` | u16 | 0..4095 | 4095 | 12-bit ADC reading at the other end of the knob. |
| 21 | `knobInvert` | bool | 0..1 | 0 | Flip knob direction. |
| 22 | `clockOutMask` | bitfield | 0..7 | 3 | Ports that receive clock/transport/MTC in SYNC_MASTER mode. |
| 23 | `bpmX10` | u16 | 200..3000 | 1200 | Current tempo x10 (120.0 BPM = 1200). |
| 24 | `mtcEnable` | bool | 0..1 | 0 | Emit MIDI Time Code in SYNC_MASTER mode. |
| 25 | `mtcFps` | u8 | 0..3 | 3 | MTC frame rate type (see mtcFpsTypes). |
| 26 | `dacMode` | enum `DacMode` | 0..2 | 0 | USB audio output: AUTO detects a DAC, ON forces it, OFF is MIDI-only. Takes effect after reboot (USB descriptors change). |
| 27 | `linkEnable` | bool | 0..1 | 1 | Enable the unit-to-unit radio link. |
| 28 | `linkBridge` | enum `LinkBridge` | 0..2 | 0 | Forward the peer deck's MIDI over this unit's USB (cable 2). AUTO bridges only when the peer has no USB host. |
| 29 | `trsMidiEnable` | bool | 0..1 | 1 | Enable the 3.5 mm TRS MIDI jack (cable 1). |
| 30 | `ledBrightness` | u8 | 0..255 | 64 | Status LED brightness. |
| 31 | `buttonDebounceMs` | u8 | 1..30 | 4 | Button debounce time. |
| 32 | `linkGroup` | u8 | 0..255 | 0 | Radio rig id. Units only link with peers that share it, so two rigs in one room do not cross-link. |
<!-- END GENERATED:config -->

### 3.2 Calibration recipes

Calibration is **host-driven** over the telemetry stream (`STREAM`), with on-device help for the two analog controls.

| Control | Procedure |
|---|---|
| Fader | `CAL_BEGIN(FADER)`, user sweeps the pot end to end, `CAL_END(FADER, apply=1)`. The device keeps min/max of the raw reading, rejects a span < 1500 counts with `CAL_FAILED`, and writes `faderRawMin/Max` (with a 1% inset). |
| Knob | Same with `KNOB`. |
| Encoder check (`AS5600`) | Host: stream telemetry while the user spins the platter by hand. `Info.caps` AS5600 clear: no chip answered at boot (I2C wiring, 3.3 V). `StateFlags.ANGLE_VALID` never set: it answered at boot but reads fail now. Valid but `Telemetry.angle` moves less than one full turn (4096 counts) in about 10 s: no magnet, too far, or not on the axle. Angle jumping implausibly far between samples: magnet off-centre, too weak or too strong, or a noisy bus. A smooth full turn: OK. Nothing is written to the unit. |
| Platter direction | Host: ask the user to spin the platter clockwise while streaming (by hand on the AS5600 platter, a finger on the ring). `jogTotal` is measured after `jogInvert`, so if it decreases the current setting is backwards: write `jogInvert = 1 - current`. |
| Hold pad (`AS5600`, `TOUCH_HOLD_ONLY`) | Same rules as the touch ring with one sector: stream telemetry, collect `touchRaw[0]` idle for 1 s (noise), then a hand on the platter. Compute the per-mille rise, set `touchOnPermille` = 50% of it and `touchOffPermille` = 60% of that (never below 3x idle noise); `SET_CONFIG`; `SAVE_CONFIG`. This does not show the pad keeps seeing the hand while the platter turns. |
| Touch ring (`TOUCH_RING`, experimental) | Host: stream telemetry, collect `touchRaw` idle for 1 s (noise), then a finger on each sector. Compute per-mille rise, set `touchOnPermille` = 50% of the weakest sector's rise and `touchOffPermille` = 60% of that (never below 3x idle noise); `SET_CONFIG`; `SAVE_CONFIG`. |
| Platter source | Host reads `Info.caps`: AS5600 present -> `AS5600` (the default); else TOUCH_RING present (3 or more pads wired) -> `TOUCH_RING` (experimental); else `TOUCH_HOLD_ONLY`. |

## 4. Radio link (unit-to-unit)

Each unit may link to its peer over **ESP-NOW** (no router, no pairing menus). Summary; the wire format lives in
[`firmware/core/include/epdj/link.h`](../firmware/core/include/epdj/link.h) and [`docs/LINK.md`](LINK.md).

* Units only link with peers that share the same `linkGroup` (default 0), so two rigs in one room never cross-link.
* Units broadcast a HELLO (deck, MAC, flags: `USB_HOST`, `FORWARDING`, `IS_MASTER`); a peer table with timeouts tracks who is alive. Two units that both claim the same deck id surface an error in `LINK_STATUS` rather than silently fighting.
* **Master** = lower deck id (A before B). The master's tempo/transport is the shared time base; followers estimate clock offset with a 4-timestamp exchange.
* **Bridge**: a unit with **no USB host** forwards its cable-0 MIDI to a peer that has one; the peer emits it on **cable 2** (channel preserved, so Deck B stays on channel 2) and relays host feedback (notes/CCs received on cable 2) back. Jog is forwarded as a cumulative 16-bit counter so lost radio packets never lose motion.
* The bridge forwards **Note and CC only**: system real-time (clock/start/stop/MTC), program change and SysEx are not bridged, so a bridged
  deck cannot follow host MIDI clock and cannot be configured over the link (plug it in directly to calibrate).
* Radio MIDI is best-effort, with a design target of <= 5 ms typical latency. That figure comes from the simulated link
  (`LINK.md` section 12); it has never been measured on a radio. Config/transport messages use sequence numbers and retries.

## 5. Conformance

Every implementation MUST pass `protocol/test-vectors.json`: 8->7 packing, checksum/frames, struct serialization,
14-bit and relative encodings, USB-MIDI event packets, MTC quarter-frames/full-frame, and clock periods.
`python3 tools/gen_protocol.py --check && python3 tools/gen_vectors.py --check` MUST pass before a contract change is merged.
Nothing runs it automatically yet (the repo has no CI configuration), so run it by hand.
