/*
 * SiRF packet monitor, originally by Rob Janssen <pe1chl@amsat.org>.
 * Heavily hacked by Eric S. Raymond for use with the gpsd project.
 *
 * Autobauds.  Takes a SiRF chip in NMEA mode to binary mode, if needed.
 * The autobauding code is fairly primitive and can sometimes fail to
 * sync properly.  If that happens, just kill and restart sirfmon.
 *
 * Not shipped with gpsd, but we keep it around as a diagnostic tool
 * to double-check gpsd's SiRF decoder.
 *
 * Useful commands:
 *	n -- switch device to NMEA at current speed and exit.
 *	b -- change baud rate.
 *	l -- start logging packets to specified file.
 *	s -- send hex bytes to device.
 *	q -- quit, leaving device in binary mode.
 *      Ctrl-S -- freeze display.
 *      Ctrl-Q -- unfreese display.
 */
#include <stdio.h>
#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/ioctl.h>

#define BUFLEN		2048

#define START1		0xa0
#define START2		0xa2
#define END1		0xb0
#define END2		0xb3

int LineFd;					/* fd for RS232 line */
int verbose;
int nfix,fix[20];
int rate;
FILE *logfile;

char *verbpat[] =
{
    "#Time:",
    "@R Time:",
    "CSTD: New almanac for",
    "NOTICE: DOP Q Boost",
    "RTC not set",
    "numOfSVs = 0",
    "rtcaj tow ",
    NULL
};

#define getb(off)	(buf[off])
#define getw(off)	((short)((getb(off) << 8) | getb(off+1)))
#define getl(off)	((int)((getw(off) << 16) | (getw(off+2) & 0xffff)))

#define putb(off,b)	{ buf[4+off] = (unsigned char)(b); }
#define putw(off,w)	{ putb(off,(w) >> 8); putb(off+1,w); }
#define putl(off,l)	{ putw(off,(l) >> 16); putw(off+2,l); }

#define RAD2DEG		5.729577795E-7		/* RAD/10^8 to DEG */

void decode_sirf(unsigned char buf[],int len);
void decode_time(int week, int tow);
void decode_ecef(double x, double y, double z, double vx, double vy, double vz);
int openline (char *name,int baud);
int sendpkt (unsigned char *buf,int len);
int readpkt (unsigned char *buf);

static struct termios ttyset;
static WINDOW *mid2win, *mid4win, *mid6win, *mid7win, *mid9win, *mid13win;
static WINDOW *cmdwin, *debugwin;

#define NO_PACKET	0
#define SIRF_PACKET	1
#define NMEA_PACKET	2

static int set_speed(unsigned int speed, unsigned int stopbits)
{
    unsigned int	rate, count, st, state;
    unsigned char buf[300];

    if (speed < 300)
	rate = 0;
    else if (speed < 1200)
      rate =  B300;
    else if (speed < 2400)
      rate =  B1200;
    else if (speed < 4800)
      rate =  B2400;
    else if (speed < 9600)
      rate =  B4800;
    else if (speed < 19200)
      rate =  B9600;
    else if (speed < 38400)
      rate =  B19200;
    else if (speed < 57600)
      rate =  B38400;
    else
      rate =  B57600;

    tcflush(LineFd, TCIOFLUSH);	/* toss stale data */
    cfsetispeed(&ttyset, (speed_t)rate);
    cfsetospeed(&ttyset, (speed_t)rate);
    ttyset.c_cflag &=~ CSIZE;
    ttyset.c_cflag |= (CSIZE & (stopbits==2 ? CS7 : CS8));
    if (tcsetattr(LineFd, TCSANOW, &ttyset) != 0)
	return NO_PACKET;
    tcflush(LineFd, TCIOFLUSH);

    /* sniff for NMEA or SiRF packet */
    state = 0;
    for (count = 0; count < sizeof(buf); count++) {
	if ((st = read(LineFd, buf, 1)) < 0)
	    return 0;
	else
	    count += st;
	if (state == 0) {
	    if (buf[0] == START1)
		state = 1;
	    else if (buf[0] == '$')
		state = 2;
	} else if (state == 1) {
	    if (buf[0] == START2)
		return SIRF_PACKET;
	    else if (buf[0] == '$')
		state = 2;
	    else
		state = 0;
	} else if (state == 2) {
	    if (buf[0] == 'G')
		state = 3;
	    else if (buf[0] == START1)
		state = 1;
	    else
		state = 0;
	} else if (state == 3) {
	    if (buf[0] == 'P')
		return NMEA_PACKET;
	    else if (buf[0] == START1)
		state = 1;
	    else
		state = 0;
	}
    }

    return NO_PACKET;
}

static void nmea_add_checksum(char *sentence)
/* add NMEA checksum to a possibly  *-terminated sentence */
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    if (*p == '$') {
	p++;
	while ( ((c = *p) != '*') && (c != '\0')) {
	    sum ^= c;
	    p++;
	}
	*p++ = '*';
	sprintf(p, "%02X\r\n", sum);
    }
}

static int nmea_send(int fd, const char *fmt, ... )
/* ship a command to the GPS, adding * and correct checksum */
{
    unsigned int status;
    char buf[BUFLEN];
    va_list ap;

    va_start(ap, fmt) ;
    vsnprintf(buf, sizeof(buf)-5, fmt, ap);
    va_end(ap);
    strcat(buf, "*");
    nmea_add_checksum(buf);
    status = write(fd, buf, strlen(buf));
    if (status == strlen(buf)) {
	return status;
    } else {
	return -1;
    }
}

int main (int argc, char **argv)
{
    int len,i,stopbits,bps,speed,v,st,quit = 0;
    char *p;
    fd_set select_set;
    unsigned char buf[BUFLEN];
    char line[80];
    static unsigned int *ip, rates[] = {4800, 9600, 19200, 38400, 57600};

    if (argc < 2) {
	fprintf(stderr,"Usage: %s <tty-device>.\n",argv[0]);
	exit(1);
    }

    if ((LineFd = open(argv[1],O_RDWR)) < 0) {
	perror(argv[1]);
	return 1;
    }
    
    /* Save original terminal parameters */
    if (tcgetattr(LineFd, &ttyset) != 0)
      goto bailout;
    /*
     * Tip from Chris Kuethe: the FIDI chip used in the Trip-Nav
     * 200 (and possibly other USB GPSes) gets completely hosed
     * in the presence of flow control.  Thus, turn off CRTSCTS.
     */
    ttyset.c_cflag &= ~(PARENB | CRTSCTS);
    ttyset.c_cflag |= CREAD | CLOCAL;
    ttyset.c_iflag = ttyset.c_oflag = ttyset.c_lflag = (tcflag_t) 0;
    ttyset.c_oflag = (ONLCR);

    for (stopbits = 1; stopbits <= 2; stopbits++)
	for (ip = rates; ip < rates + sizeof(rates)/sizeof(rates[0]); ip++)
	{
	    fprintf(stderr, "Hunting at speed %d, %dN%d\n",
		    *ip, 9-stopbits, stopbits);
	    if ((st = set_speed(*ip, stopbits)) == SIRF_PACKET)
		goto rate_ok;
	    else if (st == NMEA_PACKET) {
		fprintf(stderr, "Switching to SiRF mode...\n");
		nmea_send(LineFd,"$PSRF100,0,%d,8,1,0", *ip);
		goto rate_ok;
	    }
	}
 bailout:
    fputs("Can't sync up with device!\n", stderr);
    exit(1);
 rate_ok:;
    bps = *ip;

    initscr();
    cbreak();
    noecho();
    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE);

    mid2win   = subwin(stdscr,  6, 80,  0, 0);
    mid4win   = subwin(stdscr, 15, 30,  6, 0);
    mid6win   = subwin(stdscr, 3,  48,  6, 32);
    mid7win   = subwin(stdscr, 4,  48,  9, 32);
    mid9win   = subwin(stdscr, 3,  48, 13, 32);
    mid13win  = subwin(stdscr, 3,  48, 16, 32);
    cmdwin    = subwin(stdscr, 1,  48, 19, 32);
    debugwin  = subwin(stdscr, 0,   0, 21, 0);
    scrollok(debugwin,TRUE);
    wsetscrreg(debugwin, 0, LINES-21);

    wborder(mid2win, 0, 0, 0, 0, 0, 0, 0, 0),
    wattrset(mid2win, A_BOLD);
    wmove(mid2win, 0,1);
    mvwprintw(mid2win, 0, 12, " X "); 
    mvwprintw(mid2win, 0, 21, " Y "); 
    mvwprintw(mid2win, 0, 30, " Z "); 
    mvwprintw(mid2win, 0, 43, " North "); 
    mvwprintw(mid2win, 0, 54, " East "); 
    mvwprintw(mid2win, 0, 67, " Alt "); 

    wmove(mid2win, 1,1);
    wprintw(mid2win, "Pos:                            m                          deg         m");
    wmove(mid2win, 2,1);
    wprintw(mid2win, "Vel:                            m/s                                    m/s");
    wmove(mid2win, 3,1);
    wprintw(mid2win, "Time:                  UTC:                Heading:        deg         m/s");
    wmove(mid2win, 4,1);
    wprintw(mid2win, "DOP:      M1:    M2:    Fix:  ");
    mvwprintw(mid2win, 5, 30, " Packet type 2 ");
    wattrset(mid2win, A_NORMAL);

    wborder(mid6win, 0, 0, 0, 0, 0, 0, 0, 0),
    wattrset(mid6win, A_BOLD);
    mvwprintw(mid6win, 1, 1, "Version:");
    mvwprintw(mid6win, 2, 10, " Packet Type 6 ");
    wattrset(mid6win, A_NORMAL);

    wborder(mid4win, 0, 0, 0, 0, 0, 0, 0, 0),
    wattrset(mid4win, A_BOLD);
    mvwprintw(mid4win, 1, 1, " Ch SV  Az El Stat  C/N");
    for (i = 0; i < 12; i++) {
	mvwprintw(mid4win, i+2, 1, "%2d",i);
    }
    mvwprintw(mid4win, 14, 8, " Packet Type 4 ");
    wattrset(mid4win, A_NORMAL);

    wborder(mid7win, 0, 0, 0, 0, 0, 0, 0, 0),
    wattrset(mid7win, A_BOLD);
    mvwprintw(mid7win, 1, 1,  "SVs: ");
    mvwprintw(mid7win, 1, 9,  "Drift: ");
    mvwprintw(mid7win, 1, 23, "Bias: ");
    mvwprintw(mid7win, 2, 1,  "Estimated GPS Time: ");
    mvwprintw(mid7win, 3, 10, " Packet type 7 ");
    wattrset(mid7win, A_NORMAL);

    wborder(mid9win, 0, 0, 0, 0, 0, 0, 0, 0),
    wattrset(mid9win, A_BOLD);
    mvwprintw(mid9win, 1, 1,  "Max: ");
    mvwprintw(mid9win, 1, 13, "Lat: ");
    mvwprintw(mid9win, 1, 25, "Time: ");
    mvwprintw(mid9win, 1, 39, "MS: ");
    mvwprintw(mid9win, 2, 10, " Packet type 9 ");
    wattrset(mid9win, A_NORMAL);

    wborder(mid13win, 0, 0, 0, 0, 0, 0, 0, 0),
    wattrset(mid13win, A_BOLD);
    mvwprintw(mid13win, 1, 1, "SVs: ");
    mvwprintw(mid13win, 1, 9, "=");
    mvwprintw(mid13win, 2, 10, " Packet type 13 ");
    wattrset(mid13win, A_NORMAL);

    wattrset(stdscr, A_BOLD);
    mvwprintw(stdscr, 20, 40, "RS232: ");
    wattrset(stdscr, A_NORMAL);
    mvwprintw(stdscr, 20, 47, "%4d N %d", bps, stopbits);

    wmove(debugwin,0, 0);

    FD_ZERO(&select_set);

    /* probe for version */
    putb(0, 0x84);
    putb(1, 0x0);
    sendpkt(buf, 2);

    while (!quit)
    {
	wmove(cmdwin, 0,0);
	wprintw(cmdwin, "cmd> ");
	wclrtoeol(cmdwin);
	refresh();
	wrefresh(mid2win);
	wrefresh(mid4win);
	wrefresh(mid6win);
	wrefresh(mid7win);
	wrefresh(mid9win);
	wrefresh(mid13win);
	wrefresh(debugwin);
	wrefresh(cmdwin);

	FD_SET(0,&select_set);
	FD_SET(LineFd,&select_set);

	if (select(LineFd + 1,&select_set,NULL,NULL,NULL) < 0)
	    break;

	if (FD_ISSET(0,&select_set)) {
	    wmove(cmdwin, 0,5);
	    wrefresh(cmdwin);
	    echo();
	    wgetstr(cmdwin, line);
	    noecho();
	    //move(0,0);
	    //clrtoeol();
	    //refresh();
	    wrefresh(mid2win);
	    wrefresh(mid4win);
	    wrefresh(mid6win);
	    wrefresh(mid7win);
	    wrefresh(mid9win);
	    wrefresh(mid13win);
	    wrefresh(debugwin);
	    wrefresh(cmdwin);

	    if ((p = strchr(line,'\r')) != NULL)
		*p = '\0';

	    if (!line[0])
		continue;

	    p = line;

	    while (*p != '\0' && !isspace(*p))
		p++;
	    while (*p != '\0' && isspace(*p))
		p++;

	    switch (line[0])
	    {
	    case 'b':
		v = atoi(line+1);
		for (ip=rates; ip < rates+sizeof(rates)/sizeof(rates[0]); ip++)
		    if (v == *ip)
			goto goodspeed;
		break;
	    goodspeed:
		putb(0, 0x86);
		putl(1, v);		/* new baud rate */
		putb(5, 8);		/* 8 data bits */
		putb(6, stopbits);	/* 1 stop bit */
		putb(7, 0);		/* no parity */
		putb(8, 0);		/* reserved */
		sendpkt(buf, 9);
		usleep(50000);
		set_speed(bps = v, stopbits);
		mvwprintw(stdscr, 20, 47, "%4d N %d", bps, stopbits);
		break;

	    case 'n':				/* switch to NMEA */
		putb(0,0x81);			/* id */
		putb(1,0x02);			/* mode */
		putb(2,0x01);			/* GGA */
		putb(3,0x01);
		putb(4,0x01);			/* GLL */
		putb(5,0x01);
		putb(6,0x01);		  	/* GSA */
		putb(7,0x01);
		putb(8,0x05);			/* GSV */
		putb(9,0x01);
		putb(10,0x01);			/* RNC */
		putb(11,0x01);
		putb(12,0x01);			/* VTG */
		putb(13,0x01);
		putb(14,0x00);			/* unused fields */
		putb(15,0x01);
		putb(16,0x00);
		putb(17,0x01);
		putb(18,0x00);
		putb(19,0x01);
		putb(20,0x00);
		putb(21,0x01);
		putw(22,bps);
		sendpkt(buf,24);
		quit++;
		break;

	    case 'l':				/* open logfile */
		if (logfile != NULL)
		    fclose(logfile);

		logfile = fopen(p,"a");
		break;

	    case 'q':
		quit++;
		break;

	    case 's':
		len = 0;

		while (*p != '\0')
		{
		    sscanf(p,"%x",&v);
		    putb(len,v);
		    len++;
		    while (*p != '\0' && !isspace(*p))
			p++;
		    while (*p != '\0' && isspace(*p))
			p++;
		}

		sendpkt(buf,len);
		break;

	    case 'v':
	    	verbose = (*p == '0')? 0 : 1;
		break;
	    }
	}

	if (/* FD_ISSET(LineFd,&select_set) && */ (len = readpkt(buf)) != EOF)
	    decode_sirf(buf,len);
    }

    if (logfile != NULL)
	fclose(logfile);

    endwin();
    exit(0);
}

/* SiRF high level routines */

void
decode_sirf(buf,len)
unsigned char buf[];
int len;

{
    int i,j,ch,off,cn;

    switch (buf[0])
    {
    case 0x02:		/* Measured Navigation Data */
	wmove(mid2win, 1,6);
	wprintw(mid2win, "%8d %8d %8d",getl(1),getl(5),getl(9));
	wmove(mid2win, 2,6);
	wprintw(mid2win, "%8.1f %8.1f %8.1f",
		(float)getw(13)/8,(float)getw(15)/8,(float)getw(17)/8);
	decode_ecef((double)getl(1),(double)getl(5),(double)getl(9),
		(float)getw(13)/8,(float)getw(15)/8,(float)getw(17)/8);
	wmove(mid2win, 4,6);
	wprintw(mid2win, "%4.1f",(float)getb(20)/5);	/* HDOP */
	wmove(mid2win, 4,15);
	wprintw(mid2win, "%02x",getb(19));		/* Mode 2 */
	wmove(mid2win, 4,22);
	wprintw(mid2win, "%02x",getb(21));		/* Mode 1 */
	decode_time(getw(22),getl(24));
	wmove(mid2win, 4,30);
	nfix = getb(28);
	wprintw(mid2win, "%d",nfix);			/* SVs in fix */
	for (i = 0; i < 12; i++) {	/* SV list */
	    if (i < nfix)
		wprintw(mid2win, "%3d",fix[i] = getb(29+i));
	    else
		wprintw(mid2win, "   ");
	}
	wprintw(debugwin, "MND 0x02=");
	break;

    case 0x04:		/* Measured Tracking Data */
	decode_time(getw(1),getl(3));
	ch = getb(7);
	for (i = 0; i < ch; i++) {
	    int sv,st;
	    
	    off = 8 + 15 * i;
	    wmove(mid4win, i+2, 3);
	    sv = getb(off);
	    wprintw(mid4win, " %3d",sv);

	    wprintw(mid4win, " %3d%3d %04x",(getb(off+1)*3)/2,getb(off+2)/2,getw(off+3));

	    st = ' ';
	    if (getw(off+3) == 0xbf)
		st = 'T';
	    for (j = 0; j < nfix; j++)
		if (sv == fix[j]) {
		    st = 'N';
		    break;
		}

	    cn = 0;

	    for (j = 0; j < 10; j++)
		cn += getb(off+5+j);

	    wprintw(mid4win, "%5.1f %c",(float)cn/10,st);

	    if (sv == 0)			/* not tracking? */
		wprintw(mid4win, "   ");	/* clear other info */
	}
	wprintw(debugwin, "MTD 0x04=");
    	break;

#ifdef __UNUSED__
    case 0x05:		/* raw track data */
	for (off = 1; off < len; off += 51) {
	    ch = getl(off);
	    wmove(mid4win, ch+2, 19);
	    cn = 0;

	    for (j = 0; j < 10; j++)
		cn += getb(off+34+j);

	    printw("%5.1f",(float)cn/10);

	    printw("%9d%3d%5d",getl(off+8),getw(off+12),getw(off+14));
	    printw("%8.5f %10.5f",
	    	(float)getl(off+16)/65536,(float)getl(off+20)/1024);
	}
	wprintw(debugwin, "RTD 0x05=");
    	break;
#endif /* __UNUSED */

    case 0x06:		/* firmware version */
	mvwprintw(mid6win, 1, 10, "%s",buf + 1);
	wprintw(debugwin, "FV  0x06=");
    	break;

    case 0x07:		/* Response - Clock Status Data */
	decode_time(getw(1),getl(3));
	mvwprintw(mid7win, 1, 5,  "%2d", getb(7));	/* SVs */
	mvwprintw(mid7win, 1, 16, "%lu", getl(8));	/* Clock drift */
	mvwprintw(mid7win, 1, 29, "%lu", getl(12));	/* Clock Bias */
	mvwprintw(mid7win, 2, 21, "%lu", getl(16));	/* Estimated Time */
	wprintw(debugwin, "CSD 0x07=");
	break;

#ifdef UNUSED
    case 0x08:		/* 50 BPS data */
	ch = getb(1);
	move(CHANWIN+ch,77);
	printw("A");
	if (verbose) {
	    wprintw(debugwin, "ALM %d (%d):",getb(2),ch);
	    for (off = 3; off < len; off += 4)
		wprintw(debugwin, " %d",getl(off));
	    wprintw(debugwin, "\n");
	}
	wprintw(debugwin, "50B 0x08=");
    	break;
#endif /* __UNUSED */

    case 0x09:		/* Throughput */
	mvwprintw(mid9win, 1, 6,  "%.3f",(float)getw(1)/186);	/*SegStatMax*/
	mvwprintw(mid9win, 1, 18, "%.3f",(float)getw(3)/186);	/*SegStatLat*/
	mvwprintw(mid9win, 1, 31, "%.3f",(float)getw(5)/186);	/*SegStatTime*/
	mvwprintw(mid9win, 1, 42, "%3d",getw(7));	/* Last Millisecond */
	wprintw(debugwin, "THR 0x09=");
    	break;

    case 0x0b:		/* Command Acknowledgement */
	wprintw(debugwin, "ACK 0x0b=");
    	break;

    case 0x0c:		/* Command NAcknowledgement */
	wprintw(debugwin, "NAK 0x0c=");
    	break;

    case 0x0d:		/* Visible List */
	mvwprintw(mid13win, 1, 6, "%d",getb(1));
	wmove(mid13win, 1, 10);
	for (i = 0; i < 12; i++) {
	    if (i < getb(1))
		wprintw(mid13win, " %2d",getb(2 + 5 * i));
	    else
		wprintw(mid13win, "   ");

	}
	wprintw(mid13win, "\n");
	wprintw(debugwin, "VL  0x0d=");
    	break;

    case 0x1b:
	/******************************************************************
	 Not actually documented in any published materials.
	 Here is what Chris Kuethe got from the SiRF folks:

	Start of message
	----------------
	Message ID          1 byte    27
	Correction Source   1 byte    0=None, 1=SBAS, 2=Serial, 3=Beacon,
	4=Software

	total:              2 bytes

	Middle part of message varies if using beacon or other:
	-------------------------------------------------------
	If Beacon:
	Receiver Freq Hz    4 bytes
	Bit rate BPS        1 byte
	Status bit map      1 byte    01=Signal Valid,
				     02=Auto frequency detect
				     04=Auto bit rate detect
	Signal Magnitude    4 bytes   Note: in internal units
	Signal Strength dB  2 bytes   derived from Signal Magnitude
	SNR  dB             2 bytes

	total:             14 bytes

	If Not Beacon:
	Correction Age[12]  1 byte x 12  Age in seconds in same order as follows
	Reserved            2 bytes

	total:             14 bytes

	End of Message
	--------------
	Repeated 12 times (pad with 0 if less than 12 SV corrections):
	SVID                1 byte
	Correction (m)      1 byte

	total               2 x 12 = 24 bytes
	******************************************************************/
	wprintw(debugwin, "DST 0x1b=");
	break;

#ifdef __UNUSED__
    case 0x62:
	attrset(A_BOLD);
	move(2,40);
	printw("%9.5f %9.5f",RAD2DEG*getl(1),RAD2DEG*getl(5));
	move(2,63);
	printw("%8d",getl(9)/1000);

	move(3,63);
	printw("%8.1f",(float)getl(17)/1000);

	move(4,54);
	if (getl(13) > 50) {
	    float heading = RAD2DEG*getl(21);
	    if (heading < 0)
		heading += 360;
	    printw("%5.1f",heading);
	} else
	    printw("  0.0");

	move(4,63);
	printw("%8.1f",(float)getl(13)/1000);
	attrset(A_NORMAL);

	move(5,13);
	printw("%04d-%02d-%02d %02d:%02d:%02d.%02d",
		getw(26),getb(28),getb(29),getb(30),getb(31),
		(unsigned short)getw(32)/1000,
		((unsigned short)getw(32)%1000)/10);
	{
	    struct timeval clk,gps;
	    struct tm tm;

	    gettimeofday(&clk,NULL);

	    memset(&tm,0,sizeof(tm));
	    tm.tm_sec = (unsigned short)getw(32)/1000;
	    tm.tm_min = getb(31);
	    tm.tm_hour = getb(30);
	    tm.tm_mday = getb(29);
	    tm.tm_mon = getb(28) - 1;
	    tm.tm_year = getw(26) - 1900;

	    gps.tv_sec = mktime(&tm);
	    gps.tv_usec = (((unsigned short)getw(32)%1000)/10) * 10000;

	    move(5,2);
	    printw("           ");
	    move(5,2);
#if 1	    
	    printw("%ld",(gps.tv_usec - clk.tv_usec) +
	    		 ((gps.tv_sec - clk.tv_sec) % 3600) * 1000000);
#else
	    printw("%ld %ld %ld %ld",gps.tv_sec % 3600,gps.tv_usec,
	    			     clk.tv_sec % 3600,clk.tv_usec);
#endif
	}
		wprintw(debugwin, "??? 0x62=");
    	break;
#endif /* __UNUSED__ */

    case 0xff:		/* Development Data */
	while (len > 0 && buf[len-1] == '\n')
	    len--;
	while (len > 0 && buf[len-1] == ' ')
	    len--;
	buf[len] = '\0';
	j = 1;
	if (!verbose) {
	    for (i = 0; verbpat[i] != NULL; i++)
		if (!strncmp(buf+1,verbpat[i],strlen(verbpat[i]))) {
		    j = 0;
		    break;
		}
	}
		wprintw(debugwin, "DD  0xff=");
	if (j)
	    wprintw(debugwin, "%s\n",buf+1);
	break;

    default:
	wprintw(debugwin, "    0x%02x=", buf[0]);
	break;
    }

    wprintw(debugwin, "(%d) ", len);
    for (i = 1; i < len; i++)
	wprintw(debugwin, "%02x",buf[i]);
    wprintw(debugwin, "\n");
}

void decode_time(int week, int tow)
{
    int day = tow / 8640000;
    int tod = tow % 8640000;
    int h = tod / 360000;
    int m = tod % 360000;
    int s = m % 6000;

    m = (m - s) / 6000;

    wmove(mid2win, 3,7);
    wprintw(mid2win, "%4d+%9.2f", week, (double)tow/100);
    wmove(mid2win, 3, 29);
    wprintw(mid2win, "%d %02d:%02d:%05.2f", day, h,m,(float)s/100);
}

void
decode_ecef(x,y,z,vx,vy,vz)
double x,y,z,vx,vy,vz;

{
    const double a = 6378137;
    const double f = 1 / 298.257223563;
    const double b = a * (1 - f);
    const double e2 = (a*a - b*b) / (a*a);
    const double e_2 = (a*a - b*b) / (b*b);
    double lambda,p,theta,phi,n,h,vnorth,veast,vup,speed,heading;

    lambda = atan2(y,x);
    p = sqrt(pow(x,2) + pow(y,2));
    theta = atan2(z*a,p*b);
    phi = atan2(z + e_2*b*pow(sin(theta),3),p - e2*a*pow(cos(theta),3));
    n = a / sqrt(1.0 - e2*pow(sin(phi),2));
    h = p / cos(phi) - n;
    vnorth = -vx*sin(phi)*cos(lambda)-vy*sin(phi)*sin(lambda)+vz*cos(phi);
    veast = -vx*sin(lambda)+vy*cos(lambda);
    vup = vx*cos(phi)*cos(lambda)+vy*cos(phi)*sin(lambda)+vz*sin(phi);
    speed = sqrt(pow(vnorth,2) + pow(veast,2));
    heading = atan2(veast,vnorth);
    if (heading < 0)
	heading += 6.283185307;

    wmove(mid2win, 1,40);
    wprintw(mid2win, "%9.5f %9.5f",57.29577795*phi,57.29577795*lambda);
    wmove(mid2win, 1,63);
    wprintw(mid2win, "%8d",(int)h);

    wmove(mid2win, 2,40);
    wprintw(mid2win, "%9.1f %9.1f",vnorth,veast);
    wmove(mid2win, 2,63);
    wprintw(mid2win, "%8.1f",vup);

    wmove(mid2win, 3,54);
    wprintw(mid2win, "%5.1f",57.29577795*heading);
    wmove(mid2win, 3,63);
    wprintw(mid2win, "%8.1f",speed);

    if (logfile != NULL)
	fprintf(logfile,"%d\t%d\t%d\t%d\t%f\t%f\t%.2f\n",
		(int)time(NULL),(int)x,(int)y,(int)z,57.29577795*phi,57.29577795*lambda,h);
}

/* RS232-line routines (initialization and SiRF pkt send/receive) */

int readbyte (void)
{
    static int cnt = 0,pos = 0;
    static unsigned char inbuf[BUFLEN];

    if (pos >= cnt) {
	fd_set select_set;
	struct timeval timeval;

	FD_ZERO(&select_set);
	FD_SET(LineFd,&select_set);
	timeval.tv_sec = 0;
	timeval.tv_usec = 500000;

	if (select(LineFd + 1,&select_set,NULL,NULL,&timeval) < 0)
	    return EOF;

	if (!FD_ISSET(LineFd,&select_set))
	    return EOF;

	usleep(100000);

	if ((cnt = read(LineFd,inbuf,BUFLEN)) <= 0)
	    return EOF;

	pos = 0;
    }

    return inbuf[pos++];
}

int readword(void)
{
    int byte1,byte2;

    if ((byte1 = readbyte()) == EOF || (byte2 = readbyte()) == EOF)
	return EOF;

    return (byte1 << 8) | byte2;
}

int readpkt(unsigned char *buf)
{
    int byte,len,csum,cnt;

    do {
	while ((byte = readbyte()) != START1)
	    if (byte == EOF)
		return EOF;
    } while ((byte = readbyte()) != START2);

    if ((len = readword()) == EOF || len > BUFLEN)
	return EOF;

    csum = 0;
    cnt = len;

    while (cnt-- > 0) {
	if ((byte = readbyte()) == EOF)
	    return EOF;
	*buf++ = byte;
	csum += byte;
    }

    csum &= 0x7fff;

    if (readword() != csum)
	return EOF;

    if (readbyte() != END1 || readbyte() != END2)
	return EOF;

    return len;
}

int sendpkt (unsigned char *buf, int len)
{
    int i,csum;

    putb(-4,START1);			/* start of packet */
    putb(-3,START2);
    putw(-2,len);			/* length */

    csum = 0;
    for (i = 0; i < len; i++)
	csum += buf[4 + i];

    csum &= 0x7fff;
    putw(len,csum);			/* checksum */
    putb(len + 2,END1);			/* end of packet */
    putb(len + 3,END2);
    len += 8;

    if (verbose) {
	wprintw(debugwin, ">>>");
	for (i = 0; i < len; i++)
	    wprintw(debugwin, " %02x",buf[i]);
	wprintw(debugwin, "\n");
    }

    return (write(LineFd,buf,len) == len);
}

/*
Local Variables:
compile-command: "cc -g -O sirfmon.c -lm -lncurses -o sirfmon"
End:
*/
