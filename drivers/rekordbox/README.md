# EP-DJ3000 for rekordbox

**Status: UNTESTED in rekordbox, and the file format is community-documented, not official.**
Nothing in this folder has been loaded into rekordbox (it is closed source, runs on neither Linux nor
this sandbox). What is checked (`node drivers/test/djay-rekordbox/check.mjs`): the CSVs are
structurally consistent with the layout other people published, and every MIDI number in them
agrees with `protocol/epdj3000.json`. Whether rekordbox accepts the files, and what it does with the
jog, is a bet until you run [First run](#first-run).

Three things you should know before you spend time here:

1. **AlphaTheta documents the buttons, not the file.** The [MIDI LEARN guide][guide] describes
   IMPORT and EXPORT of a "MIDI setting file". It does not describe the CSV inside. The layout used
   here comes from people who read the CSVs that ship in the app ([sources](#sources)), so a rekordbox
   update can break it without notice. AlphaTheta also writes: *"We do not provide support for
   configuring third party products."*
2. **The jog and platter touch probably will not work.** The official MIDI types list has no jog
   type. In the shipped files jog rows are flagged `RO` ("rekordbox hardware only"), and every report I
   found of using a non-Pioneer jog needed a hack that makes rekordbox believe a Pioneer controller is
   connected. Those rows are in the CSV, **disabled**. See [Jog and touch](#jog-and-touch).
3. **A hand-made CSV may crash rekordbox 7.** One user on the [DJ TechTools forum][numark] wrote that
   after making a CSV by hand ("I messed up something") rekordbox 7 started quitting with error
   messages. I do not know what was wrong with theirs, but EXPORT your current setting before you
   IMPORT anything (step 3 below), so you can get back.

## Files

| File | What |
|---|---|
| `EP-DJ3000-Deck-A.midi.csv` | Unit configured as Deck A (MIDI channel 1) -> rekordbox deck 1 |
| `EP-DJ3000-Deck-B.midi.csv` | Unit configured as Deck B (MIDI channel 2) -> rekordbox deck 2 |
| `generate_csv.py` | Writes both from `protocol/epdj3000.json`; `--check` fails if they are stale |

Two files because the MIDI window keeps one setting per connected device, and the two units have
different USB names (`EP-DJ3000 Deck A` / `EP-DJ3000 Deck B`). Regenerate with
`python3 drivers/rekordbox/generate_csv.py`. The generator has no channel or deck options: it assumes
the default channels (A = 1, B = 2, from `midiChannel` = auto) and A -> deck 1, B -> deck 2. If you changed
`midiChannel`, learn by hand instead ([cheat sheet](#manual-midi-learn-cheat-sheet)).

## Import

Official steps, from the [MIDI LEARN guide][guide] (sections 3.1, 3.2, 3.6, 3.7):

1. Connect the unit by USB, start rekordbox, switch to **PERFORMANCE** mode.
2. Click the **MIDI** button (top right, next to Preferences). In the device menu pick the unit.
   With two units connected, pick one at a time.
3. Click **EXPORT** and save what is there. **Do this first**: it is your way back, and the exported
   file shows you what rekordbox itself writes for this unit.
4. Open that export in a text editor and compare the first two lines with ours. They should be
   `@file,1,<name>` and `#name,function,type,input,deck1,deck2,deck3,deck4,output,deck1,deck2,deck3,deck4,option,comment`,
   fifteen comma-separated fields per row. If rekordbox wrote something different, stop: the layout
   changed, use the [cheat sheet](#manual-midi-learn-cheat-sheet) and tell me what you saw.
5. Click **IMPORT**, answer the save prompt, pick our CSV for that unit. It overwrites the list for
   the selected device.
6. Check the list: PlayPause, Cue, TempoSlider, CrossFader, Gain should be there with MIDI IN codes.
   Close the window (rekordbox saves on close).

Not recommended: dropping files into the app's own `MidiMappings` folder (the bundle is signed, and
the folder is only documented by third parties). Its bundled `DDJ-*.midi.csv` files are, however, the
best reference for comparing rows; do not copy them into this repo (AlphaTheta's files).

## What is mapped

Numbers are from `protocol/epdj3000.json`. Deck A unit shown (channel 1); the Deck B unit is channel 2.
"Seen in" says where I found the command name as an actual row. A name seen only in a list is
`UNVERIFIED` and its row is **disabled** in the CSV (leading `#`).

| Control | Unit sends | rekordbox command / type | State | Command name seen in |
|---|---|---|---|---|
| `play` | Note 39 pulse (tap) | `PlayPause` / Button, LED out | active | a shipped DDJ-FLX10 row quoted in the [spec][spec]; [Numark thread][numark] |
| `cue` | Note 36 | `Cue` / Button, LED out | active | DDJ-FLX4 row on [skills.lc][skills]; [Numark thread][numark] |
| `pitch` | CC 25 / 57 (default role) | `TempoSlider` / KnobSliderHiRes | active | DDJ-FLX4 row on [skills.lc][skills]; [Numark thread][numark] |
| `crossfader` | CC 23 / 55 | `CrossFader` / KnobSliderHiRes | active | DDJ-FLX4 row on [skills.lc][skills]; [Numark thread][numark] |
| `knob` | CC 24 / 56 | `Gain` / KnobSliderHiRes | active | DDJ-FLX4 row on [skills.lc][skills] only |
| `rev` | Note 37 | `Reverse` / Button | disabled, UNVERIFIED name | a command list on [skills.lc][skills], no row |
| `channelVolume` | CC 7 / 39 | `ChannelFader` / KnobSliderHiRes | disabled, UNVERIFIED name | a command list on [skills.lc][skills], no row |
| `touch` | Note 38 | `JogTouch` / JogTouch | disabled, experimental | named by [DJ TechTools 2017][jog2017]; type name from the [67-file census](https://github.com/guillain/DJ-MIDI-Studio/pull/354) |
| `jog` | CC 22 relative | `JogScratch` / JogRotate | disabled, experimental | the [spec][spec] quotes a `JogScratch,...,JogRotate,B022,...,RO` row from a shipped file |

Not mapped: `bpm`, `bpmNudge`, `tap`, `transport`. The unit only sends them in its own clock-master
modes, which have nothing to do with rekordbox. `knob` is `Gain` only because it is a command name I
could verify; it is an arbitrary choice for an orange knob, re-learn it to whatever you like. The
slide pot sends exactly one of the three fader pairs, chosen by the unit's `faderRole`; all three rows
exist, only one ever receives data. The unit's default `faderRole` is `PITCH` (CC 25 / 57), whose `TempoSlider` row is
active, so a factory unit's pot is a tempo fader in rekordbox with nothing to set. `CHANNEL_VOLUME` is the role whose
`ChannelFader` row is disabled (unverified command name): its pot does nothing until you enable that row after confirming
the name in the ADD menu.

How the rows address a single-deck unit, in case you edit them: `input` is the code on channel 0, the
unit's channel goes into that deck's column, other deck columns are empty (`9027,0,,,` = Deck 1;
`9027,,1,,` = Deck 2, which is channel 0 + 1). The crossfader is global in rekordbox, so its row
carries the channel in the code (`B017` / `B117`) and no deck columns, like the shipped row.

## Rig: two units into the EP-136

Same idea as `docs/ARCHITECTURE.md` section 1: each unit is a controller plus its own USB sound
card, and the EP-136 mixes in hardware.

1. **Slide pot as tempo fader.** Nothing to set: the unit's default `faderRole` is `PITCH` (CC 25 / 57), which the
   active `TempoSlider` row maps. That is the factory role because it is the one that does something in this rig: with an
   external mixer, rekordbox's crossfader and channel faders do nothing audible anyway, so `CHANNEL_VOLUME` (whose
   `ChannelFader` row is disabled, see [What is mapped](#what-is-mapped)) and `CROSSFADER` would leave the pot dead.
   The other roles are for rigs without an external mixer, and are one SysEx frame (`docs/BRINGUP.md` section 2b):
   set `faderRole` to `CHANNEL_VOLUME` (config key 11, value 1) or `CROSSFADER` (config key 11, value 0), and back
   to the default with `faderRole` to `PITCH` (config key 11, value 2); `SAVE_CONFIG` keeps it.
2. **Mixer mode.** Preferences > Audio > **Mixer Mode: External** ("use an external DJ mixer/DJ
   controller"). The [rekordbox manual][manual] (pp. 235-236) says this gives *six outputs: track
   decks 1 through 4, sampler deck and preview*, assigned under **Output channels**.
3. **The catch: one audio device.** The manual describes a single **Audio** device. Two EP-DJ3000
   sound cards are two devices. The route I can propose is a macOS **Aggregate Device**
   (Audio MIDI Setup > Add > Create Aggregate Device, tick both units, same sample rate, drift
   correction on the one that is not the Sync Source; [Apple][aggregate]). That gives one 4-channel
   output: Deck 1 -> channels 1-2 (Deck A's card), Deck 2 -> channels 3-4 (Deck B's card).
   Neither AlphaTheta nor Apple documents this combination with rekordbox, so it is **untested**.
   Fallback that uses only documented behaviour: one multi-channel class-compliant interface for audio
   (units set `dacMode` OFF, MIDI only).
4. Run each unit's line out into its own EP-136 channel; EQ, cue and master are the EP-136's job.
5. The sixth External output ("preview") is rekordbox's library preview player. Headphone cueing
   for the two decks is the EP-136's job, not rekordbox's.

Licensing, from rekordbox.com: the [plan page][plan] lists "Connecting to Hardware Unlock-eligible DJ
equipment" for the Free plan. The unit is not Hardware Unlock equipment. Which plan unlocks MIDI Learn
with third-party gear I could not establish; expect to need a paid plan.

## Jog and touch

What the sources say, and what that means for this unit:

* The official **Type** list (Button, Pad, Knob/Slider 7- or 14-bit, Rotary, Indicator, Value) has no jog
  type. `JogRotate` and `JogTouch` exist only in the shipped files, where they carry `RO`.
* A DJ TechTools forum user ([thread][mf3d], 2018) says anything marked `RO` "is Rekordbox only
  hardware (like jogs for example) and it will get trashed as you re-import it".
* The only working route anyone describes is the 2017 [DJ TechTools hack][jog2017]: rename a virtual
  MIDI port to a Pioneer controller's name, save the CSV under that controller's name, and translate
  the jog values into the ranges that controller sends. On the [forum thread][hack-thread] its author
  got pitch bend and touch-to-stop working on a Vestax VCI-100 but called scratching poor. A 2019
  poster in the [Numark thread][numark] says the same hack made a DJ2GO2 work ("everything works",
  no detail about scratching). A December 2024 poster on rekordbox 7 could not get it going: the
  jog was "not learnable" and the virtual Pioneer device would not learn buttons.
* None of that is this unit. It shows the route is fragile and version dependent, not that it fails. (This unit's `touch`
  note is the hold pad's: a hand on the spinning platter, not its motion.)
* If you want to try anyway: delete the leading `#` on the `JogTouch` and `JogScratch` rows, import,
  and see whether the rows survive (EXPORT again and look). One MIDI code cannot drive two functions,
  so the unit's single jog CC cannot be both `JogScratch` and `JogPitchBend`; swap the command name if
  you prefer nudging.
* Scale is unknown. The unit sends `64 + delta`, delta up to 63 at up to 500 Hz, 1024 ticks per
  revolution, fixed by the contract (no gain setting). Pioneer jogs send small values around 64
  (a DDJ-GRV6 shift-jog sits in 59-71 per [spec][spec]); how rekordbox scales larger steps I could not find.
  If it feels wrong, a MIDI translator in front of the unit is the only lever today.
* Relative encoding: the [spec][spec] calls `Rotary` a "relative encoder"; the [skills.lc][skills]
  summary says 64 is the centre and above 64 is clockwise. The unit's encoding is in that family.
  Whether rekordbox honours the magnitude or only the sign is unknown.

Practical result: expect play, cue, tempo and the library to work through rekordbox and the jog to
need a different app (Mixxx, VirtualDJ) until someone proves otherwise on hardware.

## Other limitations

* **14-bit.** Knob/Slider (0h-3FFFh) exists officially. The CSV carries only the MSB code and rekordbox
  must take the LSB from MSB + 32 (CC 23 -> 55, 25 -> 57, 24 -> 56, 7 -> 39, all true in the contract; the
  generator refuses to run if that ever changes). The +32 pairing is MIDI convention and consistent with
  the shipped `CrossFader B61F` row, not a documented guarantee. The contract also lets the unit skip
  an unchanged half; whether rekordbox is happy with that is untested.
* **Learn picks up the wrong half.** If you learn a fader by hand, check MIDI IN shows the **MSB**
  code (`B017`, `B019`, `B018`), not the LSB (`B037`, ...). The unit sends MSB then LSB, and on a slow
  move may send only the LSB. Sweep the fader fully and learn again.
* **LEDs.** `PlayPause` and `Cue` rows have `output` = `input`, and the guide says rekordbox sends the
  same code to MIDI OUT for functions with an indicator. The unit lights its buttons from Note 39 and
  Note 36 with velocity > 0, which is what Pioneer-style feedback sends. The output deck columns mirror the input
  ones, so the feedback should go out on the unit's own channel, the only one it listens to (PROTOCOL.md section 2).
  Untested.
* **One code, one function** (official). The three fader pairs use different CCs so this never bites.
* **Fader direction** is whatever the pot is; `faderInvert` flips it on the unit.
* **No timecode / clock to rekordbox.** The unit's MIDI clock, MTC and transport output (SYNC_MASTER)
  is for other gear. I did not find rekordbox consuming MIDI clock; it has Ableton Link instead
  (manual p. 153 reference), which this unit does not speak.

## Manual MIDI Learn cheat sheet

Use this if IMPORT fails, if you changed `midiChannel`, or to double-check an imported row. It is the
official flow ([guide][guide] 3.4-3.5): MIDI button > pick the unit > click a tab > **ADD** and pick the
function (or click an existing row) > turn **LEARN** on > operate the control > read the code in **MIDI
IN**. The ADD menu is the authoritative list of function names for your version; tab names below are
from third-party documentation. Codes are Deck A / Deck B (channel 1 / 2).

| Contract id | Tab, function (ADD) | Type | You do | MIDI IN should read |
|---|---|---|---|---|
| `play` | DECK, `PlayPause` | Button | tap the platter, or learn with the PLAY note | `9027` / `9127` |
| `cue` | DECK, `Cue` | Button | press CUE | `9024` / `9124` |
| `pitch` | DECK, `TempoSlider` | Knob/Slider (0h-3FFFh) | sweep the pot (default `faderRole` = PITCH) | `B019` / `B119` |
| `crossfader` | MIXER, `CrossFader` | Knob/Slider (0h-3FFFh) | sweep the pot (needs `faderRole` = CROSSFADER) | `B017` / `B117` |
| `channelVolume` | MIXER, channel fader | Knob/Slider (0h-3FFFh) | sweep the pot (needs `faderRole` = CHANNEL_VOLUME) | `B007` / `B107` |
| `knob` | MIXER, any knob function | Knob/Slider (0h-3FFFh) | turn the orange knob | `B018` / `B118` |
| `rev` | DECK, any reverse function | Button | press REV | `9025` / `9125` |
| `touch` | not in the UI | - | - | `9026` / `9126` |
| `jog` | not in the UI | - | - | `B016` / `B116` |

For a single-deck unit rekordbox needs to know which deck the row serves. After learning, set the
**Deck** cell for the Deck A unit to deck 1 and for the Deck B unit to deck 2. (The Deck cell is
described in the guide as a "4-digit number in hex", which I do not understand; leave it at what
rekordbox proposes if it is unclear, and compare with the exported CSV.)

## First run

1. Import (above), open PERFORMANCE mode, load a track on deck 1.
2. Tap the platter: deck 1 plays/pauses. Does the unit's LED feedback follow the deck state?
3. CUE: set and return. REV: nothing yet (disabled row). Tempo pot (the default role): moves deck
   1's tempo, in the direction you expect (flip with `faderInvert`).
4. Export again: did rekordbox keep your disabled and RO rows, change any, or reorder? That tells you what
   it parsed.
5. Second unit on deck 2, then the audio routing above.
6. Only then try the jog rows.

## Not verified

Everything about rekordbox's behaviour. Specifically:

* That rekordbox accepts these CSVs at all (layout, `@file,1,<name>` header, trailing empty fields, LF
  line endings, no BOM; shipped files are UTF-8 and some have a BOM). The rows in the 2019 [Numark
  thread][numark] have fewer than 15 fields (that may be how the forum rendered them, or an older
  layout); the header lines there match ours.
* That a non-Pioneer, class-compliant device with three USB-MIDI cables appears in the MIDI window, and
  which of its ports is used for cable 0 (the Deck port).
* That the `@file` name must, or need not, match the device name.
* Every command name beyond the four quoted in rows by several people; `Gain` has one source;
  `Reverse` and `ChannelFader` are not in a row anywhere I read.
* Jog, touch, scale and relative magnitude, 14-bit LSB handling, LED feedback.
* The Aggregate Device route and whether rekordbox lists it as an Audio device.
* Plan requirements for MIDI Learn with third-party equipment.
* The rekordbox 6 vs 7 layout (the community spec lists this as unknown).

## Developing

```sh
python3 drivers/rekordbox/generate_csv.py          # rewrite both CSVs
python3 drivers/rekordbox/generate_csv.py --check  # exit 1 if stale
node drivers/test/djay-rekordbox/check.mjs         # no dependencies
```

`check.mjs` decodes every row of both CSVs independently of the generator and compares the
status byte, data byte, channel, deck column, type and LED output with the contract; it checks that
unverified and jog rows stay disabled, that no two active rows share a MIDI code, and that this
README's cheat-sheet codes match. It also feeds deliberately broken copies to the same validator to
show each check can fail.

## Sources

Read on 2026-11-01. Official sources first.

* [MIDI LEARN Operation Guide][guide] (rekordbox 7.0.5, EN PDF): MIDI window, types, IMPORT/EXPORT, the third-party disclaimer.
* [rekordbox 7.2.18 manual][manual] (EN PDF): Mixer Mode and Output channels, pp. 235-236.
* [Mixer Mode FAQ](https://rekordbox.com/en/support/faq/operation-hint) and [plans][plan].
* [Apple: aggregate devices](https://support.apple.com/guide/audio-midi-setup/combine-audio-devices-a-single-aggregate-ams6e21c3f61/mac) and [sync/drift settings][aggregate].
* Community, CSV layout: [REKORDBOX-MIDI-CSV-SPEC.md][spec] (reverse-engineered from rekordbox 7.0.5's shipped CSVs),
  [DJ-MIDI-Studio PR 354](https://github.com/guillain/DJ-MIDI-Studio/pull/354) (67 shipped CSVs),
  [DJ TechTools 2019 CSV guide](https://djtechtools.com/2019/06/24/hacking-rekordbox-fx-and-adding-rmx-1000-control/) (15 fields, `#` comments, options).
* Community, example rows: [Numark DJ2GO2 thread][numark], [skills.lc mirror of rb-midi-sniffer's doc][skills], [rb-midi-sniffer][sniffer].
  I treated the sniffer's separate `RB_MIDI_DEV_README.md` as unreliable (it describes a "Load" button that the official guide does not have) and used nothing from it.
* Community, jog: [DJ TechTools 2017][jog2017] and its [forum thread][hack-thread]; [RO rows being dropped][mf3d].

[guide]: https://cdn.rekordbox.com/files/20241203210623/rekordbox7.0.5_midi_learn_operation_guide_EN.pdf
[manual]: https://cdn.rekordbox.com/files/20260807093645/rekordbox7.2.18_manual_EN.pdf
[plan]: https://rekordbox.com/en/plan/
[aggregate]: https://support.apple.com/guide/audio-midi-setup/set-aggregate-device-settings-ams094c7edb4/mac
[spec]: https://github.com/icherniukh/rb-midi-sniffer/blob/main/REKORDBOX-MIDI-CSV-SPEC.md
[sniffer]: https://github.com/icherniukh/rb-midi-sniffer
[skills]: https://skills.lc/icherniukh/rb-midi-sniffer/icherniukh-rb-midi-sniffer-claude-skills-rekordbox-midi-integration-skill-md
[numark]: https://forum.djtechtools.com/t/numark-dj2go-2-mapping-for-rekordbox-mac/77902
[jog2017]: https://djtechtools.com/2017/05/08/hack-rekordbox-use-controllers-jogwheels/
[hack-thread]: https://forum.djtechtools.com/t/how-to-hack-rekordbox-for-using-the-jogwheels-of-any-controller-almost/75951
[mf3d]: https://forum.djtechtools.com/t/midi-fighter-3d-relordbox-possible-solution/76911
