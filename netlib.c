/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include "gpsd_config.h"

#include <string.h>
#include <fcntl.h>
#ifndef S_SPLINT_S
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif /* HAVE_NETDB_H */
#ifndef AF_UNSPEC
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif /* HAVE_SYS_SOCKET_H */
#endif /* AF_UNSPEC */
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif /* HAVE_SYS_UN_H */
#ifndef INADDR_ANY
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif /* HAVE_NETINET_IN_H */
#endif /* INADDR_ANY */
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>     /* for htons() and friends */
#endif /* HAVE_ARPA_INET_H */
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif /* HAVE_WINSOCK2_H */
#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif /* HAVE_WS2TCPIP_H */
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif /* HAVE_WINDOWS_H */
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "sockaddr.h"

static int netlib_closesock(socket_t sock)
{
#ifdef _WIN32
	return closesocket(sock);
#else /* ndef _WIN32 */
	return close(sock);
#endif /* ndef _WIN32 */
}

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
	s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
	if (BADSOCK(s))
	    ret = NL_NOSOCK;
	else if (setsockopt
		 (s, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
		  sizeof(one)) == -1) {
	    if (!BADSOCK(s))
			(void)netlib_closesock(s);
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

	if (!BADSOCK(s)) {
		(void)netlib_closesock(s);
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
	nonblock_enable(s);

    return s;
    /*@ +type +mustfreefresh @*/
}

/*@+mustfreefresh +usedef@*/

const char /*@observer@*/ *netlib_errstr(const int err)
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

#ifdef HAVE_SYS_UN_H
socket_t netlib_localsocket(const char *sockfile, int socktype)
{
    int sock;

	sock = socket(AF_UNIX, socktype, 0);
    if (BADSOCK(sock)) {
	return -1;
    } else {
	struct sockaddr_un saddr;

	memset(&saddr, 0, sizeof(struct sockaddr_un));
	saddr.sun_family = AF_UNIX;
	(void)strlcpy(saddr.sun_path, 
		      sockfile, 
		      sizeof(saddr.sun_path));

	/*@-unrecog@*/
	if (connect(sock, (struct sockaddr *)&saddr, SUN_LEN(&saddr)) < 0) {
		(void)netlib_closesock(sock);
	    return -1;
	}
	/*@+unrecog@*/

	return sock;
    }
}
#endif /* HAVE_SYS_UN_H */

char *netlib_sock2ip(int fd)
/* retrieve the IP address corresponding to a socket */ 
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
#ifdef HAVE_INET_NTOP
	    r = !inet_ntop(fsin.sa_in.sin_family, &(fsin.sa_in.sin_addr),
			   ip, sizeof(ip));
#elif defined(HAVE_INET_NTOA)
		{
            char *a;
			/* FIXME: Thread safety */
            a = inet_ntoa(fsin.sa_in.sin_addr);
            strcpy(ip, a);
			r = 0;
        }
#else /* !defined(HAVE_INET_NTOP) && !defined(HAVE_INET_NTOA) */
#error "Cannot figure out how to convert an IPv4 socket address into a string"
#endif /* ndef HAVE_INET_NTOP */
	    break;
#ifdef IPV6_ENABLE
	case AF_INET6:
	    r = !inet_ntop((int)fsin.sa_in6.sin6_family, &(fsin.sa_in6.sin6_addr),
			   ip, sizeof(ip));
	    break;
#endif
	default:
	    (void)strlcpy(ip, "<unknown AF>", sizeof(ip));
	    return ip;
	}
    }
    if (r != 0) {
	(void)strlcpy(ip, "<unknown>", sizeof(ip));
    }
    /*@ +branchstate +unrecog -boolint @*/
    return ip;
}
