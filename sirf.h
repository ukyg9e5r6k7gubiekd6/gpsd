/* sirf.h -- control functions for SiRF-II GPS chipset */

#define DGPS_SOURCE_NONE     0
#define DGPS_SOURCE_EXTERNAL 1
#define DGPS_SOURCE_INTERNAL 2
#define DGPS_SOURCE_WAAS     3

int sirf_to_sirfbin(int, int);
int sirf_waas_ctrl(int, int);
int sirf_to_nmea(int, int);
int sirf_reset(int);
int sirf_dgps_source(int, int);
int sirf_nav_lib(int, int);
int sirf_nmea_waas(int, int);
int sirf_power_mask(int, int);
int sirf_power_save(int, int);

/* sirf.h ends here */
