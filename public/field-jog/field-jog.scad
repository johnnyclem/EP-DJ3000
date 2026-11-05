// FIELD JOG
// Unofficial scratch deck shell meant to sit in a row with the
// Teenage Engineering EP-133 (240 x 176 x 16) and EP-136 (240 x 88 x 16).
// Not a TE product, and not a copy of the Frisk Fader / Scratch Ninja STL.
//
// Default size 88 x 240. The playing surface is 16 mm, same as an EP-133
// and an EP-136, so the cases sit flush. The top 52 mm rises to 22 mm so
// the 2xAA holder fits. That rise is part of the top shell, not a door.
// The two halves close on four pairs of 5 x 20 x 2 mm magnets.
//
// OpenSCAD customizer: change `part` to export one piece.
//   openscad -D 'part="top"' -o top.stl field-jog.scad
//
// Print the top and the platter face-up. See README.md.

/* [Export] */
part = "all"; // [all, section, top, bottom, platter, fader, fader_lock, knob, button, loop, flange, plus, foot]

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
// Dual-gang slide pot, 75 x 9 mm body, 60 mm travel, lever 1.2 mm thick.
// B103 is 10 k linear. Body is 7.5 mm tall, so the rails leave 8.2 mm and
// the two M2s pull it up against the deck. The slot clears the 1.2 mm lever.
fader_slot_w = 3.4;     // the lever is 1.2 mm. The old 8 mm slot was for the printed stem
fader_slot_l = 62;      // 60 mm travel, plus the lever, plus a hair
fader_y0 = 27;          // center stays at y = 58, under the 75 mm body
pot_body_l = 75;
pot_body_w = 9.6;       // 9 ± 0.5 mm body
pot_body_h = 8.2;       // drawing says 7.5. Leave room so the rails don't crush it
cap_w = 22;
cap_d = 14;
cap_h = 8;
tang_slot_w = 1.45;     // press fit on the 1.2 mm tang. 1.6 rocked
tang_slot_l = 5.4;
tang_slot_d = 6.6;      // the tang only sticks a few mm past the skin
pot_screw_span = 67;    // the two M2 threads, on the centerline
pot_y0 = fader_y0 + fader_slot_l / 2 - pot_body_l / 2;

/* [Knob] */
knob_d = 16;
knob_h = 12;
knob_hole = 6.3;      // friction fit on a 6 mm pot shaft
knob_x = 78;          // right of the 2xAA holder
knob_y = H - 24;

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
fader_cx = W / 2;

// Raised cap over the top end. Same piece as the deck. The cells come out
// when the top shell comes off.
bump_y0 = 188;
bump_h = 22;

// 5 x 20 x 2 mm magnets, four in the tray and four in the lid. The pocket
// is 0.5 mm large on every side in plan. Depth is the magnet plus 0.5 mm
// of glue behind it, so the faces sit flush and touch when the case closes.
mag_l = 20;
mag_w = 5;
mag_t = 2;
mag_clear = 0.5;
mag_glue = 0.5;
mag_pocket_l = mag_l + 2 * mag_clear;   // 21
mag_pocket_w = mag_w + 2 * mag_clear;   // 6
mag_pocket_d = mag_t + mag_glue;        // 2.5
// Long axis across the case, just inside each short end.
mag_pockets = [
    [6, 4.6],
    [W - 6 - mag_pocket_l, 4.6],
    [6, H - 4.6 - mag_pocket_w],
    [W - 6 - mag_pocket_l, H - 4.6 - mag_pocket_w]
];

// Door outline, kept so the OLED stays where the vinyl expects it.
plate_t = 1.6;
plate_x0 = 3;
plate_y0 = 188;
plate_w = 82;
plate_h = 46;

// Measured module, not the catalog 0.91" drawing. Glass is 30.5 x 11.85.
// The PCB is 40 x 12.85 and tapes to the underside of the door, in the
// air above the cells.
oled_w = 30.5;
oled_h = 11.85;
oled_cx = 28;   // plate-local
oled_cy = 22;

encoder_pad_top = 3.5;   // headerless AS5600. 6.2 left 1.9 mm and the chip hit the magnet
encoder_pad_w = 26;
encoder_pad_d = 20;

// Headerless ESP32-S3-DevKitC-1, parked on the left under the platter.
// The battery cap owns the top end, so the board's own USB no longer
// meets the back panel. Run a short lead to the window.
devkit_l = 70;
devkit_w = 25.5;
devkit_x0 = 3.6;
devkit_y0 = 102;

// Leaded 2xAA holder, the common 59 x 33 x 15 mm box. Pocket is 1 mm
// larger on X and Y and 16 mm clear above the floor, so the springs
// are not crushed by the door. Jack bodies on the end wall must stay
// under 12 mm or they meet the cells.
holder_l = 60;
holder_w = 34.5;
holder_x0 = 11;
holder_y0 = 193;

// Cue and play on the lower left, clear of the scratch fader.
play_x = 17;
play_y = 36;
cue_x = 17;
cue_y = 60;
play_d = 16;
play_h = 7.0;
in_x = 17;
in_y = 86;
out_x = 17;
out_y = 102;
loop_d = 10;
loop_h = 5.2;
led_d = 3.1;
rev_x = 71;
rev_y = 48;
// Plunger. A 2 mm boss on the deck makes a 6.2 mm guide, so the cap cannot
// rock. Travel is 0.45 mm: the click, then the cap stops, before the dome
// is crushed. Bore is 3.85. If it grinds, run a 3.5 mm drill through it.
stem_d = 3.4;
stem_hole = 3.85;
btn_travel = 0.45;
guide_h = 2.0;
guide_d = 7.6;
neck_d = 2.2;
neck_l = 1.9;
neck_from_face = guide_h + skin + btn_travel;
tip_l = 2.0;
flange_d = 8.6;
flange_t = 1.5;
tact_body = 6.3;     // 6.0 mm body, a little clearance
nest_wall = 1.3;
nest_rim = 1.5;
nest_pad = 1.2;      // 5 mm switch. The tip is on the actuator at rest.

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
    // Below the battery plate, clear of the platter. The plate owns the top end.
    translate([screw_inset, 176]) children();
    translate([W - screw_inset, 176]) children();
}

module foot_positions() {
    translate([foot_inset, foot_inset]) children();
    translate([W - foot_inset, foot_inset]) children();
    translate([foot_inset, H - foot_inset]) children();
    translate([W - foot_inset, H - foot_inset]) children();
}

// Posts the magnets sit in. Fused to the short wall.
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
            // Magnet posts, one per corner. The pocket opens on the rim.
            magnet_posts();
            // AS5600 pad. Top of pad is the board seat (solder wires, no tall headers).
            translate([platter_cx - encoder_pad_w / 2,
                       platter_cy - encoder_pad_d / 2,
                       floor_t - eps])
                cube([encoder_pad_w, encoder_pad_d, encoder_pad_top - floor_t + eps]);
            // DevKitC-1 locating lip. Left side, under the platter. Headerless.
            difference() {
                translate([devkit_x0 - 1.3, devkit_y0 - 1.3, floor_t])
                    cube([devkit_w + 2.6, devkit_l + 2.6, 0.8]);
                translate([devkit_x0 - 0.4, devkit_y0 - 0.4, floor_t - eps])
                    cube([devkit_w + 0.8, devkit_l + 0.8, 1.2]);
            }
            // Slide-pot cradle. The 75 mm body sits up against the deck.
            // Two rails pinch the 9 mm width. A channel in the floor is
            // where the pins go after they are bent flat.
            translate([fader_cx - pot_body_w / 2 - 1.6, pot_y0, floor_t])
                cube([1.6, pot_body_l, tray_h - pot_body_h - floor_t]);
            translate([fader_cx + pot_body_w / 2, pot_y0, floor_t])
                cube([1.6, pot_body_l, tray_h - pot_body_h - floor_t]);
            technic_ribs();
            // Switch nests. The rim keeps a 6x6 from walking. The gap faces
            // the middle of the deck so the pins can be bent out and soldered.
            button_nests();
        }

        // Magnet pockets, opening on the rim. Glue goes in the bottom 0.5 mm.
        for (p = mag_pockets)
            translate([p[0], p[1], tray_h - mag_pocket_d])
                cube([mag_pocket_l, mag_pocket_w, mag_pocket_d + eps]);

        // M2 pilots in the encoder pad. Blind, and they stop inside the floor.
        for (dx = [-8, 8])
            translate([platter_cx + dx, platter_cy, 0.7])
                cylinder(d = 2.1, h = encoder_pad_top - 0.7 + eps, $fn = 16);

        // USB-C window in the top end wall, lined up with the DevKit's native port.
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

        // Pin channel under the slide pot. Bend the pins flat into this.
        translate([fader_cx - 4, pot_y0 + 2, floor_t - 1.2])
            cube([8, pot_body_l - 4, 1.2 + eps]);

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
            // Battery cap. Same outline, cut off below the platter so only
            // the top end is taller.
            intersection() {
                translate([sh, sh, tray_h])
                    rounded_box(W - 2 * sh, H - 2 * sh, bump_h - tray_h, R);
                translate([-1, bump_y0, 0])
                    cube([W + 2, H, bump_h + 1]);
            }
            // Sleeve drops into the tray so a 3 mm bearing fits in a 16 mm case.
            translate([platter_cx, platter_cy, ledge_z])
                cylinder(d = bearing_d + 6.4, h = body_h - well_d - ledge_z, $fn = 48);
            // Button guides. The cap stops on the boss, so the face of the
            // deck never takes the blow. Hidden under the cap.
            for (p = [[play_x, play_y], [cue_x, cue_y], [in_x, in_y], [out_x, out_y], [rev_x, rev_y]])
                translate([p[0], p[1], body_h])
                    cylinder(d = guide_d, h = guide_h, $fn = 40);
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

        // crossfader slot. Narrow: only the 1.2 mm lever comes through.
        translate([fader_cx, 0, tray_h - eps])
            y_round_slot(fader_slot_w, fader_y0, fader_y0 + fader_slot_l, 0, skin + 2 * eps);

        // M2 into the pot's own threads. 67 mm apart, just past the ends of
        // the slot, so the screws don't break into the opening.
        for (s = [-1, 1])
            translate([fader_cx, fader_y0 + fader_slot_l / 2 + s * pot_screw_span / 2, tray_h - eps])
                cylinder(d = 2.2, h = skin + 2 * eps, $fn = 18);

        // Guided stem hole, through the boss and the skin, with a lead-in.
        for (p = [[play_x, play_y], [cue_x, cue_y], [in_x, in_y], [out_x, out_y], [rev_x, rev_y]]) {
            translate([p[0], p[1], tray_h - eps])
                cylinder(d = stem_hole, h = skin + guide_h + 2 * eps, $fn = 28);
            translate([p[0], p[1], body_h + guide_h - 0.45])
                cylinder(d1 = stem_hole, d2 = stem_hole + 1.6, h = 0.7, $fn = 28);
        }
        for (p = [[in_x + 11, in_y], [out_x + 11, out_y]])
            translate([p[0], p[1], tray_h - eps])
                cylinder(d = led_d, h = skin + 2 * eps, $fn = 20);

        // Magnet pockets in the underside of the lid, matching the tray.
        for (p = mag_pockets)
            translate([p[0], p[1], tray_h - eps])
                cube([mag_pocket_l, mag_pocket_w, mag_pocket_d + eps]);

        // pot / knob bushing, through the cap
        translate([knob_x, knob_y, tray_h - eps])
            cylinder(d = 7.3, h = bump_h - tray_h + 2 * eps, $fn = 36);

        // Cells. Open downward only. The roof is the top shell.
        translate([holder_x0, holder_y0, tray_h - eps])
            cube([holder_l, holder_w, 6.2]);

        // OLED. Glass through the roof, board pocket just under it so the
        // 40 x 12.85 module stops on the lip and cannot fall through.
        translate([plate_x0 + oled_cx - 20.3, plate_y0 + oled_cy - 6.7, tray_h + 6])
            cube([40.6, 13.4, 1.8]);
        translate([plate_x0 + oled_cx - oled_w / 2, plate_y0 + oled_cy - oled_h / 2, tray_h + 6])
            cube([oled_w, oled_h, bump_h]);
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

// Cap pushes onto the pot's metal tang. No stem, no magnet lock.
// Print the flat face on the bed. The slot opens toward the deck.
module fader() {
    difference() {
        hull() {
            translate([-cap_w / 2, -cap_d / 2, 0])
                cube([cap_w, cap_d, cap_h - 1.4]);
            translate([-cap_w / 2 + 1.3, -cap_d / 2 + 1.3, cap_h - 1.4])
                cube([cap_w - 2.6, cap_d - 2.6, 1.4]);
        }
        translate([-tang_slot_w / 2, -tang_slot_l / 2, cap_h - tang_slot_d])
            cube([tang_slot_w, tang_slot_l, tang_slot_d + eps]);
    }
}

// Old magnet lock. Not used with the slide pot. Kept so the export still builds.
stem_len = skin + 3.2;
lock_w = 16;
lock_d = 20;
lock_t = 5.4;
socket_d = 2.0;
fader_stem_w = 7.2;
fader_stem_l = 13.0;
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

module button_stem(h) {
    translate([0, 0, h - 0.8])
        cylinder(d = stem_d, h = 0.8 + neck_from_face, $fn = 24);
    translate([0, 0, h + neck_from_face])
        cylinder(d = neck_d, h = neck_l, $fn = 20);
    translate([0, 0, h + neck_from_face + neck_l])
        cylinder(d = stem_d - 0.2, h = tip_l, $fn = 20);
}

module round_cap(d, h) {
    hull() {
        cylinder(d = d, h = 1.2, $fn = 64);
        translate([0, 0, h - 1.4]) cylinder(d = d - 1.4, h = 1.4, $fn = 64);
    }
    button_stem(h);
}

// Slides onto the neck from the side. The throat is narrower than the neck,
// so it snaps and stays. Print it flat.
module flange() {
    difference() {
        cylinder(d = flange_d, h = flange_t, $fn = 36);
        translate([0, 0, -eps])
            cylinder(d = neck_d + 0.4, h = flange_t + 2 * eps, $fn = 18);
        translate([-1.0, -flange_d / 2, -eps])
            cube([2.0, flange_d / 2, flange_t + 2 * eps]);
    }
}

module button_nest(open_x) {
    outer = tact_body + 2 * nest_wall;
    translate([-outer / 2, -outer / 2, floor_t])
        cube([outer, outer, nest_pad]);
    difference() {
        translate([-outer / 2, -outer / 2, floor_t + nest_pad])
            cube([outer, outer, nest_rim]);
        translate([-tact_body / 2, -tact_body / 2, floor_t + nest_pad - eps])
            cube([tact_body, tact_body, nest_rim + 2 * eps]);
        // Pin gap, toward the middle of the deck.
        translate([open_x > 0 ? tact_body / 2 - 0.4 : -outer / 2 - 0.2,
                   -1.5,
                   floor_t + nest_pad - eps])
            cube([nest_wall + 0.8, 3.0, nest_rim + 2 * eps]);
    }
}

module button_nests() {
    for (p = [[play_x, play_y, 1], [cue_x, cue_y, 1], [in_x, in_y, 1], [out_x, out_y, 1]])
        translate([p[0], p[1], 0])
            button_nest(p[2]);
    translate([rev_x, rev_y, 0])
        button_nest(-1);
}

module button() { round_cap(play_d, play_h); }
module loop_button() { round_cap(loop_d, loop_h); }

module cap_placed(x, y, d, h) {
    translate([x, y, body_h + guide_h + h])
        rotate([180, 0, 0])
            round_cap(d, h);
}

module orange_bar() {
    cube([23.6, 2.05, 0.55]);
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

module faceplate() {
    difference() {
        cube([plate_w, plate_h, plate_t]);
        // Pot bushing. Pull the knob off, then lift the plate.
        translate([knob_x - plate_x0, knob_y - plate_y0, -eps])
            cylinder(d = 13, h = plate_t + 2 * eps, $fn = 36);
        // OLED aperture. Sharp rectangle, no bezel. Yellow band faces the jack end.
        translate([oled_cx - oled_w / 2, oled_cy - oled_h / 2, -eps])
            cube([oled_w, oled_h, plate_t + 2 * eps]);
    }
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
    color([0.93, 0.40, 0.09])
        translate([knob_x, knob_y, bump_h])
            knob();
    color([0.12, 0.12, 0.13]) {
        cap_placed(play_x, play_y, play_d, play_h);
        cap_placed(cue_x, cue_y, play_d, play_h);
        cap_placed(in_x, in_y, loop_d, loop_h);
        cap_placed(out_x, out_y, loop_d, loop_h);
        cap_placed(rev_x, rev_y, loop_d, loop_h);
    }
    color([0.93, 0.40, 0.09])
        for (p = [[in_x + 11, in_y], [out_x + 11, out_y]])
            translate([p[0], p[1], body_h - 0.4])
                cylinder(d = 2.6, h = 1.1, $fn = 20);
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
} else if (part == "loop") {
    loop_button();
} else if (part == "flange") {
    flange();
} else if (part == "plus") {
    plus_mark();
} else if (part == "faceplate") {
    faceplate();
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
echo("FADER_TRAVEL", 60);
echo("ENCODER_PAD_TOP_Z", encoder_pad_top);
echo("HALL_POST_TOP_Z", hall_z_top);
echo("DEVKIT", devkit_x0, devkit_y0, devkit_w, devkit_l);
echo("USB_WINDOW_Z", usb_z0, usb_z0 + usb_h);
echo("TECHNIC", technic_ys, "z", technic_z, "d", technic_d);
