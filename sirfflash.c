/*
 * This is the SiRF-dependent part of the gpsflash program.
 *
 * If we ever compose our own S-records, dlgsp2.bin looks for this header
 * unsigned char hdr[] = "S00600004844521B\r\n";
 *
 * Copyright (c) 2005 Chris Kuethe <chris.kuethe@gmail.com>
 */

#include "gpsflash.h"

/* From the SiRF protocol manual... may as well be consistent */
#define PROTO_SIRF 0
#define PROTO_NMEA 1

#define BOOST_38400 0
#define BOOST_57600 1
#define BOOST_115200 2

static int		sirfWrite(int, unsigned char *);
static unsigned char	nmea_checksum(unsigned char *);

static int
sirfSendUpdateCmd(int pfd){
    int status;
	unsigned char msg[] =	{0xa0,0xa2,	/* header */
				0x00,0x01,	/* message length */
				0x94,		/* 0x94: firmware update */
				0x00,0x00,	/* checksum */
				0xb0,0xb3};	/* trailer */
	status = sirfWrite(pfd, msg);
	/* wait a moment for the receiver to switch to boot rom */
	sleep(2);
	return status;
}

static int
sirfSendLoader(int pfd, struct termios *term, char *loader, int ls){
	unsigned int x;
	int r, speed = 38400;
	unsigned char boost[] = {'S', BOOST_38400};
	unsigned char *msg;

	if((msg = malloc(ls+10)) == NULL){
		return -1; /* oops. bail out */
	}

#ifdef B115200
	speed = 115200;
	boost[1] = BOOST_115200;
#else
#ifdef B57600
	speed = 57600;
	boost[1] = BOOST_57600;
#endif
#endif

	x = htonl((unsigned int)ls);
	msg[0] = 'S';
	msg[1] = (unsigned char)0;
	memcpy(msg+2, &x, 4); /* length */
	memcpy(msg+6, loader, ls); /* loader */
	memset(msg+6+ls, 0, 4); /* reset vector */
	
	/* send the command to jack up the speed */
	if((r = write(pfd, boost, 2)) != 2)
		return -1; /* oops. bail out */

	/* wait for the serial speed change to take effect */
	tcdrain(pfd);
	usleep(1000);

	/* now set up the serial port at this higher speed */
	serialSpeed(pfd, term, speed);

	/* ship the actual data */
	return binary_send(pfd, (char *)msg, ls+10);
}

static int
sirfSetProto(int pfd, struct termios *term, int speed, int proto){
	int l, r, i;
	int spd[8] = {115200, 57600, 38400, 28800, 19200, 14400, 9600, 4800};
	unsigned char *nmea, *tmp;
	unsigned char sirf[] =	{0xa0,0xa2,	/* header */
				0x00,0x01,	/* message length */
				0xa5,		/* message 0xa5: UART config */
				0x00,0,0, 0,0,0,0, 8,1,0, 0,0, /* port 0 */
				0xff,0,0, 0,0,0,0, 0,0,0, 0,0, /* port 1 */
				0xff,0,0, 0,0,0,0, 0,0,0, 0,0, /* port 2 */
				0xff,0,0, 0,0,0,0, 0,0,0, 0,0, /* port 3 */
				0x00,0x00,	/* checksum */
				0xb0,0xb3};	/* trailer */


	if (serialConfig(pfd, term, 38400) == -1)
	    return -1;

	if((nmea = malloc(32)) == NULL)
		return -1;

	if((tmp = malloc(32)) == NULL)
		return -1;

	bzero(nmea, 32);
	bzero(tmp, 32);
	snprintf((char *)tmp, 31,"PSRF100,%u,%u,8,1,0", speed, proto);
	snprintf((char *)nmea, 31,"%s%s*%02x", "$", tmp, nmea_checksum(tmp));
	l = strlen((char *)nmea);

	sirf[7] = sirf[6] = (char)proto;
	i = htonl(speed); /* borrow "i" to put speed into proper byte order */
	bcopy(&i, sirf+8, 4);

	/* send at whatever baud we're currently using */
	sirfWrite(pfd, sirf);
	if ((r = write(pfd, nmea, l)) != r)
		return -1;
	
	tcdrain(pfd);

	/* now spam the receiver with the config messages */
	for(i = 0; i < 8; i++){
		serialSpeed(pfd, term, spd[i]);
		sirfWrite(pfd, sirf);
		write(pfd, nmea, l);
		tcdrain(pfd);
		usleep(100000);
	}

	serialSpeed(pfd, term, speed);
	tcflush(pfd, TCIOFLUSH);

	return 0;
}

static unsigned char
nmea_checksum(unsigned char *s){
	unsigned char c, r = 0;
	while ((c = *s++))
		r += c;

	return r;
}

static int
sirfWrite(int fd, unsigned char *msg) {
	unsigned int crc;
	size_t i, len;

	len = (size_t)((msg[2] << 8) | msg[3]);

	/* calculate CRC */
	crc = 0;
	for (i = 0; i < len; i++)
	crc += (int)msg[4 + i];
	crc &= 0x7fff;

	/* enter CRC after payload */
	msg[len + 4] = (unsigned char)((crc & 0xff00) >> 8);
	msg[len + 5] = (unsigned char)( crc & 0x00ff);

	errno = 0;
	if (write(fd, msg, len+8) != (len+8))
		return -1;

	(void)tcdrain(fd);
	return 0;
}

static int sirfPortSetup(int fd, struct termios *term)
{
    /* the firware upload defaults to 38k4, so let's go there */
    return sirfSetProto(fd, term, PROTO_SIRF, 38400);
}

static int wait2seconds(int fd)
{
    /* again we wait, this time for our uploaded code to start running */
    return sleep(2);
}

static int wait5seconds(int fd)
{
    /* wait for firmware upload to settle in */
    return sleep(5);
}

static int sirfPortWrapup(int fd, struct termios *term)
{
    /* waitaminnit, and drop back to NMEA@4800 for luser apps */
    return sirfSetProto(fd, term, PROTO_NMEA, 4800);
}

struct flashloader_t sirf_type = {
    /* name of default flashloader */
    .flashloader = "dlgsp2.bin",
    /*
     * I can't imagine a GPS firmware less than 256KB / 2Mbit. The
     * latest build that I have (2.3.2) is 296KB. So 256KB is probably
     * low enough to allow really old firmwares to load.
     *
     * As far as I know, USB receivers have 512KB / 4Mbit of
     * flash. Application note APNT00016 (Alternate Flash Programming
     * Algorithms) says that the S2AR reference design supports 4, 8
     * or 16 Mbit flash memories, but with current firmwares not even
     * using 60% of a 4Mbit flash on a commercial receiver, I'm not
     * going to stress over loading huge images. The define below is
     * 524288 bytes, but that blows up nearly 3 times as S-records.
     * 928K srec -> 296K binary
     */
    .min_firmware_size = 262144,
    .max_firmware_size = 1572864,

    /* a reasonable loader is probably 15K - 20K */
    .min_loader_size = 15440,
    .max_loader_size = 20480,

    /* the command methods */
    .port_setup = sirfPortSetup,	/* before signal blocking */
    .stage1_command = sirfSendUpdateCmd,
    .loader_send  = sirfSendLoader,
    .stage2_command = wait2seconds,
    .firmware_send  = srecord_send,
    .stage3_command = wait5seconds,
    .port_wrapup = sirfPortWrapup,	/* after signals unblock */
};
