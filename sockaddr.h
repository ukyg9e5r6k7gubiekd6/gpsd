/* klugey def'n of a socket address struct helps hide IPV4 vs. IPV6 ugliness */

#include <netdb.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifndef S_SPLINT_S
#include <sys/socket.h>
#endif /* S_SPLINT_S */

typedef union sockaddr_u {
    struct sockaddr sa;
    struct sockaddr_in sa_in;
    struct sockaddr_in6 sa_in6;
} sockaddr_t;

/* sockaddr.h ends here */
