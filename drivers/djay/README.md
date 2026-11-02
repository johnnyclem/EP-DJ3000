# EP-DJ3000 for djay Pro

**Format: UNVERIFIED / manual. There is no importable file in this folder, on purpose.**
djay Pro for Mac does import mapping files, but Algoriddim publishes no specification for them, and
I could not confirm one from an official source. So this folder is a precise **MIDI Learn cheat
sheet** (control, message, djay target and action, in the order djay's learn UI asks for them), not a
generated file. Nothing here has been run in djay Pro (closed source, macOS/iOS/Windows only).

Why no file, specifically:

* The official [Sharing MIDI mappings][share] page says you export with the Share icon and import by
  double-clicking the file. It gives no extension, no schema, no folder path.
* The extension `.djayMidiMapping` appears only in community posts (for example the
  [DDJ-FLX4 map][flx4]).
* An account titled "Algoriddim Staff" wrote in the [file-format thread][fmt] (December 2023): "we do
  not have any documentation for MIDI Mapping outside of the built-in MIDI Learn application" and
  "Editing xml or json files will be at your own risk essentially and is not officially supported".
  The same thread has a Windows user (djay Pro 5.0.6405.0) saying the file "looks like json", while
  the staff member had "only attempted to edit the xml files for Mac". So the format may differ by
  platform and version.
* I did not download or inspect any real mapping file. A hand-written one would be a guess at an
  undocumented, unsupported, possibly platform-dependent format that I could not load to test.

What to do instead: do the 10-minute manual mapping below once per unit, then use djay's own Share
icon to export it. **That export is the artifact worth committing here** (as
`EP-DJ3000 Deck A.djayMidiMapping` and `... Deck B ...`); anyone else installs it by double-clicking
(official). Until then this README is the source of truth.

Requirements: djay Pro for Mac with an active **PRO subscription**, which the official
[mapping page][mapping] says "is required to use MIDI mapping". The unit is not a supported
controller, so djay offers MIDI Learn for it ([help article][howto]).

## Rig: two units into the EP-136

Same idea as `docs/ARCHITECTURE.md` section 1: each unit is a controller plus its own USB sound
card, and the EP-136 mixes in hardware.

1. **Slide pot as tempo fader.** Nothing to set. The unit's default `faderRole` is `PITCH` (CC 25 / 57), which the cheat
   sheet below maps to Tempo. That is the factory role because it is the one that does something in this rig: with an
   external mixer, djay's crossfader and line faders do nothing audible, so `CHANNEL_VOLUME` (Line Volume) and
   `CROSSFADER` would leave the pot inert. The other roles are for djay's internal mixer, and are one SysEx frame
   (`docs/BRINGUP.md` section 2b): set `faderRole` to `CHANNEL_VOLUME` (config key 11, value 1) or to `CROSSFADER`
   (config key 11, value 0), and back to the default with `faderRole` to `PITCH` (config key 11, value 2);
   `SAVE_CONFIG` keeps it.
2. **Mixer mode External.** Official [audio devices page][audio]: in External mode "Audio is routed to
   separate channels" and you assign a **stereo output per deck** (Decks 1-4), plus Sampler, Looper and
   Pre-cueing. Per the [external mixers page][ext], click Apply with the mixer powered on, then
   "keep track of which output goes to which mixer channel".
3. **The catch: that page describes one audio interface.** Two EP-DJ3000 sound cards are two
   devices, and the official pages do not say whether djay can use outputs from more than one
   device. The route I can propose is a macOS **Aggregate Device** (Audio MIDI Setup > Add > Create
   Aggregate Device, tick both units, same sample rate, drift correction on the one that is not the
   Sync Source; Apple: [create][agg], [sync and drift][aggset]). That gives one 4-channel output:
   Deck 1 -> channels 1-2 (Deck A's card), Deck 2 -> channels 3-4 (Deck B's card). **Untested with
   djay.** The community has a thread titled "Pre-cueing with multiple sound cards not working on
   Djay Pro for Mac" (I could not open it), so expect friction. Fallback using only documented
   behaviour: one multi-channel class-compliant interface for audio, units set to `dacMode` OFF.
4. Run each unit's line out into its own EP-136 channel; EQ, cue and master are the EP-136's job.

## Manual MIDI Learn cheat sheet

The flow, from the official [Mac mapping page][mapping]: connect the unit, accept the "configure this
device?" pop-up (or MIDI options in the toolbar, **Configure** next to the unit), press or move a
control so djay adds its note or CC to the table, choose a **Target** and an **Action**, test it
(no save needed), **Done** to save. **Advanced** has Control type, Channel (1-16), Pickup mode,
Speed/Reaction and MIDI Out.

Do each unit separately: the two have different USB names (`EP-DJ3000 Deck A` / `EP-DJ3000 Deck B`).
Deck A unit = MIDI channel 1 = Target **Deck 1**; Deck B unit = channel 2 = Target **Deck 2**. djay
fills in the channel when it learns; the table below lists the numbers it should show.

"Name source": **official** means the label is in Algoriddim's manual; **community** means it comes
from the community-maintained [action list][actions], written for djay for iOS ("the available
mappings on a PC or MAC might differ"). The Action menu in your version is authoritative.

| Contract id | You do | djay learns | Target > Action | Advanced | Name source |
|---|---|---|---|---|---|
| `play` | tap the platter | `Note 39` | Deck 1 > Play/Pause | Button. MIDI Out: Note, 39, so the unit shows play state | official |
| `cue` | press CUE | `Note 36` | Deck 1 > CUE | Button. MIDI Out: Note, 36 lights the button | community |
| `rev` | hold REV | `Note 37` | Deck 1 > Reverse | Button (plays backward while held, per the list) | community |
| `touch` | finger on the platter | `Note 38` | Deck 1 > Jog Wheels > Scratching Mode | Button (jog Plan A) | community |
| `jog` | drag clockwise | `CC 22` | Deck 1 > Jog Wheels > Scratch (jog Plan A) or Scratch (no touch detection) (Plan B) | Rotary (3Fh/41h); Speed and Reaction by feel | official |
| `pitch` | sweep the pot (default `faderRole` = PITCH) | `CC 25` (MSB), ignore `CC 57` (LSB) | Deck 1 > Tempo | Fader/Knob; Pickup mode on | community |
| `crossfader` | sweep the pot (`faderRole` = CROSSFADER) | `CC 23` (MSB), ignore `CC 55` (LSB) | Mixer > Crossfader | Fader/Knob; internal mixer only | community |
| `channelVolume` | sweep the pot (needs `faderRole` = CHANNEL_VOLUME) | `CC 7` (MSB), ignore `CC 39` (LSB) | Mixer > Deck 1: Line Volume | Fader/Knob; internal mixer only | community |
| `knob` | turn the orange knob | `CC 24` (MSB), ignore `CC 56` (LSB) | Deck 1 > Effects > Filter | Fader/Knob (the list calls Filter a Rotary action; this knob is absolute) | community |

Not mapped: `bpm`, `bpmNudge`, `tap`, `transport`. The unit only sends them in its own clock-master
modes, which are not a DJ-software mode. (djay has a BPM Tap action; the unit's `tap` note is output
from SYNC_MASTER, not a button you press.)

### Jog plans

The official [jog wheel page][jog] says a touch-sensitive wheel maps **Scratch** (a CC) and a wheel
without touch maps **Scratch (no touch detection)**, plus **Pitch bend** for the side of the wheel.
This unit has one CC for rotation and a separate note for touch, which is neither Pioneer pattern
exactly, so I can't tell which works. Try in order:

* **Plan A, touch aware:** `touch` Note 38 -> Scratching Mode, `jog` CC 22 -> Scratch. This is the
  closest to the two docs combined: touching stops the track, moving scratches. If the track does not
  stop on touch, the community list says Scratch alone needs the deck paused first.
* **Plan B, simple:** `jog` CC 22 -> Scratch (no touch detection), ignore the touch note. With the
  default touch-ring platter the unit only reports motion while a finger is on it, so this still
  scratches, but touching does not stop playback.
* **Nudge:** the unit has no separate "side of the wheel" message. With a free-spinning `AS5600`
  platter you can map CC 22 to **Pitch bend** instead of Scratch to nudge tempo; you cannot have
  both from one CC.

Control type: the unit sends `64 + delta` (64 = no motion, 63 = -1, 65 = +1, delta up to 63).
That is the "3Fh/41h" family; "01h/7Fh" would be two's complement and is wrong. The official page says
to "test each option", and the Reaction slider trades smooth for responsive.

### Gotchas while learning

* **14-bit pairs.** The pot and knob send an MSB CC then an LSB CC (MSB + 32), and may skip an
  unchanged half. djay documents no 14-bit support, so expect 128 steps from the MSB. When you learn a
  fader, look at the table: it must show the **MSB** (23, 25, 7, 24), not the LSB (55, 57, 39, 56). If
  it shows the LSB, sweep the pot fully and learn again, or fix the number in the table.
* **Pickup mode** on the tempo fader, so a pot that does not match the on-screen slider does not make
  the tempo jump (official: it prevents sudden jumps on shared controls and is especially useful for
  tempo sliders).
* **Fader direction.** Wrong way round: djay's **Invert**, or the unit's `faderInvert`.
* **Two units, one crossfader.** Only if you set both units to `faderRole` = CROSSFADER, then both drive djay's one
  crossfader. The default PITCH gives each unit its own deck's Tempo, which is what the external-mixer rig wants;
  CHANNEL_VOLUME gives each its own Line Volume, which only matters with djay's internal mixer.

### Test for the biggest risk: does djay read the magnitude?

The community action list says a rotary "only delivers relative movements clockwise(1) or
counterclockwise(-1)". The unit sends bigger steps when you spin fast (up to 63 per message at up to
500 messages per second). If djay reads only the sign, scratching speed will not follow your hand.
Test: drag the platter slowly, then fast, in the same direction. Track speed should follow. If it
does not, djay is reading direction only; the fix would be a "one tick per message" jog mode in the
firmware, which the contract does not have today.

## Limitations

* No importable file yet (above). PRO subscription required for MIDI mapping.
* The Action labels in the table are partly from an iOS-era community list.
* djay's official docs do not mention 14-bit, so the high-resolution half of every pot is probably
  wasted.
* MIDI Out (LED feedback) is configurable per control (Channel, Type, Control, Value 0-127); I wrote the
  note numbers above but the exact feedback wiring is untested. Set its Channel to the unit's own (1 for the Deck A
  unit, 2 for the Deck B unit): a unit ignores feedback on any other channel (PROTOCOL.md section 2). The play
  note only lights the LED; it never starts the unit's clock.
* Android has no MIDI Learn; this README is for the Mac app. iOS and Windows have the same flow, but
  their menus and action lists differ.
* No clock/timecode path to djay.

## First run

1. Plug in one unit, open djay Pro, accept the configure pop-up, learn the rows above.
2. Load a track. Tap the platter: play/pause. CUE: set and return.
3. Jog Plan A, then Plan B if needed; run the magnitude test above.
4. Tempo pot with Pickup mode; flip direction if needed.
5. Share/export the mapping (Share icon, "Show in Finder") and commit it here.
6. Second unit on Deck 2, then the audio routing above.

## Not verified

Everything about djay. Specifically:

* That each Action label exists under that name in your version.
* Which jog plan works, and whether djay honours rotary step size.
* That djay learns the unit's note/CC at all from the Deck port, given three USB-MIDI cables per unit,
  and which port it listens to.
* That two units with different names keep separate mappings and Targets.
* The Aggregate Device route, and the External-mode output assignment with it.
* The mapping file format on any platform.

## Developing

```sh
node drivers/test/djay-rekordbox/check.mjs     # no dependencies
```

`check.mjs` reads the cheat-sheet table above and compares every note and CC number (and the LSB of
each 14-bit pair) with `protocol/epdj3000.json`, and fails if a control that the unit sends is neither
in the table nor in the "Not mapped" sentence.

## Sources

Read on 2026-11-01. Official:

* Mapping a MIDI device (Mac): <https://help.algoriddim.com/user-manual/djay-pro-mac/midi/mapping>
* Mapping jog wheels (Mac): <https://help.algoriddim.com/user-manual/djay-pro-mac/midi/jog-wheels>
* Sharing MIDI mappings (Mac): <https://help.algoriddim.com/user-manual/djay-pro-mac/midi/sharing>
* Midi mapping settings and controls (iOS; control types, Pickup, Speed, Reaction, MIDI Out): <https://help.algoriddim.com/user-manual/djay-ios/midi/mapping-controls>
* How to map a controller (PRO requirement): <https://help.algoriddim.com/topic/hardware/how-to-map-a-controller>
* Audio devices (Mac): <https://help.algoriddim.com/user-manual/djay-pro-mac/settings/audio-devices>
* External mixers (Mac): <https://help.algoriddim.com/user-manual/djay-pro-mac/hardware/external-mixers>
* Where djay stores custom MIDI mappings (`~/Music/djay/`): <https://help.algoriddim.com/hc/en-us/articles/360014912131-Where-does-djay-Pro-store-cue-points-and-other-metadata->
* Apple, aggregate devices: <https://support.apple.com/guide/audio-midi-setup/combine-audio-devices-a-single-aggregate-ams6e21c3f61/mac> and <https://support.apple.com/guide/audio-midi-setup/set-aggregate-device-settings-ams094c7edb4/mac>

Community:

* Action list (Google Sheet, linked from the thread): <https://community.algoriddim.com/t/list-of-available-midi-actions-in-djay/12165>
* File-format thread (moderator reply, Windows JSON report): <https://community.algoriddim.com/t/midi-mapping-file-format/23308>
* Example of the shared file name and Mac install folder: <https://community.algoriddim.com/t/ddj-flx4-midi-map-unofficial/19170>

[mapping]: https://help.algoriddim.com/user-manual/djay-pro-mac/midi/mapping
[jog]: https://help.algoriddim.com/user-manual/djay-pro-mac/midi/jog-wheels
[share]: https://help.algoriddim.com/user-manual/djay-pro-mac/midi/sharing
[howto]: https://help.algoriddim.com/topic/hardware/how-to-map-a-controller
[audio]: https://help.algoriddim.com/user-manual/djay-pro-mac/settings/audio-devices
[ext]: https://help.algoriddim.com/user-manual/djay-pro-mac/hardware/external-mixers
[agg]: https://support.apple.com/guide/audio-midi-setup/combine-audio-devices-a-single-aggregate-ams6e21c3f61/mac
[aggset]: https://support.apple.com/guide/audio-midi-setup/set-aggregate-device-settings-ams094c7edb4/mac
[actions]: https://community.algoriddim.com/t/list-of-available-midi-actions-in-djay/12165
[fmt]: https://community.algoriddim.com/t/midi-mapping-file-format/23308
[flx4]: https://community.algoriddim.com/t/ddj-flx4-midi-map-unofficial/19170
