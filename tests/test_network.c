#include "../src/modules/common/module_helpers.h"
#include "../src/modules/common/network_helpers.h"

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); return 1; } } while (0)

static struct cf_ip_candidate candidate(const char *iface, const char *ip, int family, bool up, bool running, bool route, unsigned long long metric) {
    struct cf_ip_candidate result = {0};
    snprintf(result.iface, sizeof(result.iface), "%s", iface);
    snprintf(result.ip, sizeof(result.ip), "%s", ip);
    result.family = family;
    result.up = up;
    result.running = running;
    result.default_route = route;
    result.metric = metric;
    return result;
}

static int test_ranking(void) {
    struct cf_ip_candidate v6 = candidate("wifi", "2001:db8::1", AF_INET6, true, true, false, 0);
    struct cf_ip_candidate v4 = candidate("ethernet", "192.0.2.1", AF_INET, false, false, true, 0);
    CHECK(cf_ip_candidate_better(&v6, &v4));
    CHECK(!cf_ip_candidate_better(&v4, &v6));
    v4.up = true;
    CHECK(cf_ip_candidate_better(&v6, &v4));
    v4.running = true;
    v4.default_route = false;
    CHECK(cf_ip_candidate_better(&v4, &v6));
    v6.default_route = true;
    CHECK(cf_ip_candidate_better(&v6, &v4));
    v4.default_route = true;
    CHECK(cf_ip_candidate_better(&v4, &v6));
    struct cf_ip_candidate high = candidate("a", "192.0.2.2", AF_INET, true, true, true, 100);
    struct cf_ip_candidate low = candidate("z", "192.0.2.3", AF_INET, true, true, true, 10);
    CHECK(cf_ip_candidate_better(&low, &high));
    struct cf_ip_candidate input[] = {v6, high, v4, low};
    for (int start = 0; start < 4; ++start) {
        struct cf_ip_candidate best = {0};
        for (int j = 0; j < 4; ++j) {
            const struct cf_ip_candidate *next = &input[(start + j) % 4];
            if (cf_ip_candidate_better(next, &best)) best = *next;
        }
        CHECK(strcmp(best.iface, "ethernet") == 0);
    }
    high.metric = low.metric;
    CHECK(cf_ip_candidate_better(&high, &low));
    strcpy(low.iface, high.iface);
    CHECK(cf_ip_candidate_better(&high, &low));
    CHECK(!cf_ip_candidate_better(&high, &high));
    CHECK(!cf_ip_candidate_better(NULL, &high));
    return 0;
}

static int test_addresses(void) {
    const char *invalid4[] = {"0.0.0.0", "0.1.2.3", "127.0.0.1", "169.254.2.3", "224.0.0.1", "255.255.255.255"};
    const char *invalid6[] = {"::", "::1", "fe80::1", "ff02::1", "::ffff:192.0.2.1"};
    struct sockaddr_in v4 = {0};
    struct sockaddr_in6 v6 = {0};
    char text[64];
    v4.sin_family = AF_INET;
    v6.sin6_family = AF_INET6;
    for (size_t i = 0; i < sizeof(invalid4) / sizeof(*invalid4); ++i) {
        CHECK(inet_pton(AF_INET, invalid4[i], &v4.sin_addr) == 1);
        CHECK(!cf_ip_address_text((struct sockaddr *)&v4, text, sizeof(text)));
    }
    for (size_t i = 0; i < sizeof(invalid6) / sizeof(*invalid6); ++i) {
        CHECK(inet_pton(AF_INET6, invalid6[i], &v6.sin6_addr) == 1);
        CHECK(!cf_ip_address_text((struct sockaddr *)&v6, text, sizeof(text)));
    }
    CHECK(inet_pton(AF_INET, "192.168.1.9", &v4.sin_addr) == 1);
    CHECK(cf_ip_address_text((struct sockaddr *)&v4, text, sizeof(text)));
    CHECK(strcmp(text, "192.168.1.9") == 0);
    CHECK(inet_pton(AF_INET6, "fd00::9", &v6.sin6_addr) == 1);
    CHECK(cf_ip_address_text((struct sockaddr *)&v6, text, sizeof(text)));
    CHECK(strcmp(text, "fd00::9") == 0);
    CHECK(!cf_ip_address_text((struct sockaddr *)&v6, text, 1));
    CHECK(!cf_ip_address_text(NULL, text, sizeof(text)));
    CHECK(!cf_ip_address_text((struct sockaddr *)&v6, NULL, sizeof(text)));
    CHECK(!cf_detect_primary_ip(NULL, 1, text, sizeof(text), &(bool){false}));
    return 0;
}

static int test_routes(void) {
    char iface[256];
    unsigned long metric = 99;
    CHECK(cf_parse_default_route("wlan0\t00000000\t0101A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n", AF_INET, iface, sizeof(iface), &metric));
    CHECK(strcmp(iface, "wlan0") == 0 && metric == 600);
    CHECK(cf_parse_default_route("eth0 00000000 00000000 0001 0 0 0 00000000 0 0 0", AF_INET, iface, sizeof(iface), &metric));
    const char *invalid[] = {
        "Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT",
        "eth0 00000000 00000000 0000 0 0 0 00000000 0 0 0",
        "eth0 00000000 00000000 0201 0 0 0 00000000 0 0 0",
        "eth0 00000000 00000000 0001 0 0 -1 00000000 0 0 0",
        "eth0 00000000 00000000 0001 0 0 184467440737095516160 00000000 0 0 0",
        "eth0 00000000 00000000 0001 0 0 0 00FFFFFF 0 0 0",
        "eth0 00000000 00000000 0001 0 0 0 00000000 0 0",
        "eth0 00000000 00000000 0001 0 0 0 00000000 0 0 0 extra",
        "eth0 00000000 invalid! 0001 0 0 0 00000000 0 0 0"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i)
        CHECK(!cf_parse_default_route(invalid[i], AF_INET, iface, sizeof(iface), &metric));
    const char *route6 = "00000000000000000000000000000000 00 00000000000000000000000000000000 00 fe800000000000000000000000000001 00000064 00000000 00000000 00000003 wlan0\n";
    CHECK(cf_parse_default_route(route6, AF_INET6, iface, sizeof(iface), &metric));
    CHECK(strcmp(iface, "wlan0") == 0 && metric == 100);
    CHECK(!cf_parse_default_route(route6, AF_INET6, iface, 2, &metric));
    char broken[512];
    strcpy(broken, route6); broken[33] = '4';
    CHECK(!cf_parse_default_route(broken, AF_INET6, iface, sizeof(iface), &metric));
    strcpy(broken, route6); broken[69] = '4';
    CHECK(!cf_parse_default_route(broken, AF_INET6, iface, sizeof(iface), &metric));
    strcpy(broken, route6); broken[0] = 'g';
    CHECK(!cf_parse_default_route(broken, AF_INET6, iface, sizeof(iface), &metric));
    CHECK(!cf_parse_default_route(NULL, AF_INET, iface, sizeof(iface), &metric));
#ifndef _WIN32
    FILE *routes = tmpfile();
    CHECK(routes != NULL);
    fputs("other0 00000000 00000000 0001 0 0 0 00000000 0 0 0\n"
          "wlan0 00000000 00000000 0001 0 0 600 00000000 0 0 0\n"
          "wlan0 00000000 00000000 0001 0 0 100 00000000 0 0 0\n", routes);
    struct cf_ip_candidate item = candidate("wlan0", "192.0.2.1", AF_INET, true, true, false, 0);
    cf_ip_candidate_routes(&item, routes);
    CHECK(item.default_route && item.metric == 100);
    item = candidate("missing0", "192.0.2.1", AF_INET, true, true, false, 0);
    cf_ip_candidate_routes(&item, routes);
    CHECK(!item.default_route);
    fclose(routes);
    cf_ip_candidate_routes(&item, NULL);
    CHECK(!item.default_route);
#endif
    return 0;
}

#ifndef _WIN32
static int test_posix_adapters(void) {
    struct ifaddrs addresses[3] = {{0}};
    struct sockaddr_in v4 = {0};
    struct sockaddr_in6 v6 = {0};
    v4.sin_family = AF_INET;
    v6.sin6_family = AF_INET6;
    CHECK(inet_pton(AF_INET, "192.0.2.1", &v4.sin_addr) == 1);
    CHECK(inet_pton(AF_INET6, "2001:db8::1", &v6.sin6_addr) == 1);
    addresses[0].ifa_name = "eth0";
    addresses[0].ifa_addr = (struct sockaddr *)&v4;
    addresses[0].ifa_next = &addresses[1];
    addresses[1].ifa_name = "wifi0";
    addresses[1].ifa_addr = (struct sockaddr *)&v6;
    addresses[1].ifa_flags = IFF_UP | IFF_RUNNING;
    addresses[1].ifa_next = &addresses[2];
    addresses[2].ifa_name = "no-address";
    struct cf_ip_candidate best;
    CHECK(cf_select_posix_ip(addresses, NULL, NULL, &best));
    CHECK(best.family == AF_INET6 && best.up && strcmp(best.iface, "wifi0") == 0);
    addresses[0].ifa_flags = IFF_UP;
    CHECK(cf_select_posix_ip(addresses, NULL, NULL, &best) && best.family == AF_INET6);
    addresses[0].ifa_flags |= IFF_RUNNING;
    CHECK(cf_select_posix_ip(addresses, NULL, NULL, &best) && best.family == AF_INET);
    FILE *routes = tmpfile();
    CHECK(routes != NULL);
    fputs("00000000000000000000000000000000 00 00000000000000000000000000000000 00 fe800000000000000000000000000001 00000064 00000000 00000000 00000003 wifi0\n", routes);
    CHECK(cf_select_posix_ip(addresses, NULL, routes, &best) && best.family == AF_INET6);
    CHECK(best.default_route && best.metric == 100);
    addresses[0].ifa_next = &addresses[2];
    addresses[1].ifa_next = &addresses[0];
    CHECK(cf_select_posix_ip(&addresses[1], NULL, routes, &best) && best.family == AF_INET6);
    fclose(routes);
    addresses[1].ifa_flags |= IFF_LOOPBACK;
    CHECK(cf_select_posix_ip(&addresses[1], NULL, NULL, &best) && best.family == AF_INET);
    addresses[0].ifa_flags |= IFF_LOOPBACK;
    CHECK(!cf_select_posix_ip(&addresses[1], NULL, NULL, &best));
    CHECK(!cf_select_posix_ip(NULL, NULL, NULL, &best));
    return 0;
}
#endif

#ifdef _WIN32
static int test_windows_adapters(void) {
    IP_ADAPTER_ADDRESSES adapters[2] = {{0}};
    IP_ADAPTER_UNICAST_ADDRESS addresses[2] = {{0}};
    struct sockaddr_in v4 = {0};
    struct sockaddr_in6 v6 = {0};
    v4.sin_family = AF_INET;
    v6.sin6_family = AF_INET6;
    CHECK(inet_pton(AF_INET, "192.0.2.1", &v4.sin_addr) == 1);
    CHECK(inet_pton(AF_INET6, "2001:db8::1", &v6.sin6_addr) == 1);
    addresses[0].Address.lpSockaddr = (struct sockaddr *)&v4;
    addresses[0].Address.iSockaddrLength = sizeof(v4);
    addresses[1].Address.lpSockaddr = (struct sockaddr *)&v6;
    addresses[1].Address.iSockaddrLength = sizeof(v6);
    for (int i = 0; i < 2; ++i) {
        adapters[i].FirstUnicastAddress = &addresses[i];
        adapters[i].IfType = IF_TYPE_ETHERNET_CSMACD;
        adapters[i].Luid.Value = i + 1;
        addresses[i].DadState = IpDadStatePreferred;
        addresses[i].ValidLifetime = addresses[i].PreferredLifetime = 100;
    }
    adapters[0].Next = &adapters[1];
    adapters[0].FriendlyName = L"Ethernet";
    adapters[0].OperStatus = IfOperStatusDown;
    adapters[1].FriendlyName = L"\u65e5\u672c\u8a9e Wi-Fi";
    adapters[1].OperStatus = IfOperStatusUp;
    struct cf_ip_candidate best;
    CHECK(cf_select_windows_ip(adapters, NULL, &best));
    CHECK(best.family == AF_INET6 && best.up);
    CHECK(strcmp(best.iface, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e Wi-Fi") == 0);
    adapters[0].Next = NULL;
    adapters[1].Next = &adapters[0];
    CHECK(cf_select_windows_ip(&adapters[1], NULL, &best) && best.family == AF_INET6);
    adapters[1].Next = NULL;
    adapters[0].Next = &adapters[1];
    adapters[0].OperStatus = IfOperStatusUp;
    CHECK(cf_select_windows_ip(adapters, NULL, &best) && best.family == AF_INET);
    MIB_IPFORWARD_TABLE2 routes = {0};
    routes.NumEntries = 1;
    routes.Table[0].InterfaceLuid = adapters[1].Luid;
    routes.Table[0].DestinationPrefix.Prefix.si_family = AF_INET6;
    routes.Table[0].ValidLifetime = 100;
    routes.Table[0].Metric = 20;
    adapters[1].Ipv6Metric = 10;
    CHECK(cf_select_windows_ip(adapters, &routes, &best));
    CHECK(best.family == AF_INET6 && best.default_route && best.metric == 30);
    addresses[1].DadState = IpDadStateTentative;
    CHECK(cf_select_windows_ip(adapters, &routes, &best) && best.family == AF_INET);
    addresses[1].DadState = IpDadStateDuplicate;
    CHECK(cf_select_windows_ip(adapters, &routes, &best) && best.family == AF_INET);
    addresses[1].DadState = IpDadStateDeprecated;
    CHECK(cf_select_windows_ip(adapters, &routes, &best) && best.family == AF_INET);
    addresses[1].DadState = IpDadStatePreferred;
    addresses[1].PreferredLifetime = 0;
    CHECK(cf_select_windows_ip(adapters, &routes, &best) && best.family == AF_INET);
    addresses[1].PreferredLifetime = 100;
    routes.Table[0].ValidLifetime = 0;
    CHECK(cf_select_windows_ip(adapters, &routes, &best) && best.family == AF_INET);
    routes.Table[0].ValidLifetime = 100;
    routes.Table[0].DestinationPrefix.PrefixLength = 64;
    CHECK(cf_select_windows_ip(adapters, &routes, &best) && best.family == AF_INET);
    routes.Table[0].DestinationPrefix.PrefixLength = 0;
    adapters[1].IfType = IF_TYPE_SOFTWARE_LOOPBACK;
    CHECK(cf_select_windows_ip(adapters, &routes, &best) && best.family == AF_INET);
    addresses[0].Address.iSockaddrLength = 1;
    CHECK(!cf_select_windows_ip(adapters, &routes, &best));
    CHECK(!cf_select_windows_ip(NULL, NULL, &best));
    return 0;
}
#endif

int main(void) {
    if (test_ranking() || test_addresses() || test_routes()) return 1;
#ifdef _WIN32
    if (test_windows_adapters()) return 1;
#else
    if (test_posix_adapters()) return 1;
#endif
    puts("network tests passed");
    return 0;
}
