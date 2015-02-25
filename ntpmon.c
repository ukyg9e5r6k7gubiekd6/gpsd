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

static int shm_startup(int count)
/* open a specified number of segments */
{
    int	i;

    for (i = 0; i < count; i++) {
	segments[i] = shm_get(i, true);
	if (segments[i] == NULL)
	    return i;
    }

    return count;
}

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
    int opened = 0;
    int option;

#define USAGE	"usage: ntpmon [-n units] [-s]\n"
    while ((option = getopt(argc, argv, "hn:s")) != -1) {
	switch (option) {
	case 'n':
	    units = atoi(optarg);
	    opened = shm_startup(units);
	    if (opened < units){
		fprintf(stderr, "ntpmon: open of unit %d failed.\n", opened);
		exit(EXIT_FAILURE);
	    }
	    break;
	case 's':
	    if (units > 0) 
		shm_shutdown();
	    else {
		fprintf(stderr, "ntpmon: zero units declared.\n");
		exit(EXIT_FAILURE);
	    }
	    break;
	case 'h':
	default:
	    fprintf(stderr, USAGE);
	    break;
	}
    }

    (void)printf("ntpmon version 1\n");

    for (;;) {
	struct shm_stat_t	shm_stat;
	int i;

	printf("%%\n");
	for (i = 0; i < units; i++) {
	    enum segstat_t status = shm_query(segments[i], &shm_stat);
	    switch(status)
	    {
	    case OK:
		printf("%s %ld %ld %ld %ld %d\n",
		       shm_name(i),
		       shm_stat.tvr.tv_sec, shm_stat.tvr.tv_nsec,
		       shm_stat.tvt.tv_sec, shm_stat.tvt.tv_nsec,
		    shm_stat.leap);
		break;
	    default:
		fprintf(stderr, "ntpmon: unknown status %d on segment %s\n",
			status, shm_name(i));
		break;
	    }
	}
	sleep(1);
    }

    exit(0);
}

/* end */
