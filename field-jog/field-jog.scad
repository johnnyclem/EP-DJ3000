// FIELD JOG
// Unofficial scratch deck shell meant to sit in a row with the
// Teenage Engineering EP-133 (240 x 176 x 16) and EP-136 (240 x 88 x 16).
// Not a TE product, and not a copy of the Frisk Fader / Scratch Ninja STL.
//
// Default size 88 x 240 x 16 mm. Same thickness as an EP-133 and an EP-136,
// so the cases sit flush. The 683ZZ lives in a sleeve that hangs below the
// deck, and the fader lock rides in the cavity under it. The platter and
// the knob stand proud of the 16 mm case, the way the K.O. knobs do.
//
// OpenSCAD customizer: change `part` to export one piece.
//   openscad -D 'part="top"' -o top.stl field-jog.scad
//
// Print the top and the platter face-up. See README.md.

/* [Export] */
part = "all"; // [all, section, top, bottom, platter, fader, fader_lock, knob, button, plus, tab, foot]

/* [Body] */
W = 88;
H = 240;
body_h = 16;
skin = 4.2;
wall = 2.6;
floor_t = 1.8;
R = 3.4;
top_shrink = 0.15;   // per side, so the deck drops in after elephant-foot

/* [Platter] */
platter_d = 76;
platter_t = 3.8;
well_d = 0.7;
platter_clear = 0.55; // extra diameter of the well
bearing_d = 7.15;     // 683ZZ is 7.00 OD; 0.15 mm press fit
bearing_h = 3.0;
shaft_hole = 5.6;     // ledge hole, clears an M3 shank, supports the bearing race

/* [Fader] */
fader_slot_w = 8.0;
fader_slot_l = 64;
fader_y0 = 26;
fader_stem_w = 7.2;
fader_stem_l = 13.0;
fader_stem_below = 3.2;
cap_w = 22;
cap_d = 14;
cap_h = 8;

/* [Knob] */
knob_d = 16;
knob_h = 12;
knob_hole = 6.3;      // friction fit on a 6 mm pot shaft

/* [Hidden] */
eps = 0.05;
tray_h = body_h - skin;
screw_inset = 9;
boss_d = 8.2;
pilot_d = 2.15;       // M2.5 self-tap
pilot_depth = 6.5;
cbore_d = 5.6;
cbore_h = 2.0;
foot_d = 8.2;
foot_h = 1.15;
foot_inset = 16;

// LEGO Technic pin holes on both long edges.
// Centers measured from the bottom end (y = 0, the fader end):
// 16, 25, 48, 216, 224. Height is 8 mm off the bottom face, so a pin
// meets a 16 mm EP side when both boxes sit on a desk.
technic_d = 5.0;       // 4.8 mm pin. 5.0 is a firm fit after typical PLA undersize
technic_depth = 8.2;   // blind. A friction pin's gripping half is about 8 mm
technic_z = 8.0;
technic_ys = [16, 25, 48, 216, 224];

platter_cx = W / 2;
platter_cy = 148;
knob_x = W - 14;
knob_y = H - 22;
fader_cx = W / 2;
name_rule_y = platter_cy + platter_d / 2 + 8;

encoder_pad_top = 6.2;
encoder_pad_w = 26;
encoder_pad_d = 20;

pico_w = 21;          // board width
pico_l = 51;
pico_x0 = W / 2 - pico_w / 2;
pico_y0 = H - wall - pico_l - 1.2;

btn_y = 58;
btn_x1 = 17;
btn_x2 = W - 17;
btn_well = 13.2;
btn_hole = 6.0;

hall_x = fader_cx + 13;
hall_z_top = 7.0;
hall_d = 6;

usb_w = 12.5;
usb_h = 6.2;
usb_z0 = 4.3;          // window centered at z = 7.4, same line as the jacks

jack_d = 6.3;          // 3.5 mm / 1/8 in TRS
jack_z = 7.4;
// Top edge, left to right: MIDI in, MIDI out, USB, line in, line out.
jack_xs = [20.1, 31.6, 56.4, 67.9];
hp_x = W / 2;          // headphone, centered on the fader end

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

module screw_positions() {
    translate([screw_inset, screw_inset]) children();
    translate([W - screw_inset, screw_inset]) children();
    translate([screw_inset, H - screw_inset]) children();
    translate([W - screw_inset, H - screw_inset]) children();
}

module foot_positions() {
    translate([foot_inset, foot_inset]) children();
    translate([W - foot_inset, foot_inset]) children();
    translate([foot_inset, H - foot_inset]) children();
    translate([W - foot_inset, H - foot_inset]) children();
}

// Local rib so a pin has a real bore. The side wall alone is only 2.6 mm.
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
                cylinder(h = technic_depth + eps, d = technic_d, $fn = 40);
        translate([-eps, y, technic_z])
            rotate([0, 90, 0])
                cylinder(h = 0.7, d1 = technic_d + 1.4, d2 = technic_d, $fn = 40);
        translate([W - technic_depth, y, technic_z])
            rotate([0, 90, 0])
                cylinder(h = technic_depth + eps, d = technic_d, $fn = 40);
        translate([W - 0.7, y, technic_z])
            rotate([0, 90, 0])
                cylinder(h = 0.7 + eps, d1 = technic_d, d2 = technic_d + 1.4, $fn = 40);
    }
}

// ---------- bottom tray ----------

module bottom() {
    difference() {
        union() {
            difference() {
                rounded_box(W, H, tray_h, R);
                translate([wall, wall, floor_t])
                    rounded_box(W - 2 * wall, H - 2 * wall, tray_h, max(0.8, R - wall + 0.4));
            }
            // screw bosses, flush with the rim
            screw_positions()
                translate([0, 0, floor_t - eps])
                    cylinder(d = boss_d, h = tray_h - floor_t + eps, $fn = 32);
            // AS5600 pad. Top of pad is the board seat (solder wires, no tall headers).
            translate([platter_cx - encoder_pad_w / 2,
                       platter_cy - encoder_pad_d / 2,
                       floor_t - eps])
                cube([encoder_pad_w, encoder_pad_d, encoder_pad_top - floor_t + eps]);
            // Pico / Pro Micro side rails. USB end faces the top wall.
            translate([pico_x0 - 2.4, pico_y0, floor_t])
                cube([1.6, pico_l, 2.4]);
            translate([pico_x0 + pico_w + 0.6, pico_y0, floor_t])
                cube([1.6, pico_l, 2.4]);
            translate([pico_x0 - 2.4, pico_y0 - 1.4, floor_t])
                cube([pico_w + 4.6, 1.4, 2.4]);
            // Hall posts. Magnet in the fader lock passes these.
            translate([hall_x, fader_y0 + 12, floor_t - eps])
                cylinder(d = hall_d, h = hall_z_top - floor_t + eps, $fn = 24);
            translate([hall_x, fader_y0 + fader_slot_l - 12, floor_t - eps])
                cylinder(d = hall_d, h = hall_z_top - floor_t + eps, $fn = 24);
            technic_ribs();
        }

        // M2.5 pilots, blind, from the rim down
        screw_positions()
            translate([0, 0, tray_h - pilot_depth])
                cylinder(d = pilot_d, h = pilot_depth + eps, $fn = 18);

        // M2 holes in the encoder pad, 16 mm apart, blind
        for (dx = [-8, 8])
            translate([platter_cx + dx, platter_cy, encoder_pad_top - 4])
                cylinder(d = 2.1, h = 4 + eps, $fn = 16);

        // USB-C window in the top end wall, lined up with a Pico on the floor.
        translate([W / 2 - usb_w / 2, H - wall - 1, usb_z0])
            cube([usb_w, wall + 2, usb_h]);

        // Four 3.5 mm jacks on that same end: MIDI in, MIDI out, line in, line out.
        // Through the wall only, so they miss the Pico.
        for (x = jack_xs)
            translate([x, H - wall - 0.2, jack_z])
                rotate([-90, 0, 0])
                    cylinder(d = jack_d, h = wall + 0.6, $fn = 28);

        // Headphone jack, centered on the fader end. The side wall stays pins only.
        translate([hp_x, -0.4, jack_z])
            rotate([-90, 0, 0])
                cylinder(d = jack_d, h = wall + 0.8, $fn = 28);

        // feet
        foot_positions()
            translate([0, 0, -eps])
                cylinder(d = foot_d, h = foot_h + eps, $fn = 28);

        // faint centerline under the fader, inside the floor, as a glue guide
        translate([fader_cx - 0.4, fader_y0 - 2, floor_t - 0.45])
            cube([0.8, fader_slot_l + 4, 0.5]);

        technic_bores();
    }
}

// ---------- top deck ----------

module y_round_slot(w, y0, y1, z, h) {
    hull() {
        translate([0, y0 + w / 2, z]) cylinder(d = w, h = h, $fn = 28);
        translate([0, y1 - w / 2, z]) cylinder(d = w, h = h, $fn = 28);
    }
}

module top_plate() {
    sh = top_shrink;
    bearing_drop = 0.3;
    ledge = 1.3;
    ledge_z = body_h - well_d - bearing_drop - bearing_h - ledge;
    difference() {
        union() {
            translate([sh, sh, tray_h])
                rounded_box(W - 2 * sh, H - 2 * sh, skin, R);
            // Sleeve drops into the tray so a 3 mm bearing fits in a 16 mm case.
            translate([platter_cx, platter_cy, ledge_z])
                cylinder(d = bearing_d + 6.4, h = body_h - well_d - ledge_z, $fn = 48);
        }

        // platter well
        translate([platter_cx, platter_cy, body_h - well_d])
            cylinder(d = platter_d + platter_clear, h = well_d + eps, $fn = 96);

        // 683ZZ pocket, open up into the well so the bearing drops in from the top
        // and stops on the ledge. The race ends bearing_drop below the platter.
        translate([platter_cx, platter_cy, ledge_z + ledge])
            cylinder(d = bearing_d, h = bearing_h + bearing_drop + 0.02, $fn = 48);

        // ledge bore under the bearing, for the M3 shank
        translate([platter_cx, platter_cy, ledge_z - eps])
            cylinder(d = shaft_hole, h = ledge + 2 * eps, $fn = 32);

        // crossfader slot
        translate([fader_cx, 0, tray_h - eps])
            y_round_slot(fader_slot_w, fader_y0, fader_y0 + fader_slot_l, 0, skin + 2 * eps);

        // buttons: shallow well + stem hole
        for (bx = [btn_x1, btn_x2]) {
            translate([bx - btn_well / 2, btn_y - btn_well / 2, body_h - 1.5])
                cube([btn_well, btn_well, 1.5 + eps]);
            translate([bx, btn_y, tray_h - eps])
                cylinder(d = btn_hole, h = skin + 2 * eps, $fn = 28);
        }

        // pot / knob bushing
        translate([knob_x, knob_y, tray_h - eps])
            cylinder(d = 7.3, h = skin + 2 * eps, $fn = 36);

        // screw counterbores, from the face
        screw_positions() {
            translate([0, 0, body_h - cbore_h])
                cylinder(d = cbore_d, h = cbore_h + eps, $fn = 24);
            translate([0, 0, tray_h - eps])
                cylinder(d = 2.8, h = skin + 2 * eps, $fn = 18);
        }

        // nameplate rule. Stops short of the side walls.
        translate([10, name_rule_y, body_h - 0.45])
            cube([W - 20, 0.7, 0.5]);

        // orange bar pocket
        translate([10, name_rule_y + 2.6, body_h - 0.65])
            cube([18, 2.2, 0.7]);

        // USB tab pocket, top-left, clear of the knob
        translate([8, H - 16, body_h - 0.65])
            cube([24, 8, 0.7]);
        translate([platter_cx, platter_cy, body_h - 0.4])
            for (a = [0:30:330])
                rotate([0, 0, a])
                    translate([platter_d / 2 + platter_clear / 2 + 1.1, -0.3, 0])
                        cube([2.6, 0.6, 0.45]);

        // type. Smaller than the 120 mm deck so the word clears the knob.
        translate([10, name_rule_y + 14, body_h - 0.48])
            linear_extrude(0.52)
                text("SCRATCH", size = 8, font = "Liberation Sans:style=Bold",
                     halign = "left", valign = "baseline", spacing = 1.02);
        translate([10, name_rule_y + 6.4, body_h - 0.42])
            linear_extrude(0.46)
                text("スクラッチ", size = 4.2, font = "IPAGothic",
                     halign = "left", valign = "baseline");
        translate([25.8, H - 6.2, body_h - 0.4])
            linear_extrude(0.42)
                text("MIDI", size = 2.6, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "center", spacing = 1.05);
        translate([62.2, H - 6.2, body_h - 0.4])
            linear_extrude(0.42)
                text("LINE", size = 2.6, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "center", spacing = 1.05);
        translate([hp_x, 7.2, body_h - 0.4])
            linear_extrude(0.42)
                text("HP", size = 2.6, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "center", spacing = 1.05);
        translate([btn_x1, btn_y + 12, body_h - 0.4])
            linear_extrude(0.42)
                text("CUE", size = 3.0, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "baseline", spacing = 1.08);
        translate([btn_x2, btn_y + 12, body_h - 0.4])
            linear_extrude(0.42)
                text("REV", size = 3.0, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "baseline", spacing = 1.08);
    }
}

// ---------- platter ----------
// Modeled in world space. Plus pocket and grip rings on the top face.
// Button-head M3 is captured in a pocket on the underside; the shank
// points down through the 683ZZ and carries the diametric magnet.

module plus_2d(arm, stroke) {
    square([arm, stroke], center = true);
    square([stroke, arm], center = true);
}

module platter() {
    pz = body_h - well_d;
    difference() {
        translate([platter_cx, platter_cy, pz])
            cylinder(d = platter_d, h = platter_t, $fn = 120);

        // grip rings
        for (gr = [18, 26, 33])
            translate([platter_cx, platter_cy, pz + platter_t - 0.32])
                difference() {
                    cylinder(d = gr * 2 + 0.9, h = 0.4, $fn = 96);
                    translate([0, 0, -eps])
                        cylinder(d = gr * 2 - 0.9, h = 0.4 + 2 * eps, $fn = 96);
                }

        // orange plus pocket
        translate([platter_cx, platter_cy, pz + platter_t - 0.75])
            linear_extrude(0.8)
                plus_2d(15, 3.3);

        // M3 button-head pocket, opens downward. No hole through the label face.
        // 6.8 mm bridges on the bed if you print the platter face-up.
        translate([platter_cx, platter_cy, pz - eps])
            cylinder(d = 6.8, h = 2.15, $fn = 36);
    }
}

module plus_mark() {
    linear_extrude(0.65)
        plus_2d(14.6, 3.05);
}

// ---------- fader ----------
// Print orientation: flat face of the cap on the bed, chamfer and stem pointing up.
// Installed, the chamfer ends up against the deck (a small bevel) and the flat
// face is what your hand hits. Stem drops through the slot.

stem_len = skin + fader_stem_below;
lock_w = 16;
lock_d = 20;
lock_t = 5.4;
socket_d = 2.0;

module fader() {
    hull() {
        translate([-cap_w / 2, -cap_d / 2, 0])
            cube([cap_w, cap_d, cap_h - 1.4]);
        translate([-cap_w / 2 + 1.3, -cap_d / 2 + 1.3, cap_h - 1.4])
            cube([cap_w - 2.6, cap_d - 2.6, 1.4]);
    }
    translate([-fader_stem_w / 2, -fader_stem_l / 2, cap_h - eps])
        cube([fader_stem_w, fader_stem_l, stem_len + eps]);
}

// Socket faces up. Magnet pocket faces down (6 mm bridge, or pause and drop the magnet in).
module fader_lock() {
    difference() {
        cube([lock_w, lock_d, lock_t]);
        translate([(lock_w - (fader_stem_w - 0.18)) / 2,
                   (lock_d - (fader_stem_l - 0.18)) / 2,
                   lock_t - socket_d])
            cube([fader_stem_w - 0.18, fader_stem_l - 0.18, socket_d + eps]);
        translate([lock_w / 2, lock_d / 2, -eps])
            cylinder(d = 6.3, h = 2.65, $fn = 28);
    }
}

module fader_placed() {
    mid_y = fader_y0 + fader_slot_l / 2;
    translate([fader_cx, mid_y, body_h + cap_h])
        rotate([180, 0, 0])
            fader();
}

module fader_lock_placed() {
    mid_y = fader_y0 + fader_slot_l / 2;
    z_tip = body_h - stem_len;
    lock_top = z_tip + socket_d;
    translate([fader_cx - lock_w / 2, mid_y - lock_d / 2, lock_top - lock_t])
        fader_lock();
}

// ---------- knob, buttons, inlays, feet ----------

module knob() {
    difference() {
        cylinder(d = knob_d, h = knob_h, $fn = 64);
        translate([0, 0, -eps])
            cylinder(d = knob_hole, h = knob_h + 2 * eps, $fn = 28);
        // clears the pot bushing nut so the knob sits low
        translate([0, 0, -eps])
            cylinder(d = 10.5, h = 2.4, $fn = 32);
        // knurl
        for (i = [0:15])
            rotate([0, 0, i * 22.5])
                translate([knob_d / 2 - 0.2, 0, -eps])
                    cylinder(d = 2.0, h = knob_h + 2 * eps, $fn = 10);
        // top indicator notch
        translate([-0.6, knob_d / 2 - 3.2, knob_h - 0.7])
            cube([1.2, 2.6, 0.8]);
    }
}

module button() {
    hull() {
        translate([-5.7, -5.7, 0]) cube([11.4, 11.4, 2.2]);
        translate([-5.0, -5.0, 2.2]) cube([10.0, 10.0, 2.2]);
    }
    translate([-2.15, -2.15, 4.2])
        cube([4.3, 4.3, 5.2]);
}

module usb_tab() {
    difference() {
        cube([23.6, 7.6, 0.6]);
        translate([11.8, 3.8, 0.32])
            linear_extrude(0.35)
                text("USB", size = 3.3, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "center", spacing = 1.12);
    }
}

module orange_bar() {
    cube([23.6, 2.05, 0.55]);
}

module button_placed(x, y) {
    // Cap is 4.4 tall in the print model and the stem grows off the top of it.
    // Flip so the stem enters the deck and the cap sits in the well.
    translate([x, y, body_h + 4.4 - 1.25])
        rotate([180, 0, 0])
            button();
}

module foot() {
    cylinder(d = foot_d - 0.3, h = 1.5, $fn = 32);
}

// ---------- assembly (preview only) ----------

module assembly_solids() {
    color([0.78, 0.78, 0.75]) bottom();
    color([0.82, 0.82, 0.79]) top_plate();
    color([0.11, 0.11, 0.12]) platter();
    color([0.93, 0.40, 0.09])
        translate([platter_cx, platter_cy, body_h - well_d + platter_t - 0.7])
            plus_mark();
    color([0.10, 0.10, 0.11]) fader_placed();
    color([0.16, 0.16, 0.17]) fader_lock_placed();
    color([0.93, 0.40, 0.09])
        translate([knob_x, knob_y, body_h])
            knob();
    color([0.12, 0.12, 0.13]) {
        button_placed(btn_x1, btn_y);
        button_placed(btn_x2, btn_y);
    }
    color([0.93, 0.40, 0.09]) {
        translate([10.2, name_rule_y + 2.7, body_h - 0.6]) orange_bar();
        translate([8.2, H - 15.8, body_h - 0.6]) usb_tab();
    }
    color([0.18, 0.18, 0.18])
        foot_positions()
            translate([0, 0, -1.45])
                foot();
}

module ghosts() {
    // bearing
    color([0.75, 0.75, 0.78, 0.55])
        translate([platter_cx, platter_cy, body_h - well_d - 0.4 - bearing_h])
            cylinder(d = 7, h = bearing_h, $fn = 32);
    // diametric magnet on the axle, target zone
    color([0.85, 0.15, 0.12, 0.8])
        translate([platter_cx, platter_cy, body_h - well_d - 0.3 - bearing_h - 1.3 - 2.6])
            cylinder(d = 6, h = 2.5, $fn = 24);
}

module everything() {
    assembly_solids();
    if (part == "all" || part == "section" || part == "section_fader") ghosts();
}

if (part == "all") {
    everything();
} else if (part == "section") {
    difference() {
        everything();
        translate([platter_cx, -20, -10]) cube([300, 400, 80]);
    }
} else if (part == "section_fader") {
    difference() {
        everything();
        translate([fader_cx, -20, -10]) cube([300, 400, 80]);
    }
} else if (part == "top") {
    translate([0, 0, -tray_h]) top_plate();
} else if (part == "bottom") {
    bottom();
} else if (part == "platter") {
    translate([-platter_cx, -platter_cy, -(body_h - well_d)]) platter();
} else if (part == "fader") {
    fader();
} else if (part == "fader_lock") {
    fader_lock();
} else if (part == "knob") {
    knob();
} else if (part == "button") {
    button();
} else if (part == "plus") {
    plus_mark();
} else if (part == "tab") {
    usb_tab();
} else if (part == "bar") {
    orange_bar();
} else if (part == "foot") {
    foot();
}

echo("OUTSIDE", W, H, body_h);
echo("TRAY_H", tray_h, "CAVITY_H", tray_h - floor_t);
echo("PLATTER_CENTER", platter_cx, platter_cy);
echo("PLATTER_PROUD", platter_t - well_d);
echo("KNOB", knob_x, knob_y);
echo("FADER_TRAVEL", fader_slot_l - fader_stem_l);
echo("ENCODER_PAD_TOP_Z", encoder_pad_top);
echo("HALL_POST_TOP_Z", hall_z_top);
echo("PICO_ORIGIN", pico_x0, pico_y0);
echo("USB_WINDOW_Z", usb_z0, usb_z0 + usb_h);
echo("TECHNIC", technic_ys, "z", technic_z, "d", technic_d);
