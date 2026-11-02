# EP-DJ3000 architecture, decisions and scope

A 3D-printed, ESP32-based scratch deck that sits in the same row as a Teenage
Engineering EP-133 K.O. II and an EP-136 K.O.-SIDEKICK mixer. This document
records **what we are building, what we decided, and what is and is not
verified**. The wire contract (v1.1) is in [`PROTOCOL.md`](PROTOCOL.md); start at the
[root README](../README.md) for the status summary and quick-starts.

## 1. The system

```
                 +-----------------------------+
  DJ software    |  VirtualDJ / djay / Mixxx   |   drivers/   mappings (MIDI)
  (host)         |  rekordbox                  |
                 +--------------+--------------+
                                |  USB-C  (MIDI, and USB Audio out in topology A)
        +-----------------------+------------------------+
        |                                                |
+-------+--------+      ESP-NOW radio link       +-------+--------+
|   EP-DJ3000    | <---------------------------> |   EP-DJ3000    |
|    Deck A      |   roles, clock, MIDI bridge   |    Deck B      |
+---+--------+---+                               +---+--------+---+
    | 3.5mm  | I2S DAC -> line out (topology A)      |        |
    | TRS    +---------------------+      +----------+        |
    | MIDI                         v      v                   |
    v                       +------+------+------+            v
 external gear              |  EP-136 K.O.-SIDEKICK  |     external gear
 (EP-133 K.O. II)           |  2-ch mixer + interface|
                            +-----------+------------+
                                        | line out / USB
                                      speakers
```

"Poor man's CDJs": each deck is a **controller**, and the mixer does the mixing (EQ, faders, cue, FX) in
hardware, like CDJs into a DJ mixer. The decks' MIDI drives the software players. **How the audio reaches the
mixer is an open design question with two answers, and neither has been tried:**

| | **A: a sound card per deck** | **B: the decks are MIDI-only** |
|---|---|---|
| Idea | each deck has its own PCM5102A and is its own USB sound card; its line out goes to one mixer input | audio comes from some other interface (for example the Sidekick's own USB audio); the decks only send MIDI |
| USB cables | one data cable **per deck** to the computer (two ports or a hub) | one per deck, or **one in total** with the one-cable bridge (section 4) |
| Deck hardware | PCM5102A, DAC strap fitted, a line-out jack | nothing extra: no DAC fitted (or `dacMode` OFF) |
| What the DJ app must do | play deck 1 to "EP-DJ3000 Deck A" and deck 2 to "EP-DJ3000 Deck B": **two USB sound cards at once. No app documents this; untested** (`drivers/README.md`) | play deck 1 and deck 2 to two outputs of **one** multi-channel device: documented for Mixxx, VirtualDJ, djay Pro and rekordbox, untested here |
| One-cable mode | **not possible**: a deck with no USB host has no USB audio stream, so its DAC is silent | works (MIDI only) |
| Uses the firmware's USB audio / DAC code | yes, and that code is what has never met a host | no |

First thing to try: **B with Mixxx** (Preferences > Sound Hardware > Outputs, Deck 1 and Deck 2), because it
needs nothing from the deck and one output device from the app. A is the only topology that exercises the DAC and
the USB audio function the firmware implements, and the one the BOM is built around. Whether the Sidekick's USB
audio can serve B has **not been evaluated**: TE's store page for the EP-136 lists "8 ins/4 outs usb audio
interface and midi controller" and says nothing about which direction the numbers count, the USB audio class,
or how USB channels map to its two mixer channels; read its manual before buying cables. Its MIDI side is not
used (section 3).

### Repo layout

| Path | What |
|---|---|
| `protocol/` | `epdj3000.json` (contract), `test-vectors.json` (byte-exact oracle) |
| `tools/` | `gen_protocol.py`, `gen_vectors.py` (+ `--check` drift detectors), `e2e.sh` |
| `firmware/core/` | Portable C++17 firmware logic. No ESP-IDF. Host unit tests (`ctest`). |
| `firmware/esp32s3/` | ESP-IDF project: HAL, TinyUSB (MIDI + USB Audio 1.0), I2S, ESP-NOW, NVS, tasks; host checks in `tools/` |
| `macos/` | SwiftPM: pure-Swift libraries (build/test on Linux too) + CoreMIDI/AVAudio/SwiftUI app |
| `drivers/` | DJ-software mappings (Mixxx, VirtualDJ, djay Pro, rekordbox) |
| `hardware/` | Wiring, BOM, conductive-PLA platter guidance, what is not drawn yet |
| `docs/` | This file, `PROTOCOL.md`, `LINK.md`, `BRINGUP.md`, `E2E.md` |
| `field-jog/` | The earlier Pro Micro/AS5600 design: SCAD, STLs, sketch. Kept for reference. **Its shell does not fit this design** (below). |
| `src/`, `server/`, ... | The Field Jog 3D web viewer. **Untouched, unrelated to the firmware.** |

**Mechanics: there is no enclosure for this design yet.** The Field Jog shell was drawn for a Pico, a hall-sensor
loose fader and a bearing platter. Against what this firmware needs: its controller bay (21 x 51 mm) is too small
for an ESP32-S3-DevKitC-1 (about 70 x 25 mm); its USB window (12.5 x 7.5 mm) fits one micro-USB plug, not the
DevKit's two USB-C ports; it has one 6.3 mm jack hole labelled MIDI and none for the line out; the 8 x 64 mm
fader slot is for the magnet stem, with hall posts and no slide-pot mount; and its platter is a plain disc, with no
sectors, gaps, contacts or wire passages. The conductive-PLA sector platter, the headline feature, has no
printable file. [`hardware/README.md`](../hardware/README.md) lists what has to be drawn.

## 2. Decisions (made so we could start; each is cheap to reverse)

1. **ESP32-S3**, not classic ESP32. USB device mode (MIDI + audio) needs native USB OTG, which only the S2/S3 have; the S3 also has 14 capacitive touch pads, BLE 5, and ESP-NOW. This is the single biggest hardware assumption, and the pin plan (`board_pins.h`) is written for the ESP32-S3-DevKitC-1.
2. **Platter sensing is a build-time/config choice** (`platterSource`), because "conductive-PLA platter" can mean two things:
   * `TOUCH_RING` *(default)* - the platter is printed in 3-8 conductive sectors, each wired to a touch pad. Finger angle = phasor sum of sector activations; motion between samples = jog. No bearing, no magnet, no encoder. Finger-slide scratching on a **fixed** disc.
   * `AS5600` - the earlier mechanical design: free-spinning platter on a 683ZZ bearing with a diametric magnet over an AS5600. The platter (or one pad) still provides touch/hold sensing. How a *rotating* conductive platter reaches that pad (a slip contact, spring pin or brush) is not designed: see `hardware/PLATTER.md`.
   * `TOUCH_HOLD_ONLY` - touch detect only, no rotation.
   If you intended only one of these, delete the other later; the abstraction is ~one class.
3. **Slide pot** -> 12-bit ADC (ADC1 only; ADC2 is unusable while the radio is on). 14-bit MIDI out. The role is configurable and **defaults to CHANNEL_VOLUME** (CC 7/39; contract v1.1), so two units never fight over one crossfader. That default is inaudible in VirtualDJ, djay Pro and rekordbox with an external mixer; set `PITCH` there (`drivers/README.md`).
4. **DAC**: I2S, PCM5102A-class (no I2C, no MCLK) as the reference part. `dacMode` AUTO means **a DAC-present strap is fitted**: a jumper from GPIO14 to GND, sampled at boot. A scan of known I2C codec addresses exists but is compiled out by default (`EPDJ_DAC_PROBE_I2C`, off); with no strap the unit is MIDI-only. Descriptors can't change after enumeration, so a mode change reboots.
5. **USB audio = USB Audio Class 1.0** (stereo, 48 kHz, 16-bit, asynchronous, with a feedback endpoint and a mute + volume feature unit). The ESP32-S3 is a full-speed-only device, and at full speed UAC1 is the class TinyUSB itself uses: its full-speed speaker-with-feedback example is UAC1 (UAC2 only on high-speed devices), and its UAC2 driver sends the feedback value as four bytes 16.16 whatever the bus speed, whereas a full-speed host expects three bytes 10.14, which is what its UAC1 driver sends. It is intended to be class-compliant, so a host should bind its in-box UAC1 driver and nothing needs installing; **it has not been tried on any host** (`BRINGUP.md`). Async + feedback endpoint: drift between the USB 1 kHz clock and the I2S clock is the classic failure mode of DIY USB DACs, so the device measures its ring fill, tells the host its real consumption rate, and slips single samples in software for hosts that ignore the feedback.
6. **MIDI**: Deck A = ch 1, Deck B = ch 2. Jog is relative CC 22 at 1024 ticks/rev; touch note; fader/knob 14-bit. Jog (CC 22), the knob (CC 24) and the CUE/REV notes (36/37) are the same as the earlier Field Jog map; the slide pot is not: it sends CC 7/39 by default, and the Field Jog's CC 23 is now the CROSSFADER role (CC 23/55, 14-bit). See `PROTOCOL.md`.
7. **Link**: ESP-NOW between decks (TE's own vocabulary is USB-C MIDI, 3.5 mm TRS MIDI and BLE MIDI; we use TRS for external gear and radio for deck-to-deck). BLE-MIDI is **not** in v1: its connection interval adds 7-15 ms of jitter, which is bad for scratching. It's a fine roadmap item for transport-only control.
8. **TRS MIDI** is **Type A** (MIDI Association spec for 3.5 mm). Verify against your EP-133 before wiring; if its jack is Type B, swap tip/ring (one-line change in `hardware/`).
9. **Config**: everything tunable is an int32 key in the contract (`config.keys`), live-settable, persisted to NVS on `SAVE_CONFIG`. Calibration is host-driven.
10. **No SysEx secrets, no network services, no cloud.** The device is a USB peripheral; the radio link carries only MIDI/time data (optional ESP-NOW encryption is documented, off by default).

## 3. The mixer: what we know and what we do not

From TE's product page the **EP-136 K.O.-SIDEKICK** is "a two channel mixer, audio interface and fx processor", USB-C or 2xAAA powered, and units "link" to expand channels. The store page lists "8 ins/4 outs usb audio interface and midi controller" and "two 3.5 mm stereo inputs", a "3.5 mm stereo aux input", a "3.5 mm main output" and a "3.5 mm cue output". We **do not** have its MIDI/USB implementation chart, so v1 treats it as an **analog mixer** whose USB audio is a possible audio path (topology B, not evaluated):

* Audio, topology A: deck line out -> Sidekick input (3.5 mm). Zero protocol knowledge needed.
* Control: none. Its faders/EQ are analog/physical, which is the point of a hardware mixer.
* If TE publishes MIDI for the Sidekick (or you capture it), add a `mixer` profile; the link layer reserves a peer slot for non-deck devices.

Mechanically, the Field Jog shell has Technic-pin holes at positions the author chose so decks could bolt to either side of a Sidekick. **Those positions have not been checked against real EP hardware** (`field-jog/README.md` says no EP unit was at hand for the rail), and the shell itself does not fit this design (section 1).

## 4. "Seamless" 2-deck + mixer: what v1 actually delivers

1. **Zero-config identity**: each unit is Deck A or B (hold CUE+REV 5 s to swap), exposed in the USB name and MIDI channel, so two units on one Mac never collide and one mapping file serves both.
2. **Auto link**: units find each other over ESP-NOW with no menus, elect roles, and share tempo/transport. Units only link within the same `linkGroup`.
3. **One-cable mode**: a deck with no USB host (power-only) bridges its cable-0 notes and CCs through the other deck's USB (cable 2). **MIDI only**: a deck without a host has no USB audio stream, so its DAC is silent and its audio must come another way (topology B). SysEx, clock and MTC are not bridged.
4. **Rig view** in the macOS app shows both decks, link health, audio routing and a one-click test (tone to each deck, jog/fader/button check) **when both decks are on USB**. A bridged deck is listed as "Not on USB": the app only monitors cable 2, so in one-cable mode it cannot play with, display or calibrate Deck B. The app has never been compiled (`macos/README.md`).
5. **Documented DJ-app routing** for deck 1 and deck 2 audio (`drivers/README.md`); every route is untested.

**Not in v1 (honest scope-back):** audio over the radio link (ESP-NOW cannot reliably carry 1.5 Mbit/s of real-time PCM); a shared single USB audio device exposing 4 channels (needs a second I2S path or TDM wiring between decks - a hardware decision); BLE-MIDI; mixer MIDI control; firmware OTA (update over the DevKit's UART port or BOOT + reset; `REBOOT(bootloader)` over USB exists but has never been tried).

## 5. Verification matrix (what "done" means)

| Component | How verified here | NOT verified here |
|---|---|---|
| Wire contract | generator `--check`, C++/Swift/JS compile, hand-checked vectors | - |
| Firmware core logic | host unit tests incl. shared vectors, simulation of sensors, sanitizers | real sensor behavior on printed conductive PLA |
| End to end ([`E2E.md`](E2E.md)) | the real firmware `Engine` behind a text pipe with simulated sensors and a lossy radio, driven by the real Swift client libraries and the real Mixxx script on a mock engine | hardware, CoreMIDI, the real DJ apps. Open findings E2E-1..3 are listed there |
| ESP-IDF layer | **compiles and links** for ESP32-S3 via PlatformIO and `idf.py` (ESP-IDF 6.1.0, esp_tinyusb 3.0.0 = TinyUSB 0.21.0; real headers); host checks of its pure logic (`firmware/esp32s3/tools/host_checks`) and of the USB descriptors against TinyUSB (`tools/crosscheck_usb_desc`) | running on a chip; USB enumeration; I2S audio quality; ESP-NOW latency |
| USB audio | descriptor unit-tested; bytes identical to TinyUSB's own `TUD_*` macros; TinyUSB's real audio driver run on the host against the descriptors and the firmware's callbacks (feedback bytes, endpoints, mute/volume); feedback-controller logic unit-tested | enumeration on macOS, Windows, Linux, iPadOS; glitch-free playback; how each host treats the feedback endpoint |
| Radio link | `LINK.md` section 12: simulated air, simulated clocks | any real radio: latency, jitter, loss, airtime, coexistence |
| Swift libraries | `swift test` on Linux | - |
| macOS app (CoreMIDI/AVAudio/SwiftUI) | pure logic tested; the Foundation-only part of the Kit compiled and tested; the rest **type-checked against hand-written stand-ins for the Apple frameworks, written from memory** | **never compiled against the Apple SDKs, never run** |
| DJ mappings | Mixxx script run against a mock engine; XML well-formedness; numbers cross-checked to the contract | import into the real apps (Mixxx included) |
| Enclosure | nothing: none exists for this design (section 1) | everything |

The "not verified" column is the first thing to do when hardware is in hand. `docs/BRINGUP.md` is the ordered
checklist.

## 6. Order of bring-up (recommended)

1. Flash firmware, watch `pio device monitor` (or `idf.py monitor`) logs: sensors alive, raw touch values per sector.
2. macOS app -> Test tab: telemetry view. Calibrate fader, knob, touch ring; spin direction. (No Mac, or the app does not compile: the same steps with ready-made SysEx frames are in `BRINGUP.md`, "Without the macOS app".)
3. MIDI monitor: confirm jog/touch/buttons; load a track in the app's built-in player, scratch.
4. Add the DAC; confirm "EP-DJ3000 Audio" appears; play the app's test tone.
5. Second unit: confirm link, then try one-cable bridging.
6. Load the Mixxx/VirtualDJ mapping.

## 7. Using a unit as a MIDI clock / transport master

For a DAW, a drum machine or the EP-133 over the TRS jack. **A unit is either a DJ controller or a clock master, not both**:
in `SYNC_MASTER` the unit sends clock instead of jog (the platter becomes a tempo nudge, CUE a tap-tempo button, REV
stop + rewind). Everything below is implemented and simulation-tested; **none of it has been tried against a real
DAW, a real EP-133, or any DJ app** (none of the four DJ apps consumes the clock or MTC, `drivers/README.md`).

| Step | How |
|---|---|
| Enter the mode | hold CUE + REV for 2-5 s, release. Each chord steps the mode DJ -> `SYNC_MASTER` -> `SYNC_FOLLOW` -> DJ. The LED flashes white on the beat in a clock mode. |
| Or set it from a host | `SET_CONFIG mode = 1` (frame in `BRINGUP.md`, "Without the macOS app"), or Settings > mode in the Mac app |
| Make it stick | the chord changes the live mode only. Send `SAVE_CONFIG` (Settings > *Save to unit*) and the unit boots in that mode |
| Where the clock goes | `clockOutMask` (default 3): USB cable 0 and the TRS jack. Bit 2 adds the radio, which sends TRANSPORT packets, never `0xF8` bytes |
| What the controls do | CUE: tap tempo. Platter tap: play / pause. Platter spin while touched: +0.1 BPM per 32 ticks clockwise. REV: stop and rewind (Stop, SPP 0). Table: `PROTOCOL.md` section 2.5 |
| Timecode | `SET_CONFIG mtcEnable = 1` (key 24); `mtcFps` (key 25, default 3 = 30 fps). MIDI Time Code only; this is **not** DVS control audio for timecode vinyl |
| Tempo from a host | CC 20/52 (BPM x 10), CC 21 nudge, note 41 tap, or `SET_CONFIG bpmX10` |
| Start/stop from a host | the `transport` CC 29 (0 stop, 1 play, 2 pause). The `play` note never starts a clock |
| Back to a controller | chord again until DJ, or `SET_CONFIG mode = 0`; `SAVE_CONFIG` to keep it |

Followers: a second unit in `SYNC_FOLLOW` locks to the master over the radio (or to external clock on USB/TRS). With
the radio, "locks" means a TRANSPORT packet phase-aligns its beat LED; all link timing is simulated
(`LINK.md` section 7).
