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

#include "config.h"
#include "gps.h"
#include "gpsd.h"

#if !defined (INADDR_NONE)
#define INADDR_NONE   ((in_addr_t)-1)
#endif

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
    else if ((sin.sin_port = htons((u_short) atoi(service))) == 0)
	return NL_NOSERVICE;
    if ( (phe = gethostbyname(host)) )
	bcopy(phe->h_addr, (char *) &sin.sin_addr, phe->h_length);
    else if ((sin.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE)
	return -NL_NOHOST;
    if ((ppe = getprotobyname(protocol)) == 0)
	return NL_NOPROTO;
    if (strcmp(protocol, "udp") == 0)
	type = SOCK_DGRAM;
    else
	type = SOCK_STREAM;

    s = socket(PF_INET, type, ppe->p_proto);
    if (s < 0)
	return NL_NOSOCK;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one))==-1)
	return NL_NOSOCKOPT;
    if (connect(s, (struct sockaddr *) &sin, sizeof(sin)) < 0)
	return NL_NOCONNECT;
    return s;
}

