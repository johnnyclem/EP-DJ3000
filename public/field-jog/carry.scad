// FIELD JOG, CARRY
// The original face, grown so a 6 inch ring still has a field of grey.
// One 152.4 mm ring. The GC9A01 sits in the hole and does not spin.
// Letters are pockets for a second color. No vinyl, no engraving.
//
// 214 x 300 x 22 mm. Fader is 125 mm, in the right lane. Knob is upper right.
// Six 12 mm latching switches, flush, LED in the cap. 5000 mAh pouch under
// the button row. ESP32-S3. A P4 is not justified for a SPI round panel.
//
//   openscad -D 'part="top"' -o carry-top.stl carry.scad
//   openscad -D 'part="ring"' -o carry-ring.stl carry.scad

/* [Export] */
part = "all"; // [all, top, bottom, ring, pinion, letters, knob]

/* [Body] */
W = 214;
H = 300;
body_h = 22;
skin = 3.6;
wall = 2.4;
floor_t = 1.6;
R = 5;

/* [Platter] */
platter_d = 152.4;
platter_t = 3.6;
glass_d = 32.4;
hole_d = 35;
pcb_d = 38;
pcb_len = 45.5;
cx = 96;
cy = 143;
gear_d = 62;          // tooth ring on the underside of the platter
pinion_d = 14;        // driven gear
magnet_d = 6;         // small diametric magnet on the pinion
gear_xy = [cx + 40, cy];  // pinion sits clear of the LCD board

/* [Controls] */
knob_xy = [174, 252];
fader_x = 196;
fader_y0 = 80.5;
fader_len = 125;
key_xs = [36, 66, 96, 126, 156, 186];
key_y = 26;

/* [Hidden] */
eps = 0.05;
tray_h = body_h - skin;

module rounded_rect(w, h, r) {
    rr = min(r, min(w, h) / 2 - 0.01);
    hull() {
        translate([rr, rr]) circle(r = rr);
        translate([w - rr, rr]) circle(r = rr);
        translate([rr, h - rr]) circle(r = rr);
        translate([w - rr, h - rr]) circle(r = rr);
    }
}

module body_slab(h) {
    linear_extrude(h) rounded_rect(W, H, R);
}

module letter(s, sz) {
    linear_extrude(0.7)
        text(s, size = sz, font = "Liberation Sans", halign = "left", valign = "center", spacing = 1.15);
}

module top_plate() {
    difference() {
        union() {
            translate([0, 0, tray_h]) body_slab(skin);
            // Lip under the glass. The board seats from below. The ring rides outside.
            translate([cx, cy, tray_h - 1.2])
                difference() {
                    cylinder(d = 42, h = 1.4, $fn = 80);
                    translate([0, 0, -eps]) cylinder(d = hole_d, h = 2, $fn = 80);
                }
        }
        // 35 mm glass hole. The 32.4 mm glass shows. The 38 mm board stays under.
        translate([cx, cy, tray_h - 4])
            cylinder(d = hole_d, h = skin + 10, $fn = 80);
        // PCB pocket from below, tab pointing at the buttons.
        translate([cx, cy, tray_h - 2.4])
            cylinder(d = pcb_d + 1.4, h = 2.6, $fn = 80);
        translate([cx - 14, cy - pcb_len / 2 - 1, tray_h - 2.4])
            cube([28, 16, 2.6]);
        // Platter well.
        translate([cx, cy, body_h - 0.6])
            cylinder(d = platter_d + 1.1, h = 2, $fn = 120);
        // Copper tape seat, underside of the lid, just outside the LCD pocket.
        // 0.4 mm deep so the tape sits flush and the ring does not rub it.
        translate([cx, cy, tray_h - 0.5])
            difference() {
                cylinder(d = 58, h = 0.7, $fn = 80);
                translate([0, 0, -eps]) cylinder(d = 46, h = 1, $fn = 80);
            }
        // Lead channel from the tape ring toward the right edge, for the wire.
        translate([cx + 22, cy - 2, tray_h - 0.5])
            cube([70, 4, 0.7]);
        // Retaining lip. The ring skirt hooks under this. Drop the ring in, it stays.
        translate([cx, cy, tray_h - 0.2])
            difference() {
                cylinder(d = 48, h = 1.6, $fn = 80);
                translate([0, 0, -eps]) cylinder(d = 40, h = 2, $fn = 80);
            }
        // Through holes for the 12 mm switches. Cap is Ø6.9, body is 12.
        for (x = key_xs)
            translate([x, key_y, tray_h - 4])
                cylinder(d = 7.4, h = skin + 8, $fn = 24);
        // 125 mm pitch fader, cut through the skin.
        translate([fader_x, fader_y0 + fader_len / 2, tray_h - 2])
            cube([3.6, fader_len, skin + 6], center = true);
        // Encoder, through.
        translate([knob_xy[0], knob_xy[1], tray_h - 4])
            cylinder(d = 7.4, h = skin + 8, $fn = 28);
        // Corner screws, through.
        for (p = [[14, 14], [W - 14, 14], [14, H - 14], [W - 14, H - 14]])
            translate([p[0], p[1], tray_h - 2])
                cylinder(d = 3.4, h = skin + 6, $fn = 20);
        // Letter pockets. SCRATCH, katakana, the six labels.
        translate([18, 278, body_h - 0.7]) letter("SCRATCH", 12);
        // Katakana pocket. Liberation has no Japanese, so a bar stands in.
        translate([18, 246, body_h - 0.7]) cube([52, 8, 0.9]);
        for (i = [0 : 5])
            translate([key_xs[i], 48, body_h - 0.55])
                linear_extrude(0.6)
                    text(["CUE", "REV", "1", "2", "3", "4"][i],
                         size = 5.0, font = "Liberation Sans",
                         halign = "center", valign = "center");
        // Orange rule pocket under the katakana.
        translate([18, 236, body_h - 0.5])
            cube([64, 1.4, 0.7]);
        // Headphone, top edge, clear of the wordmark.
        translate([W - 28, H - 8, 11])
            rotate([-90, 0, 0])
                cylinder(d = 6.2, h = 14, $fn = 28);
        // USB-C, bottom edge.
        translate([W / 2 - 5, -2, 9])
            cube([10, 8, 3.2]);
    }
}

module gear_teeth(d, n, tooth_h, tooth_w) {
    for (i = [0 : n - 1])
        rotate([0, 0, i * 360 / n])
            translate([d / 2, 0, 0])
                cube([tooth_w, 1.6, tooth_h], center = true);
}

module ring() {
    difference() {
        union() {
            cylinder(d = platter_d, h = platter_t, $fn = 160);
            translate([0, 0, -6])
                cylinder(d = 44, h = 6.2, $fn = 80); // skirt, clears the 38 mm board
            // Flange. Catches under the lip on the lid so the ring cannot lift off.
            translate([0, 0, -6])
                cylinder(d = 47, h = 1.2, $fn = 80);
            // Gear on the underside. Drives the small pinion. Clear of the LCD.
            translate([0, 0, -2.4])
                cylinder(d = gear_d, h = 2.2, $fn = 80);
            translate([0, 0, -2.2])
                gear_teeth(gear_d, 48, 2.0, 1.4);
        }
        translate([0, 0, -6.1])
            cylinder(d = hole_d + 1.2, h = 12, $fn = 80);
        for (r = [30, 46, 62])
            translate([0, 0, platter_t - 0.32])
                difference() {
                    cylinder(d = r * 2 + 0.7, h = 0.45, $fn = 120);
                    translate([0, 0, -eps])
                        cylinder(d = r * 2 - 0.7, h = 0.65, $fn = 120);
                }
    }
}

module pinion() {
    // Small gear. The 6 mm diametric magnet sits on top. MT6701 is under it.
    difference() {
        union() {
            cylinder(d = pinion_d, h = 4, $fn = 40);
            translate([0, 0, 3.6])
                cylinder(d = magnet_d + 0.4, h = 1.2, $fn = 24);
            gear_teeth(pinion_d, 12, 3.6, 1.2);
        }
        translate([0, 0, 4.2])
            cylinder(d = magnet_d, h = 1.4, $fn = 24);
    }
}

module letters() {
    color([1, 0.30, 0.11]) {
        translate([18, 278, body_h - 0.7]) letter("SCRATCH", 12);
        // Katakana pocket. Liberation has no Japanese, so a bar stands in.
        translate([18, 246, body_h - 0.7]) cube([52, 8, 0.9]);
        for (i = [0 : 5])
            translate([key_xs[i], 48, body_h - 0.55])
                linear_extrude(0.6)
                    text(["CUE", "REV", "1", "2", "3", "4"][i],
                         size = 5.0, font = "Liberation Sans",
                         halign = "center", valign = "center");
        translate([18, 236, body_h - 0.5])
            cube([64, 1.4, 0.5]);
    }
}

module bottom() {
    difference() {
        body_slab(tray_h);
        translate([wall, wall, floor_t])
            linear_extrude(tray_h)
                rounded_rect(W - 2 * wall, H - 2 * wall, R - 1);
        // 5000 mAh pouch, under the button row. About 70 x 90 x 7.
        translate([18, 8, floor_t])
            cube([150, 48, 8]);
        // Encoder board keep-out, under the knob.
        translate([knob_xy[0] - 12, knob_xy[1] - 12, floor_t])
            cube([24, 24, 6]);
    }
}

if (part == "all") {
    color([0.92, 0.91, 0.89]) bottom();
    color([0.92, 0.91, 0.89]) top_plate();
    color([0.08, 0.08, 0.09]) translate([cx, cy, body_h - 0.7]) ring();
    color([0.3, 0.3, 0.32]) translate([gear_xy[0], gear_xy[1], body_h - 6]) pinion();
    letters();
} else if (part == "top") {
    translate([0, 0, -tray_h]) top_plate();
} else if (part == "bottom") {
    bottom();
} else if (part == "ring") {
    ring();
} else if (part == "pinion") {
    pinion();
} else if (part == "letters") {
    letters();
} else if (part == "knob") {
    cylinder(d = 16, h = 8, $fn = 48);
}

echo("BODY", W, H, body_h);
echo("RING", platter_d, "HOLE", hole_d);
echo("FADER", fader_len, "at", fader_x);
