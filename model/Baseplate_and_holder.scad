plate_dim = [90, 150, 2];

base_diam = 115; // originally: 125;
base_height = 14;

ring_depth = 4;
glass_inner_d = 95.5;
glass_inner_d_l = 2.5;
ring_breite = glass_inner_d_l + 1;
ring_d = glass_inner_d - (ring_breite / 2);

unten_abstand = 5;
slot_thick_x = 6;

cable_hole_d = 7;

show = "normal";
//show_gummi = show == "gummi" || show == "beides";
show_normal = show == "normal" || show == "beides";

fitting_test = true;

module plate()
{
	cube(plate_dim);
	translate([plate_dim.x / 2, plate_dim.y,  plate_dim.z])
		rotate([0, 0, -90])
			import("Tuningforkholder_all.stl");
	// block im Weg
	translate([82, 60, 0])
		rotate([0, 180, 0])
			cube([23, 26, 20]);
};


module base()
{
	diam = base_diam;
	height = base_height;

	knickpunkt_y = (ring_d / 2)  - ring_breite;

	difference()
	{
		translate([0, 0, height / 2]) intersection()
		{
			sphere(d = diam, $fn = $preview ? 100 : 300);
			cube([diam + 2, diam + 2, height], center = true);
		}
		
		// groove ring
		translate([0, 0, height - ring_depth])
			rotate_extrude($fn = $preview ? 100 : 300)
				translate([ring_d / 2, 0])
					square([ring_breite, ring_depth + 1]);

		// Cable holes
		translate([0, 0, (base_height - cable_hole_d / 2) / 2])
		{
			// innen
			translate([0, knickpunkt_y, 0])
				rotate([45, 0, 0])
					cylinder(d = cable_hole_d, h = ring_d, $fn = 200);
			
			// kugel
			translate([0, knickpunkt_y, 0])
				sphere(d = cable_hole_d, $fn = 200);
			//außen
			translate([0, knickpunkt_y , 0])
				rotate([-90, 0, 0])
					cylinder(d = cable_hole_d, h = ring_d, $fn = 200);
		}
	}
	
	// cable holder tower
	translate([0, knickpunkt_y  - (2 * cable_hole_d + 2), 0])
		difference()
		{
			cylinder(d = cable_hole_d, h = plate_dim.y, $fn = $preview ? 100 : 300);
			translate([0 ,cable_hole_d, 0])
				cylinder(d = 2 * cable_hole_d, h = plate_dim.y + 1, $fn = $preview ? 100 : 300);
		}
}


module slot(only_contact_slot = false)
{
	d_unten = ring_d; // - extra_stumpf;
	d_oben = ring_d / 8;
	oben_ende = unten_abstand + plate_dim.y;
	start_slim_at = oben_ende * .5;
	slim_exists = false;

	andruck = .25;
	wegdruck = .4;	// i know this sounds stupid, sorry

	if (!only_contact_slot)
	{
		difference()
		{
			union()
			{
				intersection()
				{
					union()
					{
						hull() // the slotty base stability thingimabob
						{
							translate([- d_unten /2 + slot_thick_x, 0, -1])
								cylinder(d = d_unten , h = 1, $fn = 200);

							translate([- d_oben/2 + slot_thick_x, 0, start_slim_at])
								cylinder(d = d_oben, h = 1, $fn = 200);
						}
						
						// The upper part
						if (slim_exists) hull()
						{
							translate([- d_oben/2 + slot_thick_x, 0, start_slim_at])
								cylinder(d = d_oben, h = 1, $fn = 200);
							
							translate([- d_oben/2 + slot_thick_x, 0, oben_ende])
								cylinder(d = d_oben, h = 1, $fn = 200);
						}
					}
					
					union()
					{
						max_y = max(d_unten, d_oben);
						translate([0, -max_y/2, 0])
							cube([slot_thick_x +1, max_y, oben_ende]);
						
						translate([-unten_abstand, -max_y / 2, 0])
						{
							difference()
							{
								cube([unten_abstand, max_y, unten_abstand]);
								translate([0, 0, unten_abstand])
									rotate([-90, 0, 0])
										cylinder(d = 2 * unten_abstand, h = max_y, $fn = 150);
							}
						}
					}
				}
				//?
			}

			// the actual slot
			push_on_x = false;
			slot_x = slot_thick_x / 2 - .3;	// fixme
			end_z = slim_exists ? oben_ende : start_slim_at;
			stepsize = .5;
			wavelength = ((end_z - unten_abstand) + /*lol*/ 2* stepsize) / 10;
			extra_x_abstand = push_on_x ? .25 : slot_x; // damit es nicht ganz so stark dort angedrückt wird
			for (i = [unten_abstand : stepsize : end_z + stepsize])
			{
				wave_pos = (i - unten_abstand) % wavelength;
				normalized_wave_pos = 1 - wave_pos / wavelength;
				extra = (normalized_wave_pos * (andruck + wegdruck)) - andruck; 

				translate([-1, -(plate_dim.z + extra)/2, i])
					cube([slot_x + (push_on_x ? (extra + extra_x_abstand) : slot_x) + 1, plate_dim.z + extra, stepsize]);
			}
		}
	}
	else
	{
		// Die Zone aus gummi, die aus dem "beides" rausgeschnitten wird
		y_ratio = .4;
		x_length = 1.5 * slot_thick_x;
		inner_slot_d = 2 * andruck + slot_thick_x * 2;
		outer_slot_d = 2 * andruck + slot_thick_x * 3;
		stepsize = 1;
		$fn = $preview ? 35 : 75;
		ende_z = slim_exists ? oben_ende : start_slim_at;
		for (i = [unten_abstand : stepsize : ende_z + stepsize/2])
		{
			ratio_done = ((i - unten_abstand) / (ende_z - unten_abstand));
			current_slot = i % 2 == 0 ? inner_slot_d : outer_slot_d;
			width_diff_mixed = ((y_ratio * ratio_done) + (1 - ratio_done));
			slot_d = current_slot * width_diff_mixed;
			translate([-x_length /2, -slot_d/2, i])
				cube([x_length , slot_d , stepsize]);
		}
		
		// kleiner extra halter
		uebergang_h = .5;
		extra_halter = 0;
		translate([-x_length/2, -(outer_slot_d + extra_halter)/ 2, unten_abstand - uebergang_h])
			cube([x_length, outer_slot_d + extra_halter, uebergang_h] );
		// cut more away for a spring effect
		if (show_normal)
		{
			extra_platz_drunter = 3;
			#translate([slot_thick_x * .25 + 1, 0, unten_abstand - extra_platz_drunter])
				cylinder(d = slot_thick_x, h = extra_platz_drunter);
		}
	}
}


module slot_filament()
{
	if (show == "beides")
	{
		slot();
	}
	if (show == "normal")
	{
		difference()
		{
			slot();
			slot(true);
		}
	}
	if (show == "gummi")
	{
		intersection()
		{
			slot();
			slot(true);
		}
	}
}

module slot_trans()
{
	translate([ring_d/2 - slot_thick_x, 0, base_height])
		slot_filament();
}

module slots_trans()
{
	slot_trans();
	mirror([1, 0, 0])
		slot_trans();
}

if (!fitting_test)
{
	slots_trans();
	if (show_normal)
		base();
}
else
{
	intersection()
	{
		union()
		{
			slots_trans();
			if (show_normal)
				base();
		}
		h = 30;
		translate([0, 0, h/2 + base_height - (ring_depth + 1)])
			cube([base_diam+5, base_diam, h], center = true);
	}
}

%translate([-plate_dim.x / 2, plate_dim.z/2, base_height + unten_abstand ])
	rotate([90, 0, 0])
		color("grey") plate();

