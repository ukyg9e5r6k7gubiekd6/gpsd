/****************************************************************************

NAME
   shmexport.c - shared-memory exports from the daemon and how to read them

DESCRIPTION
   

PERMISSIONS
   This file is Copyright (c) 2010 by the GPSD project
   BSD terms apply: see the file COPYING in the distribution root for details.

***************************************************************************/
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "gpsd.h"

#ifdef SHM_EXPORT_ENABLE
bool shm_acquire(struct gps_context_t *context)
/* initialize the shared-memory segment to be used for export */
{
    int shmid;

    shmid = shmget((key_t)GPSD_KEY, sizeof(struct gps_data_t), (int)(IPC_CREAT|0666));
    if (shmid == -1) {
	gpsd_report(LOG_ERROR, "shmget(%ld, %zd, 0666) failed: %s\n",
		    (long int)GPSD_KEY, 
		    sizeof(struct gps_data_t),
		    strerror(errno));
	return false;
    } 
    context->shmexport = (char *)shmat(shmid, 0, 0);
    /*@ -mustfreefresh */
    if ((int)(long)context->shmexport == -1) {
	gpsd_report(LOG_ERROR, "shmat failed: %s\n", strerror(errno));
	context->shmexport = NULL;
	return false;
    }
    gpsd_report(LOG_PROG, "shmat() succeeded, segment %d\n", shmid);
    return true;
}

void shm_release(struct gps_context_t *context)
/* release the shared-memory segment used for export */
{
    if (context->shmexport != NULL)
	(void)shmdt((const void *)context->shmexport);
}

void shm_update(struct gps_context_t *context, struct gps_data_t *gpsdata)
/* export an update to all listeners */
{
    if (context->shmexport != NULL)
    {
	static int tick;

	++tick;
	/*
	 * Following block of instructions must not be reordered, otherwise 
	 * havoc will ensue.  asm volatile("sfence") is a GCCism intended
	 * to prevent reordering.
	 */
	((struct shmexport_t *)context)->bookend2 = tick;
	asm volatile("sfence");
	memcpy((void *)(context->shmexport + offsetof(struct shmexport_t, gpsdata)),
	       (void *)gpsdata,
	       sizeof(struct gps_data_t)); 
	asm volatile("sfence");
	((struct shmexport_t *)context)->bookend1 = tick;
    }
}
#endif /* SHM_EXPORT_ENABLE */

/* end */
