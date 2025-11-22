plate_dim = [90, 150, 2];

base_diam = 125;
base_height = 14;

ring_depth = 4;
glass_inner_d = 95;
glass_inner_d_l = 2.5;
ring_breite = glass_inner_d_l + 1;
ring_d = glass_inner_d - (ring_breite / 2);

unten_abstand = 5;
slot_thick = 3;

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
			knickpunkt_y = (ring_d / 2)  - ring_breite;
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
}


module slot(only_contact_slot = false)
{
	d_unten = ring_d; // - extra_stumpf;
	d_oben = slot_thick * 2;
	oben_ende = unten_abstand + plate_dim.y;
	start_slim_at = oben_ende * .5;

	
	andruck = .2;
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
							translate([- d_unten /2 + slot_thick, 0, -1])
								cylinder(d = d_unten , h = 1, $fn = 200);

							translate([- d_oben/2 + slot_thick, 0, start_slim_at])
								cylinder(d = d_oben, h = 1, $fn = 200);
						}
						
						/* // The upper part
						hull()
						{
							translate([- d_oben/2 + slot_thick, 0, start_slim_at])
								cylinder(d = d_oben, h = 1, $fn = 200);
							
							translate([- d_oben/2 + slot_thick, 0, oben_ende])
								cylinder(d = d_oben, h = 1, $fn = 200);
						}
						*/
					}
					
					union()
					{
						max_y = max(d_unten, d_oben);
						translate([0, -max_y/2, 0])
							cube([slot_thick +1, max_y, oben_ende]);
						
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
			extra_space = (ring_d - plate_dim.x) / 2;
			slot_depth = slot_thick - extra_space;
			wavelength = 3;
			stepsize = .25;
			color("green") for (i = [unten_abstand : stepsize : oben_ende + stepsize/2])
			{
				wave_pos = i % wavelength;
				normalized_wave_pos = 1 - wave_pos / wavelength;
				extra = (normalized_wave_pos * (andruck + wegdruck)) - andruck; 

				translate([0, -(plate_dim.z + extra)/2, i])
					cube([slot_depth + extra, plate_dim.z + extra, stepsize]);
			}
		}
	}
	else
	{
		inner_slot_d = 2 * andruck + slot_thick * 1.4;
		outer_slot_d = 2 * andruck + slot_thick * 1.7;
		stepsize = 1;
		$fn = $preview ? 35 : 75;
		for (i = [unten_abstand : stepsize : oben_ende + stepsize])
		{
			slot_d = (i % 2 == 0 ? inner_slot_d : outer_slot_d);
			translate([0, 0, i])
				cylinder(d = slot_d, h = stepsize);
		}
		
		// kleiner extra halter
		uebergang_h = .3;
		extra_halter = 2;
		translate([-outer_slot_d/2, -(outer_slot_d + extra_halter)/ 2, unten_abstand - uebergang_h])
			cube([outer_slot_d-.5, outer_slot_d + extra_halter, uebergang_h] );
		// cut more away for a spring effect
		if (show_normal)
		{
			extra_platz_drunter = 1;
			translate([0, 0, unten_abstand - extra_platz_drunter])
				cylinder(d = plate_dim.z * 1.75, h = extra_platz_drunter);
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
	translate([ring_d/2 - slot_thick, 0, base_height])
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

