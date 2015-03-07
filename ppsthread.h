/*
 * This file is Copyright (c) 2015 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#ifndef PPSTHREAD_H
#define PPSTHREAD_H

/* use RFC 2782 PPS API */
/* this needs linux >= 2.6.34 and
 * CONFIG_PPS=y
 * CONFIG_PPS_DEBUG=y  [optional to kernel log pulses]
 * CONFIG_PPS_CLIENT_LDISC=y
 */
#ifndef S_SPLINT_S
#if defined(HAVE_SYS_TIMEPPS_H)
// include unistd.h here as it is missing on older pps-tools releases.
// 'close' is not defined otherwise.
#include <unistd.h>
#include <sys/time.h>
#include <sys/timepps.h>
#endif
#endif /* S_SPLINT_S */

struct pps_thread_t {
    timestamp_t fixin_real;
    struct timespec fixin_clock; /* system clock time when last fix received */
#if defined(HAVE_SYS_TIMEPPS_H)
    pps_handle_t kernelpps_handle;
#endif /* defined(HAVE_SYS_TIMEPPS_H) */
    int chronyfd;			/* for talking to chrony */
    /*@null@*/ char *(*report_hook)(struct gps_device_t *,
				    struct timedelta_t *);
    /*@null@*/ void (*wrap_hook)(struct gps_device_t *);
    struct timedelta_t ppsout_last;
    int ppsout_count;
};

extern void pps_thread_activate(struct gps_device_t *);
extern void pps_thread_deactivate(struct gps_device_t *);
extern void pps_thread_stash_fixtime(struct gps_device_t *, 
			      timestamp_t, struct timespec);
extern int pps_thread_lastpps(struct gps_device_t *, struct timedelta_t *);

#endif /* PPSTHREAD_H */
