/****************************************************************************

NAME
   shmexport.c - shared-memory export from the daemon

DESCRIPTION
   This is a very lightweight alternative to JSON-over-sockets.  Clients
won't be able to filter by device, and won't get device activation/deactivation
notifications.  But both client and daemon will avoid all the marshalling and
unmarshalling overhead.

PERMISSIONS
   This file is Copyright (c) 2010 by the GPSD project
   BSD terms apply: see the file COPYING in the distribution root for details.

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
#include "libgps.h" /* for SHM_PSEUDO_FD */


bool shm_acquire(struct gps_context_t *context)
/* initialize the shared-memory segment to be used for export */
{
    long shmkey = getenv("GPSD_SHM_KEY") ? strtol(getenv("GPSD_SHM_KEY"), NULL, 0) : GPSD_SHM_KEY;

    int shmid = shmget((key_t)shmkey, sizeof(struct shmexport_t), (int)(IPC_CREAT|0666));
    if (shmid == -1) {
	gpsd_log(&context->errout, LOG_ERROR,
		 "shmget(0x%lx, %zd, 0666) for SHM export failed: %s\n",
		 shmkey,
		 sizeof(struct shmexport_t),
		 strerror(errno));
	return false;
    } else
	gpsd_log(&context->errout, LOG_PROG,
		 "shmget(0x%lx, %zd, 0666) for SHM export succeeded\n",
		 shmkey,
		 sizeof(struct shmexport_t));

    context->shmexport = (void *)shmat(shmid, 0, 0);
    if ((int)(long)context->shmexport == -1) {
	gpsd_log(&context->errout, LOG_ERROR,
		 "shmat failed: %s\n", strerror(errno));
	context->shmexport = NULL;
	return false;
    }
    context->shmid = shmid;

    gpsd_log(&context->errout, LOG_PROG,
	     "shmat() for SHM export succeeded, segment %d\n", shmid);
    return true;
}

void shm_release(struct gps_context_t *context)
/* release the shared-memory segment used for export */
{
    if (context->shmexport == NULL)
	return;

    /* Mark shmid to go away when no longer used
     * Having it linger forever is bad, and when the size enlarges
     * it can no longer be opened
     */
    if (shmctl(context->shmid, IPC_RMID, NULL) == -1) {
	gpsd_log(&context->errout, LOG_WARN,
		 "shmctl for IPC_RMID failed, errno = %d (%s)\n",
		 errno, strerror(errno));
    }
    (void)shmdt((const void *)context->shmexport);
}

void shm_update(struct gps_context_t *context, struct gps_data_t *gpsdata)
/* export an update to all listeners */
{
    if (context->shmexport != NULL)
    {
	static int tick;
	volatile struct shmexport_t *shared = (struct shmexport_t *)context->shmexport;

	++tick;
	/*
	 * Following block of instructions must not be reordered, otherwise
	 * havoc will ensue.
	 *
	 * This is a simple optimistic-concurrency technique.  We write
	 * the second bookend first, then the data, then the first bookend.
	 * Reader copies what it sees in normal order; that way, if we
	 * start to write the segment during the read, the second bookend will
	 * get clobbered first and the data can be detected as bad.
	 *
	 * Of course many architectures, like Intel, make no guarantees
	 * about the actual memory read or write order into RAM, so this
         * is partly wishful thinking.  Thus the need for the memory_barriers()
         * to enforce the required order.
	 */
	shared->bookend2 = tick;
	memory_barrier();
	shared->gpsdata = *gpsdata;
	memory_barrier();
#ifndef USE_QT
	shared->gpsdata.gps_fd = SHM_PSEUDO_FD;
#else
	shared->gpsdata.gps_fd = (void *)(intptr_t)SHM_PSEUDO_FD;
#endif /* USE_QT */
	memory_barrier();
	shared->bookend1 = tick;
    }
}


#endif /* SHM_EXPORT_ENABLE */

/* end */
