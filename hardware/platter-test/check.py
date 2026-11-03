#!/usr/bin/env python3
"""Sanity-check the exported touch-test STLs. No dependencies."""

import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
STL = HERE / "stl"

# Expected envelope. A few tenths of slop for tessellation.
EXPECT = {
    # The 12-o'clock notch is a local cut, so the Y span stays near the full 76 mm.
"carrier.stl": dict(dx=(75.8, 76.2), dy=(75.5, 76.1), dz=(2.95, 3.05), vmin=6000, vmax=9000),
    "wedge.stl": dict(dx=(20, 36), dy=(20, 36), dz=(5.6, 6.0), vmin=250, vmax=900),
    "wedges.stl": dict(dx=(118, 128), dy=(52, 58), dz=(5.6, 6.0), vmin=5550, vmax=5800),
    "stand.stl": dict(dx=(107, 109), dy=(107, 109), dz=(10.2, 10.7), vmin=20000, vmax=80000),
}


def load_stl(path):
    data = path.read_bytes()
    if not data:
        return []
    if data[:5].lower() == b"solid" and b"\x00" not in data[:200]:
        tris = []
        cur = []
        for line in data.decode("utf-8", "replace").splitlines():
            parts = line.split()
            if parts and parts[0] == "vertex" and len(parts) == 4:
                cur.append(tuple(float(x) for x in parts[1:]))
                if len(cur) == 3:
                    tris.append(tuple(cur))
                    cur = []
        return tris
    n = struct.unpack_from("<I", data, 80)[0]
    need = 84 + n * 50
    if len(data) < need:
        raise SystemExit(f"{path.name}: truncated ({len(data)} bytes, header says {n} tris)")
    tris = []
    off = 84
    for _ in range(n):
        vals = struct.unpack_from("<12fH", data, off)
        tris.append((vals[3:6], vals[6:9], vals[9:12]))
        off += 50
    return tris


def stats(tris):
    vol = 0.0
    lo = [1e9, 1e9, 1e9]
    hi = [-1e9, -1e9, -1e9]
    edges = {}
    for a, b, c in tris:
        vol += (
            a[0] * (b[1] * c[2] - b[2] * c[1])
            + a[1] * (b[2] * c[0] - b[0] * c[2])
            + a[2] * (b[0] * c[1] - b[1] * c[0])
        )
        for p in (a, b, c):
            for k in range(3):
                lo[k] = min(lo[k], p[k])
                hi[k] = max(hi[k], p[k])
        verts = (a, b, c)
        for i in range(3):
            p = verts[i]
            q = verts[(i + 1) % 3]
            # 0.1 µm. Coarser rounding welds the font's near-duplicate
            # vertices and reports a seam that is not actually open.
            key = tuple(sorted((
                tuple(round(v, 4) for v in p),
                tuple(round(v, 4) for v in q),
            )))
            edges[key] = edges.get(key, 0) + 1
    vol /= 6.0
    bad = sum(1 for k in edges.values() if k != 2)
    size = [hi[k] - lo[k] for k in range(3)]
    return vol, size, len(tris), bad, lo, hi


def main():
    failed = False
    for name, exp in EXPECT.items():
        path = STL / name
        if not path.exists():
            print(f"FAIL {name}: missing")
            failed = True
            continue
        vol, size, n, bad, lo, hi = stats(load_stl(path))
        problems = []
        for axis, key in enumerate("xyz"):
            lo_e, hi_e = exp[f"d{key}"]
            if not (lo_e <= size[axis] <= hi_e):
                problems.append(f"{key}={size[axis]:.2f} not in {lo_e}..{hi_e}")
        if not (exp["vmin"] <= vol <= exp["vmax"]):
            problems.append(f"volume={vol:.1f} not in {exp['vmin']}..{exp['vmax']}")
        if vol <= 0:
            problems.append("volume is not positive")
        if bad:
            problems.append(f"{bad} non-manifold edges")
        status = "FAIL" if problems else "ok"
        if problems:
            failed = True
        dims = " x ".join(f"{v:.2f}" for v in size)
        print(f"{status:4} {name:14} {dims} mm   vol {vol:.0f} mm³   {n} tris"
              + ("" if not problems else "   " + "; ".join(problems)))
    # Shared faces can leave a zero-volume shell. Real interference has volume.
    for name, limit in (("overlap.stl", 2.0), ("seated.stl", 5.0)):
        path = STL / name
        if not path.exists() or path.stat().st_size < 100:
            print(f"ok   {name:14} empty (no interference)")
            continue
        tris = load_stl(path)
        if not tris:
            print(f"ok   {name:14} empty (no interference)")
            continue
        vol, size, n, bad, lo, hi = stats(tris)
        dims = " x ".join(f"{v:.2f}" for v in size)
        if abs(vol) > limit:
            print(f"FAIL {name:14} {dims} mm   vol {vol:.2f} mm³   {n} tris   interference")
            failed = True
        else:
            print(f"ok   {name:14} {dims} mm   vol {vol:.2f} mm³   {n} tris   (contact only)")
    if failed:
        sys.exit(1)
    print("stl checks passed")


if __name__ == "__main__":
    main()
