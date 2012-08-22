/*
 * NMEA2000 over CAN.
 *
 * This file is Copyright (c) 2012 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#if defined(NMEA2000_ENABLE)
#include "driver_nmea2000.h"
#include "bits.h"

#ifndef S_SPLINT_S
#include <linux/can.h>
#include <linux/can/raw.h>
#endif /* S_SPLINT_S */

#define LOG_FILE 1
#define NMEA2000_NETS 4
#define NMEA2000_UNITS 256
#define CAN_NAMELEN 32
#define MIN(a,b) ((a < b) ? a : b)


static struct gps_device_t *nmea2000_units[NMEA2000_NETS][NMEA2000_UNITS];
static char can_interface_name[NMEA2000_NETS][CAN_NAMELEN];

typedef struct PGN
    {
    unsigned int  pgn;
    unsigned int  fast;
    unsigned int  type;
    gps_mask_t    (* func)(unsigned char *bu, int len, struct PGN *pgn, struct gps_device_t *session);
    const char    *name;
    } PGN;

/*@-nullassign@*/

#if LOG_FILE
FILE *logFile = NULL;
#endif /* of if LOG_FILE */

extern bool __attribute__ ((weak)) gpsd_add_device(const char *device_name, bool flag_nowait);

static void print_data(unsigned char *buffer, int len, PGN *pgn)
{
#ifdef LIBGPS_DEBUG
    /*@-bufferoverflowhigh@*/
    if ((libgps_debuglevel >= LOG_IO) != 0) {
	int   l1, l2, ptr;
	char  bu[128];

        ptr = 0;
        l2 = sprintf(&bu[ptr], "got data:%6u:%3d: ", pgn->pgn, len);
	ptr += l2;
        for (l1=0;l1<len;l1++) {
            if (((l1 % 20) == 0) && (l1 != 0)) {
	        gpsd_report(LOG_IO,"%s\n", bu);
		ptr = 0;
                l2 = sprintf(&bu[ptr], "                   : ");
		ptr += l2;
            }
            l2 = sprintf(&bu[ptr], "%02ux ", (unsigned int)buffer[l1]);
	    ptr += l2;
        }
        gpsd_report(LOG_IO,"%s\n", bu);
    }
    /*@+bufferoverflowhigh@*/
#endif
}

static gps_mask_t get_mode(struct gps_device_t *session)
{
    if (session->driver.nmea2000.mode_valid) {
        session->newdata.mode = session->driver.nmea2000.mode;
    } else {
        session->newdata.mode = MODE_NOT_SEEN;
    }

    return MODE_SET;
}


static void decode_ais_header(unsigned char *bu, int len, struct ais_t *ais)
{
    ais->type   =  bu[0]       & 0x3f;
    ais->repeat = (bu[0] >> 6) & 0x03;
    ais->mmsi   = getleu32(bu, 1);
    gpsd_report(LOG_INF, "NMEA2000 AIS  message type %d, MMSI %09d:\n", ais->type, ais->mmsi);
    printf("NMEA2000 AIS  message type %2d, MMSI %09d:\n", ais->type, ais->mmsi);
}


static int ais_turn_rate(int rate)
{
    if (rate < 0) {
        return(-ais_turn_rate(-rate));
    }
    return(4.733 * sqrt(rate * RAD_2_DEG * .0001 * 60.0));
}


static double ais_direction(unsigned int val, double scale)
{
    return(val * RAD_2_DEG * 0.0001 * scale);
}


static gps_mask_t hnd_059392(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


static gps_mask_t hnd_060928(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


static gps_mask_t hnd_126208(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


static gps_mask_t hnd_126464(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


static gps_mask_t hnd_126996(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


static gps_mask_t hnd_129025(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    /*@-type@*//* splint has a bug here */
    session->newdata.latitude = getles32(bu, 0) * 1e-7;
    session->newdata.longitude = getles32(bu, 4) * 1e-7;
    /*@+type@*/

    (void)strlcpy(session->gpsdata.tag, "129025", sizeof(session->gpsdata.tag));

    return LATLON_SET | get_mode(session);
}


static gps_mask_t hnd_129026(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    session->driver.nmea2000.sid[0]  =  bu[0];

    /*@-type@*//* splint has a bug here */
    session->newdata.track           =  getleu16(bu, 2) * 1e-4 * RAD_2_DEG;
    session->newdata.speed           =  getleu16(bu, 4) * 1e-2;
    /*@+type@*/

    (void)strlcpy(session->gpsdata.tag, "129026", sizeof(session->gpsdata.tag));

    return SPEED_SET | TRACK_SET | get_mode(session);
}


static gps_mask_t hnd_126992(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    //uint8_t        sid;
    //uint8_t        source;

    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    //sid        = bu[0];
    //source     = bu[1] & 0x0f;

    /*@-type@*//* splint has a bug here */
    session->newdata.time = getleu16(bu, 2)*24*60*60 + getleu32(bu, 4)/1e4;
    /*@+type@*/

    (void)strlcpy(session->gpsdata.tag, "126992", sizeof(session->gpsdata.tag));

    return TIME_SET | get_mode(session);
}


static const int mode_tab[] = {MODE_NO_FIX, MODE_2D,  MODE_3D, MODE_NO_FIX,
			       MODE_NO_FIX, MODE_NO_FIX, MODE_NO_FIX, MODE_NO_FIX};

static gps_mask_t hnd_129539(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    gps_mask_t mask;

    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    mask                             = 0;
    session->driver.nmea2000.sid[1]  = bu[0];

    session->driver.nmea2000.mode_valid = 1;

    session->driver.nmea2000.mode    = mode_tab[(bu[1] >> 3) & 0x07];

    /*@-type@*//* splint has a bug here */
    session->gpsdata.dop.hdop        = getleu16(bu, 2) * 1e-2;
    session->gpsdata.dop.vdop        = getleu16(bu, 4) * 1e-2;
    session->gpsdata.dop.tdop        = getleu16(bu, 6) * 1e-2;
    /*@+type@*/
    mask                            |= DOP_SET;

    gpsd_report(LOG_DATA, "pgn %6d(%3d): sid:%02x hdop:%5.2f vdop:%5.2f tdop:%5.2f\n",
		pgn->pgn,
		session->driver.nmea2000.unit,
		session->driver.nmea2000.sid[1],
		session->gpsdata.dop.hdop,
		session->gpsdata.dop.vdop,
		session->gpsdata.dop.tdop);

    (void)strlcpy(session->gpsdata.tag, "129539", sizeof(session->gpsdata.tag));

    return mask | get_mode(session);
}


static gps_mask_t hnd_129540(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    int         l1, l2;

    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    session->driver.nmea2000.sid[2]           = bu[0];
    session->gpsdata.satellites_visible       = (int)bu[2];

    for (l2=0;l2<MAXCHANNELS;l2++) {
        session->gpsdata.used[l2] = 0;
    }
    l2 = 0;
    for (l1=0;l1<session->gpsdata.satellites_visible;l1++) {
        int    svt;
        double azi, elev, snr;

	/*@-type@*//* splint has a bug here */
        elev  = getles16(bu, 3+12*l1+1) * 1e-4 * RAD_2_DEG;
        azi   = getleu16(bu, 3+12*l1+3) * 1e-4 * RAD_2_DEG;
        snr   = getles16(bu, 3+12*l1+5) * 1e-2;
	/*@+type@*/

        svt   = (int)(bu[3+12*l1+11] & 0x0f);

        session->gpsdata.elevation[l1]  = (int) (round(elev));
	session->gpsdata.azimuth[l1]    = (int) (round(azi));
        session->gpsdata.ss[l1]         = snr;
        session->gpsdata.PRN[l1]        = (int)bu[3+12*l1+0];
	if ((svt == 2) || (svt == 5)) {
	    session->gpsdata.used[l2] = session->gpsdata.PRN[l1];
	    l2 += 1;
	}
    }
    return  SATELLITE_SET | USED_IS;
}


static gps_mask_t hnd_129029(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    gps_mask_t mask;

    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    mask                             = 0;
    session->driver.nmea2000.sid[3]  = bu[0];
 
    /*@-type@*//* splint has a bug here */
    session->newdata.time            = getleu16(bu,1) * 24*60*60 + getleu32(bu, 3)/1e4;
    /*@+type@*/
    mask                            |= TIME_SET;

    /*@-type@*//* splint has a bug here */
    session->newdata.latitude        = getles64(bu, 7) * 1e-16;
    session->newdata.longitude       = getles64(bu, 15) * 1e-16;
    /*@+type@*/
    mask                            |= LATLON_SET;

    /*@-type@*//* splint has a bug here */
    session->newdata.altitude        = getles64(bu, 23) * 1e-6;
    /*@+type@*/
    mask                            |= ALTITUDE_SET;

//  printf("mode %x %x\n", (bu[31] >> 4) & 0x0f, bu[31]);
    switch ((bu[31] >> 4) & 0x0f) {
    case 0:
        session->gpsdata.status      = STATUS_NO_FIX;
	break;
    case 1:
        session->gpsdata.status      = STATUS_FIX;
	break;
    case 2:
        session->gpsdata.status      = STATUS_DGPS_FIX;
	break;
    case 3:
    case 4:
    case 5:
        session->gpsdata.status      = STATUS_FIX; /* Is this correct ? */
	break;
    default:
        session->gpsdata.status      = STATUS_NO_FIX;
	break;
    }
    mask                            |= STATUS_SET;

    /*@-type@*//* splint has a bug here */
    session->gpsdata.separation      = getles32(bu, 38) / 100.0;
    /*@+type@*/
    session->newdata.altitude       -= session->gpsdata.separation;

    session->gpsdata.satellites_used = (int)bu[33];

    /*@-type@*//* splint has a bug here */
    session->gpsdata.dop.hdop        = getleu16(bu, 34) * 0.01;
    session->gpsdata.dop.pdop        = getleu16(bu, 36) * 0.01;
    /*@+type@*/
    mask                            |= DOP_SET;

    (void)strlcpy(session->gpsdata.tag, "129029", sizeof(session->gpsdata.tag));

    return mask | get_mode(session);
}


static gps_mask_t hnd_129038(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    decode_ais_header(bu, len, ais);
    ais->type1.lon       = getles32(bu, 5) * 0.06;
    ais->type1.lat       = getles32(bu, 9) * 0.06;
    ais->type1.accuracy	 = (bu[13] >> 0) & 0x01;
    ais->type1.raim	 = (bu[13] >> 1) & 0x01;
    ais->type1.second	 = (bu[13] >> 2) & 0x3f;
    ais->type1.course	 = ais_direction(getleu16(bu, 14), 10.0);
    ais->type1.speed	 = getleu16(bu, 16) * MPS_TO_KNOTS * 0.01 / 0.1;
    ais->type1.radio	 = getleu32(bu, 18) & 0x7ffff;
    ais->type1.heading	 = ais_direction(getleu16(bu, 21), 1.0);
    ais->type1.turn	 = ais_turn_rate(getles16(bu, 23));
    ais->type1.status	 = (bu[25] >> 0) & 0xff;
    ais->type1.maneuver	 = 0; /* Not transmitted ???? */

    return(ONLINE_SET | AIS_SET);
}


static gps_mask_t hnd_129039(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    decode_ais_header(bu, len, ais);
    return(0);
}


static gps_mask_t hnd_129040(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    decode_ais_header(bu, len, ais);
    return(0);
}


static gps_mask_t hnd_129794(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    decode_ais_header(bu, len, ais);
    return(0);
}


static gps_mask_t hnd_129798(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    decode_ais_header(bu, len, ais);
    return(0);
}


static gps_mask_t hnd_129802(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);
    return(0);
}


static gps_mask_t hnd_129809(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    decode_ais_header(bu, len, ais);
    return(0);
}


static gps_mask_t hnd_129810(unsigned char *bu, int len, PGN *pgn, struct gps_device_t *session)
{
    struct ais_t *ais;

    ais =  &session->gpsdata.ais;
    print_data(bu, len, pgn);
    gpsd_report(LOG_DATA, "pgn %6d(%3d):\n", pgn->pgn, session->driver.nmea2000.unit);

    decode_ais_header(bu, len, ais);
    return(0);
}

/*@-usereleased@*/
static const char msg_059392[] = {"ISO  Acknowledgment"};
static const char msg_060928[] = {"ISO  Address Claim"};
static const char msg_126208[] = {"NMEA Command/Request/Acknowledge"};
static const char msg_126464[] = {"ISO  Transmit/Receive PGN List"};
static const char msg_126992[] = {"GNSS System Time"};
static const char msg_126996[] = {"ISO  Product Information"};
static const char msg_129025[] = {"GNSS Position Rapid Update"};
static const char msg_129026[] = {"GNSS COG and SOG Rapid Update"};
static const char msg_129029[] = {"GNSS Positition Data"};
static const char msg_129539[] = {"GNSS DOPs"};
static const char msg_129540[] = {"GNSS Satellites in View"};
static const char msg_129038[] = {"AIS  Class A Position Report"};
static const char msg_129039[] = {"AIS  Class B Position Report"};
static const char msg_129040[] = {"AIS  Class B Extended Position Report"};
static const char msg_129794[] = {"AIS  Class A Static and Voyage Related Data"};
static const char msg_129798[] = {"AIS  SAR Aircraft Position Report"};
static const char msg_129802[] = {"AIS  Safty Related Broadcast Message"};
static const char msg_129809[] = {"AIS  Class B CS Static Data Report, Part A"};
static const char msg_129810[] = {"AIS  Class B CS Static Data Report, Part B"};
static const char msg_error [] = {"**error**"};

static PGN gpspgn[] = {{ 59392, 0, 0, hnd_059392, &msg_059392[0]},
		       { 60928, 0, 0, hnd_060928, &msg_060928[0]},
		       {126208, 0, 0, hnd_126208, &msg_126208[0]},
		       {126464, 1, 0, hnd_126464, &msg_126464[0]},
		       {126992, 0, 0, hnd_126992, &msg_126992[0]},
		       {126996, 1, 0, hnd_126996, &msg_126996[0]},
		       {129025, 0, 1, hnd_129025, &msg_129025[0]},
		       {129026, 0, 1, hnd_129026, &msg_129026[0]},
		       {129029, 1, 1, hnd_129029, &msg_129029[0]},
		       {129539, 0, 1, hnd_129539, &msg_129539[0]},
		       {129540, 1, 1, hnd_129540, &msg_129540[0]},
		       {0     , 0, 0, NULL,       &msg_error [0]}};

static PGN aispgn[] = {{ 59392, 0, 0, hnd_059392, &msg_059392[0]},
		       { 60928, 0, 0, hnd_060928, &msg_060928[0]},
		       {126208, 0, 0, hnd_126208, &msg_126208[0]},
		       {126464, 1, 0, hnd_126464, &msg_126464[0]},
		       {126992, 0, 0, hnd_126992, &msg_126992[0]},
		       {126996, 1, 0, hnd_126996, &msg_126996[0]},
		       {129038, 1, 2, hnd_129038, &msg_129038[0]},
		       {129039, 1, 2, hnd_129039, &msg_129039[0]},
		       {129040, 1, 2, hnd_129040, &msg_129040[0]},
		       {129794, 1, 2, hnd_129794, &msg_129794[0]},
		       {129798, 1, 2, hnd_129798, &msg_129798[0]},
		       {129802, 1, 2, hnd_129802, &msg_129802[0]},
		       {129809, 1, 2, hnd_129809, &msg_129809[0]},
		       {129810, 1, 2, hnd_129810, &msg_129810[0]},
		       {0     , 0, 0, NULL,       &msg_error [0]}};
/*@+usereleased@*/

/*@-immediatetrans@*/
static /*@null@*/ PGN *search_pgnlist(unsigned int pgn, PGN *pgnlist)
{
    int l1;
    PGN *work;

    l1 = 0;
    work = NULL;
    while (pgnlist[l1].pgn != 0) {
        if (pgnlist[l1].pgn == pgn) {
	    work = &pgnlist[l1];
	    break;
	} else {
	    l1 = l1 + 1;
	    }
	}
    return work;
}
/*@+immediatetrans@*/

/*@-nullstate -branchstate -globstate -mustfreeonly@*/
static void find_pgn(struct can_frame *frame, struct gps_device_t *session)
{
    PGN *work;
    unsigned int can_net;

    session->driver.nmea2000.workpgn = NULL;
    can_net = session->driver.nmea2000.can_net;
    if (can_net > (NMEA2000_NETS-1)) {
        gpsd_report(LOG_ERROR, "NMEA2000 find_pgn: Invalid can network %d.\n", can_net);
        return;
    }

    /*@ignore@*//* because the CAN include files choke splint */
    if (frame->can_id & 0x80000000) {
	// cppcheck-suppress unreadVariable
	unsigned int source_prio UNUSED;
	unsigned int daddr UNUSED;
	// cppcheck-suppress unreadVariable
	unsigned int source_pgn;
	unsigned int source_unit;

#if LOG_FILE
        if (logFile != NULL) {
	    struct timespec  msgTime;

	    clock_gettime(CLOCK_REALTIME, &msgTime);
	    fprintf(logFile,
		    "(%010d.%06d) can0 %08x#",
		    (unsigned int)msgTime.tv_sec,
		    (unsigned int)msgTime.tv_nsec/1000,
		    frame->can_id & 0x1ffffff);
	    if ((frame->can_dlc & 0x0f) > 0) {
		int l1;
	        for(l1=0;l1<(frame->can_dlc & 0x0f);l1++) {
		    fprintf(logFile, "%02x", frame->data[l1]);
		}
	    }
	    fprintf(logFile, "\n");
	}
#endif /* of if LOG_FILE */
	/*@end@*/
	session->driver.nmea2000.can_msgcnt += 1;
	/*@ignore@*//* because the CAN include files choke splint */
	source_pgn = (frame->can_id >> 8) & 0x1ffff;
	source_prio = (frame->can_id >> 26) & 0x7;
	source_unit = frame->can_id & 0x0ff;
	/*@end@*/

	if ((source_pgn >> 8) < 240) {
	    daddr  = source_pgn & 0x000ff;
	    source_pgn  = source_pgn & 0x1ff00;
	} else {
	    daddr = 0;
	}

	if (session->driver.nmea2000.unit_valid == 0) {
	    unsigned int l1, l2;
	    
	    for (l1=0;l1<NMEA2000_NETS;l1++) {
	        for (l2=0;l2<NMEA2000_UNITS;l2++) {
		    if (session == nmea2000_units[l1][l2]) {
		        session->driver.nmea2000.unit = l2;
		        session->driver.nmea2000.unit_valid = 1;
			session->driver.nmea2000.can_net = l1;
			can_net = l1;
		    }
		}
	    }
	}

	if (session->driver.nmea2000.unit_valid == 0) {
	    session->driver.nmea2000.unit = source_unit;
	    session->driver.nmea2000.unit_valid = 1;
	    nmea2000_units[can_net][source_unit] = session;
	}

	if (source_unit == session->driver.nmea2000.unit) {
	    if (session->driver.nmea2000.pgnlist != NULL) {
	        work = search_pgnlist(source_pgn, session->driver.nmea2000.pgnlist);
	    } else {
	        PGN *pgnlist;
	    
		pgnlist = &gpspgn[0];
		work = search_pgnlist(source_pgn, pgnlist);
		if (work == NULL) {
		    pgnlist = &aispgn[0];
		    work = search_pgnlist(source_pgn, pgnlist);
		}
		if ((work != 0) && (work->type > 0)) {
		    session->driver.nmea2000.pgnlist = pgnlist;
		}
	    }
	    if (work != NULL) {
	        if (work->fast == 0) {
		    size_t l2;

		    gpsd_report(LOG_DATA, "pgn %6d:%s \n", work->pgn, work->name);
		    session->driver.nmea2000.workpgn = (void *) work;
		    session->driver.nmea2000.idx = 0;
		    session->driver.nmea2000.ptr = 0;
		    /*@i1@*/session->packet.outbuflen =  frame->can_dlc & 0x0f;
		    for (l2=0;l2<session->packet.outbuflen;l2++) {
		        /*@i3@*/session->packet.outbuffer[session->driver.nmea2000.ptr++]= frame->data[l2];
		    }
		}
		/*@i2@*/else if ((frame->data[0] & 0x1f) == 0) {
		    unsigned int l2;

		    /*@i2@*/session->driver.nmea2000.fast_packet_len = frame->data[1];
		    /*@i2@*/session->driver.nmea2000.idx = frame->data[0];
		    session->driver.nmea2000.ptr = 0;
		    session->driver.nmea2000.idx += 1;
		    for (l2=2;l2<8;l2++) {
		        /*@i3@*/session->packet.outbuffer[session->driver.nmea2000.ptr++]= frame->data[l2];
		    }
		    gpsd_report(LOG_DATA, "pgn %6d:%s \n", work->pgn, work->name);
		}
		/*@i2@*/else if (frame->data[0] == session->driver.nmea2000.idx) {
		    unsigned int l2;

		    for (l2=1;l2<8;l2++) {
		        if (session->driver.nmea2000.fast_packet_len > session->driver.nmea2000.ptr) {
			    /*@i3@*/session->packet.outbuffer[session->driver.nmea2000.ptr++] = frame->data[l2];
			}
		    }
		    if (session->driver.nmea2000.ptr == session->driver.nmea2000.fast_packet_len) {
		        session->driver.nmea2000.workpgn = (void *) work;
		        session->packet.outbuflen = session->driver.nmea2000.fast_packet_len;
			session->driver.nmea2000.fast_packet_len = 0;
		    } else {
		        session->driver.nmea2000.idx += 1;
		    }
		} else {
		    session->driver.nmea2000.idx = 0;
		    session->driver.nmea2000.fast_packet_len = 0;
		    gpsd_report(LOG_ERROR, "Fast error\n");
		}
	    } else {
	    }
	} else {
	    // we got a unknown unit number
	    if (nmea2000_units[can_net][source_unit] == NULL) {
	        char buffer[32];

		(void) snprintf(buffer,
				sizeof(buffer),
				"nmea2000://%s:%u",
				can_interface_name[can_net],
				source_unit);
		if (gpsd_add_device != NULL) {
		    (void) gpsd_add_device(buffer, true);
		}
	    }
	}
    } else {
        // we got RTR or 2.0A CAN frame, not used
    }
}
/*@+nullstate +branchstate +globstate +mustfreeonly@*/


static ssize_t nmea2000_get(struct gps_device_t *session)
{   
    struct can_frame frame;
    ssize_t          status;

//  printf("NMEA2000 get: enter\n");
    session->packet.outbuflen = 0;
    status = read(session->gpsdata.gps_fd, &frame, sizeof(frame));
    if (status == (ssize_t)sizeof(frame)) {
        session->packet.type = NMEA2000_PACKET;
	find_pgn(&frame, session);
//	printf("NMEA2000 get: exit(%d)\n", status);
	if (session->driver.nmea2000.workpgn == NULL) {
	    status = 0;
	}
        return frame.can_dlc & 0x0f;
    }
//  printf("NMEA2000 get: exit(0)\n");
    return 0;
}

/*@-mustfreeonly@*/
static gps_mask_t nmea2000_parse_input(struct gps_device_t *session)
{    
    gps_mask_t mask;
    PGN *work;
 
//  printf("NMEA2000 parse_input called\n");
    mask = 0;
    work = (PGN *) session->driver.nmea2000.workpgn;

    if (work != NULL) {
        mask = (work->func)(&session->packet.outbuffer[0], (int)session->packet.outbuflen, work, session);
        session->driver.nmea2000.workpgn = NULL;
    }
    session->packet.outbuflen = 0;

    return mask;
}
/*@+mustfreeonly@*/

/*@+nullassign@*/

#ifndef S_SPLINT_S

int nmea2000_open(struct gps_device_t *session)
{
    char interface_name[strlen(session->gpsdata.dev.path)];
    socket_t sock;
    int status;
    int unit_number;
    int can_net;
    unsigned int l;
    struct ifreq ifr;
    struct sockaddr_can addr;
    char *unit_ptr;

    session->gpsdata.gps_fd = -1;

    session->driver.nmea2000.can_net = 0;
    can_net = -1;

    unit_number = -1;

    (void)strlcpy(interface_name, session->gpsdata.dev.path + 11, sizeof(interface_name));
    unit_ptr = NULL;
    for (l=0;l<strnlen(interface_name,sizeof(interface_name));l++) {
        if (interface_name[l] == ':') {
	    unit_ptr = &interface_name[l+1];
	    interface_name[l] = 0;
	    continue;
	}
	if (unit_ptr != NULL) {
	    if (isdigit(interface_name[l]) == 0) {
	        gpsd_report(LOG_ERROR, "NMEA2000 open: Invalid character in unit number.\n");
	        return -1;
	    }
	}
    }

    if (unit_ptr != NULL) {
        unit_number = atoi(unit_ptr);
	if ((unit_number < 0) || (unit_number > (NMEA2000_UNITS-1))) {
	    gpsd_report(LOG_ERROR, "NMEA2000 open: Unit number out of range.\n");
	    return -1;
	}
	for (l = 0; l < NMEA2000_NETS; l++) {
	    if (strncmp(can_interface_name[l], 
			interface_name,
			MIN(sizeof(interface_name), sizeof(can_interface_name[l]))) == 0) {
	        can_net = l;
		break;
	    }
	}
	if (can_net < 0) {
	    gpsd_report(LOG_ERROR, "NMEA2000 open: CAN device not open: %s .\n", interface_name);
	    return -1;
	}
    } else {
	for (l = 0; l < NMEA2000_NETS; l++) {
	    if (strncmp(can_interface_name[l], 
			interface_name,
			MIN(sizeof(interface_name), sizeof(can_interface_name[l]))) == 0) {
	        gpsd_report(LOG_ERROR, "NMEA2000 open: CAN device duplicate open: %s .\n", interface_name);
		return -1;
	    }
	}
	for (l = 0; l < NMEA2000_NETS; l++) {
	    if (can_interface_name[l][0] == 0) {
	        can_net = l;
		break;
	    }
	}
	if (can_net < 0) {
	    gpsd_report(LOG_ERROR, "NMEA2000 open: Too many CAN networks open.\n");
	    return -1;
	}
    }

    /* Create the socket */
    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
 
    if (sock == -1) {
        gpsd_report(LOG_ERROR, "NMEA2000 open: can not get socket.\n");
	return -1;
    }

    status = fcntl(sock, F_SETFL, O_NONBLOCK);
    if (status != 0) {
        gpsd_report(LOG_ERROR, "NMEA2000 open: can not set socket to O_NONBLOCK.\n");
	close(sock);
	return -1;
    }

    /* Locate the interface you wish to use */
    strlcpy(ifr.ifr_name, interface_name, sizeof(ifr.ifr_name));
    status = ioctl(sock, SIOCGIFINDEX, &ifr); /* ifr.ifr_ifindex gets filled 
					       * with that device's index */

    if (status != 0) {
        gpsd_report(LOG_ERROR, "NMEA2000 open: can not find CAN device.\n");
	close(sock);
	return -1;
    }

    /* Select that CAN interface, and bind the socket to it. */
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    status = bind(sock, (struct sockaddr*)&addr, sizeof(addr) );
    if (status != 0) {
        gpsd_report(LOG_ERROR, "NMEA2000 open: bind failed.\n");
	close(sock);
	return -1;
    }

    gpsd_switch_driver(session, "NMEA2000");
    session->gpsdata.gps_fd = sock;
    session->sourcetype = source_can;
    session->servicetype = service_sensor;
    session->driver.nmea2000.can_net = can_net;

    if (unit_ptr != NULL) {
        nmea2000_units[can_net][unit_number] = session;
	session->driver.nmea2000.unit = unit_number;
	session->driver.nmea2000.unit_valid = 1;
    } else {
        strncpy(can_interface_name[can_net],
		interface_name, 
		MIN(sizeof(can_interface_name[0]), sizeof(interface_name)));
	session->driver.nmea2000.unit_valid = 0;
	for (l=0;l<NMEA2000_UNITS;l++) {
	    nmea2000_units[can_net][l] = NULL;	  
	}
    }
    return session->gpsdata.gps_fd;
}
#endif /* of ifndef S_SPLINT_S */

void nmea2000_close(struct gps_device_t *session)
{
    if (session->gpsdata.gps_fd != -1) {
	gpsd_report(LOG_SPIN, "close(%d) in nmea2000_close(%s)\n",
		    session->gpsdata.gps_fd, session->gpsdata.dev.path);
	(void)close(session->gpsdata.gps_fd);
	session->gpsdata.gps_fd = -1;
    }
}

/* *INDENT-OFF* */
const struct gps_type_t nmea2000 = {
    .type_name      = "NMEA2000",       /* full name of type */
    .packet_type    = NMEA2000_PACKET,	/* associated lexer packet type */
    .flags	    = DRIVER_NOFLAGS,	/* no rollover or other flags */
    .trigger	    = NULL,		/* detect their main sentence */
    .channels       = 12,		/* not an actual GPS at all */
    .probe_detect   = NULL,
    .get_packet     = nmea2000_get,	/* how to get a packet */
    .parse_packet   = nmea2000_parse_input,	/* how to interpret a packet */
    .rtcm_writer    = NULL,		/* Don't send RTCM to this */
    .event_hook     = NULL,
#ifdef RECONFIGURE_ENABLE
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no rate switcher */
    .min_cycle      = 1,		/* nominal 1-per-second GPS cycle */
#endif /* RECONFIGURE_ENABLE */
#ifdef CONTROLSEND_ENABLE
    .control_send   = NULL,		/* how to send control strings */
#endif /* CONTROLSEND_ENABLE */
#ifdef NTPSHM_ENABLE
    .ntp_offset     = NULL,
#endif /* NTPSHM_ ENABLE */
};
/* *INDENT-ON* */

/* end */

#endif /* of  defined(NMEA2000_ENABLE) */
