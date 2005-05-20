/*
 * Copyright (C) 2005 Alfredo Pironti
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/* This simple program shows the basic functionality of the C++ wrapper class */
#include <iostream>
#include <string>
#include <unistd.h>

#include "libgpsmm.h"

using namespace std;

static void callback(struct gps_data_t* p, char* buf);

int main(void) {
	gpsmm gps_rec;
	struct gps_data_t *resp;

	resp=gps_rec.open();
	if (resp==NULL) {
		cout << "Error opening gpsd\n";
		return (1);
	}

	cout << "Going to set the callback...\n";
	if (gps_rec.set_callback(callback)!=0 ) {
		cout << "Error setting callback.\n";
		return (1);
	}

	cout << "Callback setted, sleeping...\n";
	sleep(10);
	cout << "Exited from sleep...\n";
	
	if (gps_rec.del_callback()!=0) {
		cout << "Error deleting callback\n";
		return (1);
	}
	cout << "Sleeping again, to make sure the callback is disabled\n";
	sleep(4);

	cout << "Exiting\n";
	return 0;
}

static void callback(struct gps_data_t* p, char* buf) {
	
	if (p==NULL) {
		cout << "Error polling gpsd\n";
		return;
	}
	cout << "Online:\t" << p->online << "\n";
	cout << "Status:\t" << p->status << "\n";
	cout << "Mode:\t" << p->fix.mode << "\n";
	if (p->fix.mode>=MODE_2D) {
		if (p->set & LATLON_SET) {
			cout << "LatLon changed\n";
		}
		else {
			cout << "LatLon unchanged\n";
		}
		cout << "Longitude:\t" << p->fix.longitude <<"\n";
		cout << "Latitude:\t" << p->fix.latitude <<"\n";
	}
}
