// FIELD JOG, GO
// Handheld, with a platter that spins around a stationary GC9A01.
//
// Elegoo Mars 5 Ultra plate: 153.36 x 77.76 x 165 mm.
// The shell is 150 x 108 x 18. It does not lie flat. Stand it on the
// long edge: 150 along the plate, 18 across, 108 up. Supports stay
// in the cavity. The platter is 72 mm and does lie flat.
//
// The glass does not spin. The ring rides a hub, and a cream bezel
// pressed onto the hub keeps the ring from lifting off. A gear on the
// underside turns a pinion with a 6 mm diametric magnet. The MT6701
// sits under that pinion, clear of the LCD board.
//
//   openscad -D 'part="top"' -o go-top.stl go.scad
//   openscad -D 'part="ring"' -o go-ring.stl go.scad

/* [Export] */
part = "all"; // [all, top, bottom, ring, pinion, knob, cap, cue, play, util, bezel]

/* [Body] */
W = 150;
H = 108;
body_h = 18;
skin = 2.4;
wall = 1.8;
floor_t = 1.4;
R = 3.2;

/* [Platter] */
platter_d = 72;
platter_t = 2.6;
hub_d = 39.4;
hub_post_d = 36.2;
platter_id = 40.4;
glass_d = 32.4;
hole_d = 33.0;
pcb_d = 38.6;
cx = 78;
cy = 68;
gear_d = 52;
pinion_d = 13;
magnet_d = 6;
gear_xy = [cx + 33, cy];

/* [Fader] */
fader_cx = 78;
fader_y = 13;
fader_slot_w = 3.2;
fader_slot_l = 62;
pot_body_l = 75;
pot_body_w = 9.6;
pot_screw = 67;

/* [Controls] */
knob_xy = [132, 94];
cue_xy = [18, 22];
play_xy = [18, 8];
util_xs = [127, 141];
util_y = 10;
led_y = 19;

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

module gear_teeth(d, n, tooth_h, tooth_w) {
    for (i = [0 : n - 1])
        rotate([0, 0, i * 360 / n])
            translate([d / 2, 0, 0])
                cube([tooth_w, 1.4, tooth_h], center = true);
}

module top_plate() {
    difference() {
        union() {
            translate([0, 0, tray_h]) slab(skin);
            // Skirt into the tray. Overlaps the skin so it is one solid.
            translate([wall + 0.2, wall + 0.2, tray_h - 3.2])
                linear_extrude(3.6)
                    difference() {
                        rounded_rect(W - 2 * (wall + 0.2), H - 2 * (wall + 0.2), R - 1);
                        offset(r = -1.15)
                            rounded_rect(W - 2 * (wall + 0.2), H - 2 * (wall + 0.2), R - 1);
                    }
            // Stationary hub. The platter rides the wide part. The bezel
            // presses onto the post and traps the ring.
            translate([cx, cy, tray_h - 1.2])
                cylinder(d = hub_d, h = skin + 2.4, $fn = 80);
            translate([cx, cy, body_h + 0.2])
                cylinder(d = hub_post_d, h = 1.5, $fn = 64);
        }
        // Glass, through the hub. The board stays under it.
        translate([cx, cy, tray_h - 1])
            cylinder(d = hole_d, h = skin + 6, $fn = 80);
        translate([cx, cy, tray_h - 1.7])
            cylinder(d = pcb_d, h = 1.7, $fn = 72);
        // Pin tab, to the left, clear of the skirt and the pinion.
        translate([cx - 38, cy - 7, tray_h - 1.7])
            cube([16, 14, 1.7]);
        // Platter well. 0.35 mm of air under the ring.
        translate([cx, cy, body_h - 1.05])
            difference() {
                cylinder(d = platter_d + 1.3, h = 1.3, $fn = 120);
                translate([0, 0, -eps]) cylinder(d = hub_d - 0.2, h = 1.5, $fn = 80);
            }
        // Skirt channel around the hub, open into the cavity.
        translate([cx, cy, body_h - 1.4])
            difference() {
                cylinder(d = platter_id + 2.2, h = 1.3, $fn = 80);
                translate([0, 0, -eps]) cylinder(d = hub_d - 0.5, h = 1.5, $fn = 80);
            }
        // Gear recess, so the teeth clear the well floor.
        translate([cx, cy, body_h - 2.15])
            difference() {
                cylinder(d = gear_d + 6, h = 1.3, $fn = 80);
                translate([0, 0, -eps]) cylinder(d = hub_d + 1.6, h = 1.5, $fn = 80);
            }
        // Copper-tape seat, just inside the vinyl, 0.35 mm deep.
        translate([cx, cy, body_h - 1.4])
            difference() {
                cylinder(d = 68, h = 0.5, $fn = 90);
                translate([0, 0, -eps]) cylinder(d = 62, h = 0.7, $fn = 90);
            }
        // Pinion pocket. The gear hangs into this from the ring.
        translate([gear_xy[0], gear_xy[1], body_h - 5.2])
            cylinder(d = pinion_d + 3.4, h = 5.6, $fn = 36);
        // Inset face groove.
        translate([0, 0, body_h - 0.32])
            linear_extrude(0.45)
                difference() {
                    offset(r = -2.2) rounded_rect(W, H, R);
                    offset(r = -2.7) rounded_rect(W, H, R);
                }
        // Encoder.
        translate([knob_xy[0], knob_xy[1], tray_h - 1])
            cylinder(d = 7.2, h = skin + 3, $fn = 32);
        translate([knob_xy[0], knob_xy[1], tray_h - 1.3])
            cylinder(d = 10.2, h = 1.4, $fn = 32);
        // Fader slot and the two screws that pull the pot up.
        translate([fader_cx - fader_slot_l / 2, fader_y - fader_slot_w / 2, tray_h - 1])
            cube([fader_slot_l, fader_slot_w, skin + 3]);
        for (s = [-1, 1])
            translate([fader_cx + s * pot_screw / 2, fader_y, tray_h - 4.2])
                cylinder(d = 1.7, h = 4.4, $fn = 16);
        for (p = [cue_xy, play_xy]) {
            translate([p[0], p[1], body_h - 1.7])
                cylinder(d = 14.4, h = 2, $fn = 40);
            translate([p[0], p[1], tray_h - 1])
                cylinder(d = 6.6, h = skin + 3, $fn = 28);
            translate([p[0], p[1], tray_h - 5.6])
                cube([12.4, 12.4, 5.8], center = true);
        }
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
        translate([8, 99, body_h - 0.55]) label("FIELD JOG", 4.2);
        translate([8, 95.2, body_h - 0.45])
            cube([24, 0.7, 0.6]);
        translate([27, cue_xy[1], body_h - 0.5])
            linear_extrude(0.8)
                text("CUE", size = 2.6, font = "Liberation Sans",
                     halign = "left", valign = "center", spacing = 1.05);
        translate([27, play_xy[1], body_h - 0.5])
            linear_extrude(0.8)
                text("PLAY", size = 2.6, font = "Liberation Sans",
                     halign = "left", valign = "center", spacing = 1.05);
        translate([util_xs[0] - 3.2, 25, body_h - 0.5])
            linear_extrude(0.7)
                text("LOOP", size = 2.1, font = "Liberation Sans",
                     halign = "left", valign = "center");
        translate([util_xs[1] - 2.4, 25, body_h - 0.5])
            linear_extrude(0.7)
                text("REV", size = 2.1, font = "Liberation Sans",
                     halign = "left", valign = "center");
        for (p = [[6.5, 6.5], [W - 6.5, 6.5], [6.5, H - 6.5], [W - 6.5, H - 6.5]])
            translate([p[0], p[1], tray_h - 6.4])
                cylinder(d = 1.6, h = 6.2, $fn = 16);
        // USB-C, left end.
        translate([-1, 48, 7.2])
            cube([wall + 4, 11, 4.6]);
        // Headphone, right end.
        translate([W - wall - 2, 46, 9])
            rotate([0, 90, 0])
                cylinder(d = 6.3, h = wall + 4, $fn = 28);
    }
    for (p = [[6.5, 6.5], [W - 6.5, 6.5], [6.5, H - 6.5], [W - 6.5, H - 6.5]])
        translate([p[0], p[1], tray_h - 6.4])
            difference() {
                cylinder(d = 5.2, h = 7.4, $fn = 20);
                translate([0, 0, -eps]) cylinder(d = 1.7, h = 7.8, $fn = 16);
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
            for (s = [-1, 1])
                translate([fader_cx - pot_body_l / 2 - 0.4,
                           fader_y + s * (pot_body_w / 2 + 0.3),
                           floor_t])
                    cube([pot_body_l + 0.8, 1.3, 4.2]);
        }
        for (p = [[6.5, 6.5], [W - 6.5, 6.5], [6.5, H - 6.5], [W - 6.5, H - 6.5]]) {
            translate([p[0], p[1], -1])
                cylinder(d = 2.2, h = floor_t + 2, $fn = 16);
            translate([p[0], p[1], -eps])
                cylinder(d = 4.2, h = 1.1, $fn = 16);
        }
        translate([fader_cx - pot_body_l / 2, fader_y - 1.4, -eps])
            cube([pot_body_l, 2.8, 1.0]);
        // Keep-out for the MT6701 under the pinion.
        translate([gear_xy[0] - 10, gear_xy[1] - 8, floor_t])
            cube([20, 16, 3]);
        for (p = [[wall + 1, H / 2], [W - wall - 2.4, H / 2]])
            translate([p[0], p[1], tray_h - 1.2])
                cube([1.6, 3, 1.4]);
    }
}

module ring() {
    difference() {
        union() {
            cylinder(d = platter_d, h = platter_t, $fn = 140);
            // Skirt. Rides the hub so the ring cannot tilt.
            translate([0, 0, -1.15])
                difference() {
                    cylinder(d = platter_id + 1.6, h = 1.2, $fn = 80);
                    translate([0, 0, -eps])
                        cylinder(d = platter_id, h = 1.4, $fn = 80);
                }
            translate([0, 0, -1.7])
                difference() {
                    cylinder(d = gear_d + 1.2, h = 1.8, $fn = 72);
                    translate([0, 0, -eps])
                        cylinder(d = gear_d - 5, h = 2, $fn = 72);
                }
            translate([0, 0, -1.5])
                gear_teeth(gear_d, 36, 1.6, 1.3);
        }
        translate([0, 0, -2.6])
            cylinder(d = platter_id, h = 8, $fn = 80);
        // Inner step. The bezel lip sits here and does not rub the vinyl.
        translate([0, 0, platter_t - 1.15])
            difference() {
                cylinder(d = platter_id + 4.6, h = 1.4, $fn = 80);
                translate([0, 0, -eps])
                    cylinder(d = platter_id - 0.2, h = 1.6, $fn = 80);
            }
        // Grooves a fingertip can find.
        for (r = [26, 30, 34])
            translate([0, 0, platter_t - 0.3])
                difference() {
                    cylinder(d = r * 2 + 0.55, h = 0.45, $fn = 100);
                    translate([0, 0, -eps])
                        cylinder(d = r * 2 - 0.55, h = 0.6, $fn = 100);
                }
        // Index notch, for a dab of orange.
        translate([-0.7, platter_d / 2 - 3.2, platter_t - 0.55])
            cube([1.4, 2.4, 0.7]);
    }
}

module pinion() {
    difference() {
        union() {
            cylinder(d = pinion_d, h = 3.4, $fn = 36);
            translate([0, 0, 3.2])
                cylinder(d = magnet_d + 0.5, h = 1.15, $fn = 24);
            gear_teeth(pinion_d, 9, 3.2, 1.15);
        }
        translate([0, 0, 3.55])
            cylinder(d = magnet_d, h = 1.2, $fn = 24);
        translate([0, 0, -eps])
            cylinder(d = 2.1, h = 2.2, $fn = 16);
    }
}

module knob() {
    difference() {
        union() {
            cylinder(d = 16, h = 8.5, $fn = 48);
            translate([0, 0, 8.5]) cylinder(d = 12, h = 2.2, $fn = 40);
        }
        translate([0, 0, -eps]) cylinder(d = 6.05, h = 8, $fn = 24);
        translate([-0.4, 2.15, -eps]) cube([4, 2, 8]);
        for (i = [0 : 23])
            rotate([0, 0, i * 15])
                translate([7.7, 0, 0.6])
                    cube([1.1, 0.45, 7.2], center = true);
        translate([-0.4, 6.2, 8.6]) cube([0.8, 2.2, 2.4]);
    }
}

module fader_cap() {
    difference() {
        hull() {
            cube([16, 8, 1], center = true);
            translate([0, 0, 5]) cube([14, 6.4, 1], center = true);
        }
        translate([-0.75, -2.6, -eps]) cube([1.5, 5.2, 4.2]);
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
    // Press onto the hub post. The lip covers the platter step.
    difference() {
        union() {
            cylinder(d = 43.4, h = 0.7, $fn = 90);
            cylinder(d = hub_post_d + 0.9, h = 1.45, $fn = 64);
        }
        translate([0, 0, -eps])
            cylinder(d = hole_d + 0.3, h = 2, $fn = 72);
        translate([0, 0, -eps])
            cylinder(d = hub_post_d + 0.35, h = 1.15, $fn = 64);
    }
}

if (part == "all") {
    color([0.78, 0.77, 0.74]) top_plate();
    color([0.70, 0.69, 0.66]) bottom_tray();
    color([0.09, 0.09, 0.1]) translate([cx, cy, body_h - 0.65]) ring();
    color([0.22, 0.22, 0.24]) translate([gear_xy[0], gear_xy[1], body_h - 4.6]) pinion();
    color([1, 0.30, 0.11]) translate([knob_xy[0], knob_xy[1], body_h]) knob();
    color([1, 0.30, 0.11]) translate([fader_cx, fader_y, body_h + 0.2]) fader_cap();
    color([0.08, 0.08, 0.09]) translate([cue_xy[0], cue_xy[1], body_h - 1.5]) cue_cap_solid();
    color([0.08, 0.08, 0.09]) translate([play_xy[0], play_xy[1], body_h - 1.5]) play_cap_solid();
    for (x = util_xs)
        color([0.08, 0.08, 0.09])
            translate([x, util_y, body_h - 1.2]) util_cap_solid();
    color([0.95, 0.93, 0.88]) translate([cx, cy, body_h + 0.15]) bezel();
} else if (part == "top") top_plate();
else if (part == "bottom") bottom_tray();
else if (part == "ring") ring();
else if (part == "pinion") pinion();
else if (part == "knob") knob();
else if (part == "cap") fader_cap();
else if (part == "cue") cue_cap_solid();
else if (part == "play") play_cap_solid();
else if (part == "util") util_cap_solid();
else if (part == "bezel") bezel();

echo("PLATE", 153.36, 77.76, 165);
echo("BODY", W, H, body_h);
echo("STAND", W < 153.36 && body_h < 77.76 && H < 165);
echo("RING", platter_d, "fits flat", platter_d < 77.76);
