/* gpsutils.h -- geodesy and time conversions */

extern time_t mkgmtime(register struct tm *);
extern double timestamp(void);
extern double iso8601_to_unix(char *);
extern char *unix_to_iso8601(double t, char *);
extern double gpstime_to_unix(int, double, int);
extern double earth_distance(double, double, double, double);

/* return geoid separtion (MSL - WGS84) in meters, given a lat/lot in degrees */
extern double wgs84_separation(double lat, double lon);

/* guess at preferred units from environment and compiled in defaults
 * return: 0 = miles/feet, 1 = knots/feet, 2 = km/meters
 */
extern int gpsd_units(void);
