/*
 * Handle the Garmin binary packet format supported by the USB Garmins
 * tested with the Garmin 18
 *
 * This code is partly from the Garmin IOSDK and partly from the
 * sample code in the Linux garmin_gps driver.
 *
 * Protocol info from the:
 *	 GPS18_TechnicalSpecification.pdf
 *	 iop_spec.pdf
 * http://www.garmin.com/support/commProtocol.html
 *
 * bad code by: Gary E. Miller <gem@rellim.com>
 *
 * -D 3 = packet trace
 * -D 4 = packet details
 * -D 5 = more packet details
 *
 * limitations:
 * do not really have from garmin:
 *      pdop, so use epe instead
 *      hdop, so use eph instead
 *      vdop, so use epv instead
 * do not have:
 *	magnetic variation
 *
 * known bugs:
 * won't work on a little-endian machine
 *	Satellties over 32 are being reported.
 *	xgps says "NO FIX" and refuses to show the speed and track.
 *      hangs in the fread loop instead of keeping state and returning.
 */
#include <stdio.h>
#include <stdlib.h>
#define __USE_ISOC99
#define __USE_GNU
#include <math.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "gpsd.h"

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
#define GARMIN_MAXCHANNELS 12

// something magic about 64, garmin driver will not return more than
// 64 at a time.  If you read less than 64 bytes the next read will
// just get the last of the 64 byte buffer.
#define ASYNC_DATA_SIZE 64


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
	float	lon_vel; /* velovity east (meters/second) */
	float	lat_vel; /* velovity north (meters/second) */
	float	alt_vel; /* velovity up (meters/sec) */
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
	cpo_rcv_sv_data sv[GARMIN_MAXCHANNELS];
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
static inline int get_int(const unsigned char *buf)
{
        return  (0xFF & buf[0]) | ((0xFF & buf[1]) << 8) | ((0xFF & buf[2]) << 16) | ((0xFF & buf[3]) << 24);
}

// convert radians to degrees
static inline double  radtodeg( double rad) {
	return ( rad * 180.0 *  M_1_PIl);
}


static double degtodm(double a)
{
    double m, t;
    m = modf(a, &t);
    t = floor(a) * 100 + m * 60;
    return t;
}

static void PrintPacket(struct gps_session_t *session, Packet_t *pkt );
void SendPacket (struct gps_session_t *session, Packet_t *aPacket );
Packet_t* GetPacket (struct gps_session_t *session );

// For debugging, decodes and prints some known packets.
static void PrintPacket(struct gps_session_t *session, Packet_t *pkt ) {
    int maj_ver;
    int min_ver;
    int mode;
    int prod_id;
    int ver;
    time_t time_l = 0;
    unsigned int serial;
    cpo_sat_data *sats = NULL;
    cpo_pvt_data *pvt = NULL;
    struct tm *tm = NULL;
    char buf[BUFSIZE], *bufp = buf, *bufp2 = buf;
    int i = 0, j = 0;
    double track;

    gpsd_report(3, "PrintPacket() ");

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
		prod_id = get_int(&pkt->mData[0]);
		ver = get_int(&pkt->mData[4]);
		gpsd_report(3, "Product Data, sz: %d, ProdID: %d, SoftVer: %d, Desc: %s\n"
			, pkt->mDataSize
			, prod_id, ver, &pkt->mData[8]);
		break;
	case GARMIN_PKTID_PVT_DATA:

		gpsd_report(3, "PVT Data Sz: %d\n", pkt->mDataSize);

		pvt = (cpo_pvt_data*)pkt->mData;

		// 631065600, unix seconds for 31 Dec 1989
		time_l = 631065600 + (pvt->grmn_days * 86400);
		time_l -= pvt->leap_sec;
		// gps_tow is always like x.999 or x.998 so just round it
		//time_l += (long)nearbyint(pvt->gps_tow);
		time_l += lrint(pvt->gps_tow);
		//time_l += pvt->gps_tow;

		gpsd_report(5, "time_l: %ld\n", time_l);
		tm = gmtime(&time_l);
		session->hours   = tm->tm_hour;
		session->minutes = tm->tm_min;
		session->seconds = tm->tm_sec;
		session->day     = tm->tm_mday;
		session->month   = tm->tm_mon + 1;
		session->year    = tm->tm_year + 1900;

		sprintf(session->gNMEAdata.utc
			, "%04d/%02d/%dT%02d:%02d:%02dZ"
			, tm->tm_year + 1900
			, tm->tm_mon + 1
			, tm->tm_mday
			, tm->tm_hour
			, tm->tm_min
			, tm->tm_sec);

		session->gNMEAdata.latitude = radtodeg(pvt->lat);
		session->gNMEAdata.longitude = radtodeg(pvt->lon);
		REFRESH(session->gNMEAdata.latlon_stamp);

                // altitude over WGS84
		session->gNMEAdata.altitude = pvt->alt;
		// geoid separation from WGS 84
		session->separation = pvt->msl_hght;
		REFRESH(session->gNMEAdata.altitude_stamp);

		// do not really have an ?dop, use ep? instead
		session->gNMEAdata.pdop = pvt->epe;
		session->gNMEAdata.hdop = pvt->eph;
		session->gNMEAdata.vdop = pvt->epv;
		REFRESH(session->gNMEAdata.fix_quality_stamp);

		// convert lat/lon in meters/sec to speed in knots
		session->gNMEAdata.speed
		   = hypot(pvt->lon_vel, pvt->lat_vel) * 1.9438445;
		REFRESH(session->gNMEAdata.speed_stamp);

		track = atan2(pvt->lon_vel, pvt->lat_vel);
                if (track < 0) {
                        track += 2 * M_PIl;
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

            // stolen verbatim from zodiac.c
	    if (session->gNMEAdata.mode > 1) {
		sprintf(bufp,
			"$GPGGA,%02d%02d%02d,%f,%c,%f,%c,%d,%02d,%.2f,%.1f,%c,%f,%c,%s,%s*",
		   session->hours,
                   session->minutes,
                   session->seconds,
			degtodm(fabs(session->gNMEAdata.latitude)),
			((session->gNMEAdata.latitude > 0) ? 'N' : 'S'),
			degtodm(fabs(session->gNMEAdata.longitude)),
			((session->gNMEAdata.longitude > 0) ? 'E' : 'W'),
		    session->gNMEAdata.mode,
                    session->gNMEAdata.satellites_used,
                    session->gNMEAdata.hdop,
		    session->gNMEAdata.altitude, 'M',
                    session->separation, 'M', "", "");
		nmea_add_checksum(bufp + 1);
		bufp = bufp + strlen(bufp);
	    }
	    sprintf(bufp,
		    "$GPRMC,%02d%02d%02d,%c,%f,%c,%f,%c,%f,%f,%02d%02d%02d,,*",
		    session->hours, session->minutes, session->seconds,
		    session->gNMEAdata.status ? 'A' : 'V',
                    degtodm(fabs(session->gNMEAdata.latitude)),
		    ((session->gNMEAdata.latitude > 0) ? 'N' : 'S'),
		    degtodm(fabs(session->gNMEAdata.longitude)),
		    ((session->gNMEAdata.longitude > 0) ? 'E' : 'W'),
                    session->gNMEAdata.speed,
		    session->gNMEAdata.track,
                    session->day,
                    session->month,
		    (session->year % 100));
		nmea_add_checksum(bufp + 1);
                // end stolen verbatim from zodiac.c
		break;
            case GARMIN_PKTID_SAT_DATA:
		gpsd_report(3, "SAT Data Sz: %d\n", pkt->mDataSize);
		sats = (cpo_sat_data*)pkt->mData;

		session->gNMEAdata.satellites = 12; // always got data for 12
		session->gNMEAdata.satellites_used = 0;

		// clear used table
		for ( i = 0 ; i < MAXCHANNELS ; i++ ) {
			session->gNMEAdata.used[i] = 0;
		}
		for ( i = 0, j = 0 ; i < GARMIN_MAXCHANNELS ; i++, sats++ ) {
			gpsd_report(4,
			  "  Sat %d, snr: %d, elev: %d, Azmth: %d, Stat: %x\n"
				, sats->svid
				, sats->snr
				, sats->elev
				, sats->azmth
				, sats->status);

			// busted??
// FIXME!! need to elimate sats over 32
			//if ( 255 == sats->svid ) {
				// Garmin uses 255 for empty
                                // gpsd uses 0 for empty
				//sats->svid = 0;
			//}

			session->gNMEAdata.PRN[i] = sats->svid;
			session->gNMEAdata.azimuth[i] = sats->azmth;
			session->gNMEAdata.elevation[i] = sats->elev;
			// snr units??
			// garmin 0 -> 0xffff, NMEA 99 -> 0
			session->gNMEAdata.ss[i]
				= 99 - ((100 *(long)sats->snr) >> 16);
			if ( sats->status & 4 ) {
				// used in solution?
				session->gNMEAdata.used[ session->gNMEAdata.satellites_used++] = sats->svid;
			}
		}
		REFRESH(session->gNMEAdata.satellite_stamp);

		// stolen verbatim from zodiac.c
	    j = (session->gNMEAdata.satellites / 4) + (((session->gNMEAdata.satellites % 4) > 0) ? 1 : 0);

	    for( i = 0 ; i < GARMIN_MAXCHANNELS; i++ ) {
		if (i % 4 == 0)
		    sprintf(bufp, "$GPGSV,%d,%d,%02d", j, (i / 4) + 1, session->gNMEAdata.satellites);
		bufp += strlen(bufp);
		if (i <= session->gNMEAdata.satellites && session->gNMEAdata.elevation[i])
		    sprintf(bufp, ",%02d,%02d,%03d,%02d", session->gNMEAdata.PRN[i],
			    session->gNMEAdata.elevation[i], session->gNMEAdata.azimuth[i], session->gNMEAdata.ss[i]);
		else
		    sprintf(bufp, ",%02d,00,000,%02d,", session->gNMEAdata.PRN[i],
			    session->gNMEAdata.ss[i]);
		bufp += strlen(bufp);
		if (i % 4 == 3) {
		    sprintf(bufp, "*");
		    nmea_add_checksum(bufp2 + 1);
		    bufp += strlen(bufp);
		    bufp2 = bufp;
		}
	    }
	    sprintf(bufp, "$GPGSA,%c,%d,", 'A', session->gNMEAdata.mode);
	    j = 0;
	    for (i = 0; i < MAXCHANNELS; i++) {
		if (session->gNMEAdata.used[i]) {
		    bufp = bufp + strlen(bufp);
		    sprintf(bufp, "%02d,", session->gNMEAdata.PRN[i]);
		    j++;
		}
	    }
	    for (i = j; i < MAXCHANNELS; i++) {
		bufp = bufp + strlen(bufp);
		sprintf(bufp, ",");
	    }
	    bufp = bufp + strlen(bufp);
	    sprintf(bufp, "%.2f,%.2f,%.2f*", session->gNMEAdata.pdop, session->gNMEAdata.hdop,
		    session->gNMEAdata.vdop);
	    nmea_add_checksum(bufp2 + 1);
	    bufp2 = bufp = bufp + strlen(bufp);

                // end stolen verbatim from zodiac.c
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
		    gpsd_report(3, "  Version %d. %d, Mode: %d, Serial:%u\n"
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
	if (session->gNMEAdata.raw_hook) {
	    session->gNMEAdata.raw_hook(buf);
        }
    }

}

//-----------------------------------------------------------------------------
// send a packet in GarminUSB format
void SendPacket (struct gps_session_t *session, Packet_t *aPacket ) {

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
// The Garmin sample WinXX code also assumes the same behavior, so
// maybe it is something in the USB protocol.
Packet_t* GetPacket (struct gps_session_t *session ) {

	Packet_t *thePacket = NULL;
	long theBufferSize = 0;
	unsigned char* theBuffer = 0;
	struct timespec delay, rem;
        unsigned char theTempBuffer[ASYNC_DATA_SIZE];

	gpsd_report(4, "GetPacket()\n");

	for( ; ; ) {
		// Read async data until the driver returns less than the
		// max async data size, which signifies the end of a packet

		// not optimal, but given the speed and packet nature of
                // the USB not too bad for a start
		unsigned char* theNewBuffer = NULL;
		long theBytesReturned = 0;

		theBytesReturned = read(session->gNMEAdata.gps_fd
		    , theTempBuffer, sizeof(theTempBuffer));
		if ( 0 >  theBytesReturned ) {
			// gotta handle theses better, but they happen often
                        // during init
			continue;
		}
		gpsd_report(5, "got %d bytes\n", theBytesReturned);

		theBufferSize += ASYNC_DATA_SIZE;
		theNewBuffer = (unsigned char*) malloc( theBufferSize );
		memcpy( theNewBuffer, theBuffer, theBufferSize - ASYNC_DATA_SIZE );
		memcpy( theNewBuffer + theBufferSize - ASYNC_DATA_SIZE,
		theTempBuffer, ASYNC_DATA_SIZE );

		free( theBuffer );

		theBuffer = theNewBuffer;

		if( theBytesReturned != ASYNC_DATA_SIZE ) {
			thePacket = (Packet_t*) theBuffer;
			break;
		}
		delay.tv_sec = 0;
		delay.tv_nsec = 333000L;
		while (nanosleep(&delay, &rem) < 0)
			;

	}
	gpsd_report(5, "GotPacket() sz=%d \n", theBufferSize);
	return thePacket;
}

/*
 * garmin_init()
 *
 * init a garmin_usb device,
 * session->gNMEAdata.gps_fd is assumed to already be open.
 *
 * the garmin_usb driver ignores all termios, baud rates, etc. so
 * any twiddling of that previously done is harmless.
 *
 * gps_fd was opened in NDELAY mode to be carefull about reads.
 *
 */
static void garmin_init(struct gps_session_t *session)
{

	Packet_t theProductDataPacket
		= { GARMIN_LAYERID_APPL, 0, 0
			, GARMIN_PKTID_PRODUCT_RQST, 0 , 0, "" };
	Packet_t PvtOnPacket
		= { GARMIN_LAYERID_APPL, 0, 0
			, GARMIN_PKTID_L001_COMMAND_DATA, 0 , 2
		, "1" }; // 49, CMND_START_PVT_DATA
#ifdef __UNUSED__
	Packet_t RmdOnPacket
		= { GARMIN_LAYERID_APPL, 0, 0
			, GARMIN_PKTID_L001_COMMAND_DATA, 0 , 2
		, "n" }; // 110, CMND_START_ Rcv Measurement Data
#endif

	Packet_t theStartSessionPacket
		= { 0, 0, 0, GARMIN_PKTID_TRANSPORT_START_SESSION_REQ, 0
			, 0, "" };

	Packet_t* thePacket = 0;

	char buffer[256];


	gpsd_report(1, "garmin_init\n");

	// get Mode 0
        set_int(buffer, GARMIN_LAYERID_PRIVATE);
        set_int(buffer+4, PRIV_PKTID_SET_MODE);
        set_int(buffer+8, 4); // data length 4
        set_int(buffer+12, 0); // mode 0

	gpsd_report(3, "Set garmin_usb driver mode = 0\n");
	SendPacket( session,  (Packet_t*) buffer);
	// expect no return packet !?

	// get Version info
	gpsd_report(3, "Get garmin_usb driver version\n");
        set_int(buffer, GARMIN_LAYERID_PRIVATE);
        set_int(buffer+4, PRIV_PKTID_INFO_REQ);
        set_int(buffer+8, 0); // data length 0

	SendPacket(session,  (Packet_t*) buffer);

	// get and print the driver Version info

	// Wait until the device returns the Version info
	// Toss any other packets
	for( ; ; ) {
		thePacket = GetPacket( session );
		PrintPacket(session,  thePacket);

		if( ( 75 == thePacket->mPacketType)
		    && (PRIV_PKTID_INFO_RESP == thePacket->mPacketId) ) {
			break;
		}
		free( thePacket );
	}
	free( thePacket );

	// Tell the device that we are starting a session.
	gpsd_report(3, "Send Garmin Start Session\n");
	SendPacket( session,  &theStartSessionPacket );

	// Wait until the device is ready to the start the session
	// Toss any other packets
	for( ; ; ) {
		thePacket = GetPacket( session );
		PrintPacket(session,  thePacket);

		if( (GARMIN_LAYERID_TRANSPORT == thePacket->mPacketType)
		    && (GARMIN_PKTID_TRANSPORT_START_SESSION_RESP
                      == thePacket->mPacketId) ) {
			break;
		}
		free( thePacket );
	}
	free( thePacket );

	// Tell the device to send product data
	gpsd_report(3, "Get Garmin Product Data\n");
	SendPacket( session, &theProductDataPacket );

	// Get the product data packet
	// Ignore any other packets on the way
	for( ; ; ) {
		thePacket = GetPacket( session);
		PrintPacket( session, thePacket);

		if( (GARMIN_LAYERID_APPL == thePacket->mPacketType)
		    && ( GARMIN_PKTID_PRODUCT_DATA == thePacket->mPacketId) ) {
			break;
		}
		free( thePacket );
	}

	free( thePacket );

	// turn on PVT data 49
	gpsd_report(3, "Set Garmin to send reports every 1 second\n");
	SendPacket( session, &PvtOnPacket );
	// turn on RMD data 110
	//SendPacket( session, &RmdOnPacket );

}

static void garmin_handle_input(struct gps_session_t *session)
{
	Packet_t *thePacket = NULL;

	thePacket = GetPacket( session );
	PrintPacket( session, thePacket);
	free( thePacket );
}

/* caller needs to specify a wrapup function */

/* this is everything we export */
struct gps_type_t garmin_binary =
{
    'g',		/* select with 'g' */
    "Garmin binary",	/* full name of type */
    NULL,		/* only switched to by some other driver */
    garmin_init,	/* initialize the device */
    NULL,		/* no validator */
    garmin_handle_input,/* read and parse message packets */
    NULL,		/* send DGPS correction */
    NULL,		/* caller needs to supply a close hook */
    9600,		/* bit rate, unused */
    1,			/* stop bits, unused */
    1,			/* updates every second */
};

#endif /* GARMIN_ENABLE */

