#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined (HAVE_STRINGS_H)
#include <strings.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <arpa/inet.h>

#if defined (HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif

#include "gps.h"
#include "gpsd.h"

#if !defined (INADDR_NONE)
#define INADDR_NONE   ((in_addr_t)-1)
#endif

static char mbuf[128];

int passivesock(char *service, char *protocol, int qlen)
{
    struct servent *pse;
    struct protoent *ppe;
    struct sockaddr_in sin;
    int s, type;
    int one = 1;

    bzero((char *) &sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;

    if ( (pse = getservbyname(service, protocol)) )
	sin.sin_port = htons(ntohs((u_short) pse->s_port));
    else if ((sin.sin_port = htons((u_short) atoi(service))) == 0) {
	gpscli_report(0, "Can't get \"%s\" service entry.\n", service);
	return -1;
    }
    if ((ppe = getprotobyname(protocol)) == 0) {
	gpscli_report(0, "Can't get \"%s\" protocol entry.\n", protocol);
	return -1;
    }
    if (strcmp(protocol, "udp") == 0)
	type = SOCK_DGRAM;
    else
	type = SOCK_STREAM;

    s = socket(PF_INET, type, ppe->p_proto);
    if (s < 0)
    {
	gpscli_report(0, "Can't create socket:");
	return -1;
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == -1) {
	gpscli_report(0, mbuf, "%s", "Error: SETSOCKOPT SO_REUSEADDR");
	return -1;
    }
    if (bind(s, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
	gpscli_report(1, "Can't bind to port %s", service);
	return -1;
    }
    if (type == SOCK_STREAM && listen(s, qlen) < 0) {
	gpscli_report(0, "Can't listen on %s port:", service);
	return -1;
    }
    return s;
}

int netlib_passiveTCP(char *service, int qlen)
{
    return passivesock(service, "tcp", qlen);
}


int netlib_connectsock(char *host, char *service, char *protocol)
{
    struct hostent *phe;
    struct servent *pse;
    struct protoent *ppe;
    struct sockaddr_in sin;
    int s, type;
    int one = 1;

    bzero((char *) &sin, sizeof(sin));
    sin.sin_family = AF_INET;

    if ( (pse = getservbyname(service, protocol)) )
	sin.sin_port = htons(ntohs((u_short) pse->s_port));
    else if ((sin.sin_port = htons((u_short) atoi(service))) == 0) {
	gpscli_report(0, "Can't get \"%s\" service entry.\n", service);
	return -1;
    }
    if ( (phe = gethostbyname(host)) )
	bcopy(phe->h_addr, (char *) &sin.sin_addr, phe->h_length);
    else if ((sin.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE) {
	gpscli_report(0, "Can't get host entry: \"%s\".\n", host);
	return -1;
    }
    if ((ppe = getprotobyname(protocol)) == 0) {
	gpscli_report(0, "Can't get \"%s\" protocol entry.\n", protocol);
	return -1;
    }
    if (strcmp(protocol, "udp") == 0)
	type = SOCK_DGRAM;
    else
	type = SOCK_STREAM;

    s = socket(PF_INET, type, ppe->p_proto);
    if (s < 0)
    {
	gpscli_report(0, "Can't create socket:");
	return -1;
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)) == -1) {
	gpscli_report(0, "%s", "Error: SETSOCKOPT SO_REUSEADDR");
	return -1;
    }

    if (connect(s, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
	gpscli_report(0, "Can't connect to %s.%s: %s\n", host, service, strerror(errno));
	return -1;
    }
    return s;
}

int netlib_connectTCP(char *host, char *service)
{
    return netlib_connectsock(host, service, "tcp");
}
