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
> * There is **no finished enclosure** for this design, and the shell has never been printed or fitted. The owner redrew the Field
>   Jog shell to 16 mm (jack row, battery bay, faceplate); shell commit `15417b6` seats a headerless ESP32-S3-DevKitC-1 and
>   lowers the AS5600 pad to 3.5 mm. Still wrong for this deck: the hall-fader slot has no slide-pot mount, PLAY / LOOP IN /
>   LOOP OUT, their LEDs, the OLED, line in and the headphone jack have no firmware, the AS5600 stack-up margin is thin, and
>   nothing draws a contact from the spinning platter to the hold pad ([`hardware/README.md`](hardware/README.md),
>   "Shell controls vs firmware").

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
| `protocol/`, `tools/` | The wire contract (`epdj3000.json`, v1.4), its generators and the byte-exact test vectors. Everything generated carries a `--check` drift detector. |
| `firmware/core/` | Portable C++17 firmware logic, no ESP-IDF, host tests |
| `firmware/esp32s3/` | ESP-IDF project: sensors, TinyUSB (MIDI + USB Audio 1.0), I2S DAC, TRS MIDI, ESP-NOW, NVS |
| `macos/` | SwiftPM: Linux-testable protocol/engine libraries plus a CoreMIDI/AVAudio/SwiftUI test app |
| `drivers/` | Mixxx, VirtualDJ, djay Pro, rekordbox mappings and the compatibility matrix |
| `hardware/` | Wiring, BOM, platter guidance, what is not drawn yet |
| `docs/` | `PROTOCOL`, `ARCHITECTURE`, `LINK`, `BRINGUP`, `E2E`, `REFERENCE-ORIGINAL-FIRMWARE` |
| `field-jog/` | The Field Jog shell and Pro Micro sketch, redrawn by the owner to 16 mm (jack row, battery bay, faceplate, a lip for a headerless ESP32-S3-DevKitC-1 since `15417b6`). Its platter mechanics are the ground truth for the AS5600 platter; several of its controls have no firmware (table in [`hardware/README.md`](hardware/README.md#shell-controls-vs-firmware)). `firmware/rp2040/` there is a separate experiment (open question below). |
| `src/ server/ public/ scripts/ .grok/ migrations/` and the root `package.json`, `vite.config.ts`, `vercel.json`, `AGENTS.md`, ... | The **unrelated** Field Jog 3D web viewer. Not part of the firmware. |

## What is new (contract v1.4)

Three ideas from the original Field Jog firmware (the owner pasted its sketch; **no code was copied**, and what we took, what
we already had and what we declined is in [`docs/REFERENCE-ORIGINAL-FIRMWARE.md`](docs/REFERENCE-ORIGINAL-FIRMWARE.md)):

* **AS5600 chip configuration.** The firmware writes the chip's configuration at start-up (watchdog off, hysteresis 0, slow
  filter 16x, fast-filter threshold setting 7), reads the chip's filtered angle register instead of the raw one, and asks again
  every 500 ms if no chip answered, so a wire fixed after power-up is found without a reboot. Whether the filter helps or only
  adds lag has not been measured, and that the filters act on the register we read is the original's assumption, not something
  read in a datasheet: `EPDJ_AS5600_FILTERED_ANGLE` in `menuconfig` switches back to the raw register for a bench comparison.
* **Sticky touch release.** After a touch longer than 7 s the unit accepts a release only once the platter has been still for
  80 ms, so a hold pad that drops out in the middle of a long scratch does not throw the DJ app out of scratch mode and straight
  back in. Cost: a hand lifted off a platter that is still spinning keeps `touch` on until it stops. Simulation-tested only.
* **Untouched-jog policy**, a new config key `jogUntouched` (id 33, default `AUTO`). A free-spinning platter coasts for seconds
  after you let go, and sent as jog that would bend the pitch the whole time; the original also sends jog only while touched.
  So the unit sends untouched motion **until the hold pad has registered its first touch since power-up** (the encoder works
  before the pad does), and from then on sends jog **only under a hand**. The proof is not stored: it starts over at every reboot
  and whenever `platterSource` changes. `REPORT` always sends untouched motion (a nudge or pitch bend; how to set it:
  [`drivers/README.md`](drivers/README.md)), `SUPPRESS` never does, and `Telemetry.jogTotal` counts everything, so the app's
  calibration steps need no hand on the pad. The Mixxx script's untouched pitch-bend path is therefore dormant once the pad has
  worked, unless you choose `REPORT`. In the macOS app the setting is a picker in Settings, and the Decks
  tab draws the platter from `Telemetry.angle` while the telemetry stream runs, so it still turns when nothing is being sent.

It also records the owner's redrawn shell (16 mm; jack row, battery bay, faceplate) and the RP2040 experiment that came with it,
under [Known open issues](#known-open-issues).

## What is verified and what is not

| Part | Evidence | Not verified |
|---|---|---|
| Wire contract | generators `--check`; C++, Swift and JS built from it; vectors reproduced by every implementation | - |
| Firmware core logic | 29 host test binaries under ASan/UBSan, simulated sensors (an AS5600 angle with one hold pad, and the experimental ring), one simulated hour on two linked units | any real sensor: an AS5600, a hold pad on a spinning platter, printed conductive PLA |
| ESP32-S3 firmware | `pio run` builds and links clean (ESP-IDF 6.1.0); pure logic and USB descriptors host-checked against TinyUSB itself | running on a chip: USB enumeration, I2S, touch counts, ESP-NOW, timing |
| End to end | real `Engine` + real Swift client + real Mixxx script on a mock Mixxx, simulated hands ([`docs/E2E.md`](docs/E2E.md)) | hardware, CoreMIDI, real DJ apps |
| USB audio | descriptor and class driver run on the host against the firmware's callbacks | **any real host** (macOS, Linux, Windows, iPadOS), glitch-free playback |
| Radio link | `docs/LINK.md`: simulated air and clocks | real latency, jitter, loss; every figure there is a model |
| macOS app | pure logic: 358 tests on Linux. The Foundation-only part of the Kit is compiled and tested | **never built with Xcode**, never run: expect compile errors (`macos/README.md` lists 11 residual risks) |
| DJ mappings | Mixxx script on a mock; the VirtualDJ XML and the djay/rekordbox files checked against the contract's numbers | import into the real apps; scratching in rekordbox is not mapped at all (jog rows disabled) |
| Platter angle (AS5600, the default) | firmware core: exact integer jog and `jogInvert` with a simulated angle, the `jogUntouched` policy; host-side encoder check, direction and hold-pad wizards on Linux against an in-memory unit; the Decks-tab platter angle as pure logic | an AS5600 on a real I2C bus, the 1 to 3 mm magnet gap, a real spin, the log's magnet status, the chip's filter settings and filtered-angle register |
| Hold pad on the spinning platter | firmware core: touch, hold and tap from one simulated pad; the sticky release; the wizard's thresholds on simulated readings | **everything**: a contact across the rotating joint, signal to noise, dropouts while it turns (and whether the sticky release covers them), whether a hand registers at all |
| Touch ring (experimental) | simulated ring (clean 40 per-mille bump, uniform noise) | everything about a real conductive-PLA ring: signal-to-noise, sector size, finger coverage |
| Enclosure | nothing (the redrawn 16 mm shell, `15417b6`, has not been printed or fitted) | everything, starting with the AS5600 stack-up, which is thin on paper |
| CI | none (no `.github/`). `tools/check-all.sh` runs every portable check locally | - |

DJ-app support at a glance (full matrix, evidence grades and caveats: [`drivers/README.md`](drivers/README.md)). Only open-source
Mixxx has executable mapping logic; VirtualDJ and djay Pro are proprietary.

| | Mixxx | VirtualDJ | djay Pro | rekordbox |
|---|---|---|---|---|
| Jog / scratch / touch (untouched jog: before the pad's first touch, and with `jogUntouched` = `REPORT`) | script, mock-tested | XML from the docs, never loaded | learn-it-yourself recipe | **not usable** (rows disabled) |
| Slide pot (default `PITCH`) | tempo (or per-deck volume cut) | tempo | tempo | tempo |
| Host tempo / transport / clock to the unit | no | no | no | no |

Serato and Traktor are not covered (Serato cannot map platters; Traktor's mapping format is undocumented).

**Scratching in these apps hangs on the touch note, not on motion.** Mixxx, VirtualDJ and djay's touch-aware plan go into
scratch mode on note 38. The AS5600 senses rotation with or without a hand on the platter, so on the default platter the
touch note comes from the one hold pad, and since contract v1.4 a unit whose pad has registered a touch sends jog only while that
pad says touched (`jogUntouched` = `AUTO`). If the pad never registers a hand on the spinning platter, nothing scratches; `AUTO`
keeps sending the platter as a pitch-bend wheel (Mixxx) because the pad has not proven itself, and `jogUntouched` = `REPORT`
does so even after it has. Details: [`drivers/README.md`](drivers/README.md).

## Quick start

Every command below was run for this README (Linux, Swift 6.0.3, PlatformIO with ESP-IDF 6.1.0), from this checkout or a clean
copy of the firmware tree, except the macOS app row, which could not be.

| For | Command | Result when run |
|---|---|---|
| **Everything portable, one command** | `tools/check-all.sh` (options: `--list`, a subset such as `tools/check-all.sh drivers swift`, `--build-dir`, `--swift-scratch`, `--no-sanitize`, `--strict`; `--help`). Runs the checks behind the rows marked (*) below, as six steps (the end-to-end row is two), and prints a PASS / FAIL / SKIP table. A step whose tool is missing (python3, node, cmake and a C++ compiler, swift) is SKIPPED, shown as such and never counted as a pass | all six steps PASS in about two minutes on a warm Swift build; it does not run the ESP32-S3 build or host checks, which need PlatformIO |
| Contract drift (*) | `python3 tools/gen_protocol.py --check && python3 tools/gen_vectors.py --check` | `protocol artifacts up to date`, `test vectors up to date` |
| Firmware core (*) | `firmware/core/run_tests.sh --sanitize` (needs cmake and a C++17 compiler; builds in `firmware/core/build-san`; a few minutes) | 29 of 29 tests pass |
| ESP32-S3 build | `pio run -d firmware/esp32s3` (needs PlatformIO; the first run downloads the toolchain and `esp_tinyusb`) | builds, no warnings. Flash over the DevKit's **UART** port (in the shell: with the deck open) with `-t upload`, then follow [`docs/BRINGUP.md`](docs/BRINGUP.md) |
| ESP32-S3 host checks | `sh firmware/esp32s3/tools/host_checks/run.sh` and `sh firmware/esp32s3/tools/crosscheck_usb_desc/run.sh` (the second needs one `pio run` first for the `esp_tinyusb` download) | 31 host tests, 0 failed; 64 checks (the USB-descriptor cross-check, last run before v1.4 and not re-run) |
| Swift libraries (*) | `cd macos && swift test` (any OS, Swift 5.9+) | 433 tests listed, 75 skipped (they need the simulator below), 0 failed; `check-all.sh` runs the other 358 and leaves the 75 to its two e2e steps |
| macOS app | `cd macos && swift run EPDJ3000App`, or `scripts/make-app.sh` then `open EP-DJ3000.app` (macOS 13+, Xcode 15+) | **not run: never compiled.** Fallback with no app: [`docs/BRINGUP.md`](docs/BRINGUP.md) section 2b |
| End to end (*) | `tools/e2e.sh` (builds the firmware simulator, runs the Node and Swift suites) | 41 Node and 75 Swift tests pass |
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
  native port is a MIDI device, not a serial port. In the shell only the native port reaches the window
  ([Known open issues](#known-open-issues), flashing).
* **Audio needs the strap.** `dacMode` AUTO means a jumper from GPIO14 to GND is fitted; there is no automatic DAC detection by default.
  No strap, no USB audio.
* **LED.** The default build drives the DevKitC-1 v1.1 WS2812 on GPIO38; for a v1.0 board (GPIO48) pick the revision in `menuconfig` (`EPDJ_BOARD_REV`).
* **Jog without a hand, until the pad has worked.** Out of the box (`jogUntouched` = `AUTO`) the unit sends jog for a platter
  nobody is touching **until the hold pad has registered its first touch since power-up**. So on the bench, with the pad not
  connected yet, spinning the platter shows jog on a MIDI monitor and a DJ app sees a pitch nudge: the encoder works before the
  pad does. Once the pad has seen a hand, a coasting platter is silent (by design: it would bend the pitch for seconds) until the
  next reboot. Set `jogUntouched` = `REPORT` to always send it (Settings > Platter, or the SysEx frame in
  [`drivers/README.md`](drivers/README.md)), `SUPPRESS` to never send it. The app's wizards and its Decks tab read telemetry and
  do not care.
* **Calibrate before judging anything**: encoder check, platter direction, hold-pad thresholds, fader, knob (Mac app, or
  SysEx by hand). The wizard follows the unit's `platterSource`.
* **A unit that saved its config under contract v1.2 or earlier keeps `TOUCH_RING`** (the factory default became `AS5600`
  in v1.3). Settings > *Load defaults*, then *Save to unit*, or `SET_CONFIG platterSource = 0`
  ([`docs/BRINGUP.md`](docs/BRINGUP.md) section 2b).

## First things to prove on hardware, in order

1. **The AS5600 answers and its magnet status is "detected".** The boot log's platter line says whether the chip answered
   and how the magnet looks (in the shell it also judges the magnet gap: bring an M3x5 and an M3x6); the app's *Encoder check*
   spins the platter and says what to fix if the angle does not follow.
2. **Direction.** Spin clockwise; `jogTotal` must rise, otherwise flip `jogInvert` (the app's *Platter direction* step).
3. **A hold pad actually detects a hand on the spinning platter.** This is the biggest remaining unknown: the hold sensing has
   to cross a rotating joint (capacitive coupling to a fixed pad, or a spring or brush on the axle; neither tried:
   [`hardware/PLATTER.md`](hardware/PLATTER.md)), and DJ software enters scratch mode on the touch note it produces. Check it
   with the platter still, then turning, and write down the signal and the dropouts.
4. USB Audio 1.0 with its feedback endpoint on a real host (macOS first, then Linux, then Windows).
5. ESP-NOW latency and loss under the bridge and time sync.
6. `REBOOT(bootloader)` over USB, as soon as USB enumerates and **before the deck is closed for good**
   ([`docs/BRINGUP.md`](docs/BRINGUP.md) section 2c). Never tried on a chip; in the shell it is the only update path through the
   window, the alternatives being the UART port or BOOT + reset with the deck open.
7. Detecting a pulled USB cable without VBUS sense (one-cable mode depends on it).

The touch signal-to-noise experiment on real conductive PLA, which used to be step 1, only matters **if you return to the
ring** (`platterSource = TOUCH_RING`, [`docs/BRINGUP.md`](docs/BRINGUP.md) appendix A).

## Decisions for the owner to confirm

| Decision | Current default | Note |
|---|---|---|
| Board and firmware path | ESP32-S3-DevKitC-1, **decided by the owner** ("selected s3-dev-kit"); headerless in the shell since `15417b6`. The pin plan (`board_pins.h`) is written for it, 21 GPIOs | RESOLVED: the Audio Kit lip was dropped (classic ESP32, no USB device mode). The pin plan was checked on paper against Espressif's header tables: every planned pin is on a header and none is reserved ([`hardware/WIRING.md`](hardware/WIRING.md#checked-against-espressifs-header-tables)); not checked on a board. No smaller board has been checked against it |
| Platter | **AS5600 for now** (`platterSource = AS5600`, contract v1.3): free-spinning platter on a 683ZZ bearing, diametric magnet over an AS5600, one touch pad for hold and tap | the owner's call, "for now". The fixed conductive-PLA sector ring (`TOUCH_RING`) is kept as an experimental option, code, tests and docs included. Open: the rotating contact from the platter to the hold pad is undesigned |
| Enclosure | none finished | the owner's redrawn 16 mm shell (`15417b6`) seats a headerless DevKitC-1 and keeps the platter well, bearing sleeve and a 3.5 mm AS5600 pad with an M3x6 axle (margin thin: buy an M3x5 too); its hall-fader slot has no slide-pot mount, several controls and jacks have no firmware, and there is no hold-pad contact ([`hardware/README.md`](hardware/README.md)) |
| Untouched platter jog | `jogUntouched` = `AUTO` (contract v1.4): sent until the hold pad has registered a touch since power-up, then not sent | `REPORT` always sends it (a nudge), `SUPPRESS` never does. The unit cannot measure that the pad works, only that it has reported a touch (a phantom touch counts too); the proof is not stored, it restarts at every reboot and when `platterSource` changes |
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
* **The redrawn shell is 16 mm and still not a finished enclosure for this deck** (read at shell commit `15417b6`, where the
  owner's other tool seated a headerless ESP32-S3-DevKitC-1 and lowered the AS5600 pad to 3.5 mm). It draws what the firmware does
  not do: no PLAY, LOOP IN or LOOP OUT button, no LEDs, no OLED driver, no line in, no headphone amp, no SD playback, no battery
  path, and a fader slot for a hall-sensor block with no slide-pot mount; nothing carries the hold pad's contact. Per control,
  with the smallest next step and a proposal (not implemented): the
  [Shell controls vs firmware](hardware/README.md#shell-controls-vs-firmware) table.
* **The AS5600 stack-up is thin, not settled.** Paper arithmetic from the SCAD (bolt head and breakout heights are from memory):
  with an M3x6 the magnet-to-chip gap is -0.1 mm (touching) if the bolt head rests on the bearing race and +0.7 mm if it is
  pressed against the pocket ceiling; with an M3x5 it is +0.9 or +1.7 mm, and the second puts the magnet 0.1 mm into the sleeve
  ledge. The AS5600 wants roughly 1 to 3 mm. Paper cannot settle it: buy both bolts, test-fit with the real breakout and let the
  boot log's magnet status judge ([cross-check](hardware/README.md#stack-up-cross-check-on-paper-shell-15417b6-nothing-measured)).
* **Flashing and updating through the shell's window.** The window exposes only the native USB port; the UART port stays inside.
  Our firmware turns the native port into a MIDI/audio device, which is not the ROM download interface. So: flash over the UART
  port with the deck open; later updates through the window depend on the app's `REBOOT(bootloader)` command
  ([`docs/PROTOCOL.md`](docs/PROTOCOL.md); implemented, **never tested on a chip**) or on BOOT + RESET with the deck open. Prove
  `REBOOT(bootloader)` before closing the deck for good ([`docs/BRINGUP.md`](docs/BRINGUP.md) section 2c).
* **Board: RESOLVED.** ESP32-S3-DevKitC-1, headerless, which the firmware already targets. The ESP32-Audio-Kit V2.2 (a classic
  ESP32 with no USB device mode, so no USB MIDI and no USB sound card) was dropped in `15417b6`.
* **OPEN QUESTION for the owner: keep, archive or port the RP2040 experiment?** `field-jog/firmware/rp2040/` (from the owner's
  other tool) is a separate stack: an RP2040 that synthesizes audio on the device and speaks no MIDI. This repo neither builds nor
  tests it. With the board decided, the ESP32-S3 deck is the main path.
* The chip's filter settings and filtered-angle read, the sticky release and the untouched-jog policy are written from the
  original author's description and tested in simulation; none has met a real AS5600 or a real hold pad. The pad proof in
  `AUTO` (untouched jog is sent until the pad's first touch since power-up) is our own addition, not the original's: it is
  simulation-tested only, and it cannot tell a hand from a phantom touch of a floating or chattering pad.

## Not in v1

Audio over the radio; a shared 4-channel USB audio device; BLE-MIDI; mixer MIDI control (EP-136); firmware OTA; DVS timecode (the
MTC here is MIDI Time Code); Serato and Traktor mappings; a Windows ASIO driver; a clock/transport guide verified against a real
DAW (the steps are in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) section 7); an enclosure; CI.

## Docs

[`PROTOCOL`](docs/PROTOCOL.md) (wire contract) - [`ARCHITECTURE`](docs/ARCHITECTURE.md) (decisions, topologies, verification matrix, clock-master
guide) - [`LINK`](docs/LINK.md) (radio link) - [`BRINGUP`](docs/BRINGUP.md) (first power-up, SysEx without the app) -
[`E2E`](docs/E2E.md) (conformance harness) - [`REFERENCE-ORIGINAL-FIRMWARE`](docs/REFERENCE-ORIGINAL-FIRMWARE.md) (what the original Field Jog firmware taught us) - [`hardware/`](hardware/README.md) (wiring, platter, BOM) -
[`firmware/core`](firmware/core/README.md), [`firmware/esp32s3`](firmware/esp32s3/README.md), [`macos`](macos/README.md),
[`drivers`](drivers/README.md).

## License

None yet, so no license is granted until the owner adds one.
