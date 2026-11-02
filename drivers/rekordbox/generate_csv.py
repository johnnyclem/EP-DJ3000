#!/usr/bin/env python3
"""rekordbox MIDI-mapping CSVs for the EP-DJ3000, generated from protocol/epdj3000.json.

    python3 drivers/rekordbox/generate_csv.py           # write the two CSVs next to this file
    python3 drivers/rekordbox/generate_csv.py --check   # exit 1 if they are stale

Every MIDI number comes from the contract, so a changed CC cannot drift. The command names
(PlayPause, Cue, ...) and the column layout do NOT come from the contract; they are copied from
rows that other people published from AlphaTheta's bundled mapping files. AlphaTheta documents
the import/export buttons, not the CSV layout, so this format is community-documented only.
Nothing here has been loaded into rekordbox. Read README.md before importing.

Sources (read 2026-11-01):
  Official, UI only (types, IMPORT/EXPORT, "no support for third party products"):
    https://cdn.rekordbox.com/files/20241203210623/rekordbox7.0.5_midi_learn_operation_guide_EN.pdf
  Official, audio (Preferences > Audio > Mixer Mode External = decks 1-4 + sampler + preview):
    https://cdn.rekordbox.com/files/20260807093645/rekordbox7.2.18_manual_EN.pdf   (pp. 235-236)
  CSV layout, reverse-engineered from the CSVs that ship in the app (rekordbox 7.0.5):
    https://github.com/icherniukh/rb-midi-sniffer/blob/main/REKORDBOX-MIDI-CSV-SPEC.md
    https://github.com/guillain/DJ-MIDI-Studio/pull/354   (67 bundled CSVs; header row, types, options)
  CSV layout, 15 fields / 14 commas / '#' comments / hand-editing (rekordbox 5-6 era):
    https://djtechtools.com/2019/06/24/hacking-rekordbox-fx-and-adding-rmx-1000-control/
  Jog commands (JogTouch, JogScratch, JogPitchBend; RO rows) and why they are experimental:
    https://djtechtools.com/2017/05/08/hack-rekordbox-use-controllers-jogwheels/
    https://forum.djtechtools.com/t/numark-dj2go-2-mapping-for-rekordbox-mac/77902
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "protocol", "epdj3000.json")

HEADER = "#name,function,type,input,deck1,deck2,deck3,deck4,output,deck1,deck2,deck3,deck4,option,comment"
FIELDS = 15

# MIDI status high nibbles
NOTE_ON, CC = 0x9, 0xB

# live: command name seen in a published row of a bundled file; the row is active.
# try:  name only seen in a list, never in a row; row is written but disabled ('#').
# jog:  rekordbox treats jog rows as hardware-only (RO); reports say they do not work for
#       other controllers. Written but disabled; enable by deleting the leading '#'.
LIVE, TRY, JOG = "live", "try", "jog"

# (section title or None, [rows]); row = (contract id, command, csv type, status, scope, has LED output, option, comment)
# scope "deck": base code on channel 0 + the unit's channel in its rekordbox deck column.
# scope "global": channel baked into the code and no deck columns, like the bundled CrossFader row.
LAYOUT = [
    ("Deck", [
        ("play", "PlayPause", "Button", LIVE, "deck", True, "Fast;Priority=50", "Play/Pause - platter tap - LED follows deck"),
        ("cue", "Cue", "Button", LIVE, "deck", True, "Fast;Priority=50", "Cue - LED follows cue state"),
        ("rev", "Reverse", "Button", TRY, "deck", False, "Fast", "UNVERIFIED command name - confirm in the ADD menu first"),
        ("pitch", "TempoSlider", "KnobSliderHiRes", LIVE, "deck", False, "Fast", "Tempo fader - set faderRole to PITCH"),
    ]),
    ("Mixer", [
        ("crossfader", "CrossFader", "KnobSliderHiRes", LIVE, "global", False, "Fast", "Crossfader - faderRole CROSSFADER - no use with an external mixer"),
        ("channelVolume", "ChannelFader", "KnobSliderHiRes", TRY, "deck", False, "Fast", "UNVERIFIED command name - faderRole CHANNEL_VOLUME"),
        ("knob", "Gain", "KnobSliderHiRes", LIVE, "deck", False, "Fast", "Orange knob as trim - an arbitrary choice - re-learn to taste"),
    ]),
    ("Jog - EXPERIMENTAL - disabled - see README", [
        ("touch", "JogTouch", "JogTouch", JOG, "deck", False, "RO", "Platter touch"),
        ("jog", "JogScratch", "JogRotate", JOG, "deck", False, "RO", "Platter scratch - relative 64 + delta"),
    ]),
]


def die(msg):
    sys.exit(f"generate_csv.py: {msg}")


def load_contract():
    with open(SRC, encoding="utf-8") as f:
        return json.load(f)


def hexcode(hi, channel, data):
    if not (0 <= channel <= 15 and 0 <= data <= 127):
        die(f"MIDI value out of range: channel {channel}, data {data}")
    return f"{(hi << 4) | channel:02X}{data:02X}"


def line(fields):
    if len(fields) != FIELDS:
        die(f"row has {len(fields)} fields: {fields}")
    for x in fields:
        if any(c in x for c in ',"\r\n'):
            die(f"field would need CSV quoting, which rekordbox files never use: {x!r}")
    return ",".join(fields)


def blank_note(text):
    return line([text] + [""] * (FIELDS - 1))


def render(contract, unit):
    dev = contract["device"]
    midi = contract["midi"]
    controls = {c["id"]: c for c in midi["controls"]}
    channel = midi["defaultChannels"][unit]
    name = f'{dev["name"]} {dev["deckNames"][unit]}'

    def ctl(cid, kind):
        c = controls.get(cid)
        if c is None or c["type"] != kind:
            die(f"contract control {cid!r} is missing or not {kind!r}")
        return c

    out = [line(["@file", "1", name] + [""] * (FIELDS - 3)), HEADER,
           blank_note("# Generated by drivers/rekordbox/generate_csv.py from protocol/epdj3000.json - do not edit"),
           blank_note("# UNTESTED in rekordbox - read drivers/rekordbox/README.md before importing"),
           line([""] * FIELDS)]

    for title, rows in LAYOUT:
        out.append(blank_note(f"# {title}"))
        for cid, cmd, typ, status, scope, led, opt, comment in rows:
            if typ in ("Button", "JogTouch"):
                c = ctl(cid, "note")
                hi, data = NOTE_ON, c["number"]
            elif typ == "JogRotate":
                c = ctl(cid, "ccRel")
                hi, data = CC, c["number"]
            elif typ == "KnobSliderHiRes":
                c = ctl(cid, "cc14")
                hi, data = CC, c["number"]
                # The CSV carries only the MSB; the pairing MSB+32 is the MIDI convention, not documented
                # by AlphaTheta. Refuse to generate if the contract ever breaks it.
                if c["lsb"] != c["number"] + 32:
                    die(f"{cid}: LSB {c['lsb']} is not MSB {c['number']} + 32")
            else:
                die(f"unknown csv type {typ}")

            if scope == "deck":
                inp = hexcode(hi, 0, data)
                decks = [str(channel) if d == unit else "" for d in range(4)]
            else:
                inp = hexcode(hi, channel, data)
                decks = [""] * 4
            outp, odecks = (inp, decks) if led else ("", [""] * 4)
            prefix = "#" if status != LIVE else ""
            out.append(line([prefix + cmd, cmd, typ, inp, *decks, outp, *odecks, opt, comment]))
        out.append(line([""] * FIELDS))
    return "\n".join(out) + "\n"


def outputs(contract):
    dev = contract["device"]
    for unit, deck in enumerate(dev["deckNames"]):
        fname = f'{dev["name"]}-{deck.replace(" ", "-")}.midi.csv'
        yield os.path.join(HERE, fname), render(contract, unit)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if the committed CSVs differ from a fresh render")
    args = ap.parse_args()
    contract = load_contract()
    stale = False
    for path, text in outputs(contract):
        if args.check:
            have = open(path, encoding="utf-8", newline="").read() if os.path.exists(path) else None
            if have != text:
                print(f"STALE {os.path.relpath(path, ROOT)}")
                stale = True
        else:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(text)
            print(f"wrote {os.path.relpath(path, ROOT)}")
    if args.check:
        if stale:
            sys.exit(1)
        print("rekordbox CSVs are up to date")


if __name__ == "__main__":
    main()
