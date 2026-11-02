# EP-DJ3000 for VirtualDJ

**Status: UNTESTED in VirtualDJ.** VirtualDJ is closed source and does not run where this was
written, so these files have never been loaded into it. What *is* checked
(`node drivers/test/virtualdj/check.mjs`): both files are well-formed, use only elements and
attributes from VirtualDJ's official MIDI definition reference, and agree number for number with
`protocol/epdj3000.json`. What VirtualDJ then *does* with them (detection, deck assignment of two
units, jog feel, LEDs, audio presets) is a bet until you run [First run](#first-run-ten-minutes).

**Format: taken from VirtualDJ's published documentation; never loaded into the app.** The file schema comes from
VirtualDJ's official wiki ([sources](#sources)) plus a VirtualDJ developer's own touch-jog example. Nothing here shows
that VirtualDJ accepts these files; the checks only compare them with that documentation and with the contract. Two
places where the docs contradict themselves, and what I chose, are in [Doc inconsistencies](#doc-inconsistencies).

## Files

| File | Install into | What |
|---|---|---|
| `Devices/EPDJ3000.xml` | VirtualDJ `Devices` folder | Definition: gives every MIDI message a name |
| `Mappers/EP-DJ3000.xml` | VirtualDJ `Mappers` folder | Mapping: gives each name a VDJscript action |

## Install

macOS (VirtualDJ's folders are in your Documents folder), from a checkout of this repo:

```sh
cp -R drivers/virtualdj/Devices/. ~/Documents/VirtualDJ/Devices/
cp -R drivers/virtualdj/Mappers/. ~/Documents/VirtualDJ/Mappers/
```

Windows: mappers go in `Documents\VirtualDJ\Mappers`. For definitions the two official pages disagree:
the Controller Developers page says `%LOCALAPPDATA%\VirtualDJ\Devices`, the MIDI definition page says
`Documents\VirtualDJ\Devices`. Try the first; if the unit is not listed, the second.

Restart VirtualDJ with the unit plugged in. Licensing, from the VirtualDJ manual: without a Pro
Infinity or Pro Monthly subscription or a controller-specific license a controller works for the
first 10 minutes of use after the software launches, and the external-mixer audio setup below needs "an Analog Mixer or Pro
Infinity License".

## Rig: two units into the EP-136 mixer

This is the "poor man's CDJs" setup from `docs/ARCHITECTURE.md` section 1: each unit is a controller
plus its own USB sound card, and the EP-136 does the mixing in hardware.

1. **Two USB-C cables**, one per unit, both to the Mac. One-cable bridge mode (Deck B over Deck A's
   USB, MIDI cable 2) is **not supported** by this definition; it assumes one MIDI connection per unit.
2. **Deck assignment.** Each unit is a single-deck controller (`decks="1"`). VirtualDJ assigns
   connected units to decks itself (a VirtualDJ developer says identical single-deck units such as CDJs
   work this way, and that you should check they landed on the right decks). Look in the Controllers
   tab of Settings, touch a platter and see which deck reacts; fix the assignment there if it is the
   wrong way round. Both units show the same name, and per user reports they can swap after a
   reconnect, so start VirtualDJ with both plugged in. The unit's MIDI channel (Deck A = 1, Deck B = 2)
   does not decide the VirtualDJ deck, because the definition accepts either channel: "Deck A" on the
   unit's label is deck 1 in VirtualDJ only if the assignment says so.
3. **Slide pot as tempo fader.** The unit's default `faderRole` is `PITCH` (CC 25 / 57): each unit's pot drives
   `pitch` on its own deck, a CDJ-style tempo fader, and nothing needs setting. That is the factory role because
   it is the one that does something here: with an external mixer VirtualDJ's own channel level and crossfader do
   nothing audible. `CHANNEL_VOLUME` (CC 7 / 39, `level`) would leave the pot inert, and `CROSSFADER` (CC 23 / 55)
   is just as inert and, with two units, would have both drive the one global crossfader. They exist for rigs
   without an external mixer; to change the role see `docs/BRINGUP.md` section 2b.
4. **Audio.** Needs the DAC fitted (`dacMode` AUTO/ON; changing it reboots the unit). In Settings,
   Audio, pick **SEPARATE DECKS** as the output (the manual's external-mixer procedure), then in the
   Advanced lines set Deck 1 to sound card `EP-DJ3000 Deck A`, channels 1 and 2, and Deck 2 to
   `EP-DJ3000 Deck B`, channels 1 and 2. Apply. Run each unit's line out into its own channel of the
   EP-136; master, EQ, cue and headphones all happen on the EP-136. If a card is missing from the
   list, the manual's workaround is to choose COMPUTER AUDIO first and pick the card in the sound-card
   field afterwards. The definition also declares an `<audio>` preset ("EP-DJ3000 Deck" in the Audio
   tab) with `mixer="no"`; it describes one unit's card, so for two units use the manual steps above.

## Mapping

Every name exists twice, once per default MIDI channel: the plain name is channel 1 (Deck A unit), the
`_CH2` name is channel 2 (Deck B unit). Both twins have the same action, so the table lists the plain
names. Numbers are from `protocol/epdj3000.json`.

| Name | Unit sends | VDJscript | Notes |
|---|---|---|---|
| `JOG` | CC 22, relative `64 + delta`, 1024 ticks per revolution | `touchwheel` | `zero="0x40" full="1024"`, clockwise = forward |
| `JOG_TOUCH` | Note 38, vel 127 / 0 | `touchwheel_touch` | holds the track while a hand is on the platter (the hold pad) |
| `PLAY` | Note 39 pulse on a platter tap | `play_pause` | `LED_PLAY` follows play state |
| `CUE` | Note 36 | `cue_stop` | CDJ-style cue; `LED_CUE` follows cue state |
| `REV` | Note 37 | `reverse` | the verb list says only "play the song backward": whether a press latches or is momentary is not documented, find out on first run. `dump while_pressed` is the documented momentary alternative (reverses while held, then resumes where the track would have been) |
| `CROSSFADER` | CC 23 (MSB) / 55 (LSB) | `crossfader` | only when `faderRole` = CROSSFADER |
| `LEVEL` | CC 7 / 39 | `level` | only when `faderRole` = CHANNEL_VOLUME |
| `PITCH` | CC 25 / 57 | `pitch` | when `faderRole` = PITCH, the unit's default |
| `KNOB` | CC 24 / 56 | `filter` | colour-FX filter, nothing applied at centre; silent if `knobRole` = BPM |

**Untouched platter motion.** The unit's default `jogUntouched` is `AUTO`: it sends `JOG` for a platter nobody touches until
the hold pad has registered a touch since power-up, and only while a hand is on the platter after that (contract v1.4; a
coasting platter would otherwise keep driving `touchwheel`). To get motion from a platter nobody is touching *after the pad
has worked*, for instance as a nudge, set `jogUntouched` = `REPORT` (config key 33, value 1) in the macOS app's Settings or
with the SysEx frame in [`../README.md`](../README.md#untouched-platter-motion-joguntouched). What
`touchwheel` does with jog and no touch is not documented; nothing in this mapping changes for it.

LEDs are not mapped separately: per the definition reference, an LED with `default="CUE"` uses the
action of the `CUE` button as its state.

**Deliberately not mapped:** `bpm`, `bpmNudge`, `tap` and `transport`. The unit only sends them in its
clock-master modes (and `bpm` with `knobRole` = BPM), which are not DJ-software modes, and VirtualDJ's
tempo verbs are not a fit for a controller-driven BPM. For tempo, see the next section.

## MIDI clock to the unit (optional)

VirtualDJ can send its master tempo as MIDI clock to a connected controller: the manual says to
right-click the controller in the Settings, Controllers device list and assign it as the MIDI Clock
Output. The unit always accepts clock on cable 0 and tracks BPM from it. The manual mentions tempo
only, so whether Start/Stop are sent is unknown. UNTESTED.

## First run (ten minutes)

1. Plug in one unit, start VirtualDJ. The Controllers tab should list **EP-DJ3000 Deck** with the
   mapper `EP-DJ3000`. Not listed: see [Troubleshooting](#troubleshooting).
2. Load a playing track. Put a hand on the platter: the track should stop (the unit's touch note comes
   from its hold pad; no stop means the pad is not seeing your hand, so run the app's Hold pad step or
   `docs/BRINGUP.md` first). Spin it clockwise: it should play forward. Backwards is the unit's `jogInvert`,
   not an edit here.
3. One full revolution of the platter should be one full revolution of VirtualDJ's platter. If not,
   `full` in the definition is the knob (the VirtualDJ developers say the same of other jogs).
4. Quick tap on the platter toggles play. CUE, REV and the slide pot do what the table says.
5. PLAY and CUE LEDs follow the deck (the unit lights them from incoming notes 39 and 36, per the contract).
6. Plug in the second unit; each must drive its own deck (step 2 of the rig).
7. Audio: deck 1 comes out of Deck A's line out only, deck 2 out of Deck B's.

## Troubleshooting

* **Unit not listed.** Detection is by USB vid/pid `0x1209` / `0x0001`, the contract's *development
  placeholder* (a shared pid.codes test PID), so change both in `<device>` and `<audio>` once the unit
  has a real PID. Otherwise: Settings, Options, set `CreateMidiLog` to Yes (Advanced Options), restart
  VirtualDJ, read `Log Report.txt` in `Documents/VirtualDJ` for the unit's driver name and add
  `drivername="..."` (macOS: `macdrivername`) to `<device>`. As a last resort remove the identifiers: the
  docs say a definition without any applies to every MIDI interface no other definition matches.
* **No definition at all.** Use VirtualDJ's own mapper editor and enter the table above by hand. I have
  not verified how that editor adds keys for a device it has no definition for, which is why the
  definition file is the primary route.
* **Cable 1 / cable 2 ports.** The unit exposes three USB-MIDI cables (deck, TRS, linked deck). Only
  cable 0 is meant to be used here; which port VirtualDJ binds to on each OS is unverified.

## Not verified

Everything about VirtualDJ's behaviour. Specifically:

* That `vid`/`pid` detection finds the unit on macOS and Windows, and which of the three MIDI cables it binds.
* That two identical units get decks 1 and 2 (rather than decks 1 and 3) and keep them after reconnect.
* That `touchwheel` with no touch behaves sensibly. By default the unit sends no jog for an untouched platter once the hold
  pad has registered a touch (`jogUntouched` = `AUTO`), so this arises on a fresh unit before any touch, with `jogUntouched` =
  `REPORT`, or in a build with no hold pad; the docs only say the verb is "for a jogwheel with touch sensitivity".
* That `full="1024"` and a `64`-centred relative CC give one-to-one platter motion.
* That 14-bit sliders with a skipped half (the contract allows skipping an unchanged half) behave.
* That the `default=""` LEDs show cue and play state with `cue_stop` / `play_pause`.
* That VirtualDJ will drive two separate USB sound cards for the two deck outputs. The manual's
  external-mixer example uses one multi-channel card; per-line sound-card choice exists in the
  Advanced audio settings, but I found no official statement about two different cards.
* The `<audio>` preset, and the MIDI-clock output.
* That the unit ignores VirtualDJ's Identity Request SysEx (`F0 7E 7F 06 01 F7`). The contract says
  receivers ignore foreign headers, so it should.
* The `_CH2` twins rely on VirtualDJ treating `channel=""` as documented ("MIDI channel numbers in
  VirtualDJ start from 0").

## Doc inconsistencies

* **14-bit sliders.** The wiki's header example writes `ccmsb` and `cclsb`; its attribute list defines
  `cc` and `ccmsb` only, and its `EQ_LOW` example is `cc="0x28" ccmsb="0x08"` (0x28 = 0x08 + 0x20). I
  followed the list and the example: `cc` is the LSB controller, `ccmsb` the MSB. The unit's pairs are
  MSB + 32 (23/55, 7/39, 25/57, 24/56), which fits.
* **Windows `Devices` folder.** See [Install](#install).

## Developing

```sh
node drivers/test/virtualdj/check.mjs     # needs no dependencies
xmllint --noout drivers/virtualdj/Devices/EPDJ3000.xml drivers/virtualdj/Mappers/EP-DJ3000.xml
```

`check.mjs` derives the expected elements from `protocol/epdj3000.json`, so a changed CC number, jog
resolution, default channel or USB id fails it until the XML follows. A new contract control fails it
until it is either bound (`BIND` in the script) or listed as deliberately unmapped. It also feeds
deliberately broken copies to the same validator to show each check can fail. `vdj-verbs.txt` is a typo guard built from the
manual's verb list, not a copy of the manual.

## Sources

Read on 2026-11-01.

* Definition reference: <https://www.virtualdj.com/wiki/ControllerDefinitionMIDI.html>
* Definition plus mapping overview and install folders: <https://www.virtualdj.com/wiki/Controller%20Developers.html>
* Mapping file: <https://www.virtualdj.com/wiki/ControllerMappingFile%20v8.html>
* VDJscript basics (implicit parameters: a jogwheel adds a relative decimal, +1.0 = one revolution): <https://virtualdj.com/wiki/VDJScript>
* Verb list (`touchwheel`, `touchwheel_touch`, `cue_stop`, `play_pause`, `reverse`, `dump`, `filter`, ...): <https://www.virtualdj.com/manuals/virtualdj/appendix/vdjscriptverbs.html>
* Touch jog pattern from the VirtualDJ development manager (jog to `touchwheel`, touch note to `touchwheel_touch`, tune with `full`): <https://virtualdj.com/forums/222179/VirtualDJ_Technical_Support/Mapping_Jog_Wheels_(DDJ-1000).html>
* Two identical controllers: <https://virtualdj.com/forums/211768/General_Discussion/Can_I_use_2_identical_controllers_with_VDJ_8%3F.html>
* Finding vid/pid, driver name and the MIDI log: <https://virtualdj.com/wiki/controlleridentification.html>
* External mixer audio setup: <https://www.virtualdj.com/manuals/virtualdj/settings/audiosetup/externalmixer.html>
* Audio settings: <https://www.virtualdj.com/manuals/virtualdj/settings/audiosetup.html>
* Controllers settings (licensing, MIDI clock output): <https://www.virtualdj.com/manuals/virtualdj/settings/controllers.html>
