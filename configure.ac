AC_INIT
AM_INIT_AUTOMAKE(gpsd, 2.16)
AC_PREFIX_PROGRAM(gcc)
AM_CONFIG_HEADER(config.h)
AC_LANG([C])

AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_C_BIGENDIAN
AC_FUNC_ALLOCA

if eval "test x$GCC = xyes"; then
	CFLAGS="$CFLAGS -Wall -Wcast-align -Wmissing-declarations -Wmissing-prototypes -Wstrict-prototypes -Wpointer-arith -Wreturn-type"
	dnl -Wcast-qual -Wextra
fi

AC_CHECK_HEADERS(termios.h sys/termios.h sys/select.h strings.h sys/filio.h sys/time.h X11/xpm.h xpm.h getopt.h sys/modem.h sys/aio.h)

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

dnl check for Garmin support
AC_ARG_ENABLE(garmin,
  AC_HELP_STRING([--disable-garmin],
                 [disable DeLorme Garmin Garmin support]),
  [ac_garmin=$enableval], [ac_garmin=yes])
AC_MSG_CHECKING([for Garmin support])
if test x"$ac_garmin" = "xyes"; then
    AC_MSG_RESULT([yes])
	AC_DEFINE([GARMIN_ENABLE], 1, [Garmin support])
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

AC_OUTPUT(Makefile gpsd.spec)

dnl Output the configuration summary
echo ""
echo "=========================================="
echo "$PACKAGE $VERSION"
echo "------------------------------------------"
echo "SiRF-II                 : $ac_sirfII"
echo "Tripmate                : $ac_tripmate"
echo "Earthmate               : $ac_earthmate"
echo "Garmin                  : $ac_garmin"
echo "Logfile                 : $ac_logfile"
echo "Enable shared libraries : $enable_shared"
echo "------------------------------------------"
echo "Configure finished, type 'make' to build."

