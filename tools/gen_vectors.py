#!/usr/bin/env python3
"""Independent reference implementation + shared test vectors.

Emits protocol/test-vectors.json and firmware/core/test/vectors.generated.h.
Every implementation (C++ firmware core, Swift protocol library, JS mappings)
must reproduce these byte-for-byte. This file is the *oracle*; it deliberately
does not import anything from the generated protocol code.

    python3 tools/gen_vectors.py           # write
    python3 tools/gen_vectors.py --check   # fail if stale
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "protocol", "epdj3000.json")
OUT_JSON = os.path.join(ROOT, "protocol", "test-vectors.json")
OUT_H = os.path.join(ROOT, "firmware", "core", "test", "vectors.generated.h")

SIZES = {"u8": (1, False), "u16": (2, False), "u32": (4, False), "i8": (1, True), "i16": (2, True), "i32": (4, True)}


# ------------------------------------------------------------ reference codec
def pack8to7(raw):
    out = []
    for i in range(0, len(raw), 7):
        grp = raw[i:i + 7]
        hdr = 0
        for j, b in enumerate(grp):
            hdr |= ((b >> 7) & 1) << j
        out.append(hdr)
        out.extend(b & 0x7F for b in grp)
    return out


def unpack7to8(sept):
    out = []
    i = 0
    while i < len(sept):
        hdr = sept[i]
        grp = sept[i + 1:i + 8]
        for j, b in enumerate(grp):
            out.append(b | (((hdr >> j) & 1) << 7))
        i += 8
    return out


def checksum(body):
    return (-sum(body)) & 0x7F


def sysex_frame(deck, cmd, payload, header, version):
    body = [version, deck, cmd] + pack8to7(payload)
    return [0xF0] + header + body + [checksum(body), 0xF7]


def le(value, n, signed):
    if signed:
        value &= (1 << (8 * n)) - 1
    return [(value >> (8 * i)) & 0xFF for i in range(n)]


def serialize_struct(fields, values):
    out = []
    for f in fields:
        n, signed = SIZES[f[1]]
        cnt = f[2] if len(f) > 2 else 1
        v = values.get(f[0], [0] * cnt if cnt > 1 else 0)
        if cnt > 1:
            for x in v:
                out += le(x, n, signed)
        else:
            out += le(v, n, signed)
    return out


def usb_midi_packets(cable, msg):
    """Wrap one complete MIDI message (list of bytes) into USB-MIDI 1.0 event packets."""
    status = msg[0]
    if status == 0xF0:  # SysEx: split in 3s
        pkts = []
        for i in range(0, len(msg), 3):
            chunk = msg[i:i + 3]
            last = i + 3 >= len(msg)
            cin = {1: 0x5, 2: 0x6, 3: 0x7}[len(chunk)] if last else 0x4
            pkts.append([(cable << 4) | cin] + chunk + [0] * (3 - len(chunk)))
        return pkts
    if status >= 0xF8:
        cin, n = 0xF, 1
    elif status == 0xF1 or status == 0xF3:
        cin, n = 0x2, 2
    elif status == 0xF2:
        cin, n = 0x3, 3
    elif status >= 0xF0:
        cin, n = 0x5, 1
    else:
        cin = status >> 4
        n = 2 if cin in (0xC, 0xD) else 3
    return [[(cable << 4) | cin] + msg[:n] + [0] * (3 - n)]


def mtc_quarter_frames(h, m, s, f, fps_type):
    nib = [f & 0xF, f >> 4, s & 0xF, s >> 4, m & 0xF, m >> 4, h & 0xF, ((h >> 4) & 1) | (fps_type << 1)]
    return [[0xF1, (i << 4) | n] for i, n in enumerate(nib)]


def mtc_full_frame(h, m, s, f, fps_type):
    return [0xF0, 0x7F, 0x7F, 0x01, 0x01, (fps_type << 5) | h, m, s, f, 0xF7]


# ------------------------------------------------------------ vectors
def build():
    d = json.load(open(SRC))
    header = [int(x, 16) for x in d["sysex"]["header"]]
    ver = d["sysex"]["version"]
    structs = d["structs"]

    pack = []
    for raw in ([], [0], [0x7F], [0x80], [0xFF], list(range(7)), [0xFF] * 7, [0x80 + i for i in range(8)],
                list(range(0, 256, 7))[:36]):
        p = pack8to7(raw)
        assert unpack7to8(p) == raw
        pack.append({"raw": raw, "packed": p})

    cfg = {k["name"]: k["id"] for k in d["config"]["keys"]}
    frames = []

    def frame(name, deck, cmd, payload):
        frames.append({"name": name, "deck": deck, "cmd": cmd, "payload": payload,
                       "frame": sysex_frame(deck, cmd, payload, header, ver)})

    frame("getInfo deck A", 0, 1, [])
    frame("getConfig all", 0, 2, [0xFF])
    frame("setConfig platterSource=1 deck B", 1, 3, [cfg["platterSource"]] + le(1, 4, True))
    frame("setConfig faderRawMax=4055 any deck", 127, 3, [cfg["faderRawMax"]] + le(4055, 4, True))
    frame("setConfig negative value", 0, 3, [cfg["bpmX10"]] + le(-5, 4, True))
    frame("ack for setConfig", 0, 0x7E, [3])
    frame("nak(setConfig, OUT_OF_RANGE)", 0, 0x7F, [3, 5])
    info_vals = {"protocolVersion": 1, "fwMajor": 0, "fwMinor": 1, "fwPatch": 2, "hwRev": 1, "deck": 1, "mode": 0,
                 "platterSource": 1, "caps": 0x003E, "serial": 0xDEADBEEF, "uptimeS": 3600}
    frame("info reply", 1, 0x41, serialize_struct(structs["Info"], info_vals))
    tele_vals = {"seq": 200, "buttons": 3, "flags": 0x0B, "transport": 1, "faderRaw": 2048, "knobRaw": 4095,
                 "faderOut": 8191, "knobOut": 16383, "angle": 1234, "bpmX10": 1285, "jogTotal": -70000,
                 "touchRaw": [100, 200, 300, 400, 500, 600, 700, 65535]}
    frame("telemetry", 0, 0x47, serialize_struct(structs["Telemetry"], tele_vals))

    struct_vecs = []
    for name, vals in (("Info", info_vals), ("Telemetry", tele_vals),
                       ("CalResult", {"kind": 1, "applied": 1, "rawMin": 12, "rawMax": 4090}),
                       ("Peer", {"deck": 1, "flags": 5, "rssi": -60, "status": 0, "rttUs": 1500, "offsetUs": -250})):
        struct_vecs.append({"struct": name, "values": vals, "bytes": serialize_struct(structs[name], vals)})

    cc14 = []
    for bpm_x10 in (200, 1200, 1285, 3000, 16383):
        cc14.append({"value14": bpm_x10, "msb": bpm_x10 >> 7, "lsb": bpm_x10 & 0x7F})

    rel = []
    for delta in (0, 1, -1, 5, -5, 63, -63, 64, -64, 200, -200):
        rel.append({"delta": delta, "value": 64 + max(-63, min(63, delta))})

    usb = []
    cases = [
        ("noteOn cue ch1 cable0", 0, [0x90, 36, 127]),
        ("noteOff cue ch1 cable0", 0, [0x80, 36, 0]),
        ("cc jog ch2 cable0", 0, [0xB1, 22, 67]),
        ("clock cable0", 0, [0xF8]),
        ("start cable0", 0, [0xFA]),
        ("note on cable2 deck B", 2, [0x91, 39, 127]),
        ("mtc quarter frame", 0, [0xF1, 0x76]),
        ("song position", 0, [0xF2, 0x06, 0x00]),
        ("program change", 0, [0xC0, 5]),
        ("sysex getInfo", 0, frames[0]["frame"]),
        ("sysex 10 bytes mtc full frame", 1, mtc_full_frame(1, 2, 3, 4, 3)),
    ]
    for name, cable, msg in cases:
        usb.append({"name": name, "cable": cable, "message": msg, "packets": usb_midi_packets(cable, msg)})

    mtc = [{"h": 1, "m": 2, "s": 3, "f": 4, "fpsType": 3,
            "quarterFrames": mtc_quarter_frames(1, 2, 3, 4, 3), "fullFrame": mtc_full_frame(1, 2, 3, 4, 3)},
           {"h": 23, "m": 59, "s": 58, "f": 24, "fpsType": 1,
            "quarterFrames": mtc_quarter_frames(23, 59, 58, 24, 1), "fullFrame": mtc_full_frame(23, 59, 58, 24, 1)}]

    clock = [{"bpmX10": b, "periodUs": 60e6 / (b / 10.0 * 24)} for b in (600, 1200, 1285, 3000)]

    return {"_comment": "GENERATED by tools/gen_vectors.py - DO NOT EDIT.", "pack8to7": pack, "sysexFrames": frames,
            "structs": struct_vecs, "cc14": cc14, "relative": rel, "usbMidi": usb, "mtc": mtc,
            "clockPeriod": clock}


def cpp_vec(v):
    return "{" + ", ".join(str(x) for x in v) + "}"


def gen_header(v):
    o = ["// GENERATED by tools/gen_vectors.py - DO NOT EDIT. Host tests only.", "#pragma once",
         "#include <cstdint>", "#include <string>", "#include <vector>", "namespace epdj_vec {",
         "using Bytes = std::vector<uint8_t>;",
         "struct Pack { Bytes raw, packed; };",
         "struct Frame { const char* name; uint8_t deck; uint8_t cmd; Bytes payload; Bytes frame; };",
         "struct Cc14 { uint16_t value14; uint8_t msb, lsb; };",
         "struct Rel { int delta; uint8_t value; };",
         "struct Usb { const char* name; uint8_t cable; Bytes message; std::vector<Bytes> packets; };",
         "struct Mtc { int h, m, s, f, fpsType; std::vector<Bytes> quarterFrames; Bytes fullFrame; };",
         "struct Clock { uint16_t bpmX10; double periodUs; };"]
    o.append("inline const std::vector<Pack>& packVectors() { static const std::vector<Pack> v = {")
    for p in v["pack8to7"]:
        o.append("  {%s, %s}," % (cpp_vec(p["raw"]), cpp_vec(p["packed"])))
    o.append("}; return v; }")
    o.append("inline const std::vector<Frame>& frameVectors() { static const std::vector<Frame> v = {")
    for f in v["sysexFrames"]:
        o.append('  {"%s", %d, %d, %s, %s},' % (f["name"], f["deck"], f["cmd"], cpp_vec(f["payload"]), cpp_vec(f["frame"])))
    o.append("}; return v; }")
    o.append("// Serialized struct bytes keyed by struct name (values are in test-vectors.json).")
    for s in v["structs"]:
        o.append("inline const Bytes& bytes%s() { static const Bytes b = %s; return b; }" % (s["struct"], cpp_vec(s["bytes"])))
    o.append("inline const std::vector<Cc14>& cc14Vectors() { static const std::vector<Cc14> v = {")
    for c in v["cc14"]:
        o.append("  {%d, %d, %d}," % (c["value14"], c["msb"], c["lsb"]))
    o.append("}; return v; }")
    o.append("inline const std::vector<Rel>& relVectors() { static const std::vector<Rel> v = {")
    for r in v["relative"]:
        o.append("  {%d, %d}," % (r["delta"], r["value"]))
    o.append("}; return v; }")
    o.append("inline const std::vector<Usb>& usbVectors() { static const std::vector<Usb> v = {")
    for u in v["usbMidi"]:
        pk = "{" + ", ".join(cpp_vec(p) for p in u["packets"]) + "}"
        o.append('  {"%s", %d, %s, %s},' % (u["name"], u["cable"], cpp_vec(u["message"]), pk))
    o.append("}; return v; }")
    o.append("inline const std::vector<Mtc>& mtcVectors() { static const std::vector<Mtc> v = {")
    for m in v["mtc"]:
        qf = "{" + ", ".join(cpp_vec(q) for q in m["quarterFrames"]) + "}"
        o.append("  {%d, %d, %d, %d, %d, %s, %s}," % (m["h"], m["m"], m["s"], m["f"], m["fpsType"], qf, cpp_vec(m["fullFrame"])))
    o.append("}; return v; }")
    o.append("inline const std::vector<Clock>& clockVectors() { static const std::vector<Clock> v = {")
    for c in v["clockPeriod"]:
        o.append("  {%d, %.6f}," % (c["bpmX10"], c["periodUs"]))
    o.append("}; return v; }")
    o.append("}  // namespace epdj_vec")
    return "\n".join(o) + "\n"


def main():
    v = build()
    outs = {OUT_JSON: json.dumps(v, indent=1) + "\n", OUT_H: gen_header(v)}
    check = "--check" in sys.argv[1:]
    stale = []
    for path, content in outs.items():
        cur = open(path).read() if os.path.exists(path) else None
        if cur != content:
            stale.append(path)
            if not check:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                open(path, "w").write(content)
                print("wrote", os.path.relpath(path, ROOT))
    if check:
        if stale:
            print("STALE:", *[os.path.relpath(s, ROOT) for s in stale])
            sys.exit(1)
        print("test vectors up to date")


if __name__ == "__main__":
    main()
