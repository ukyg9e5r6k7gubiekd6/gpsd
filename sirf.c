/*
 * Copyright (C) 2003 Arnim Laeuger <arnim.laeuger@gmx.net>
 * Issued under GPL.  Originally part of an unpublished utility
 * called sirf_ctrl.  Contributed to gpsd by the author.
 *
 * Modified to not use stderr and so each function returns 0 on success,
 * nonzero on failure.  Alsso to use gpsd's own checksum and send code.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "gpsd.h"
#include "sirf.h"

/* This we can do from NMEA mode */

int sirf_mode(struct gps_session_t *session, int binary, int speed) 
/* switch GPS to specified mode at 8N1, optionarry to binary */
{
   int status = nmea_send(session->gNMEAdata.gps_fd, 
		    "$PSRF100,%d,%d,8,1,0", !binary, speed);
   gpsd_report(1, "Send returned %d.\n", status);
   tcdrain(session->gNMEAdata.gps_fd);
   /* 
    * This definitely fails below 40 milliseconds on a BU-303b.
    * 50ms is also verified by Chris Kuethe on 
    *        Pharos iGPS360 + GSW 2.3.1ES + prolific
    *        Rayming TN-200 + GSW 2.3.1 + ftdi
    *        Rayming TN-200 + GSW 2.3.2 + ftdi
    * so it looks pretty solid.
    */
   usleep(50000);
   return status && 
	gpsd_set_speed(session->gNMEAdata.gps_fd, &session->ttyset, (speed_t)speed);
}

/* These require binary mode */

#define HI(n)	((n) >> 8)
#define LO(n)	((n) & 0xff)

static u_int16_t crc_sirf(u_int8_t *msg) {
   int       pos = 0;
   u_int16_t crc = 0;
   int       len;

   len = (msg[2] << 8) | msg[3];

   /* calculate CRC */
   while (pos != len)
      crc += msg[pos++ + 4];
   crc &= 0x7fff;

   /* enter CRC after payload */
   msg[len + 4] = (u_int8_t)((crc & 0xff00) >> 8);
   msg[len + 5] = (u_int8_t)( crc & 0x00ff);

   return(crc);
}

int sirf_waas_ctrl(int ttyfd, int enable) 
/* enable or disable WAAS */
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x07,
                     0x85, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00,
                     0x00, 0x00, 0xb0, 0xb3};

   msg[5] = (u_int8_t)enable;
   crc_sirf(msg);
   return (write(ttyfd, msg, 15) != 15);
}


int sirf_to_nmea(int ttyfd, int speed) 
/* switch from binary to NMEA at specified baud */
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x18,
                     0x81, 0x02,
                     0x01, 0x01, /* GGA */
                     0x00, 0x01, /* GLL */
                     0x01, 0x01, /* GSA */
                     0x05, 0x01, /* GSV */
                     0x01, 0x01, /* RMC */
                     0x00, 0x01, /* VTG */
                     0x00, 0x01, 0x00, 0x01,
                     0x00, 0x01, 0x00, 0x01,
                     0x12, 0xc0, /* 4800 bps */
                     0x00, 0x00, 0xb0, 0xb3};

   msg[26] = HI(speed);
   msg[27] = LO(speed);
   crc_sirf(msg);
   return (write(ttyfd, msg, 0x18+8) != 0x18+8);
}


int sirf_reset(int ttyfd) 
/* reset GPS parameters */
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x19,
                     0x81,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00,
                     0x0c,
                     0x04,
                     0x00, 0x00, 0xb0, 0xb3};

   crc_sirf(msg);
   return (write(ttyfd, msg, 0x19+8) != 0x19+8);
}


int sirf_dgps_source(int ttyfd, int source) 
/* set source for DGPS corrections */
{
   int i;
   u_int8_t msg1[] = {0xa0, 0xa2, 0x00, 0x07,
                      0x85,
                      0x00,                    /* DGPS source   */
                      0x00, 0x01, 0xe3, 0x34,  /* 123.7 kHz     */
                      0x00,                    /* auto bitrate  */
                      0x00, 0x00, 0xb0, 0xb3};
   u_int8_t msg2[] = {0xa0, 0xa2, 0x00, 0x03,
                      0x8a,
                      0x00, 0xff,              /* auto, 255 sec */
                      0x00, 0x00, 0xb0, 0xb3};
   u_int8_t msg3[] = {0xa0, 0xa2, 0x00, 0x09,
                      0x91,
                      0x00, 0x00, 0x12, 0xc0,  /* 4800 baud     */
                      0x08,                    /* 8 bits        */
                      0x01,                    /* 1 Stop bit    */
                      0x00,                    /* no parity     */
                      0x00,
                      0x00, 0x00, 0xb0, 0xb3};

   /*
    * set DGPS source
    */
   switch (source) {
      case DGPS_SOURCE_NONE:
         /* set no DGPS source */
         msg1[5] = 0x00;
         break;
      case DGPS_SOURCE_INTERNAL:
         /* set to internal DGPS beacon */
         msg1[5] = 0x03;
         break;
      case DGPS_SOURCE_WAAS:
         /* set to WAAS/EGNOS */
         msg1[5] = 0x01;
         for (i = 6; i < 11; i++)
            msg1[i] = 0x00;
         break;
      case DGPS_SOURCE_EXTERNAL:
         /* set to external RTCM input */
         msg1[5] = 0x02;
         break;
   }
   crc_sirf(msg1);
   if (write(ttyfd, msg1, 0x07+8) != 0x07+8)
      return 1;

   /*
    * set DGPS control to auto
    */
   if (source != DGPS_SOURCE_WAAS) {
      crc_sirf(msg2);
      if (write(ttyfd, msg2, 0x03+8) != 0x03+8)
         return 2;
   }

   /*
    * set DGPS port
    */
   if (source == DGPS_SOURCE_EXTERNAL) {
      crc_sirf(msg3);
      if (write(ttyfd, msg3, 0x09+8) == 0x09+8)
	  return 3;
   }

   return(0);
}


int sirf_nav_lib (int ttyfd, int enable)
/* set single-channel mode */
{
   u_int8_t msg_1[] = {0xa0, 0xa2, 0x00, 0x19,
                       0x80,
                       0x00, 0x00, 0x00, 0x00, /* ECEF X       */
                       0x00, 0x00, 0x00, 0x00, /* ECEF Y       */
                       0x00, 0x00, 0x00, 0x00, /* ECEF Z       */
                       0x00, 0x00, 0x00, 0x00, /* Clock Offset */
                       0x00, 0x00, 0x00, 0x00, /* Time of Week */
                       0x00, 0x00,             /* Week Number  */
                       0x0c,                   /* Channels     */
                       0x00,                   /* Reset Config */
                       0x00, 0x00, 0xb0, 0xb3};

   if (enable == 1)
      msg_1[28] = 0x10;

   crc_sirf(msg_1);
   return (write(ttyfd, msg_1, 0x19+8) != 0x19+8);
}

int sirf_power_mask(int ttyfd, int low)
/* set dB cutoff level below which satellite info will be ignored */
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x03,
                     0x8c, 0x1c, 0x1c,
                     0x00, 0x00, 0xb0, 0xb3};

   if (low == 1)
      msg[6] = 0x14;

   crc_sirf(msg);
   return (write(ttyfd, msg, 0x03+8) != 0x03+8);
}


int sirf_power_save(int ttyfd, int enable)
/* enable/disable SiRF trickle-power mode */ 
{
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x09,
                     0x97,
                     0x00, 0x00,
                     0x03, 0xe8,
                     0x00, 0x00, 0x00, 0xc8,
                     0x00, 0x00, 0xb0, 0xb3};

   if (enable == 1) {
      /* power save: duty cycle is 20% */
      msg[7] = 0x00;
      msg[8] = 0xc8;
   }

   crc_sirf(msg);
   return (write(ttyfd, msg, 0x09+8) != 0x09+8);
}
