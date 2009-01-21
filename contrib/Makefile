# This is a skeleton makefile to simplify building some of these little
# utilities. They probably won't be useful to many users, and they probably
# won't get used on a daily basis, but someone might find them useful for
# tinkering with their gear.

PROGS=	ashctl binlog binreplay garreset lla2ecef motosend sirfctl

ADDOBJS=../strl.o

all: $(PROGS)

ashctl: ashctl.c
	$(CC) $(CFLAGS) $(ADDOBJS) -o ashctl ashctl.c

binlog: binlog.c
	$(CC) $(CFLAGS) $(ADDOBJS) -o binlog binlog.c

binreplay: binreplay.c
	$(CC) $(CFLAGS) $(ADDOBJS) -o binreplay binreplay.c -lutil

garreset: garreset.c
	$(CC) $(CFLAGS) $(ADDOBJS) -o garreset garreset.c

lla2ecef: lla2ecef.c
	$(CC) $(CFLAGS) $(ADDOBJS) -o lla2ecef lla2ecef.c -lm

motosend: motosend.c
	$(CC) $(CFLAGS) $(ADDOBJS) -o motosend motosend.c

sirfctl: sirfctl.c
	$(CC) $(CFLAGS) $(ADDOBJS) -o sirfctl sirfctl.c

clean:
	rm -f $(PROGS) *.o *.core
