/*
 * Handle the Garmin binary packet format supported by the USB Garmins
 * tested with the Garmin 18 and other models.  This driver is NOT for
 * serial port connected Garmins, they provide adequate NMEA support.
 *
 * This code is partly from the Garmin IOSDK and partly from the
 * sample code in the Linux garmin_gps driver.
 *
 * Presently this code needs the Linux garmin_gps driver and will
 * not function without it.  It also depends on the Intel byte order
 * (little-endian) so will not work on PPC or other big-endian machines
 *
 * Protocol info from:
 *	 GPS18_TechnicalSpecification.pdf
 *	 iop_spec.pdf
 * http://www.garmin.com/support/commProtocol.html
 *
 * bad code by: Gary E. Miller <gem@rellim.com>
 * all rights abandoned, a thank would be nice if you use this code.
 *
 * -D 3 = packet trace
 * -D 4 = packet details
 * -D 5 = more packet details
 *
 * limitations:
 *
 * do not have from garmin:
 *      pdop
 *      hdop
 *      vdop
 *	magnetic variation
 *
 * known bugs:
 *      won't work on a little-endian machine
 *	xgps says "NO FIX" and refuses to show the speed and track.
 *      hangs in the fread loop instead of keeping state and returning.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>

#if defined (HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#include "config.h"
#include "gpsd.h"
#include "gps.h"

#ifdef GARMIN_ENABLE

#define GARMIN_LAYERID_TRANSPORT  0
#define GARMIN_LAYERID_APPL      20
// Linux Garmin USB driver layer-id to use for some control mechanisms
#define GARMIN_LAYERID_PRIVATE  0x01106E4B

// packet ids used in private layer
#define PRIV_PKTID_SET_DEBUG    1
#define PRIV_PKTID_SET_MODE     2
#define PRIV_PKTID_INFO_REQ     3
#define PRIV_PKTID_INFO_RESP    4
#define PRIV_PKTID_RESET_REQ    5
#define PRIV_PKTID_SET_DEF_MODE 6

#define MODE_NATIVE          0
#define MODE_GARMIN_SERIAL   1

#define GARMIN_PKTID_TRANSPORT_START_SESSION_REQ 5
#define GARMIN_PKTID_TRANSPORT_START_SESSION_RESP 6

#define GARMIN_PKTID_PROTOCOL_ARRAY     253
#define GARMIN_PKTID_PRODUCT_RQST       254
#define GARMIN_PKTID_PRODUCT_DATA       255
#define GARMIN_PKTID_PVT_DATA           51
#define GARMIN_PKTID_SAT_DATA           114

#define GARMIN_PKTID_L001_XFER_CMPLT     12
#define GARMIN_PKTID_L001_COMMAND_DATA   10
#define GARMIN_PKTID_L001_DATE_TIME_DATA 14
#define GARMIN_PKTID_L001_RECORDS        27
#define GARMIN_PKTID_L001_WPT_DATA       35

#define MAX_BUFFER_SIZE 4096

// something magic about 64, garmin driver will not return more than
// 64 at a time.  If you read less than 64 bytes the next read will
// just get the last of the 64 byte buffer.
#define ASYNC_DATA_SIZE 64


// This is the packet format to/from the Garmin USB
#pragma pack(1)
typedef struct {
    unsigned char  mPacketType;
    unsigned char  mReserved1;
    unsigned short mReserved2;
    unsigned short mPacketId;
    unsigned short mReserved3;
    unsigned long  mDataSize;
    char  mData[MAX_BUFFER_SIZE];
} Packet_t;

// This is the data format of the satellite data from the garmin USB
typedef struct {
	unsigned char  svid;
	unsigned short snr; // 0 - 0xffff
	unsigned char  elev;
	unsigned short azmth;
	unsigned char  status; // bit 0, has ephemeris, 1, has diff correction
                               // bit 2 used in solution
			       // bit 3??
} cpo_sat_data;

/* Garmin D800_Pvt_Date_Type */
// This is the data format of the position data from the garmin USB
typedef struct {
	float alt;  /* altitude above WGS 84 */
	float epe;  /* estimated position error, 2 sigma (meters)  */
	float eph;  /* epe, but horizontal only (meters) */
	float epv;  /* epe but vertical only (meters ) */
	short	fix; /* 0 - failed integrity check
                      * 1 - invalid or unavailable fix
                      * 2 - 2D
                      * 3 - 3D
		      * 4 - 2D Diff
                      * 5 - 3D Diff
                      */
	double	gps_tow; /* gps time  os week (seconds) */
	double	lat;     /* latitude (radians) */
	double	lon;     /* longitude (radians) */
	float	lon_vel; /* velocity east (meters/second) */
	float	lat_vel; /* velocity north (meters/second) */
	float	alt_vel; /* velocity up (meters/sec) */
	float	msl_hght; /* height of WGS 84 above MSL */
	short	leap_sec; /* diff between GPS and UTC (seconds) */
	long	grmn_days;
} cpo_pvt_data;

typedef struct {
	unsigned long cycles;
	double	 pr;
	unsigned short phase;
	char slp_dtct;
	unsigned char snr_dbhz;
	char  svid;
	char valid;
} cpo_rcv_sv_data;

typedef struct {
	double rcvr_tow;
	short	rcvr_wn;
	cpo_rcv_sv_data sv[MAXCHANNELS];
} cpo_rcv_data;


// useful funcs to read/write ints, only tested on Intel byte order
//  floats and doubles are Intel order only...
static inline void set_int(unsigned char *buf, int value)
{
        buf[0] = 0xFF & value;
        buf[1] = 0xFF & (value >> 8);
        buf[2] = 0xFF & (value >> 16);
        buf[3] = 0xFF & (value >> 24);
}

static inline int get_short(const unsigned char *buf)
{
        return  (0xFF & buf[0]) | ((0xFF & buf[1]) << 8);
}

static inline int get_int(const unsigned char *buf)
{
        return  (0xFF & buf[0]) | ((0xFF & buf[1]) << 8) | ((0xFF & buf[2]) << 16) | ((0xFF & buf[3]) << 24);
}

// convert radians to degrees
static inline double  radtodeg( double rad) {
	return ( rad * RAD_2_DEG );
}

static void PrintPacket(struct gps_session_t *session, Packet_t *pkt );
static void SendPacket (struct gps_session_t *session, Packet_t *aPacket );
static int GetPacket (struct gps_session_t *session );

// For debugging, decodes and prints some known packets.
static void PrintPacket(struct gps_session_t *session, Packet_t *pkt)
{
    int maj_ver;
    int min_ver;
    int mode;
    int prod_id;
    int ver;
    time_t time_l = 0;
    unsigned int serial;
    cpo_sat_data *sats = NULL;
    cpo_pvt_data *pvt = NULL;
    struct tm tm;
    char buf[BUFSIZ], *bufp = buf;
    int i = 0, j = 0;
    double track;

    gpsd_report(3, "PrintPacket() ");
    if ( 4096 < pkt->mDataSize) {
	gpsd_report(3, "bogus packet, size too large=%d\n", pkt->mDataSize);
	return;
    }

    switch ( pkt->mPacketType ) {
    case GARMIN_LAYERID_TRANSPORT:
	gpsd_report(3, "Transport ");
	switch( pkt->mPacketId ) {
	case GARMIN_PKTID_TRANSPORT_START_SESSION_REQ:
	    gpsd_report(3, "Start Session req\n");
	    break;
	case GARMIN_PKTID_TRANSPORT_START_SESSION_RESP:
	    gpsd_report(3, "Start Session resp\n");
	    break;
	default:
	    gpsd_report(3, "Packet: Type %d %d %d, ID: %d, Sz: %d\n"
			, pkt->mPacketType
			, pkt->mReserved1
			, pkt->mReserved2
			, pkt->mPacketId
			, pkt->mDataSize);
	    break;
	}
	break;
    case GARMIN_LAYERID_APPL:
	gpsd_report(3, "Appl ");
	switch( pkt->mPacketId ) {
	case GARMIN_PKTID_PRODUCT_RQST:
	    gpsd_report(3, "Product Data req\n");
	    break;
	case GARMIN_PKTID_PRODUCT_DATA:
	    prod_id = get_short(&pkt->mData[0]);
	    ver = get_short(&pkt->mData[2]);
	    maj_ver = ver / 100;
	    min_ver = ver - (maj_ver * 100);
	    gpsd_report(3, "Product Data, sz: %d\n"
			, pkt->mDataSize);
	    gpsd_report(1, "Garmin Product ID: %d, SoftVer: %d.%02d\n"
			, prod_id, maj_ver, min_ver);
	    gpsd_report(1, "Garmin Product Desc: %s\n"
			, &pkt->mData[4]);
	    break;
	case GARMIN_PKTID_PVT_DATA:
	    gpsd_report(3, "PVT Data Sz: %d\n", pkt->mDataSize);

	    pvt = (cpo_pvt_data*)pkt->mData;

	    // 631065600, unix seconds for 31 Dec 1989
	    time_l = 631065600 + (pvt->grmn_days * 86400);
	    time_l -= pvt->leap_sec;
	    // gps_tow is always like x.999 or x.998 so just round it
	    time_l += (time_t) rint(pvt->gps_tow);

	    gpsd_report(5, "time_l: %ld\n", time_l);
	    gmtime_r(&time_l, &tm);
	    session->hours   = tm.tm_hour;
	    session->minutes = tm.tm_min;
	    session->seconds = tm.tm_sec;
	    session->day     = tm.tm_mday;
	    session->month   = tm.tm_mon + 1;
	    session->year    = tm.tm_year + 1900;

	    sprintf(session->gNMEAdata.utc
		    , "%04d/%02d/%dT%02d:%02d:%02dZ"
		    , tm.tm_year + 1900
		    , tm.tm_mon + 1
		    , tm.tm_mday
		    , tm.tm_hour
		    , tm.tm_min
		    , tm.tm_sec);

	    session->gNMEAdata.latitude = radtodeg(pvt->lat);
	    session->gNMEAdata.longitude = radtodeg(pvt->lon);
	    REFRESH(session->gNMEAdata.latlon_stamp);

	    // altitude over WGS84 cnverted to MSL
	    session->gNMEAdata.altitude = pvt->alt + pvt->msl_hght;

	    // geoid separation from WGS 84
	    session->separation = pvt->msl_hght;
	    REFRESH(session->gNMEAdata.altitude_stamp);

	    // esrtimated position error in meters (two sigmas)
	    session->gNMEAdata.epe = pvt->epe;
	    session->gNMEAdata.eph = pvt->eph;
	    session->gNMEAdata.epv = pvt->epv;
	    REFRESH(session->gNMEAdata.epe_quality_stamp);

	    // convert lat/lon to knots
	    session->gNMEAdata.speed
		= hypot(pvt->lon_vel, pvt->lat_vel) * 1.9438445;
	    REFRESH(session->gNMEAdata.speed_stamp);

            // keep climb in meters/sec
	    session->gNMEAdata.climb = pvt->alt_vel;
	    REFRESH(session->gNMEAdata.climb_stamp);

	    track = atan2(pvt->lon_vel, pvt->lat_vel);
	    if (track < 0) {
		track += 2 * PI;
	    }
	    session->gNMEAdata.track = radtodeg(track);
	    REFRESH(session->gNMEAdata.track_stamp);

	    switch ( pvt->fix) {
	    case 0:
	    case 1:
	    default:
		// no fix
		session->gNMEAdata.status = STATUS_NO_FIX;
		session->gNMEAdata.mode = MODE_NO_FIX;
		break;
	    case 2:
		// 2D fix
		session->gNMEAdata.status = STATUS_FIX;
		session->gNMEAdata.mode = MODE_2D;
		break;
	    case 3:
		// 3D fix
		session->gNMEAdata.status = STATUS_FIX;
		session->gNMEAdata.mode = MODE_3D;
		break;
	    case 4:
		// 2D Differential fix
		session->gNMEAdata.status = STATUS_DGPS_FIX;
		session->gNMEAdata.mode = MODE_2D;
		break;
	    case 5:
		// 3D differential fix
		session->gNMEAdata.status = STATUS_DGPS_FIX;
		session->gNMEAdata.mode = MODE_3D;
		break;
	    }

	    gpsd_report(4, "mode %d, status %d\n"
			, session->gNMEAdata.mode
			, session->gNMEAdata.status);
	    REFRESH(session->gNMEAdata.status_stamp);
	    REFRESH(session->gNMEAdata.mode_stamp);


	    gpsd_report(3, "UTC Time: %s\n", session->gNMEAdata.utc);
	    gpsd_report(3, "Alt: %.3f, Epe: %.3f, Eph: %.3f, Epv: %.3f, Fix: %d, Gps_tow: %f, Lat: %.3f, Lon: %.3f, LonVel: %.3f, LatVel: %.3f, AltVel: %.3f, MslHgt: %.3f, Leap: %d, GarminDays: %ld\n"
			, pvt->alt
			, pvt->epe
			, pvt->eph
			, pvt->epv
			, pvt->fix
			, pvt->gps_tow
			, session->gNMEAdata.latitude
			, session->gNMEAdata.longitude
			, pvt->lon_vel
			, pvt->lat_vel
			, pvt->alt_vel
			, pvt->msl_hght
			, pvt->leap_sec
			, pvt->grmn_days);

	    gpsd_binary_fix_dump(session, bufp);
	    bufp += strlen(bufp);
	    break;
	case GARMIN_PKTID_SAT_DATA:
	    gpsd_report(3, "SAT Data Sz: %d\n", pkt->mDataSize);
	    sats = (cpo_sat_data*)pkt->mData;

	    session->gNMEAdata.satellites_used = 0;
	    gpsd_zero_satellites(&session->gNMEAdata);
	    for ( i = 0 ; i < MAXCHANNELS ; i++ )
                session->gNMEAdata.used[i] = 0;
	    for ( i = 0, j = 0 ; i < MAXCHANNELS ; i++, sats++ ) {
		gpsd_report(4,
			    "  Sat %d, snr: %d, elev: %d, Azmth: %d, Stat: %x\n"
			    , sats->svid
			    , sats->snr
			    , sats->elev
			    , sats->azmth
			    , sats->status);

		// busted??
// FIXME!! need to elimate sats over 32
		if ( (32 < sats->svid) || (0 == sats->svid) ) {
		    // Garmin uses 255 for empty
		    // gpsd uses 0 for empty
		    continue;
		}

		session->gNMEAdata.PRN[j] = sats->svid;
		session->gNMEAdata.azimuth[j] = sats->azmth;
		session->gNMEAdata.elevation[j] = sats->elev;
		// snr units??
		// garmin 0 -> 0xffff, NMEA 99 -> 0
		session->gNMEAdata.ss[j]
		    = 99 - ((100 *(long)sats->snr) >> 16);
		if ( sats->status & 4 ) {
		    // used in solution?
		    session->gNMEAdata.used[ session->gNMEAdata.satellites_used++] = sats->svid;
		}
		session->gNMEAdata.satellites++;
		j++;
	    }
	    REFRESH(session->gNMEAdata.satellite_stamp);
	    gpsd_binary_satellite_dump(session, bufp);
	    bufp += strlen(bufp);
	    gpsd_binary_quality_dump(session, bufp+strlen(bufp));
	    bufp += strlen(bufp);
	    break;
	default:
	    gpsd_report(3, "ID: %d, Sz: %d\n"
			, pkt->mPacketId
			, pkt->mDataSize);
	    break;
	}
	break;
    case 75:
	// private
	gpsd_report(3, "Private ");
	switch( pkt->mPacketId ) {
	case PRIV_PKTID_INFO_REQ:
	    gpsd_report(3, "ID: Info Req\n");
	    break;
	case PRIV_PKTID_INFO_RESP:
	    ver = get_int(pkt->mData);
	    maj_ver = ver >> 16;
	    min_ver = ver & 0xffff;
	    mode = get_int(pkt->mData + 4);
	    serial = get_int(pkt->mData + 8);
	    gpsd_report(3, "ID: Info Resp\n");
	    gpsd_report(1, "Garmin USB Driver found, Version %d.%d, Mode: %d, GPS Serial# %u\n"
			,  maj_ver, min_ver, mode, serial);
	    break;
	default:
	    gpsd_report(3, "Packet: ID: %d, Sz: %d\n"
			, pkt->mPacketId
			, pkt->mDataSize);
	    break;
	}
	break;
    default:
	gpsd_report(3, "Packet: Type %d %d %d, ID: %d, Sz: %d\n"
		    , pkt->mPacketType
		    , pkt->mReserved1
		    , pkt->mReserved2
		    , pkt->mPacketId
		    , pkt->mDataSize);
	break;
    }

    if ( bufp != buf ) {
	gpsd_report(3, "%s", buf);
    }
}

//-----------------------------------------------------------------------------
// send a packet in GarminUSB format
static void SendPacket (struct gps_session_t *session, Packet_t *aPacket ) 
{
	long theBytesToWrite = 12 + aPacket->mDataSize;
	long theBytesReturned = 0;

        gpsd_report(4, "SendPacket(), writing %d bytes\n", theBytesToWrite);
        PrintPacket ( session,  aPacket);

	theBytesReturned = write( session->gNMEAdata.gps_fd
		    , aPacket, theBytesToWrite);
	gpsd_report(4, "SendPacket(), wrote %d bytes\n", theBytesReturned);

	// Garmin says:
	// If the packet size was an exact multiple of the USB packet
	// size, we must make a final write call with no data

	// as a practical matter no known pckets are 64 bytes long so
        // this is untested

	// So here goes just in case
	if( 0 == (theBytesToWrite % ASYNC_DATA_SIZE) ) {
		char *n = "";
		theBytesReturned = write( session->gNMEAdata.gps_fd
		    , &n, 0);
	}
}

//-----------------------------------------------------------------------------
// Gets a single packet.
// this is odd, the garmin usb driver will only return 64 bytes, or less
// at a time, no matter what you ask for.
//
// is you ask for less than 64 bytes then the next packet will include
// just the remaining bytes of the last 64 byte packet.
//
// Reading a packet of length Zero signals the end of the entire packet.
//
// The Garmin sample WinXX code also assumes the same behavior, so
// maybe it is something in the USB protocol.
//
// Return: 0 = got a good packet
//         -1 = error
//         1 = got partial packet
static int GetPacket (struct gps_session_t *session ) 
{
    Packet_t *thePacket = (Packet_t*)session->GarminBuffer;
    struct timespec delay, rem;

    memset( session->GarminBuffer, 0, sizeof(session->GarminBuffer));
    session->GarminBufferLen = 0;

    gpsd_report(4, "GetPacket()\n");

    for( ; ; ) {
	// Read async data until the driver returns less than the
	// max async data size, which signifies the end of a packet

	// not optimal, but given the speed and packet nature of
	// the USB not too bad for a start
	long theBytesReturned = 0;

	theBytesReturned = read(session->gNMEAdata.gps_fd
		, &session->GarminBuffer[session->GarminBufferLen]
		, ASYNC_DATA_SIZE);
	if ( !theBytesReturned ) {
	    // zero length read is a flag for got the whole packet
            break;
	} else if ( 0 >  theBytesReturned ) {
	    // read error...
	    gpsd_report(0, "GetPacket() read error=%d, errno=%d\n"
		, theBytesReturned, errno);
	    continue;
	}
	gpsd_report(5, "got %d bytes\n", theBytesReturned);

	session->GarminBufferLen += theBytesReturned;
	if ( 256 <=  session->GarminBufferLen ) {
	    // really bad read error...
	    session->GarminBufferLen = 0;
	    gpsd_report(3, "GetPacket() packet too long!\n");
	    break;
	}

	delay.tv_sec = 0;
	delay.tv_nsec = 3330000L;
	while (nanosleep(&delay, &rem) < 0)
	    continue;

    }
    gpsd_report(5, "GotPacket() sz=%d \n", session->GarminBufferLen);
    return 0;
}

/*
 * garmin_probe()
 *
 * return 1 if garmin_gps device found
 * return 0 if not
 */
static int garmin_probe(struct gps_session_t *session)
{

    Packet_t *thePacket = (Packet_t*)session->GarminBuffer;
    char buffer[256];
    fd_set fds, rfds;
    struct timeval tv;
    int sel_ret = 0;
    int ok = 0;
    FILE *fp;
    int i;

    gpsd_report(1, "garmin_init\n");

    // check for USB serial drivers
    // very Linux specific

    if ((fp = fopen( "/proc/tty/driver/usbserial", "r")) == NULL) {
	gpsd_report(2, "No USB serial drivers found.\n");
	return 0;
    } else {
        // try to find garmin_gps driver
	while ( fgets( buffer, sizeof(buffer), fp) ) {
		if ( strstr( buffer, "garmin_gps") ) {
			// yes, the garmin_gps driver is active
			ok = 1;
			break;
		}
	}
    }
    (void) fclose(fp);
    if ( ! ok ) {
	gpsd_report(2, "garmin_gps not active.\n");
	return 0;
    }

    // set Mode 0
    set_int(buffer, GARMIN_LAYERID_PRIVATE);
    set_int(buffer+4, PRIV_PKTID_SET_MODE);
    set_int(buffer+8, 4); // data length 4
    set_int(buffer+12, 0); // mode 0

    gpsd_report(3, "Set garmin_gps driver mode = 0\n");
    SendPacket( session,  (Packet_t*) buffer);
    // expect no return packet !?

    // get Version info
    gpsd_report(3, "Get garmin_gps driver version\n");
    set_int(buffer, GARMIN_LAYERID_PRIVATE);
    set_int(buffer+4, PRIV_PKTID_INFO_REQ);
    set_int(buffer+8, 0); // data length 0

    SendPacket(session,  (Packet_t*) buffer);

    // get and print the driver Version info

    FD_ZERO(&fds); 
    FD_SET(session->gNMEAdata.gps_fd, &fds);

    // Wait, nicely, until the device returns the Version info
    // Toss any other packets, up to 4
    ok = 0;
    for( i = 0 ; i < 4 ; i++ ) {
        memcpy((char *)&rfds, (char *)&fds, sizeof(rfds));

	tv.tv_sec = 1; tv.tv_usec = 0;
	sel_ret = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
	if (sel_ret < 0) {
	    if (errno == EINTR)
		continue;
	    gpsd_report(0, "select: %s\n", strerror(errno));
	    return(0);
	} else if ( sel_ret == 0 ) {
	    gpsd_report(3, "garmin_probe() timeout\n");
	    return(0);
        }
	if ( !GetPacket( session ) ) {
	    PrintPacket(session, (Packet_t*)session->GarminBuffer);

	    if( ( 75 == thePacket->mPacketType)
	        && (PRIV_PKTID_INFO_RESP == thePacket->mPacketId) ) {
                ok = 1;
	        break;
	    }
	}
    }

    if ( ! ok ) {
	gpsd_report(2, "Garmin driver never answeredi to INFO_REQ.\n");
	return 0;
    }
    // Tell the device that we are starting a session.
    gpsd_report(3, "Send Garmin Start Session\n");

    set_int(buffer, GARMIN_LAYERID_TRANSPORT);
    set_int(buffer+4, GARMIN_PKTID_TRANSPORT_START_SESSION_REQ);
    set_int(buffer+8, 0); // data length 0

    SendPacket(session,  (Packet_t*) buffer);

    // Wait until the device is ready to the start the session
    // Toss any other packets, up to 4
    ok = 0;
    for( i = 0 ; i < 4 ; i++ ) {
        memcpy((char *)&rfds, (char *)&fds, sizeof(rfds));

	tv.tv_sec = 1; tv.tv_usec = 0;
	sel_ret = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
	if (sel_ret < 0) {
	    if (errno == EINTR)
		continue;
	    gpsd_report(0, "select: %s\n", strerror(errno));
	    return(0);
	} else if ( sel_ret == 0 ) {
	    gpsd_report(3, "garmin_probe() timeout\n");
	    return(0);
        }
	if ( !GetPacket( session ) ) {
	    PrintPacket(session, (Packet_t*)session->GarminBuffer);

	    if( (GARMIN_LAYERID_TRANSPORT == thePacket->mPacketType)
	        && (GARMIN_PKTID_TRANSPORT_START_SESSION_RESP
		    == thePacket->mPacketId) ) {
                ok = 1;
	        break;
	    }
	}
    }
    if ( ! ok ) {
	gpsd_report(2, "Garmin driver never answered to START_SESSION.\n");
	return 0;
    }

    // Tell the device to send product data
    gpsd_report(3, "Get Garmin Product Data\n");

    set_int(buffer, GARMIN_LAYERID_APPL);
    set_int(buffer+4, GARMIN_PKTID_PRODUCT_RQST);
    set_int(buffer+8, 0); // data length 0

    SendPacket(session,  (Packet_t*) buffer);

    // Get the product data packet
    // Toss any other packets, up to 4
    ok = 0;
    for( i = 0 ; i < 4 ; i++ ) {
        memcpy((char *)&rfds, (char *)&fds, sizeof(rfds));

	tv.tv_sec = 1; tv.tv_usec = 0;
	sel_ret = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
	if (sel_ret < 0) {
	    if (errno == EINTR)
		continue;
	    gpsd_report(0, "select: %s\n", strerror(errno));
	    return(0);
	} else if ( sel_ret == 0 ) {
	    gpsd_report(3, "garmin_probe() timeout\n");
	    return(0);
        }
	if ( !GetPacket( session ) ) {
	    PrintPacket(session, (Packet_t*)session->GarminBuffer);

	    if( (GARMIN_LAYERID_APPL == thePacket->mPacketType)
	        && ( GARMIN_PKTID_PRODUCT_DATA == thePacket->mPacketId) ) {
    		ok = 1;
	        break;
	    }
	}
    }

    if ( ! ok ) {
	gpsd_report(2, "Garmin driver never answered to PRODUCT_DATA.\n");
	return 0;
    }
    // turn on PVT data 49
    gpsd_report(3, "Set Garmin to send reports every 1 second\n");

    set_int(buffer, GARMIN_LAYERID_APPL);
    set_int(buffer+4, GARMIN_PKTID_L001_COMMAND_DATA);
    set_int(buffer+8, 2); // data length 2
    set_int(buffer+12, 49); //  49, CMND_START_PVT_DATA

    SendPacket(session,  (Packet_t*) buffer);

    // turn on RMD data 110
    //set_int(buffer, GARMIN_LAYERID_APPL);
    //set_int(buffer+4, GARMIN_PKTID_L001_COMMAND_DATA);
    //set_int(buffer+8, 2); // data length 2
    //set_int(buffer+12, 110); // 110, CMND_START_ Rcv Measurement Data

    //SendPacket(session,  (Packet_t*) buffer);
    return(1);
}

/*
 * garmin_init()
 *
 * init a garmin_gps device,
 * session->gNMEAdata.gps_fd is assumed to already be open.
 *
 * the garmin_gps driver ignores all termios, baud rates, etc. so
 * any twiddling of that previously done is harmless.
 *
 * gps_fd was opened in NDELAY mode so be careful about reads.
 */
static void garmin_init(struct gps_session_t *session)
{
	int ret;

	gpsd_report(5, "to garmin_probe()\n");
	ret = garmin_probe( session );
	gpsd_report(3, "from garmin_probe() = %d\n", ret);
}

static void garmin_handle_input(struct gps_session_t *session)
{
	if ( !GetPacket( session ) ) {
		PrintPacket(session, (Packet_t*)session->GarminBuffer);
	}
}

/* caller needs to specify a wrapup function */

/* this is everything we export */
struct gps_type_t garmin_binary =
{
    "Garmin binary",	/* full name of type */
    NULL,		/* only switched to by some other driver */
    garmin_probe,	/* how to detect this at startup time */
    garmin_init,	/* initialize the device */
    garmin_handle_input,/* read and parse message packets */
    NULL,		/* send DGPS correction */
    NULL,		/* no speed switcher */
    NULL,		/* caller needs to supply a close hook */
    1,			/* updates every second */
};

#endif /* GARMIN_ENABLE */

