/* ntpshmmon.c -- monitor the inner end of an ntpshmwrite.connection
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd_config.h"
#include "ntpshm.h"
#include "revision.h"

#define NTPSEGMENTS	256	/* NTPx for x any byte */

static struct shmTime *segments[NTPSEGMENTS + 1];
static struct timespec tick[NTPSEGMENTS + 1];

static void shm_shutdown(void)
/* shut down all active segments */
{
    struct shmTime **pp;

    for (pp = segments; pp < segments + NTPSEGMENTS; pp++)
	if (*pp != NULL)
	    (void)shmdt((void *)(*pp));
}

int main(int argc, char **argv)
{
    int units = 0;
    int option;
    int	i;
    bool verbose = false;
    int timeout = INT_MAX, nsamples = INT_MAX;
    time_t starttime = time(NULL);

#define USAGE	"usage: ntpshmmon [-s] [-n max] [-t timeout] [-v] [-h] [-V]\n"
    while ((option = getopt(argc, argv, "hn:st:vV")) != -1) {
	switch (option) {
	case 'n':
	    nsamples = atoi(optarg);
	    break;
	case 's':
	    if (units > 0) {
		shm_shutdown();
		exit(EXIT_SUCCESS);
	    } else {
		fprintf(stderr, "ntpshmmon: zero units declared.\n");
		exit(EXIT_FAILURE);
	    }
	    //break;
	case 't':
	    timeout = atoi(optarg);
	    break;
	case 'v':
	    verbose = true;
	    break;
	case 'V':
	    (void)fprintf(stderr, "%s: version %s (revision %s)\n",
			  argv[0], VERSION, REVISION);
	    exit(EXIT_SUCCESS);
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
    (void)printf("ntpshmmon version 1\n");

    do {
	struct shm_stat_t	shm_stat;

	for (i = 0; i < NTPSEGMENTS; i++) {
	    enum segstat_t status = shm_query(segments[i], &shm_stat);
	    if (verbose)
		fprintf(stderr, "unit %d status %d\n", i, status);
	    switch(status)
	    {
	    case OK:
		/*@-mustfreefresh -formattype@*/
		/*@-type@*//* splint is confused about struct timespec */
		if (shm_stat.tvc.tv_sec != tick[i].tv_sec || shm_stat.tvc.tv_nsec != tick[i].tv_nsec) {
		    printf("sample %s %ld.%09ld %ld.%09ld %ld.%09ld %d %3d\n",
			   shm_name(i),
			   shm_stat.tvc.tv_sec, shm_stat.tvc.tv_nsec,
			   shm_stat.tvr.tv_sec, shm_stat.tvr.tv_nsec,
			   shm_stat.tvt.tv_sec, shm_stat.tvt.tv_nsec,
			   shm_stat.leap, shm_stat.precision);
		    tick[i] = shm_stat.tvc;
		    --nsamples;
		}
		/*@+type@*/
		/*@+mustfreefresh +formattype@*/
		break;
	    case NO_SEGMENT:
		break;
	    case NOT_READY:
		/* do nothing, data not ready, wait another cycle */
		break;
	    case BAD_MODE:
		/*@-mustfreefresh@*/
		fprintf(stderr, "ntpshmmon: unknown mode %d on segment %s\n",
			shm_stat.status, shm_name(i));
		/*@+mustfreefresh@*/
		break;
	    case CLASH:
		/* do nothing, data is corrupt, wait another cycle */
		break;
	    default:
		/*@-mustfreefresh@*/
		fprintf(stderr, "ntpshmmon: unknown status %d on segment %s\n",
			status, shm_name(i));
	/*@+mustfreefresh@*/
		break;
	    }
	}
 
	/*
	 * Even on a 1 Hz PPS, a sleep(1) may end up
         * being sleep(1.1) and missing a beat.  Since
	 * we're ignoring duplicates via timestamp, polling
	 * at interval < 1 sec shouldn't be a problem.
	 */
	(void)usleep(1000);
    } while 
	    (nsamples != 0 && time(NULL) - starttime < timeout);

    exit(EXIT_SUCCESS);
}

/* end */
