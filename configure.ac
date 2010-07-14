dnl This file is Copyright (c)2010 by the GPSD project
dnl BSD terms apply: see the file COPYING in the distribution root for details.

AC_INIT
AM_INIT_AUTOMAKE(gpsd, 2.96~dev)
dnl AC_PREFIX_PROGRAM(gcc)
AM_CONFIG_HEADER(gpsd_config.h)
AC_LANG([C])

# ACREQUIRE_BUGFIX
# ----------------
# Due to a longstanding Autoconf bug (Autoconf 2.50 to at least 2.63),
# any macro that is AC_REQUIREd at any point must be AC_REQUIREd
# *before* it is directly expanded.  The macros below were being
# directly expanded before being AC_REQUIREd, so we AC_REQUIRE them
# early to prevent out-of-order expansion problems.  See the threads
# at:
# http://lists.gnu.org/archive/html/bug-autoconf/2008-12/msg00039.html
# http://lists.gnu.org/archive/html/autoconf-patches/2008-12/msg00058.html
# http://lists.gnu.org/archive/html/bug-autoconf/2009-01/msg00019.html
# http://lists.gnu.org/archive/html/bug-gnulib/2009-01/msg00247.html
AC_DEFUN_ONCE([ACREQUIRE_BUGFIX],
[
  AC_REQUIRE([AC_PROG_CPP])
  AC_REQUIRE([AC_PROG_EGREP])
  AC_REQUIRE([AC_PROG_CC])
])
ACREQUIRE_BUGFIX
# ACREQUIRE_BUGFIX done

dnl AM_PATH_PYTHON provided with automake can be too old.  Look
dnl for newer python first, and include 2.6 on the list.
m4_define([_AM_PYTHON_INTERPRETER_LIST],
	    [python2.6 python2.5 python2.4 python])
AM_PATH_PYTHON([2.4])
ac_python=yes
if test "x$PYTHON" = "x"; then
	AC_PATH_PROG(PYTHON, python, none)
fi

if test "x$PYTHON" = "xnone"; then
AC_MSG_WARN([*** Python interpreter not found, Python support disabled.])
	ac_python=no
fi

if test "x$ac_python" = "xyes"; then
	AC_MSG_CHECKING(Python version and location)
	PYTHON_PREFIX=`$PYTHON -c "import sys; print(sys.prefix)"`
	PYTHON_VERSION_MAJOR=[`$PYTHON -c "import sys; print('%d' % (sys.version_info[0]));"`]
	PYTHON_VERSION_MINOR=[`$PYTHON -c "import sys; print('%d' % (sys.version_info[1]));"`]
	PYTHON_VERSION="${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}"
	AC_MSG_RESULT([$PYTHON, $PYTHON_VERSION, $PYTHON_PREFIX])

	PYTHON_CFLAGS="-DHAVE_PYTHON -I$PYTHON_PREFIX/include/python$PYTHON_VERSION"

	# Define the directories we ask setup.py to install the
	# modules/extensions and scripts to. The way chosen here reproduces
	# the internal behaviour of distutils. Unfortunately distutils does
	# not export the pre-defined/configured directories, so we have to
	# define them on our own. For default installations of distutils the
	# chosen values here will match what distutils uses.
	# See Makefile.am to see how they're used with setup.py.
	PYTHON_DISTUTILS_LIBDIR=[`$PYTHON -c 'import distutils.util; import sys; print ("build/lib.%s-%s" %(distutils.util.get_platform(), sys.version[0:3]))'`]
	PYTHON_DISTUTILS_SCRIPTDIR=[`$PYTHON  -c 'import sys; print ("build/scripts-%s" %(sys.version[0:3], ))'`]

	OLD_CPPFLAGS="$CPPFLAGS"
	OLD_CXXFLAGS="$CXXFLAGS"
	CPPFLAGS="$CPPFLAGS $PYTHON_CFLAGS"
	CXXFLAGS="$CXXFLAGS $PYTHON_CFLAGS"

	AC_CHECK_HEADERS([Python.h],
			[],
			[AC_MSG_WARN([*** Python include files not found! You should install the Python development package. Python support disabled]); ac_python=no])
	CPPFLAGS="$OLD_CPPFLAGS"
	CXXFLAGS="$OLD_CXXFLAGS"

	if test "x$ac_python" = "xyes"; then
		AC_SUBST([PYTHON_CFLAGS])
		AC_SUBST([PYTHON_DISTUTILS_LIBDIR])
		AC_SUBST([PYTHON_DISTUTILS_SCRIPTDIR])

		ac_python=no
		for pylibpath in '/usr/lib' $PYTHON_PREFIX/lib $PYTHON_PREFIX/lib/python$PYTHON_VERSION/config; do
			eval `echo unset ac_cv_lib_python$PYTHON_VERSION'___'Py_Finalize | tr '.' '_'`

			save_LIBS=$LIBS
			LIBS="$LIBS -L$pylibpath $PYTHON_LIBS"
			AC_CHECK_LIB(python$PYTHON_VERSION, Py_Finalize, PYTHON_LIBS="-L$pylibpath -lpython$PYTHON_VERSION $PYTHON_DEPS"; ac_python=yes,,$PYTHON_DEPS)
			LIBS=$save_LIBS
			if test "x$ac_python" = "xyes"; then
				break
			fi
		done

		if test "x$ac_python" != "xyes"; then
			AC_MSG_WARN(*** Python development libraries required, Python support disabled)
		fi
		AC_SUBST([PYTHON_LIBS])

		AC_SUBST(pkgpythondir)
		if test "x$python_install" = "xyes"; then
			pkgpythondir=$PYTHON_PREFIX"/lib/python"$PYTHON_VERSION"/site-packages/gpsd"
		fi
	fi
fi
AM_CONDITIONAL([HAVE_PYTHON], [test x"$ac_python" = xyes])

AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_CXX
AC_C_BIGENDIAN
type_error="no"
AC_CHECK_SIZEOF([char])
AC_CHECK_SIZEOF([short])
AC_CHECK_SIZEOF([int])
AC_CHECK_SIZEOF([long])
AC_CHECK_SIZEOF([long long])
AC_CHECK_SIZEOF([float])
AC_CHECK_SIZEOF([double])
if test x"$ac_cv_sizeof_char" != "x1" ; then
	AC_MSG_WARN(gpsd requires sizeof(char)==1);
	type_error="yes"
fi
if test x"$ac_cv_sizeof_short" != "x2" ; then
	AC_MSG_WARN(gpsd requires sizeof(short)==2);
	type_error="yes"
fi
if test x"$ac_cv_sizeof_int" != "x4" ; then
	AC_MSG_WARN(gpsd requires sizeof(int)==4);
	type_error="yes"
fi
if test x"$ac_cv_sizeof_long_long" != "x8" ; then
	AC_MSG_WARN(gpsd requires sizeof(long long)==8);
	type_error="yes"
fi
if test x"$ac_cv_sizeof_float" != "x4" ; then
	AC_MSG_WARN(gpsd requires sizeof(float)==4);
	type_error="yes"
fi
if test x"$ac_cv_sizeof_double" != "x8" ; then
	AC_MSG_WARN(gpsd requires sizeof(double)==8);
	type_error="yes"
fi
if test x"$type_error" = "xyes" ; then
	AC_ERROR(Your system does not provide all required data types);
fi

AC_FUNC_ALLOCA
AC_STDC_HEADERS
AC_C_CONST

AC_MSG_CHECKING([for properly working floating point implementation])
if test "x$build" = "x$host"; then
	if eval "$CC $CFLAGS -o test_float ${srcdir}/test_float.c"; then
		if ./test_float > /dev/null; then
			AC_MSG_RESULT([yes])
		else
			AC_MSG_RESULT([no])
			AC_MSG_WARN([We strongly recommend you manually examine the test_float results])
		fi
	else
		AC_MSG_RESULT([failure compiling test_float])
	fi
else
	AC_MSG_RESULT([skipped test (cross-compiling)])
	AC_MSG_WARN([We are cross-compiling, and thus cannot run the floating point test now.
We strongly recommend running test_float on the target platform.])
fi


if eval "test x$GCC = xyes"; then
	CFLAGS="$CFLAGS -Wall -Wcast-align -Wmissing-declarations -Wmissing-prototypes -Wstrict-prototypes -Wpointer-arith -Wreturn-type -D_GNU_SOURCE"
	dnl -Wcast-qual -W
fi

AC_CHECK_HEADERS(sys/termios.h sys/select.h sys/time.h sys/modem.h sys/ipc.h sys/shm.h sys/stat.h sys/socket.h sys/ioctl.h sys/un.h)
AC_CHECK_HEADERS(arpa/inet.h netinet/in_systm.h netinet/in.h netinet/tcp.h)
AC_CHECK_HEADERS([netinet/ip.h], [], [],
[[#if HAVE_NETINET_IN_SYSTM_H && HAVE_NETINET_IN_H
#include netinet/ip.h
#endif
]])
AC_CHECK_HEADERS(termios.h strings.h getopt.h netdb.h syslog.h pwd.h grp.h)

AC_CHECK_FUNCS(round, roundf)
AC_CHECK_FUNCS(strlcpy)
AC_CHECK_FUNCS(strlcat)
AC_CHECK_FUNCS(strtonum)
AC_CHECK_FUNCS(setlocale)
AC_CHECK_FUNCS(vsnprintf)

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
	   [Define if you have the external 'daylight' variable.])
fi

AC_CHECK_LIB(nsl, gethostbyname, LIBNSL="-lnsl")
AC_SUBST(LIBNSL)
AC_CHECK_LIB(socket, socket, LIBSOCKET="-lsocket")
AC_SUBST(LIBSOCKET)
AC_CHECK_LIB(m, rint, LIBM="-lm")
AC_SUBST(LIBM)
AC_CHECK_LIB(c, open, LIBC="-lc")
AC_SUBST(LIBC)
AC_CHECK_LIB(pthread, pthread_setcancelstate,
	[LIBPTHREAD="-lpthread"
	AC_DEFINE([HAVE_LIBPTHREAD], [], [pthread libraries are present])])
AC_SUBST(LIBPTHREAD)

PKG_CHECK_MODULES(LIBUSB, libusb-1.0 >= 1.0.0, [ac_libusb=yes], [ac_libusb=no])
AC_SUBST(LIBUSB_LIBS)
AC_SUBST(LIBUSB_CFLAGS)
if test x"$ac_libusb" = x"yes" ; then
	AC_DEFINE([HAVE_LIBUSB], 1, [libusb support])
fi

AH_VERBATIM([_GNU_SOURCE],
	  [/* Some libc's don't have strlcat/strlcpy. Local copies are provided */
#ifndef HAVE_STRLCAT
# ifdef __cplusplus
extern "C" {
# endif
size_t strlcat(/*@out@*/char *dst, /*@in@*/const char *src, size_t size);
# ifdef __cplusplus
}
# endif
#endif
#ifndef HAVE_STRLCPY
# ifdef __cplusplus
extern "C" {
# endif
size_t strlcpy(/*@out@*/char *dst, /*@in@*/const char *src, size_t size);
# ifdef __cplusplus
}
# endif
#endif

#define GPSD_CONFIG_H])



dnl Check for curses headers and libraries.  The presence of the
dnl library is used to decide whether or not to build some programs.
dnl Those programs use ncurses.h if found, and otherwise use curses.h.
dnl TODO: give an example of what system uses curses.h with
dnl libncurses.  We define our own variables to reduce the risk of
dnl breaking if autoconf changes (private) variable names.
AC_CHECK_HEADERS(ncurses.h)
AC_CHECK_LIB(ncurses, initscr, NCURSES_LIBS="-lncurses")
AC_SUBST(NCURSES_LIBS)
if test x"$NCURSES_LIBS" = x"" ; then
  AC_MSG_WARN([Not including curses support])
  AM_CONDITIONAL([HAVE_NCURSES],false)
  ac_ncurses="no"
else
  AM_CONDITIONAL([HAVE_NCURSES],true)
  ac_ncurses="yes"
fi

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

dnl check for OnCore support
AC_ARG_ENABLE(oncore,
  AC_HELP_STRING([--disable-oncore],
		 [disable Motorola OnCore chipset support]),
  [ac_oncore=$enableval], [ac_oncore=yes])
AC_MSG_CHECKING([for Motorola OnCore support])
if test x"$ac_oncore" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([ONCORE_ENABLE], 1, [Motorola OnCore chipset support])
else
  AC_MSG_RESULT([no])
fi

dnl check for SiRF support
AC_ARG_ENABLE(sirf,
  AC_HELP_STRING([--disable-sirf],
		 [disable SiRF chipset support]),
  [ac_sirf=$enableval], [ac_sirf=yes])
AC_MSG_CHECKING([for SiRF support])
if test x"$ac_sirf" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([SIRF_ENABLE], 1, [SiRF chipset support])
else
  AC_MSG_RESULT([no])
fi

dnl check for Novatel SuperStarII support
AC_ARG_ENABLE(superstar2,
  AC_HELP_STRING([--disable-superstar2],
		 [disable SuperStarII chipset support]),
  [ac_superstar2=$enableval], [ac_superstar2=yes])
AC_MSG_CHECKING([for SuperStarII support])
if test x"$ac_superstar2" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([SUPERSTAR2_ENABLE], 1, [SuperStarII chipset support])
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
    ac_nmea=yes
    AC_MSG_RESULT([yes])
	AC_DEFINE([FV18_ENABLE], 1, [San Jose Navigation FV-18 support])
	AC_DEFINE([NMEA_ENABLE], 1, [San Jose Navigation FV-18 requires NMEA support])
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
		 [disable iTrax hardware support]),
  [ac_itrax=$enableval], [ac_itrax=yes])
AC_MSG_CHECKING([for iTrax support])
if test x"$ac_itrax" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([ITRAX_ENABLE], 1, [iTrax chipset support])
else
  AC_MSG_RESULT([no])
fi

dnl check for Ashtech support
AC_ARG_ENABLE(ashtech,
  AC_HELP_STRING([--disable-ashtech],
		 [disable Ashtech support]),
  [ac_ashtech=$enableval], [ac_ashtech=yes])
AC_MSG_CHECKING([for Ashtech support])
if test x"$ac_ashtech" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([ASHTECH_ENABLE], 1, [Ashtech chipset support])
else
  AC_MSG_RESULT([no])
fi

dnl check for Navcom support
AC_ARG_ENABLE(navcom,
  AC_HELP_STRING([--disable-navcom],
		 [disable Navcom support]),
  [ac_navcom=$enableval], [ac_navcom=yes])
AC_MSG_CHECKING([for Navcom support])
if test x"$ac_navcom" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([NAVCOM_ENABLE], 1, [Navcom support])
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

dnl check for Garmin Simple Text support
AC_ARG_ENABLE(garmintxt,
  AC_HELP_STRING([--enable-garmintxt],
		 [enable Garmin Simple Text support]),
  [ac_garmintxt=$enableval], [ac_garmintxt=yes])
AC_MSG_CHECKING([for Garmin Simple Text support])
if test x"$ac_garmintxt" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([GARMINTXT_ENABLE], 1, [Garmin Simple Text support])
else
  AC_MSG_RESULT([no])
fi

dnl check for True North support
AC_ARG_ENABLE(tnt,
  AC_HELP_STRING([--enable-tnt],
		 [disable True North Technologies support]),
  [ac_tnt=$enableval], [ac_tnt=yes])
AC_MSG_CHECKING([for True North support])
if test x"$ac_tnt" = "xyes"; then
    ac_nmea=yes
    AC_MSG_RESULT([yes])
	AC_DEFINE([TNT_ENABLE], 1, [True North Technologies support])
	AC_DEFINE([NMEA_ENABLE], 1, [True North requires NMEA support])
else
  AC_MSG_RESULT([no])
fi

dnl check for OceanServer support
AC_ARG_ENABLE(oceanserver,
  AC_HELP_STRING([--disable-oceanserver],
		 [disable OceanServer support]),
  [ac_oceanserver=$enableval], [ac_oceanserver=yes])
AC_MSG_CHECKING([for OceanServer support])
if test x"$ac_oceanserver" = "xyes"; then
    ac_nmea=yes
    AC_MSG_RESULT([yes])
	AC_DEFINE([OCEANSERVER_ENABLE], 1, [OceanServer support])
	AC_DEFINE([NMEA_ENABLE], 1, [OceanServer requires NMEA support])
else
  AC_MSG_RESULT([no])
fi

dnl check for UBX support
AC_ARG_ENABLE(ubx,
  AC_HELP_STRING([--disable-ubx],
		 [disable UBX Protocol support]),
  [ac_ubx=$enableval], [ac_ubx=yes])
AC_MSG_CHECKING([for UBX support])
if test x"$ac_ubx" = "xyes"; then
  AC_MSG_RESULT([yes])
  AC_DEFINE([UBX_ENABLE], 1, [UBX Protocol support])
else
  AC_MSG_RESULT([no])
fi

dnl check for EverMore support
AC_ARG_ENABLE(evermore,
  AC_HELP_STRING([--disable-evermore],
		 [disable EverMore binary support]),
  [ac_evermore=$enableval], [ac_evermore=yes])
AC_MSG_CHECKING([for EverMore support])
if test x"$ac_evermore" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([EVERMORE_ENABLE], 1, [EverMore binary support])
else
  AC_MSG_RESULT([no])
fi

dnl check for MTK-3301 support
AC_ARG_ENABLE(mtk3301,
  AC_HELP_STRING([--disable-mtk3301],
		 [disable MTK-3301 support]),
  [ac_mtk3301=$enableval], [ac_mtk3301=yes])
AC_MSG_CHECKING([for MTK-3301 support])
if test x"$ac_mtk3301" = "xyes"; then
    ac_nmea=yes
    AC_MSG_RESULT([yes])
	AC_DEFINE([MTK3301_ENABLE], 1, [MTK-3301 support])
	AC_DEFINE([NMEA_ENABLE], 1, [MTK-3301 requires NMEA support])
else
  AC_MSG_RESULT([no])
fi

dnl check for GPSClock support
AC_ARG_ENABLE(gpsclock,
  AC_HELP_STRING([--disable-gpsclock],
		 [disable GPSClock support]),
  [ac_gpsclock=$enableval], [ac_gpsclock=yes])
AC_MSG_CHECKING([for GPSClock support])
if test x"$ac_gpsclock" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([GPSCLOCK_ENABLE], 1, [GPSclock chipset support])
else
  AC_MSG_RESULT([no])
fi

dnl check for RTCM104V2 support
AC_ARG_ENABLE(rtcm104v2,
  AC_HELP_STRING([--disable-rtcm104v2],
		 [disable rtcm104v2 support]),
  [ac_rtcm104v2=$enableval], [ac_rtcm104v2=yes])
AC_MSG_CHECKING([for rtcm104v2 support])
if test x"$ac_earthmate" = "xno" -a x"$ac_evermore" = "xno" -a x"$ac_garmin" = "xno" -a x"$ac_itrax" = "xno" -a x"$ac_sirf" = "xno" -a x"$ac_superstar2" = "xno" -a x"$ac_tsip" = "xno" -a x"$ac_navcom" = "xno"; then
    ac_rtcm104v2=no
fi
if test x"$ac_rtcm104v2" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([RTCM104V2_ENABLE], 1, [rtcm104v2 binary support])
else
  AC_MSG_RESULT([no])
fi

AM_CONDITIONAL([HAVE_RTCM104V2], [test "$ac_rtcm104v2" = "yes"])

dnl check for RTCM104V3 support
AC_ARG_ENABLE(rtcm104v3,
  AC_HELP_STRING([--disable-rtcm104v3],
		 [disable rtcm104v3 support]),
  [ac_rtcm104v3=$enableval], [ac_rtcm104v3=yes])
AC_MSG_CHECKING([for rtcm104v3 support])
if test x"$ac_earthmate" = "xno" -a x"$ac_evermore" = "xno" -a x"$ac_garmin" = "xno" -a x"$ac_itrax" = "xno" -a x"$ac_sirf" = "xno" -a x"$ac_tsip" = "xno" -a x"$ac_navcom" = "xno"; then
    ac_rtcm104v3=no
fi
if test x"$ac_rtcm104v3" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([RTCM104V3_ENABLE], 1, [rtcm104v3 binary support])
else
  AC_MSG_RESULT([no])
fi

AM_CONDITIONAL([HAVE_RTCM104V3], [test "$ac_rtcm104v3" = "yes"])

dnl check for NTRIP support
AC_ARG_ENABLE(ntrip,
  AC_HELP_STRING([--disable-ntrip],
		 [disable NTRIP support]),
  [ac_ntrip=$enableval], [ac_ntrip=yes])
AC_MSG_CHECKING([for NTRIP support])
if test x"$ac_ntrip" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([NTRIP_ENABLE], 1, [NTRIP support])
else
  AC_MSG_RESULT([no])
fi

AM_CONDITIONAL([HAVE_NTRIP], [test "$ac_ntrip" = "yes"])

dnl check for AIVDM support
AC_ARG_ENABLE(aivdm,
  AC_HELP_STRING([--disable-aivdm],
		 [disable Aivdm support]),
  [ac_aivdm=$enableval], [ac_aivdm=yes])
AC_MSG_CHECKING([for AIVDM support])
if test x"$ac_aivdm" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([AIVDM_ENABLE], 1, [AIVDM protocol support)])
else
  AC_MSG_RESULT([no])
fi
AM_CONDITIONAL([HAVE_AIVDM], [test "$ac_aivdm" = "yes"])

dnl check for latency timing support
AC_ARG_ENABLE(timing,
  AC_HELP_STRING([--disable-timing],
		 [disable latency timing support]),
  [ac_timing=$enableval], [ac_timing=yes])
AC_MSG_CHECKING([for latency timing support])
if test x"$ac_timing" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([TIMING_ENABLE], 1, [latency timing support)])
else
  AC_MSG_RESULT([no])
fi

dnl check for client debugging support
AC_ARG_ENABLE(clientdebug,
  AC_HELP_STRING([--disable-clientdebug],
		 [disable client debugging support]),
  [ac_clientdebug=$enableval], [ac_clientdebug=yes])
AC_MSG_CHECKING([for client debugging support])
if test x"$ac_clientdebug" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([CLIENTDEBUG_ENABLE], 1, [client debugging support)])
else
  AC_MSG_RESULT([no])
fi

AM_CONDITIONAL([CLIENTDEBUG_ENABLE], [test x"$ac_clientdebug" = x"yes"])

dnl check for support for oldstyle protocol
AC_ARG_ENABLE(oldstyle,
  AC_HELP_STRING([--disable-oldstyle],
		 [disable oldstyle (pre-JSON) protocol support]),
  [ac_oldstyle=$enableval], [ac_oldstyle=yes])
AC_MSG_CHECKING([for oldstyle support])
if test x"$ac_oldstyle" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([OLDSTYLE_ENABLE], 1, [oldstyle (pre-JSON) protocol support])
else
  AC_MSG_RESULT([no])
fi

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

dnl check for PPS input on CTS line
AC_ARG_ENABLE(pps-on-cts,
  AC_HELP_STRING([--enable-pps-on-cts],
		 [Enable PPS pulse on CTS rather than DCD]),
  [ac_ppsoncts=$enableval], [ac_ppsoncts=no])
AC_MSG_CHECKING([for PPS pulse on CTS rather than DCD])
if test x"$ac_ppsoncts" = "xyes"; then
    AC_MSG_RESULT([yes])
       AC_DEFINE([PPS_ON_CTS], 1, [PPS on CTS rather than DCD])
else
  AC_MSG_RESULT([no])
fi

dnl check for privilege revocation user at compile-time
AC_ARG_ENABLE(gpsd-user,
  AC_HELP_STRING([--enable-gpsd-user=username],
		 [GPSD privilege revocation user]),
  [ac_user=$enableval], [ac_user=nobody])
AC_MSG_CHECKING([privilege revocation user])
if test x"$ac_user" != "xnobody"; then
    AC_MSG_RESULT([$ac_user])
	AC_DEFINE_UNQUOTED([GPSD_USER], "$ac_user", [GPSD privilege revocation user])
else
  AC_MSG_RESULT([nobody])
fi

dnl check for privilege revocation group at compile-time
AC_ARG_ENABLE(gpsd-group,
  AC_HELP_STRING([--enable-gpsd-group=groupname],
		 [GPSD privilege revocation group, use if /dev/ttyS0 not found]),
  [ac_group=$enableval])
AC_MSG_CHECKING([privilege revokation group])
if test x"$ac_user" != "xnobody"; then
    AC_MSG_RESULT([$ac_group])
	AC_DEFINE_UNQUOTED([GPSD_GROUP], "$ac_group", [GPSD privilege revokation group])
fi

dnl check for port speed fixed at compile-time
AC_ARG_ENABLE(fixed-port-speed,
  AC_HELP_STRING([--enable-fixed-port-speed=nnn],
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

dnl Check for BlueZ support
AC_ARG_ENABLE(bluetooth, AC_HELP_STRING([--enable-bluetooth], [Enable support for Bluetooth GPS devices via BlueZ (experimental)]),
  [ac_bluetooth=$enableval], [ac_bluetooth=no])
AC_MSG_CHECKING([for BlueZ support])
if test x"$ac_bluetooth" = x"yes"; then
    AC_MSG_RESULT([yes])
    AC_DEFINE(HAVE_BLUEZ,1,[Define if we have Bluez])
    # Older versions of autotools barf and die on this.
    #PKG_CHECK_MODULES(BLUEZ, bluez )
    BLUEZ_CFLAGS=`pkg-config --cflags bluez`
    BLUEZ_LIBS=`pkg-config --libs bluez`
    AC_SUBST(BLUEZ_CFLAGS)
    AC_SUBST(BLUEZ_LIBS)
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
if test x"$ac_dbus" = "xyes"; then
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

dnl check for limited maximum clients
AC_ARG_ENABLE(max-clients,
  AC_HELP_STRING([--enable-max-clients=nnn],
		 [compile with limited maximum clients]),
  [ac_maxclients=$enableval], [ac_maxclients=no])
AC_MSG_CHECKING([for limited max clients])
if test x"$ac_maxclients" != "xno"; then
    AC_MSG_RESULT([$ac_maxclients])
	AC_DEFINE_UNQUOTED([LIMITED_MAX_CLIENTS], $ac_maxclients, [Limited maximum clients])
	LIMITED_MAX_CLIENTS="-DLIMITED_MAX_CLIENTS=$ac_maxclients"
else
  AC_MSG_RESULT([no])
fi

dnl check for max number of GPS devices
AC_ARG_ENABLE(max-devices,
  AC_HELP_STRING([--enable-max-devices=nnn],
		 [compile with maximum allowed devices]),
  [ac_maxdevices=$enableval], [ac_maxdevices=no])
AC_MSG_CHECKING([for max devices])
if test x"$ac_maxdevices" != "xno"; then
    AC_MSG_RESULT([$ac_maxdevices])
	AC_DEFINE_UNQUOTED([LIMITED_MAX_DEVICES], $ac_maxdevices, [Maximum gps devices])
	LIMITED_MAX_DEVICES="-DLIMITED_MAX_DEVICES=$ac_maxdevices"
else
  AC_MSG_RESULT([no])
fi

dnl allow gpsd to reconfigure gps receiver
AC_ARG_ENABLE(reconfigure,
  AC_HELP_STRING([--disable-reconfigure],
		 [do not allow gpsd to change device settings]),
  [ac_reconfigure=$enableval], [ac_reconfigure=yes])
AC_MSG_CHECKING([if device reconfiguration is allowed])
if test x"$ac_reconfigure" != "xno"; then
    AC_MSG_RESULT([$ac_reconfigure])
	AC_DEFINE([ALLOW_RECONFIGURE], 1, [Allow gpsd to reconfigure device])
else
  AC_MSG_RESULT([no])
fi

dnl allow tools to use control_send method
AC_ARG_ENABLE(controlsend,
  AC_HELP_STRING([--disable-controlsend],
		 [do not allow gpsctl/gpsmon to change device settings]),
  [ac_controlsend=$enableval], [ac_controlsend=yes])
AC_MSG_CHECKING([if control sending is allowed])
if test x"$ac_controlsend" != "xno"; then
    AC_MSG_RESULT([$ac_controlsend])
	AC_DEFINE([ALLOW_CONTROLSEND], 1, [Allow gpsd to controlsend device])
else
  AC_MSG_RESULT([no])
fi

dnl enable raw measurements
AC_ARG_ENABLE(raw,
  AC_HELP_STRING([--enable-raw],
		 [enable raw measurement processing]),
  [ac_raw=$enableval], [ac_raw=yes])
AC_MSG_CHECKING([for Raw Measurement support])
if test x"$ac_raw" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([RAW_ENABLE], 1, [Raw Measurement support])
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
AM_CONDITIONAL([HAVE_BLUEZ], [test x"$ac_bluetooth" = x"yes"])

dnl mute logging functions?
AC_ARG_ENABLE(squelch,
  AC_HELP_STRING([--enable-squelch],
		 [squelch gpsd_report and gpsd_hexdump to save cpu]),
  [ac_squelch=$enableval], [ac_squelch=no])
AC_MSG_CHECKING([for disabled logging])
if test x"$ac_squelch" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([SQUELCH_ENABLE], 1, [Squelch logging and hexdumps])
else
  AC_MSG_RESULT([no])
fi

dnl C++ bindings
if test -n "$CXX" && ( test "X$CXX" != "Xno" &&
    ( (test "X$CXX" = "Xg++" && `g++ -v >/dev/null 2>&1` ) ||
    (test "X$CXX" != "Xg++"))) ; then
  ISCXX="yes"
else
  ISCXX="no"
fi

AC_ARG_ENABLE(libgpsmm,
  AC_HELP_STRING([--disable-libgpsmm],
		 [don't build C++ bindings]),
  [ac_libgpsmm=$enableval], [ac_libgpsmm=$ISCXX])
AC_MSG_CHECKING([for C++ support])
if test x"$ac_libgpsmm" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([LIBGPSMM_ENABLE], 1, [C++ support])
else
  AC_MSG_RESULT([no])
fi

AM_CONDITIONAL([LIBGPSMM_ENABLE], [test x"$ac_libgpsmm" = x"yes"])



dnl Check for QtNetwork library and qmake. Both are necessary
dnl to build libQgpsmm. We use pkgconfig to check for the library
dnl and AC_CHECK_PROGS to search for qmake. As it is possible to
dnl install qmake from qt and qt4 in parallel, check for qmake-qt4
dnl first as this ensures we're using qmake for qt4.
AC_ARG_ENABLE(libQgpsmm,
		AC_HELP_STRING([--disable-libQgpsmm],
		[don't build QT bindings]),
		[ac_libQgpsmm=$enableval], [ac_libQgpsmm=$ISCXX])
AC_MSG_CHECKING([for QT support])
if test x"$ac_libQgpsmm" = "xyes"; then
  PKG_CHECK_MODULES(QtNetwork, [QtNetwork >= 4.4],  ac_qt="yes", ac_qt="no")
  if test x"$ac_qt" != "xyes"; then
    AC_MSG_WARN([QtNetwork library not found. Not including QT support])
    ac_libQgpsmm="no"
    AM_CONDITIONAL([LIB_Q_GPSMM_ENABLE], false)
    AC_MSG_RESULT([no])
  else
    AC_CHECK_PROGS(QMAKE, [qmake-qt4 qmake], [no])
    if test x"$QMAKE" != "xno"; then
      AM_CONDITIONAL([LIB_Q_GPSMM_ENABLE], true)
      AC_MSG_RESULT([yes])
    else
      AC_MSG_WARN([qmake not found. Not including QT support])
      ac_libQgpsmm="no"
      AM_CONDITIONAL([LIB_Q_GPSMM_ENABLE], false)
      AC_MSG_RESULT([no])
    fi
  fi
else
  AC_MSG_RESULT([no])
  AM_CONDITIONAL([LIB_Q_GPSMM_ENABLE], false)
fi



AC_ARG_ENABLE(ipv6,
  AC_HELP_STRING([--disable-ipv6],
		 [don't build IPv6 support]),
  [ac_ipv6=$enableval], [ac_ipv6=$ISCXX])
AC_MSG_CHECKING([for IPv6 support])
if test x"$ac_ipv6" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([IPV6_ENABLE], 1, [IPv6 support])
else
  AC_MSG_RESULT([no])
fi

AM_CONDITIONAL([IPV6_ENABLE], [test x"$ac_ipv6" = x"yes"])

dnl Test for XSLT processor (xsltproc or xmlto)
AC_CHECK_PROG(WITH_XSLTPROC,[xsltproc],[yes],[no])
XMLPROC=
MANTARGET=
XMLPROCFLAGS=
if test "x$WITH_XSLTPROC" = "xyes"; then
    AC_MSG_CHECKING([whether xsltproc knows about docbook xsl])
    DOCBOOK_MAN_URI='http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl'
    DOCBOOK_HTML_URI='http://docbook.sourceforge.net/release/xsl/current/html/docbook.xsl'
    cat >conftest.xml <<_EOF
       <refentry id="foo.1">
	  <refmeta>
	    <refentrytitle>foo</refentrytitle>
	    <manvolnum>1</manvolnum>
	    <refmiscinfo class='date'>9 Aug 2004</refmiscinfo>
	  </refmeta>
	  <refnamediv id='name'>
	    <refname>foo</refname>
	    <refpurpose>check man page generation from docbook source</refpurpose>
	  </refnamediv>
	</refentry>
_EOF
    if xsltproc --nonet --noout "$DOCBOOK_MAN_URI" conftest.xml >/dev/null 2>&1; then
	XMLPROC=xsltproc
	MANTARGET="$DOCBOOK_MAN_URI"
	HTMLTARGET="$DOCBOOK_HTML_URI"
	XMLPROCFLAGS="--nonet"
	AC_MSG_RESULT([yes])
    else
	AC_MSG_RESULT([no])
    fi
    rm -f conftest.xml foo.1
fi
if test x"$XMLPROC" = x; then
    AC_CHECK_PROG(WITH_XMLTO,[xmlto],[yes],[no])
    if test "x$WITH_XMLTO" = "xyes"; then
	XMLPROC=xmlto
	MANTARGET=man
	HTMLTARGET=xhtml-nochunks
	XMLPROCFLAGS=
    else
	AC_MSG_WARN([Neither xsltproc nor xmlto works: I will not build man pages.])
	AC_MSG_WARN([To build man pages, install xsltproc/xsltlib or xmlto and run autogen.sh again.])
    fi
fi
AM_CONDITIONAL([HAVE_XSLT_PROCESSOR],
	       [test x"$XMLPROC" != x])
AM_CONDITIONAL([XMLTOSTDOUT], [test x"$XMLPROC" = x"xsltproc"])
AC_SUBST(XMLPROC)
AC_SUBST(MANTARGET)
AC_SUBST(HTMLTARGET)
AC_SUBST(XMLPROCFLAGS)

dnl Gentoo systems can have a problem with the Python path
if test -e /etc/gentoo-release; then
    AC_MSG_WARN([This is a Gentoo system.])
    AC_MSG_WARN([Adjust your PYTHONPATH to see library directories under /usr/local/lib])
fi 

dnl Output the configuration summary
echo ""
echo "=========================================="
echo "$PACKAGE $VERSION"
echo "------------------------------------------"
dnl When you add a protocol here, don't forget the null-set check below
echo "	Protocols"
echo "	---------"
echo "Ashtech                 : $ac_ashtech"
echo "Earthmate               : $ac_earthmate"
echo "EverMore                : $ac_evermore"
echo "FV-18                   : $ac_fv18"
echo "Garmin                  : $ac_garmin"
echo "Garmin Simple Text      : $ac_garmintxt"
echo "iTrax                   : $ac_itrax"
echo "NMEA                    : $ac_nmea"
echo "NTRIP                   : $ac_ntrip"
echo "Navcom                  : $ac_navcom"
echo "OnCore                  : $ac_oncore"
echo "RTCM104V2               : $ac_rtcm104v2"
echo "RTCM104V3               : $ac_rtcm104v3"
echo "SiRF                    : $ac_sirf"
echo "SuperStarII             : $ac_superstar2"
echo "Trimble TSIP            : $ac_tsip"
echo "Tripmate                : $ac_tripmate"
echo "True North              : $ac_tnt"
echo "OceanServer             : $ac_oceanserver"
echo "UBX                     : $ac_ubx"
echo "GPSclock                : $ac_gpsclock"
echo "AIVDM support           : $ac_aivdm"
echo "Timing support          : $ac_timing"
echo "Client debugging support: $ac_clientdebug"
echo "MTK-3301                : $ac_mtk3301"
dnl Below this line are non-protocol switches
echo "	Daemon Features"
echo "	---------------"
echo "NTP SHM                 : $ac_ntpshm"
echo "NTP PPS                 : $ac_pps"
echo -n "PPS input on            : " ; case $ac_ppsoncts in yes) echo "CTS" ;; no) echo "DCD" ;; *) echo "Not defined" ;; esac
echo "Fixed port speed        : $ac_baud"
echo "Priv-drop user          : $ac_user"
echo "Enable shared libraries : $enable_shared"
echo "Enable DBUS support     : $ac_dbus"
echo "Enable BlueZ support    : $ac_bluetooth"
echo "Enable IPv6 support     : $ac_ipv6"
echo "Limited max clients     : $ac_maxclients"
echo "Limited max devices     : $ac_maxdevices"
echo "Allow device reconfig   : $ac_reconfigure"
echo "Allow control send      : $ac_controlsend"
echo "Squelch logging/hexdump : $ac_squelch"
echo "Raw Measurements        : $ac_raw"
echo "libusb device discovery : $ac_libusb"
echo "	Client Features"
echo "	---------------"
echo "Old protocol in libgps  : $ac_oldstyle"
echo "Build ncurses programs  : $ac_ncurses"
echo "Enable Python support   : $ac_python"
echo "Enable C++ support      : $ac_libgpsmm"
echo "Enable Qt support       : $ac_libQgpsmm"
echo "------------------------------------------"
echo ""

if test "xdummy" = "xdummy" -a \
    x"$ac_earthmate" = "xno" -a \
    x"$ac_evermore" = "xno" -a \
    x"$ac_fv18" = "xno" -a \
    x"$ac_garmin" = "xno" -a \
    x"$ac_garmintxt" = "xno" -a \
    x"$ac_itrax" = "xno" -a \
    x"$ac_navcom" = "xno" -a \
    x"$ac_nmea" = "xno" -a \
    x"$ac_ntrip" = "xno" -a \
    x"$ac_oncore" = "xno" -a \
    x"$ac_rtcm104v2" = "xno" -a \
    x"$ac_sirf" = "xno" -a \
    x"$ac_superstar2" = "xno" -a \
    x"$ac_tnt" = "xno" -a \
    x"$ac_oceanserver" = "xno" -a \
    x"$ac_tripmate" = "xno" -a \
    x"$ac_tsip" = "xno" -a \
    x"$ac_ubx" = "xno"; then
    AC_MSG_ERROR(Can't build gpsd with no protocols enabled)
fi
AC_OUTPUT(Makefile packaging/rpm/gpsd.spec libgps.pc libgpsd.pc
		   jsongen.py maskaudit.py
		   valgrind-audit,
		   [chmod +x jsongen.py maskaudit.py gpscat gpsfake gpsprof xgps valgrind-audit])
echo "Configure finished, type 'make' to build."

