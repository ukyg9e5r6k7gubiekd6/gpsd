/* gps_json.h - JSON handling for libgps and gpsd */

#include "json.h"

#define GPS_JSON_COMMAND_MAX	80
#define GPS_JSON_RESPONSE_MAX	1024

char *json_stringify(/*@out@*/char *, size_t, /*@in@*/const char *);
int json_watch_read(const char *, struct policy_t *, /*@null@*/const char **);
int json_device_read(const char *, struct devconfig_t *, /*@null@*/const char **);
void json_version_dump(/*@out@*/char *, size_t);
void json_tpv_dump(struct gps_data_t *, struct gps_fix_t *, char *, size_t);
void json_sky_dump(struct gps_data_t *, char *, size_t);
void json_device_dump(struct gps_device_t *, char *, size_t);
void json_watch_dump(struct policy_t *, char *, size_t);
int json_rtcm2_read(const char *, char *, size_t, struct rtcm2_t *, /*@null@*/const char **);
int json_ais_read(const char *, char *, size_t, struct ais_t *, /*@null@*/const char **);
int libgps_json_unpack(const char *, struct gps_data_t *);

/* gps_json.h ends here */
