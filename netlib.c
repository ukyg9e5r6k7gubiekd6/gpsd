/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#ifndef S_SPLINT_S
#include <netdb.h>
#ifndef AF_UNSPEC
#include <sys/socket.h>
#endif /* AF_UNSPEC */
#ifndef INADDR_ANY
#include <netinet/in.h>
#endif /* INADDR_ANY */
#include <sys/un.h>
#include <arpa/inet.h>     /* for htons() and friends */
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "sockaddr.h"

/*@-mustfreefresh -usedef@*/
socket_t netlib_connectsock(int af, const char *host, const char *service,
			    const char *protocol)
{
    struct protoent *ppe;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int ret, type, proto, one = 1;
    socket_t s = -1;
    bool bind_me;

    /*@-type@*/
    ppe = getprotobyname(protocol);
    if (strcmp(protocol, "udp") == 0) {
	type = SOCK_DGRAM;
	proto = (ppe) ? ppe->p_proto : IPPROTO_UDP;
    } else {
	type = SOCK_STREAM;
	proto = (ppe) ? ppe->p_proto : IPPROTO_TCP;
    }
    /*@+type@*/

    /* we probably ought to pass this in as an explicit flag argument */
    bind_me = (type == SOCK_DGRAM);

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = af;
    hints.ai_socktype = type;
    hints.ai_protocol = proto;
#ifndef S_SPLINT_S
    if (bind_me)
	hints.ai_flags = AI_PASSIVE;
    if ((ret = getaddrinfo(host, service, &hints, &result))) {
	return NL_NOHOST;
    }
#endif /* S_SPLINT_S */

    /*
     * From getaddrinfo(3):
     *     Normally, the application should try using the addresses in the
     *     order in which they are returned.  The sorting function used within
     *     getaddrinfo() is defined in RFC 3484).
     * From RFC 3484 (Section 10.3):
     *     The default policy table gives IPv6 addresses higher precedence than
     *     IPv4 addresses.
     * Thus, with the default parameters, we get IPv6 addresses first.
     */
    /*@-type@*/
    for (rp = result; rp != NULL; rp = rp->ai_next) {
	ret = NL_NOCONNECT;
	if ((s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0)
	    ret = NL_NOSOCK;
	else if (setsockopt
		 (s, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
		  sizeof(one)) == -1) {
	    (void)close(s);
	    ret = NL_NOSOCKOPT;
	} else {
	    if (bind_me) {
		if (bind(s, rp->ai_addr, rp->ai_addrlen) == 0) {
		    ret = 0;
		    break;
		}
	    } else {
		if (connect(s, rp->ai_addr, rp->ai_addrlen) == 0) {
		    ret = 0;
		    break;
		}
	    }
	}

	if (s > 0) {
	    (void)close(s);
	}
    }
    /*@+type@*/
#ifndef S_SPLINT_S
    freeaddrinfo(result);
#endif /* S_SPLINT_S */
    if (ret)
	return ret;

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
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
#endif

    /* set socket to noblocking */
    (void)fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);

    return s;
    /*@ +type +mustfreefresh @*/
}

/*@+mustfreefresh +usedef@*/

char /*@observer@*/ *netlib_errstr(const int err)
{
    switch (err) {
    case NL_NOSERVICE:
	return "can't get service entry";
    case NL_NOHOST:
	return "can't get host entry";
    case NL_NOPROTO:
	return "can't get protocol entry";
    case NL_NOSOCK:
	return "can't create socket";
    case NL_NOSOCKOPT:
	return "error SETSOCKOPT SO_REUSEADDR";
    case NL_NOCONNECT:
	return "can't connect to host/port pair";
    default:
	return "unknown error";
    }
}

char *netlib_sock2ip(int fd)
{
    sockaddr_t fsin;
    socklen_t alen = (socklen_t) sizeof(fsin);
    /*@i1@*/ static char ip[INET6_ADDRSTRLEN];
    int r;

    r = getpeername(fd, &(fsin.sa), &alen);
    /*@ -branchstate -unrecog +boolint @*/
    if (r == 0) {
	switch (fsin.sa.sa_family) {
	case AF_INET:
	    r = !inet_ntop(fsin.sa_in.sin_family, &(fsin.sa_in.sin_addr),
			   ip, sizeof(ip));
	    break;
#ifdef IPV6_ENABLE
	case AF_INET6:
	    r = !inet_ntop((int)fsin.sa_in6.sin6_family, &(fsin.sa_in6.sin6_addr),
			   ip, sizeof(ip));
	    break;
#endif
	default:
	    gpsd_report(LOG_ERROR, "Unhandled address family %d in %s\n",
			fsin.sa.sa_family, __FUNCTION__);
	    (void)strlcpy(ip, "<unknown AF>", sizeof(ip));
	    return ip;
	}
    }
    if (r != 0) {
	gpsd_report(LOG_INF, "getpeername() = %d, error = %s (%d)\n", r,
		    strerror(errno), errno);
	(void)strlcpy(ip, "<unknown>", sizeof(ip));
    }
    /*@ +branchstate +unrecog -boolint @*/
    return ip;
}
