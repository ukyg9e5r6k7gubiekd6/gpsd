/************************************************************************
**   gpsread.c
**
**   H.Berns, U.Washington, Seattle, for WALTA/Quarknet/CROP
**   last edited: 22 May 2002
**
**   This is a test program to set up a Leadtek GPS-9532 receiver
**   through the serial port.  Before startup, please reset the
**   GPS-9532 (e.g. power off for approx. 1 minute) so it will start
**   in default mode with NMEA protocol at 4800 baud.
*************************************************************************
**
**   This program runs the following sequence:
**   - setup serial port COM1 (/dev/ttyS0) to 4800 baud, 8N1, ascii.
**   - read a couple of lines of NMEA serial data at 4800 baud and
**     send NMEA command $PSRF105 to switch GPS "Development Data ON" 
**   - send NMEA command $PSRF100 to switch GPS to SiRF mode & 19200 baud
**   - switch serial port COM1 to 19200 baud, 8N1, binary (EOL=0xb3).
**   - read a couple of SiRF messages and
**     send SiRF command 0x97 to set GPS trickle power to "continuous".
**   - send SiRF command 0x81 to switch GPS to NMEA with only
**     RMC and GGA messaging enabled at 19200 baud.
**   - set serial port COM1 back to ascii default at 19200 baud.
**   - read NMEA lines indefinitely (until CTRL-C).
**
*************************************************************************
**   The serial port routines are based on an example found at
**   ftp://sunsite.unc.edu:/pub/Linux/docs/HOWTO/Serial-Programming-HOWTO
**   "The Linux Serial Programming HOWTO" [v1.0, 22 January 1998]
**   by Peter H. Baumann, Peter.Baumann@dlr.de
**   (chapter 3.1. "Canonical Input Processing")
************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

/* baudrate settings are defined in <asm/termbits.h>, which is
   included by <termios.h> */

#define	BAUDRATE_4800	B4800	/*  4800 baud */
#define	BAUDRATE_19200	B19200	/* 19200 baud */

/* change this definition for the correct port
** COM1: /dev/ttyS0, COM2: /dev/ttyS1, etc.
**
** Note: with a default Linux setup, only the superuser (root) has 
** read/write permission to the serial port, unless permission is
** set for all users.  E.g. set permission from root with command
** `chmod 666 /dev/ttyS?`
*/ 
#define	MODEMDEVICE	"/dev/ttyUSB0"	/* COM1 */

#define	_POSIX_SOURCE	1	/* POSIX compliant source */
#define	FALSE	0
#define	TRUE	1
#define	DEBUG	1	/* 1=debug 0=no_debug */
#define	BUFMAX	10000
#define HEXFILE         "log/gps.hex"
#define LOGFILE         "log/gps.log"
#define DATFILE         "log/gps.dat"

/* global variables */
struct	termios	oldtio, newtio;
struct	timeval	sys_time, prev_sys_time;
struct	timezone	tz;
int	fd;
short   buf2[2*BUFMAX];
char    buf[2*BUFMAX], buf_nmea[2*BUFMAX], payload[BUFMAX];
int     sats=0, pos_fix=0;
int     STOP=FALSE, START=TRUE;
float   altitude=-1.0, hdop=-1.0;
FILE    *out, *dout;

/* -------------------------- main program --------------------------------- */

main(int argc, char *argv[])
{
    int    i, j, k, n, res, sent, SiRF_loop=100, NMEA_loop=20;
    int    STOP=FALSE, START=FALSE;
    char   ctemp, choice;
    short  mnem[4];
    FILE   *hout;

    if ((out=fopen(LOGFILE,"w")) == NULL)
    {
      fprintf(stderr,"can't open %s !\n", LOGFILE);
      exit(1);
    }

    if ((hout=fopen(HEXFILE,"w")) == NULL)
    {
      fprintf(stderr,"can't open %s !\n", HEXFILE);
      exit(1);
    }

    if ((dout=fopen(DATFILE,"w")) == NULL)
    {
      fprintf(stderr,"can't open %s !\n", DATFILE);
      exit(1);
    }
    fprintf(stderr,"Did the Leadtek GPS just reset (power off/on) [y/n]? ");
    choice = getc(stdin);
    if (choice==0x59 || choice==0x79) START = TRUE;

    fprintf(stderr,"###################################################\n");
    fprintf(stderr,"# %s started\n# to stop program hit <CTRL><C>\n",argv[0]);
    fprintf(stderr,"###################################################\n");

    j=0;
    bzero(buf_nmea, sizeof(buf_nmea));
    bzero(buf,      sizeof(buf));
    gettimeofday(&prev_sys_time, &tz);  /* read precision system time from PC */

    if (START)
    {
      fprintf(stderr,"now waiting for serial NMEA data at 4800 baud from " MODEMDEVICE "\n");

      /* Open modem device for reading and writing and not as controlling
         tty because we don't want to get killed if linenoise sends CTRL-C.
      */

      fd = setup_terminal(fd,0);  /* mode=0 = NMEA mode at 4800 baud */

      for (i=0; i<NMEA_loop; i++)
      {
        res = (int) read(fd, buf, BUFMAX);
        gettimeofday(&sys_time, &tz);  /* read precision system time from PC */

        for (n=0; n<res; n++)
        {
          fprintf(stderr,"%c", buf[n] & 0xff);
          fprintf(out,"%c", buf[n] & 0xff);
          if (buf[n]==0x24)   /* "$" */
          {
            buf_nmea[j] = 0;
            if (!strncmp(buf_nmea,"$GPRMC,",7)) extract_RMC_data();
            if (!strncmp(buf_nmea,"$GPGGA,",7)) extract_GGA_data();
            buf_nmea[0] = buf[n] & 0xff;
            j=1;
          }
          else
          {
            buf_nmea[j] = buf[n] & 0xff;
            j++;
          }         
        }
        fflush(out);
        memcpy(&prev_sys_time, &sys_time, sizeof(sys_time));  /* save system time for next readout */

        if (i==20)
        {
          /* send NMEA command - switch "Development Data" on */
          sprintf(payload,"PSRF105,1");
          send_NMEA_message(9);
        }
        if (DEBUG && i>=30 && i<=35)
        {
          /* send NMEA commands to set query controls */

          if (i==30) sprintf(payload,"PSRF103,00,00,10,01");   /* set GGA to 1/10 Hz */
          if (i==31) sprintf(payload,"PSRF103,01,00,00,01");   /* disable GLL */
          if (i==32) sprintf(payload,"PSRF103,02,00,00,01");   /* disable GSA */
          if (i==33) sprintf(payload,"PSRF103,03,00,00,01");   /* disable GSV */
          if (i==34) sprintf(payload,"PSRF103,04,00,01,01");   /* set RMC to 1 Hz */
          if (i==35) sprintf(payload,"PSRF103,05,00,00,01");   /* disable VTG */
          send_NMEA_message(19);
        }
      }

      /* send NMEA command to switch GPS to SiRF mode and 19200 baud */

      sprintf(payload,"PSRF100,0,19200,8,1,0");
      send_NMEA_message(21);

      if (DEBUG)
      {
        sprintf(payload,"PSRF100,0,19200,8,1,0"); /* send again to make sure... */
        send_NMEA_message(21);
      }

      /* switch PC's serial port now to 19200 baud */

      sleep(1);                   /* wait 1 second */
      restore_terminal(fd);
      fprintf(stderr,"\n================================================\n");
      fprintf(stderr,"Switch to 19200 baud for SiRF binary protocol\n");
      fprintf(stderr,"================================================\n");
      fd = setup_terminal(fd,1);  /* mode=1 = SiRF mode at 19200 baud */

      /* poll software version */

      j  = k = 0;
      for (i=0; i<4; i++) mnem[i]=0;

      for (i=0; i<SiRF_loop; i++)     /* loop over some serial port readout lines */
      {
        /* read blocks program execution until a line terminating character 
           is input, even if more than BUFMAX chars are input. If the number
           of characters read is smaller than the number of chars available,
           subsequent reads will return the remaining chars. res will be set
           to the actual number of characters actually read
        */
      
        res = (int) read(fd, buf, BUFMAX);

        for (n=0; n<res; n++)
        {
          mnem[j%4] = (short) buf[n] & 0xff;
          buf2[k]   = (short) buf[n] & 0xff;

          if (j>1) fprintf(hout,"%02x", mnem[(j-2)%4]);

          if (mnem[(j-3)%4]==176 && mnem[(j-2)%4]==179 && mnem[(j-1)%4]==160 && mnem[j%4]==162)
          {
            fprintf(stderr,"\nSiRF ID %2d (%3d bytes): ", buf2[4], k-1);

            if (buf2[4]==2)  display_SiRF_message_02();
            if (buf2[4]==4)  display_SiRF_message_04();
            if (buf2[4]==6)  display_SiRF_message_06();
            if (buf2[4]==11) display_SiRF_message_11();
            if (buf2[4]==12) display_SiRF_message_12();
            if (buf2[4]==19) display_SiRF_message_19();

            k=1;
            buf2[0] = 160;
            buf2[1] = 162;
            fprintf(hout,"\n");
            fflush(hout);
          }
          k++;
          j++;
        }

        /* send SiRF commands at certain times */

        if (i==4)
        {
          /* poll software version */

          payload[0] = 0x84;
          payload[1] = 0x00;
          send_SiRF_message(2);
        }
        if (i==5)
        {
          /* Enable WAAS */

          payload[0] = 0x81;
          payload[1] = 0x01;
          payload[2] = 0x00;
          payload[3] = 0x00;
          send_SiRF_message(4);
        }
        else if (i==6)
        {
          /* set trickle power parameters to "continuous" */
/*
          payload[0] = 0x97;
          payload[1] = 0x00;
          payload[2] = 0x00;
          payload[3] = 0x03;
          payload[4] = 0xe8;
          payload[5] = 0x00;
          payload[6] = 0x00;
          payload[7] = 0x01;
          payload[8] = 0xf4;
*/
          payload[0] = 0x97;
          payload[1] = 0x00;
          payload[2] = 0x00;
          payload[3] = 0xc8;
          payload[4] = 0x00;
          payload[5] = 0x00;
          payload[6] = 0x00;
          payload[7] = 0xc8;
          payload[8] = 0x00;
          send_SiRF_message(9);
        }
        else if (i==8)
        {
          /* poll Navigation parameters */

          payload[0] = 0x98;
          payload[1] = 0x00;
          send_SiRF_message(2);
        }
        else if (i==(SiRF_loop-10))
        {
          /* switch to NMEA protocol */

          payload[0]  = 0x81;  /* message ID 129 (0x81) */
          payload[1]  = 0x02;
          payload[2]  = 0x0a;  /* GGA on at 10 second interval */
          payload[3]  = 0x01;
          payload[4]  = 0x00;  /* GLL off */
          payload[5]  = 0x01;
          payload[6]  = 0x00;  /* GSA off */
          payload[7]  = 0x01;
          payload[8]  = 0x00;  /* GSV off */
          payload[9]  = 0x01;
          payload[10] = 0x01;  /* RMC on at 1 second */
          payload[11] = 0x01;
          payload[12] = 0x00;  /* VTG message off */
          payload[13] = 0x01;
          payload[14] = 0x00;
          payload[15] = 0x01;
          payload[16] = 0x00;
          payload[17] = 0x01;
          payload[18] = 0x00;
          payload[19] = 0x01;
          payload[20] = 0x00;
          payload[21] = 0x01;
          payload[22] = 0x4b;  /* baudrate = 19200 */
          payload[23] = 0x00;
          send_SiRF_message(24);
        }
      }
      fclose(hout);

      /* done with SiRF protocol, now switch back to NMEA protocol */ 

      restore_terminal(fd);
      sleep(1);

      fprintf(stderr,"\n================================================\n");
      fprintf(stderr,"Switch back to NMEA protocol, stay at 19200 baud\n");
      fprintf(stderr,"================================================\n");
    }

    fd = setup_terminal(fd,2);  /* mode=0 = NMEA mode at 19200 baud */

    sprintf(payload,"PSRF105,1");            /* "Development Data" on */
    send_NMEA_message(9);
    sprintf(payload,"PSRF103,00,00,05,01");  /* set GGA to 5 sec interval */
    send_NMEA_message(19);
    sprintf(payload,"PSRF103,01,00,00,01");  /* disable GLL */
    send_NMEA_message(19);
    sprintf(payload,"PSRF103,02,00,00,01");  /* disable GSA */
    send_NMEA_message(19);
    sprintf(payload,"PSRF103,03,00,00,01");  /* disable GSV */
    send_NMEA_message(19);
    sprintf(payload,"PSRF103,04,00,01,01");  /* set RMC to 1 sec interval */
    send_NMEA_message(19);
    sprintf(payload,"PSRF103,05,00,00,01");  /* disable VTG */
    send_NMEA_message(19);

    j=0;
    gettimeofday(&prev_sys_time, &tz);

/*    START = FALSE; */

/*    for (;;)  /* run indefinitely or until CTRL-C */
    for (i=0; i<NMEA_loop; i++)
    {
      gettimeofday(&prev_sys_time, &tz);
      res = (int) read(fd, buf, BUFMAX);

      for (n=0; n<res; n++)
      {
        fprintf(stderr,"%c", buf[n] & 0xff);
        fprintf(out,"%c", buf[n] & 0xff);
        
        buf_nmea[j] = buf[n] & 0xff;
        if (buf_nmea[j]==0x0a)
        {
          buf_nmea[j+1]=0;
          if (!strncmp(buf_nmea,"$GPRMC,",7)) extract_RMC_data();
          if (!strncmp(buf_nmea,"$GPGGA,",7)) extract_GGA_data();
          j=0;
        }
        else
        {
          j++;
        }
      }
      fflush(out);
    }
      /* send NMEA command to switch GPS to NMEA mode and 4800 baud */

    sprintf(payload,"PSRF100,1,4800,8,1,0");
    send_NMEA_message(21);
    sleep(1);

    sprintf(payload,"PSRF100,1,4800,8,1,0");
    send_NMEA_message(21);

    restore_terminal(fd);
    sleep(1);

    fprintf(stderr,"\n================================================\n");
    fprintf(stderr,"Switch back to NMEA protocol, 4800 baud\n");
    fprintf(stderr,"================================================\n");
    fd = setup_terminal(fd,0);  /* mode=0 = NMEA mode at 4800 baud */

    sprintf(payload,"PSRF105,1");            /* "Development Data" on */
    send_NMEA_message(9);
    sprintf(payload,"PSRF108,1");            /* "WAAS" on */
    send_NMEA_message(9);
    sprintf(payload,"PSRF103,00,00,01,01");  /* set GGA to 5 sec interval */
    send_NMEA_message(19);
    sprintf(payload,"PSRF103,01,00,01,01");  /* disable GLL */
    send_NMEA_message(19);
    sprintf(payload,"PSRF103,02,00,01,01");  /* disable GSA */
    send_NMEA_message(19);
    sprintf(payload,"PSRF103,03,00,01,01");  /* disable GSV */
    send_NMEA_message(19);
    sprintf(payload,"PSRF103,04,00,01,01");  /* set RMC to 1 sec interval */
    send_NMEA_message(19);
    sprintf(payload,"PSRF103,05,00,01,01");  /* disable VTG */
    send_NMEA_message(19);

    for (i=0; i<NMEA_loop; i++)
    {
      gettimeofday(&prev_sys_time, &tz);
      res = (int) read(fd, buf, BUFMAX);

      for (n=0; n<res; n++)
      {
        fprintf(stderr,"%c", buf[n] & 0xff);
        fprintf(out,"%c", buf[n] & 0xff);
        
        buf_nmea[j] = buf[n] & 0xff;
        if (buf_nmea[j]==0x0a)
        {
          buf_nmea[j+1]=0;
          if (!strncmp(buf_nmea,"$GPRMC,",7)) extract_RMC_data();
          if (!strncmp(buf_nmea,"$GPGGA,",7)) extract_GGA_data();
          j=0;
        }
        else
        {
          j++;
        }
      }
      fflush(out);
    }

    fclose(out);
    fclose(dout);
}

/* ------------------------------ subroutines ------------------------------ */

int setup_terminal(int fd, int mode)
{
    /* mode: 0 = NMEA mode at 4800 baud (GPS power up default)
             1 = SiRF mode at 19200 baud
             2 = NMEA mode at 19200 baud
    */

    fd = open(MODEMDEVICE, O_RDWR | O_NOCTTY );
    if (fd <0) { perror(MODEMDEVICE); exit(-1); }

    if (DEBUG) fprintf(stderr,"   %s open\n", MODEMDEVICE);

    tcgetattr(fd,&oldtio);         /* save current serial port settings */
    bzero(&newtio, sizeof(newtio)); /* clear struct for new port settings */

    /* BAUDRATE: Set bps rate. You could also use cfsetispeed and
                 cfsetospeed.
       CRTSCTS : output hardware flow control (only used if the cable has
                 all necessary lines. See sect. 7 of Serial-HOWTO)
                 [not used here = no flow control]
       CS8     : 8n1 (8bit,no parity,1 stopbit)
       CLOCAL  : local connection, no modem contol
       CREAD   : enable receiving characters
    */

/*  newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;  */

    if (mode==0)
    {
      newtio.c_cflag = BAUDRATE_4800 | CS8 | CLOCAL | CREAD; /* no flow control */
    }
    else
    {
      newtio.c_cflag = BAUDRATE_19200 | CS8 | CLOCAL | CREAD; /* no flow control */
    }

    /* IGNPAR  : ignore bytes with parity errors
       ICRNL   : map CR to NL (otherwise a CR input on the other computer
                 will not terminate input)
       otherwise make device raw (no other input processing)
    */

    newtio.c_iflag = IGNPAR;

    /* Raw output. */

    newtio.c_oflag = 0;

    /*  ICANON  : enable canonical input
        disable all echo functionality, and don't send signals to calling
        program
    */

    newtio.c_lflag = ICANON;

    /* initialize all control characters
       default values can be found in /usr/include/termios.h, and are
       given in the comments, but we don't need them here
    */

    newtio.c_cc[VINTR]    = 0;   /* Ctrl-c */
    newtio.c_cc[VQUIT]    = 0;   /* Ctrl-\ */
    newtio.c_cc[VERASE]   = 0;   /* del */
    newtio.c_cc[VKILL]    = 0;   /* @ */
    newtio.c_cc[VEOF]     = 0;   /* Ctrl-d */
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 0;   /* blocking read until 1 character arrives */
    newtio.c_cc[VSWTC]    = 0;   /* '\0' */
    newtio.c_cc[VSTART]   = 0;   /* Ctrl-q */
    newtio.c_cc[VSTOP]    = 0;   /* Ctrl-s */
    newtio.c_cc[VSUSP]    = 0;   /* Ctrl-z */
    if (mode==1)
    {
      newtio.c_cc[VEOL]   = 0xb3;  /* EOL character for SiRF mode = 0xb3 */
    }
    else
    {
      newtio.c_cc[VEOL]   = 0;   /* 0='\0' */
    }
    newtio.c_cc[VREPRINT] = 0;   /* Ctrl-r */
    newtio.c_cc[VDISCARD] = 0;   /* Ctrl-u */
    newtio.c_cc[VWERASE]  = 0;   /* Ctrl-w */
    newtio.c_cc[VLNEXT]   = 0;   /* Ctrl-v */
    newtio.c_cc[VEOL2]    = 0;   /* '\0' */

    /* now clean the modem line and activate the settings for the port */

    if (DEBUG) fprintf(stderr,"   %s new terminal settings loaded\n",MODEMDEVICE);

    tcflush(fd, TCIFLUSH);
    tcsetattr(fd,TCSANOW,&newtio);

    /* terminal settings done, now handle input
       In this example, inputting a 'z' at the beginning of a line will
       exit the program.
    */

    if (DEBUG) fprintf(stderr,"   %s activated ...\n",MODEMDEVICE);
    return(fd);
}

restore_terminal(int fd)
{
    /* restore the old port settings */

    tcsetattr(fd,TCSANOW,&oldtio);
}

display_SiRF_message_02()
{
    /* Message ID 2: Navigation & Time data */

    u_int week, t100, seconds, day, hour, min, sec;
    int   n, Xpos, Ypos, Zpos, mode, sats;

    /* x,y,z position in meters (from Earth center) */

    Xpos = ((buf2[5]  << 24) & 0xff000000) + \
           ((buf2[6]  << 16) & 0x00ff0000) + \
           ((buf2[7]  <<  8) & 0x0000ff00) + \
            (buf2[8]         & 0x000000ff);
    Ypos = ((buf2[9]  << 24) & 0xff000000) + \
           ((buf2[10] << 16) & 0x00ff0000) + \
           ((buf2[11] <<  8) & 0x0000ff00) + \
            (buf2[12]        & 0x000000ff);
    Zpos = ((buf2[13] << 24) & 0xff000000) + \
           ((buf2[14] << 16) & 0x00ff0000) + \
           ((buf2[15] <<  8) & 0x0000ff00) + \
            (buf2[16]        & 0x000000ff);

    /* GPS week since Jan. 1980 (modulo 1024) */

    week = ((buf2[26] <<  8) & 0x0000ff00) + \
            (buf2[27]        & 0x000000ff);

    /* time in 1/100 seconds since start of GPS week */

    t100 = ((buf2[28] << 24) & 0xff000000) + \
           ((buf2[29] << 16) & 0x00ff0000) + \
           ((buf2[30] <<  8) & 0x0000ff00) + \
            (buf2[31]        & 0x000000ff);

    seconds = t100/100;
    day  = seconds / 86400;
    hour = (seconds%86400) / 3600;
    min  = (seconds%3600) / 60;
    sec  = seconds%60;

    mode = (int) buf2[23];  /* satellite tracking mode */
    sats = (int) buf2[32];  /* number of tracked satellites */

    fprintf(stderr," Week=%d Time=%d:%02d:%02d:%02d.%02d mode=%02x X=%d Y=%d Z=%d sats=%d",\
            week, day, hour, min, sec, t100%100, mode, Xpos, Ypos, Zpos, sats);
    if (sats>0)
    {
      fprintf(stderr," sat#");
      for (n=0; n<sats; n++) fprintf(stderr," %02d", buf2[33+n]);
    }
}

display_SiRF_message_04()
{
    /* Message ID 4: Tracker data */

    u_int week, t100, seconds, day, hour, min, sec;

    /* GPS week */

    week = ((buf2[5]  <<  8) & 0x0000ff00) + \
            (buf2[6]         & 0x000000ff);

    /* time in 1/100 seconds since start of GPS week */

    t100 = ((buf2[7]  << 24) & 0xff000000) + \
           ((buf2[8]  << 16) & 0x00ff0000) + \
           ((buf2[9]  <<  8) & 0x0000ff00) + \
            (buf2[10]        & 0x000000ff);

    seconds = t100/100;
    day  = seconds / 86400;
    hour = (seconds%86400) / 3600;
    min  = (seconds%3600) / 60;
    sec  = seconds%60;

    fprintf(stderr," Week=%d Time=%d:%02d:%02d:%02d.%02d",\
            week, day, hour, min, sec, t100%100);
}

display_SiRF_message_06()
{
    /* Message ID 6: Software version */

    int n=0;
    
    fprintf(stderr," = S/W VERSION: ");
    while(n<20 && buf2[n+5]!=0)
    {
      fprintf(stderr,"%c",(char) buf2[n+5]);
      n++;
    }
}

display_SiRF_message_11()
{
    /* Message ID 11: Command Acknowledgement */

    fprintf(stderr," => Command 0x%02x acknowledged", buf2[5]);
}

display_SiRF_message_12()
{
    /* Message ID 12: Command NAcknowledgement */

    fprintf(stderr," => Command 0x%02x not understood - ERROR!!", buf2[5]);
}

display_SiRF_message_19()
{
    /* Message ID 19: Navigation Parameter response */

    int n;

    fprintf(stderr," Navigation Parameters: ");
    for (n=0; n<23; n++) fprintf(stderr,"%02x", buf2[n+5]);
}

send_SiRF_message(int numpayload)
{
    short checksum=0;
    int   n, sent, numbytes;
    char  bufs[BUFMAX];

    bufs[0] = 0xa0;
    bufs[1] = 0xa2;
    bufs[2] = (numpayload >> 8) & 0xff;
    bufs[3] = numpayload & 0xff;

    for (n=0; n<numpayload; n++)
    {
      bufs[n+4] = payload[n] & 0xff;
      checksum += (short) (payload[n] & 0xff);
    }

    bufs[numpayload+4] = (checksum >> 8) & 0xff;
    bufs[numpayload+5] = checksum & 0xff;
    bufs[numpayload+6] = 0xb0;
    bufs[numpayload+7] = 0xb3;
    
    numbytes = numpayload + 8;

    sent = (int) write(fd, bufs, numbytes);
    if (sent != numbytes)
    {
      fprintf(stderr,"\nSerial Port Write Error, wstat=%d\n", sent);
    }
    else
    {
      fprintf(stderr,"\n---------------------------------------------\n");
      fprintf(stderr,"SiRF message %d sent with %d bytes\n", payload[0] & 0xff, numbytes);
      fprintf(stderr,"---------------------------------------------\n");
    }
}

send_NMEA_message(int numpayload)
{
    char  checksum=0, check_sum[2];
    int   n, sent, numbytes;
    char  bufs[BUFMAX];

    bufs[0] = 0x24;  /* "$" */

    for (n=0; n<numpayload; n++)
    {
      bufs[n+1] = payload[n] & 0xff;
      checksum ^= payload[n] & 0xff;
    }

    sprintf(check_sum,"%02x",checksum & 0xff);

    bufs[numpayload+1] = 0x2a;  /* "*" */
    bufs[numpayload+2] = check_sum[0] & 0xff;
    bufs[numpayload+3] = check_sum[1] & 0xff;
    bufs[numpayload+4] = 0x0d;  /* <CR> */
    bufs[numpayload+5] = 0x0a;  /* <LF> */
    
    numbytes = numpayload + 6;

    sent = (int) write(fd, bufs, numbytes);
    if (sent != numbytes)
    {
      fprintf(stderr,"\nSerial Port Write Error, wstat=%d\n", sent);
    }
    else if (START)   /* display sent message for debug */
    {
      fprintf(stderr,"\n-----------------------------------------------------------\n");
      fprintf(stderr,"NMEA message sent (%d bytes): ", numbytes);
      for (n=0; n<numbytes; n++) fprintf(stderr,"%c", bufs[n] & 0xff);
      fprintf(stderr,"-----------------------------------------------------------\n");
    }
}

extract_RMC_data()
{
    int    DATA_VALID=FALSE;
    int    itime, mtime, ilat, mlat, ilong, mlong;
    int    date, year, month, day, hour, min, sec, msec;
    int    latdeg, latmin, longdeg, longmin;
    int    igps_sec;
    float  course, speed;
    long double gps_sec, sys_sec, psys_sec, diff_sec, wait_sec;
    char   valid_flag[10], lat_dir[10], long_dir[10];

    if (!START) gettimeofday(&sys_time, &tz);

//    fprintf(stderr,"RMC data detected: %s\n", buf_nmea);

    if (12==sscanf(buf_nmea,"$GPRMC,%6d.%3d,%[AV],%4d.%4d,%[NS],%5d.%4d,%[EW],%f,%f,%6d", \
                   &itime, &msec, valid_flag, &ilat, &mlat, lat_dir, &ilong, &mlong, \
                   long_dir, &speed, &course, &date))
    {
      DATA_VALID=TRUE;
    }
    else if(11==sscanf(buf_nmea,"$GPRMC,%6d.%3d,%[AV],%4d.%4d,%[NS],%5d.%4d,%[EW],%f,,%6d", \
                   &itime, &msec, valid_flag, &ilat, &mlat, lat_dir, &ilong, &mlong, \
                   long_dir, &speed, &date))
    {
      DATA_VALID=TRUE;
    }
    else if (10==sscanf(buf_nmea,"$GPRMC,%6d.%3d,%[AV],%4d.%4d,%[NS],%5d.%4d,%[EW],,,%6d", \
                   &itime, &msec, valid_flag, &ilat, &mlat, lat_dir, &ilong, &mlong,  \
                   long_dir, &date))
    {
      DATA_VALID=TRUE;
    }
    else
    {
      DATA_VALID=FALSE;
    }

    if (DATA_VALID)
    {

      hour  = itime/10000;
      min   = (itime%10000)/100;
      sec   = itime%100;

      year  = 2000 + (date%100);
      month = (date%10000)/100;
      day   = date/10000;

      igps_sec = calc_daytime(year, month, day, hour, min, sec);
      gps_sec  = (long double) igps_sec + (long double) msec / 1000.0;
      sys_sec  = (long double) sys_time.tv_sec + (long double) sys_time.tv_usec / 1000000.0;
      psys_sec = (long double) prev_sys_time.tv_sec + (long double) prev_sys_time.tv_usec / 1000000.0;
      diff_sec = sys_sec - gps_sec;
      wait_sec = sys_sec - psys_sec;

      latdeg   = ilat/100;
      latmin   = ilat%100;

      longdeg  = ilong/100;
      longmin  = ilong%100;

      fprintf(dout,"%4d/%02d/%02d", year, month, day);
      fprintf(dout," %02d:%02d:%02d.%03d %c", hour, min, sec, msec, valid_flag[0]);      
      fprintf(dout," Lat=%02d:%02d.%04d-%c", latdeg, latmin, mlat, lat_dir[0]);
      fprintf(dout," Lng=%03d:%02d.%04d-%c", longdeg, longmin, mlong, long_dir[0]);
      fprintf(dout," Alt=%05.1f sats=%02d", altitude, sats);
      fprintf(dout," hdop=%04.1f fix=%d", hdop, pos_fix);
      fprintf(dout," SYS-GPS=%5.3Lf wait=%5.3Lf\n", diff_sec, wait_sec);
      fflush(dout);
    }
}

extract_GGA_data()
{
    float  latsec, longsec;
    int    itime, mtime, ilat, mlat, ilong, mlong;
    int    date, year, month, day, hour, min, sec, msec;
    int    latdeg, latmin, longdeg, longmin;
    char   valid_flag[10], lat_dir[10], long_dir[10];

//    fprintf(stderr,"GGA data detected: %s\n", buf_nmea);

    if (12==sscanf(buf_nmea,"$GPGGA,%6d.%3d,%4d.%4d%[NS,]%5d.%4d%[EW,]%d,%2d,%f,%f", \
                   &itime, &msec, &ilat, &mlat, lat_dir, &ilong, &mlong, long_dir, \
                   &pos_fix, &sats, &hdop, &altitude))
    {
      hour  = itime/10000;
      min   = (itime%10000)/100;
      sec   = itime%100;

      latdeg   = ilat/100;
      latmin   = ilat%100;
      latsec   = (float) mlat * 0.006;

      longdeg  = ilong/100;
      longmin  = ilong%100;
      longsec  = (float) mlong * 0.006;
    }
}

int calc_daytime(int year,int month,int day,int hh,int mm,int ss)
{
  /* author credits: M.Kohama for K2K GPS, Japan, 4/7/1999 */

  static int year0 = 1970;
  static int m[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int i,daytime,k1,k2,k3,k4,d0,u1,time0,timeu0;
 
  u1 = 24 * 3600;
  d0 = 365 * 24 * 3600;
 
  timeu0 = (year0-1)/4 - (year0-1)/100 + (year0-1)/400;
  time0 = (year0-1) * 365 + timeu0;
 
  k1 = (year-1)/4 - (year-1)/100 + (year-1)/400;
  k2 = ((year-1) * 365 + k1 - time0 )* u1;
 
  k3 = 0;
  for(i=0;i<month-1;i++) k3 += u1*m[i];
  if ((year%4)==0 && ((year%100)!=0 || (year%400)==0) && month>2) k3 += u1;
 
  k4 = (day-1) * u1 + 3600 * hh + 60 * mm + ss;
  daytime = k2 + k3 + k4;
  return daytime;
}
