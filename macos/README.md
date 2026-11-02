# EP-DJ3000 for macOS

A SwiftUI test app for the EP-DJ3000 deck: live view of the platter and controls, guided calibration, a MIDI and
SysEx monitor, a small scratch-capable player, and a two-deck rig view. It speaks the wire protocol in
[`docs/PROTOCOL.md`](../docs/PROTOCOL.md) over CoreMIDI and plays audio with AVAudioEngine.

> ## NOT COMPILED IN THE AUTHORING SANDBOX
>
> **This is the single biggest unverified item in the repo, and bring-up leans on it** (telemetry, calibration, saving
> settings). Building and launching it once on a Mac is the highest-value check left. If it does not build, or you have no
> Mac, the same steps work without it: ready-made SysEx frames and a small decoder are in
> [`docs/BRINGUP.md`](../docs/BRINGUP.md) section 2b.
>
> The authoring sandbox is Linux: no Xcode, no macOS SDK, none of CoreMIDI, CoreAudio, AVFoundation, SwiftUI or
> AppKit. **The `EPDJ3000Kit` and `EPDJ3000App` targets have never been built against the real Apple frameworks and
> have never been run.** Expect to fix compile errors on first build.
>
> **What was checked**
>
> | Check | Result |
> |---|---|
> | `swift build` and `swift test` of `EPDJ3000Protocol`, `EPDJ3000Engine` and `EPDJ3000KitPure` on Linux (Swift 6.0.3) | pass, 347 tests (211 + 81 + 55; the Protocol and Engine counts include the 7 added for the slide-pot role fix, finding E2E-2, and the Protocol count the 36 for the AS5600 calibration flow); no warnings from the three library targets with `-Xswiftc -strict-concurrency=complete` (the E2E test target has four `stderr` concurrency warnings, in test code only). `swift test` also lists the 69 `EPDJ3000E2ETests` (the firmware engine as a child process); all of them skip when `epdj_engine_sim` is not built |
> | The Foundation-only part of the Kit (`Sources/EPDJ3000Kit/Pure/`, the `EPDJ3000KitPure` target: MIDI formatting, log, waveform overview, test signals, config presentation, rig checklist, error text, name matching) | compiled and tested with the real toolchain as part of `swift test` (`EPDJ3000KitPureTests`) |
> | All Kit and app sources type-checked in Swift 5 language mode, with complete strict-concurrency checking, against **hand-written stand-ins** for CoreMIDI, CoreAudio, AudioToolbox, AVFoundation, Combine, SwiftUI, AppKit and UniformTypeIdentifiers (not part of this repository) | no errors (re-run after the AS5600 calibration flow went in: the new `CalibrationController`, `CalibrateView` and `SettingsView` code is included); none in the Kit; 5 warnings in the app, all artefacts of the stand-ins (the `NSSecureCoding` casts that only fail on Linux, and isolation of the stand-in `NSApplicationDelegate` / `App` protocols, whose isolation in the real SDK may differ; on an older SDK the same warnings may appear for real) |
> | `DeckAudio` / `AudioOutput` device-change handling, run (not just type-checked) on Linux with a stateful `AVAudioEngine` stand-in that stops itself and posts `AVAudioEngineConfigurationChange` on command (not part of this repository) | six scenarios pass: a second change right after a rebuild is restarted after the 0.5 s interval instead of dropped; bursts coalesce into one restart; a notification while the engine still runs rebuilds nothing; `shutdown()` and a later rebuild cancel a pending restart; a change long after the last rebuild is acted on at once. The previous code fails the scenarios that need a restart inside the window (it dropped the second change and left the engine stopped). This exercises our logic only; the stand-in says nothing about when or how often CoreAudio really posts the notification |
>
> The stand-ins were written from memory of Apple's documentation. That check proves the code is consistent with
> itself and calls the Protocol and Engine libraries correctly (those are real). It proves nothing about whether
> Apple's actual signatures, nullability, deprecations or behaviour match. It did catch three real mistakes
> (a name clash between our `AudioBuffer` and CoreAudio's, an `NSLock` taken in an async function, a `TimelineView`
> closure the compiler cannot infer), which is why it is worth having, and also why the next paragraph matters.
>
> **What was not checked at all:** linking; every Apple API call being spelled and typed as the real SDK has it;
> deprecation warnings (the legacy `MIDIInputPortCreateWithBlock` / `MIDISend` family is deprecated in recent SDKs
> and will warn); anything at runtime: hot-plug, packet-list parsing against the real struct layout, the cable
> numbering assumption below, output-device pinning, drag and drop, the layout and look of any view, behaviour with
> real hardware. The residual-risk list at the end names the spots most likely to need a fix.

## Build and run

Requirements: macOS 13 or later, Xcode 15 or later (or its command line tools; Swift 5.9+ by the manifest's tools version; the pure libraries were built and tested with Swift 6.0.3 on Linux).

```sh
cd macos
swift run EPDJ3000App          # quick: runs the bare executable
scripts/make-app.sh            # builds macos/EP-DJ3000.app (release, ad-hoc signed)
open EP-DJ3000.app
UNIVERSAL=1 scripts/make-app.sh   # arm64 + x86_64
```

Or open `Package.swift` in Xcode and run the `EPDJ3000App` scheme.

`swift run` starts an unbundled executable. The app compensates (it sets itself to a regular app and activates), but the
bundle from `make-app.sh` is the better way to run it. The ad-hoc signature is enough on the Mac that built it; a copy
moved to another Mac needs right-click > Open the first time.

On any OS, the pure libraries build and test: `swift build && swift test` (no CI runs this yet). The Kit and app
targets are only declared when the manifest is evaluated on macOS.

## Layout

| Target | Imports | What |
|---|---|---|
| `EPDJ3000Protocol` | Foundation | Wire contract (generated), SysEx codec, `DeviceClient`, MIDI parser, `ControllerState`, calibration state machines (slide pot and knob sweeps, AS5600 encoder check, platter direction, hold pad, experimental touch ring) and `CalibrationFlow`, which picks the steps for a platter source. Tested. |
| `EPDJ3000Engine` | Foundation | `ScratchPlayerCore` (the sample player), `DeckModel` (MIDI to player commands), jog velocity. Tested. |
| `EPDJ3000KitPure` | Foundation | The part of the Kit that needs no Apple framework (`Sources/EPDJ3000Kit/Pure/`): MIDI formatting and log, waveform overview, test signals, config presentation, rig checklist, error text, name matching. A target of its own so it builds and is tested everywhere. |
| `EPDJ3000Kit` | CoreMIDI, CoreAudio, AVFoundation, Combine | The Apple glue: `CoreMidiService` / `CoreMidiTransport` (a `MidiTransport` for `DeviceClient`), `DeviceManager`, `DeckSession`, `DeckAudio` / `AudioOutput` (source node into `ScratchPlayerCore`), `TrackLoader`, `AudioDevices`, `CalibrationController`. Depends on `EPDJ3000KitPure` (its `Pure/` folder is excluded from this target). |
| `EPDJ3000App` | SwiftUI, AppKit | Six tabs. No logic of its own beyond layout. |

The Apple layer is deliberately thin. Anything with a decision in it (timing of the calibration steps, thresholds,
what a message means, what the player does) lives in the two tested libraries.

## The tabs

**Decks.** One card per connected unit: an animated platter driven by the jog ticks, a touch indicator, the slide pot
and knob, CUE / REV / TOUCH lights, tempo, transport, song position and MIDI time code as the unit reports them. Buttons
send transport and tempo to the unit (the unit acts on them in sync-master mode; elsewhere they only drive its LEDs).
The unit's PLAY and CUE LEDs follow the player unless you switch that off. The unit takes the play note as LED feedback
only and never starts its clock from it, in every mode. A tap on a unit in sync-master mode arrives as
Start/Stop and as the play pulse; the player treats the pair as one tap.

**Calibrate.** A guided wizard over the protocol library's state machines. Which steps it offers follows the unit's
`platterSource` setting (live: change it in Settings and the list changes):

| `platterSource` | Steps, in order |
|---|---|
| `AS5600` (the default) | *Encoder check* (spin the platter a full turn: the chip answered, the angle follows, with a live angle dial), *Platter direction*, *Hold pad* (idle noise, then a flat hand on the platter), *Slide pot*, *Knob* |
| `TOUCH_RING` (experimental) | *Slide pot*, *Knob*, *Touch ring* (idle noise, then a finger on each sector, with live per-sector bars), *Platter direction* |
| `TOUCH_HOLD_ONLY` | *Hold pad*, *Slide pot*, *Knob* |

Each step shows its live reading, an instruction, and pass or fail. A failed encoder check says what to look at (no chip
answered: I2C wiring and the 3.3 V strap; no valid angle; the angle did not move a full turn: magnet; the angle jumps
around: magnet position or bus noise). A result is sent to the unit with *Apply* (live) and made permanent with *Save to
unit*; the encoder check changes nothing on the unit. The hold-pad step sets the touch thresholds from a hand on a platter
that is not turning: it cannot tell whether the pad keeps seeing the hand while the platter spins (a rotating-contact
problem), so the step shows the unit's own touch flag live and `docs/BRINGUP.md` section 2 asks for that check by hand.

**Test.** MIDI monitor (filter by message type and by source, pause, clear; clock ticks fold into one line; the other USB
cables, TRS and linked deck, are listened to as well), raw telemetry (every field, the hex of the frame, the eight touch
pads), a SysEx log in both directions with the frames decoded, and bench tools: identify (flash the LED), the unit's own
test tone through its DAC, and a latency note with the one number the app can measure (CoreMIDI timestamp to this app
handling the packet).

**Player.** Drop an audio file (or open one, or load the built-in demo loop or 440 Hz tone, which need no file and no
DAC). Waveform overview with playhead and cue point; click to seek. Play / pause, stop, CUE (hold) and REV (hold) work from
the buttons *and* from the unit at the same time; a hand on the unit's platter (the touch note, from its hold pad) holds the
record and turning the platter scratches. Spinning the AS5600 platter with no hand on it moves the platter on the Decks tab
and nothing else: the player follows the jog only while the touch note is on.
The knob and slide pot act as in `DeckModel`: the slide pot is the pitch (tempo) fader by default (the unit's `faderRole`
is `PITCH`), or a channel volume, or a crossfader-style cut when the role is changed (a Player-tab picker sets it live). The output picker pins this deck's player to any CoreAudio
output, including the deck's own USB audio device, which is the end-to-end audio test. With no unit connected the *On-screen
deck* works with the buttons and a platter you drag with the mouse.

**Rig.** Deck A and Deck B side by side with their capabilities, the radio link each one sees (`LINK_STATUS`: signal, round
trip, clock offset, USB host / forwarding / master flags, deck-id conflicts), a checklist judged from what the units and
macOS report (the list in `docs/ARCHITECTURE.md` section 4: decks present, distinct ids, link, DAC, USB audio visible to
macOS, DJ-software routing, mixer), and per-deck audio tests: the unit's own tone, and the app's 440 Hz tone sent through
that deck's USB audio output so you can hear it arrive on that mixer channel.

**Settings.** An editor generated from `ConfigTable`: a row per key, with pickers for enums, switches for flags, check
boxes for bit fields and sliders for numbers (including `linkGroup`, the radio rig id: units only link within the same
group). `platterSource` reads "AS5600 encoder (default)", "Touch ring (experimental)" or "Hold only", and `touchSectors` is
greyed out unless the ring is selected. Changes are live; *Save to unit* persists them. The deck id is shown but read-only (the unit refuses it in
`SET_CONFIG`); change it with *Make Deck ...*, which saves it and restarts the unit. Also: reload, load defaults (which
keeps the deck id), restart, and enter the ROM bootloader. Each of the last four asks first.

## Permissions

None are requested. CoreMIDI needs no permission on macOS. Audio is output only, so no microphone prompt. Files are
opened through the open panel or drag and drop. The app is not sandboxed and uses no network.

## Using it with hardware

1. Plug in one unit with a data-capable USB-C cable. The status strip shows `Deck A connected` (or B).
2. **Test > Telemetry**: switch streaming on, spin the platter, put a hand on it and move the controls; the angle, the hold pad's count and the raw values move.
3. **Calibrate**: the steps for the unit's platter source (for the default AS5600: encoder check, platter direction, hold pad,
   slide pot, knob), then *Save to unit*. Then turn the platter with a hand on it and watch the touch light on the Decks tab:
   the wizard cannot test that.
4. **Test > MIDI monitor**: touch and spin the platter, press CUE and REV; look for jog, touch and note messages.
5. **Player**: load the demo loop, press play on the unit's platter (tap) or on screen, then scratch.
6. With a DAC fitted: **Player > Output**, choose the deck's USB audio device; or **Rig > Mac tone to this deck**.
7. Second unit: it appears next to the first. **Rig** shows the link and runs the checklist.

## Troubleshooting

**No unit appears.**
- The cable must carry data. Try another port.
- Open Audio MIDI Setup > Window > Show MIDI Studio: is an `EP-DJ3000 Deck A` / `Deck B` device there, and not greyed
  out? Greyed out means macOS remembers it but it is not connected.
- The app finds units by the name `EP-DJ3000` in the MIDI device name or model. If you renamed the device in MIDI Studio
  it will not match.
- The unit may be in the ROM bootloader (after *Enter bootloader*): power-cycle it.

**A unit shows "Not responding" or `Deck A?`.** It enumerates as MIDI but does not answer `GET_INFO`. Press *Try again*.
Common causes: the firmware is not running, the firmware and app disagree about the protocol version (the message says
so), or another program has the port open and is consuming the replies.

**Both units say Deck A.** Hold CUE and REV on one of them for 5 s, or use Settings > *Make Deck B*. The status strip warns
about this.

**Nothing moves on the Decks tab but telemetry works.** The app listens on MIDI channel 1 for Deck A and 2 for Deck B, or on
the unit's `midiChannel` setting. A unit on another channel is ignored by the deck model on purpose; the MIDI monitor still
shows it.

**No sound.**
- Is a track loaded and playing, and is the unit's slide pot open? With the default channel-volume role the level
  follows the pot linearly, so it is silent at the closed end (with the crossfader role it is off there and reaches full
  volume about 10 % into the travel). The player stays open until the pot first moves.
- **Player > Output**: check which device is chosen. *System default* follows whatever macOS uses.
- The deck's own output exists only if macOS lists it (a DAC is fitted and `dacMode` is not OFF). The Rig checklist says so.
- Test the path without the app's player: **Test > Tools > Send test tone** (the unit's own DAC) should be audible on
  the line out.

**Wrong output device.** Pick it in **Player > Output** and use *Refresh list* after plugging something in. A device that
disappears makes the app fall back to the system default and say so.

**Crackles.** The player renders at 48 kHz and lets macOS convert to the device rate. Make sure the device is not at a
very different rate in Audio MIDI Setup, and that nothing else is loading the CPU.

**Platter spins the wrong way.** Calibrate > Platter direction, then Save.

**The platter does nothing (Decks tab, MIDI monitor).** Calibrate > Encoder check says why. A unit that saved its settings
before contract v1.3 may still be on `TOUCH_RING`: Settings > `platterSource`, or *Load defaults* and *Save to unit*.

**Touching the platter does not hold the track.** The touch note comes from the hold pad: Calibrate > Hold pad, then check the
touch light with the platter turning. If it flickers, the contact from the spinning platter to the pad is the suspect
(`docs/BRINGUP.md` risk 11).

**A track will not load.** The player takes mono or stereo files that macOS can decode, up to 12 minutes (the whole
decoded track is held in memory). DRM-protected files are refused by macOS.

## Behaviour worth knowing

- **Latency.** MIDI is parsed and applied on the main thread, so UI work adds jitter between a platter tick and the
  player. The Test tab shows the worst delay over the last second. A dedicated queue for the deck model is the obvious
  next step if that number is large during scratching.
- **Sample rate.** Every player renders at 48 kHz and files are converted to it once at load time with the highest
  quality of `AVAudioConverter`.
- **Output changes.** When macOS reports a hardware change under a running engine (a pinned DAC unplugged, the default
  device or its rate changed), the deck's engine is rebuilt on the chosen output, or on the system default if that
  output is gone. AVAudioEngine has already stopped itself when it reports this, so a report is never ignored; rebuilds
  are kept at least half a second apart, and a report that arrives sooner is acted on when that interval is up.
- **One player per unit.** A unit's loaded track belongs to its session and goes away when the unit is unplugged.
- **Mono / stereo only**, and one deck's audio goes to exactly one output device.
- **Cable numbering.** Unit cable 0 (Deck) carries the SysEx protocol and the deck's MIDI; cables 1 (TRS) and 2 (linked
  deck) are listened to for the monitor only.
- **A bridged deck is not usable from this app.** With one-cable mode, Deck B reaches the Mac on cable 2 of Deck A. The app
  only monitors that cable: it cannot play with Deck B, show its controls or calibrate it (SysEx is not bridged by design),
  and the Rig tab lists Deck B as "Not on USB". Plug a deck into the Mac directly to calibrate or to use it here.

## Tests

`swift test` runs the Protocol, Engine and KitPure tests on any OS (347 tests on Linux with Swift 6.0.3; the full run reports 416 because it also lists the 69 end-to-end tests, which skip until `tools/e2e.sh` has built the firmware simulator, see [`docs/E2E.md`](../docs/E2E.md)); `tools/check-all.sh swift e2e-swift` runs both halves. The Kit's
Foundation-only code (`Sources/EPDJ3000Kit/Pure/`) is the `EPDJ3000KitPure` target, tested by `EPDJ3000KitPureTests`;
`Package.swift` declares the Kit and the app only on macOS, so nothing that imports CoreMIDI, AVFoundation or SwiftUI is
tested here. `DeviceClientTests` talk to `FakeDevice`, an in-memory unit written from `docs/PROTOCOL.md` section 3 and
held to it by `FakeDeviceTests`.

## Residual risks (read before the first build)

These are the places most likely to need a fix on a real Mac, roughly by likelihood.

1. **Deprecations.** `MIDIInputPortCreateWithBlock`, `MIDISend`, `MIDIPacketList*` are the MIDI 1.0 byte-stream API that
   Apple deprecated in favour of `MIDIEventList` in macOS 11. It still works; expect warnings. Chosen because the whole
   protocol library speaks bytes.
2. **Cable-to-endpoint mapping.** `CoreMidiService.discoverUnits` assumes the n-th source / destination of the device is
   USB-MIDI cable n. If CoreMIDI orders them differently the deck traffic may be read from the wrong endpoint: the unit
   would then look unresponsive. Check the endpoint names in MIDI Studio (the firmware's jack strings should name them).
3. **Packet-list walking.** `CoreMidiInput.deliver` locates the first packet and the data bytes with
   `MemoryLayout.offset(of:)` (fallbacks 4 and 10, the documented packed layout) and advances with `MIDIPacketNext`.
   It was never run against a real `MIDIPacketList`.
4. **`MIDIPacketListAdd` types.** Its byte-count parameters are `Int` or `UInt` depending on how the SDK imports
   `ByteCount`, and its result may or may not be optional. The code is written to compile either way.
5. **`AVAudioIONode.audioUnit` and `AudioUnitSetProperty(kAudioOutputUnitProperty_CurrentDevice)`** are used to pin an
   engine to a device. This is the long-standing way, but it was not exercised.
6. **`@StateObject` of a main-actor object** in the `App` struct (`EPDJ3000App.swift`). If an older Xcode complains about
   main-actor isolation on that line, create the manager in a main-actor context instead.
7. **`NSApplicationDelegate` conformance on a `@MainActor` class** may produce an isolation warning on older SDKs.
8. **SF Symbol names** (`dial.medium`, `clock.badge.checkmark`, `cable.connector`, and `arrow.triangle.2.circlepath` and
   `hand.raised` for the encoder and hold-pad steps...) need macOS 13's symbol set; a missing one shows as an empty icon, not
   an error.
9. **`TimelineView` ticking.** `Live` re-runs its content on a timer. If a view does not update, check there first.
10. **SwiftUI container limits.** SwiftUI allows at most ten children in a `ViewBuilder` block; the stand-in enforced the
    same limit and passed.
11. **Output hot-plug.** `DeckAudio.engineConfigurationChanged` restarts the engine after an
    `AVAudioEngineConfigurationChange`, deferring (never dropping) one that comes within 0.5 s of the last rebuild. It
    follows Apple's documented behaviour (the engine stops itself before posting) and was only exercised against a
    stand-in. Unplug a pinned DAC while playing and check that sound resumes on the default output within a second. A
    device that keeps changing is retried every 0.5 s with no upper bound.
