#include <sys/types.h>
#include "gpsd.h"


void gps_send_NMEA(fd_set *afds, fd_set *nmea_fds, char *buf)
{
    /* this is a stub. It will be used if the app does
     * not provide a real gps_send_NMEA()
     */
}

