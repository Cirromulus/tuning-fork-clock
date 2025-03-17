fork_abstand = 17;
fork_l = 123;
fork_arm_d = 5;
fork_hals_d = 5.5+.25;
fork_hals_l = 40;
fork_hals_sphere = 9;
magnet = [12+.3,4.6+.3];
spule = [10.2, 9.5, 14];

screw_d = 3;
screw_head = [5.5, 0, 3];
ws = 1.4;
slot_h = 1;
mounting_height = magnet.x/2 + ws;

show = "all";	//all, base, top, bottom

module fork() {
	round_part_h = ((fork_abstand-fork_hals_d)/2);
	translate([0,0,fork_hals_sphere/2]) {
		sphere(d = fork_hals_sphere);
		cylinder(d = fork_hals_d, h = fork_hals_l-fork_hals_sphere/2, $fn = $preview ? 30 : 80);
	}
	translate([0,0,fork_hals_l+round_part_h]) rotate([-90, 0, 0]) rotate_extrude(angle = 180) {
		translate([round_part_h, 0])circle(d = fork_hals_d);
	}
	for(x = [-round_part_h, round_part_h]) {
		translate([x, 0, fork_hals_l+round_part_h]) {
			cylinder(d = fork_arm_d, h = fork_l - fork_hals_l - round_part_h);
		}
	}
}

module translated_fork(){
translate([0, 0 , mounting_height]) rotate([90, 0, 90])
	fork();
}

module magnet(){
	$fn = $preview ? 30 : 80;
	color("grey")cylinder(d = magnet.x, h = magnet.y);
}

module translated_magnet(){
	translate([fork_l+magnet.x,-magnet.y/2, mounting_height]) rotate([0, 90, 90]) magnet();
}

module spule() {
	$fn = $preview ? 30 : 80;
	color("#809080") {
		cylinder(d = spule.y, h = spule.z);
		for(x = [0,2*spule.z/3]) translate([0, 0, x])
			cylinder(d = spule.x, h = spule.z/3);
	}
	for(x = [-spule.x/4, spule.x/4]) translate([x, 0, spule.z])
		cylinder(d = spule.y/6, h = 10);
}

module spule_array() {
	for(side = [1, -1]) {
		translate([fork_l-spule.x/2, side*(fork_abstand/2+ws), mounting_height])rotate([side*-90, 0, 0])spule();
	}
}

module screw(bite = true, l = 10, headsunk = false) {
	$fn = $preview ? 10 : 30;
	d = bite ? screw_d - .75 : screw_d;
	translate([0,0,-(l-.01)])cylinder(d = d, h = l);
	cylinder(d = screw_head.x, h = screw_head.z + (headsunk ? l : 0));
	
}

%spule_array();
%translated_magnet();
%translated_fork();

holder_w = fork_hals_d * 4;
holder_l = (fork_hals_l-fork_hals_sphere)/2;

module main_holder_screws(bite = true) {
	for(x = [holder_l/5, 4*holder_l/5]) {
		for(y = [-((holder_w - screw_head.x)/2-ws), ((holder_w - screw_head.x)/2-ws)]) {
			translate([(fork_hals_l-holder_l)/2+x, y, mounting_height+2*ws])
				screw(bite = bite);
		}
	}
}

// main holder
if(show == "all" || show == "base") difference() {
	translate([(fork_hals_l-holder_l)/2, -holder_w/2, 0])
		cube([holder_l, holder_w, mounting_height-slot_h/2]);
	translated_fork();
	main_holder_screws();
}

// deckel main holder
if(show == "all" || show == "top") difference() {
	hull() {
		intersection() {
			scale([1, 1.1, 1.1]) translated_fork();
			translate([(fork_hals_l-holder_l)/2, -holder_w/2, mounting_height+slot_h/2]) {
				cube([holder_l, holder_w, 2*mounting_height]);
			}
		}
		translate([(fork_hals_l-holder_l)/2, -holder_w/2, mounting_height+slot_h/2])
			cube([holder_l, holder_w, ws]);
	}
	translated_fork();
	// screws
	main_holder_screws(bite = false);
}

// spule und magnet
sp_holder_l = spule.x + 2*ws;
sp_holder_w_clearance = 1;
sp_holder_w = spule.z;
mg_holder_l = magnet.x+2*ws;
start_holder_outside_y = (fork_abstand/2+ws) + sp_holder_w_clearance;

module spmg_holder() {
	for(side = [1,0]) {
		mirror([0,side, 0]) translate([fork_l,0,0]){
			// spule
			translate([-spule.x/2-sp_holder_l/2,start_holder_outside_y ,0]) {
				cube([sp_holder_l,sp_holder_w - sp_holder_w_clearance,mounting_height-slot_h/2]);
			}
			hull() {
				// short spule
				translate([0,start_holder_outside_y ,0]) {
					cube([ws,sp_holder_w - sp_holder_w_clearance,mounting_height-slot_h/2]);
				}
				//magnet
				translate([magnet.x-mg_holder_l/2,0, 0]) {
					cube([mg_holder_l,magnet.y/2+ws,mounting_height-slot_h/2]);
				}
			}
		}
	}
}

module spmg_screws(bite = true) {
	for(side = [1,0])
		mirror([0,side, 0]) 
			translate([fork_l+(magnet.x/2), fork_abstand/2, mounting_height+3*ws])
				screw(bite = bite, headsunk = !bite);
}

// spulemagnet holder
if(show == "all" || show == "base") difference() {
	spmg_holder();
	spule_array();
	translated_magnet();
	spmg_screws();
}

// spulemagnet deckel
if(show == "all" || show == "bottom") difference() {
	translate([0,0,mounting_height+slot_h/2])
		spmg_holder();
	spule_array();
	translated_magnet();
	spmg_screws(bite = false);
}

// connector arms
conn_h = 2*ws;
if(show == "all" || show == "base") for(side = [0, 1]) {
	mirror([0,side, 0]) difference() {
		hull() {
			// short spule
			translate([fork_l-sp_holder_l+ws,start_holder_outside_y ,0]) {
				cube([1,sp_holder_w - sp_holder_w_clearance,conn_h]);
			}
			translate([holder_l,0,0])
				cube([1, holder_w/2, conn_h]);
		}
		translate([fork_l-sp_holder_l-screw_head.x, (fork_abstand+sp_holder_w)/2, conn_h])
			screw(bite = false);
		magic_offs = [1.5, .5];	// To make it align to the raster of my platine
		translate([fork_hals_l-screw_head.x+magic_offs.x, fork_abstand/2+magic_offs.y, conn_h])
			screw(bite = false);
		main_holder_screws();
	}
}
