/*
 * Copyright (C) 2003 Arnim Laeuger <arnim.laeuger@gmx.net>
 * Issued under GPL.  Originally part of an unpublished utility
 * called sirf_ctrl.  Contributed to gpsd by the author.
 *
 * Modified to not use stderr and so each function returns 0 on success,
 * nonzero on failure.
 */

#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "sirf.h"

static u_int8_t crc_nmea(char *msg) {
   int      pos;
   char     *tag;
   u_int8_t crc = 0;
   static char nib_to_hex[] = {'0','1','2','3','4','5','6','7',
			       '8','9','A','B','C','D','E','F'};

   /* ignore a leading '$' */
   pos = msg[0] == '$' ? 1 : 0;

   /* calculate CRC */
   while (msg[pos] != '*' && msg[pos] != 0)
      crc ^= msg[pos++];

   /* set upper nibble of CRC */
   tag  = index(msg, '%');
   *tag = nib_to_hex[(crc & 0xf0) >> 4];
   /* set lower nibble of CRC */
   tag  = index(msg, '%');
   *tag = nib_to_hex[crc & 0x0f];

   return(crc);
}


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


int sirf_to_sirfbin(int ttyfd) {
   int      len;
   char     msg[] = "$PSRF100,0,19200,8,1,0*%%\r\n";

   crc_nmea(msg);

   len = strlen(msg);
   return (write(ttyfd, msg, len) != len);
}


int sirf_waas_ctrl(int ttyfd, int enable) {
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x07,
                     0x85, 0x00,
                     0x00, 0x00, 0x00, 0x00,
                     0x00,
                     0x00, 0x00, 0xb0, 0xb3};

   msg[5] = (u_int8_t)enable;
   crc_sirf(msg);
   return (write(ttyfd, msg, 15) != 15);
}


int sirf_to_nmea(int ttyfd) {
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
                     0x12, 0xc0,
                     0x00, 0x00, 0xb0, 0xb3};

   crc_sirf(msg);
   return (write(ttyfd, msg, 0x18+8) != 0x18+8);
}


int sirf_reset(int ttyfd) {
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


int sirf_dgps_source(int ttyfd, int source) {
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


int sirf_nav_lib (int ttyfd, int enable) {
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


int sirf_nmea_waas(int ttyfd, int enable) {
   int  len;
   char msg[] = "$PSRF108,0?*%%\r\n";
   char *tag;

   tag = index(msg, '?');
   *tag = enable == 1 ? '1' : '0';

   crc_nmea(msg);

   len = strlen(msg);
   return (write(ttyfd, msg, len) != len);
}


int sirf_power_mask(int ttyfd, int low) {
   u_int8_t msg[] = {0xa0, 0xa2, 0x00, 0x03,
                     0x8c, 0x1c, 0x1c,
                     0x00, 0x00, 0xb0, 0xb3};

   if (low == 1)
      msg[6] = 0x14;

   crc_sirf(msg);
   return (write(ttyfd, msg, 0x03+8) != 0x03+8);
}


int sirf_power_save(int ttyfd, int enable) {
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
