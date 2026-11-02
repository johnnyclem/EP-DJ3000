# EP-DJ3000 DJ-software drivers

Mappings that make the EP-DJ3000 work as a deck controller in other people's DJ software. The unit
speaks plain class-compliant USB-MIDI ([`docs/PROTOCOL.md`](../docs/PROTOCOL.md) section 2), so a "driver"
is a mapping file or a MIDI-learn recipe, never an installable driver.

**Nothing in this directory has run inside the real application.** The unit does not exist as hardware
in the sandbox this was written in, and none of the four apps does either. What is checked is described
in [Testing](#testing); what that cannot show is the whole of the real behaviour: scratch feel, deck
assignment, LED timing, whether an app accepts a file at all. Every claim below is graded with the
status codes in the next section so you can see which kind of evidence stands behind it.

| App | Folder | You install | Detail |
|---|---|---|---|
| Mixxx 2.4 / 2.5 | [`mixxx/`](mixxx/README.md) | three files into Mixxx's user mapping folder | script + XML, the reference driver |
| VirtualDJ 8 | [`virtualdj/`](virtualdj/README.md) | `Devices/EPDJ3000.xml` and `Mappers/EP-DJ3000.xml` into `Documents/VirtualDJ` | declarative XML |
| djay Pro (Mac) | [`djay/`](djay/README.md) | nothing: MIDI-learn cheat sheet, then export your own mapping | no importable file, on purpose |
| rekordbox 6 / 7 | [`rekordbox/`](rekordbox/README.md) | one CSV per unit through the MIDI window's IMPORT | community-documented CSV layout |
| (shared) | [`common/`](common/README.md) | - | the generated wire constants and the rules every mapping follows |

Run all driver tests: `node drivers/test/run-all.mjs`.

> **Do this first with VirtualDJ, djay Pro or rekordbox: set the slide pot to `PITCH`.** A factory unit sends channel
> volume (CC 7/39), and with an external mixer those three apps do nothing audible with their own channel faders (in
> rekordbox the `ChannelFader` row is even disabled), so the pot is dead until you change its role. It is one SysEx frame
> (`SET_CONFIG faderRole = 2`, then `SAVE_CONFIG`), ready to paste in [`docs/BRINGUP.md`](../docs/BRINGUP.md) section 2b, or
> Settings in the macOS app (which has never been compiled). Only Mixxx gains from the default. A CDJ's slide fader is the
> tempo fader, so `PITCH` is also the natural role.

## What the unit sends by default

A factory-default unit (nothing set over SysEx), straight from `protocol/epdj3000.json`. The table is
generated: `node drivers/test/defaults/check.mjs --write` rewrites it, and `node drivers/test/run-all.mjs`
fails when it no longer matches the contract.

<!-- BEGIN GENERATED:defaults -->
| What | Factory default |
|---|---|
| MIDI channel | `midiChannel` = AUTO: Deck A on channel 1 (status `0x90` / `0xB0`), Deck B on channel 2 (status `0x91` / `0xB1`) |
| Slide pot | `faderRole` = `CHANNEL_VOLUME`: CC 7 (MSB) / 39 (LSB), 14-bit. Other roles: `CROSSFADER` CC 23 / 55, `PITCH` CC 25 / 57 |
| Orange knob | `knobRole` = `GENERIC`: CC 24 (MSB) / 56 (LSB), 14-bit |
| Platter | `platterSource` = `TOUCH_RING`, `jogInvert` = 0: CC 22, relative `64 + delta`, **1024 ticks per revolution**, clockwise positive; touch is Note 38 |
| Buttons | `cue` Note 36, `rev` Note 37; a platter tap (`tapPlay` = 1) pulses `play` Note 39 |
| Mode | `mode` = `DJ`: no clock, transport or timecode out, so `bpm`, `bpmNudge`, `tap` and `transport` stay silent |
<!-- END GENERATED:defaults -->

The default slide-pot role is a per-deck channel volume, so two units never share a crossfader. Each
mapping binds all three pot roles; what the app does with each is in its README and in the matrix below.

## Status codes

| Code | Meaning | What it does **not** mean |
|---|---|---|
| **MOCK** | Verified with mock. The mapping's logic was executed in `node` against a stand-in for the app's scripting API, fed raw MIDI bytes built from the contract, and every call it made was checked. | That it was ever loaded into the real app. |
| **DOCS** | Format confirmed from docs. The file uses the elements, attributes and verbs the vendor's own documentation defines, and a script compares every MIDI number with the contract. | That the app accepted the file, or that the verb behaves as hoped. |
| **COMM** | Format from community sources. The file layout was reverse-engineered by third parties; the vendor publishes no schema. Numbers are checked against the contract, the layout cannot be. | That rekordbox opens it. A hand-made file may crash some versions (see its README). |
| **LEARN** | Manual MIDI-learn. No importable file: the folder holds a recipe (control, message, target, action) to enter in the app's own learn screen. Action names come from vendor or community lists and may differ in your version. | That the action exists under that name. |
| **EXP** | Experimental. The binding ships disabled or with an unverified command name; expected not to work without a hack. | - |
| **NONE** | Unsupported. No binding; the cell says why. | - |

## Compatibility matrix

Unit-side prerequisites for every column: default MIDI channels (`midiChannel` = AUTO, Deck A on
channel 1, Deck B on channel 2) and the unit calibrated first (the macOS app, or the SysEx frames in
[`docs/BRINGUP.md`](../docs/BRINGUP.md) section 2b). Mixxx works with the default slide-pot role; for the "two units into
the EP-136" rig, VirtualDJ, djay and rekordbox need `PITCH` (see [Slide pot in this rig](#slide-pot-in-this-rig)).

| Feature (what the unit sends) | Mixxx | VirtualDJ | djay Pro | rekordbox |
|---|---|---|---|---|
| **Jog / scratch** (CC 22, relative, 1024 ticks/rev) | **MOCK**. `scratchTick` while touched; untouched, a pitch bend through `[ChannelN],jog`. Feel untuned. | **DOCS**. `touchwheel`, `zero="0x40" full="1024"`. Untouched behaviour of a free-spinning platter unknown. | **LEARN**. CC 22 to Scratch, two plans. djay may read only the sign of a step, not its size. | **EXP**. `JogScratch` row disabled; jog rows are hardware-only (`RO`) in rekordbox. Expect it not to work. |
| **Platter touch** (note 38) | **MOCK**. `scratchEnable` / `scratchDisable`. | **DOCS**. `touchwheel_touch`. | **LEARN**. Scratching Mode (name from the community list). | **EXP**. `JogTouch` row disabled. |
| **Cue** (note 36) | **MOCK**. `cue_default`. | **DOCS**. `cue_stop`. | **LEARN**. CUE (community name). | **COMM**. `Cue` row, active. |
| **Play / pause** (note 39, platter tap) | **MOCK**. Toggles `play`. | **DOCS**. `play_pause`. | **LEARN**. Play/Pause (name in the official manual). | **COMM**. `PlayPause` row, active. |
| **Reverse** (note 37) | **MOCK**. `reverseroll` while held; `reverse` toggle by setting. | **DOCS**. `reverse`. Whether a press latches or is momentary is not documented. | **LEARN**. Reverse (community name). | **EXP**. `Reverse` row disabled, command name unverified. |
| **Slide pot** (CC 7/39 by default; 25/57 or 23/55 by `faderRole`; 14-bit) | **MOCK**. Deck volume (default role), deck rate (`PITCH`) or, for `CROSSFADER`, what `FADER_ROLE` says (master crossfader unless changed); 14-bit. | **DOCS**. `level` (default role), `pitch`, `crossfader`; 14-bit by `ccmsb`. | **LEARN**. Line Volume (default role), Tempo or Crossfader. MSB only, so 128 steps. | **COMM**. `TempoSlider` (`PITCH`) and `CrossFader` (`CROSSFADER`) active; `ChannelFader`, the default role's row, is disabled (name unverified), so with a default unit the pot does nothing until you change the role or enable the row. 14-bit LSB handling unverified. |
| **Orange knob** (CC 24/56, 14-bit) | **MOCK**. Quick Effect `super1`. | **DOCS**. `filter`. | **LEARN**. Effects > Filter. MSB only. | **COMM**. `Gain`; an arbitrary pick, re-learn to taste. |
| **LED feedback** (host sends note 36 and 39) | **MOCK**. `cue_indicator` and `play` to notes 36 and 39. | **DOCS**. `<led default="CUE">` follows the button's action state. Unconfirmed. | **LEARN**. MIDI Out per control. Wiring untested. | **COMM**. `output` = `input` on `PlayPause` and `Cue`. |
| **BPM / transport / clock** (CC 20/52, 21, 29, note 41; out only in SYNC modes) | **MOCK** for unit to host: `bpm`, `bpmNudge`, transport CC, tap (Mixxx 2.5+ only). **NONE** for host to unit: Mixxx drops MIDI clock and the mapping sends no tempo. | **NONE**. Left unmapped. The manual says VirtualDJ can send MIDI clock to a controller; Start/Stop unknown, untested. | **NONE**. | **NONE**. |
| **Two decks** (A = channel 1, B = channel 2) | **MOCK**. Channel selects `[ChannelN]`; the "Linked deck" cable works through a second enabled port. | **DOCS**. Single-deck definition with `_CH2` twins. Which deck VirtualDJ gives each unit is unverified; the linked-deck cable is not supported. | **LEARN**. One mapping per unit; Target Deck 1 / Deck 2. | **COMM**. One CSV per unit (`Deck-A`, `Deck-B`), selected by the unit's USB name. |

Reading the matrix honestly: only the Mixxx column has had any of its behaviour executed, and only
against a stand-in. VirtualDJ and rekordbox are files whose numbers are right and whose reception is unknown.
djay is a recipe.

### Audio into the EP-136

`docs/ARCHITECTURE.md` section 1 has two topologies. **A**, "each unit is its own USB sound card into one channel of the
mixer", needs the DAC fitted (`dacMode` AUTO or ON) and **an app that drives two audio devices at once**, which is the weak
point (table below). **B**, the units are MIDI-only (`dacMode` OFF or no DAC) and the audio comes from one multi-channel
interface (for example the Sidekick's own USB audio, not evaluated), needs only one output device from the app: the
"Per-deck outputs" column below is what to configure for it. Start with **B in Mixxx**. None of this was tested:

| App | Per-deck outputs | Two EP-DJ3000 cards |
|---|---|---|
| Mixxx | Preferences > Sound Hardware > Outputs, Deck 1 / Deck 2 (post-fader, so the unit's volume pot is audible; leave the rest of Mixxx's mixer neutral) | Not tested. Two cards have independent clocks. |
| VirtualDJ | Settings > Audio > SEPARATE DECKS, one sound card per line | Manual's example uses one multi-channel card; no official statement found for two different cards. |
| djay Pro | Mixer mode External, one stereo output per deck | The page describes one interface. Proposed route: a macOS Aggregate Device. Untested; the title of a community thread ("Pre-cueing with multiple sound cards not working", not opened) suggests friction. |
| rekordbox | Mixer Mode External, six outputs on one device | One audio device. Same Aggregate Device proposal, untested. |

Topology B is the fallback that only uses documented behaviour: one multi-channel class-compliant interface for the
audio and the units set to `dacMode` OFF (MIDI only).

#### Slide pot in this rig

For VirtualDJ, djay and rekordbox set `faderRole` to `PITCH` (config key 11, value 2) and the pot becomes a CDJ-style tempo
fader. They hand the mixing to the external mixer, and their READMEs (from the vendors' external-mixer pages) say their own
crossfader and channel faders do nothing audible then, so the default volume role is inert there. For Mixxx the default role
(`CHANNEL_VOLUME`) is the one to use: each unit cuts its own deck's volume, Mixxx's Deck outputs are post-fader so it is
audible, and the software crossfader is left alone. `CROSSFADER` is the role to avoid with two units: both pots would drive
the one software crossfader.

## Cross-driver caveats and open items

* **Default channels only.** Mixxx, VirtualDJ and the rekordbox CSVs hard-code channels 1 and 2. A unit with
  `midiChannel` set to something else needs the mapping edited (djay learns whatever it hears).
* **Input channel filtering is specified.** A unit ignores channel-voice messages on any channel but its own,
  including ones relayed on the linked-deck cable (`docs/PROTOCOL.md` section 2, "Channel filter"). It is implemented
  in the firmware core (`Engine::handleHostMessage`) and pinned by its host tests; it has not been tried on hardware.
  Mixxx depends on it: it sends the cue and play LEDs of both decks to every enabled port, each on its deck's
  channel. VirtualDJ's definition sends LEDs on both channels and each unit acts on its own. djay and rekordbox use
  one channel per unit and do not depend on it.
* **The play note is feedback only.** Note 39 from the host (velocity above 0 = playing) lights the unit's LED in DJ and
  SYNC_FOLLOW; it never starts or stops a unit's clock, in any mode. Only the `transport` CC (29) commands a SYNC_MASTER
  unit, and no mapping sends it, so mirroring the app's play state to the unit is safe whatever mode the unit is in
  (`docs/PROTOCOL.md` section 2.6). In SYNC_MASTER the firmware currently ignores the note for the LED too (open finding
  E2E-3, `docs/E2E.md`); a DJ app is not normally used with a unit in that mode.
* **Tap-to-play briefly scratches.** The unit cannot know a touch is a tap until it ends, so the touch note arrives
  first. Any mapping that holds the deck on touch stalls it for up to `tapMaxMs` (180 ms by default) before the play
  toggle lands: Mixxx does (mock-verified), VirtualDJ's `touchwheel_touch` is documented to hold the song, so expect
  the same. A delay filter would hurt the touch-to-hold feel scratching needs, so none was added.
* **14-bit is wasted on djay**, which documents no 14-bit CC support. The pot and knob work in 128 steps there.
* **Placeholder USB ids.** VirtualDJ detects the unit by `0x1209:0x0001`, the contract's development placeholder
  (a shared pid.codes test PID, so any other test device with the same ids collides). Change it in the definition when the
  unit gets its own id.
* **One cable per unit.** The linked-deck cable (one-cable bridging, cable 2) is handled by Mixxx only. VirtualDJ
  explicitly does not support it; djay and rekordbox are untested with it.
* **MIDI-learn names drift.** djay action names and rekordbox command names come from community lists in
  several places; both READMEs say which.

## MIDI clock, timecode and transport

The unit's own clock, transport and MTC output only run in `SYNC_MASTER` mode and are meant for other gear and
DAWs, not DJ software. None of the four drivers consumes them: Mixxx's MIDI controller code drops `0xF8` (read in
`midicontroller.cpp`) and the mapping handles no MTC; VirtualDJ's manual describes sending clock to a controller,
and I found nothing on it consuming clock; no documentation was found for djay or rekordbox reading MIDI clock
(rekordbox has Ableton Link instead). That
output is exercised by the firmware tests and the macOS app, not by anything here. Feeding it to real gear (the
EP-133 over the TRS jack, a DAW over USB) is unverified. How to turn a unit into a clock master and what each control
does: [`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md) section 7.

## Apps that cannot be supported as an open mapping

### Serato DJ Pro / Lite

Serato has its own MIDI Assign feature for non-Serato hardware, but it does not cover platters, and the jog
is the point of this unit. I could not read Serato's pages (`support.serato.com` answered my fetch with HTTP 403), so
this is from search-result summaries of the support articles "MIDI mapping with Serato DJ" and "MIDI Platters"
and from DJ TechTools and forum posts. Verify it against the current Serato manual:

* buttons, dials and faders on a third-party controller can be learned and saved as presets;
* Serato says its virtual decks are not user-assignable to MIDI control, and that MIDI devices with platters
  send different data, so user mapping of a platter is not possible;
* community posts describe editing the saved mapping XML by hand for functions the learn screen does not offer.

Consequence: cue, play, tempo and filter could be learned by hand, but there would be no scratching, so no mapping
is shipped. Serato's licensing and hardware-certification terms for a third-party controller were not researched.

### Traktor Pro (TSI)

Traktor mappings are `.tsi` files: an XML wrapper around a Base64 blob of nested tag-length-value binary chunks.
Native Instruments publishes no specification. The format has been reverse-engineered by the community
([ivanz/TraktorMappingFileFormat](https://github.com/ivanz/TraktorMappingFileFormat), roughly 95 percent documented
for Traktor Pro 2.x per its author; the `py-ni-traktor-tsi` package on PyPI reads and writes it). I did not verify
either against a current Traktor Pro. A generated binary I cannot load is a guess in an opaque format, so none is
shipped.

What works instead is the djay approach: map the controls by hand in Traktor's Controller Manager as a generic
MIDI device, using the numbers in [`common/README.md`](common/README.md) and `docs/PROTOCOL.md`, then export the
TSI yourself and commit it here. Menu names are from memory of the product and not checked.

### Not evaluated

Engine DJ, Cross DJ, DJUCED and DAWs (Ableton Live, Logic and the like, which are the natural consumers of the
unit's clock and MTC). No research was done on them.

## Windows notes

* **MIDI works without a driver.** The unit is USB-MIDI 1.0 class-compliant; Windows 10 and later bind their
  in-box class driver. Each cable is its own port: Deck (cable 0), TRS MIDI, Linked deck. Historically the in-box
  driver lets one application open a port at a time, so quit one app before starting another (newer Windows MIDI
  Services lifts this on Windows 11; not checked here).
* **Audio is meant to be class-compliant, but is not low-latency.** The USB audio function is USB Audio Class 1.0, stereo,
  48 kHz, 16-bit (`docs/PROTOCOL.md` section 1). Windows 10 and later should play it through their in-box USB Audio 1.0
  driver, and it should show up to the DJ app as an ordinary audio device (WASAPI, MME, DirectSound). Not tried on Windows. **Low-latency audio on Windows
  normally wants an ASIO driver and this unit has none**; writing or licensing one is outside v1. Expect higher
  output latency than on macOS, which matters for scratch monitoring. A generic ASIO layer on top (ASIO4ALL,
  FlexASIO) might help; untested.
* **Two units, two sound cards.** macOS Aggregate Devices (the proposal for djay and rekordbox above) do not exist on
  Windows, and ASIO exposes one device per application. Whether each app can address two WASAPI devices is
  unverified.
* **Install paths.** Mixxx: `%LOCALAPPDATA%\Mixxx\controllers`. VirtualDJ mappers: `Documents\VirtualDJ\Mappers`;
  for definitions VirtualDJ's two official pages disagree (`%LOCALAPPDATA%\VirtualDJ\Devices` or
  `Documents\VirtualDJ\Devices`), see its README.
* **The test/calibration app is macOS-only.** The configuration protocol is plain SysEx (PROTOCOL section 3), so any
  SysEx-capable tool can drive it: ready-made frames are in [`docs/BRINGUP.md`](../docs/BRINGUP.md) section 2b. Nothing
  was run on Windows.
* djay Pro for Windows and rekordbox on Windows were not reviewed; both READMEs cover the Mac flow.

## Testing

```sh
node drivers/test/run-all.mjs            # one line per step; full output for failures; exit 1 on any failure
node drivers/test/run-all.mjs --verbose  # print every step's output
```

Written and run on Node 22; needs `python3` (override with `PYTHON=...`); `xmllint` is optional (reported as SKIP, never as pass, when absent).

| Step | What it proves | What it cannot |
|---|---|---|
| `tools/gen_protocol.py --check` | `common/epdj3000-protocol.js`, `mixxx/epdj3000-protocol.js` (the generator writes both) and the other generated files are current with `protocol/epdj3000.json` | - |
| `test/defaults/check.mjs` | The "what the unit sends by default" table above equals a render of the contract; the Mixxx, VirtualDJ, djay and rekordbox READMEs all state the contract's default fader role and its CC pair; broken copies fail | That the firmware sends it (the firmware tests do) |
| `test/mixxx/run.mjs` (38 tests) | The XML and script load the way Mixxx loads them; every `<status>`/`<midino>` equals the set derived from the contract; each handler behaves correctly against a strict mock engine that rejects any control name not in the Mixxx docs; the generated XML is current and both generated copies of the constants are identical | That real Mixxx accepts it. Scratch feel, soft takeover with a real pot |
| `test/virtualdj/check.mjs` (25 tests) | Both XML files are well-formed, use only elements and attributes of the official reference, agree with the contract number for number, use only verbs from a snapshot of the official verb list; deliberately broken copies fail | Anything VirtualDJ does with them |
| `test/djay-rekordbox/check.mjs` | Both CSVs decoded independently of the generator match the contract (status, data byte, channel, deck column, type, LED); unverified and jog rows stay disabled; the djay and rekordbox READMEs' tables carry the contract numbers; broken copies fail | That rekordbox opens a CSV or djay has the action |
| `xmllint --noout` on the three XML files | Parses with a second, independent parser | - |

Suites are independent of the current directory. The VirtualDJ and djay/rekordbox suites feed themselves
deliberately broken copies to show each check can fail; the Mixxx suite shows its XML parser rejects malformed input
and its strict mock throws on any control or API name outside the Mixxx docs. `run-all.mjs` was itself checked by breaking a CC in the constants, a note in the VirtualDJ
definition and a row in a rekordbox CSV (each made the run exit 1 on its own step), and by pointing it at a
missing `python3`.

## Adding a mapping for another app

1. **Read the app's documentation first and write down what you relied on**: URLs, the date, and for each
   fact whether it is official or community. Decide the mechanism: a script, a declarative file, or MIDI learn.
   If the vendor documents no file format, do not invent one; write a MIDI-learn recipe like `djay/`.
2. **Create `drivers/<app>/` with a README** that opens with a status banner saying what is verified and what is
   not, then install steps, a mapping table, known limits, a "not verified" list and sources. The existing four READMEs
   are the template.
3. **Take every number from the constants, never retype one.** In JS use `drivers/common/epdj3000-protocol.js`
   (`E.cc.jog`, `E.note.touch`, `E.defaultChannels`, `E.jogTicksPerRev`, `E.relativeCenter`, ...); in Python or
   other tooling read `protocol/epdj3000.json`. Where the format is declarative, generate the file from the constants
   (`test/mixxx/gen-xml.mjs` and `rekordbox/generate_csv.py` do) so it cannot drift.
4. **Follow the rules in [`common/README.md`](common/README.md)**: channel is the deck, notes release as Note Off or
   velocity 0, relative CCs are offset-64, 14-bit pairs are MSB first with either half skippable, the slide pot is
   one of three pairs by `faderRole` (default `CHANNEL_VOLUME`), LED feedback is Note On on notes 36 and 39.
   `common/README.md` has a decoder that does these correctly; copy its logic, not your memory of MIDI.
5. **Account for every contract control.** `jog`, `touch`, `cue`, `rev`, `play`, `tap`, `crossfader`,
   `channelVolume`, `pitch`, `knob`, `bpm`, `bpmNudge`, `transport`: each is bound or listed as unmapped with a
   reason. The VirtualDJ and djay/rekordbox checks fail when a control is neither.
6. **Bind both decks.** Channel 1 is Deck A and channel 2 is Deck B; say how the app tells the two units apart
   (by MIDI channel as Mixxx does, by USB name as the rekordbox CSVs do, or by learning).
7. **Write a test** in `drivers/test/<app>/`: Node only, no dependencies, exit non-zero on failure, expectations derived
   from the contract rather than copied from the mapping, and a few deliberately broken inputs that must fail. Then add
   it to `STEPS` in `drivers/test/run-all.mjs`.
8. **Add a column to the matrix above** using only the status codes. **MOCK** only if your test executes the mapping
   logic; **DOCS** only if you read the vendor's documentation of that format; otherwise **COMM**, **LEARN**,
   **EXP** or **NONE**. Do not promote a cell because the app is popular.
9. **Put a first-run checklist in the README** (load a track, touch the platter, spin both ways, tap, cue, pot,
   knob, second unit, LEDs). The first person with hardware is the one who moves cells from MOCK or DOCS to confirmed;
   say where to record what they found.

If the wire contract is missing something the app needs, do not work around it in the mapping: raise it against
`protocol/epdj3000.json` and regenerate (`python3 tools/gen_protocol.py` writes `drivers/common/epdj3000-protocol.js`
and the copy in `drivers/mixxx/`).
