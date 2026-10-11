#ifndef NETWORK_HELPERS_H
#define NETWORK_HELPERS_H

#include "../../cupidfetch.h"

struct cf_ip_candidate {
    char iface[256];
    char ip[64];
    int family;
    bool up;
    bool running;
    bool default_route;
    unsigned long long metric;
};
bool cf_ip_candidate_better(const struct cf_ip_candidate *candidate, const struct cf_ip_candidate *best);
bool cf_ip_address_text(const struct sockaddr *address, char *out, size_t size);
bool cf_parse_default_route(const char *line, int family, char *iface, size_t size, unsigned long *metric);
#ifdef _WIN32
#include <iphlpapi.h>
#include <netioapi.h>
bool cf_select_windows_ip(const IP_ADAPTER_ADDRESSES *adapters, const MIB_IPFORWARD_TABLE2 *routes,
                          struct cf_ip_candidate *best);
#else
void cf_ip_candidate_routes(struct cf_ip_candidate *candidate, FILE *routes);
bool cf_select_posix_ip(const struct ifaddrs *addresses, FILE *v4_routes, FILE *v6_routes,
                        struct cf_ip_candidate *best);
#endif

#endif
