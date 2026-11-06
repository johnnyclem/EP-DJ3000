"""Split Four into an FDM tray and resin tiles.

Plate: 153.36 x 77.76 x 165 mm. Every resin part fits in 150.9 x 75.3 mm,
which leaves 1.2 mm of clearance on each side. Height is not the limit.
Re-run from /workspace: python3 scripts/four_split.py
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import trimesh

OUT = Path("/workspace/public/field-jog/four")
DATA = Path("/workspace/src/data/four-kit.ts")

W, H = 176.0, 240.5
TRAY_H = 22.0
WALL = 2.4
LEDGE = 4.0
FLOOR = 2.0
TILE_T = 2.2
SEAT = TRAY_H - TILE_T  # 19.8
GAP = 0.25  # each side of an internal seam
EDGE = 0.2  # drop-in clearance at the rim

# Seams, in the same coordinates as the tray.
# Two fader tiles, not five: 153.36 x 77.76 can hold a 70 mm strip.
X_FADER = [73.0, 137.0]
X_TRANS = 83.0
X_SCREEN = [134.0]
Y_TRANS = 146.0
Y_SCREEN = 165.5

OX0, OX1 = WALL, W - WALL
OY0, OY1 = WALL, H - WALL

# 1.2 mm clearance on every side of the real plate.
PLATE = (153.36, 77.76, 165.0)
FIT_LONG, FIT_SHORT = PLATE[0] - 2.4, PLATE[1] - 2.4


def box(x0, y0, z0, x1, y1, z1):
    mesh = trimesh.creation.box(extents=[x1 - x0, y1 - y0, z1 - z0])
    mesh.apply_translation([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
    return mesh


def cyl(x, y, z, r, h, axis="z", sections=28):
    mesh = trimesh.creation.cylinder(radius=r, height=h, sections=sections)
    if axis == "x":
        mesh.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [0, 1, 0]))
    elif axis == "y":
        mesh.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))
    mesh.apply_translation([x, y, z])
    return mesh


def fuse(meshes):
    meshes = [m for m in meshes if m is not None]
    if len(meshes) == 1:
        return meshes[0]
    return trimesh.boolean.union(meshes, engine="manifold")


def cut(mesh, holes):
    holes = [h for h in holes if h is not None]
    if not holes:
        return mesh
    return trimesh.boolean.difference([mesh, *holes], engine="manifold")


def spans(edges, a, b):
    xs = [a, *[x for x in edges if a < x < b], b]
    return list(zip(xs[:-1], xs[1:]))


def tile_bounds(x0, x1, y0, y1):
    # Pull internal edges back so neighboring tiles don't touch.
    def lo(v, edge):
        return v + EDGE if abs(v - edge) < 1e-6 else v + GAP

    def hi(v, edge):
        return v - EDGE if abs(v - edge) < 1e-6 else v - GAP

    return (lo(x0, OX0), hi(x1, OX1), lo(y0, OY0), hi(y1, OY1))


def fits(name, x0, x1, y0, y1):
    a, b = x1 - x0, y1 - y0
    long, short = max(a, b), min(a, b)
    if long > FIT_LONG + 1e-6 or short > FIT_SHORT + 1e-6:
        raise SystemExit(f"{name} is {a:.1f} x {b:.1f}, over the {FIT_LONG} x {FIT_SHORT} plate")
    return a, b


def slot(x, y0, y1, w=3.6):
    return ("slot", x - w / 2, y0, x + w / 2, y1)


def rect(x, y, w, h):
    return ("rect", x - w / 2, y - h / 2, x + w / 2, y + h / 2)


def hole_mesh(kind, a, b, c, d, z0, z1):
    if kind == "round":
        return cyl((a + c) / 2, (b + d) / 2, (z0 + z1) / 2, (c - a) / 2, z1 - z0)
    return box(a, b, z0, c, d, z1)


def build_tiles():
    tiles = []
    groups = [
        ("faders-12", [28.0, 58.0], False),
        ("faders-34", [88.0, 118.0], False),
        ("master", [156.0], True),
    ]
    cols = spans(X_FADER, OX0, OX1)
    for (name, fxs, is_master), (x0, x1) in zip(groups, cols):
        holes = []
        for fx in fxs:
            holes.append(slot(fx, 41.0, 115.0))
            if is_master:
                holes.append(("round", 156 - 4, 128 - 4, 156 + 4, 128 + 4))
            else:
                holes.append(("round", fx - 4.2, 128 - 4.2, fx + 4.2, 128 + 4.2))
        tiles.append((name, tile_bounds(x0, x1, OY0, Y_TRANS), holes))

    keys = [28, 50, 72, 94, 116, 138]
    for name, (x0, x1) in zip(["transport-l", "transport-r"], spans([X_TRANS], OX0, OX1)):
        holes = [rect(k, 156.0, 13.0, 8.0) for k in keys if x0 + 1 < k < x1 - 1]
        tiles.append((name, tile_bounds(x0, x1, Y_TRANS, Y_SCREEN), holes))

    screen_holes = [rect(88.0, 202.0, 64.0, 47.0)]
    for name, (x0, x1), holes in zip(
        ["screen", "ear-r"],
        spans(X_SCREEN, OX0, OX1),
        [screen_holes, []],
    ):
        tiles.append((name, tile_bounds(x0, x1, Y_SCREEN, OY1), holes))
    return tiles


def tile_mesh(bounds, holes):
    x0, x1, y0, y1 = bounds
    body = box(x0, y0, SEAT, x1, y1, SEAT + TILE_T)
    cutters = []
    for kind, a, b, c, d in holes:
        # Keep the hole inside the tile.
        if c < x0 or a > x1 or d < y0 or b > y1:
            continue
        cutters.append(hole_mesh(kind, a, b, c, d, SEAT - 0.4, SEAT + TILE_T + 0.4))
    return cut(body, cutters)


def tray_mesh():
    outer = box(0, 0, 0, W, H, TRAY_H)
    cavity = box(WALL + LEDGE, WALL + LEDGE, FLOOR, W - WALL - LEDGE, H - WALL - LEDGE, SEAT + 0.2)
    opening = box(OX0, OY0, SEAT - 0.05, OX1, OY1, TRAY_H + 1)
    mesh = cut(outer, [cavity, opening])

    ribs = []
    z0, z1 = 12.0, SEAT + 0.02
    for x in X_FADER:
        ribs.append(box(x - 1.6, OY0, z0, x + 1.6, Y_TRANS, z1))
    ribs.append(box(OX0, Y_TRANS - 1.6, z0, OX1, Y_TRANS + 1.6, z1))
    ribs.append(box(X_TRANS - 1.6, Y_TRANS, z0, X_TRANS + 1.6, Y_SCREEN, z1))
    ribs.append(box(OX0, Y_SCREEN - 1.6, z0, OX1, Y_SCREEN + 1.6, z1))
    for x in X_SCREEN:
        ribs.append(box(x - 1.6, Y_SCREEN, z0, x + 1.6, OY1, z1))
    mesh = fuse([mesh, *ribs])

    holes = []
    for x in (36, 88, 140):
        holes.append(cyl(x, 0, 11, 3.2, 20, axis="y"))
    for x in (18, 38, 58, 78, 98, 118, 138, 158):
        holes.append(cyl(x, H, 11, 3.2, 20, axis="y"))
    for y in (16, 25, 48, 216, 224):
        holes.append(cyl(0, y, 8, 2.4, 16, axis="x"))
        holes.append(cyl(W, y, 8, 2.4, 16, axis="x"))
    return cut(mesh, holes)


def small_parts():
    parts = []

    def cap(master=False):
        body = box(-7, -5.5, 0, 7, 5.5, 6.5)
        fin = box(-1.1, -6, -6, 1.1, 6, 0.4)
        return fuse([body, fin]), "master cap" if master else "fader cap"

    c, _ = cap()
    parts.append(("cap", c))
    m, _ = cap(True)
    parts.append(("cap-master", m))

    knob = cyl(0, 0, 4, 6.2, 8)
    knob = cut(knob, [cyl(0, 0, 4, 3.15, 9)])
    parts.append(("knob", knob))

    key = fuse([box(-7.5, -5.5, 0, 7.5, 5.5, 3.2), cyl(0, 0, -2.2, 1.5, 4.6)])
    parts.append(("key", key))
    rec = fuse([cyl(0, 0, 1.6, 5.0, 3.2), cyl(0, 0, -2.0, 1.4, 4.2)])
    parts.append(("rec", rec))
    return parts


def layout_plate(parts):
    placed = []
    cursor = 4.0
    y = 8.0
    for name in ("cap", "cap", "cap", "cap", "cap-master"):
        mesh = parts[name].copy()
        mesh.apply_translation([cursor + 8, y + 8, 6])
        placed.append(mesh)
        cursor += 20
    cursor = 6.0
    y = 30.0
    mesh = parts["knob"].copy()
    mesh.apply_translation([cursor + 8, y + 8, 0])
    placed.append(mesh)
    cursor = 4.0
    y = 50.0
    for _ in range(6):
        mesh = parts["key"].copy()
        mesh.apply_translation([cursor + 9, y, 2.2])
        placed.append(mesh)
        cursor += 20
    cursor = 8.0
    y = 66.0
    for _ in range(4):
        mesh = parts["rec"].copy()
        mesh.apply_translation([cursor, y, 2.0])
        placed.append(mesh)
        cursor += 16
    plate = trimesh.util.concatenate(placed)
    a, b = plate.bounds[1][0] - plate.bounds[0][0], plate.bounds[1][1] - plate.bounds[0][1]
    if max(a, b) > FIT_LONG or min(a, b) > FIT_SHORT:
        raise SystemExit(f"caps plate is {a:.1f} x {b:.1f}")
    return plate


def write_scad(tiles):
    lines = [
        "// Four, split for a 153.36 x 77.76 x 165 mm resin plate.",
        "// The tray is FDM. Every other part fits in 150.9 x 75.3 mm.",
        "// Tiles drop into a 2.2 mm rabbet. Glue them. Do not screw them.",
        "//",
        "//   openscad -D 'part=\"tray\"' -o tray.stl four.scad",
        "//",
        f"W = {W};",
        f"H = {H};",
        f"tray_h = {TRAY_H};",
        f"wall = {WALL};",
        f"tile_t = {TILE_T};",
        'part = "tray"; // [tray, fader-1, fader-2, fader-3, fader-4, master, transport-l, transport-r, ear-l, screen, ear-r]',
        "",
        "module tile(x0, y0, x1, y1) {",
        "  translate([x0, y0, tray_h - tile_t]) cube([x1 - x0, y1 - y0, tile_t]);",
        "}",
        "",
    ]
    for name, (x0, x1, y0, y1), _holes in tiles:
        lines.append(f'module {name.replace("-", "_")}() {{ tile({x0:.2f}, {y0:.2f}, {x1:.2f}, {y1:.2f}); }}')
    lines += [
        "",
        "if (part == \"tray\") {",
        "  difference() {",
        "    cube([W, H, tray_h]);",
        "    translate([wall + 4, wall + 4, 2]) cube([W - 2 * wall - 8, H - 2 * wall - 8, tray_h]);",
        "    translate([wall, wall, tray_h - tile_t]) cube([W - 2 * wall, H - 2 * wall, tile_t + 1]);",
        "  }",
        "}",
    ]
    for name, *_ in tiles:
        ident = name.replace("-", "_")
        lines.append(f'if (part == "{name}") {ident}();')
    (OUT / "four.scad").write_text("\n".join(lines) + "\n")


def write_ts(tiles):
    rows = []
    for name, (x0, x1, y0, y1), holes in tiles:
        hole_bits = []
        for kind, a, b, c, d in holes:
            hole_bits.append(
                "{ "
                + f'kind: "{kind}", x0: {a:.2f}, y0: {b:.2f}, x1: {c:.2f}, y1: {d:.2f}'
                + " }"
            )
        label = name.replace("-", " ")
        rows.append(
            "  {\n"
            f'    id: "{name}",\n'
            f'    label: "{label}",\n'
            f"    x0: {x0:.2f}, y0: {y0:.2f}, x1: {x1:.2f}, y1: {y1:.2f},\n"
            f"    holes: [{', '.join(hole_bits)}],\n"
            "  }"
        )
    text = (
        "// Generated by scripts/four_split.py. Do not edit.\n"
        "export type KitHole = { kind: string; x0: number; y0: number; x1: number; y1: number };\n"
        "export type KitTile = {\n"
        "  id: string;\n"
        "  label: string;\n"
        "  x0: number; y0: number; x1: number; y1: number;\n"
        "  holes: KitHole[];\n"
        "};\n"
        f"export const TRAY = {{ w: {W}, h: {H}, t: {TRAY_H}, seat: {SEAT} }};\n"
        "export const TILES: KitTile[] = [\n" + ",\n".join(rows) + ",\n];\n"
    )
    DATA.write_text(text)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    DATA.parent.mkdir(parents=True, exist_ok=True)
    tiles = build_tiles()
    report = []
    for name, bounds, holes in tiles:
        size = fits(name, *bounds)
        mesh = tile_mesh(bounds, holes)
        if mesh.volume < 100:
            raise SystemExit(f"{name} volume looks wrong: {mesh.volume}")
        mesh.export(OUT / f"{name}.stl")
        report.append((name, size[0], size[1], "resin"))
        print(f"{name:14} {size[0]:6.1f} x {size[1]:5.1f} mm")

    print("tray...")
    tray = tray_mesh()
    tray.export(OUT / "tray.stl")
    print(f"{'tray':14} {W:6.1f} x {H:5.1f} mm  FDM")

    print("small parts...")
    parts = {name: mesh for name, mesh in small_parts()}
    for name, mesh in parts.items():
        mesh.export(OUT / f"{name}.stl")
    plate = layout_plate(parts)
    plate.export(OUT / "caps-plate.stl")
    print(f"{'caps-plate':14} one resin plate, 4 caps + master + knob + 6 keys + 4 rec")

    write_scad(tiles)
    write_ts(tiles)
    (OUT / "manifest.json").write_text(json.dumps([{"name": n, "w": w, "h": h, "print": p} for n, w, h, p in report], indent=2))
    print("done", OUT)


if __name__ == "__main__":
    main()
