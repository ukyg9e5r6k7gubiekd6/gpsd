/****************************************************************************

NAME
   libgps_shm.c - reader access to shared-memory export

DESCRIPTION
   This is a very lightweight alternative to JSON-over-sockets.  Clients
won't be able to filter by device, and won't get device activation/deactivation
notifications.  But both client and daemon will avoid all the marshalling and
unmarshalling overhead.

PERMISSIONS
   This file is Copyright (c) 2010 by the GPSD project
   SPDX-License-Identifier: BSD-2-clause

***************************************************************************/

/* sys/ipc.h needs _XOPEN_SOURCE, 500 means X/Open 1995 */
#define _XOPEN_SOURCE 500

#include "gpsd_config.h"

#ifdef SHM_EXPORT_ENABLE

#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "gpsd.h"
#include "libgps.h"

struct privdata_t
{
    void *shmseg;
    int tick;
};


int gps_shm_open(struct gps_data_t *gpsdata)
/* open a shared-memory connection to the daemon */
{
    int shmid;

    long shmkey = getenv("GPSD_SHM_KEY") ? strtol(getenv("GPSD_SHM_KEY"), NULL, 0) : GPSD_SHM_KEY;

    libgps_debug_trace((DEBUG_CALLS, "gps_shm_open()\n"));

    gpsdata->privdata = NULL;
    shmid = shmget((key_t)shmkey, sizeof(struct gps_data_t), 0);
    if (shmid == -1) {
	/* daemon isn't running or failed to create shared segment */
	return -1;
    }
    gpsdata->privdata = (void *)malloc(sizeof(struct privdata_t));
    if (gpsdata->privdata == NULL)
	return -1;

    PRIVATE(gpsdata)->shmseg = shmat(shmid, 0, 0);
    if (PRIVATE(gpsdata)->shmseg == (void *) -1) {
	/* attach failed for sume unknown reason */
	free(gpsdata->privdata);
	gpsdata->privdata = NULL;
	return -2;
    }
#ifndef USE_QT
    gpsdata->gps_fd = SHM_PSEUDO_FD;
#else
    gpsdata->gps_fd = (void *)(intptr_t)SHM_PSEUDO_FD;
#endif /* USE_QT */
    return 0;
}

bool gps_shm_waiting(const struct gps_data_t *gpsdata, int timeout)
/* check to see if new data has been written */
/* timeout is in uSec */
{
    volatile struct shmexport_t *shared = (struct shmexport_t *)PRIVATE(gpsdata)->shmseg;
    volatile bool newdata = false;
    timestamp_t endtime = timestamp() + (((double)timeout)/1000000);

    /* busy-waiting sucks, but there's not really an alternative */
    for (;;) {
	volatile int bookend1, bookend2;
	memory_barrier();
	bookend1 = shared->bookend1;
	memory_barrier();
	bookend2 = shared->bookend2;
	memory_barrier();
	if (bookend1 == bookend2 && bookend1 > PRIVATE(gpsdata)->tick)
	    newdata = true;
	if (newdata || (timestamp() >= endtime))
	    break;
    }

    return newdata;
}

int gps_shm_read(struct gps_data_t *gpsdata)
/* read an update from the shared-memory segment */
{
    if (gpsdata->privdata == NULL)
	return -1;
    else
    {
	int before, after;
	void *private_save = gpsdata->privdata;
	volatile struct shmexport_t *shared = (struct shmexport_t *)PRIVATE(gpsdata)->shmseg;
	struct gps_data_t noclobber;

	/*
	 * Following block of instructions must not be reordered,
	 * otherwise havoc will ensue.  The memory_barrier() call
	 * should prevent reordering of the data accesses.
	 *
	 * This is a simple optimistic-concurrency technique.  We wrote
	 * the second bookend first, then the data, then the first bookend.
	 * Reader copies what it sees in normal order; that way, if we
	 * start to write the segment during the read, the second bookend will
	 * get clobbered first and the data can be detected as bad.
	 */
	before = shared->bookend1;
	memory_barrier();
	(void)memcpy((void *)&noclobber,
		     (void *)&shared->gpsdata,
		     sizeof(struct gps_data_t));
	memory_barrier();
	after = shared->bookend2;

	if (before != after)
	    return 0;
	else {
	    (void)memcpy((void *)gpsdata,
			 (void *)&noclobber,
			 sizeof(struct gps_data_t));
	    gpsdata->privdata = private_save;
#ifndef USE_QT
	    gpsdata->gps_fd = SHM_PSEUDO_FD;
#else
	    gpsdata->gps_fd = (void *)(intptr_t)SHM_PSEUDO_FD;
#endif /* USE_QT */
	    PRIVATE(gpsdata)->tick = after;
	    if ((gpsdata->set & REPORT_IS)!=0) {
		if (gpsdata->fix.mode >= 2)
		    gpsdata->status = STATUS_FIX;
		else
		    gpsdata->status = STATUS_NO_FIX;
		gpsdata->set = STATUS_SET;
	    }
	    return (int)sizeof(struct gps_data_t);
	}
    }
}

void gps_shm_close(struct gps_data_t *gpsdata)
{
    if (PRIVATE(gpsdata) && PRIVATE(gpsdata)->shmseg != NULL)
	(void)shmdt((const void *)PRIVATE(gpsdata)->shmseg);
}

int gps_shm_mainloop(struct gps_data_t *gpsdata, int timeout,
			 void (*hook)(struct gps_data_t *gpsdata))
/* run a shm main loop with a specified handler */
{
    for (;;) {
	if (!gps_shm_waiting(gpsdata, timeout)) {
	    return -1;
	} else {
	    int status = gps_shm_read(gpsdata);

	    if (status == -1)
		return -1;
	    if (status > 0)
		(*hook)(gpsdata);
	}
    }
    //return 0;
}

#endif /* SHM_EXPORT_ENABLE */

/* end */
