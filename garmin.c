/* $Id$ */
/*
 * Handle the Garmin binary packet format supported by the USB Garmins
 * tested with the Garmin 18 and other models.  This driver is NOT for
 * serial port connected Garmins, they provide adequate NMEA support.
 *
 * This code is partly from the Garmin IOSDK and partly from the
 * sample code in the Linux garmin_gps driver.
 *
 * This code supports both Garmin on a serial port and USB Garmins.
 *
 * USB Garmins need the Linux garmin_gps driver and will not function
 * without it.  This code has been tested and at least at one time is
 * known to work on big- and little-endian CPUs and 32 and 64 bit cpu
 * modes.
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
 * -D 6 = very excessive details
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
 *      hangs in the fread loop instead of keeping state and returning.
 *      may or may not work on a little-endian machine
 */

#define __USE_POSIX199309 1
#include <time.h> // for nanosleep()

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>

#if defined (HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#if defined(HAVE_STRINGS_H)
#include <strings.h>
#endif

#include "config.h"
#include "gpsd.h"
#include "gps.h"

#ifdef GARMIN_ENABLE

#define GARMIN_LAYERID_TRANSPORT (uint8_t)  0
#define GARMIN_LAYERID_APPL      (uint32_t) 20
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
/* 0x33 '3' */
#define GARMIN_PKTID_PVT_DATA           51
/* 0x72 'r' */
#define GARMIN_PKTID_SAT_DATA           114

#define GARMIN_PKTID_L001_XFER_CMPLT     12
#define GARMIN_PKTID_L001_COMMAND_DATA   10
#define GARMIN_PKTID_L001_DATE_TIME_DATA 14
#define GARMIN_PKTID_L001_RECORDS        27
#define GARMIN_PKTID_L001_WPT_DATA       35

#define	CMND_ABORT			 0
#define	CMND_START_PVT_DATA		 49
#define	CMND_STOP_PVT_DATA		 50
#define	CMND_START_RM_DATA		 110

#define MAX_BUFFER_SIZE 4096

#define GARMIN_CHANNELS	12

// something magic about 64, garmin driver will not return more than
// 64 at a time.  If you read less than 64 bytes the next read will
// just get the last of the 64 byte buffer.
#define ASYNC_DATA_SIZE 64


#pragma pack(1)
// This is the data format of the satellite data from the garmin USB
typedef struct {
	uint8_t  svid;
	uint16_t snr; // 0 - 0xffff
	uint8_t  elev;
	uint16_t azmth;
	uint8_t  status; // bit 0, has ephemeris, 1, has diff correction
                               // bit 2 used in solution
			       // bit 3??
} cpo_sat_data;

/* Garmin D800_Pvt_Date_Type */
// This is the data format of the position data from the garmin USB
typedef struct {
	float alt;  /* altitude above WGS 84 (meters) */
	float epe;  /* estimated position error, 2 sigma (meters)  */
	float eph;  /* epe, but horizontal only (meters) */
	float epv;  /* epe but vertical only (meters ) */
	int16_t	fix; /* 0 - failed integrity check
                      * 1 - invalid or unavailable fix
                      * 2 - 2D
                      * 3 - 3D
		      * 4 - 2D Diff
                      * 5 - 3D Diff
                      */
	double	gps_tow; /* gps time  os week (seconds) */
	double	lat;     /* ->latitude (radians) */
	double	lon;     /* ->longitude (radians) */
	float	lon_vel; /* velocity east (meters/second) */
	float	lat_vel; /* velocity north (meters/second) */
	float	alt_vel; /* velocity up (meters/sec) */
	float	msl_hght; /* height of WGS 84 above MSL (meters) */
	int16_t	leap_sec; /* diff between GPS and UTC (seconds) */
	int32_t	grmn_days;
} cpo_pvt_data;

#ifdef __UNUSED__
typedef struct {
	uint32_t cycles;
	double	 pr;
	uint16_t phase;
	int8_t slp_dtct;
	uint8_t snr_dbhz;
	int8_t  svid;
	int8_t valid;
} cpo_rcv_sv_data;

typedef struct {
	double rcvr_tow;
	int16_t	rcvr_wn;
	cpo_rcv_sv_data sv[GARMIN_CHANNELS];
} cpo_rcv_data;
#endif /* __UNUSED__ */

// This is the packet format to/from the Garmin USB
typedef struct {
    uint8_t  mPacketType;
    uint8_t  mReserved1;
    uint16_t mReserved2;
    uint16_t mPacketId;
    uint16_t mReserved3;
    uint32_t  mDataSize;
    union {
	    int8_t chars[MAX_BUFFER_SIZE];
	    uint8_t uchars[MAX_BUFFER_SIZE];
            cpo_pvt_data pvt;
            cpo_sat_data sats;
    } mData;
} Packet_t;

// useful funcs to read/write ints
//  floats and doubles are Intel order only...
static inline void set_int16(uint8_t *buf, uint32_t value)
{
        buf[0] = (uint8_t)(0x0FF & value);
        buf[1] = (uint8_t)(0x0FF & (value >> 8));
}

static inline void set_int32(uint8_t *buf, uint32_t value)
{
        buf[0] = (uint8_t)(0x0FF & value);
        buf[1] = (uint8_t)(0x0FF & (value >> 8));
        buf[2] = (uint8_t)(0x0FF & (value >> 16));
        buf[3] = (uint8_t)(0x0FF & (value >> 24));
}

static inline uint16_t get_uint16(const uint8_t *buf)
{
        return  (uint16_t)(0xFF & buf[0]) 
		| ((uint16_t)(0xFF & buf[1]) << 8);
}

static inline uint32_t get_int32(const uint8_t *buf)
{
        return  (uint32_t)(0xFF & buf[0]) 
		| ((uint32_t)(0xFF & buf[1]) << 8) 
		| ((uint32_t)(0xFF & buf[2]) << 16) 
		| ((uint32_t)(0xFF & buf[3]) << 24);
}

// convert radians to degrees
static inline double  radtodeg( double rad) {
	return (double)(rad * RAD_2_DEG );
}

static gps_mask_t PrintSERPacket(struct gps_device_t *session, unsigned char pkt_id, int pkt_len, unsigned char *buf );
static gps_mask_t PrintUSBPacket(struct gps_device_t *session, Packet_t *pkt );
static int GetPacket (struct gps_device_t *session );

gps_mask_t PrintSERPacket(struct gps_device_t *session, unsigned char pkt_id
	, int pkt_len, unsigned char *buf ) 
{

    gps_mask_t mask = 0;
    int i = 0, j = 0;
    uint16_t prod_id = 0;
    uint16_t ver = 0;
    int maj_ver;
    int min_ver;
    time_t time_l = 0;
    double track;
    char msg_buf[512] = "";
    char *msg = NULL;
    cpo_sat_data *sats = NULL;
    cpo_pvt_data *pvt = NULL;

    gpsd_report(4, "PrintSERPacket(, %#02x, %#02x, )\n", pkt_id, pkt_len);

    switch( pkt_id ) {
    case GARMIN_PKTID_L001_COMMAND_DATA:
	prod_id = get_uint16(buf);
	switch ( prod_id ) {
	case CMND_ABORT:
	    msg = "Abort current xfer";
	    break;
	case CMND_START_PVT_DATA:
	    msg = "Start Xmit PVT data";
	    break;
	case CMND_STOP_PVT_DATA:
	    msg = "Stop Xmit PVT data";
	    break;
	case CMND_START_RM_DATA:
	    msg = "Start RMD data";
	    break;
	default:
	    (void)snprintf(msg_buf, sizeof(msg_buf), "Unknown: %u", 
			(unsigned int)prod_id);
	    msg = msg_buf;
	    break;
	}
	gpsd_report(3, "Appl, Command Data: %s\n", msg);
	break;
    case GARMIN_PKTID_PRODUCT_RQST:
	gpsd_report(3, "Appl, Product Data req\n");
	break;
    case GARMIN_PKTID_PRODUCT_DATA:
	prod_id = get_uint16(buf);
	ver = get_uint16(&buf[2]);
	maj_ver = (int)(ver / 100);
	min_ver = (int)(ver - (maj_ver * 100));
	gpsd_report(3, "Appl, Product Data, sz: %d\n", pkt_len);
	gpsd_report(1, "Garmin Product ID: %d, SoftVer: %d.%02d\n"
		, prod_id, maj_ver, min_ver);
	gpsd_report(1, "Garmin Product Desc: %s\n"
		, &buf[4]);
	break;
    case GARMIN_PKTID_PVT_DATA:
	gpsd_report(3, "Appl, PVT Data Sz: %d\n", pkt_len);

	pvt = (cpo_pvt_data*) buf;

	// 631065600, unix seconds for 31 Dec 1989 Zulu 
	time_l = (time_t)(631065600 + (pvt->grmn_days * 86400));
	time_l -= pvt->leap_sec;
	session->context->leap_seconds = pvt->leap_sec;
	session->context->valid = LEAP_SECOND_VALID;
	// gps_tow is always like x.999 or x.998 so just round it
	time_l += (time_t) round(pvt->gps_tow);
	session->gpsdata.fix.time 
	  = session->gpsdata.sentence_time 
	  = (double)time_l;
	gpsd_report(5, "time_l: %ld\n", (long int)time_l);

	session->gpsdata.fix.latitude = radtodeg(pvt->lat);
	session->gpsdata.fix.longitude = radtodeg(pvt->lon);

	// altitude over WGS84 converted to MSL
	session->gpsdata.fix.altitude = pvt->alt + pvt->msl_hght;

	// geoid separation from WGS 84
	// gpsd sign is opposite of garmin sign
	session->gpsdata.separation = -pvt->msl_hght;

	// Estimated position error in meters.
	session->gpsdata.epe = pvt->epe * (GPSD_CONFIDENCE/2);
	session->gpsdata.fix.eph = pvt->eph * (GPSD_CONFIDENCE/2);
	session->gpsdata.fix.epv = pvt->epv * (GPSD_CONFIDENCE/2);

	// convert lat/lon to directionless speed
	session->gpsdata.fix.speed = hypot(pvt->lon_vel, pvt->lat_vel);

	// keep climb in meters/sec
	session->gpsdata.fix.climb = pvt->alt_vel;

	track = atan2(pvt->lon_vel, pvt->lat_vel);
	if (track < 0) {
	    track += 2 * PI;
	}
	session->gpsdata.fix.track = radtodeg(track);

	switch ( pvt->fix) {
	case 0:
	case 1:
	default:
	    // no fix
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->gpsdata.fix.mode = MODE_NO_FIX;
	    break;
	case 2:
	    // 2D fix
	    session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.fix.mode = MODE_2D;
	    break;
	case 3:
	    // 3D fix
	    session->gpsdata.status = STATUS_FIX;
	    session->gpsdata.fix.mode = MODE_3D;
	    break;
	case 4:
	    // 2D Differential fix
	    session->gpsdata.status = STATUS_DGPS_FIX;
	    session->gpsdata.fix.mode = MODE_2D;
	    break;
	case 5:
	    // 3D differential fix
	    session->gpsdata.status = STATUS_DGPS_FIX;
	    session->gpsdata.fix.mode = MODE_3D;
	    break;
	}
#ifdef NTPSHM_ENABLE
	if (session->gpsdata.fix.mode > MODE_NO_FIX)
	    (void) ntpshm_put(session, session->gpsdata.fix.time);
#endif /* NTPSHM_ENABLE */

	gpsd_report(4, "Appl, mode %d, status %d\n"
	    , session->gpsdata.fix.mode
	    , session->gpsdata.status);

	gpsd_report(3, "UTC Time: %lf\n", session->gpsdata.fix.time);
	gpsd_report(3
	    , "Geoid Separation (MSL-WGS84): from garmin %lf, calculated %lf\n"
	    , -pvt->msl_hght
	    , wgs84_separation(session->gpsdata.fix.latitude
	    , session->gpsdata.fix.longitude));

	gpsd_report(3, "Alt: %.3f, Epe: %.3f, Eph: %.3f, Epv: %.3f, Fix: %d, Gps_tow: %f, Lat: %.3f, Lon: %.3f, LonVel: %.3f, LatVel: %.3f, AltVel: %.3f, MslHgt: %.3f, Leap: %d, GarminDays: %ld\n"
	    , pvt->alt
	    , pvt->epe
	    , pvt->eph
	    , pvt->epv
	    , pvt->fix
	    , pvt->gps_tow
	    , session->gpsdata.fix.latitude
	    , session->gpsdata.fix.longitude
	    , pvt->lon_vel
	    , pvt->lat_vel
	    , pvt->alt_vel
	    , pvt->msl_hght
	    , pvt->leap_sec
	    , pvt->grmn_days);

	mask |= TIME_SET | LATLON_SET | ALTITUDE_SET | STATUS_SET | MODE_SET | SPEED_SET | TRACK_SET | CLIMB_SET | HERR_SET | VERR_SET | PERR_SET | CYCLE_START_SET;
	break;
    case GARMIN_PKTID_SAT_DATA:
	gpsd_report(3, "Appl, SAT Data Sz: %d\n", pkt_len);
	sats = (cpo_sat_data *)buf;

	session->gpsdata.satellites_used = 0;
	memset(session->gpsdata.used,0,sizeof(session->gpsdata.used));
	gpsd_zero_satellites(&session->gpsdata);
	for ( i = 0, j = 0 ; i < GARMIN_CHANNELS ; i++, sats++ ) {
	    gpsd_report(4,"  Sat %d, snr: %d, elev: %d, Azmth: %d, Stat: %x\n"
		, sats->svid
		, sats->snr
		, sats->elev
		, sats->azmth
		, sats->status);

	    if ( 255 == (int)sats->svid ) {
		// Garmin uses 255 for empty
		// gpsd uses 0 for empty
		continue;
	    }

	    session->gpsdata.PRN[j]       = (int)sats->svid;
	    session->gpsdata.azimuth[j]   = (int)sats->azmth;
	    session->gpsdata.elevation[j] = (int)sats->elev;
	    // snr units??
	    // garmin 0 -> 0xffff, NMEA 99 -> 0
	    session->gpsdata.ss[j]
	        = 99 - (int)((100 *( unsigned long)sats->snr) >> 16);
	    if ( (uint8_t)0 != (sats->status & 4 ) )  {
	        // used in solution?
	        session->gpsdata.used[session->gpsdata.satellites_used++]
		    = (int)sats->svid;
	    }
	    session->gpsdata.satellites++;
	    j++;

	}
	mask |= SATELLITE_SET | USED_SET;
	break;
    case GARMIN_PKTID_PROTOCOL_ARRAY:
	// this packet is never requested, it just comes, in some case
	// after a GARMIN_PKTID_PRODUCT_RQST 
	gpsd_report(3, "Appl, Product Capability, sz: %d\n", pkt_len);
	for ( i = 0; i < pkt_len ; i += 3 ) {
	    gpsd_report(3, "  %c%03d\n", buf[i], get_uint16( &buf[i+1] ) );
	}
	break;
    default:
	gpsd_report(3, "Appl, ID: %d, Sz: %d\n"
		, pkt_id, pkt_len);
	break;
    }
    gpsd_report(3, "PrintSERPacket(, %#02x, %#02x, ) = %#02x\n"
	, pkt_id, pkt_len, mask);
    return mask;
}


/*@ -branchstate @*/
// For debugging, decodes and prints some known packets.
static gps_mask_t PrintUSBPacket(struct gps_device_t *session, Packet_t *pkt)
{
    gps_mask_t mask = 0;
    int maj_ver;
    int min_ver;
    uint32_t mode = 0;
    uint16_t prod_id = 0;
    uint32_t veri = 0;
    uint32_t serial;
    uint32_t mDataSize = get_int32( (uint8_t*)&pkt->mDataSize);

    gpsd_report(3, "PrintUSBPacket()\n");
    if ( 4096 < mDataSize) {
	gpsd_report(3, "bogus packet, size too large=%d\n", mDataSize);
	return 0;
    }

    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag), "%u"
	, (unsigned int)pkt->mPacketType);
    switch ( pkt->mPacketType ) {
    case GARMIN_LAYERID_TRANSPORT:
        /* Garmin USB layer specific */
	switch( pkt->mPacketId ) {
	case GARMIN_PKTID_TRANSPORT_START_SESSION_REQ:
	    gpsd_report(3, "Transport, Start Session req\n");
	    break;
	case GARMIN_PKTID_TRANSPORT_START_SESSION_RESP:
	    mode = get_int32(&pkt->mData.uchars[0]);
	    gpsd_report(3, "Transport, Start Session resp, unit: 0x%x\n"
		, mode);
	    break;
	default:
	    gpsd_report(3, "Transport, Packet: Type %d %d %d, ID: %d, Sz: %d\n"
			, pkt->mPacketType
			, pkt->mReserved1
			, pkt->mReserved2
			, pkt->mPacketId
			, mDataSize);
	    break;
	}
	break;
    case GARMIN_LAYERID_APPL:
        /* raw data transport, shared with Garmin Serial Driver */

        mask = PrintSERPacket(session, (unsigned char)pkt->mPacketId
		,  (int)mDataSize, pkt->mData.uchars );

	break;
    case 75:
	// private, garmin USB kernel driver specific
	switch( pkt->mPacketId ) {
	case PRIV_PKTID_SET_MODE:
	    prod_id = get_uint16(&pkt->mData.uchars[0]);
	    gpsd_report(3, "Private, Set Mode: %d\n", prod_id);
	    break;
	case PRIV_PKTID_INFO_REQ:
	    gpsd_report(3, "Private, ID: Info Req\n");
	    break;
	case PRIV_PKTID_INFO_RESP:
	    veri = get_int32(pkt->mData.uchars);
	    maj_ver = (int)(veri >> 16);
	    min_ver = (int)(veri & 0xffff);
	    mode = get_int32(&pkt->mData.uchars[4]);
	    serial = get_int32(&pkt->mData.uchars[8]);
	    gpsd_report(3, "Private, ID: Info Resp\n");
	    gpsd_report(1, "Garmin USB Driver found, Version %d.%d, Mode: %d, GPS Serial# %u\n"
			,  maj_ver, min_ver, mode, serial);
	    break;
	default:
	    gpsd_report(3, "Private, Packet: ID: %d, Sz: %d\n"
			, pkt->mPacketId
			, mDataSize);
	    break;
	}
	break;
    default:
	gpsd_report(3, "Packet: Type %d %d %d, ID: %d, Sz: %d\n"
		    , pkt->mPacketType
		    , pkt->mReserved1
		    , pkt->mReserved2
		    , pkt->mPacketId
		    , mDataSize);
	break;
    }

    return mask;
}
/*@ +branchstate @*/


/* build and send a packet */
static void Build_Send_Packet( struct gps_device_t *session,
       uint32_t layer_id, uint32_t pkt_id, uint32_t length, uint32_t data ) 
{
        uint8_t *buffer = (uint8_t *)session->driver.garmin.Buffer;
	Packet_t *thePacket = (Packet_t*)buffer;
	ssize_t theBytesReturned = 0;
	ssize_t theBytesToWrite = 12 + length;

	set_int32(buffer, layer_id);
	set_int32(buffer+4, pkt_id);
	set_int32(buffer+8, length); 
        if ( 2 == length ) {
		set_int16(buffer+12, data);
        } else if ( 4 == length ) {
		set_int32(buffer+12, data);
	}

#if 0
        gpsd_report(4, "SendPacket(), writing %d bytes: %s\n"
		, theBytesToWrite, gpsd_hexdump(thePacket, theBytesToWrite));
#endif
        (void)PrintUSBPacket ( session,  thePacket);

	theBytesReturned = write( session->gpsdata.gps_fd
		    , thePacket, theBytesToWrite);
	gpsd_report(4, "SendPacket(), wrote %d bytes\n", theBytesReturned);

	// Garmin says:
	// If the packet size was an exact multiple of the USB packet
	// size, we must make a final write call with no data

	// as a practical matter no known pckets are 64 bytes long so
        // this is untested

	// So here goes just in case
	if( 0 == (theBytesToWrite % ASYNC_DATA_SIZE) ) {
		char *n = "";
		theBytesReturned = write( session->gpsdata.gps_fd
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
// Reading a packet of length Zero, or less than 64, signals the end of 
// the entire packet.
//
// The Garmin sample WinXX code also assumes the same behavior, so
// maybe it is something in the USB protocol.
//
// Return: 0 = got a good packet
//         -1 = error
//         1 = got partial packet
static int GetPacket (struct gps_device_t *session ) 
{
    struct timespec delay, rem;
    int cnt = 0;
    // int x = 0; // for debug dump

    memset( session->driver.garmin.Buffer, 0, sizeof(Packet_t));
    memset( &delay, 0, sizeof(delay));
    session->driver.garmin.BufferLen = 0;
    session->outbuflen = 0;

    gpsd_report(4, "GetPacket()\n");

//delay.tv_sec = 0;
//delay.tv_nsec = 33300000L;
//while (nanosleep(&delay, &rem) < 0)
//    continue;

    for( cnt = 0 ; cnt < 10 ; cnt++ ) {
	// Read async data until the driver returns less than the
	// max async data size, which signifies the end of a packet

	// not optimal, but given the speed and packet nature of
	// the USB not too bad for a start
	ssize_t theBytesReturned = 0;
	uint8_t *buf = (uint8_t *)session->driver.garmin.Buffer;
	Packet_t *thePacket = (Packet_t*)buf;

	theBytesReturned = read(session->gpsdata.gps_fd
		, buf + session->driver.garmin.BufferLen
		, ASYNC_DATA_SIZE);
	// zero byte returned is a legal value and denotes the end of a 
        // binary packet.
        if ( 0 >  theBytesReturned ) {
	    // read error...
            // or EAGAIN, but O_NONBLOCK is never set
	    gpsd_report(0, "GetPacket() read error=%d, errno=%d\n"
		, theBytesReturned, errno);
	    continue;
	}
	gpsd_report(5, "got %d bytes\n", theBytesReturned);

	session->driver.garmin.BufferLen += theBytesReturned;
	if ( 256 <=  session->driver.garmin.BufferLen ) {
	    // really bad read error...
	    gpsd_report(3, "GetPacket() packet too long, %ld > 255 !\n"
		    , session->driver.garmin.BufferLen);
	    session->driver.garmin.BufferLen = 0;
	    break;
	}
	size_t pkt_size = 12 + get_int32((uint8_t*)&thePacket->mDataSize);
	if ( 12 <= session->driver.garmin.BufferLen) {
	    // have enough data to check packet size
	    if ( session->driver.garmin.BufferLen > pkt_size) {
	        // wrong amount of data in buffer
	        gpsd_report(3
		    , "GetPacket() packet size wrong! Packet: %ld, s/b %ld\n"
		    , session->driver.garmin.BufferLen
		    , pkt_size);
	        session->driver.garmin.BufferLen = 0;
	        break;
	    }
	}
	if ( 64 > theBytesReturned ) {
	    // zero length, or short, read is a flag for got the whole packet
            break;
	}
		

	/*@ ignore @*/
	delay.tv_sec = 0;
	delay.tv_nsec = 3330000L;
	while (nanosleep(&delay, &rem) < 0)
	    continue;
	/*@ end @*/
    }
    // dump the individual bytes, debug only
    // for ( x = 0; x < session->driver.garmin.BufferLen; x++ ) {
        // gpsd_report(6, "p[%d] = %x\n", x, session->driver.garmin.Buffer[x]);
    // }
    if ( 10 <= cnt ) {
	    gpsd_report(3, "GetPacket() packet too long or too slow!\n");
	    return -1;
    }

    gpsd_report(5, "GotPacket() sz=%d \n", session->driver.garmin.BufferLen);
    session->outbuflen = session->driver.garmin.BufferLen;
    return 0;
}

/*
 * garmin_probe()
 *
 * return 1 if garmin_gps device found
 * return 0 if not
 */
static bool garmin_probe(struct gps_device_t *session)
{

    Packet_t *thePacket = NULL;
    uint8_t *buffer = NULL;
    fd_set fds, rfds;
    struct timeval tv;
    int sel_ret = 0;
    int ok = 0;
    int i;

    /* check for USB serial drivers -- very Linux-specific */
    if (access("/sys/module/garmin_gps", R_OK) != 0) {
	gpsd_report(5, "garmin_gps not active.\n"); 
        return false;
    }

    /* Save original terminal parameters */
    if (tcgetattr(session->gpsdata.gps_fd,&session->ttyset_old) != 0) {
	gpsd_report(0, "garmin_probe: error getting port attributes: %s\n",
             strerror(errno));
	return false;
    }
    memcpy(&session->ttyset,&session->ttyset_old,sizeof(session->ttyset));

    (void)cfmakeraw(&session->ttyset);

    if (tcsetattr( session->gpsdata.gps_fd, TCIOFLUSH, &session->ttyset) < 0) {
	gpsd_report(0, "garmin_probe: error changing port attributes: %s\n",
             strerror(errno));
	return false;
    }

    /* reset the buffer and buffer length */
    memset( session->driver.garmin.Buffer, 0, sizeof(session->driver.garmin.Buffer) );
    session->driver.garmin.BufferLen = 0;

    if (sizeof(session->driver.garmin.Buffer) < sizeof(Packet_t)) {
	gpsd_report(0, "garmin_probe: Compile error, garmin.Buffer too small.\n",
             strerror(errno));
	return false;
    }

    buffer = (uint8_t *)session->driver.garmin.Buffer;
    thePacket = (Packet_t*)buffer;

    // set Mode 1, mode 0 is broken somewhere past 2.6.14
    // but how?
    gpsd_report(3, "Set garmin_gps driver mode = 0\n");
    Build_Send_Packet( session, GARMIN_LAYERID_PRIVATE
        , PRIV_PKTID_SET_MODE, 4, 0);
    // expect no return packet !?

    // get Version info
    gpsd_report(3, "Get garmin_gps driver version\n");
    Build_Send_Packet(session, GARMIN_LAYERID_PRIVATE, PRIV_PKTID_INFO_REQ
	, 0, 0);

    /* get and print the driver Version info */

    FD_ZERO(&fds); 
    FD_SET(session->gpsdata.gps_fd, &fds);

    /* Wait, nicely, until the device returns the Version info
     * Toss any other packets, up to 4 */
    ok = 0;
    memset( &tv,0,sizeof(tv));
    for( i = 0 ; i < 4 ; i++ ) {
        memcpy((char *)&rfds, (char *)&fds, sizeof(rfds));

	tv.tv_sec = 1; tv.tv_usec = 0;
	sel_ret = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
	if (sel_ret < 0) {
	    if (errno == EINTR)
		continue;
	    gpsd_report(0, "select: %s\n", strerror(errno));
	    return false;
	} else if ( sel_ret == 0 ) {
	    gpsd_report(3, "garmin_probe() timeout, INFO_REQ\n");
	    // restore old terminal settings
            // TCIOFLUSH here causes gpsfake to hang, so skip that
            (void)tcsetattr(session->gpsdata.gps_fd, TCSANOW
		, &session->ttyset_old);
	    return false;
        }
	if ( 0 == GetPacket( session ) ) {
	    (void)PrintUSBPacket(session, thePacket);

	    if( ( (uint8_t)75 == thePacket->mPacketType)
	        && (PRIV_PKTID_INFO_RESP == thePacket->mPacketId) ) {
                ok = 1;
	        break;
	    }
	}
    }

    if ( 0 == ok ) {
	gpsd_report(2, "Garmin driver never answered to INFO_REQ.\n");
	// restore old terminal settings
        (void)tcsetattr(session->gpsdata.gps_fd, TCIOFLUSH
		, &session->ttyset_old);
	return false;
    }
    /* depending on the GARMIN version, the device may spontaneously
       return the Product Capability here */

    /* Tell the device that we are starting a session. */
    gpsd_report(3, "Send Garmin Start Session\n");

    Build_Send_Packet(session, GARMIN_LAYERID_TRANSPORT
        , GARMIN_PKTID_TRANSPORT_START_SESSION_REQ, 0, 0);

    /* Wait until the device is ready to the start the session
     * Toss any other packets, up to 4 */
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
	    gpsd_report(3, "garmin_probe() timeout, START_SESSION\n");
	    // restore old terminal settings
            (void)tcsetattr(session->gpsdata.gps_fd, TCIOFLUSH
		, &session->ttyset_old);
	    return(0);
        }
	if ( 0 == GetPacket( session ) ) {
	    gpsd_report(3, "Got packet waiting for START_SESSION\n");
	    (void)PrintUSBPacket(session, thePacket);

	    if( (GARMIN_LAYERID_TRANSPORT == thePacket->mPacketType)
	        && (GARMIN_PKTID_TRANSPORT_START_SESSION_RESP
		    == thePacket->mPacketId) ) {
                ok = 1;
	        break;
	    }
	}
    }

    if ( 0 == ok ) {
	gpsd_report(2, "Garmin driver never answered to START_SESSION.\n");
	// restore old terminal settings
        (void)tcsetattr(session->gpsdata.gps_fd, TCIOFLUSH
		, &session->ttyset_old);
	return false;
    }

    // Tell the device to send product data
    gpsd_report(3, "Get Garmin Product Data\n");

    Build_Send_Packet(session, GARMIN_LAYERID_APPL
        , GARMIN_PKTID_PRODUCT_RQST, 0, 0);

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
	    return false;
	} else if ( sel_ret == 0 ) {
	    gpsd_report(3, "garmin_probe() timeout, PRODUCT_DATA\n");
	    // restore old terminal settings
            (void)tcsetattr(session->gpsdata.gps_fd, TCIOFLUSH
		, &session->ttyset_old);
	    return false;
        }
	if ( 0 == GetPacket( session ) ) {
	    (void)PrintUSBPacket(session, thePacket);

	    if( (GARMIN_LAYERID_APPL == (uint32_t)thePacket->mPacketType)
	        && ( GARMIN_PKTID_PRODUCT_DATA == thePacket->mPacketId) ) {
    		ok = 1;
	        break;
	    }
	}
    }

    if ( 0 == ok ) {
	gpsd_report(2, "Garmin driver never answered to PRODUCT_DATA.\n");
	// restore old terminal settings
        (void)tcsetattr(session->gpsdata.gps_fd, TCIOFLUSH
		, &session->ttyset_old);
	return false;
    }
    return true;
}

/*
 * garmin_init()
 *
 * init a garmin_gps device,
 * session->gpsdata.gps_fd is assumed to already be open.
 *
 * the garmin_gps driver ignores all termios, baud rates, etc. so
 * any twiddling of that previously done is harmless.
 *
 */
static void garmin_init(struct gps_device_t *session)
{
	bool ret;

	gpsd_report(5, "to garmin_probe()\n");
	ret = garmin_probe( session );
        /* FIXME - what if return code was bad */
        /* FIXME - return code is always bad */
	gpsd_report(3, "from garmin_probe() = %d\n", (int)ret);

	// turn on PVT data 49
	gpsd_report(3, "Set Garmin to send reports every 1 second\n");

        Build_Send_Packet(session, GARMIN_LAYERID_APPL
	    , GARMIN_PKTID_L001_COMMAND_DATA, 2, CMND_START_PVT_DATA);

	// turn on RMD data 110
        // Build_Send_Packet(session, GARMIN_LAYERID_APPL
	//      , GARMIN_PKTID_L001_COMMAND_DATA, 2, CMND_START_RM_DATA);
}

static void garmin_close(struct gps_device_t *session UNUSED) 
{
    /* FIXME -- do we need to put the garmin to sleep?  or is closing the port
       sufficient? */
    gpsd_report(3, "garmin_close()\n");
    return;
}

static ssize_t garmin_get_packet(struct gps_device_t *session) 
{
    return (ssize_t)( 0 == GetPacket( session ) ? 1 : 0);
}

static gps_mask_t garmin_usb_parse(struct gps_device_t *session)
{
    gpsd_report(5, "garmin_usb_parse()\n");
    return PrintUSBPacket(session, (Packet_t*)session->driver.garmin.Buffer);
}

/*@ +charint @*/
gps_mask_t garmin_ser_parse(struct gps_device_t *session)
{
    unsigned char *buf = session->outbuffer;
    size_t len = session->outbuflen;
    unsigned char data_buf[MAX_BUFFER_SIZE];
    unsigned char c;
    int i = 0;
    size_t n = 0;
    int data_index = 0;
    int got_dle = 0;
    unsigned char pkt_id = 0;
    unsigned char pkt_len = 0;
    unsigned char chksum = 0;
    gps_mask_t mask = 0;

    gpsd_report(5, "garmin_ser_parse()\n");
    if (  6 > len ) {
	/* WTF? */
        /* minimum packet; <DLE> [pkt id] [length=0] [chksum] <DLE> <STX> */
	gpsd_report(6, "Garmin serial too short: %#2x\n", len);
	return 0;
    }
    /* debug */
    for ( i = 0 ; i < (int)len ; i++ ) {
	gpsd_report(6, "Char: %#02x\n", buf[i]);
    }
    
    if ( '\x10' != buf[0] ) {
	gpsd_report(6, "buf[0] not DLE\n", buf[0]);
        return 0;
    }
    n = 1;
    pkt_id = buf[n++];
    chksum = pkt_id;
    if ( '\x10' == pkt_id ) {
        if ( '\x10' != buf[n++] ) {
	    gpsd_report(6, "Bad pkt_id %#02x\n", pkt_id);
	    return 0;
        }
    }

    pkt_len = buf[n++];
    chksum += pkt_len;
    if ( '\x10' == pkt_len ) {
        if ( '\x10' != buf[n++] ) {
	    gpsd_report(6, "Bad pkt_len %#02x\n", pkt_len);
	    return 0;
        }
    }
    data_index = 0;
    for ( i = 0; i < 256 ; i++ ) {

	if ( pkt_len == data_index )  {
		// got it all
		break;
	}
        if ( len < n + i ) {
	    gpsd_report(6, "Packet too short %#02x < %#0x\n", len, n + i);
	    return 0;
        }
	c = buf[n + i];
        if ( got_dle ) {
	    got_dle = 0;
            if ( '\x10' != c ) {
	        gpsd_report(6, "Bad DLE %#02x\n", c);
	        return 0;
            }
	} else {
            chksum += c;
	    data_buf[ data_index++ ] = c;
            if ( '\x10' == c ) {
		got_dle = 1;
	    }
	}
    }
    /* get checksum */
    if ( len < n + i ) {
        gpsd_report(6, "No checksum, Packet too short %#02x < %#0x\n"
	    , len, n + i);
        return 0;
    }
    c = buf[n + i++];
    chksum += c;
    /* get final DLE */
    if ( len < n + i ) {
        gpsd_report(6, "No final DLE, Packet too short %#02x < %#0x\n"
	    , len, n + i);
        return 0;
    }
    c = buf[n + i++];
    if ( '\x10' != c ) {
	gpsd_report(6, "Final DLE not DLE\n", c);
        return 0;
    }
    /* get final ETX */
    if ( len < n + i ) {
        gpsd_report(6, "No final ETX, Packet too short %#02x < %#0x\n"
	    , len, n + i);
        return 0;
    }
    c = buf[n + i++];
    if ( '\x03' != c ) {
	gpsd_report(6, "Final ETX not ETX\n", c);
        return 0;
    }

    /* debug */
    for ( i = 0 ; i < data_index ; i++ ) {
	gpsd_report(6, "Char: %#02x\n", data_buf[i]);
    }

    gpsd_report(4
	, "garmin_ser_parse() Type: %#02x, Len: %#02x, chksum: %#02x\n"
        , pkt_id, pkt_len, chksum);

    mask = PrintSERPacket(session, pkt_id, pkt_len, data_buf);
    return mask;
}
/*@ -charint @*/

/* this is everything we export */
struct gps_type_t garmin_usb_binary =
{
    .typename       = "Garmin USB binary",	/* full name of type */
    .trigger        = NULL,		/* no trigger, it has a probe */
    .channels       = GARMIN_CHANNELS,	/* consumer-grade GPS */
    .probe          = garmin_probe,	/* how to detect at startup time */
    .initializer    = garmin_init,	/* initialize the device */
    .get_packet     = garmin_get_packet,/* how to grab a packet */
    .parse_packet   = garmin_usb_parse,	/* parse message packets */
    .rtcm_writer    = NULL,		/* don't send DGPS corrections */
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .cycle_chars    = -1,		/* not relevant, no rate switch */
    .wrapup         = garmin_close,	/* close hook */
    .cycle          = 1,		/* updates every second */
};

struct gps_type_t garmin_ser_binary =
{
    .typename       = "Garmin Serial binary",	/* full name of type */
    .trigger        = NULL,		/* no trigger, it has a probe */
    .channels       = GARMIN_CHANNELS,	/* consumer-grade GPS */
    .probe          = NULL,        	/* how to detect at startup time */
    .initializer    = NULL,        	/* initialize the device */
    .get_packet     = packet_get,       /* how to grab a packet */
    .parse_packet   = garmin_ser_parse,	/* parse message packets */
    .rtcm_writer    = NULL,		/* don't send DGPS corrections */
    .speed_switcher = NULL,		/* no speed switcher */
    .mode_switcher  = NULL,		/* no mode switcher */
    .rate_switcher  = NULL,		/* no sample-rate switcher */
    .cycle_chars    = -1,		/* not relevant, no rate switch */
    .wrapup         = NULL,	        /* close hook */
    .cycle          = 1,		/* updates every second */
};

#endif /* GARMIN_ENABLE */

