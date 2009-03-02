/* gpsdclient.h -- common functions for GPSD clients */

struct fixsource_t 
/* describe a data source */
{
    char *spec;		/* pointer to actual storage */
    char *server;
    char *port;
    char *device;
};

enum unit {unspecified, imperial, nautical, metric};
enum unit gpsd_units(void);
enum deg_str_type { deg_dd, deg_ddmm, deg_ddmmss };

extern /*@observer@*/ char *deg_to_str( enum deg_str_type type,  double f);

extern void gpsd_source_spec(/*@null@*/const char *fromstring, 
			     /*@out@*/struct fixsource_t *source);

/* gpsdclient.h ends here */
