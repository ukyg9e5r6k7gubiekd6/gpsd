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
#ifndef _GPSMM_H_
#define _GPSMM_H_

#include "gps.h" //the C library we are going to wrap

class gpsmm {
	public:
		gpsmm() { };
		virtual ~gpsmm();
		struct gps_data_t* open(const char *host,const char *port); //opens the connection with gpsd, MUST call this before any other method
		struct gps_data_t* open(void); //open() with default values
		struct gps_data_t* query(const char *request); //put a command to gpsd and return the updated struct
		struct gps_data_t* poll(void); //block until gpsd returns new data, then return the updated struct
		int set_callback(void (*hook)(struct gps_data_t *sentence, char *buf, size_t len, int level)); //set a callback funcition, called each time new data arrives
		int del_callback(void); //delete the callback function
		void clear_fix(void);

	private:
		struct gps_data_t *gps_data;
		struct gps_data_t *to_user;	//we return the user a copy of the internal structure. This way she can modify it without
						//integrity loss for the entire class
		struct gps_data_t* backup(void) { *to_user=*gps_data; return to_user;}; //return the backup copy
		pthread_t *handler; //needed to handle the callback registration/deletion
};
#endif // _GPSMM_H_
