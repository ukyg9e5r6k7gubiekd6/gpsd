/* gps_json.h - JSON handling for libgps and gpsd */

#include "json.h"

#define GPS_JSON_COMMAND_MAX	80
#define GPS_JSON_RESPONSE_MAX	1024
#define GPS_JSON_DEVICES_MAX	4

struct devconfig_t {
    char	device[PATH_MAX];
    int		native;
    int 	bps;
    char	serialmode[4];
    double	cycle;
    double	mincycle;
};
void json_tpv_dump(struct gps_data_t *, struct gps_fix_t *, char *, size_t);
void json_sky_dump(struct gps_data_t *, char *, size_t);
void json_device_dump(struct gps_device_t *, char *, size_t);
int json_watch_read(struct policy_t *, const char *, const char **);
void json_watch_dump(struct policy_t *, char *, size_t);
int json_configdev_read(struct devconfig_t *, const char *, const char **);
void json_configdev_dump(struct gps_device_t *, char *, size_t);
int libgps_json_unpack(const char *, struct gps_data_t *);

/* gps_json.h ends here */
