# EP-DJ3000

A 3D-printed scratch deck for the desk of an EP-133 K.O. II and an EP-136 K.O.-SIDEKICK. An **ESP32-S3** shows up to a
computer as a USB MIDI controller (optionally also a USB sound card). You scratch on a **free-spinning platter on a bearing**:
a magnet on its axle over an **AS5600** magnetic encoder gives the angle, and one touch pad tells the DJ software whether a
hand is holding the record. There is a slide pot, an orange knob, CUE and REV, a TRS MIDI jack for the EP-133, and
two decks find each other over radio. The earlier idea, a fixed platter printed in conductive-PLA sectors that you scratch
by sliding a finger, is kept in the firmware as an **experimental** option (`platterSource = TOUCH_RING`). This repo holds
the firmware, a macOS test app, DJ-software mappings, and the wire protocol that ties them together.

Not a Teenage Engineering product and not affiliated with them.

> ## Read this first: nothing here has run on hardware
>
> * The firmware **compiles and links** for the ESP32-S3 and its logic is tested in simulation. It has **never run on a chip.**
> * **No AS5600 has ever been read by this firmware, and the hold pad has never been tried on a spinning platter.** Whether a
>   touch pad can see a hand on a platter that turns, through a rotating contact nobody has designed, is the biggest
>   unknown left. The experimental conductive-PLA ring has never produced a real signal either.
> * The macOS app has **never been compiled against the Apple SDKs** (only against hand-written stand-ins).
> * No DJ-app mapping has been **loaded into the real app**, Mixxx included.
> * There is **no enclosure** for this design. The Field Jog shell in `field-jog/` has the platter mechanics (bearing pocket,
>   M3 axle, magnet, AS5600 pad) but its bay, USB window, jack hole and fader slot do not fit this design, and nothing draws
>   a contact from the spinning platter to the hold pad ([`hardware/README.md`](hardware/README.md)).

```
   DJ software  (Mixxx, VirtualDJ, djay Pro, rekordbox)
        |  USB-C: MIDI, and optionally USB audio
   +----+--------------+     ESP-NOW radio: link, clock,    +--------------------+
   | EP-DJ3000 Deck A  | <--------- MIDI bridge -----------> |  EP-DJ3000 Deck B  |
   +----+--------+-----+                                     +--------------------+
        |        | line out (optional DAC) -> mixer channel
        | TRS MIDI -> EP-133 K.O. II          EP-136 K.O.-SIDEKICK = analog mixer
```

## What is where

| Path | What |
|---|---|
| `protocol/`, `tools/` | The wire contract (`epdj3000.json`, v1.3), its generators and the byte-exact test vectors. Everything generated carries a `--check` drift detector. |
| `firmware/core/` | Portable C++17 firmware logic, no ESP-IDF, host tests |
| `firmware/esp32s3/` | ESP-IDF project: sensors, TinyUSB (MIDI + USB Audio 1.0), I2S DAC, TRS MIDI, ESP-NOW, NVS |
| `macos/` | SwiftPM: Linux-testable protocol/engine libraries plus a CoreMIDI/AVAudio/SwiftUI test app |
| `drivers/` | Mixxx, VirtualDJ, djay Pro, rekordbox mappings and the compatibility matrix |
| `hardware/` | Wiring, BOM, platter guidance, what is not drawn yet |
| `docs/` | `PROTOCOL`, `ARCHITECTURE`, `LINK`, `BRINGUP`, `E2E` |
| `field-jog/` | The Pro Micro/AS5600 deck: SCAD, STLs, sketch. Its platter mechanics are the ground truth for the AS5600 platter; its shell does not fit the rest. |
| `src/ server/ public/ scripts/ .grok/ migrations/` and the root `package.json`, `vite.config.ts`, `vercel.json`, `AGENTS.md`, ... | The **unrelated** Field Jog 3D web viewer. Not part of the firmware. |

## What is verified and what is not

| Part | Evidence | Not verified |
|---|---|---|
| Wire contract | generators `--check`; C++, Swift and JS built from it; vectors reproduced by every implementation | - |
| Firmware core logic | 28 host test binaries under ASan/UBSan, simulated sensors (an AS5600 angle with one hold pad, and the experimental ring), one simulated hour on two linked units | any real sensor: an AS5600, a hold pad on a spinning platter, printed conductive PLA |
| ESP32-S3 firmware | `pio run` builds and links clean (ESP-IDF 6.1.0); pure logic and USB descriptors host-checked against TinyUSB itself | running on a chip: USB enumeration, I2S, touch counts, ESP-NOW, timing |
| End to end | real `Engine` + real Swift client + real Mixxx script on a mock Mixxx, simulated hands ([`docs/E2E.md`](docs/E2E.md)) | hardware, CoreMIDI, real DJ apps |
| USB audio | descriptor and class driver run on the host against the firmware's callbacks | **any real host** (macOS, Linux, Windows, iPadOS), glitch-free playback |
| Radio link | `docs/LINK.md`: simulated air and clocks | real latency, jitter, loss; every figure there is a model |
| macOS app | pure logic: 347 tests on Linux. The Foundation-only part of the Kit is compiled and tested | **never built with Xcode**, never run: expect compile errors (`macos/README.md` lists 11 residual risks) |
| DJ mappings | Mixxx script on a mock; the VirtualDJ XML and the djay/rekordbox files checked against the contract's numbers | import into the real apps; scratching in rekordbox is not mapped at all (jog rows disabled) |
| Platter angle (AS5600, the default) | firmware core: exact integer jog and `jogInvert` with a simulated angle; host-side encoder check, direction and hold-pad wizards on Linux against an in-memory unit | an AS5600 on a real I2C bus, the 1 to 3 mm magnet gap, a real spin, the log's magnet status |
| Hold pad on the spinning platter | firmware core: touch, hold and tap from one simulated pad; the wizard's thresholds on simulated readings | **everything**: a contact across the rotating joint, signal to noise, dropouts while it turns, whether a hand registers at all |
| Touch ring (experimental) | simulated ring (clean 40 per-mille bump, uniform noise) | everything about a real conductive-PLA ring: signal-to-noise, sector size, finger coverage |
| Enclosure | nothing | everything |
| CI | none (no `.github/`). `tools/check-all.sh` runs every portable check locally | - |

DJ-app support at a glance (full matrix, evidence grades and caveats: [`drivers/README.md`](drivers/README.md)). Only open-source
Mixxx has executable mapping logic; VirtualDJ and djay Pro are proprietary.

| | Mixxx | VirtualDJ | djay Pro | rekordbox |
|---|---|---|---|---|
| Jog / scratch / touch | script, mock-tested | XML from the docs, never loaded | learn-it-yourself recipe | **not usable** (rows disabled) |
| Slide pot (default `PITCH`) | tempo (or per-deck volume cut) | tempo | tempo | tempo |
| Host tempo / transport / clock to the unit | no | no | no | no |

Serato and Traktor are not covered (Serato cannot map platters; Traktor's mapping format is undocumented).

**Scratching in these apps hangs on the touch note, not on motion.** Mixxx, VirtualDJ and djay's touch-aware plan go into
scratch mode on note 38. The AS5600 reports rotation with or without a hand on the platter, so on the default platter the
touch note comes from the one hold pad. If that pad does not reliably see a hand on the spinning platter, nothing scratches
(Mixxx falls back to pitch bend). Details: [`drivers/README.md`](drivers/README.md).

## Quick start

Every command below was run for this README (Linux, Swift 6.0.3, PlatformIO with ESP-IDF 6.1.0), from this checkout or a clean
copy of the firmware tree, except the macOS app row, which could not be.

| For | Command | Result when run |
|---|---|---|
| **Everything portable, one command** | `tools/check-all.sh` (options: `--list`, a subset such as `tools/check-all.sh drivers swift`, `--build-dir`, `--swift-scratch`, `--no-sanitize`, `--strict`; `--help`). Runs the checks behind the rows marked (*) below, as six steps (the end-to-end row is two), and prints a PASS / FAIL / SKIP table. A step whose tool is missing (python3, node, cmake and a C++ compiler, swift) is SKIPPED, shown as such and never counted as a pass | all six steps PASS in about two minutes on a warm Swift build; it does not run the ESP32-S3 build or host checks, which need PlatformIO |
| Contract drift (*) | `python3 tools/gen_protocol.py --check && python3 tools/gen_vectors.py --check` | `protocol artifacts up to date`, `test vectors up to date` |
| Firmware core (*) | `firmware/core/run_tests.sh --sanitize` (needs cmake and a C++17 compiler; builds in `firmware/core/build-san`; a few minutes) | 28 of 28 tests pass |
| ESP32-S3 build | `pio run -d firmware/esp32s3` (needs PlatformIO; the first run downloads the toolchain and `esp_tinyusb`) | builds, no warnings. Flash over the DevKit's **UART** port with `-t upload`, then follow [`docs/BRINGUP.md`](docs/BRINGUP.md) |
| ESP32-S3 host checks | `sh firmware/esp32s3/tools/host_checks/run.sh` and `sh firmware/esp32s3/tools/crosscheck_usb_desc/run.sh` (the second needs one `pio run` first for the `esp_tinyusb` download) | 14 tests; 64 checks |
| Swift libraries (*) | `cd macos && swift test` (any OS, Swift 5.9+) | 416 tests listed, 69 skipped (they need the simulator below), 0 failed; `check-all.sh` runs the other 347 and leaves the 69 to its two e2e steps |
| macOS app | `cd macos && swift run EPDJ3000App`, or `scripts/make-app.sh` then `open EP-DJ3000.app` (macOS 13+, Xcode 15+) | **not run: never compiled.** Fallback with no app: [`docs/BRINGUP.md`](docs/BRINGUP.md) section 2b |
| End to end (*) | `tools/e2e.sh` (builds the firmware simulator, runs the Node and Swift suites) | 33 Node and 69 Swift tests pass |
| DJ mappings (*) | `node drivers/test/run-all.mjs`, then install Mixxx's mapping first ([`drivers/README.md`](drivers/README.md)) | 6 steps pass; real-app behaviour unknown |

`swift test` and `tools/e2e.sh` were run with their build output redirected to scratch directories (`--scratch-path`, and
`EPDJ_E2E_BUILD` / `EPDJ_SWIFT_SCRATCH`); by default they build into `macos/.build` and `firmware/core/build-e2e`.

## Getting audio into the mixer: two topologies, neither tried

Details and the full comparison in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) section 1.

* **A, a sound card per deck.** Two USB cables, a PCM5102A and a line-out jack in each deck; the DJ app plays deck 1 and deck 2
  to two different USB sound cards, each into a mixer channel. **None of the four apps' documentation was found to describe driving two USB sound cards**, and a deck
  with no USB host (one-cable mode) has no audio.
* **B, MIDI-only decks.** No DAC. Audio comes from one multi-channel interface, possibly the Sidekick's own USB audio
  (TE lists "8 ins/4 outs"; how it maps to the mixer was not checked). One USB cable per deck, or **one in total** with Deck B
  bridged through Deck A over the radio (MIDI only; the Mac app cannot use a bridged deck).

First try: **B with Mixxx** (Deck 1 / Deck 2 outputs).

## First-run gotchas

* **Slide pot.** Out of the box it is a CDJ-style tempo fader (`faderRole = PITCH`, CC 25/57), the one role that does something
  in all four DJ apps behind an external mixer. `CHANNEL_VOLUME` (CC 7/39) is a per-deck scratch cut that is audible in Mixxx only,
  and `CROSSFADER` (CC 23/55) is for one unit into a single software mixer; either is one SysEx frame or the Mac app's Settings
  away ([`docs/BRINGUP.md`](docs/BRINGUP.md) section 2b). A unit that saved its config under contract v1.1 keeps `CHANNEL_VOLUME`
  until you load the defaults and save.
* **Deck B.** Hold CUE + REV for 5 s to swap Deck A/B (saves and reboots), or build the second unit with `EPDJ_FIRST_BOOT_DECK_B`
  (`pio run -d firmware/esp32s3 -t menuconfig`).
* **Two USB-C cables** on the DevKitC-1: UART to flash and read the log, the native USB port to the computer. After the first flash the
  native port is a MIDI device, not a serial port.
* **Audio needs the strap.** `dacMode` AUTO means a jumper from GPIO14 to GND is fitted; there is no automatic DAC detection by default.
  No strap, no USB audio.
* **LED.** The default build drives the DevKitC-1 v1.1 WS2812 on GPIO38; for a v1.0 board (GPIO48) pick the revision in `menuconfig` (`EPDJ_BOARD_REV`).
* **Calibrate before judging anything**: encoder check, platter direction, hold-pad thresholds, fader, knob (Mac app, or
  SysEx by hand). The wizard follows the unit's `platterSource`.
* **A unit that saved its config under contract v1.2 or earlier keeps `TOUCH_RING`** (the factory default became `AS5600`
  in v1.3). Settings > *Load defaults*, then *Save to unit*, or `SET_CONFIG platterSource = 0`
  ([`docs/BRINGUP.md`](docs/BRINGUP.md) section 2b).

## First things to prove on hardware, in order

1. **The AS5600 answers and its magnet status is "detected".** The boot log's platter line says whether the chip answered
   and how the magnet looks; the app's *Encoder check* spins the platter and says what to fix if the angle does not follow.
2. **Direction.** Spin clockwise; `jogTotal` must rise, otherwise flip `jogInvert` (the app's *Platter direction* step).
3. **A hold pad actually detects a hand on the spinning platter.** This is the biggest remaining unknown: the hold sensing has
   to cross a rotating joint (capacitive coupling to a fixed pad, or a spring or brush on the axle; neither tried:
   [`hardware/PLATTER.md`](hardware/PLATTER.md)), and DJ software enters scratch mode on the touch note it produces. Check it
   with the platter still, then turning, and write down the signal and the dropouts.
4. USB Audio 1.0 with its feedback endpoint on a real host (macOS first, then Linux, then Windows).
5. ESP-NOW latency and loss under the bridge and time sync.
6. `REBOOT(bootloader)` over USB. Never tried; the supported update paths are the DevKit's UART port and BOOT + reset.
7. Detecting a pulled USB cable without VBUS sense (one-cable mode depends on it).

The touch signal-to-noise experiment on real conductive PLA, which used to be step 1, only matters **if you return to the
ring** (`platterSource = TOUCH_RING`, [`docs/BRINGUP.md`](docs/BRINGUP.md) appendix A).

## Decisions for the owner to confirm

| Decision | Current default | Note |
|---|---|---|
| Board | ESP32-S3-DevKitC-1; the pin plan (`board_pins.h`) is invented for it, 21 GPIOs | no smaller board has been checked against it |
| Platter | **AS5600 for now** (`platterSource = AS5600`, contract v1.3): free-spinning platter on a 683ZZ bearing, diametric magnet over an AS5600, one touch pad for hold and tap | the owner's call, "for now". The fixed conductive-PLA sector ring (`TOUCH_RING`) is kept as an experimental option, code, tests and docs included. Open: the rotating contact from the platter to the hold pad is undesigned |
| Enclosure | none | the shell's platter well, bearing pocket and AS5600 pad are the part to keep; its Pico-sized bay, one micro-USB window and hall-fader slot do not fit, and there is no hold-pad contact. Draw the rest |
| Slide-pot default | `PITCH`, the tempo slider (contract v1.2; v1.1 had `CHANNEL_VOLUME`) | works in all four apps behind an external mixer, and two units never fight over one control. `CHANNEL_VOLUME` (audible in Mixxx only) and `CROSSFADER` stay selectable |
| USB audio class | UAC1 (the S3 is full-speed only) | meant to bind the in-box driver on macOS, Linux and Windows 10+, untried on any host; Windows wants ASIO for low latency |
| EP-136 | analog mixer; its MIDI is unknown | its USB audio is a candidate audio path |
| VBUS sense | not wired | add the divider now, or live with idle detection |
| USB IDs | `0x1209:0x0001`, the shared pid.codes **test** PID, also baked into the VirtualDJ file | get a real PID before sharing units |
| TRS MIDI | Type A | check the EP-133's jack |
| License | **none yet** | choose one |

## Known open issues

* The three disagreements the end-to-end harness found (E2E-1 `CAL_END` insets, E2E-2 stale `faderRole` in the Swift fold,
  E2E-3 the play LED in `SYNC_MASTER`) are fixed; [`docs/E2E.md`](docs/E2E.md) keeps their history.
* ADC conversions run with interrupts off (up to 400 us per ms) on the core that also serves USB; unmeasured.
* No CI configuration; `tools/check-all.sh` is the by-hand equivalent for everything portable.
* A bridged deck (one-cable mode) cannot be used, shown or calibrated from the Mac app.

## Not in v1

Audio over the radio; a shared 4-channel USB audio device; BLE-MIDI; mixer MIDI control (EP-136); firmware OTA; DVS timecode (the
MTC here is MIDI Time Code); Serato and Traktor mappings; a Windows ASIO driver; a clock/transport guide verified against a real
DAW (the steps are in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) section 7); an enclosure; CI.

## Docs

[`PROTOCOL`](docs/PROTOCOL.md) (wire contract) - [`ARCHITECTURE`](docs/ARCHITECTURE.md) (decisions, topologies, verification matrix, clock-master
guide) - [`LINK`](docs/LINK.md) (radio link) - [`BRINGUP`](docs/BRINGUP.md) (first power-up, SysEx without the app) -
[`E2E`](docs/E2E.md) (conformance harness) - [`hardware/`](hardware/README.md) (wiring, platter, BOM) -
[`firmware/core`](firmware/core/README.md), [`firmware/esp32s3`](firmware/esp32s3/README.md), [`macos`](macos/README.md),
[`drivers`](drivers/README.md).

## License

None yet, so no license is granted until the owner adds one.
