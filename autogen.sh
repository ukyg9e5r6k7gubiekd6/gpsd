#!/bin/sh

# Automakeversion
AM_1=1
AM_2=7
AM_3=6

# Autoconfversion
AC_1=2
AC_2=57

# Libtoolversion
LT_1=1
LT_2=5

# Check automake version
AM_VERSION=`automake --version | sed -n -e 's#[^0-9]* \([0-9]*\)\.\([0-9]*\)\.*\([0-9]*\).*$#\1 \2 \3#p'`
AM_V1=`echo $AM_VERSION | awk '{print $1}'`
AM_V2=`echo $AM_VERSION | awk '{print $2}'`
AM_V3=`echo $AM_VERSION | awk '{print $3}'`

if [ "$AM_1" -gt "$AM_V1" ]; then
	AM_ERROR=1 
else
	if [ "$AM_1" -eq "$AM_V1" ]; then
		if [ "$AM_2" -gt "$AM_V2" ]; then
			AM_ERROR=1 
		else
			if [ "$AM_2" -eq "$AM_V2" ]; then
				if [ -n "$AM_V3" -o "$AM_3" -gt "$AM_V3" ]; then
					AM_ERROR=1 
				fi
			fi
		fi
	fi
fi

if [ "$AM_ERROR" = "1" ]; then
	echo -n "Your automake version `automake --version | sed -n -e 's#[^0-9]* \([0-9]*\.[0-9]*\.[0-9]*\).*#\1#p'`"
	echo " is older than the suggested one, $AM_1.$AM_2.$AM_3"
	echo "Go on at your own risk. :-)"
	echo
fi

# Check autoconf version
AC_VERSION=`autoconf --version | sed -n -e 's#[^0-9]* \([0-9]*\)\.\([0-9]*\).*$#\1 \2#p'`
AC_V1=`echo $AC_VERSION | awk '{print $1}'`
AC_V2=`echo $AC_VERSION | awk '{print $2}'`

if [ "$AC_1" -gt "$AC_V1" ]; then
	AC_ERROR=1 
else
	if [ "$AC_1" -eq "$AC_V1" ]; then
		if [ "$AC_2" -gt "$AC_V2" ]; then
			AC_ERROR=1 
		fi
	fi
fi

if [ "$AC_ERROR" = "1" ]; then
	echo -n "Your autoconf version `autoconf --version | sed -n -e 's#[^0-9]* \([0-9]*\.[0-9]*\).*#\1#p'`"
	echo " is older than the suggested one, $AC_1.$AC_2"
	echo "Go on at your own risk. :-)"
	echo
fi

#Check for pkg-config
if which pkg-config 1>/dev/null 2>&1; then
	#pkg-config seems to be installed. Check for m4 macros:
	tmpdir=`mktemp -d "./autogenXXXXXX"`
	if [ -z ${tmpdir} ]; then
		echo -n "Creating a temporary directory failed. "
		echo 'Is mktemp in $PATH?'
		echo
		exit 1
	fi

	oldpwd=`pwd`
	cd "${tmpdir}"
	cat > configure.ac << _EOF_
AC_INIT
PKG_CHECK_MODULES(QtNetwork, [QtNetwork >= 4.4],  ac_qt="yes", ac_qt="no")
_EOF_
	aclocal
	autoconf --force
	grep -q PKG_CHECK_MODULES configure
	PKG_MACRO_AVAILABLE=$?
	cd "${oldpwd}"
	rm -rf "${tmpdir}"
	if [ ${PKG_MACRO_AVAILABLE} -eq 0 ]; then
		echo -n "pkg-config installed, but autoconf is not able to find pkg.m4. "
		echo "Unfortunately the generated configure would not work, so we stop here."
		echo
		exit 1
	fi
else
	echo -n "pkg-config not found. "
	echo "pkg-config is required to create a working configure, so we stop here."
	echo
	exit 1
fi


# Check libtool version
if [ -z "$LIBTOOL" ] ; then
	LIBTOOL="libtool"
fi
LT_VERSION=`${LIBTOOL} --version | sed -n -e 's#[^0-9]* \([0-9]*\)\.\([0-9]*\).*$#\1 \2#p'`
LT_V1=`echo $LT_VERSION | awk '{print $1}'`
LT_V2=`echo $LT_VERSION | awk '{print $2}'`

if [ "$LT_1" -gt "$LT_V1" ]; then
	LT_ERROR=1 
else
	if [ "$LT_1" -eq "$LT_V1" ]; then
		if [ "$LT_2" -gt "$LT_V2" ]; then
			LT_ERROR=1 
		fi
	fi
fi

if [ "$LT_ERROR" = "1" ]; then
	echo -n "Your libtool version `libtool --version | sed -n -e 's#[^0-9]* \([0-9]*\.[0-9]*\).*#\1#p'`"
	echo " is older than the suggested one, $LT_1.$LT_2"
	echo "Go on at your own risk. :-)"
	echo
fi

if [ -z "$LIBTOOLIZE" ] ; then
	LIBTOOLIZE="libtoolize"
fi
echo Configuring build environment for gpsd
aclocal \
  && ${LIBTOOLIZE} --force --copy \
  && autoheader --force \
  && automake --add-missing --foreign --copy  --include-deps \
  && autoconf --force \
  && echo Now running configure to configure gpsd \
  && echo "./configure $@" \
  && ./configure "$@"
