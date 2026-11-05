// ECM sleeve
// A handheld shell in the spirit of a grey field mic with an orange
// button and a slot grille. It is not that product. It is a drop-in
// tube for a Sony ECM-77B head: 5.6 mm diameter, 12.5 mm long, on its
// own 2 mm cable. The XLR power barrel stays on the cable, outside.
//
// Feed the cable in from the tip until the capsule stops on the lip.
// One wrap of tape on the capsule if it rattles. The button is a cap.
// It does not switch anything unless you splice one in yourself.
//
//   openscad -D 'part="body"' -o body.stl ecm-handy.scad
//   openscad -D 'part="button"' -o button.stl ecm-handy.scad

/* [Export] */
part = "all"; // [all, body, button]

/* [Mic] */
// Published Handy Mic envelope, so it reads as the same object.
od = 18.8;
length = 96;

// ECM-77B head. Bore is 0.5 mm large. The lip is smaller than the head
// so it cannot fall out the tip. The cable tunnel is 2.6 mm.
cap_d = 5.6;
cap_l = 12.5;
bore = cap_d + 0.5;          // 6.1
lip_d = 4.6;
lip_t = 1.4;
cable_d = 2.6;
cable_offset = 0;            // on center. The stem stops short of the tunnel.

button_z = 34;               // from the tip
button_d = 8.2;
button_proud = 1.5;
stem_d = 1.7;
stem_hole = 2.0;
stem_l = 2.4;

eps = 0.04;

module slots() {
    for (i = [0:9])
        rotate([0, 0, i * 36])
            translate([-0.65, 4.2, 2.6])
                cube([1.3, 8, 6.2]);
}

module body() {
    difference() {
        union() {
            // Tip eases from a smaller nose up to the full diameter.
            cylinder(d1 = od - 2.4, d2 = od, h = 3.2, $fn = 80);
            translate([0, 0, 3.2])
                cylinder(d = od, h = length - 10, $fn = 80);
            // Strain relief. The cable leaves on center.
            translate([0, 0, length - 7])
                cylinder(d1 = od, d2 = 8.4, h = 7, $fn = 64);
        }

        // Front opening. Smaller than the capsule.
        translate([0, 0, -eps])
            cylinder(d = lip_d, h = lip_t + eps, $fn = 48);

        // Capsule chamber.
        translate([0, 0, lip_t])
            cylinder(d = bore, h = cap_l + 2, $fn = 48);

        // Cable tunnel, offset so it clears the button stem.
        translate([cable_offset, 0, lip_t + cap_l - 1])
            cylinder(d = cable_d, h = length, $fn = 28);

        slots();

        // Button stem hole, from the outside in. Stops short of the tunnel.
        translate([-od / 2 - eps, 0, button_z])
            rotate([0, 90, 0])
                cylinder(d = stem_hole, h = 3.2, $fn = 20);
    }
}

module button() {
    cylinder(d = button_d, h = button_proud, $fn = 48);
    translate([0, 0, button_proud - eps])
        cylinder(d = stem_d, h = stem_l, $fn = 20);
}

if (part == "all") {
    color([0.78, 0.78, 0.76]) body();
    color([0.93, 0.40, 0.09])
        translate([-od / 2 - button_proud, 0, button_z])
            rotate([0, 90, 0])
                button();
} else if (part == "body") {
    body();
} else if (part == "button") {
    button();
}

echo("OD", od, "LENGTH", length);
echo("BORE", bore, "LIP", lip_d, "CABLE", cable_d);
