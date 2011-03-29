/*
 * gpsdclient.h -- common functions for GPSD clients
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 */

#ifndef _GPSD_GPSDCLIENT_H_
#define _GPSD_GPSDCLIENT_H_
struct fixsource_t 
/* describe a data source */
{
    char *spec;		/* pointer to actual storage */
    char *server;
    char *port;
    /*@null@*/char *device;
};

enum unit {unspecified, imperial, nautical, metric};
enum unit gpsd_units(void);
enum deg_str_type { deg_dd, deg_ddmm, deg_ddmmss };

extern /*@observer@*/ char *deg_to_str( enum deg_str_type type,  double f);

extern void gpsd_source_spec(/*@null@*/const char *fromstring, 
			     /*@out@*/struct fixsource_t *source);

char *maidenhead(double n,double e);

#endif /* _GPSDCLIENT_H_ */
/* gpsdclient.h ends here */
