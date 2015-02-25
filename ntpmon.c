/* ntpmon.c -- monitor the inner end of an ntpshm connection
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "ntpshm.h"

#define NTPSEGMENTS	256	/* NTPx for x any byte */

static struct shmTime *segments[NTPSEGMENTS + 1];
static struct timespec tick[NTPSEGMENTS + 1];

static void shm_shutdown(void)
/* shut down all active segments */
{
    struct shmTime **pp;

    for (pp = segments; *pp; pp++)
	(void)shmdt((void *)(*pp));
}

int main(int argc, char **argv)
{
    int units = 0;
    int option;
    int	i;
    bool verbose = false;

#define USAGE	"usage: ntpmon [-s] [-v] [-h]\n"
    while ((option = getopt(argc, argv, "hsv")) != -1) {
	switch (option) {
	case 's':
	    if (units > 0) {
		shm_shutdown();
		exit(EXIT_SUCCESS);
	    } else {
		fprintf(stderr, "ntpmon: zero units declared.\n");
		exit(EXIT_FAILURE);
	    }
	    break;
	case 'v':
	    verbose = true;
	    break;
	case 'h':
	default:
	    fprintf(stderr, USAGE);
	    break;
	}
    }

    /* grab all segments, keep the non-null ones */
    for (i = 0; i < NTPSEGMENTS; i++) {
	segments[i] = shm_get(i, false, true);
	if (verbose && segments[i] != NULL)
	    fprintf(stderr, "unit %d opened\n", i);
    }
    if (verbose)

    (void)printf("ntpmon version 1\n%%\n");

    for (;;) {
	struct shm_stat_t	shm_stat;
	int i;

	for (i = 0; i < NTPSEGMENTS; i++) {
	    enum segstat_t status = shm_query(segments[i], &shm_stat);
	    if (verbose)
		fprintf(stderr, "unit %d status %d\n", i, status);
	    switch(status)
	    {
	    case OK:
		if (shm_stat.tvc.tv_sec != tick[i].tv_sec || shm_stat.tvc.tv_nsec != tick[i].tv_nsec) {
		    printf("sample %s %ld %ld %ld %ld %ld %ld %d %d\n",
			   shm_name(i),
			   shm_stat.tvc.tv_sec, shm_stat.tvc.tv_nsec,
			   shm_stat.tvr.tv_sec, shm_stat.tvr.tv_nsec,
			   shm_stat.tvt.tv_sec, shm_stat.tvt.tv_nsec,
			   shm_stat.leap, shm_stat.precision);
		    tick[i] = shm_stat.tvc;
		}
		break;
	    case NO_SEGMENT:
		break;
	    case NOT_READY:
		/* do nothing, data not ready, wait another cycle */
		break;
	    case BAD_MODE:
		fprintf(stderr, "ntpmon: unknown mode %d on segment %s\n",
			shm_stat.mode, shm_name(i));
		break;
	    case CLASH:
		/* do nothing, data is corrupt, wait another cycle */
		break;
	    default:
		fprintf(stderr, "ntpmon: unknown status %d on segment %s\n",
			status, shm_name(i));
		break;
	    }
	}
 
	/*
	 * Even on a 1 Hz PPS, a sleep(1) may end up
         * being sleep(1.1) and missing a beat.  Since
	 * we're ignoring duplicates via timestamp, polling
	 * at interval < 1 sec shouldn't be a problem.
	 */
	usleep(1000);
    }

    exit(EXIT_SUCCESS);
}

/* end */
