/* $Id$ */
#include <sys/types.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#endif /* S_SPLINT_S */
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <pwd.h>
#include <stdbool.h>
#include <math.h>

#include "gpsd_config.h"
#include "gpsd.h"
#include "gps.h"


/* from gpsd.c */
extern struct gps_context_t context;
extern struct gps_device_t channels[MAXDEVICES];
extern struct subscriber_t subscribers[MAXSUBSCRIBERS];

int handle_oldstyle(struct subscriber_t *sub, char *buf, int buflen)
/* interpret a client request; cfd is the socket back to the client */
{
    char reply[BUFSIZ], phrase[BUFSIZ], *p, *stash;
    int i, j;
    struct gps_device_t *newchan;

    (void)strlcpy(reply, "GPSD", BUFSIZ);
    p = buf;
    while (*p != '\0' && p - buf < buflen) {
	phrase[0] = '\0';
	switch (toupper(*p++)) {
	case 'A':
	    if (assign_channel(sub) && have_fix(sub) && sub->fixbuffer.mode == MODE_3D)
		(void)snprintf(phrase, sizeof(phrase), ",A=%.3f",
			sub->fixbuffer.altitude);
	    else
		(void)strlcpy(phrase, ",A=?", BUFSIZ);
	    break;
#ifdef ALLOW_RECONFIGURE
	case 'B':		/* change baud rate */
#ifndef FIXED_PORT_SPEED
	    if (assign_channel(sub) && sub->device->device_type!=NULL && *p=='=' && privileged_user(sub) && !context.readonly) {
		speed_t speed;
		unsigned int stopbits = sub->device->gpsdata.stopbits;
		char parity = (char)sub->device->gpsdata.parity;
		int wordsize = 8;

		speed = (speed_t)atoi(++p);
		while (isdigit(*p))
		    p++;
		while (isspace(*p))
		    p++;
		if (strchr("78", *p)!= NULL) {
		    while (isspace(*p))
			p++;
		    wordsize = (int)(*p++ - '0');
		    if (strchr("NOE", *p)!= NULL) {
			parity = *p++;
			while (isspace(*p))
			    p++;
			if (strchr("12", *p)!=NULL)
			    stopbits = (unsigned int)(*p - '0');
		    }
		}
#ifdef ALLOW_RECONFIGURE
		/* no support for other word sizes yet */
		if (wordsize != (int)(9 - stopbits) && sub->device->device_type->speed_switcher!=NULL)
		    if (sub->device->device_type->speed_switcher(sub->device,
								 speed,
								 parity,
								 (int)stopbits)) {
			/*
			 * Allow the control string time to register at the
			 * GPS before we do the baud rate switch, which
			 * effectively trashes the UART's buffer.
			 *
			 * This definitely fails below 40 milliseconds on a
			 * BU-303b. 50ms is also verified by Chris Kuethe on
			 *	Pharos iGPS360 + GSW 2.3.1ES + prolific
			 *	Rayming TN-200 + GSW 2.3.1 + ftdi
			 *	Rayming TN-200 + GSW 2.3.2 + ftdi
			 * so it looks pretty solid.
			 *
			 * The minimum delay time is probably constant
			 * across any given type of UART.
			 */
			(void)tcdrain(sub->device->gpsdata.gps_fd);
			(void)usleep(50000);
			gpsd_set_speed(sub->device, speed,
				(unsigned char)parity, stopbits);
		    }
#endif /* ALLOW_RECONFIGURE */
	    }
#endif /* FIXED_PORT_SPEED */
	    if (sub->device) {
		if ( sub->device->gpsdata.parity == 0 ) {
			/* zero parity breaks the next snprintf */
			sub->device->gpsdata.parity = (unsigned)'N';
		}
		(void)snprintf(phrase, sizeof(phrase), ",B=%d %u %c %u",
		    (int)gpsd_get_speed(&sub->device->ttyset),
			9 - sub->device->gpsdata.stopbits,
			(int)sub->device->gpsdata.parity,
			sub->device->gpsdata.stopbits);
	    } else {
		(void)strlcpy(phrase, ",B=?", BUFSIZ);
	    }
	    break;
	case 'C':
	    if (!assign_channel(sub) || sub->device->device_type==NULL)
		(void)strlcpy(phrase, ",C=?", BUFSIZ);
	    else {
		const struct gps_type_t *dev = sub->device->device_type;
		if (*p == '=' && privileged_user(sub)) {
		    double cycle = strtod(++p, &p);
		    if (dev->rate_switcher != NULL && cycle >= dev->min_cycle)
			if (dev->rate_switcher(sub->device, cycle))
			    sub->device->gpsdata.cycle = cycle;
		}
		if (dev->rate_switcher == NULL)
		    (void)snprintf(phrase, sizeof(phrase),
				   ",C=%.2f", sub->device->gpsdata.cycle);
		else
		    (void)snprintf(phrase, sizeof(phrase), ",C=%.2f %.2f",
				   sub->device->gpsdata.cycle, sub->device->gpsdata.cycle);
	    }
	    break;
#endif /* ALLOW_RECONFIGURE */
	case 'D':
	    (void)strlcpy(phrase, ",D=", BUFSIZ);
	    if (assign_channel(sub) && isnan(sub->fixbuffer.time)==0)
		(void)unix_to_iso8601(sub->fixbuffer.time,
				phrase+3, sizeof(phrase)-3);
	    else
		(void)strlcat(phrase, "?", BUFSIZ);
	    break;
	case 'E':
	    (void)strlcpy(phrase, ",E=", BUFSIZ);
	    if (assign_channel(sub) && have_fix(sub)) {
#if 0
		/*
		 * Only unpleasant choices here:
		 * 1. Always return ? for EPE (what we now do).
		 * 2. Get this wrong - what we used to do, becvfore
		 *    noticing that the response genweration for this
		 *    obsolete command had not been updated to go with
		 *    fix buffering.
		 * 3. Lift epe into the gps_fix_t structure, for no
		 *    functional reason other than this.
		 *    Unfortunately, this would force a bump in the
		 *    shared-library version.
		 */
		if (isnan(sub->device->gpsdata.epe) == 0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   "%.3f", sub->device->gpsdata.epe);
		else
#endif
		    (void)strlcat(phrase, "?", sizeof(phrase));
		if (isnan(sub->fixbuffer.eph) == 0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f", sub->fixbuffer.eph);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
		if (isnan(sub->fixbuffer.epv) == 0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f", sub->fixbuffer.epv);
		else
		    (void)strlcat(phrase, " ?", sizeof(phrase));
	    } else
		(void)strlcat(phrase, "?", sizeof(phrase));
	    break;
	case 'F':
	    /*@ -branchstate @*/
	    if (*p == '=') {
		p = snarfline(++p, &stash);
		gpsd_report(LOG_INF,"<= client(%d): switching to %s\n",sub_index(sub),stash);
		if ((newchan = find_device(stash))) {
		    /*@i@*/sub->device = newchan;
		    sub->tied = true;
		}
	    }
	    /*@ +branchstate @*/
	    if (sub->device != NULL)
		(void)snprintf(phrase, sizeof(phrase), ",F=%s",
			 sub->device->gpsdata.gps_device);
	    else
		(void)strlcpy(phrase, ",F=?", BUFSIZ);
	    break;
	case 'G':
	    if (*p == '=') {
		gpsd_report(LOG_INF,"<= client(%d): requesting data type %s\n",sub_index(sub),++p);
		if (strncasecmp(p, "rtcm104v2", 7) == 0)
		    sub->requires = RTCM104v2;
		else if (strncasecmp(p, "gps", 3) == 0)
		    sub->requires = GPS;
		else
		    sub->requires = ANY;
		p += strcspn(p, ",\r\n");
	    }
	    (void)assign_channel(sub);
	    if (sub->device==NULL||sub->device->packet.type==BAD_PACKET)
		(void)strlcpy(phrase, ",G=?", BUFSIZ);
	    else if (sub->device->packet.type == RTCM2_PACKET)
		(void)snprintf(phrase, sizeof(phrase), ",G=RTCM104v2");
	    else
		(void)snprintf(phrase, sizeof(phrase), ",G=GPS");
	    break;
	case 'I':
	    if (assign_channel(sub) && sub->device->device_type!=NULL) {
		(void)snprintf(phrase, sizeof(phrase), ",I=%s",
			       gpsd_id(sub->device));
	    } else
		(void)strlcpy(phrase, ",I=?", BUFSIZ);
	    break;
	case 'J':
	    if (*p == '=') ++p;
	    if (*p == '1' || *p == '+') {
		sub->buffer_policy = nocasoc;
		p++;
	    } else if (*p == '0' || *p == '-') {
		sub->buffer_policy = casoc;
		p++;
	    }
	    (void)snprintf(phrase, sizeof(phrase), ",J=%u", sub->buffer_policy);
	    break;
	case 'K':
	    for (j = i = 0; i < MAXDEVICES; i++)
		if (allocated_channel(&channels[i]))
		    j++;
	    (void)snprintf(phrase, sizeof(phrase), ",K=%d ", j);
	    for (i = 0; i < MAXDEVICES; i++) {
		if (allocated_channel(&channels[i]) && strlen(phrase)+strlen(channels[i].gpsdata.gps_device)+1 < sizeof(phrase)) {
		    (void)strlcat(phrase, channels[i].gpsdata.gps_device, BUFSIZ);
		    (void)strlcat(phrase, " ", BUFSIZ);
		}
	    }
	    phrase[strlen(phrase)-1] = '\0';
	    break;
	case 'L':
	    (void)snprintf(phrase, sizeof(phrase), ",L=%d %d %s abcdefgijklmnopqrstuvwxyz", GPSD_API_MAJOR_VERSION, GPSD_API_MINOR_VERSION, VERSION);	//h
	    break;
	case 'M':
	    if (!assign_channel(sub) && (!sub->device || sub->fixbuffer.mode == MODE_NOT_SEEN))
		(void)strlcpy(phrase, ",M=?", BUFSIZ);
	    else
		(void)snprintf(phrase, sizeof(phrase), ",M=%d", sub->fixbuffer.mode);
	    break;
#ifdef ALLOW_RECONFIGURE
	case 'N':
	    if (!assign_channel(sub) || sub->device->device_type == NULL)
		(void)strlcpy(phrase, ",N=?", BUFSIZ);
	    else if (!sub->device->device_type->mode_switcher)
		(void)strlcpy(phrase, ",N=0", BUFSIZ);
#ifdef ALLOW_RECONFIGURE
	    else if (privileged_user(sub) && !context.readonly) {
		if (*p == '=') ++p;
		if (*p == '1' || *p == '+') {
		    sub->device->device_type->mode_switcher(sub->device, 1);
		    p++;
		} else if (*p == '0' || *p == '-') {
		    sub->device->device_type->mode_switcher(sub->device, 0);
		    p++;
		}
	    }
#endif /* ALLOW_RECONFIGURE */
	    if (!sub->device)
		(void)snprintf(phrase, sizeof(phrase), ",N=?");
	    else
		(void)snprintf(phrase, sizeof(phrase), ",N=%u", sub->device->gpsdata.driver_mode);
	    break;
#endif /* ALLOW_RECONFIGURE */
	case 'O':
	    if (!assign_channel(sub) || !have_fix(sub))
		(void)strlcpy(phrase, ",O=?", BUFSIZ);
	    else {
		(void)snprintf(phrase, sizeof(phrase), ",O=%s",
			       sub->device->gpsdata.tag[0]!='\0' ? sub->device->gpsdata.tag : "-");
		if (isnan(sub->fixbuffer.time)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f",
				   sub->fixbuffer.time);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (isnan(sub->fixbuffer.ept)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f",
				   sub->fixbuffer.ept);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (isnan(sub->fixbuffer.latitude)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.9f",
				   sub->fixbuffer.latitude);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (isnan(sub->fixbuffer.longitude)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.9f",
				   sub->fixbuffer.longitude);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (isnan(sub->fixbuffer.altitude)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f",
				   sub->fixbuffer.altitude);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (isnan(sub->fixbuffer.eph)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				  " %.3f",  sub->fixbuffer.eph);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (isnan(sub->fixbuffer.epv)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f",  sub->fixbuffer.epv);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (isnan(sub->fixbuffer.track)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.4f %.3f",
				   sub->fixbuffer.track,
				   sub->fixbuffer.speed);
		else
		    (void)strlcat(phrase, " ? ?", BUFSIZ);
		if (isnan(sub->fixbuffer.climb)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f",
				   sub->fixbuffer.climb);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (isnan(sub->fixbuffer.epd)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.4f",
				   sub->fixbuffer.epd);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (isnan(sub->fixbuffer.eps)==0)
		    (void)snprintf(phrase+strlen(phrase),
			     sizeof(phrase)-strlen(phrase),
			     " %.2f", sub->fixbuffer.eps);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (isnan(sub->fixbuffer.epc)==0)
		    (void)snprintf(phrase+strlen(phrase),
			     sizeof(phrase)-strlen(phrase),
			     " %.2f", sub->fixbuffer.epc);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
		if (sub->fixbuffer.mode > 0)
		    (void)snprintf(phrase+strlen(phrase),
			     sizeof(phrase)-strlen(phrase),
			     " %d", sub->fixbuffer.mode);
		else
		    (void)strlcat(phrase, " ?", BUFSIZ);
	    }
	    break;
	case 'P':
	    if (assign_channel(sub) && have_fix(sub))
		(void)snprintf(phrase, sizeof(phrase), ",P=%.9f %.9f",
			sub->fixbuffer.latitude,
			sub->fixbuffer.longitude);
	    else
		(void)strlcpy(phrase, ",P=?", BUFSIZ);
	    break;
	case 'Q':
#define ZEROIZE(x)	(isnan(x)!=0 ? 0.0 : x)
	    if (assign_channel(sub) &&
		(isnan(sub->device->gpsdata.pdop)==0
		 || isnan(sub->device->gpsdata.hdop)==0
		 || isnan(sub->device->gpsdata.vdop)==0))
		(void)snprintf(phrase, sizeof(phrase), ",Q=%d %.2f %.2f %.2f %.2f %.2f",
			sub->device->gpsdata.satellites_used,
			ZEROIZE(sub->device->gpsdata.pdop),
			ZEROIZE(sub->device->gpsdata.hdop),
			ZEROIZE(sub->device->gpsdata.vdop),
			ZEROIZE(sub->device->gpsdata.tdop),
			ZEROIZE(sub->device->gpsdata.gdop));
	    else
		(void)strlcpy(phrase, ",Q=?", BUFSIZ);
#undef ZEROIZE
	    break;
	case 'R':
	    if (*p == '=') ++p;
	    if (*p == '2') {
		(void)assign_channel(sub);
		sub->raw = 2;
		gpsd_report(LOG_INF, "client(%d) turned on super-raw mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",R=2");
		p++;
	    } else if (*p == '1' || *p == '+') {
		(void)assign_channel(sub);
		sub->raw = 1;
		gpsd_report(LOG_INF, "client(%d) turned on raw mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",R=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		sub->raw = 0;
		gpsd_report(LOG_INF, "client(%d) turned off raw mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",R=0");
		p++;
	    } else if (sub->raw) {
		sub->raw = 0;
		gpsd_report(LOG_INF, "client(%d) turned off raw mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",R=0");
	    } else {
		(void)assign_channel(sub);
		sub->raw = 1;
		gpsd_report(LOG_INF, "client(%d) turned on raw mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",R=1");
	    }
	    break;
	case 'S':
	    if (assign_channel(sub))
		(void)snprintf(phrase, sizeof(phrase), ",S=%d", sub->device->gpsdata.status);
	    else
		(void)strlcpy(phrase, ",S=?", BUFSIZ);
	    break;
	case 'T':
	    if (assign_channel(sub) && have_fix(sub) && isnan(sub->fixbuffer.track)==0)
		(void)snprintf(phrase, sizeof(phrase), ",T=%.4f", sub->fixbuffer.track);
	    else
		(void)strlcpy(phrase, ",T=?", BUFSIZ);
	    break;
	case 'U':
	    if (assign_channel(sub) && have_fix(sub) && sub->fixbuffer.mode == MODE_3D)
		(void)snprintf(phrase, sizeof(phrase), ",U=%.3f", sub->fixbuffer.climb);
	    else
		(void)strlcpy(phrase, ",U=?", BUFSIZ);
	    break;
	case 'V':
	    if (assign_channel(sub) && have_fix(sub) && isnan(sub->fixbuffer.speed)==0)
		(void)snprintf(phrase, sizeof(phrase), ",V=%.3f", sub->fixbuffer.speed * MPS_TO_KNOTS);
	    else
		(void)strlcpy(phrase, ",V=?", BUFSIZ);
	    break;
	case 'W':
	    if (*p == '=') ++p;
	    if (*p == '1' || *p == '+') {
		sub->watcher = true;
		(void)assign_channel(sub);
		(void)snprintf(phrase, sizeof(phrase), ",W=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		sub->watcher = false;
		(void)snprintf(phrase, sizeof(phrase), ",W=0");
		p++;
	    } else if (sub->watcher!=0) {
		sub->watcher = false;
		(void)snprintf(phrase, sizeof(phrase), ",W=0");
	    } else {
		sub->watcher = true;
		(void)assign_channel(sub);
		gpsd_report(LOG_INF, "client(%d) turned on watching\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",W=1");
	    }
	    break;
	case 'X':
	    if (assign_channel(sub) && sub->device != NULL)
		(void)snprintf(phrase, sizeof(phrase), ",X=%f", sub->device->gpsdata.online);
	    else
		(void)strlcpy(phrase, ",X=?", BUFSIZ);
	    break;
	case 'Y':
	    if (assign_channel(sub) && sub->device->gpsdata.satellites > 0) {
		int used, reported = 0;
		(void)strlcpy(phrase, ",Y=", BUFSIZ);
		if (sub->device->gpsdata.tag[0] != '\0')
		    (void)strlcat(phrase, sub->device->gpsdata.tag, BUFSIZ);
		else
		    (void)strlcat(phrase, "-", BUFSIZ);
		if (isnan(sub->device->gpsdata.sentence_time)==0)
		    (void)snprintf(phrase+strlen(phrase),
				   sizeof(phrase)-strlen(phrase),
				   " %.3f ",
				   sub->device->gpsdata.sentence_time);
		else
		    (void)strlcat(phrase, " ? ", BUFSIZ);
		/* insurance against flaky drivers */
		for (i = 0; i < sub->device->gpsdata.satellites; i++)
		    if (sub->device->gpsdata.PRN[i])
			reported++;
		(void)snprintf(phrase+strlen(phrase),
			       sizeof(phrase)-strlen(phrase),
			       "%d:", reported);
		for (i = 0; i < sub->device->gpsdata.satellites; i++) {
		    used = 0;
		    for (j = 0; j < sub->device->gpsdata.satellites_used; j++)
			if (sub->device->gpsdata.used[j] == sub->device->gpsdata.PRN[i]) {
			    used = 1;
			    break;
			}
		    if (sub->device->gpsdata.PRN[i]) {
			(void)snprintf(phrase+strlen(phrase),
				      sizeof(phrase)-strlen(phrase),
				      "%d %d %d %.0f %d:",
				      sub->device->gpsdata.PRN[i],
				      sub->device->gpsdata.elevation[i],sub->device->gpsdata.azimuth[i],
				      sub->device->gpsdata.ss[i],
				      used);
		    }
		}
		if (sub->device->gpsdata.satellites != reported)
		    gpsd_report(LOG_WARN,"Satellite count %d != PRN count %d\n",
				sub->device->gpsdata.satellites, reported);
	    } else
		(void)strlcpy(phrase, ",Y=?", BUFSIZ);
	    break;
	case 'Z':
	    (void)assign_channel(sub);
	    if (*p == '=') ++p;
	    if (sub->device == NULL) {
		(void)snprintf(phrase, sizeof(phrase), ",Z=?");
		p++;
	    } else if (*p == '1' || *p == '+') {
		sub->device->gpsdata.profiling = true;
		gpsd_report(LOG_INF, "client(%d) turned on profiling mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",Z=1");
		p++;
	    } else if (*p == '0' || *p == '-') {
		sub->device->gpsdata.profiling = false;
		gpsd_report(LOG_INF, "client(%d) turned off profiling mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",Z=0");
		p++;
	    } else {
		sub->device->gpsdata.profiling = !sub->device->gpsdata.profiling;
		gpsd_report(LOG_INF, "client(%d) toggled profiling mode\n", sub_index(sub));
		(void)snprintf(phrase, sizeof(phrase), ",Z=%d",
			       (int)sub->device->gpsdata.profiling);
	    }
	    break;
	case '$':
	    if (!assign_channel(sub))
		(void)strlcpy(phrase, ",$=?", BUFSIZ);
	    else if (sub->device->gpsdata.sentence_time!=0)
		(void)snprintf(phrase, sizeof(phrase), ",$=%s %d %lf %lf %lf %lf %lf %lf",
			sub->device->gpsdata.tag,
			(int)sub->device->gpsdata.sentence_length,
			sub->device->gpsdata.sentence_time,
			sub->device->gpsdata.d_xmit_time - sub->device->gpsdata.sentence_time,
			sub->device->gpsdata.d_recv_time - sub->device->gpsdata.sentence_time,
			sub->device->gpsdata.d_decode_time - sub->device->gpsdata.sentence_time,
			sub->device->poll_times[sub_index(sub)] - sub->device->gpsdata.sentence_time,
			timestamp() - sub->device->gpsdata.sentence_time);
	    else
		(void)snprintf(phrase, sizeof(phrase), ",$=%s %d 0 %lf %lf %lf %lf %lf",
			sub->device->gpsdata.tag,
			(int)sub->device->gpsdata.sentence_length,
			sub->device->gpsdata.d_xmit_time,
			sub->device->gpsdata.d_recv_time - sub->device->gpsdata.d_xmit_time,
			sub->device->gpsdata.d_decode_time - sub->device->gpsdata.d_xmit_time,
			sub->device->poll_times[sub_index(sub)] - sub->device->gpsdata.d_xmit_time,
			timestamp() - sub->device->gpsdata.d_xmit_time);
	    break;
	case '\r': case '\n':
	    goto breakout;
	}
	if (strlen(reply) + strlen(phrase) < sizeof(reply) - 1)
	    (void)strlcat(reply, phrase, BUFSIZ);
	else
	    return -1;	/* Buffer would overflow.  Just return an error */
    }
 breakout:
    (void)strlcat(reply, "\r\n", BUFSIZ);

    return (int)throttled_write(sub, reply, (ssize_t)strlen(reply));
}

int handle_gpsd_request(struct subscriber_t *sub, char *buf, int buflen)
{
#ifdef GPSDNG_ENABLE
    if (strncmp(buf, "?TPV", 4) == 0) {
	char reply[BUFSIZ];
	(void)strlcpy(reply, "!TPV={", sizeof(reply));
	if (assign_channel(sub) && have_fix(sub)) {
	    (void)snprintf(reply+strlen(reply),
			   sizeof(reply)- strlen(reply),
			   "\"tag\":\"%s\",",
			   sub->device->gpsdata.tag[0]!='\0' ? sub->device->gpsdata.tag : "-");
	    if (isnan(sub->fixbuffer.time)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"time\":%.3f,",
			       sub->fixbuffer.time);
	    if (isnan(sub->fixbuffer.ept)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"ept\":%.3f,",
			       sub->fixbuffer.ept);
	    if (isnan(sub->fixbuffer.latitude)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"lat\":%.9f,",
			       sub->fixbuffer.latitude);
	    if (isnan(sub->fixbuffer.longitude)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"lon\":%.9f,",
			       sub->fixbuffer.longitude);
	    if (isnan(sub->fixbuffer.altitude)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"alt\":%.3f,",
			       sub->fixbuffer.altitude);
	    if (isnan(sub->fixbuffer.eph)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			      "\"eph\":%.3f,",
			       sub->fixbuffer.eph);
	    if (isnan(sub->fixbuffer.epv)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"epv\":%.3f,",
			       sub->fixbuffer.epv);
	    if (isnan(sub->fixbuffer.track)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"track\":%.4f,",
			       sub->fixbuffer.track);
	    if (isnan(sub->fixbuffer.speed)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"speed\":%.3f,",
			       sub->fixbuffer.speed);
	    if (isnan(sub->fixbuffer.climb)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"climb\":%.3f,",
			       sub->fixbuffer.climb);
	    if (isnan(sub->fixbuffer.epd)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"epd\":%.4f,",
			       sub->fixbuffer.epd);
	    if (isnan(sub->fixbuffer.eps)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"eps\":%.2f,", sub->fixbuffer.eps);
	    if (isnan(sub->fixbuffer.epc)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			 "\"epc\":%.2f,", sub->fixbuffer.epc);
	    if (sub->fixbuffer.mode > 0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"mode\":%d,", sub->fixbuffer.mode);
	}
	if (reply[strlen(reply)-1] == ',')
	    reply[strlen(reply)-1] = '\0';	/* trim trailing comma */
	(void)strlcat(reply, "}\r\n", sizeof(reply)-strlen(reply));
	return (int)throttled_write(sub, reply, (ssize_t)strlen(reply));
    } else if (strncmp(buf, "?SAT", 4) == 0) {
	char reply[BUFSIZ];
	(void)strlcpy(reply, "!SAT={", sizeof(reply));
	if (assign_channel(sub) && sub->device->gpsdata.satellites > 0) {
	    int i, j, used, reported = 0;
	    (void)snprintf(reply+strlen(reply),
			   sizeof(reply)- strlen(reply),
			   "\"tag\":\"%s\",",
			   sub->device->gpsdata.tag[0]!='\0' ? sub->device->gpsdata.tag : "-");
	    if (isnan(sub->device->gpsdata.sentence_time)==0)
		(void)snprintf(reply+strlen(reply),
			       sizeof(reply)-strlen(reply),
			       "\"time\":%.3f ",
			       sub->device->gpsdata.sentence_time);
	    /* insurance against flaky drivers */
	    for (i = 0; i < sub->device->gpsdata.satellites; i++)
		if (sub->device->gpsdata.PRN[i])
		    reported++;
	    (void)snprintf(reply+strlen(reply),
			   sizeof(reply)-strlen(reply),
			   "\"reported\":%d,", reported);
	    if (reported) {
		(void)strlcat(reply, "\"satellites\":[", sizeof(reply));
		for (i = 0; i < reported; i++) {
		    used = 0;
		    for (j = 0; j < sub->device->gpsdata.satellites_used; j++)
			if (sub->device->gpsdata.used[j] == sub->device->gpsdata.PRN[i]) {
			    used = 1;
			    break;
			}
		    if (sub->device->gpsdata.PRN[i]) {
			(void)snprintf(reply+strlen(reply),
				       sizeof(reply)-strlen(reply),
				       "{\"PRN\":%d,\"el\":%d,\"az\":%d,\"ss\":%.0f,\"used\":%s},",
				       sub->device->gpsdata.PRN[i],
				       sub->device->gpsdata.elevation[i],sub->device->gpsdata.azimuth[i],
				       sub->device->gpsdata.ss[i],
				       used ? "true" : "false");
		    }
		}
		reply[strlen(reply)-1] = '\0';	/* trim trailing comma */
		(void)strlcat(reply, "],", sizeof(reply));
	    }
	    if (sub->device->gpsdata.satellites != reported)
		gpsd_report(LOG_WARN,"Satellite count %d != PRN count %d\n",
			    sub->device->gpsdata.satellites, reported);
	}
	if (reply[strlen(reply)-1] == ',')
	    reply[strlen(reply)-1] = '\0';	/* trim trailing comma */
	(void)strlcat(reply, "}\r\n", sizeof(reply)-strlen(reply));
	return (int)throttled_write(sub, reply, (ssize_t)strlen(reply));
    } else  if (buf[0] == '?') {
#define JSON_ERROR_OBJECT	"{\"class\":ERR\",\"msg\":\"Unrecognized request\"}\r\n"
	return (int)throttled_write(sub, JSON_ERROR_OBJECT, (ssize_t)strlen(JSON_ERROR_OBJECT));
#undef JSON_ERROR_OBJECT
    }
#endif /* GPSDNG_ENABLE */
    /* fall back to old-style requests */
    return handle_oldstyle(sub, buf, buflen);
}
