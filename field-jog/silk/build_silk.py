#!/usr/bin/env python3
"""1:1 vinyl layouts for a Cricut Maker 4. Units are millimetres.

Black shapes are the cut. Red strokes are a guide: in Design Space set red
to Ignore, or use the *-cut.svg files, which have no guide.

Import check: the deck artboard is 88 x 240 mm. If Design Space shrinks it,
select the deck and type 88 mm by 240 mm.
"""

import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

ROOT = os.path.dirname(os.path.abspath(__file__))
SANS = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
BOLD = "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"

# Deck coordinates match field-jog.scad. y = 0 is the fader end.
W, H = 88.0, 240.0
PLATE = (7.0, 188.0, 74.0, 45.0)  # x, y, w, h
OLED_LOCAL = (31.0, 30.0, 30.5, 11.85)  # cx, cy, w, h on the plate
KNOB_LOCAL = (67.0, 30.0, 13.0)  # cx, cy, hole diameter


class Face:
    def __init__(self, path):
        self.font = TTFont(path)
        self.gs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.upem = self.font["head"].unitsPerEm
        os2 = self.font["OS/2"]
        self.cap = os2.sCapHeight or int(self.upem * 0.7)

    def measure(self, text, cap_mm, tracking):
        scale = cap_mm / self.cap
        extra = tracking * self.upem
        width = 0.0
        for i, ch in enumerate(text):
            g = self.gs[self.cmap[ord(ch)]]
            width += g.width * scale
            if i != len(text) - 1:
                width += extra * scale
        return width

    def path(self, text, x, baseline, cap_mm, tracking=0.06):
        scale = cap_mm / self.cap
        extra = tracking * self.upem * scale
        parts = []
        cursor = x
        for ch in text:
            name = self.cmap[ord(ch)]
            pen = SVGPathPen(self.gs)
            tpen = TransformPen(pen, (scale, 0, 0, -scale, cursor, baseline))
            self.gs[name].draw(tpen)
            d = pen.getCommands()
            if d:
                parts.append(d)
            cursor += self.gs[name].width * scale + extra
        return " ".join(parts), cursor - (extra if text else 0)


def svg_y(scad_y):
    return H - scad_y


def hole(cx, cy, d):
    r = d / 2
    return (
        f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}" '
        f'fill="none" stroke="#ff0000" stroke-width="0.2"/>'
    )


def rect(x, y, w, h):
    return (
        f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" '
        f'fill="none" stroke="#ff0000" stroke-width="0.2"/>'
    )


def cut_path(d):
    return f'<path d="{d}" fill="#000" fill-rule="nonzero"/>'


def header(w, h):
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.2f}mm" height="{h:.2f}mm" '
        f'viewBox="0 0 {w:.2f} {h:.2f}">\n'
    )


def write(name, body):
    path = os.path.join(ROOT, name)
    with open(path, "w") as f:
        f.write(body)
    print(name)


def label(face, text, x, baseline, cap, tracking=0.08, anchor="left"):
    width = face.measure(text, cap, tracking)
    if anchor == "middle":
        x -= width / 2
    elif anchor == "end":
        x -= width
    d, _ = face.path(text, x, baseline, cap, tracking)
    return cut_path(d), width


def deck_art(regular, bold, guides):
    parts = []
    # Button words sit just clear of the caps. PLAY and CUE are 16 mm,
    # IN and OUT are 10 mm with an LED at x = 28, REV is 10 mm on the right.
    words = [
        ("PLAY", 27.5, 36, "left"),
        ("CUE", 27.5, 60, "left"),
        ("IN", 33.0, 86, "left"),
        ("OUT", 33.0, 102, "left"),
        ("REV", 63.0, 48, "end"),
        ("PHONES", 44.0, 12, "middle"),
    ]
    for text, x, y, anchor in words:
        # Cap 3.2 mm. Baseline a hair below the button centre.
        p, _ = label(bold, text, x, svg_y(y) + 1.1, 3.2, 0.04, anchor)
        parts.append(p)

    # One centre mark, on the open side of the slot. The left side is the CUE label.
    parts.append(
        f'<rect x="48.6" y="{svg_y(58) - 0.35:.2f}" width="3.6" height="0.7" fill="#000"/>'
    )

    guide = ""
    if guides:
        px, py, pw, ph = PLATE
        ox, oy, ow, oh = OLED_LOCAL
        guide = "\n".join(
            [
                rect(0.4, 0.4, W - 0.8, H - 0.8),
                hole(44, svg_y(148), 76),
                hole(17, svg_y(36), 16),
                hole(17, svg_y(60), 16),
                hole(17, svg_y(86), 10),
                hole(17, svg_y(102), 10),
                hole(28, svg_y(86), 3.1),
                hole(28, svg_y(102), 3.1),
                hole(71, svg_y(48), 10),
                rect(42.3, svg_y(89), 3.4, 62),
                rect(px, svg_y(py + ph), pw, ph),
                rect(
                    px + ox - ow / 2,
                    svg_y(py + oy + oh / 2),
                    ow,
                    oh,
                ),
            ]
        )
    return "\n".join(parts), guide


def face_art(regular, bold, guides):
    # Local plate coords, jack end at the top of the file.
    def ly(local_y):
        return 45.0 - local_y

    parts = []
    # One row above the glass, toward the jacks. The glass top is local y = 35.9.
    ports = ["MIDI IN", "MIDI OUT", "USB", "LINE IN", "LINE OUT"]
    cap = 2.2
    gap = 1.6
    widths = [regular.measure(t, cap, 0.01) for t in ports]
    total = sum(widths) + gap * (len(ports) - 1)
    x = (74.0 - total) / 2
    baseline = ly(41.6)
    for text, width in zip(ports, widths):
        d, _ = regular.path(text, x, baseline, cap, 0.01)
        parts.append(cut_path(d))
        x += width + gap

    # Name on the platter side of the glass, clear of the two M2s at local y = 3.
    p, _ = label(bold, "FIELD JOG", 4.0, ly(8.2), 4.2, 0.08, "left")
    parts.append(p)
    p, _ = label(regular, "TEMPO", 66.0, ly(12.2), 2.2, 0.04, "middle")
    parts.append(p)

    guide = ""
    if guides:
        ox, oy, ow, oh = OLED_LOCAL
        kx, ky, kd = KNOB_LOCAL
        guide = "\n".join(
            [
                rect(0.3, 0.3, 73.4, 44.4),
                rect(ox - ow / 2, ly(oy + oh / 2), ow, oh),
                hole(kx, ly(ky), kd),
                hole(15, ly(3), 2.2),
                hole(41, ly(3), 2.2),
            ]
        )
    return "\n".join(parts), guide


def main():
    regular = Face(SANS)
    bold = Face(BOLD)

    cuts, guide = deck_art(regular, bold, True)
    write("deck-cut.svg", header(W, H) + cuts + "\n</svg>\n")
    write("deck-guide.svg", header(W, H) + guide + "\n" + cuts + "\n</svg>\n")

    cuts, guide = face_art(regular, bold, True)
    write("faceplate-cut.svg", header(74, 45) + cuts + "\n</svg>\n")
    write("faceplate-guide.svg", header(74, 45) + guide + "\n" + cuts + "\n</svg>\n")

    # One mat: deck, 12 mm gap, faceplate. A 10 mm bar to check the import.
    d_cuts, _ = deck_art(regular, bold, False)
    f_cuts, _ = face_art(regular, bold, False)
    mat_w, mat_h = 88 + 12 + 74, 240
    bar = '<rect x="0" y="0" width="10" height="1.2" fill="#000"/>'
    moved = f_cuts.replace("<path ", '<path transform="translate(100 0)" ')
    body = (
        header(mat_w, mat_h)
        + d_cuts
        + "\n"
        + moved
        + "\n"
        + f'<g transform="translate(4 250)">{bar}</g>\n'
        + "</svg>\n"
    )
    # The bar would fall outside the artboard. Keep it inside, under nothing:
    # the deck is 240 tall and the mat is 240, so put the bar in the gap.
    body = (
        header(mat_w, mat_h)
        + d_cuts
        + "\n"
        + moved
        + "\n"
        + '<rect x="89" y="114" width="10" height="1.2" fill="#000"/>\n'
        + "</svg>\n"
    )
    write("mat-cut.svg", body)


if __name__ == "__main__":
    main()
