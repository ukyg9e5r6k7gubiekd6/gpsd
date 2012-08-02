/* klugey def'n of a socket address struct helps hide IPV4 vs. IPV6 ugliness */

typedef union sockaddr_u {
    struct sockaddr sa;
    struct sockaddr_in sa_in;
#ifdef IPV6_ENABLE
    struct sockaddr_in6 sa_in6;
#endif /* IPV6_ENABLE */
} sockaddr_t;

/* sockaddr.h ends here */
