/*
 * Copyright (C) 2005 Alfredo Pironti
 *
 * This software is distributed under a BSD-style license. See the
 * file "COPYING" in the top-level directory of the disribution for details.
 *
 */
#include <cstdlib>

#include "gpsd_config.h"
#include "libgpsmm.h"


gpsmm::gpsmm() : to_user(0)
{
}

struct gps_data_t* gpsmm::open(void) 
{
	return open("localhost",DEFAULT_GPSD_PORT);
}

struct gps_data_t* gpsmm::open(const char *host, const char *port) 
{
	const bool err = (gps_open(host, port, gps_data()) != 0);
	if ( err ) {
		return NULL;
	}
	else { // connection successfully opened
		to_user= new struct gps_data_t;
		return backup(); //we return the backup of our internal structure
	}
}

struct gps_data_t* gpsmm::stream(int flags) 
{
	if (gps_stream(gps_data(),flags, NULL)==-1) {
		return NULL;
	}
	else {
		return backup();
	}
}

struct gps_data_t* gpsmm::send(const char *request) 
{
	if (gps_send(gps_data(),request)==-1) {
		return NULL;
	}
	else {
		return backup();
	}
}

struct gps_data_t* gpsmm::read(void)
{
	if (gps_read(gps_data())<=0) {
		// we return null if there was a read() error, if no
		// data was ready in POLL_NOBLOCK mode, or if the
		// connection is closed by gpsd
		return NULL;
	}
	else {
		return backup();
	}
}

int gpsmm::close(void)
{
	return gps_close(gps_data());
}

bool gpsmm::waiting(void)
{
	return gps_waiting(gps_data());
}

void gpsmm::clear_fix(void)
{
	gps_clear_fix(&(gps_data()->fix));
}

void gpsmm::enable_debug(int level, FILE *fp)
{
#ifdef CLIENTDEBUG_ENABLE
	gps_enable_debug(level, fp);
#endif /* CLIENTDEBUG_ENABLE */
}

gpsmm::~gpsmm()
{
	if ( to_user != NULL ) {
		gps_close(gps_data());
		delete to_user;
	}
}
