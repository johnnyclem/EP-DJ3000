// FIELD JOG, WIDE
// The 88 mm deck doubled to the measured K.O. II body: 176 x 240.5 x 16.3.
// layout = "twin"  two 76 mm platters
// layout = "six"   one 152.4 mm platter
//
// Same construction as the narrow shell. Two halves, four magnet pairs,
// Technic holes on the long edges. The pitch slots and the crossfader
// slot are cut for the 75 mm dual-gang pot (60 mm travel), centered on
// the marks in the viewer. The 6 inch deck has no room for that pot
// beside the wheel, so its pitch slot is 46 mm for a short fader.
//
//   openscad -D 'layout="twin"' -D 'part="top"' -o wide-twin-top.stl wide.scad
//   openscad -D 'layout="six"'  -D 'part="top"' -o wide-six-top.stl  wide.scad

/* [Export] */
layout = "twin"; // [twin, six]
part = "all";    // [all, top, bottom, platter, cue, loop, pad, pitch, cross]

/* [Body] */
W = 176;
H = 240.5;
body_h = 16.3;
skin = 4.2;
wall = 2.6;
floor_t = 1.8;
R = 3.4;
top_shrink = 0.15;

/* [Platter] */
platter_t = 3.8;
well_d = 0.7;
platter_clear = 0.55;
bearing_d = 7.15;
bearing_h = 3.0;
shaft_hole = 5.6;
// Snap cup for a 6 x 2.5 mm diametric magnet. Bottom face lands 2.7 mm
// above the encoder pad. The barb springs open under the sleeve.
mag_bottom = 6.2;
lip_h = 0.45;
mag_cav_h = 2.55;
stem_od = 7.7;
barb_od = 8.9;
barb_z0 = 9.3;
barb_h = 0.9;

/* [Faders] */
// 75 x 9 mm body, 60 mm travel, the same pot as the narrow deck.
fader_slot_w = 3.4;
fader_slot_l = 62;
pot_screw_span = 67;
// Short pitch, only on the 6 inch deck. Measure the fader before printing.
short_slot_l = 46;

/* [Screen] */
// Real 0.91" glass, centered on the mark in the viewer.
oled_w = 30.5;
oled_h = 11.85;

/* [Hidden] */
eps = 0.05;
tray_h = body_h - skin;
stem_hole = 3.85;
guide_h = 2.0;
guide_d = 7.6;
led_d = 3.1;
knob_hole = 7.3;

mag_pocket_l = 21;
mag_pocket_w = 6;
mag_pocket_d = 2.5;
mag_pockets = [
    [8, 5],
    [W - 8 - mag_pocket_l, 5],
    [8, H - 5 - mag_pocket_w],
    [W - 8 - mag_pocket_l, H - 5 - mag_pocket_w]
];

technic_d = 5.0;
technic_depth = 8.2;
technic_z = 8.0;
technic_ys = [16, 25, 48, 216, 224];

jack_d = 6.3;
jack_z = 7.4;
// Clear of the corner magnets. The posts occupy x 7–30 and x 146–169.
// A jack in those spans dead-ends in the post.
jack_xs = [52, 68, 108, 124];
usb_w = 12.5;
usb_h = 4.6;
usb_x = W / 2;
usb_z0 = 4.3;

encoder_pad_top = 3.5;
encoder_pad_w = 26;       // MT6701 board is 23 mm square
encoder_pad_d = 26;
encoder_hole_d = 2.2;
encoder_hole_pitch = 17.5;

$fn = 48;

module rounded_rect(w, h, r) {
    rr = min(r, min(w, h) / 2 - 0.01);
    hull() {
        translate([rr, rr]) circle(r = rr);
        translate([w - rr, rr]) circle(r = rr);
        translate([rr, h - rr]) circle(r = rr);
        translate([w - rr, h - rr]) circle(r = rr);
    }
}

module rounded_box(w, h, z, r) {
    linear_extrude(z) rounded_rect(w, h, r);
}

module y_slot(x, y0, y1, z, h, w) {
    hull() {
        translate([x, y0 + w / 2, z]) cylinder(d = w, h = h, $fn = 24);
        translate([x, y1 - w / 2, z]) cylinder(d = w, h = h, $fn = 24);
    }
}

module x_slot(y, x0, x1, z, h, w) {
    hull() {
        translate([x0 + w / 2, y, z]) cylinder(d = w, h = h, $fn = 24);
        translate([x1 - w / 2, y, z]) cylinder(d = w, h = h, $fn = 24);
    }
}

module technic_ribs() {
    rib_x = technic_depth + 1.4 - wall;
    for (y = technic_ys) {
        y0 = y - technic_d / 2 - 1.5;
        y1 = y + technic_d / 2 + 1.5;
        z1 = min(technic_z + technic_d / 2 + 1.2, tray_h - 0.35);
        translate([wall - eps, y0, floor_t - eps])
            cube([rib_x, y1 - y0, z1 - floor_t]);
        translate([W - wall - rib_x, y0, floor_t - eps])
            cube([rib_x + eps, y1 - y0, z1 - floor_t]);
    }
}

module technic_bores() {
    for (y = technic_ys) {
        translate([-eps, y, technic_z])
            rotate([0, 90, 0])
                cylinder(h = technic_depth + eps, d = technic_d, $fn = 32);
        translate([W - technic_depth, y, technic_z])
            rotate([0, 90, 0])
                cylinder(h = technic_depth + eps, d = technic_d, $fn = 32);
    }
}

module magnet_posts() {
    for (p = mag_pockets) {
        x0 = p[0] - 1.2;
        x1 = p[0] + mag_pocket_l + 1.2;
        y0 = p[1] < H / 2 ? wall - eps : p[1] - 1.2;
        y1 = p[1] < H / 2 ? p[1] + mag_pocket_w + 1.2 : H - wall + eps;
        translate([x0, y0, floor_t])
            cube([x1 - x0, y1 - y0, tray_h - floor_t]);
    }
}

module magnet_pockets(z) {
    for (p = mag_pockets)
        translate([p[0], p[1], z])
            cube([mag_pocket_l, mag_pocket_w, mag_pocket_d + eps]);
}

// ---------- layout marks, matching the viewer ----------

module twin_platters() {
    // centers
}

function platter_list() =
    layout == "twin"
        ? [[46, 158, 76], [130, 158, 76]]
        : [[88, 142, 152.4]];

function round_buttons() =
    layout == "twin"
        ? [[40, 40, 18], [66, 40, 18], [110, 40, 18], [136, 40, 18],
           [40, 76, 12], [66, 76, 12], [110, 76, 12], [136, 76, 12]]
        : [[28, 30, 20], [54, 30, 20], [28, 52, 12], [22, 196, 12], [22, 214, 12]];

function led_list() =
    layout == "twin"
        ? [[40, 88], [66, 88], [110, 88], [136, 88]]
        : [[34, 196], [34, 214]];

function pad_list() =
    layout == "twin"
        ? [for (x = [36, 50, 64, 78, 98, 112, 126, 140]) [x, 100, 11]]
        : [for (x = [100, 116, 132, 148]) [x, 36, 12]];

function pitch_list() =
    layout == "twin"
        ? [[18, 35, 97], [158, 35, 97]]
        : [[166, 18, 18 + short_slot_l]];

function knob_at() = layout == "twin" ? [158, 218] : [150, 226];
function screen_at() = layout == "twin" ? [88, 220] : [78, 228];

module encoder_pads() {
    for (p = platter_list())
        translate([p[0] - encoder_pad_w / 2, p[1] - encoder_pad_d / 2, floor_t - eps])
            cube([encoder_pad_w, encoder_pad_d, encoder_pad_top - floor_t + eps]);
}

module bottom() {
    difference() {
        union() {
            difference() {
                rounded_box(W, H, tray_h, R);
                translate([wall, wall, floor_t])
                    rounded_box(W - 2 * wall, H - 2 * wall, tray_h, max(0.8, R - wall + 0.4));
            }
            magnet_posts();
            encoder_pads();
            technic_ribs();
        }
        magnet_pockets(tray_h - mag_pocket_d);
        for (p = platter_list())
            for (sx = [-1, 1])
                for (sy = [-1, 1])
                    translate([p[0] + sx * encoder_hole_pitch / 2,
                               p[1] + sy * encoder_hole_pitch / 2, 0.7])
                        cylinder(d = encoder_hole_d, h = encoder_pad_top - 0.7 + eps, $fn = 14);
        // USB-C and the four jacks, through the rear wall into open cavity.
        // The corner magnet posts stop at x 30 and x 146, so this row is clear.
        translate([usb_x - usb_w / 2, H - wall - 3, usb_z0])
            cube([usb_w, wall + 6, usb_h]);
        for (x = jack_xs)
            translate([x, H - wall - 3, jack_z])
                rotate([-90, 0, 0])
                    cylinder(d = jack_d, h = wall + 6, $fn = 24);
        translate([W / 2, -0.4, jack_z])
            rotate([-90, 0, 0])
                cylinder(d = jack_d, h = wall + 0.8, $fn = 24);
        technic_bores();
    }
}

module bearing_sleeve(x, y) {
    ledge = 1.3;
    bearing_drop = 0.3;
    ledge_z = body_h - well_d - bearing_drop - bearing_h - ledge;
    translate([x, y, ledge_z])
        cylinder(d = bearing_d + 6.4, h = body_h - well_d - ledge_z, $fn = 40);
}

module top_plate() {
    sh = top_shrink;
    difference() {
        union() {
            translate([sh, sh, tray_h])
                rounded_box(W - 2 * sh, H - 2 * sh, skin, R);
            for (p = platter_list()) bearing_sleeve(p[0], p[1]);
            for (b = round_buttons())
                translate([b[0], b[1], body_h])
                    cylinder(d = min(guide_d, b[2] - 2), h = guide_h, $fn = 28);
            for (b = pad_list())
                translate([b[0], b[1], body_h])
                    cylinder(d = guide_d, h = guide_h, $fn = 24);
        }

        for (p = platter_list()) {
            translate([p[0], p[1], body_h - well_d])
                cylinder(d = p[2] + platter_clear, h = well_d + eps, $fn = 96);
            ledge = 1.3;
            bearing_drop = 0.3;
            ledge_z = body_h - well_d - bearing_drop - bearing_h - ledge;
            translate([p[0], p[1], ledge_z - eps])
                cylinder(d = 8.2, h = body_h - ledge_z + 1, $fn = 40);
            translate([p[0], p[1], body_h - well_d - 0.8])
                cylinder(d1 = 8.2, d2 = 9.6, h = 0.8, $fn = 40);
        }

        for (s = pitch_list()) {
            y0 = s[1];
            y1 = s[2];
            translate([0, 0, tray_h - eps])
                y_slot(s[0], y0, y1, 0, skin + 2 * eps, fader_slot_w);
            mid = (y0 + y1) / 2;
            // The 75 mm pot's screws are 67 mm apart, just past a 62 mm slot.
            // The short fader's screws sit 4 mm past each end of its slot.
            span = (y1 - y0) >= 60 ? pot_screw_span : (y1 - y0) + 8;
            for (dy = [-span / 2, span / 2])
                translate([s[0], mid + dy, tray_h - eps])
                    cylinder(d = 2.2, h = skin + 2 * eps, $fn = 14);
        }

        if (layout == "twin") {
            x0 = 88 - fader_slot_l / 2;
            x1 = 88 + fader_slot_l / 2;
            translate([0, 0, tray_h - eps])
                x_slot(16, x0, x1, 0, skin + 2 * eps, fader_slot_w);
            for (dx = [-pot_screw_span / 2, pot_screw_span / 2])
                translate([88 + dx, 16, tray_h - eps])
                    cylinder(d = 2.2, h = skin + 2 * eps, $fn = 14);
        }

        for (b = round_buttons()) {
            translate([b[0], b[1], tray_h - eps])
                cylinder(d = stem_hole, h = skin + guide_h + 2 * eps, $fn = 20);
            translate([b[0], b[1], body_h - 0.7])
                cylinder(d = b[2] + 0.4, h = 0.7 + guide_h + eps, $fn = 36);
        }
        for (b = pad_list()) {
            translate([b[0], b[1], tray_h - eps])
                cylinder(d = stem_hole, h = skin + guide_h + 2 * eps, $fn = 18);
            translate([b[0] - b[2] / 2, b[1] - b[2] / 2, body_h - 0.8])
                cube([b[2], b[2], 0.8 + guide_h + eps]);
        }
        for (p = led_list())
            translate([p[0], p[1], tray_h - eps])
                cylinder(d = led_d, h = skin + 2 * eps, $fn = 16);

        k = knob_at();
        translate([k[0], k[1], tray_h - eps])
            cylinder(d = knob_hole, h = skin + 2 * eps, $fn = 28);

        s = screen_at();
        translate([s[0] - 20.3, s[1] - 6.7, tray_h - eps])
            cube([40.6, 13.4, 1.6]);
        translate([s[0] - oled_w / 2, s[1] - oled_h / 2, tray_h + 1.4])
            cube([oled_w, oled_h, skin]);

        magnet_pockets(tray_h - eps);
    }
}

module platter_solid(d) {
    pz = body_h - well_d;
    mouth = mag_bottom - lip_h;
    difference() {
        union() {
            translate([0, 0, pz])
                cylinder(d = d, h = platter_t, $fn = 120);
            translate([0, 0, mouth])
                cylinder(d = stem_od, h = pz - mouth, $fn = 64);
            translate([0, 0, barb_z0])
                cylinder(d = barb_od, h = barb_h, $fn = 64);
            translate([0, 0, barb_z0 + barb_h])
                cylinder(d1 = barb_od, d2 = stem_od, h = 0.6, $fn = 64);
            translate([0, 0, mouth])
                cylinder(d1 = stem_od - 1.2, d2 = stem_od, h = 0.5, $fn = 48);
        }
        arm = d > 100 ? 22 : 13;
        translate([0, 0, pz + platter_t - 0.7])
            linear_extrude(0.8) {
                square([arm, 3.2], center = true);
                square([3.2, arm], center = true);
            }
        translate([0, 0, mouth - eps])
            cylinder(d1 = stem_od - 1.4, d2 = 5.6, h = lip_h + eps, $fn = 48);
        translate([0, 0, mag_bottom - eps])
            cylinder(d = 6.15, h = mag_cav_h + eps, $fn = 48);
        translate([0, 0, barb_z0 + barb_h])
            cylinder(d = 4.4, h = pz - (barb_z0 + barb_h) + eps, $fn = 36);
        for (a = [0, 120, 240])
            rotate([0, 0, a])
                translate([-0.6, 0, 8.9])
                    cube([1.2, stem_od, pz - 8.9]);
        for (gr = d > 100 ? [36, 52, 66] : [18, 26, 33])
            translate([0, 0, pz + platter_t - 0.32])
                difference() {
                    cylinder(d = gr * 2 + 0.9, h = 0.4, $fn = 80);
                    translate([0, 0, -eps])
                        cylinder(d = gr * 2 - 0.9, h = 0.5, $fn = 80);
                }
    }
}

module round_cap(d, h) {
    cylinder(d = d, h = h, $fn = 48);
    translate([0, 0, h - eps])
        cylinder(d1 = d, d2 = d - 1.2, h = 1.2, $fn = 48);
}

module pad_cap(w) {
    linear_extrude(2.4) rounded_rect(w, w, 1.2);
}

module pitch_cap() {
    linear_extrude(6) rounded_rect(12, 16, 1.4);
}

module cross_cap() {
    linear_extrude(6) rounded_rect(18, 12, 1.4);
}

if (part == "all") {
    color([0.78, 0.78, 0.75]) bottom();
    color([0.84, 0.84, 0.80]) top_plate();
    color([0.1, 0.1, 0.11])
        for (p = platter_list())
            translate([p[0], p[1], 0])
                platter_solid(p[2]);
} else if (part == "top") {
    translate([0, 0, -tray_h]) top_plate();
} else if (part == "bottom") {
    bottom();
} else if (part == "platter") {
    translate([0, 0, -(mag_bottom - lip_h)]) platter_solid(platter_list()[0][2]);
} else if (part == "cue") {
    round_cap(layout == "twin" ? 18 : 20, 5.2);
} else if (part == "loop") {
    round_cap(12, 3.6);
} else if (part == "pad") {
    pad_cap(layout == "twin" ? 11 : 12);
} else if (part == "pitch") {
    pitch_cap();
} else if (part == "cross") {
    cross_cap();
}

echo("LAYOUT", layout);
echo("BODY", W, H, body_h);
echo("PLATTERS", len(platter_list()));
