# drivers/common: the wire constants

`epdj3000-protocol.js` is **generated** from [`protocol/epdj3000.json`](../../protocol/epdj3000.json)
by `python3 tools/gen_protocol.py` and must not be edited (`--check` fails if it is stale; so does
`node drivers/test/run-all.mjs`). It is plain ES5, defines one global `EPDJ3000`, and exports it under
CommonJS, so the same file loads in Node, in Mixxx's script engine and in a browser.

`drivers/package.json` sets `"type": "commonjs"`: from a `.js` file in `drivers/` use `require()`, from a
`.mjs` use `createRequire(import.meta.url)` (the tests do). Wire format and semantics are in
[`docs/PROTOCOL.md`](../../docs/PROTOCOL.md); this file only says how to use the constants.

**Copies.** Mixxx resolves script files relative to the mapping folder, so `drivers/mixxx/` holds a
byte-identical copy. After regenerating: `cp drivers/common/epdj3000-protocol.js drivers/mixxx/`. The Mixxx
test fails if the two differ.

## What is in it

| Field | Value / meaning |
|---|---|
| `protocolVersion` | 1 |
| `jogTicksPerRev` | 1024 jog ticks per platter revolution |
| `relativeCenter` | 64: the value of a relative CC that means "no motion" |
| `defaultChannels` | `[0, 1]`, zero-based MIDI channels of Deck A and Deck B (channels 1 and 2) |
| `cc.<name>` | CC numbers: `jog`, `crossfaderMsb/Lsb`, `channelVolumeMsb/Lsb`, `pitchMsb/Lsb`, `knobMsb/Lsb`, `bpmMsb/Lsb`, `bpmNudge`, `transport` |
| `note.<name>` | note numbers: `cue`, `rev`, `touch`, `play`, `tap` |
| `clockPpqn`, `bpmMinX10`, `bpmMaxX10` | 24 clocks per quarter note; tempo limits as BPM x 10 (200 and 3000) |
| `cable.<name>` | USB-MIDI cable numbers: `deck` 0, `trsMidi` 1, `linkedDeck` 2 |
| `sysex.*` | manufacturer header, version, `deckAny`, reply/ACK/NAK bytes, `cmd.<name>` ids |
| `enums.<Name>.<value>` | `Deck`, `Mode`, `PlatterSource`, `FaderRole`, `FaderCurve`, `KnobRole`, `DacMode`, `LinkBridge`, `CalKind`, `TransportState`, `RebootMode` |

Config key ids, payload structs and the USB vendor/product id are **not** in the JS. They are in
`protocol/epdj3000.json` (and the generated C++ and Swift). A DJ-software mapping normally needs none of them.

## Rules every mapping must follow

These are the places the protocol is easy to get subtly wrong. Each links to the section that defines it.

1. **Channel is the deck** (PROTOCOL 2). `defaultChannels.indexOf(channel)`: 0 is Deck A, 1 is Deck B,
   -1 means "not one of ours". Channels here are zero-based; apps differ
   (djay's Advanced panel shows 1-16, VirtualDJ's `channel=""` takes 0-15, Mixxx hands scripts 0-15, rekordbox
   puts the channel in the low nibble of the hex status code). A unit whose `midiChannel` is not AUTO is off
   the defaults, and a mapping that hard-codes channels needs editing.
2. **Notes** (2.1). Down is Note On with velocity above 0. Up is Note Off with any velocity, or Note On with
   velocity 0. Receivers must accept both forms of release.
3. **Relative CCs** (2.1, 2.2): `jog` and `bpmNudge` are `64 + delta`. `delta = value - relativeCenter`,
   positive is clockwise for the jog. Value 64 is "no motion", and value 0 is never sent. A message
   carries up to +/-63 and the unit sends up to 500 per second, so a host control that is read once per
   audio buffer must be *added to*, not overwritten (this was a bug in the Mixxx pitch-bend path).
4. **14-bit CCs** (2.1, 2.4): the MSB CC is sent first and the LSB CC (MSB + 32) second; either half may be
   skipped when it did not change. Keep both halves, combine as `(msb << 7) | lsb` (0..16383), and
   apply nothing until both have been seen once. `bpm` carries BPM x 10.
5. **The slide pot is one of three CC pairs** (2.4), chosen by the unit's `faderRole`: `crossfader*`,
   `channelVolume*` or `pitch*`. Bind whichever the app can use; only one pair ever carries data.
6. **`bpm`, `bpmNudge`, `tap` and `transport` come out of the unit only in `SYNC_MASTER`** (2.6), plus `bpm`
   when `knobRole = BPM`. A DJ-software mapping can leave them unbound (VirtualDJ, djay and rekordbox do;
   Mixxx binds them).
7. **LED feedback** (2): Note On on `note.cue` or `note.play`, on the deck's own channel, velocity 127 for
   lit and 0 for off. Do not rely on a unit ignoring other channels' feedback unless the firmware says so:
   PROTOCOL.md does not state it (open item in `drivers/README.md`).
8. **A tap-to-play arrives as touch down, touch up and a `play` pulse** (2.3). The unit cannot know it is a
   tap when the touch begins, so a scratch-capable mapping briefly enters scratch mode on every tap.

## Worked example: a decoder

Most of the rules above in one short decoder. When this was written the block was extracted and run against
the byte-exact oracle `protocol/test-vectors.json` (relative and 14-bit vectors for every pair on both decks,
the USB-MIDI note and jog messages, and the edge cases: Note On velocity 0, a foreign channel). Nothing
re-runs it, so if you change it, re-run that check yourself.

```js
const E = require("./epdj3000-protocol.js");

const noteName = {};                       // 36 -> "cue"
for (const name of Object.keys(E.note)) noteName[E.note[name]] = name;
const pair = {};                           // CC number -> { name, half }
for (const name of ["crossfader", "channelVolume", "pitch", "knob", "bpm"]) {
  pair[E.cc[name + "Msb"]] = { name, half: "msb" };
  pair[E.cc[name + "Lsb"]] = { name, half: "lsb" };
}
const enumName = (table, value) => Object.keys(table).find((k) => table[k] === value);

// Returns a new decoder. Feed it raw channel messages (status, data1, data2); it returns an
// event object or null (not ours, nothing to report, or a 14-bit pair not yet complete).
function createDecoder() {
  const halves = {};                       // "deck:name" -> { msb, lsb }
  return function decode(status, d1, d2) {
    const kind = status & 0xF0;
    const deck = E.defaultChannels.indexOf(status & 0x0F);
    if (deck < 0) return null;

    if (kind === 0x80 || kind === 0x90) {  // Note Off / Note On
      const control = noteName[d1];
      return control ? { deck, control, down: kind === 0x90 && d2 > 0 } : null;
    }
    if (kind !== 0xB0) return null;        // Control Change

    if (d1 === E.cc.jog || d1 === E.cc.bpmNudge) {
      const delta = d2 - E.relativeCenter;
      if (delta === 0) return null;
      const control = d1 === E.cc.jog ? "jog" : "bpmNudge";
      return control === "jog"
        ? { deck, control, ticks: delta, revolutions: delta / E.jogTicksPerRev }
        : { deck, control, bpmX10: delta };  // one step is 0.1 BPM
    }
    if (d1 === E.cc.transport) {
      return { deck, control: "transport", state: enumName(E.enums.TransportState, d2) };
    }
    const p = pair[d1];
    if (!p) return null;
    const h = halves[deck + ":" + p.name] || (halves[deck + ":" + p.name] = {});
    h[p.half] = d2;
    if (h.msb === undefined || h.lsb === undefined) return null;
    return { deck, control: p.name, value: (h.msb << 7) | h.lsb };  // bpm: BPM x 10
  };
}

module.exports = { createDecoder };
```

## Adding a mapping for another app

See "Adding a mapping for another app" in [`drivers/README.md`](../README.md).
