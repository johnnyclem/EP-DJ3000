# EP-DJ3000 mapping for Mixxx

One mapping for both decks (MIDI channel 1 = `[Channel1]`, channel 2 = `[Channel2]`),
for Mixxx 2.4 and 2.5. Mixxx is open source (GPL) and has a documented scripting API, so
this is the reference driver for the unit.

**Status: written against the Mixxx docs and source, tested against a mock engine only.
It has never been loaded into a real Mixxx.** See [What is verified](#what-is-verified).

## Read this first: two decks into the EP-136 mixer

For the "poor man's CDJs" rig you send each deck to its own unit's USB audio and mix on
the Sidekick. In Mixxx that is *Preferences ‣ Sound Hardware ‣ Outputs ‣ Deck 1 / Deck 2*.
The Mixxx manual says Deck outputs "are taken post-fader, which means they include channel
volume and crossfader adjustments and applied effects", and that the internal mixer must
be left neutral when mixing on external hardware.

The unit's default `faderRole` is `PITCH` (CC 25 / 57): each unit's slide pot is a CDJ-style tempo fader
(`[ChannelN],rate`), Mixxx's crossfader is never touched, and two units do not fight over anything. Leave the
rest of Mixxx's mixer neutral (crossfader centred, EQ flat); nothing else needs setting.

Mixxx is the one app where the other roles are useful too, because the Deck outputs are post-fader:

- `CHANNEL_VOLUME` (CC 7 / 39, set it with one SysEx frame: `docs/BRINGUP.md` section 2b) makes each unit's pot cut its
  own deck's volume (`[ChannelN],volume`): a per-deck scratch cut that is audible on the Sidekick channel. Pick it
  when you want to cut with the slide pot and tempo comes from elsewhere. In VirtualDJ, djay Pro and rekordbox this
  role is inert with an external mixer, which is why it is not the default.
- `CROSSFADER` (CC 23 / 55) is the one to avoid with two units: by default `FADER_ROLE` sends it to Mixxx's master
  crossfader, which (decks on opposite sides) cuts one deck's output, and two units would both drive it. Use it for a
  single unit in a software-only setup.

The orange knob drives the deck's Quick Effect (filter). That one is deliberate: applied
effects are included in the Deck outputs, so it stays audible through the hardware mixer.

## Install

Three files go in the same folder. Mixxx looks for each `<file>` in the mapping's own
folder first, then Mixxx's system folder, so `epdj3000-protocol.js` has to sit next to the
XML:

| File | What |
|---|---|
| `EP-DJ3000.midi.xml` | MIDI bindings (generated, see [Developing](#developing)) |
| `EP-DJ3000-scripts.js` | the mapping logic |
| `epdj3000-protocol.js` | wire constants, generated from the contract by `tools/gen_protocol.py` |

Easiest on every OS: in Mixxx open *Preferences ‣ Controllers* and click **Open User
Mapping Folder**, copy the three files in, then close and reopen *Preferences ‣
Controllers* (no restart needed). That is the procedure in the Mixxx 2.5 manual.

The folder it opens, per the Mixxx wiki and manual:

| OS | User mapping folder |
|---|---|
| Linux | `~/.mixxx/controllers` (Flatpak: `~/.var/app/org.mixxx.Mixxx/.mixxx/controllers`) |
| Windows | `%LOCALAPPDATA%\Mixxx\controllers` (`C:\Users\<you>\AppData\Local\Mixxx\controllers`) |
| macOS | `~/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx/controllers` (the manual gives this for Mixxx 2.3; the wiki gives it without a version); `~/Library/Application Support/Mixxx/controllers` for 2.2 and earlier. The 2.5 manual states no macOS path for 2.4/2.5, so use the button above. |

Copy from a checkout of this repo, for example on macOS/Linux:

```sh
cp drivers/mixxx/EP-DJ3000.midi.xml drivers/mixxx/EP-DJ3000-scripts.js \
   drivers/mixxx/epdj3000-protocol.js "<user mapping folder>"
```

## Enable

1. Plug the unit in. *Preferences ‣ Controllers* lists the unit. A USB-MIDI device with
   several cables normally appears as one entry per port (Deck, TRS MIDI, Linked deck);
   the names depend on the OS and on the firmware's jack strings.
2. Select the unit's **Deck** port, choose **EP-DJ3000** in *Load Mapping*, tick *Enabled*, Apply.
3. A second unit on its own USB cable: repeat on that unit's Deck port. Set one unit to
   Deck B first (hold CUE + REV for 5 s) so it sends on channel 2.
4. One-cable mode (Deck B bridged through Deck A's "Linked deck" cable): also enable
   **EP-DJ3000** on the Linked deck port. Deck B arrives on channel 2, so it needs nothing else.
   Each enabled port gets its own script engine in Mixxx, so the mapping keeps its state per instance.
5. Audio: *Sound Hardware ‣ Outputs*, assign **Deck 1** to Deck A's USB audio device and
   **Deck 2** to Deck B's. For scratching, use a small audio buffer: the manual calls
   23-64 ms acceptable with a controller and recommends under 10 ms for vinyl control, which
   is the same responsiveness class.

## What each control does

| Unit | MIDI | Mixxx |
|---|---|---|
| Platter touched / released | note 38 | `engine.scratchEnable` / `scratchDisable` for the deck: 1024 intervals per rev, 33 1/3 rpm, alpha 1/8, beta alpha/32, ramp on both |
| Platter moved, touched | CC 22 (relative, 64 = none) | `engine.scratchTick(deck, ticks)`; positive = clockwise |
| Platter moved, untouched (the default AS5600 platter) | CC 22 | pitch bend: ticks x `BEND_SCALE` is added to `[ChannelN],jog`, clamped to Mixxx's documented -3..3. `jog` is an accumulator that Mixxx zeroes after each audio callback (`RateControl::getJogFactor`), and the unit can send several messages per callback, so the script adds rather than overwrites |
| Platter tap | note 39 | toggles `[ChannelN],play`. The touch down/up of the tap scratches for a moment and releases |
| CUE | note 36 | `cue_default` follows the button (honours Mixxx's cue mode setting). LED follows `cue_indicator` |
| REV | note 37 | `reverseroll` while held. See `REV_BEHAVIOR` |
| Slide pot, unit default (`PITCH`) | CC 25/57 | deck `rate`, regardless of `FADER_ROLE` |
| Slide pot, unit set to `CHANNEL_VOLUME` | CC 7/39 | deck `volume`, regardless of `FADER_ROLE` |
| Slide pot, unit set to `CROSSFADER` | CC 23/55 | per `FADER_ROLE`: `[Master],crossfader` (default), deck `volume`, or deck `rate` |
| Orange knob | CC 24/56 | `[QuickEffectRack1_[ChannelN]],super1` (the deck's filter knob) |
| Play LED | host to unit, note 39 | lit while `[ChannelN],play` is 1. Feedback only: it never starts the unit's clock, in any mode |
| Tempo set (unit as tempo source) | CC 20/52, BPM x 10 | `[ChannelN],bpm` = value / 10. Needs an analysed track, 20.0-300.0 only, ignored when within 0.05 BPM |
| Tempo nudge | CC 21 (relative) | `bpm` +/- 0.1 per step, clamped to 20-300 |
| Transport | CC 29 | 1 = `play` on, 2 = `play` off, 0 = `start_stop` (seek to start and stop) |
| Tap tempo | note 41 | `tempo_tap`, **Mixxx 2.5 or newer only** (the control does not exist in 2.4) |

Only the press of a note acts for play/tap; a release is any Note Off (`0x8n`, any
velocity) or Note On with velocity 0. The XML binds both forms because Mixxx does not fold
Note Off into Note On.

Fader and knob values are 14-bit (MSB then LSB, either half may be skipped) and are applied
as `engine.setParameter(value / 16383)`, so Mixxx's own range and taper for the control
apply. Nothing is applied until both halves have been seen once, which avoids a jump to
the bottom when Mixxx starts after the unit.

## Settings

At the top of `EP-DJ3000-scripts.js`, as properties of `EPDJ3000Mixxx`:

| Setting | Default | |
|---|---|---|
| `FADER_ROLE` | `"crossfader"` | `"crossfader"`, `"volume"` or `"rate"`; what CC 23/55 controls, i.e. a unit set to `faderRole` = `CROSSFADER`. Not used for the default CC 25/57 or for CC 7/39 |
| `REV_BEHAVIOR` | `"roll"` | `"roll"`: `reverseroll` while held (momentary reverse that resumes where the track would have been, Mixxx's "censor"). `"toggle"`: `reverse` flips each press, CDJ style |
| `BEND_SCALE` | `1.0` | untuned |
| `SCRATCH` | see file | `rpm`, `alpha`, `beta`, ramp on touch / release. alpha and beta are the values the Mixxx docs suggest as a starting point, untuned |

Why `roll` for REV: it mirrors the button's down/up exactly, has no latched state to fall
out of sync, and a momentary reverse is the useful gesture on a two-button scratch deck.
Switch to `"toggle"` if you want a latching reverse.

## Known limits

- **Scratching needs the touch note.** `scratchEnable` is only called on note 38, and on the default AS5600 platter
  that note comes from the one hold pad. Until that pad reliably sees a hand on the spinning platter
  (`docs/BRINGUP.md`, first bring-up step 3), every platter movement is a pitch bend and nothing scratches.
- **Not run in real Mixxx.** Scratch feel, pitch-bend strength, soft-takeover behaviour with
  a real pot, LED timing and the pitch fader's direction (it follows Mixxx's *rate slider
  direction* preference; flip `faderInvert` on the unit if it is backwards) are unknown
  until tried on the hardware.
- With two units both set to `faderRole` = `CROSSFADER` and `FADER_ROLE = "crossfader"`, both pots
  drive the one master crossfader; the last one moved wins, with no soft takeover (a crossfader should
  follow the hand).
- Deck volume, rate and the filter knob use soft takeover: a pot that disagrees with Mixxx's value is ignored
  until it reaches that value, so the deck does not jump. The pot can seem dead at first: move it until it passes Mixxx's current value.
- LED feedback for both decks is sent to every enabled port, on each deck's own channel. That is correct
  because a unit ignores channel-voice messages that are not on its own channel (PROTOCOL.md section 2,
  "Channel filter"; implemented and host-tested in the firmware core, not tried on hardware).
- Mixxx does not use MIDI clock (its MIDI controller code drops `0xF8`) and the mapping has
  no MTC, so a unit in SYNC_MASTER mode can set the deck tempo (CC 20/52) and transport but
  its clock and timecode go unused.
- Mixxx-to-unit tempo and transport (feeding the unit's clock from a deck) is not
  implemented. Only the cue and play LEDs are sent to the unit.
- 14-bit transient: when both halves change, the MSB is applied against the old LSB for
  under a millisecond (at most 0.8 % of travel) before the LSB arrives.
- The script does not filter the few ticks of movement inside a platter tap; Mixxx's
  scratch filter just sees them.
- If a touch-release message were lost, the deck would stay in scratch mode (frozen) until
  the next touch and release. The unit sends the release, so this needs a USB fault.

## What is verified

Run: `node drivers/test/mixxx/run.mjs` (Node only, no dependencies, exit code 1 on failure).

- **Verified, mock engine only:** all handler logic. The script is loaded with `node:vm`,
  files and init order taken from the XML's `<scriptfiles>`, and raw MIDI bytes built from the
  contract constants are routed through the XML's `(status, midino)` bindings to the
  handlers the way Mixxx does. The mock is strict: any Mixxx control or engine function
  not on the lists taken from the Mixxx docs throws. Covered: touch to `scratchEnable` per
  deck, tick sums, release, bend, play/cue/rev/tap/transport, both fader ends (14-bit 0 and
  16383) for crossfader/volume/rate/knob, skipped-half sweeps, bpm, LEDs, shutdown, deck by
  channel, and a second script instance for the linked cable.
- **Verified, static:** the XML parses with the test's own well-formedness check and with
  `xmllint`; every `<status>`/`<midino>` equals the set derived independently from
  `protocol/epdj3000.json`; the checked-in XML equals the generator output; the `mixxx/` constants
  file equals the one in `drivers/common` (the generator writes both). Engine signatures and control names were checked by
  reading the Mixxx 2.4 and 2.5 sources and manual, not by running them.
- **Not verified:** that real Mixxx accepts the XML and evaluates the script, that the
  unit's ports show up and bind as described, and everything listed under Known limits.

## Developing

`EP-DJ3000.midi.xml` is generated from the contract constants so its numbers cannot drift:

```sh
python3 tools/gen_protocol.py                 # writes the constants, here and in drivers/common
node drivers/test/mixxx/gen-xml.mjs           # rewrite the XML
node drivers/test/mixxx/gen-xml.mjs --check   # fail if it is stale
node drivers/test/mixxx/run.mjs
```

## Documentation relied on

- Scripting: <https://github.com/mixxxdj/mixxx/wiki/midi-scripting>
- Mapping file format: <https://github.com/mixxxdj/mixxx/wiki/Midi-Controller-Mapping-File-Format>
- Mapping locations: <https://github.com/mixxxdj/mixxx/wiki/Controller-Mapping-File-Locations>
- Controls: <https://manual.mixxx.org/2.5/en/chapters/appendix/mixxx_controls.html> (and 2.4)
- Installing a mapping, settings directory, Sound Hardware outputs: the 2.5 manual pages
  `controlling_mixxx`, `appendix/settings_directory`, `preferences/sound_hardware`
- Source (`main`, 2.4, 2.5): `controllerscriptinterfacelegacy.{h,cpp}` (scratch and
  engine API), `legacycontrollermappingfilehandler.cpp` (script lookup, `0x` parsing),
  `midicontroller.cpp` (no Note Off folding, handler arguments),
  `controllerscriptenginelegacy.cpp` (init/shutdown via `functionprefix`),
  `controllerscriptenginebase.cpp` (`console`), `controller.cpp` (one script engine per
  port), `bpmcontrol.cpp` (`bpm` accepts writes)
- Shipped example: `Pioneer-DDJ-400-script.js` / `.midi.xml`
