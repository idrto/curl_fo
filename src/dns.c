#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#  pragma comment(lib, "ws2_32.lib")
#  pragma comment(lib, "iphlpapi.lib")
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

typedef struct {
    char addr[64];
    uint32_t ttl;
} cf_dns_record;

static int cf_dns_append_label(uint8_t *buf, size_t buflen, size_t *off,
                               const char *host)
{
    char tmp[256];
    strncpy(tmp, host, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *p = tmp;
    while (*p) {
        char *dot = strchr(p, '.');
        size_t seg = dot ? (size_t)(dot - p) : strlen(p);
        if (seg == 0 || seg > 63 || *off + 1 + seg > buflen)
            return -1;
        buf[(*off)++] = (uint8_t)seg;
        memcpy(buf + *off, p, seg);
        *off += seg;
        if (!dot) break;
        p = dot + 1;
    }
    if (*off + 1 > buflen) return -1;
    buf[(*off)++] = 0;
    return 0;
}

static int cf_dns_read_name(const uint8_t *pkt, size_t pktlen,
                            size_t *offset, char *out, size_t outlen)
{
    size_t pos = *offset;
    size_t o = 0;
    int jumped = 0;
    size_t jump_back = 0;

    while (pos < pktlen) {
        uint8_t len = pkt[pos];
        if (len == 0) {
            if (!jumped) *offset = pos + 1;
            if (o < outlen) out[o] = '\0';
            return 0;
        }
        if ((len & 0xC0) == 0xC0) {
            if (pos + 1 >= pktlen) return -1;
            size_t ptr = ((size_t)(len & 0x3F) << 8) | pkt[pos + 1];
            if (!jumped) {
                jump_back = pos + 2;
                jumped = 1;
            }
            pos = ptr;
            continue;
        }
        pos++;
        if (pos + len > pktlen || o + len + 2 >= outlen) return -1;
        if (o > 0) out[o++] = '.';
        memcpy(out + o, pkt + pos, len);
        o += len;
        pos += len;
        if (!jumped) *offset = pos;
        if (jumped) pos = jump_back;
    }
    return -1;
}

static int cf_get_system_dns(char *server, size_t serverlen)
{
#ifdef _WIN32
    FIXED_INFO *info = NULL;
    ULONG buflen = 0;
    if (GetNetworkParams(NULL, &buflen) == ERROR_BUFFER_OVERFLOW) {
        info = malloc(buflen);
        if (info && GetNetworkParams(info, &buflen) == NO_ERROR) {
            strncpy(server, info->DnsServerList.IpAddress.String, serverlen - 1);
            server[serverlen - 1] = '\0';
            free(info);
            if (server[0]) return 0;
        }
        free(info);
    }
    strncpy(server, "8.8.8.8", serverlen - 1);
    return 0;
#else
    FILE *f = fopen("/etc/resolv.conf", "r");
    if (!f) {
        strncpy(server, "8.8.8.8", serverlen - 1);
        return 0;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char ip[64];
        if (sscanf(line, " nameserver %63s", ip) == 1 ||
            sscanf(line, "nameserver %63s", ip) == 1) {
            strncpy(server, ip, serverlen - 1);
            server[serverlen - 1] = '\0';
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    strncpy(server, "8.8.8.8", serverlen - 1);
    return 0;
#endif
}

static int cf_dns_query_type(const char *host, uint16_t qtype,
                             cf_dns_record *recs, size_t rec_cap,
                             size_t *rec_count, uint32_t *min_ttl,
                             cf_config *cfg)
{
    uint8_t query[512];
    size_t qlen = 0;
    uint16_t id = (uint16_t)(cf_now_ms() & 0xFFFF);

    query[qlen++] = (uint8_t)(id >> 8);
    query[qlen++] = (uint8_t)(id & 0xFF);
    query[qlen++] = 0x01;
    query[qlen++] = 0x00;
    query[qlen++] = 0x00;
    query[qlen++] = 0x01;
    query[qlen++] = 0x00;
    query[qlen++] = 0x00;
    query[qlen++] = 0x00;
    query[qlen++] = 0x00;
    query[qlen++] = 0x00;
    query[qlen++] = 0x00;

    if (cf_dns_append_label(query, sizeof(query), &qlen, host) < 0)
        return -1;

    if (qlen + 4 > sizeof(query)) return -1;
    query[qlen++] = (uint8_t)(qtype >> 8);
    query[qlen++] = (uint8_t)(qtype & 0xFF);
    query[qlen++] = 0x00;
    query[qlen++] = 0x01;

    char dns_server[64];
    cf_get_system_dns(dns_server, sizeof(dns_server));

    const char *rtype = (qtype == 1) ? "A" : (qtype == 28) ? "AAAA" : "TYPE?";
    cf_log_dns_query(cfg, host, rtype, dns_server, query, qlen, id);

#ifdef _WIN32
    static int wsa_init;
    if (!wsa_init) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wsa_init = 1;
    }
#endif

    int sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

#ifdef _WIN32
    DWORD tv = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(53);
    inet_pton(AF_INET, dns_server, &sa.sin_addr);

    if (sendto(sock, (const char *)query, (int)qlen, 0,
               (struct sockaddr *)&sa, sizeof(sa)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }

    uint8_t resp[1500];
    int rlen = (int)recvfrom(sock, (char *)resp, sizeof(resp), 0, NULL, NULL);
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    if (rlen < 12) return -1;

    uint16_t resp_id = (uint16_t)((resp[0] << 8) | resp[1]);
    if (resp_id != id) return -1;

    uint16_t qdcount = (uint16_t)((resp[4] << 8) | resp[5]);
    uint16_t ancount = (uint16_t)((resp[6] << 8) | resp[7]);
    size_t off = 12;

    char namebuf[256];
    for (uint16_t qi = 0; qi < qdcount && off < (size_t)rlen; qi++) {
        if (cf_dns_read_name(resp, (size_t)rlen, &off, namebuf, sizeof(namebuf)) < 0)
            return -1;
        off += 4;
    }

    *rec_count = 0;

    for (uint16_t ai = 0; ai < ancount && off + 12 <= (size_t)rlen; ai++) {
        if (cf_dns_read_name(resp, (size_t)rlen, &off, namebuf, sizeof(namebuf)) < 0)
            break;
        if (off + 10 > (size_t)rlen) break;

        uint16_t type = (uint16_t)((resp[off] << 8) | resp[off + 1]);
        off += 4;
        uint32_t ttl = ((uint32_t)resp[off] << 24) | ((uint32_t)resp[off + 1] << 16) |
                       ((uint32_t)resp[off + 2] << 8) | (uint32_t)resp[off + 3];
        off += 4;
        uint16_t rdlen = (uint16_t)((resp[off] << 8) | resp[off + 1]);
        off += 2;
        if (off + rdlen > (size_t)rlen) break;

        if (ttl > 0 && ttl < *min_ttl)
            *min_ttl = ttl;

        if (*rec_count < rec_cap) {
            cf_dns_record *rec = &recs[*rec_count];
            memset(rec, 0, sizeof(*rec));
            rec->ttl = ttl;

            if (type == 1 && rdlen == 4) {
                struct in_addr ia;
                memcpy(&ia, resp + off, 4);
                inet_ntop(AF_INET, &ia, rec->addr, sizeof(rec->addr));
                cf_log_dns_answer(cfg, rec->addr, ttl);
                (*rec_count)++;
            } else if (type == 28 && rdlen == 16) {
                struct in6_addr ia6;
                memcpy(&ia6, resp + off, 16);
                inet_ntop(AF_INET6, &ia6, rec->addr, sizeof(rec->addr));
                cf_log_dns_answer(cfg, rec->addr, ttl);
                (*rec_count)++;
            }
        }
        off += rdlen;
    }
    return 0;
}

static int cf_dns_resolve_getaddrinfo(const char *host, cf_dns_result *out,
                                      unsigned default_ttl, cf_config *cfg)
{
    cf_log_dns_fallback(cfg, host, "UDP query failed or empty");
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, NULL, &hints, &res) != 0)
        return -1;

    size_t cap = 16;
    out->addrs = malloc(cap * sizeof(char *));
    if (!out->addrs) {
        freeaddrinfo(res);
        return -1;
    }
    out->count = 0;
    out->ttl_sec = default_ttl;
    out->family_mixed = 0;

    for (rp = res; rp; rp = rp->ai_next) {
        char buf[64];
        void *addr = NULL;
        if (rp->ai_family == AF_INET)
            addr = &((struct sockaddr_in *)rp->ai_addr)->sin_addr;
        else if (rp->ai_family == AF_INET6)
            addr = &((struct sockaddr_in6 *)rp->ai_addr)->sin6_addr;
        else
            continue;

        if (!inet_ntop(rp->ai_family, addr, buf, sizeof(buf)))
            continue;

        int dup = 0;
        for (size_t i = 0; i < out->count; i++) {
            if (strcmp(out->addrs[i], buf) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;

        if (out->count >= cap) {
            cap *= 2;
            char **n = realloc(out->addrs, cap * sizeof(char *));
            if (!n) break;
            out->addrs = n;
        }
        out->addrs[out->count++] = strdup(buf);
    }
    freeaddrinfo(res);
    if (out->count > 0)
        cf_log_dns_summary(cfg, host, out, "getaddrinfo");
    return out->count > 0 ? 0 : -1;
}

void cf_dns_result_free(cf_dns_result *r)
{
    if (!r) return;
    if (r->addrs) {
        for (size_t i = 0; i < r->count; i++)
            free(r->addrs[i]);
        free(r->addrs);
    }
    memset(r, 0, sizeof(*r));
}

static void cf_dns_merge_records(cf_dns_result *out,
                                 cf_dns_record *recs, size_t count,
                                 uint32_t ttl)
{
    for (size_t i = 0; i < count; i++) {
        int dup = 0;
        for (size_t j = 0; j < out->count; j++) {
            if (strcmp(out->addrs[j], recs[i].addr) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;
        char **n = realloc(out->addrs, (out->count + 1) * sizeof(char *));
        if (!n) return;
        out->addrs = n;
        out->addrs[out->count++] = strdup(recs[i].addr);
    }
    if (ttl > 0 && ttl < out->ttl_sec)
        out->ttl_sec = ttl;
}

int cf_dns_resolve(const char *host, cf_dns_result *out,
                   unsigned default_ttl, cf_config *cfg)
{
    cf_dns_record recs_a[32], recs_aaaa[32];
    size_t a_count = 0, aaaa_count = 0;
    uint32_t min_ttl = default_ttl;

    memset(out, 0, sizeof(*out));
    out->ttl_sec = default_ttl;
    out->addrs = NULL;
    out->count = 0;

    int ok_a = cf_dns_query_type(host, 1, recs_a, 32, &a_count, &min_ttl, cfg) == 0;
    int ok_aaaa = cf_dns_query_type(host, 28, recs_aaaa, 32, &aaaa_count, &min_ttl, cfg) == 0;

    if (!ok_a && !ok_aaaa)
        return cf_dns_resolve_getaddrinfo(host, out, default_ttl, cfg);

    out->ttl_sec = min_ttl > 0 ? min_ttl : default_ttl;
    out->addrs = NULL;
    out->count = 0;

    if (ok_a)
        cf_dns_merge_records(out, recs_a, a_count, min_ttl);
    if (ok_aaaa)
        cf_dns_merge_records(out, recs_aaaa, aaaa_count, min_ttl);

    if (out->count == 0)
        return cf_dns_resolve_getaddrinfo(host, out, default_ttl, cfg);

    cf_log_dns_summary(cfg, host, out, "UDP DNS");
    return 0;
}
