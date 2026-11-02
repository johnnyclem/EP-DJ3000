# EP-DJ3000 radio link (v1)

The wire format and behavior of the unit-to-unit link. [`PROTOCOL.md`](PROTOCOL.md) section 4 is the summary;
this is the specification. The reference implementation is
[`firmware/core/include/epdj/link.h`](../firmware/core/include/epdj/link.h) (portable, host-tested); the
platform layer only moves packets and timestamps. Normative words as in RFC 2119.

## 1. Transport assumptions

The link runs on **ESP-NOW broadcast**: at most 250 bytes per frame, no delivery guarantee, frames may be
lost, **duplicated and reordered**, one-way latency about 1-3 ms. Nothing else is assumed. There is no
pairing; every unit broadcasts and listens. The link layer has four jobs: find the other deck, carry its
MIDI (so that a deck without a USB host can ride on the other deck's cable), keep the two clocks aligned,
and carry transport state. Audio does not go over the link.

## 2. Packet format

All multi-byte fields are little-endian. A packet is `header (18) + payload (0..230) + CRC16 (2)`, 20..250
bytes.

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 2 | magic | `45 4A` ("EJ") |
| 2 | 1 | version | 1. Any other value: reject the whole packet (a future version is never misparsed). |
| 3 | 1 | type | table below |
| 4 | 1 | group | rig id. Units only talk to the same group (default 0); lets two rigs share a room. |
| 5 | 1 | deck | sender's deck id (0 = A, 1 = B); `FF` = not a deck (reserved for a mixer) |
| 6 | 1 | flags | sender's `PeerFlags`: bit 0 USB_HOST, bit 1 FORWARDING, bit 2 IS_MASTER. Refreshed by every packet. |
| 7 | 2 | epoch | random per boot. A change means the sender rebooted: all state kept for it is discarded. |
| 9 | 2 | seq | +1 per packet sent (all types), wraps. Compared with serial-number arithmetic. |
| 11 | 6 | mac | sender (the station MAC). Zero and multicast MACs are invalid. |
| 17 | 1 | length | payload bytes |
| 18 | length | payload | |
| 18+length | 2 | crc16 | CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout) over everything before it; `crc16("123456789") = 0x29B1` |

ESP-NOW has its own frame check, but frames from other software on the channel and bit errors that slip
through must still be rejected cleanly. A receiver validates **in this order** and drops the packet at the
first failure (counting it): length >= 20; magic; version; `18 + length + 2 == packet size` exactly; CRC;
type in 1..7; source MAC valid. Then group (other group: ignored), then own MAC (ignored). Every
single-bit error anywhere in a packet is rejected (tested exhaustively).

| Type | Name | Direction | Reliable | Payload |
|---|---|---|---|---|
| 1 | HELLO | broadcast, every 500 ms | no | `bridge u8` (LinkBridge: 0 AUTO, 1 OFF, 2 FORCE) |
| 2 | HEARTBEAT | broadcast, every 100 ms while forwarding | no | state block (below) |
| 3 | TIME_REQ | to partner | no | `dst[6] reqId u8 t1 u32` |
| 4 | TIME_RESP | to partner | no | `dst[6] reqId u8 t1 u32 t2 u32 t3 u32` |
| 5 | MIDI | to partner | events part: yes | `flags u8` then optional events block, then optional state block |
| 6 | TRANSPORT | to partner | yes | `dst[6] msgId u8 state u8 bpmX10 u16 positionClocks u32 atUs u32` |
| 7 | ACK | to partner | no | `dst[6] acked u8 (5 or 6) msgId u8` |

"To partner" frames are still broadcast on the air; `dst` (the receiver's MAC) says who acts on them, so a
third unit that hears them ignores them. Packets whose payload fails strict decoding (exact length, every
field in range) are dropped and counted; whatever a decoder accepts re-encodes to the same bytes.

**State block** (HEARTBEAT payload, and part of MIDI): `jogChannel u8 (0..15)`, `jogCount u16`,
`nLatched u8 (0..16)`, then `nLatched x {channel u8, cc u8, value u8}`. A complete snapshot of the
sender's continuous controls.

**MIDI payload**: `flags u8` (bit 0 = events block present, bit 1 = state block present, others 0, not both
zero). Events block: `dst[6] msgId u8 count u8 (1..8)` then `count x {cable u8 (0..15), status u8, d1 u8,
d2 u8}`. Only Note Off / Note On / Control Change are valid events. Largest MIDI payload: 1 + 40 + 52 = 93
bytes.

**TRANSPORT**: "at the sender's clock time `atUs` the transport was `state` (0 stopped, 1 playing, 2 paused),
`positionClocks` MIDI clocks (24 PPQN) from the start, at `bpmX10` (200..3000)". A follower can phase-align
its clock generator from this alone (the receiver converts `atUs` into its own clock when time sync has an
estimate, see 7).

Typical sizes: HELLO 21 B, HEARTBEAT 24 + 3n B, TIME_REQ 31 B, TIME_RESP 39 B, ACK 28 B, TRANSPORT 38 B,
MIDI with a jog counter and one fader pair 31 B.

## 3. Peers, liveness, identity

* **Peer table**: 3 entries. Any valid packet of our group from a MAC (re)starts its 1.5 s liveness timer; a peer silent
  for 1.5 s is dropped. A fourth MAC is ignored while the table is full (counted). The third slot exists for
  a future non-deck device (mixer, `deck = FF`): it is listed in LINK_STATUS but is never elected, never the
  partner and never counts as a conflict.
* **Epoch**: a known MAC arriving with a new epoch is a reboot even if it came back within the timeout (a
  `SET_DECK` reboot does). The receiver resets everything it holds for that peer exactly as if the peer had
  timed out: held notes are released, jog and fader state re-adopted, reliable-message windows cleared.
* **Master election**: the master is the live deck with the lowest `(deck id, MAC)`. Every unit that sees
  the same set computes the same answer, so a duplicate deck id does not produce two masters. A unit sets
  `IS_MASTER` in its own packets from its own computation.
* **Partner**: the one deck peer that data flows with (time sync, bridging, transport): the live deck peer
  with the lowest `(deck, MAC)`. With the two-deck hardware it is simply "the other deck". Which units hear
  each other is isolated by **group**; two rigs in one room need different groups (section 11).
* **Duplicate deck id**: if a live peer announces our deck id, or two peers announce the same one, that is
  reported in LINK_STATUS (`Peer.reserved` bit 0, section 10). The link keeps working (election falls back
  to the MAC); the host is expected to tell the user to change a deck id.

## 4. The bridge ("one cable" mode)

A unit with no USB host forwards its cable-0 MIDI to a peer that has one; the peer emits it on **its USB
cable 2** with the **MIDI channel unchanged** (Deck B stays on channel 2), and relays anything the host
sends to cable 2 back.

```
 Deck B (no host)                                        Deck A (USB to the Mac)
 engine -> forwardMidi(m) --------- MIDI packets ------> onBridgedMidi(m) -> USB cable 2 -> Mac
 onFeedbackMidi(m) <------ MIDI events, cable 2 -------- relayHostMidi(m) <- USB cable 2 <- Mac
```

`linkBridge` is read from the **receiving** unit ("forward the peer deck's MIDI over *this* unit's USB").
A route `src -> dst` is active when

| dst.linkBridge | src.linkBridge | Route |
|---|---|---|
| any | OFF | no |
| OFF | any | no |
| FORCE | AUTO / FORCE | **yes**, whatever the host flags say |
| AUTO | AUTO / FORCE | yes **iff** dst has a USB host and src does not |

Both units evaluate this same pure function (`bridgeRoute`) from the host flag in every packet header and
the `linkBridge` byte in HELLO, so they agree within one packet / one HELLO (500 ms). Consequences with the
defaults: a power-only deck bridges through the deck on the Mac; two decks both plugged into the Mac do not
bridge (each talks on its own cable); neither deck plugged in: no route. Plugging a deck into a host or
unplugging it flips the route with the next packet that deck sends (at most 500 ms when it is otherwise idle).

What is forwarded: cable-0 Note On/Off and Control Change (jog, faders, knob, buttons, tempo). What is
**not**: system messages (clock, start/stop, SPP, MTC), program change, SysEx. Tempo and transport between
decks go through TRANSPORT and the clock sync, not through the bridge, and a bridged deck cannot follow
host MIDI clock in v1. Cable 1 (TRS) is not bridged. Host feedback relayed back is limited to Note and CC
(LED states, tempo).

When a route ends for any reason (peer timeout, reboot, host plugged in, `linkBridge` changed), the
receiver emits Note Off for every note still held on behalf of the peer, so a lost link cannot leave a note
stuck at the host.

## 5. How MIDI is forwarded

The sender classifies each message; each class has the delivery semantics it needs.

| Class | Messages | Mechanism | Guarantee |
|---|---|---|---|
| Motion | cable-0 CC 22 (jog, relative) | folded into a **cumulative 16-bit counter**, mod 2^16, in every state block | lost, duplicated or reordered packets can neither lose nor double-count a tick |
| Continuous controls | every other cable-0 CC except tempo nudge (fader, knob, bpm pairs...) | per `(channel, cc)` slot, **last value wins**; every state block carries all slots (max 16) | converges to the sender's current value; never regresses to an older one |
| Events | Note On/Off, CC 21 (tempo nudge), latch overflow, everything on cable 2 | queue (64), batches of up to 8, **stop-and-wait with retries** (section 6) | applied once, in order, or counted as abandoned after the retry budget |
| Refused | system messages, program change... | `forwardMidi` returns false | - |

**Receiver rules.** A state block is applied only if its packet `seq` is newer than the last applied one
(serial arithmetic); older or equal ones are ignored, which makes duplicates and reordering harmless. The
jog counter's difference to the previous applied counter, read as a signed 16-bit value, is the number of
ticks to emit; the first state after a (re)start adopts the counter without emitting, so motion that
happened while the peer was away is not replayed as a burst. Ticks are emitted through the same 2 ms /
+-63 limited emitter as local jog, so the host sees ordinary relative CCs. A latched value is emitted only
when it differs from the last one emitted, so repeated snapshots do not spam the host. Events of a
packet are applied before its state block.

**Latency.** The first change after a quiet period is sent on the next `poll()`; after that MIDI packets
leave at most every 2 ms (500 per second). Heartbeats (100 ms) repair a lost trailing packet: after the last
movement the receiver is exact within one heartbeat period plus loss. While an event batch is unacknowledged
it is repeated inside every MIDI packet, so state cannot overtake it; events queued behind the batch in
flight can still be applied up to one stop-and-wait round (RTT ~3 ms, more under loss) after continuous
state that was produced later.

## 6. Reliable messages (events and TRANSPORT)

Stop-and-wait, one message in flight per kind.

* The sender transmits, then repeats after 6 ms, 12 ms, 24 ms, then every 40 ms; 12 transmissions in all,
  about 400 ms, after which the batch is **abandoned** (counted) and the queue moves on.
* The receiver ACKs **every** reception of a message addressed to it, duplicates included (the first ACK may
  have been lost), and applies it only the first time: message ids are 8-bit serial numbers per kind; an id
  newer than the last applied one is new (the sender may skip ids it gave up on), the same or older one is a
  duplicate.
* TRANSPORT is a state, not a command: a newer `sendTransport` supersedes one still unacknowledged (new id,
  immediate send). When its retry budget runs out it starts over with the same id (so the peer still applies
  it once) for as long as the peer counts as alive; losing the peer cancels it. The last transport state is
  remembered and sent again to a peer that appears or reboots.
* Events, by contrast, are **bounded**: a batch lost 12 times in a row (about 400 ms of dead air with the
  peer still inside its 1.5 s liveness window) is dropped, so a Note Off in it is never delivered. The
  receiver releases every note it holds when it declares the link lost, which limits such a stuck note to
  the time until that happens.
* With 30% loss per packet in each direction the probability that a batch is never delivered is
  0.3^12 = 5e-7.

## 7. Time sync

Each unit estimates the partner's microsecond clock as a function of its own, using 4-timestamp exchanges
(TIME_REQ / TIME_RESP): `t1` request sent (requester clock), `t2` request received and `t3` response sent
(responder clock), `t4` response received (requester clock). Timestamps are the low 32 bits of the unit's
64-bit microsecond clock; only differences are formed, so wrap-around and offsets larger than 2^31 us (a
unit that has been up for hours) are handled.

* `RTT = (t4 - t1) - (t3 - t2)`; the offset sample is `((t2 - t1) + (t3 - t4)) / 2`. It is wrong by half
  the difference between the two one-way delays, which is bounded by half the round trip's excess over the
  best one seen.
* The estimator keeps the last 64 exchanges and fits a **weighted least-squares line** (offset and skew)
  with weight `1 / (excess/2 + 20 us)^2`: a sample that rode a radio retry counts for nothing, the fastest
  ones carry the fit. The slope is shrunk toward zero by a +-100 ppm prior so a few close-together
  samples cannot invent a skew, and clamped to +-300 ppm. This is the outlier rejection; exchanges with an
  impossible timestamp relation or RTT above 50 ms are discarded outright.
* **Slewing**: when a refit moves the estimate, the difference is bled out at 500 ppm (0.5 us per ms)
  instead of stepping, so anything derived from the mapping (a clock generator phase) never jumps. A
  difference above 2 ms is stepped at once.
* **Clock steps**: three consecutive fast samples more than 2 ms off the model mean the peer's clock
  stepped; the estimator restarts from that sample.
* **Pacing**: a request every 100 ms until 32 answers have arrived (3.2 s), then every 250 ms. A reboot or a
  new partner restarts the fast phase.

Responses are stamped and sent inside `onPacket`: `t2` is the radio's receive timestamp, `t3` the time of
the call (the processing time between them is subtracted by the formula and costs no accuracy). The error
that remains is the radio's: **about one third of the mean one-way delay jitter**. Measured against a
simulated link (800 us fixed delay each way, exponential extra delay with different means in the two
directions, 5% of exchanges hit by a 1-6 ms retry in one direction, two free-running crystals up to +-100
ppm apart, counters starting anywhere including across the 32-bit wrap):

| Radio (mean extra delay forward / reverse) | Worst error after the first 10-20 s |
|---|---|
| quiet: 40 / 80 us, no retries | 20 us |
| typical test model: 150 / 400 us, 5% retries | **80 us** over 4 seeds, **115 us** over 60 sessions of 120 s |
| rough: 300 / 800 us, 5% retries | 140 us |

The estimate is within 200 us from about 3 s after the first exchange in all the typical-model sessions
(last excursion above 200 us at 2.4 s), and the skew estimate is within 15 ppm of the true crystal mismatch.
**These are simulated figures.** A fixed asymmetry between the two directions (antenna, driver queueing
that differs by direction) is invisible to any such scheme and adds half of itself as a constant error;
measure the real link before relying on 200 us.

`Link::masterTimeToLocal` / `localTimeToMaster` convert between the master's clock and ours (identity on
the master).

## 8. Constants

| Name | Value |
|---|---|
| peer timeout | 1.5 s |
| HELLO / HEARTBEAT period | 500 ms / 100 ms |
| MIDI packet interval (min) | 2 ms |
| event batch / queue / latch slots | 8 / 64 / 16 |
| retransmission schedule | 6, 12, 24, 40... ms; 12 transmissions |
| time sync window / pacing | 64 exchanges / 100 ms then 250 ms |
| max slew / step threshold / max skew | 500 ppm / 2 ms / 300 ppm |
| max packet / header / payload | 250 / 18 / 230 bytes |

Memory (host build, 64-bit): `Link` 1.7 kB, of which `TimeSync` 0.8 kB and `MidiForwarder` 0.5 kB. Stack per
call is a few hundred bytes (largest frames: a 250-byte packet buffer in `sendPacket`, a 256-byte weight array
in the time-sync refit); not measured on the target. No heap anywhere.

## 9. Statistics

`Link::stats()`: packets sent / failed (the transport refused them), received accepted, corrupt (framing,
CRC, version), foreign (other group, own echo), malformed (valid frame, invalid payload), stale state
ignored (reordering / duplication seen), peer-table-full, transport retry budgets used up; `forwarder()` exposes dropped (queue full),
abandoned (retries exhausted) and retried event counts.

## 10. LINK_STATUS

`Link::fillPeers` fills one `Peer` ([`PROTOCOL.md`](PROTOCOL.md) section 3) per live peer, in table order:

| Field | Value |
|---|---|
| `deck` | the peer's announced deck id (`FF` = not a deck) |
| `flags` | USB_HOST and FORWARDING as the peer announced them; IS_MASTER set on the peer we elected (not on a peer when we are the master) |
| `rssi` | dBm of the last packet |
| `reserved` | bit 0 = **deck-id conflict** (this peer claims our deck id or the same id as another peer). Bits 1-7 are 0. |
| `rttUs` | fastest recent round trip to the partner (0 for other peers, or before the first answer) |
| `offsetUs` | partner clock minus ours now, microseconds (0 for other peers) |

A host also sees a conflict without the bit: a `Peer.deck` equal to the replying unit's own `Info.deck`.

## 11. What the platform layer must provide

* **Identity**: station MAC from efuse; `deck` from `deckId`; `epoch` from the hardware RNG at every boot;
  `group` (default 0). Call `Link::configure`, then `setUsbHost(tud_mounted())` whenever it changes and
  `setBridgeMode(linkBridge)` when the config key changes.
* **Radio**: all units on the same Wi-Fi channel (fix it; the ESP-NOW default is the current channel),
  broadcast peer registered, no encryption (as far as the ESP-IDF docs go, ESP-NOW does not encrypt broadcast
  frames; unicast with a shared LMK would be the later option; verify). Set a PHY rate well above the 1 Mbps default for broadcast
  (`esp_wifi_config_espnow_rate`): a ~60-byte frame costs about 0.7 ms of airtime at 1 Mbps, so 500
  packets per second would otherwise occupy over a third of the channel. **Not measured here.**
* **Threads**: one link task owns every `Link` call. The receive callback must not call into `Link`: it
  pushes `{bytes, rssi, esp_timer_get_time()}` to a queue and the task calls
  `onPacket(data, len, rssi, rxUs, nowUs)` with the callback's timestamp as `rxUs` and the clock as `nowUs`.
  `poll(nowUs)` every 1 ms (never slower than 2 ms). The same task owns the `LinkTransport::send` call; a
  `false` return (queue full) is treated as packet loss.
* **Timestamps**: `esp_timer_get_time()` for everything. Time sync accuracy is only as good as how late the
  receive timestamp is taken and how soon after the stamp the frame is queued: take it in the callback.
* **Sinks**: `onBridgedMidi` -> USB-MIDI cable 2; `onFeedbackMidi` -> the engine, as cable 0 input;
  `onTransport` -> the sync engine; call `forwardMidi` with every cable-0 message the engine would send and
  `relayHostMidi` with every Note/CC received from the host on cable 2.
* **Status**: `StateFlags::LINK_UP` = `linkUp()`, `BRIDGING` = `bridging()`; `PeerFlags` bits in the header
  come from the link itself.

## 12. Verification status

Host tests under ASan/UBSan (`test_link_*`): framing with an independent Python vector, exhaustive bit-flip
and truncation rejection, 200k random frames and 190k fuzzed payloads; peer table, election agreement over
random sets, conflicts, timeouts to the microsecond; bridge truth table; time sync matrix (5 crystal
combinations x 3 clock origins x 4 seeds, wrap, large offsets, clock step, slew continuity); forwarder under
30% loss + 10% duplication + 12 ms reordering with exact jog conservation, exactly-once in-order events and
last-value-wins fader (mutation-checked: stale-state acceptance, missing dedupe, missing 16-bit wrap, no
retries and no heartbeat each make it fail); and two complete `Link` instances over a simulated air:
discovery, bridge in both directions, mode changes, loss of a peer with a held note, reboot with a new epoch,
reliable transport with time translation, duplicate deck id, group isolation, hostile input.

**Not verified**: anything on real hardware or a real radio (ESP-NOW latency, jitter, loss, airtime, PHY
rate, channel coexistence with Wi-Fi/BLE); the time-sync figures above come from the simulated link only.

## 13. Contract changes this document relies on

Proposed for [`protocol/epdj3000.json`](../protocol/epdj3000.json) (not applied; the orchestrator owns it):

1. `structs.Peer.reserved` -> name it `status` and add a `PeerStatus` bit field with `CONFLICT = bit 0`.
   The implementation already writes the conflict flag into that byte.
2. A config key `linkGroup` (u8 0..255, default 0) so two rigs in one room can be separated without
   firmware changes; the group is already in the wire header.
