# Generated automatically from Makefile.in by configure.
# Makefile.in
# $Modified: Thu Jan 22 11:55:32 1998 by brook $


## The C compiler

# What we use to compile C files.
# CC = cc
CC = gcc

# The flags we use to compile C files.
CDEBUGFLAGS = -g
CFLAGS = -g -O2 $(CDEBUGFLAGS)


## The C preprocessor.

# What we use for preprocessing.  (gcc -E -traditional-cpp)
CPP = gcc -E -traditional-cpp

# Flags passed to the C preprocessor.  ()
CPPFLAGS = 

# Definitions to be passed to the C preprocessor.  (-DHAVE_CONFIG_H)
DEFS =  -DHAVE_TERMIO_H=1 -DHAVE_SYS_PARAM_H=1 -DHAVE_ATHENA=1 -DHAVE_X11_XAW_FORM_H=1 -DHAVE_X11_XAW_PANNER_H=1 -DHAVE_X11_XAW_PORTHOLE_H=1 -DHAVE_X11_XMU_EDITRES_H=1 -DHAVE_XPM=1 -DHAVE_X11_XPM_H=1 -DHAVE_XP=1 -DHAVE_MOTIF=1 


## The Linker.

# Flags passed to the linker.  (-g -O)
LDFLAGS = 

# Use this for building statically linked executables with GCC.
# LDFLAGS = -static 


## Local libraries

# Math library (-lm)
LIBM       = -lm
# C library (-lc)
LIBC       = -lc

# All libraries shown above
LIBS = $(LIBM) $(LIBC)


## X Libraries

# Special flags for linking with X.  (-L/usr/X11R5/lib)
X_LDFLAGS =  -L/usr/X11R6/lib

# Motif library.  (-lXm)
LIBXM = -lXm

# Use this alternative for building `semistatic' executables
# where Motif libraries are statically linked in.
# LIBXM = -Wl,-Bstatic -lXm -Wl,-Bdynamic

# Athena libraries.  (-lXaw -lXmu)
LIBXAW = -lXaw -lXmu 

# X extension library; needed for Athena and Motif >= 2.0.  (-lXext)
LIBXEXT = -lXext

# Xpm library; needed for DDD and sometimes for Motif >= 2.0.  (-lXpm)
LIBXPM = -lXpm

# Xp library; sometimes needed for Motif >= 2.1.  (-lXp)
LIBXP = -lXp

# gen library; sometimes needed for Motif >= 2.0.  (-lgen)
LIBGEN = 

# X toolkit library.  (-lXt)
LIBXT = -lXt

# X library.  (-lSM -lICE -lX11 -lnsl -lsocket)
LIBX11 =  -lSM -lICE -lX11 

# All libraries shown above
ALL_X_LIBS = $(X_LDFLAGS) $(LIBXM) $(LIBXP) $(LIBXPM) $(LIBXAW) $(LIBXEXT) \
$(LIBXT) $(LIBX11) $(LIBGEN)


## C compile commands.
COMPILE.c = $(CC) $(CPPFLAGS) $(DEFS) $(CFLAGS) -c
LINK.c = $(CC) $(LDFLAGS)
COMPILE_AND_LINK.c = \
    $(CC) $(CPPFLAGS) $(DEFS) $(CFLAGS) $(LDFLAGS)

## Where to look for X include files.  (-I/usr/X11R5/include)
X_INCLUDE =  -I/usr/X11R6/include

## Where to look for include files.
INCLUDE = -I. $(X_INCLUDE)

## Implicit rules.
.SUFFIXES: .c
.c.o:
	$(COMPILE.c) $(INCLUDE) -o $@ $<


## Libraries and object files
LIB_OBJECTS = netlib.o nmea_parse.o serial.o tm.o $(MOTIF_OBJECTS)

## Programs
PROGRAMS=gpsd

## Motif dependent programs and object files
MOTIF_PROGRAMS=gps
MOTIF_OBJECTS = display.o


all: $(PROGRAMS) $(MOTIF_PROGRAMS)

gpsd: gpsd.o libgpsd.a
	$(LINK.c) -o $@ gpsd.o -L. -lgpsd $(LIBS)

gps: gps.o libgpsd.a
	$(LINK.c) -o $@ gps.o -L. -lgpsd $(ALL_X_LIBS) $(LIBS)

libgpsd.a:  $(LIB_OBJECTS)
	ar -r libgpsd.a $(LIB_OBJECTS)
	ranlib libgpsd.a

clean:
	rm -f *.o *.a gpsd gps *~
