/* gps_json.h - JSON handling for libgps and gpsd */

#include "json.h"

void json_tpv_dump(char *, struct gps_fix_t *, char *, size_t);
void json_sky_dump(struct gps_data_t *, char *, size_t);
int json_tpv_read(const char *, struct gps_data_t *);
int json_sky_read(const char *, struct gps_data_t *);

/* gps_json.h ends here */
