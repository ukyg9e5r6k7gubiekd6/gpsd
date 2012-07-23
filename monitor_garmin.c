/*
 * Garmin binary object for the GPS packet monitor.
 *
 * This file is Copyright (c) 2011 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <assert.h>
#include <math.h>
#include <curses.h>

#include "gpsd.h"
#include "bits.h"
#include "gpsmon.h"

#if defined(GARMIN_ENABLE) && defined(BINARY_ENABLE)
extern const struct gps_type_t garmin_ser_binary;

static WINDOW *miscwin, *mid51win, *mid114win;

#define display	(void)mvwprintw

#define GARMIN_CHANNELS 12

#define GPSD_LE16TOH(x) getles16((char *)(&(x)), 0)
#define GPSD_LE32TOH(x) getles32((char *)(&(x)), 0)

#pragma pack(1)
/* Satellite Data Record */
typedef struct
{
    uint8_t svid;
    uint16_t snr;
    uint8_t elev;
    uint16_t azmth;
    uint8_t status;
} cpo_sat_data;

/* Position Record */
typedef struct
{
    float alt;
    float epe;
    float eph;
    float epv;
    int16_t fix;
    double gps_tow;
    double lat;
    double lon;
    float lon_vel;
    float lat_vel;
    float alt_vel;
    float msl_hght;
    int16_t leap_sec;
    int32_t grmn_days;
} cpo_pvt_data;

/* Receiver Measurement Record */
typedef struct
{
    uint32_t cycles;
    // cppcheck-suppress unusedStructMember
    double pr;
    uint16_t phase;
    int8_t slp_dtct;
    uint8_t snr_dbhz;
    uint8_t svid;
    int8_t valid;
} cpo_rcv_sv_data;
typedef struct
{
    // cppcheck-suppress unusedStructMember
    double rcvr_tow;
    int16_t rcvr_wn;
    cpo_rcv_sv_data sv[GARMIN_CHANNELS];
} cpo_rcv_data;

/* description of the status */
static char *fixdesc[] = {
    "no fix",
    "no fix",
    "2D",
    "3D",
    "2D dif",
    "3D dif",
};

/* check range of an unsigned quantity */
#define CHECK_RANGE(vec, i) ((i) < sizeof(vec)/sizeof(vec[0]))

static bool garmin_bin_initialize(void)
{
    /*@-globstate@*/
    unsigned int i;

#ifndef CONTROLSEND_ENABLE
    if(serial) {
	monitor_complain("Direct mode doesn't supported.");
	return false;
    }
#endif

    /*@ -onlytrans @*/
    miscwin = subwin(devicewin, 1, 80, 1, 0);
    mid51win = subwin(devicewin, 12, 18, 2, 0);
    mid114win = subwin(devicewin, GARMIN_CHANNELS + 3, 23, 2, 18);
    if (miscwin == NULL || mid51win == NULL || mid114win == NULL)
	return false;

    (void)syncok(miscwin, true);
    (void)syncok(mid51win, true);
    (void)syncok(mid114win, true);

    /*@ -nullpass @*/
    (void)wattrset(miscwin, A_BOLD);
    display(miscwin, 0, 0, "Time:");
    (void)wattrset(miscwin, A_NORMAL);

    (void)wborder(mid51win, 0, 0, 0, 0, 0, 0, 0, 0),
    (void)wattrset(mid51win, A_BOLD);
    display(mid51win, 0, 4, " Position ");
    display(mid51win, 1, 2, "Fix:");
    display(mid51win, 2, 2, "Lat:");
    (void)mvwaddch(mid51win, 2, 16, ACS_DEGREE);
    display(mid51win, 3, 2, "Lon:");
    (void)mvwaddch(mid51win, 3, 16, ACS_DEGREE);
    display(mid51win, 4, 2, "Alt:          m");
    display(mid51win, 5, 2, "Speed:      m/s");
    display(mid51win, 6, 2, "Climb:      m/s");
    display(mid51win, 7, 2, "Leap:   sec");
    display(mid51win, 8, 2, "epe:       m");
    display(mid51win, 9, 2, "eph:       m");
    display(mid51win, 10, 2, "epv:       m");
    display(mid51win, 11, 3, " ID 51 (0x33) ");
    (void)wattrset(mid51win, A_NORMAL);

    (void)wborder(mid114win, 0, 0, 0, 0, 0, 0, 0, 0),
	(void)wattrset(mid114win, A_BOLD);
    display(mid114win, 1, 1, "Ch PRN  Az El  SNR ST");
    for (i = 0; i < GARMIN_CHANNELS; i++) {
	display(mid114win, (int)i + 2, 1, "%2d", i);
    }
    display(mid114win, 0, 5, " Satellite ");
    display(mid114win, 14, 4, " ID 114 (0x72) ");
    (void)wattrset(mid114win, A_NORMAL);

    /* initialize the GPS context's time fields */
    gpsd_time_init(session.context, time(NULL));

    return true;
    /*@+globstate@*/
}

/*@ -globstate -compdef */
static void garmin_bin_update(uint16_t pkt_id, uint32_t pkt_size UNUSED, unsigned char *pkt_data)
{
    int i;
    cpo_sat_data *sats = NULL;
    cpo_pvt_data *pvt = NULL;
    //cpo_rcv_data *rmd = NULL;
    char tbuf[JSON_DATE_MAX+1];

    switch (pkt_id) {
    case 0x29:	/* Receiver Measurement Record */
    case 0x34:
	/* for future use */
	//rmd = (cpo_rcv_data *)pkt_data;
	monitor_log("RMD 0x%02x=", pkt_id);
	break;

    case 0x33:	/* Position Record */
	display(miscwin, 0, 6, "%-24s",
		unix_to_iso8601(session.gpsdata.fix.time, tbuf, sizeof(tbuf)));
	pvt = (cpo_pvt_data *)pkt_data;
	display(mid51win, 1, 7, "%s",
		(CHECK_RANGE(fixdesc, (uint8_t)GPSD_LE16TOH(pvt->fix)) ? \
			fixdesc[GPSD_LE16TOH(pvt->fix)] : "unknown"));
	display(mid51win, 2, 8, "%3.5f", pvt->lat * RAD_2_DEG);
	display(mid51win, 3, 8, "%3.5f", pvt->lon * RAD_2_DEG);
	display(mid51win, 4, 8, "%8.2f", pvt->alt + pvt->msl_hght);
	display(mid51win, 5, 9, "%5.1f", hypot(pvt->lon_vel, pvt->lat_vel));
	display(mid51win, 6, 9, "%5.1f", pvt->alt_vel);
	display(mid51win, 7, 8, "%d", (int)GPSD_LE16TOH(pvt->leap_sec));
	if (GPSD_LE16TOH(pvt->fix) < 2) /* error value is very large when status no fix */
	   pvt->epe = pvt->eph = pvt->epv = NAN;
	display(mid51win, 8, 7, "%6.2f", pvt->epe);
	display(mid51win, 9, 7, "%6.2f", pvt->eph);
	display(mid51win, 10, 7, "%6.2f", pvt->epv);
	monitor_log("PVT 0x%02x=", pkt_id);
	break;

    case 0x72:	/* Satellite Data Record */
	sats = (cpo_sat_data *)pkt_data;
	for (i = 0; i < GARMIN_CHANNELS; i++, sats++) {
	   display(mid114win, i + 2, 3,  " %3u %3u %2u %4.1f %2x",
		 sats->svid,
		 GPSD_LE16TOH(sats->azmth),
		 sats->elev,
		 (float)GPSD_LE16TOH(sats->snr) / 100.0,
		 sats->status);
	}
	monitor_log("SAT 0x%02x=", pkt_id);
	break;

    case 0xff:	/* Product Data Record */
	monitor_log("PDR 0x%02x=", pkt_id);
	break;

    default:
	monitor_log("UNK 0x%02x=", pkt_id);
	break;
    }
}

static void garmin_bin_ser_update(void)
{
   unsigned char *buf;
   size_t len;
   uint16_t pkt_id;
   uint32_t pkt_size;
   unsigned char pkt_data[255];
   int i, j;
   unsigned char c;
   unsigned char chksum;
   bool pkt_good = false, got_dle = false;

   buf = session.packet.outbuffer;
   len = session.packet.outbuflen;

   if (!(buf[0] == (unsigned char)0x10 &&		/* DLE */
		buf[len-2] == (unsigned char)0x10 &&	/* DLE */
		buf[len-1] == (unsigned char)0x03))	/* ETX */
	goto end;	/* bad pkt */

   if (buf[1] == (unsigned char)0x10 && buf[2] != (unsigned char)0x10)
	goto end;	/* bad pkt */
   pkt_id = (uint16_t)buf[1];

   if (buf[2] == (unsigned char)0x10 && buf[3] != (unsigned char)0x10)
	goto end;	/* bad pkt */
   pkt_size = (uint32_t)buf[2];

   chksum = buf[1] + buf[2];	/* pkt_id + pkt_size */

   j = 0;
   for (i = 0; i <= 255; i++) {
	if (pkt_size == (uint32_t)j)
	   break;
	c = buf[i+3];
	if (got_dle) {
	   got_dle = false;
	   if (c != (unsigned char)0x10)
		goto end;	/* bad pkt */
	} else {
	   pkt_data[j++] = c;
	   chksum += c;
	   if (c == (unsigned char)0x10)
		got_dle = true;
	}
   }

   /* check pkt chksum */
   if ((unsigned char)(-chksum) == buf[len-3])
	pkt_good = true;

   end:
   if (pkt_good) {
#ifdef CONTROLSEND_ENABLE
	if(serial)
	   /* good packet, send ACK */
	   (void)monitor_control_send((unsigned char *)"\x10\x06\x00\xfa\x10\x03", 6);
#endif
	garmin_bin_update(pkt_id, pkt_size, pkt_data);
   } else {
#ifdef CONTROLSEND_ENABLE
	if(serial)
	   /* bad packet, send NAK */
	   (void)monitor_control_send((unsigned char *)"\x10\x15\x00\xeb\x10\x03", 6);
#endif
	monitor_log("BAD 0x%02x=", buf[1]);
   }
}
/*@ +globstate +compdef */

static void garmin_bin_wrap(void)
{
    (void)delwin(miscwin);
    (void)delwin(mid51win);
    (void)delwin(mid114win);
}

const struct monitor_object_t garmin_bin_ser_mmt = {
    .initialize = garmin_bin_initialize,
    .update = garmin_bin_ser_update,
    .command = NULL,
    .wrap = garmin_bin_wrap,
    .min_y = 16,.min_x = 80,
    .driver = &garmin_ser_binary,
};

#endif /* defined(GARMIN_ENABLE) && defined(BINARY_ENABLE) */

