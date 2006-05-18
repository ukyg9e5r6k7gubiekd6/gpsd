AC_INIT
AM_INIT_AUTOMAKE(gpsd, 2.33)
AC_PREFIX_PROGRAM(gcc)
AM_CONFIG_HEADER(config.h)
AC_LANG([C])

AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_CXX
AC_C_BIGENDIAN
AC_FUNC_ALLOCA
AC_STDC_HEADERS
AC_C_CONST

if eval "test x$GCC = xyes"; then
	CFLAGS="$CFLAGS -Wall -Wcast-align -Wmissing-declarations -Wmissing-prototypes -Wstrict-prototypes -Wpointer-arith -Wreturn-type -D_GNU_SOURCE"
	dnl -Wcast-qual -W
fi

AC_CHECK_HEADERS(termios.h sys/termios.h sys/select.h strings.h sys/time.h X11/xpm.h xpm.h getopt.h sys/modem.h sys/ipc.h sys/shm.h)

AC_CHECK_FUNCS(round, roundf)

AC_HEADER_TIME
AC_STRUCT_TIMEZONE

AC_CACHE_CHECK(for timezone variable, ac_cv_var_timezone,
        AC_TRY_COMPILE([
#include <time.h>
        ], [
                timezone = 1;
        ], ac_cv_var_timezone=yes, ac_cv_var_timezone=no))
if test $ac_cv_var_timezone = yes; then
        AC_DEFINE(HAVE_TIMEZONE, [], [Have timezone variable])
else
        AC_CACHE_CHECK(for tm_gmtoff in struct tm, ac_cv_struct_tm_gmtoff,
                AC_TRY_COMPILE([
#include <time.h>
                        ], [
                        struct tm tm;
                        tm.tm_gmtoff = 1;
                        ], ac_cv_struct_tm_gmtoff=yes, ac_cv_struct_tm_gmtoff=no
))
        if test $ac_cv_struct_tm_gmtoff = yes; then
                AC_DEFINE(HAVE_TM_GMTOFF, [], [struct tm has tm_gmtoff])
        else
                AC_ERROR(unable to find a way to determine timezone)
        fi
fi            
AC_CACHE_CHECK(for daylight external, mb_cv_var_daylight,
[  AC_TRY_LINK([#include <time.h>], [return (int)daylight;],
      mb_cv_var_daylight=yes,
      mb_cv_var_daylight=no)
])
if test $mb_cv_var_daylight = yes; then
   AC_DEFINE([HAVE_DAYLIGHT], 1,
           [Define if you have the external `daylight' variable.])
fi

AC_CHECK_LIB(nsl, gethostbyname, LIBNSL="-lnsl")
AC_SUBST(LIBNSL)
AC_CHECK_LIB(socket, socket, LIBSOCKET="-lsocket")
AC_SUBST(LIBSOCKET)
AC_CHECK_LIB(m, rint, LIBM="-lm")
AC_SUBST(LIBM)
AC_CHECK_LIB(c, open, LIBC="-lc")
AC_SUBST(LIBC)
AC_CHECK_LIB(pthread, main, LIBPTHREAD="-lpthread")
AC_SUBST(LIBPTHREAD)

AH_VERBATIM([_GNU_SOURCE],
          [/* Macro for declaring function arguments unused. */
#if defined(__GNUC__)
#  define UNUSED __attribute__((unused)) /* Flag variable as unused */
#else /* not __GNUC__ */
#  define UNUSED
#endif])

dnl Check for X libraries
AC_PATH_XTRA
LIBS="$LIBS $X_EXTRA_LIBS"
CFLAGS="$CFLAGS $X_CFLAGS"
CPPFLAGS="$CPPFLAGS $X_CFLAGS"
LDFLAGS="$LDFLAGS $X_LIBS"
AC_SUBST(X_LIBS)
AC_SUBST(X11_LIBS)
AC_SUBST(X_PRE_LIBS)

dnl Check for some auxiliary X libraries we'll need
AC_CHECK_LIB(Xt, XtToolkitInitialize, XT_LIBS="-lXt",,
	${X_PRE_LIBS} ${X11_LIBS})
AC_SUBST(XT_LIBS)
AC_CHECK_LIB(Xaw, XawInitializeWidgetSet, XAW_LIBS="-lXaw",,
	${X_PRE_LIBS} ${X11_LIBS})
AC_SUBST(XAW_LIBS)
AC_CHECK_LIB(Xext, XShapeQueryVersion, XEXT_LIBS="-lXext",,
	${X_PRE_LIBS} ${X11_LIBS})
AC_SUBST(XEXT_LIBS)
AC_CHECK_LIB(Xpm, XpmCreatePixmapFromXpmImage,
	XPM_LIBS="-lXpm"
	AC_DEFINE([HAVE_XPM],1,[Xpm available]),,${X_PRE_LIBS} ${X11_LIBS})
AC_SUBST(XPM_LIBS)

dnl Check for Motif widget libraries
XM_LIBS=""
XP_LIBS=""
if test "$motif_includes" != "no" && test "$motif_libraries" != "no"
then
dnl Motif 2.1 wants `-lXp' (X11R6.3 print server)
AC_CHECK_LIB(Xp, XpSelectInput,
	XP_LIBS="-lXp"
	AC_DEFINE([HAVE_XP],1,[Xp available]),,
	${X_PRE_LIBS} ${XPM_LIBS} ${XEXT_LIBS} ${XT_LIBS} ${XEXT_LIBS} ${X11_LIBS} ${LIBGEN})
dnl
AC_CHECK_LIB(Xm, XmCreateOptionMenu, 
	XM_LIBS="-lXm"
	AC_DEFINE([HAVE_MOTIF],1,[Motif available]),,
	${X_PRE_LIBS} ${XP_LIBS} ${XPM_LIBS} ${XEXT_LIBS} ${XT_LIBS} ${XEXT_LIBS} ${X11_LIBS} ${LIBGEN})
fi
AC_SUBST(XP_LIBS)
AC_SUBST(XM_LIBS)
AM_CONDITIONAL([HAVE_MOTIF], [test "$XM_LIBS" != ""])

dnl check for NMEA support
AC_ARG_ENABLE(nmea,
  AC_HELP_STRING([--disable-nmea],
                 [disable NMEA support]),
  [ac_nmea=$enableval], [ac_nmea=yes])
AC_MSG_CHECKING([for NMEA support])
if test x"$ac_nmea" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([NMEA_ENABLE], 1, [NMEA chipset support])
else
  AC_MSG_RESULT([no])
fi

dnl check for SiRF-II support
AC_ARG_ENABLE(sirfII,
  AC_HELP_STRING([--disable-sirfII],
                 [disable SiRF-II chipset support]),
  [ac_sirfII=$enableval], [ac_sirfII=yes])
AC_MSG_CHECKING([for SiRF-II support])
if test x"$ac_sirfII" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([SIRFII_ENABLE], 1, [SiRF-II chipset support])
else
  AC_MSG_RESULT([no])
fi

dnl check for Trimble TSIP support
AC_ARG_ENABLE(tsip,
  AC_HELP_STRING([--disable-tsip],
                 [disable Trimble TSIP support]),
  [ac_tsip=$enableval], [ac_tsip=yes])
AC_MSG_CHECKING([for Trimble TSIP support])
if test x"$ac_tsip" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([TSIP_ENABLE], 1, [Trimble TSIP support])
else
  AC_MSG_RESULT([no])
fi

dnl check for FV-18 support
AC_ARG_ENABLE(fv18,
  AC_HELP_STRING([--disable-fv18],
                 [disable San Jose Navigation FV-18 support]),
  [ac_fv18=$enableval], [ac_fv18=yes])
AC_MSG_CHECKING([for FV-18 support])
if test x"$ac_fv18" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([FV18_ENABLE], 1, [San Jose Navigation FV-18 support])
else
  AC_MSG_RESULT([no])
fi

dnl check for Tripmate support
AC_ARG_ENABLE(tripmate,
  AC_HELP_STRING([--disable-tripmate],
                 [disable DeLorme TripMate support]),
  [ac_tripmate=$enableval], [ac_tripmate=yes])
AC_MSG_CHECKING([for Tripmate support])
if test x"$ac_tripmate" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([TRIPMATE_ENABLE], 1, [DeLorme TripMate support])
else
  AC_MSG_RESULT([no])
fi

dnl check for EarthMate support
AC_ARG_ENABLE(earthmate,
  AC_HELP_STRING([--disable-earthmate],
                 [disable DeLorme EarthMate Zodiac support]),
  [ac_earthmate=$enableval], [ac_earthmate=yes])
AC_MSG_CHECKING([for EarthMate support])
if test x"$ac_earthmate" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([EARTHMATE_ENABLE], 1, [DeLorme EarthMate Zodiac support])
else
  AC_MSG_RESULT([no])
fi

dnl check for iTrax support
AC_ARG_ENABLE(itrax,
  AC_HELP_STRING([--disable-itrax],
                 [disable iTrax support]),
  [ac_itrax=$enableval], [ac_itrax=yes])
AC_MSG_CHECKING([for iTrax support])
if test x"$ac_itrax" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([ITRAX_ENABLE], 1, [iTrax chipset support])
else
  AC_MSG_RESULT([no])
fi

dnl check for italk support
AC_ARG_ENABLE(italk,
  AC_HELP_STRING([--disable-italk],
                 [disable iTalk support]),
  [ac_italk=$enableval], [ac_italk=yes])
AC_MSG_CHECKING([for iTalk support])
if test x"$ac_italk" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([ITALK_ENABLE], 1, [iTalk binary support])
else
  AC_MSG_RESULT([no])
fi

dnl check for Garmin support
AC_ARG_ENABLE(garmin,
  AC_HELP_STRING([--disable-garmin],
                 [disable Garmin kernel driver support]),
  [ac_garmin=$enableval], [ac_garmin=yes])
AC_MSG_CHECKING([for Garmin support])
if test x"$ac_garmin" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([GARMIN_ENABLE], 1, [Garmin support])
else
  AC_MSG_RESULT([no])
fi

dnl check for True North support
AC_ARG_ENABLE(tnt,
  AC_HELP_STRING([--enable-tnt],
                 [enable True North Technologies support]),
  [ac_tnt=$enableval], [ac_tnt=no])
AC_MSG_CHECKING([for True North support])
if test x"$ac_tnt" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([TNT_ENABLE], 1, [True North Technologies support])
else
  AC_MSG_RESULT([no])
fi

dnl check for Evermore support
AC_ARG_ENABLE(evermore,
  AC_HELP_STRING([--disable-evermore],
                 [disable Evermore binary support]),
  [ac_evermore=$enableval], [ac_evermore=yes])
AC_MSG_CHECKING([for Evermore support])
if test x"$ac_evermore" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([EVERMORE_ENABLE], 1, [Evermore binary support])
else
  AC_MSG_RESULT([no])
fi

dnl check for RTCM104 support
AC_ARG_ENABLE(rtcm104,
  AC_HELP_STRING([--disable-rtcm104],
                 [disable rtcm104 support]),
  [ac_rtcm104=$enableval], [ac_rtcm104=yes])
AC_MSG_CHECKING([for rtcm104 support])
if test x"$ac_rtcm104" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([RTCM104_ENABLE], 1, [rtcm104 binary support])
else
  AC_MSG_RESULT([no])
fi

AM_CONDITIONAL([HAVE_RTCM104], [test "$ac_rtcm104" = "yes"])

dnl check for profiling support
AC_ARG_ENABLE(profiling,
  AC_HELP_STRING([--enable-profiling],
                 [enable profiling support]),
  [ac_profiling=$enableval], [ac_profiling=no])
AC_MSG_CHECKING([for profiling support])
if test x"$ac_profiling" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([PROFILING], 1, [profiling support])
else
  AC_MSG_RESULT([no])
fi

dnl check for NTP time hinting support
AC_ARG_ENABLE(ntpshm,
  AC_HELP_STRING([--disable-ntpshm],
                 [disable NTP time hinting support]),
  [ac_ntpshm=$enableval], [ac_ntpshm=yes])
AC_MSG_CHECKING([for NTP time hinting support])
if test x"$ac_ntpshm" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([NTPSHM_ENABLE], 1, [NTP time hinting support])
else
  AC_MSG_RESULT([no])
fi

dnl check for PPS time syncing support
AC_ARG_ENABLE(pps,
  AC_HELP_STRING([--disable-pps],
                 [disable PPS time syncing support]),
  [ac_pps=$enableval], [ac_pps=yes])
AC_MSG_CHECKING([for PPS time syncing support])
if test x"$ac_pps" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([PPS_ENABLE], 1, [PPS time syncing support])
else
  AC_MSG_RESULT([no])
fi

dnl check for port speed fixed at compile-time
AC_ARG_ENABLE(fixed-port-speed,
  AC_HELP_STRING([--enable-fixed-port-speed],
                 [compile with fixed serial port speed]),
  [ac_baud=$enableval], [ac_baud=no])
AC_MSG_CHECKING([for fixed port speed])
if test x"$ac_baud" != "xno"; then
    AC_MSG_RESULT([$ac_baud])
	AC_DEFINE_UNQUOTED([FIXED_PORT_SPEED], $ac_baud, [Fixed port speed])
	FIXED_PORT_SPEED="-DSPEEDFLAGS=$ac_baud"
else
  AC_MSG_RESULT([no])
fi

dnl Manually configure DBUS until we figure out a 
dnl distro-independent was to check for both libraries and headers
AC_ARG_ENABLE(dbus,
    AC_HELP_STRING([--enable-dbus],
                [enable DBUS support]),
    [ac_dbus=$enableval], [ac_dbus=no])
AC_MSG_CHECKING([for DBUS support])
if test x"$ac_dbus" == "xyes"; then
    AC_MSG_RESULT([yes])
    AC_DEFINE([DBUS_ENABLE], 1, [DBUS support])
    # Older versions of autotools barf and die on this.
    #PKG_CHECK_MODULES(DBUS, dbus-1 >= 0.23.4 )
    DBUS_CFLAGS=`pkg-config --cflags dbus-glib-1`
    DBUS_LIBS=`pkg-config --libs dbus-1`
    AC_SUBST(DBUS_CFLAGS)
    AC_SUBST(DBUS_LIBS)
    #PKG_CHECK_MODULES(DBUS_GLIB, dbus-glib-1 >= 0.22 )
    DBUS_GLIB_LIBS=`pkg-config --libs dbus-glib-1`
    AC_SUBST(DBUS_GLIB_CFLAGS)
    AC_SUBST(DBUS_GLIB_LIBS)
else
    AC_MSG_RESULT([no])
fi

dnl Automatic check for DBUS.
dnl It's broken -- leads to bad compiles on systems with DBUS libraies
dnl but no DBUS headers.
dnl ac_dbus=no
dnl AC_CHECK_LIB(dbus-1, dbus_bus_get, [true], [false]) # do not update $LIBS 
dnl if test x"yes" = x"$ac_cv_lib_dbus_1_dbus_bus_get" ; then
dnl    # Found dbus library, check for required versions and enable it
dnl 
dnl    AC_MSG_CHECKING([for DBUS support])
dnl    PKG_CHECK_MODULES(DBUS, dbus-1 >= 0.23.4,
dnl       [ac_dbus=yes],[ac_dbus=no])
dnl    AC_MSG_RESULT([$ac_dbus])
dnl    AC_SUBST(DBUS_CFLAGS)
dnl    AC_SUBST(DBUS_LIBS)
dnl 
dnl    if test x"ac_dbus" = x"yes" ; then
dnl       AC_DEFINE([DBUS_ENABLE],1,[Found DBUS libraries])
dnl    fi
dnl 
dnl    # Check for glib and dbus-glib, used by gpxlogger
dnl    PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.2.0 )
dnl    AC_SUBST(GLIB_CFLAGS)
dnl    AC_SUBST(GLIB_LIBS)
dnl 
dnl    PKG_CHECK_MODULES(DBUS_GLIB, dbus-glib-1 >= 0.22)
dnl    AC_SUBST(DBUS_GLIB_CFLAGS)
dnl    AC_SUBST(DBUS_GLIB_LIBS)
dnl fi
AM_CONDITIONAL([HAVE_DBUS], [test x"$ac_dbus" = x"yes"])

AC_OUTPUT(Makefile gpsd.spec)

dnl Output the configuration summary
echo ""
echo "=========================================="
echo "$PACKAGE $VERSION"
echo "------------------------------------------"
echo "NMEA                    : $ac_nmea"
echo "FV-18                   : $ac_fv18"
echo "SiRF-II                 : $ac_sirfII"
echo "Trimble TSIP            : $ac_tsip"
echo "Tripmate                : $ac_tripmate"
echo "Earthmate               : $ac_earthmate"
echo "iTrax                   : $ac_itrax"
echo "iTalk                   : $ac_italk"
echo "Garmin                  : $ac_garmin"
echo "True North              : $ac_tnt"
echo "Evermore                : $ac_evermore"
echo "RTCM104 support         : $ac_rtcm104"
echo "NTP SHM                 : $ac_ntpshm"
echo "NTP PPS                 : $ac_pps"
echo "Fixed port speed        : $ac_baud"
echo "Enable shared libraries : $enable_shared"
echo "Enable DBUS support     : $ac_dbus"
echo "------------------------------------------"
echo "Configure finished, type 'make' to build."

