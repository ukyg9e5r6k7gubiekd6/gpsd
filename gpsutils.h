/* gpsutils.h -- geodesy and time conversions */

extern double timestamp(void);
extern double iso8661_to_unix(char *);
extern char *unix_to_iso8661(double t, char *);
extern int tzoffset(void);
extern double gpstime_to_unix(int, double);
extern double earth_distance(double, double, double, double);
