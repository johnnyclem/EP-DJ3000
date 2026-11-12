// FIELD JOG, GO
// Handheld. Prints flat on an Elegoo Mars 5 Ultra.
// The plate is 153.36 x 77.76 x 165 mm. This shell is 150 x 74 x 16,
// so each half sits on the plate with a few millimetres spare.
// Print one half per plate, face on the plate, cavity toward the vat.
// The caps, the knob, the fader cap and the cream bezel share a second plate.
//
// The fader is the same 75 x 9 mm dual-gang pot as the 88 mm deck
// (60 mm travel, B103). The screen is the 1.28 inch GC9A01.
// USB-C is the left end. The headphone jack is the right end.
//
//   openscad -D 'part="top"' -o go-top.stl go.scad

/* [Export] */
part = "all"; // [all, top, bottom, knob, cap, cue, play, util, bezel]

/* [Body] */
W = 150;
H = 74;
body_h = 16;
skin = 2.0;
wall = 1.8;
floor_t = 1.4;
R = 3.2;

/* [Screen] */
glass_d = 32.4;
hole_d = 33.0;
pcb_d = 38.6;
pcb_len = 45.5;
cx = 78;
cy = 48;

/* [Fader] */
// 75 x 9 mm body, 60 mm travel. Slot clears the 1.2 mm lever.
fader_cx = 78;
fader_y = 14;
fader_slot_w = 3.2;
fader_slot_l = 62;
pot_body_l = 75;
pot_body_w = 9.6;
pot_screw = 67;

/* [Controls] */
knob_xy = [132, 58];
cue_xy = [18, 24];
play_xy = [18, 9];
util_xs = [127, 141];
util_y = 11;
led_y = 20.2;

/* [Hidden] */
eps = 0.04;
tray_h = body_h - skin;

module rounded_rect(w, h, r) {
    hull() {
        translate([r, r]) circle(r = r, $fn = 28);
        translate([w - r, r]) circle(r = r, $fn = 28);
        translate([r, h - r]) circle(r = r, $fn = 28);
        translate([w - r, h - r]) circle(r = r, $fn = 28);
    }
}

module slab(h) {
    linear_extrude(h) rounded_rect(W, H, R);
}

module label(s, sz) {
    linear_extrude(0.85)
        text(s, size = sz, font = "Liberation Sans", halign = "left",
             valign = "center", spacing = 1.18);
}

module ticks() {
    for (i = [0 : 47])
        rotate([0, 0, i * 7.5])
            translate([0, 21.2, 0])
                cube([0.28, i % 4 == 0 ? 2.1 : 1.15, 0.7]);
}

module top_plate() {
    difference() {
        union() {
            translate([0, 0, tray_h]) slab(skin);
            // Skirt. Drops into the tray. 0.2 mm of air a side.
            translate([wall + 0.2, wall + 0.2, tray_h - 3.2])
                linear_extrude(3.2)
                    difference() {
                        rounded_rect(W - 2 * (wall + 0.2), H - 2 * (wall + 0.2), R - 1);
                        offset(r = -1.15)
                            rounded_rect(W - 2 * (wall + 0.2), H - 2 * (wall + 0.2), R - 1);
                    }
        }
        // Glass. 32.4 mm shows through a 33 mm hole.
        translate([cx, cy, tray_h - 1])
            cylinder(d = hole_d, h = skin + 3, $fn = 96);
        // Board pocket, from below. Tab points at the fader.
        translate([cx, cy, tray_h - 2.5])
            cylinder(d = pcb_d, h = 2.6, $fn = 80);
        translate([cx - 13, cy - pcb_len / 2, tray_h - 2.5])
            cube([26, 14, 2.6]);
        // Cream bezel sits in this ring, flush with the face.
        translate([cx, cy, body_h - 0.6])
            difference() {
                cylinder(d = 40.2, h = 0.8, $fn = 96);
                translate([0, 0, -eps]) cylinder(d = hole_d - 0.4, h = 1, $fn = 96);
            }
        // Hairline ticks around the glass.
        translate([cx, cy, body_h - 0.5]) ticks();
        // Inset face groove, 0.3 mm. A resin line, not a mould line.
        translate([0, 0, body_h - 0.32])
            linear_extrude(0.4)
                difference() {
                    offset(r = -2.2) rounded_rect(W, H, R);
                    offset(r = -2.7) rounded_rect(W, H, R);
                }
        // Encoder bushing.
        translate([knob_xy[0], knob_xy[1], tray_h - 1])
            cylinder(d = 7.2, h = skin + 3, $fn = 32);
        translate([knob_xy[0], knob_xy[1], tray_h - 1.3])
            cylinder(d = 10.2, h = 1.4, $fn = 32);
        // Fader slot.
        translate([fader_cx - fader_slot_l / 2, fader_y - fader_slot_w / 2, tray_h - 1])
            cube([fader_slot_l, fader_slot_w, skin + 3]);
        // Pot screws, from below. They do not break the face.
        for (s = [-1, 1])
            translate([fader_cx + s * pot_screw / 2, fader_y, tray_h - 4.2])
                cylinder(d = 1.7, h = 4.4, $fn = 16);
        // Cue and play. 12 mm switch under a 14 mm flush cap.
        for (p = [cue_xy, play_xy]) {
            translate([p[0], p[1], body_h - 1.7])
                cylinder(d = 14.4, h = 2, $fn = 40);
            translate([p[0], p[1], tray_h - 1])
                cylinder(d = 6.6, h = skin + 3, $fn = 28);
            translate([p[0], p[1], tray_h - 5.6])
                cube([12.4, 12.4, 5.8], center = true);
        }
        // Two small utilities, and a 3 mm LED above each.
        for (x = util_xs) {
            translate([x, util_y, body_h - 1.35])
                cylinder(d = 8.8, h = 1.6, $fn = 28);
            translate([x, util_y, tray_h - 1])
                cylinder(d = 4.2, h = skin + 3, $fn = 20);
            translate([x, util_y, tray_h - 4.2])
                cube([8.2, 8.2, 4.4], center = true);
            translate([x, led_y, tray_h - 1])
                cylinder(d = 3.15, h = skin + 3, $fn = 20);
            translate([x, led_y, body_h - 0.7])
                cylinder(d = 3.7, h = 0.9, $fn = 20);
        }
        // Wordmark and the orange rule. Pockets, 0.4 mm, for paint or a second resin.
        translate([8, 67.2, body_h - 0.55]) label("FIELD JOG", 4.4);
        translate([8, 63.4, body_h - 0.4])
            cube([26, 0.7, 0.5]);
        translate([27, cue_xy[1], body_h - 0.4])
            linear_extrude(0.8)
                text("CUE", size = 2.6, font = "Liberation Sans",
                     halign = "left", valign = "center", spacing = 1.05);
        translate([27, play_xy[1], body_h - 0.4])
            linear_extrude(0.8)
                text("PLAY", size = 2.6, font = "Liberation Sans",
                     halign = "left", valign = "center", spacing = 1.05);
        translate([util_xs[0] - 3.2, 26.2, body_h - 0.4])
            linear_extrude(0.4)
                text("LOOP", size = 2.1, font = "Liberation Sans",
                     halign = "left", valign = "center");
        translate([util_xs[1] - 2.4, 26.2, body_h - 0.4])
            linear_extrude(0.4)
                text("REV", size = 2.1, font = "Liberation Sans",
                     halign = "left", valign = "center");
        // Corner bosses, tapped from the tray.
        for (p = [[6.5, 6.5], [W - 6.5, 6.5], [6.5, H - 6.5], [W - 6.5, H - 6.5]])
            translate([p[0], p[1], tray_h - 6.4])
                cylinder(d = 1.6, h = 6.2, $fn = 16);
        // USB-C, left end. A panel-mount tongue, 11 x 4.6.
        translate([-1, 50, 6.6])
            cube([wall + 4, 11, 4.6]);
        // Headphone, right end. 3.5 mm jack, 6.3 mm hole.
        translate([W - wall - 2, 40, 8])
            rotate([0, 90, 0])
                cylinder(d = 6.3, h = wall + 4, $fn = 28);
    }
    // Boss walls around the four screws, so the pilot has meat.
    for (p = [[6.5, 6.5], [W - 6.5, 6.5], [6.5, H - 6.5], [W - 6.5, H - 6.5]])
        translate([p[0], p[1], tray_h - 6.4])
            difference() {
                cylinder(d = 5.2, h = 6.6, $fn = 20);
                translate([0, 0, -eps]) cylinder(d = 1.7, h = 7, $fn = 16);
            }
}

module bottom_tray() {
    difference() {
        union() {
            slab(floor_t);
            linear_extrude(tray_h)
                difference() {
                    rounded_rect(W, H, R);
                    offset(r = -wall) rounded_rect(W, H, max(R - wall, 0.8));
                }
            // Rails. They locate the 9.6 mm pot. The screws pull it to the lid.
            for (s = [-1, 1])
                translate([fader_cx - pot_body_l / 2 - 0.4,
                           fader_y + s * (pot_body_w / 2 + 0.3),
                           floor_t])
                    cube([pot_body_l + 0.8, 1.3, 4.2]);
        }
        // Screw clearance and a counterbore, from the outside.
        for (p = [[6.5, 6.5], [W - 6.5, 6.5], [6.5, H - 6.5], [W - 6.5, H - 6.5]]) {
            translate([p[0], p[1], -1])
                cylinder(d = 2.2, h = floor_t + 2, $fn = 16);
            translate([p[0], p[1], -eps])
                cylinder(d = 4.2, h = 1.1, $fn = 16);
        }
        // Pin channel under the pot, so the legs can fold flat.
        translate([fader_cx - pot_body_l / 2, fader_y - 1.4, -eps])
            cube([pot_body_l, 2.8, 1.0]);
        // Wash notches in the lip, four of them, so rinse water leaves.
        for (p = [[wall + 1, H / 2], [W - wall - 2.4, H / 2]])
            translate([p[0], p[1], tray_h - 1.2])
                cube([1.6, 3, 1.4]);
    }
}

module knob() {
    difference() {
        union() {
            cylinder(d = 16, h = 8.5, $fn = 48);
            translate([0, 0, 8.5]) cylinder(d = 12, h = 2.2, $fn = 40);
        }
        translate([0, 0, -eps]) cylinder(d = 6.05, h = 8, $fn = 24);
        // Flat for a D-shaft.
        translate([-0.4, 2.15, -eps]) cube([4, 2, 8]);
        // Knurl. Shallow, because resin holds a 0.35 mm flute.
        for (i = [0 : 23])
            rotate([0, 0, i * 15])
                translate([7.7, 0, 0.6])
                    cube([1.1, 0.45, 7.2], center = true);
        // Indicator notch.
        translate([-0.4, 6.2, 8.6]) cube([0.8, 2.2, 2.4]);
    }
}

module fader_cap() {
    difference() {
        hull() {
            translate([0, 0, 0]) cube([16, 8, 1], center = true);
            translate([0, 0, 5]) cube([14, 6.4, 1], center = true);
        }
        translate([0, 0, -eps]) cube([1.45, 5.2, 4.2], center = false);
        translate([-0.75, -2.6, -eps]) cube([1.5, 5.2, 4.2]);
        // Grip grooves.
        for (x = [-4.5, -2.2, 0, 2.2, 4.5])
            translate([x, -4.2, 2.2])
                cube([0.4, 8.4, 4]);
    }
}

module cue_cap_solid() {
    difference() {
        cylinder(d = 13.6, h = 3.0, $fn = 40);
        translate([0, 0, -eps]) cylinder(d = 3.0, h = 1.6, $fn = 16);
        translate([0, 0, 2.3])
            linear_extrude(0.85)
                text("CUE", size = 3.1, font = "Liberation Sans",
                     halign = "center", valign = "center", spacing = 1.0);
    }
}

module play_cap_solid() {
    difference() {
        cylinder(d = 13.6, h = 3.0, $fn = 40);
        translate([0, 0, -eps]) cylinder(d = 3.0, h = 1.6, $fn = 16);
        translate([0, -0.2, 2.3])
            linear_extrude(0.85)
                polygon([[-1.5, -1.7], [-1.5, 1.7], [1.8, 0]]);
    }
}

module util_cap_solid() {
    difference() {
        cylinder(d = 8.2, h = 2.5, $fn = 28);
        translate([0, 0, -eps]) cylinder(d = 2.2, h = 1.3, $fn = 14);
        translate([0, 0, 1.85]) cylinder(d = 2.4, h = 0.8, $fn = 16);
    }
}

module bezel() {
    difference() {
        cylinder(d = 39.6, h = 0.55, $fn = 96);
        translate([0, 0, -eps]) cylinder(d = 33.3, h = 0.8, $fn = 96);
    }
}

if (part == "all") {
    color([0.78, 0.77, 0.74]) top_plate();
    color([0.70, 0.69, 0.66]) bottom_tray();
    color([1, 0.30, 0.11]) translate([knob_xy[0], knob_xy[1], body_h]) knob();
    color([1, 0.30, 0.11]) translate([fader_cx, fader_y, body_h + 0.2]) fader_cap();
    color([0.08, 0.08, 0.09]) translate([cue_xy[0], cue_xy[1], body_h - 1.5]) cue_cap_solid();
    color([0.08, 0.08, 0.09]) translate([play_xy[0], play_xy[1], body_h - 1.5]) play_cap_solid();
    for (x = util_xs)
        color([0.08, 0.08, 0.09])
            translate([x, util_y, body_h - 1.2]) util_cap_solid();
    color([0.95, 0.93, 0.88]) translate([cx, cy, body_h - 0.55]) bezel();
} else if (part == "top") {
    top_plate();
} else if (part == "bottom") {
    bottom_tray();
} else if (part == "knob") {
    knob();
} else if (part == "cap") {
    fader_cap();
} else if (part == "cue") {
    cue_cap_solid();
} else if (part == "play") {
    play_cap_solid();
} else if (part == "util") {
    util_cap_solid();
} else if (part == "bezel") {
    bezel();
}

echo("PLATE", 153.36, 77.76, 165);
echo("BODY", W, H, body_h);
echo("FITS", W < 153.36 && H < 77.76);
