// Four, split for a 153.36 x 77.76 x 165 mm resin plate.
// The tray is FDM. Every other part fits in 150.9 x 75.3 mm.
// Tiles drop into a 2.2 mm rabbet. Glue them. Do not screw them.
//
//   openscad -D 'part="tray"' -o tray.stl four.scad
//
W = 176.0;
H = 240.5;
tray_h = 22.0;
wall = 2.4;
tile_t = 2.2;
part = "tray"; // [tray, fader-1, fader-2, fader-3, fader-4, master, transport-l, transport-r, ear-l, screen, ear-r]

module tile(x0, y0, x1, y1) {
  translate([x0, y0, tray_h - tile_t]) cube([x1 - x0, y1 - y0, tile_t]);
}

module faders_12() { tile(2.60, 2.60, 72.75, 145.75); }
module faders_34() { tile(73.25, 2.60, 136.75, 145.75); }
module master() { tile(137.25, 2.60, 173.40, 145.75); }
module transport_l() { tile(2.60, 146.25, 82.75, 165.25); }
module transport_r() { tile(83.25, 146.25, 173.40, 165.25); }
module screen() { tile(2.60, 165.75, 133.75, 237.90); }
module ear_r() { tile(134.25, 165.75, 173.40, 237.90); }

if (part == "tray") {
  difference() {
    cube([W, H, tray_h]);
    translate([wall + 4, wall + 4, 2]) cube([W - 2 * wall - 8, H - 2 * wall - 8, tray_h]);
    translate([wall, wall, tray_h - tile_t]) cube([W - 2 * wall, H - 2 * wall, tile_t + 1]);
  }
}
if (part == "faders-12") faders_12();
if (part == "faders-34") faders_34();
if (part == "master") master();
if (part == "transport-l") transport_l();
if (part == "transport-r") transport_r();
if (part == "screen") screen();
if (part == "ear-r") ear_r();
