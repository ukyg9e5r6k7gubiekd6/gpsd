/* $Id$ */
#include <sys/types.h>
#include "gpsd_config.h"
#ifndef S_SPLINT_S
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/ip.h>
#endif
#endif /* S_SPLINT_S */
#ifndef S_SPLINT_S
#include <netdb.h>
#include <arpa/inet.h>
#endif /* S_SPLINT_S */
#include <errno.h>
#include <stdlib.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <string.h>

#include "gpsd.h"

#if !defined (INADDR_NONE)
#define INADDR_NONE   ((in_addr_t)-1)
#endif

int netlib_connectsock(const char *host, const char *service, const char *protocol)
{
    struct hostent *phe;
    struct servent *pse;
    struct protoent *ppe;
    struct sockaddr_in sin;
    int s, type, proto, one = 1;

    memset((char *) &sin, 0, sizeof(sin));
    /*@ -type -mustfreefresh @*/
    sin.sin_family = AF_INET;
    if ((pse = getservbyname(service, protocol)))
	sin.sin_port = htons(ntohs((unsigned short) pse->s_port));
    else if ((sin.sin_port = htons((unsigned short) atoi(service))) == 0)
	return NL_NOSERVICE;

    if ((phe = gethostbyname(host)))
	memcpy((char *) &sin.sin_addr, phe->h_addr, phe->h_length);
#ifndef S_SPLINT_S
    else if ((sin.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE)
	return NL_NOHOST;
#endif /* S_SPLINT_S */

    ppe = getprotobyname(protocol);
    if (strcmp(protocol, "udp") == 0) {
	type = SOCK_DGRAM;
	proto = (ppe) ? ppe->p_proto : IPPROTO_UDP;
    } else {
	type = SOCK_STREAM;
	proto = (ppe) ? ppe->p_proto : IPPROTO_TCP;
    }

    if ((s = socket(PF_INET, type, proto)) == -1)
	return NL_NOSOCK;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one))==-1) {
	(void)close(s);
	return NL_NOSOCKOPT;
    }
    if (connect(s, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
	(void)close(s);
	return NL_NOCONNECT;
    }

#ifdef IPTOS_LOWDELAY
    {
    int opt = IPTOS_LOWDELAY;
    /*@ -unrecog @*/
    (void)setsockopt(s, IPPROTO_IP, IP_TOS, &opt, sizeof opt);
    /*@ +unrecog @*/
    }
#endif
#ifdef TCP_NODELAY
    if (type == SOCK_STREAM)
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#endif
    return s;
    /*@ +type +mustfreefresh @*/
}

char /*@observer@*/ *netlib_errstr(const int err)
{
    switch (err) {
    case NL_NOSERVICE:  return "can't get service entry";
    case NL_NOHOST:     return "can't get host entry";
    case NL_NOPROTO:    return "can't get protocol entry";
    case NL_NOSOCK:     return "can't create socket";
    case NL_NOSOCKOPT:  return "error SETSOCKOPT SO_REUSEADDR";
    case NL_NOCONNECT:  return "can't connect to host/port pair";
    default:		return "unknown error";
    }
}

char *sock2ip(int fd)
{
    struct sockaddr fsin;
    socklen_t alen = (socklen_t)sizeof(fsin);
    char *ip;
    int r;

    r = getpeername(fd, (struct sockaddr *) &fsin, &alen);
    /*@ -branchstate @*/
    if (r == 0){
	ip = inet_ntoa(((struct sockaddr_in *)(&fsin))->sin_addr);
    } else {
	gpsd_report(LOG_INF, "getpeername() = %d, error = %s (%d)\n", r, strerror(errno), errno);
	ip = "<unknown>";
    }
    /*@ +branchstate @*/
    return ip;
}
