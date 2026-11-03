// EP-DJ3000 touch-test platter
//
// A bench coupon for the first conductive-PLA measurement.
// Not a deck shell: no DevKit bay, no fader, no jacks.
// Geometry follows hardware/PLATTER.md (76 mm disc, 8 sectors,
// sector 0 at 12 o'clock, clockwise from above = GPIO4..11).
//
//   openscad -D 'part="carrier"' -o carrier.stl platter-test.scad
//
// Print orientations (also in README.md):
//   carrier  pockets up, flat face on the bed
//   wedge    play face on the bed, boss pointing up
//   wedges   the same, eight of them, already spaced
//   stand    floor on the bed

/* [Export] */
part = "assembly"; // [assembly, section, carrier, wedge, wedges, stand, overlap, seated]

/* [Sectors] */
N = 8;
gap = 1.35;          // insulating rib between pockets, mm. Worst-case conductive gap.
clearance = 0.30;    // extra inset of each wedge so it drops in after elephant foot
r_in = 12;           // conductive inner radius
r_out = 36;          // conductive outer radius (2 mm rim remains)
r_rim = 38;          // 76 mm disc
wedge_t = 1.6;
floor_t = 1.4;
arc_n = 18;

/* [Contact boss] */
boss_r = 17;         // along the sector centreline
boss_d = 5.2;
boss_hole_d = 6.6;   // clearance through the carrier floor
boss_proud = 2.8;    // how far the boss sticks out under the carrier
pilot_d = 1.8;       // M2×3 self-tap, from the boss tip
pilot_depth = 3.6;
boss_chamfer = 0.45;

/* [Stand] */
stand_floor = 3.0;
ledge_h = 6.0;       // air under the carrier. Boss tips clear the floor by ~3 mm.
ledge_lip = 1.4;
wall_t = 7.2;
bore_clear = 0.40;   // radial. Diametral clearance is 0.8 mm.
ledge_inner = 26;
base_r = 54;
groove_w = 3.6;
groove_d = 1.6;
notch_w = 8.0;
notch_d = 1.3;
rib_w = 6.6;
rib_d = 0.85;

/* [Hidden] */
eps = 0.02;
$fn = 72;

boss_h = floor_t + boss_proud;
bore_r = r_rim + bore_clear;
carrier_h = floor_t + wedge_t;

assert(N >= 3 && N <= 8, "firmware touchSectors is 3..8");
assert(gap / 2 + clearance < r_in - 1, "gap eats the inner radius");
assert(boss_d / 2 + 0.8 < boss_r - (r_in + clearance), "boss leaves the wedge");
assert(boss_d + 0.8 < boss_hole_d, "boss will not pass the hole");
assert(pilot_d + 1.2 < boss_d, "pilot leaves no wall");
assert(pilot_depth + 1.4 < wedge_t + boss_h, "pilot breaks the play face");
assert(ledge_inner > boss_r + boss_hole_d / 2 + 1, "ledge hits a boss");
assert(notch_d > rib_d + 0.3, "key rib hits the notch");
assert(notch_w > rib_w + 0.6, "key rib is wider than the notch");

// --- sector polygon -------------------------------------------------------
// Constant-width gap: the radial edges are straight lines offset by g from
// the pie cuts. The arcs stay on r0 and r1, so the conductive OD stays put.

function wrap360(a) = a - 360 * floor(a / 360);

function ccw_delta(a_from, a_to) =
    let(d = wrap360(a_to) - wrap360(a_from))
    (d < 0 ? d + 360 : d);

function arc_ccw(R, a_from, a_to, n) =
    let(sweep = ccw_delta(a_from, a_to))
    [for (k = [0:n])
        let(a = a_from + sweep * k / n)
        [R * cos(a), R * sin(a)]];

function arc_cw(R, a_from, a_to, n) =
    let(sweep = ccw_delta(a_to, a_from))
    [for (k = [0:n])
        let(a = a_from - sweep * k / n)
        [R * cos(a), R * sin(a)]];

// Point on circle radius R, on the line parallel to ray `b` and inset by g.
// inward_is_clockwise is true for the CCW edge of the sector.
function pt_on_offset_circle(R, b, inward_is_clockwise, g) =
    let(
        s = inward_is_clockwise ? 1 : -1,
        nx = s * sin(b),
        ny = -s * cos(b),
        t = sqrt(R * R - g * g)
    )
    [t * cos(b) + g * nx, t * sin(b) + g * ny];

function sector_center(i) = 90 - i * 360 / N;

function wedge_pts(i, g, r0, r1, n) =
    let(
        ac = sector_center(i),
        half = 180 / N,
        b_lo = ac - half,
        b_hi = ac + half,
        p_out_lo = pt_on_offset_circle(r1, b_lo, false, g),
        p_out_hi = pt_on_offset_circle(r1, b_hi, true, g),
        p_in_lo = pt_on_offset_circle(r0, b_lo, false, g),
        p_in_hi = pt_on_offset_circle(r0, b_hi, true, g)
    )
    concat(
        arc_ccw(r1, atan2(p_out_lo.y, p_out_lo.x), atan2(p_out_hi.y, p_out_hi.x), n),
        arc_cw(r0, atan2(p_in_hi.y, p_in_hi.x), atan2(p_in_lo.y, p_in_lo.x), n)
    );

function boss_xy(i) = boss_r * [cos(sector_center(i)), sin(sector_center(i))];

// --- wedge ----------------------------------------------------------------
// Print orientation: play face at z = 0 (on the bed), boss toward +z.

module boss_cylinder() {
    difference() {
        cylinder(h = boss_h, d = boss_d);
        translate([0, 0, boss_h - boss_chamfer])
            difference() {
                translate([0, 0, -eps])
                    cylinder(h = boss_chamfer + 2 * eps, d = boss_d + 0.8);
                translate([0, 0, -2 * eps])
                    cylinder(h = boss_chamfer + 4 * eps, d1 = boss_d + 0.04, d2 = boss_d - 2 * boss_chamfer);
            }
    }
}

module wedge_raw() {
    p = boss_xy(0);
    difference() {
        union() {
            linear_extrude(wedge_t, convexity = 8)
                polygon(wedge_pts(0, gap / 2 + clearance, r_in + clearance, r_out - clearance, arc_n));
            translate([p.x, p.y, wedge_t - eps])
                boss_cylinder();
        }
        translate([p.x, p.y, wedge_t + boss_h - pilot_depth])
            cylinder(h = pilot_depth + eps, d = pilot_d);
    }
}

// Installed: play face flush with the carrier top, boss down through the floor.
// wedge_raw() lives on the +Y bisector. A 180° flip about X parks it on -Y
// with the boss pointing down; the Z rotation brings sector 0 back to +Y
// and walks the rest clockwise.
module wedge_installed(i) {
    translate([0, 0, carrier_h])
        rotate([0, 0, 180 - i * 360 / N])
            rotate([180, 0, 0])
                wedge_raw();
}

module wedges_plate() {
    // 4 x 2, play face down. The shift parks sector 0's min corner on the
    // origin (its bbox is about x -12.8..12.8, y 11.7..35.7). Pitch leaves
    // a gap so a slicer can split the eight bodies apart.
    pitch_x = 32;
    pitch_y = 30;
    for (i = [0:N - 1])
        translate([(i % 4) * pitch_x, floor(i / 4) * pitch_y, 0])
            translate([-12.8, -11.7, 0])
                wedge_raw();
}

// --- carrier --------------------------------------------------------------

module pointer() {
    // Shallow arrow on the hub, pointing at sector 0 (12 o'clock).
    translate([0, 0, carrier_h - 0.45])
        linear_extrude(0.45 + eps)
            polygon([[-1.3, 3.2], [1.3, 3.2], [0, 8.2]]);
}

module carrier() {
    difference() {
        cylinder(h = carrier_h, r = r_rim);
        for (i = [0:N - 1])
            translate([0, 0, floor_t])
                linear_extrude(wedge_t + eps, convexity = 8)
                    polygon(wedge_pts(i, gap / 2, r_in, r_out, arc_n));
        for (i = [0:N - 1]) {
            p = boss_xy(i);
            translate([p.x, p.y, -eps])
                cylinder(h = floor_t + 2 * eps, d = boss_hole_d);
        }
        // Clocking notch at 12 o'clock. The stand's rib seats in it.
        translate([-notch_w / 2, r_rim - notch_d, -eps])
            cube([notch_w, notch_d + 2, carrier_h + 2 * eps]);
        pointer();
    }
}

// --- stand ----------------------------------------------------------------

module groove(i) {
    a = sector_center(i);
    rotate([0, 0, a]) {
        translate([12, -groove_w / 2, stand_floor - groove_d])
            cube([base_r, groove_w, groove_d + eps]);
        // Slot through the ledge and the wall so the wire can leave.
        translate([ledge_inner - 0.4, -groove_w / 2, stand_floor - eps])
            cube([bore_r + wall_t - ledge_inner + 1, groove_w, 2.3]);
    }
}

module stand() {
    label_r = (bore_r + wall_t + base_r) / 2 - 0.4;
    difference() {
        union() {
            cylinder(h = stand_floor, r = base_r);
            translate([0, 0, stand_floor])
                difference() {
                    cylinder(h = ledge_h + ledge_lip, r = bore_r + wall_t);
                    translate([0, 0, -eps])
                        cylinder(h = ledge_h + ledge_lip + 2 * eps, r = bore_r);
                }
            translate([0, 0, stand_floor])
                difference() {
                    cylinder(h = ledge_h, r = bore_r);
                    translate([0, 0, -eps])
                        cylinder(h = ledge_h + 2 * eps, r = ledge_inner);
                }
            // Key rib, sector 0, sticking into the bore above the ledge.
            rotate([0, 0, sector_center(0)])
                translate([bore_r - rib_d, -rib_w / 2, stand_floor + ledge_h])
                    cube([rib_d, rib_w, ledge_lip + eps]);
            // Numbers read from the middle of the platter. Top of the glyph
            // points outward. A missing font falls back to OpenSCAD's default.
            for (i = [0:N - 1])
                rotate([0, 0, sector_center(i) - 10])
                    translate([label_r, 0, stand_floor - 0.2])
                        rotate([0, 0, -90])
                            linear_extrude(1.25)
                                text(str(i), size = 5.2, font = "Helvetica:style=Bold",
                                     halign = "center", valign = "center");
        }
        for (i = [0:N - 1]) groove(i);
        // Finger scoops between sectors, so the disc can be lifted out.
        for (a = [22, 202])
            rotate([0, 0, a])
                translate([bore_r - 0.6, -7, stand_floor + ledge_h - eps])
                    cube([wall_t + 3, 14, ledge_lip + 1]);
        // Zip-tie holes opposite the notch, clear of the sector-4 groove.
        for (s = [-1, 1])
            rotate([0, 0, sector_center(4) + s * 14])
                translate([base_r - 7, 0, -eps])
                    cylinder(h = stand_floor + 2 * eps, d = 4.2);
    }
}

// --- assembly -------------------------------------------------------------

module assembly() {
    color([0.22, 0.22, 0.24]) stand();
    color([0.62, 0.62, 0.64])
        translate([0, 0, stand_floor + ledge_h])
            carrier();
    color([0.08, 0.08, 0.09])
        translate([0, 0, stand_floor + ledge_h])
            for (i = [0:N - 1]) wedge_installed(i);
}

module section() {
    difference() {
        assembly();
        translate([-200, -200, -30]) cube([200, 400, 120]);
    }
}

// Fit checks. Each should be empty (or a zero-volume shared face).
// "seated" is the carrier dropped onto the stand.
module overlap() {
    intersection() {
        carrier();
        union() {
            for (i = [0:N - 1]) wedge_installed(i);
        }
    }
}

module seated() {
    intersection() {
        stand();
        translate([0, 0, stand_floor + ledge_h]) carrier();
    }
}

if (part == "assembly") assembly();
else if (part == "section") section();
else if (part == "carrier") carrier();
else if (part == "wedge") wedge_raw();
else if (part == "wedges") wedges_plate();
else if (part == "stand") stand();
else if (part == "overlap") overlap();
else if (part == "seated") seated();
else assert(false, str("unknown part ", part));
