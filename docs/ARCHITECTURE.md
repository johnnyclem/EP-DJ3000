# EP-DJ3000 architecture, decisions and scope

A 3D-printed, ESP32-based scratch deck that sits in the same row as a Teenage
Engineering EP-133 K.O. II and an EP-136 K.O.-SIDEKICK mixer. This document
records **what we are building, what we decided, and what is and is not
verified**. The wire contract is in [`PROTOCOL.md`](PROTOCOL.md).

## 1. The system

```
                 +-----------------------------+
  DJ software    |  VirtualDJ / djay / Mixxx   |   drivers/   mappings (MIDI)
  (host)         |  rekordbox                  |
                 +--------------+--------------+
                                |  USB-C  (MIDI + optional USB Audio out)
        +-----------------------+------------------------+
        |                                                |
+-------+--------+      ESP-NOW radio link       +-------+--------+
|   EP-DJ3000    | <---------------------------> |   EP-DJ3000    |
|    Deck A      |   roles, clock, MIDI bridge   |    Deck B      |
+---+--------+---+                               +---+--------+---+
    | 3.5mm  | I2S DAC -> line out                   |        |
    | TRS    +---------------------+      +----------+        |
    | MIDI                         v      v                   |
    v                       +------+------+------+            v
 external gear              |  EP-136 K.O.-SIDEKICK  |     external gear
 (EP-133 K.O. II)           |  2-ch mixer + interface|
                            +-----------+------------+
                                        | line out / USB
                                      speakers
```

"Poor man's CDJs": each deck is a **controller + its own USB sound card**.
The DJ software sends deck 1 audio to Deck A's USB audio output and deck 2 audio
to Deck B's. Each deck's DAC drives a line out into one channel of the Sidekick,
which does the actual mixing (EQ, faders, cue, FX) in hardware, exactly like
CDJs into a DJ mixer. The decks' MIDI drives the software players.

### Repo layout

| Path | What |
|---|---|
| `protocol/` | `epdj3000.json` (contract), `test-vectors.json` (byte-exact oracle) |
| `tools/` | `gen_protocol.py`, `gen_vectors.py` (+ `--check` drift detectors) |
| `firmware/core/` | Portable C++17 firmware logic. No ESP-IDF. Host unit tests (`ctest`). |
| `firmware/esp32s3/` | ESP-IDF project: HAL, TinyUSB (MIDI + UAC2), I2S, ESP-NOW, NVS, tasks |
| `macos/` | SwiftPM: pure-Swift libraries (build/test on Linux too) + CoreMIDI/AVAudio/SwiftUI app |
| `drivers/` | DJ-software mappings (Mixxx, VirtualDJ, djay Pro, rekordbox) |
| `hardware/` | Wiring, BOM, conductive-PLA platter guidance |
| `field-jog/` | The earlier Pro Micro/AS5600 enclosure + sketch (kept; its mechanical envelope still applies) |
| `src/`, `server/`, ... | The Field Jog 3D web viewer. **Untouched.** |

## 2. Decisions (made so we could start; each is cheap to reverse)

1. **ESP32-S3**, not classic ESP32. USB device mode (MIDI + audio) needs native USB OTG, which only the S2/S3 have; the S3 also has 14 capacitive touch pads, BLE 5, and ESP-NOW. This is the single biggest hardware assumption.
2. **Platter sensing is a build-time/config choice** (`platterSource`), because "conductive-PLA platter" can mean two things:
   * `TOUCH_RING` *(default)* - the platter is printed in 3-8 conductive sectors, each wired to a touch pad. Finger angle = phasor sum of sector activations; motion between samples = jog. No bearing, no magnet, no encoder. Finger-slide scratching.
   * `AS5600` - the earlier mechanical design: free-spinning platter on a 683ZZ bearing with a diametric magnet over an AS5600. The platter (or one pad) still provides touch/hold sensing.
   * `TOUCH_HOLD_ONLY` - touch detect only, no rotation.
   If you intended only one of these, delete the other later; the abstraction is ~one class.
3. **Slide pot** -> 12-bit ADC (ADC1 only; ADC2 is unusable while the radio is on). 14-bit MIDI out. Role is configurable (crossfader default).
4. **DAC**: I2S, PCM5102A-class (no I2C, no MCLK) as the reference part. "When a suitable DAC is available" = `dacMode` AUTO probes known I2C codec addresses and a DAC-present strap pin; otherwise MIDI-only. Descriptors can't change after enumeration, so a mode change reboots.
5. **USB audio = UAC2**, because TinyUSB only implements UAC2 for devices. macOS, iPadOS and Linux support it natively; Windows 10+ does for basic output. Async + feedback endpoint (drift between the USB 1 kHz clock and the I2S clock is the classic failure mode of DIY USB DACs; it is handled explicitly).
6. **MIDI**: Deck A = ch 1, Deck B = ch 2. Jog is relative CC 22 at 1024 ticks/rev; touch note; fader/knob 14-bit. Compatible in spirit with the earlier Field Jog map (CC 22/23/24, notes 36/37). See `PROTOCOL.md`.
7. **Link**: ESP-NOW between decks (TE's own vocabulary is USB-C MIDI, 3.5 mm TRS MIDI and BLE MIDI; we use TRS for external gear and radio for deck-to-deck). BLE-MIDI is **not** in v1: its connection interval adds 7-15 ms of jitter, which is bad for scratching. It's a fine roadmap item for transport-only control.
8. **TRS MIDI** is **Type A** (MIDI Association spec for 3.5 mm). Verify against your EP-133 before wiring; if its jack is Type B, swap tip/ring (one-line change in `hardware/`).
9. **Config**: everything tunable is an int32 key in the contract (`config.keys`), live-settable, persisted to NVS on `SAVE_CONFIG`. Calibration is host-driven.
10. **No SysEx secrets, no network services, no cloud.** The device is a USB peripheral; the radio link carries only MIDI/time data (optional ESP-NOW encryption is documented, off by default).

## 3. The mixer: what we know and what we do not

From TE's product page the **EP-136 K.O.-SIDEKICK** is "a two channel mixer, audio interface and fx processor", USB-C or 2xAAA powered, and units "link" to expand channels. We **do not** have its MIDI/USB implementation chart, so v1 treats it as an **audio-only** mixer:

* Audio: deck line out -> Sidekick input (3.5 mm). Zero protocol knowledge needed.
* Control: none. Its faders/EQ are analog/physical, which is the point of a hardware mixer.
* If TE publishes MIDI for the Sidekick (or you capture it), add a `mixer` profile; the link layer reserves a peer slot for non-deck devices.

Mechanically, the shell already has Technic-pin holes at the EP spacing so decks bolt to either side of the Sidekick.

## 4. "Seamless" 2-deck + mixer: what v1 actually delivers

1. **Zero-config identity**: each unit is Deck A or B (hold CUE+REV 5 s to swap), exposed in the USB name and MIDI channel, so two units on one Mac never collide and one mapping file serves both.
2. **Auto link**: units find each other over ESP-NOW with no menus, elect roles, and share tempo/transport.
3. **One-cable mode**: a deck with no USB host (power-only) bridges its MIDI through the other deck's USB (cable 2). Audio still needs each deck's own out (or a single shared DAC; see roadmap).
4. **Rig view** in the macOS app shows both decks, link health, audio routing and a one-click test (tone to each deck, jog/fader/button check).
5. Documented DJ-app routing for deck 1 -> Deck A audio, deck 2 -> Deck B audio.

**Not in v1 (honest scope-back):** audio over the radio link (ESP-NOW cannot reliably carry 1.5 Mbit/s of real-time PCM); a shared single USB audio device exposing 4 channels (needs a second I2S path or TDM wiring between decks - a hardware decision); BLE-MIDI; mixer MIDI control; firmware OTA (use the ROM USB bootloader via `REBOOT`).

## 5. Verification matrix (what "done" means)

| Component | How verified here | NOT verified here |
|---|---|---|
| Wire contract | generator `--check`, C++/Swift/JS compile, hand-checked vectors | - |
| Firmware core logic | host unit tests incl. shared vectors, simulation of sensors, sanitizers | real sensor behavior on printed conductive PLA |
| ESP-IDF layer | **compiles and links** for ESP32-S3 via PlatformIO (real IDF + TinyUSB headers) | running on a chip; USB enumeration; I2S audio quality; ESP-NOW latency |
| USB audio | descriptor and feedback-controller logic unit-tested | enumeration on macOS; glitch-free playback |
| Swift libraries | `swift test` on Linux | - |
| macOS app (CoreMIDI/AVAudio/SwiftUI) | reviewed against Apple API signatures; pure logic tested | **not compiled** (no Xcode here) |
| DJ mappings | Mixxx script run against a mock engine; XML well-formedness; numbers cross-checked to the contract | import into the real apps (VirtualDJ, djay, rekordbox) |

The three "not verified" columns are the first things to do when hardware is in
hand. `docs/BRINGUP.md` is the ordered checklist.

## 6. Order of bring-up (recommended)

1. Flash firmware, watch `idf.py monitor` logs: sensors alive, raw touch values per sector.
2. macOS app -> Test tab: telemetry view. Calibrate fader, knob, touch ring; spin direction.
3. MIDI monitor: confirm jog/touch/buttons; load a track in the app's built-in player, scratch.
4. Add the DAC; confirm "EP-DJ3000 Audio" appears; play the app's test tone.
5. Second unit: confirm link, then try one-cable bridging.
6. Load the Mixxx/VirtualDJ mapping.
