#include <sys/types.h>
#include "gpsd.h"


void send_nmea(fd_set *afds, fd_set *nmea_fds, char *buf)
{
    /* this is a stub. It will be used if the app does
     * not provide a real send_nmea()
     */
}

