/* $Id$ */
/*
 * Copyright (C) 2005 Alfredo Pironti
 *
 * This software is distributed under a BSD-style license. See the
 * file "COPYING" for more information.
 *
 */
#include <cstdlib>

#include "gpsd_config.h"
#include "libgpsmm.h"

gpsmm::gpsmm() : gps_data(0) { gps_data = NULL; }

struct gps_data_t* gpsmm::open(void) {
	return open("127.0.0.1",DEFAULT_GPSD_PORT);
}

struct gps_data_t* gpsmm::open(const char *host, const char *port) {
	gps_data=gps_open(host,port);
	if (gps_data==NULL) { //connection not opened
		return NULL;
	}
	else { //connection succesfully opened
		to_user= new struct gps_data_t;
		return backup(); //we return the backup of our internal structure
	}
}

struct gps_data_t* gpsmm::stream(int flags) {
  if (gps_stream(gps_data,flags, NULL)==-1) {
		return NULL;
	}
	else {
		return backup();
	}
}

struct gps_data_t* gpsmm::send(const char *request) {
	if (gps_send(gps_data,request)==-1) {
		return NULL;
	}
	else {
		return backup();
	}
}

struct gps_data_t* gpsmm::poll(void) {
	if (gps_poll(gps_data)<0) {
		// we return null if there was a read() error or connection is cloed by gpsd
		return NULL;
	}
	else {
		return backup();
	}
}

void gpsmm::clear_fix(void) {
	gps_clear_fix(&(gps_data->fix));
}

void gpsmm::enable_debug(int level, FILE *fp) {
#ifdef CLIENTDEBUG_ENABLE
	gps_enable_debug(level, fp);
#endif /* CLIENTDEBUG_ENABLE */
}

gpsmm::~gpsmm() {
	if (gps_data!=NULL) {
		gps_close(gps_data);
		delete to_user;
	}
}
