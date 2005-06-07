/* gpsutils.h -- geodesy and time conversions */

extern time_t mkgmtime(register struct tm *);
extern double timestamp(void);
extern double iso8601_to_unix(char *);
extern /*@observer@*/char *unix_to_iso8601(double t, /*@ out @*/char[], int len);
extern double gpstime_to_unix(int, double);
/* extern double earth_distance(double, double, double, double); */
extern double wgs84_separation(double lat, double lon);
extern int gpsd_units(void);
