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

static int cf_tcp_connect_latency(const char *addr, uint16_t port,
                                    unsigned *raw_ms)
{
#ifdef _WIN32
    static int wsa_init;
    if (!wsa_init) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wsa_init = 1;
    }
#endif

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(addr, portstr, &hints, &res) != 0 || !res)
        return -1;

    int sock = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        return -1;
    }

#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    uint64_t start = cf_now_ms();
    int rc = connect(sock, res->ai_addr, (int)res->ai_addrlen);
#ifdef _WIN32
    if (rc < 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(sock);
        freeaddrinfo(res);
        return -1;
    }
#else
    if (rc < 0 && errno != EINPROGRESS) {
        close(sock);
        freeaddrinfo(res);
        return -1;
    }
#endif

    int prc;
#ifdef _WIN32
    WSAPOLLFD pfd;
    pfd.fd = (SOCKET)sock;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    prc = WSAPoll(&pfd, 1, 3000);
#else
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLOUT;
    prc = poll(&pfd, 1, 3000);
#endif
    uint64_t elapsed = cf_now_ms() - start;

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    freeaddrinfo(res);

    if (prc <= 0)
        return -1;

    *raw_ms = (unsigned)elapsed;
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
                  cf_ip_rank **out_ranks, size_t *out_count)
{
    (void)host;
    if (!addrs || count == 0 || !out_ranks || !out_count)
        return -1;

    cf_ip_rank *ranks = calloc(count, sizeof(cf_ip_rank));
    if (!ranks) return -1;

    size_t valid = 0;
    for (size_t i = 0; i < count; i++) {
        unsigned raw = 0;
        if (cf_tcp_connect_latency(addrs[i], port, &raw) < 0)
            continue;
        cf_ip_rank *r = &ranks[valid++];
        strncpy(r->addr, addrs[i], sizeof(r->addr) - 1);
        r->raw_ms = raw;
        r->bucket_ms = cf_round_bucket(raw, bucket_ms);
    }

    if (valid == 0) {
        free(ranks);
        return -1;
    }

    qsort(ranks, valid, sizeof(cf_ip_rank), cf_rank_cmp);

    /* Randomize within equal buckets */
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
    return 0;
}
