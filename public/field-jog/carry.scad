// FIELD JOG, CARRY
// The original deck, a little bigger, made to be held.
// One 152.4 mm ring. A round screen sits in the hole and does not spin.
// Letters are pockets for a second color, not a vinyl cut and not an engraving.
//
// 168 x 228 x 26 mm. A 5000 mAh pouch lies flat under the buttons.
// The ESP32-S3 stays. A P4 is not justified for a SPI round panel.
//
//   openscad -D 'part="top"' -o carry-top.stl carry.scad
//   openscad -D 'part="ring"' -o carry-ring.stl carry.scad

/* [Export] */
part = "all"; // [all, top, bottom, ring, letters, knob, cue]

/* [Body] */
W = 168;
H = 228;
body_h = 26;
skin = 4.0;
wall = 2.4;
floor_t = 1.8;
R = 4.2;

/* [Platter] */
platter_d = 152.4;
platter_t = 3.6;
glass_d = 32.4;       // GC9A01 active area
hole_d = 35;          // shows the glass, hides the blue board
pcb_d = 38;           // round part of the module
pcb_len = 45.5;       // includes the pin tab
cx = 84;
cy = 134;

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

module letter(s, w) {
    linear_extrude(0.8)
        text(s, size = 4.2, font = "Liberation Sans:style=Bold", halign = "center", valign = "center");
}

module top_plate() {
    difference() {
        union() {
            translate([0, 0, tray_h]) body_slab(skin);
            // Stationary hub. The flex drops down the bore. The ring rides the outside.
            translate([cx, cy, tray_h - 1])
                cylinder(d = 28, h = skin + 1.2, $fn = 80);
        }
        translate([cx, cy, tray_h - 2])
            cylinder(d = 8, h = skin + 8, $fn = 24);
        // PCB pocket, and the tab aiming at the buttons. Wires leave there.
        translate([cx, cy, body_h - 4.2])
            cylinder(d = pcb_d + 1.2, h = 2.4, $fn = 80);
        translate([cx - 14, cy - pcb_len / 2 - 1, body_h - 4.2])
            cube([28, 16, 2.4]);
        translate([cx, cy, body_h - 0.7])
            cylinder(d = platter_d + 1.1, h = 2, $fn = 120);
        // Button stems
        for (p = [[36, 48], [84, 48]])
            translate([p[0], p[1], tray_h - eps])
                cylinder(d = 3.9, h = skin + 3, $fn = 24);
        // Hot cue pads, 22 x 16 pockets
        for (x = [28, 62, 96, 130])
            translate([x - 11, 14 - 8, body_h - 1.6])
                cube([22, 16, 2]);
        // Letter pockets, 0.8 mm, filled by the letters part
        for (p = [["CUE", 36, 32, 18], ["PLAY", 84, 32, 24]])
            translate([p[1], p[2], body_h - 0.8])
                cube([p[3], 6, 1], center = true);
        // Knob
        translate([146, 44, tray_h - eps])
            cylinder(d = 7.3, h = skin + 2, $fn = 28);
        // Headphone, top edge
        translate([28, H - 8, 13])
            rotate([-90, 0, 0])
                cylinder(d = 6.2, h = 14, $fn = 28);
        // USB-C
        translate([127, H - 6, 12.4])
            cube([10, 10, 3.4]);
    }
}

module ring() {
    difference() {
        union() {
            cylinder(d = platter_d, h = platter_t, $fn = 160);
            translate([0, 0, -6])
                cylinder(d = 44, h = 6.2, $fn = 80); // skirt, clears the 38 mm board
        }
        translate([0, 0, -6.1])
            cylinder(d = hole_d + 1.2, h = 12, $fn = 80);
        for (r = [40, 52, 66])
            translate([0, 0, platter_t - 0.35])
                difference() {
                    cylinder(d = r * 2 + 0.7, h = 0.5, $fn = 120);
                    translate([0, 0, -eps])
                        cylinder(d = r * 2 - 0.7, h = 0.7, $fn = 120);
                }
    }
}

module letters() {
    color([0.94, 0.35, 0.1]) {
        translate([36, 32, body_h - 0.8]) letter("CUE", 18);
        translate([84, 32, body_h - 0.8]) letter("PLAY", 24);
        for (i = [0 : 3])
            translate([[28, 62, 96, 130][i], 14, body_h - 0.6])
                letter(str(i + 1), 8);
    }
}

module bottom() {
    difference() {
        body_slab(tray_h);
        translate([wall, wall, floor_t])
            linear_extrude(tray_h)
                rounded_rect(W - 2 * wall, H - 2 * wall, R - 1);
        // Battery bay, 5000 mAh pouch, under the controls. 62 x 80 x 7.
        translate([20, 8, floor_t])
            cube([128, 42, 8]);
    }
}

if (part == "all") {
    color([0.83, 0.83, 0.80]) bottom();
    color([0.83, 0.83, 0.78]) top_plate();
    color([0.08, 0.08, 0.09]) translate([cx, cy, body_h - 0.7]) ring();
    letters();
} else if (part == "top") {
    translate([0, 0, -tray_h]) top_plate();
} else if (part == "bottom") {
    bottom();
} else if (part == "ring") {
    ring();
} else if (part == "letters") {
    letters();
} else if (part == "knob") {
    cylinder(d = 16, h = 8, $fn = 48);
} else if (part == "cue") {
    cylinder(d = 18, h = 5, $fn = 48);
}

echo("BODY", W, H, body_h);
echo("RING", platter_d, "HOLE", hole_d);
