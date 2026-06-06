#include "internal.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

static unsigned cf_round_bucket(unsigned ms, unsigned bucket)
{
    if (bucket == 0) bucket = 10;
    unsigned rounded = ((ms + bucket - 1) / bucket) * bucket;
    return rounded == 0 ? bucket : rounded;
}

static int cf_probe_timeout_ms(cf_config *cfg)
{
    long ms = cfg ? cf_config_get_connect_timeout_ms(cfg) : 3000L;
    if (ms <= 0)
        ms = 3000L;
    if (ms > 60000L)
        ms = 60000L;
    return (int)ms;
}

typedef struct cf_probe_sock {
    int              sock;
    int              active;
    char             addr[64];
    struct addrinfo *ai;
    uint64_t         start_ms;
} cf_probe_sock;

static void cf_probe_sock_close(cf_probe_sock *ps)
{
    if (!ps || ps->sock < 0)
        return;
#ifdef _WIN32
    closesocket(ps->sock);
#else
    close(ps->sock);
#endif
    ps->sock = -1;
}

static int cf_probe_sock_start(cf_probe_sock *ps, const char *addr, uint16_t port)
{
    memset(ps, 0, sizeof(*ps));
    ps->sock = -1;
    strncpy(ps->addr, addr, sizeof(ps->addr) - 1);

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(addr, portstr, &hints, &ps->ai) != 0 || !ps->ai)
        return -1;

    ps->sock = (int)socket(ps->ai->ai_family, ps->ai->ai_socktype, ps->ai->ai_protocol);
    if (ps->sock < 0) {
        freeaddrinfo(ps->ai);
        ps->ai = NULL;
        return -1;
    }

#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(ps->sock, FIONBIO, &nb);
#else
    int flags = fcntl(ps->sock, F_GETFL, 0);
    fcntl(ps->sock, F_SETFL, flags | O_NONBLOCK);
#endif

    ps->start_ms = cf_now_ms();
    int rc = connect(ps->sock, ps->ai->ai_addr, (int)ps->ai->ai_addrlen);
#ifdef _WIN32
    if (rc < 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        cf_probe_sock_close(ps);
        freeaddrinfo(ps->ai);
        ps->ai = NULL;
        return -1;
    }
#else
    if (rc < 0 && errno != EINPROGRESS) {
        cf_probe_sock_close(ps);
        freeaddrinfo(ps->ai);
        ps->ai = NULL;
        return -1;
    }
#endif
    ps->active = 1;
    return 0;
}

static int cf_probe_sock_finish(cf_probe_sock *ps, unsigned *raw_ms)
{
    if (!ps->active || ps->sock < 0)
        return -1;

    int so_err = 0;
#ifdef _WIN32
    int slen = (int)sizeof(so_err);
    if (getsockopt(ps->sock, SOL_SOCKET, SO_ERROR, (char *)&so_err, &slen) != 0
        || so_err != 0) {
        cf_probe_sock_close(ps);
        return -1;
    }
#else
    socklen_t slen = (socklen_t)sizeof(so_err);
    if (getsockopt(ps->sock, SOL_SOCKET, SO_ERROR, &so_err, &slen) != 0
        || so_err != 0) {
        cf_probe_sock_close(ps);
        return -1;
    }
#endif

    *raw_ms = (unsigned)(cf_now_ms() - ps->start_ms);
    cf_probe_sock_close(ps);
    return 0;
}

static int cf_rank_cmp(const void *a, const void *b)
{
    const cf_ip_rank *ra = a;
    const cf_ip_rank *rb = b;
    if (ra->bucket_ms < rb->bucket_ms) return -1;
    if (ra->bucket_ms > rb->bucket_ms) return 1;
    return 0;
}

int cf_probe_rank(const char *host, uint16_t port,
                  char **addrs, size_t count,
                  unsigned bucket_ms, size_t top_n,
                  cf_ip_rank **out_ranks, size_t *out_count,
                  cf_config *cfg)
{
    if (!addrs || count == 0 || !out_ranks || !out_count)
        return -1;

    cf_log_probe_start(cfg, host, port, count, bucket_ms);

#ifdef _WIN32
    static int wsa_init;
    if (!wsa_init) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wsa_init = 1;
    }
#endif

    int timeout_ms = cf_probe_timeout_ms(cfg);
    cf_probe_sock *socks = calloc(count, sizeof(cf_probe_sock));
    if (!socks)
        return -1;

    size_t started = 0;
    for (size_t i = 0; i < count; i++) {
        socks[i].sock = -1;
        if (cf_probe_sock_start(&socks[i], addrs[i], port) == 0)
            started++;
    }

    if (started == 0) {
        free(socks);
        return -1;
    }

    uint64_t deadline = cf_now_ms() + (uint64_t)timeout_ms;
    while (started > 0 && cf_now_ms() < deadline) {
        int wait = (int)(deadline - cf_now_ms());
        if (wait <= 0)
            break;

        int prc = 0;
#ifdef _WIN32
        WSAPOLLFD *pfds = calloc(started, sizeof(WSAPOLLFD));
        size_t nfds = 0;
        for (size_t i = 0; i < count; i++) {
            if (!socks[i].active || socks[i].sock < 0)
                continue;
            pfds[nfds].fd = (SOCKET)socks[i].sock;
            pfds[nfds].events = POLLOUT;
            pfds[nfds].revents = 0;
            nfds++;
        }
        prc = nfds > 0 ? WSAPoll(pfds, (ULONG)nfds, wait) : 0;
        if (prc > 0) {
            size_t idx = 0;
            for (size_t i = 0; i < count; i++) {
                if (!socks[i].active || socks[i].sock < 0)
                    continue;
                if (pfds[idx].revents & (POLLERR | POLLHUP)) {
                    cf_probe_sock_close(&socks[i]);
                    socks[i].active = 0;
                    started--;
                } else if (pfds[idx].revents & POLLOUT) {
                    unsigned raw = 0;
                    if (cf_probe_sock_finish(&socks[i], &raw) == 0) {
                        socks[i].active = 2; /* success marker */
                        socks[i].start_ms = raw; /* reuse field */
                    } else {
                        socks[i].active = 0;
                    }
                    started--;
                }
                idx++;
            }
        }
        free(pfds);
#else
        struct pollfd *pfds = calloc(started, sizeof(struct pollfd));
        size_t nfds = 0;
        for (size_t i = 0; i < count; i++) {
            if (!socks[i].active || socks[i].sock < 0)
                continue;
            pfds[nfds].fd = socks[i].sock;
            pfds[nfds].events = POLLOUT;
            pfds[nfds].revents = 0;
            nfds++;
        }
        prc = nfds > 0 ? poll(pfds, (nfds_t)nfds, wait) : 0;
        if (prc > 0) {
            size_t idx = 0;
            for (size_t i = 0; i < count; i++) {
                if (!socks[i].active || socks[i].sock < 0)
                    continue;
                if (pfds[idx].revents & (POLLERR | POLLHUP)) {
                    cf_probe_sock_close(&socks[i]);
                    socks[i].active = 0;
                    started--;
                } else if (pfds[idx].revents & POLLOUT) {
                    unsigned raw = 0;
                    if (cf_probe_sock_finish(&socks[i], &raw) == 0) {
                        socks[i].active = 2;
                        socks[i].start_ms = raw;
                    } else {
                        socks[i].active = 0;
                    }
                    started--;
                }
                idx++;
            }
        }
        free(pfds);
#endif

        if (prc <= 0)
            break;
    }

    cf_ip_rank *ranks = calloc(count, sizeof(cf_ip_rank));
    if (!ranks) {
        for (size_t i = 0; i < count; i++) {
            cf_probe_sock_close(&socks[i]);
            freeaddrinfo(socks[i].ai);
        }
        free(socks);
        return -1;
    }

    size_t valid = 0;
    for (size_t i = 0; i < count; i++) {
        if (socks[i].active == 2) {
            unsigned raw = (unsigned)socks[i].start_ms;
            cf_ip_rank *r = &ranks[valid++];
            strncpy(r->addr, socks[i].addr, sizeof(r->addr) - 1);
            r->raw_ms = raw;
            r->bucket_ms = cf_round_bucket(raw, bucket_ms);
            cf_log_probe_ip(cfg, socks[i].addr, 1, raw, r->bucket_ms);
        } else {
            cf_log_probe_ip(cfg, addrs[i], 0, 0, 0);
            cf_probe_sock_close(&socks[i]);
        }
        freeaddrinfo(socks[i].ai);
    }
    free(socks);

    if (valid == 0) {
        free(ranks);
        return -1;
    }

    qsort(ranks, valid, sizeof(cf_ip_rank), cf_rank_cmp);

    for (size_t i = 0; i < valid; ) {
        size_t j = i + 1;
        while (j < valid && ranks[j].bucket_ms == ranks[i].bucket_ms)
            j++;
        if (j - i > 1) {
            for (size_t a = i; a < j - 1; a++) {
                size_t b = a + (size_t)(rand() % (int)(j - a));
                cf_ip_rank tmp = ranks[a];
                ranks[a] = ranks[b];
                ranks[b] = tmp;
            }
        }
        i = j;
    }

    size_t n = valid < top_n ? valid : top_n;
    cf_ip_rank *out = malloc(n * sizeof(cf_ip_rank));
    if (!out) {
        free(ranks);
        return -1;
    }
    memcpy(out, ranks, n * sizeof(cf_ip_rank));
    free(ranks);

    *out_ranks = out;
    *out_count = n;
    cf_log_probe_ranking(cfg, out, n);
    return 0;
}
