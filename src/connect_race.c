#include "internal.h"

#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#  include <pthread.h>
#else
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <process.h>
#  include <windows.h>
#endif

#ifdef _WIN32
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

typedef enum {
    CF_RACE_SOCK_PENDING = 0,
    CF_RACE_SOCK_WINNER  = 1,
    CF_RACE_SOCK_LOSER   = 2,
    CF_RACE_SOCK_FAILED  = 3,
} cf_race_sock_state;

typedef struct cf_race_sock {
    int              sock;
    cf_race_sock_state state;
    char             addr[64];
    struct addrinfo *ai;
    uint64_t         start_ms;
    unsigned         raw_ms;
} cf_race_sock;

struct cf_race_result {
    int              winner_fd;
    char             winner_addr[64];
    unsigned         winner_raw_ms;
    cf_ip_rank      *ranks;
    size_t           rank_count;
    size_t           rank_cap;
    cf_race_sock    *socks;
    size_t           sock_count;
    int              timeout_ms;
    unsigned         bucket_ms;
    size_t           top_n;
    cf_config       *cfg;
};

typedef struct cf_race_drain_args {
    cf_ctx          *ctx;
    char             host[256];
    uint16_t         port;
    cf_race_result  *race;
} cf_race_drain_args;

static unsigned cf_round_bucket(unsigned ms, unsigned bucket)
{
    if (bucket == 0) bucket = 10;
    unsigned rounded = ((ms + bucket - 1) / bucket) * bucket;
    return rounded == 0 ? bucket : rounded;
}

static int cf_race_timeout_ms(cf_config *cfg)
{
    long ms = cfg ? cf_config_get_connect_timeout_ms(cfg) : 3000L;
    if (ms <= 0) ms = 3000L;
    if (ms > 60000L) ms = 60000L;
    return (int)ms;
}

static void cf_sock_rst_close(int sock)
{
    if (sock < 0) return;
    struct linger lg;
    lg.l_onoff = 1;
    lg.l_linger = 0;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_LINGER, (const char *)&lg, sizeof(lg));
    closesocket(sock);
#else
    setsockopt(sock, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    close(sock);
#endif
}

static int cf_race_sock_check(cf_race_sock *rs, unsigned *raw_ms)
{
    int so_err = 0;
#ifdef _WIN32
    int slen = (int)sizeof(so_err);
    if (getsockopt(rs->sock, SOL_SOCKET, SO_ERROR, (char *)&so_err, &slen) != 0)
        return -1;
#else
    socklen_t slen = (socklen_t)sizeof(so_err);
    if (getsockopt(rs->sock, SOL_SOCKET, SO_ERROR, &so_err, &slen) != 0)
        return -1;
#endif
    if (so_err != 0)
        return -1;
    *raw_ms = (unsigned)(cf_now_ms() - rs->start_ms);
    return 0;
}

static int cf_race_sock_start(cf_race_sock *rs, const char *addr, uint16_t port)
{
    memset(rs, 0, sizeof(*rs));
    rs->sock = -1;
    rs->state = CF_RACE_SOCK_PENDING;
    strncpy(rs->addr, addr, sizeof(rs->addr) - 1);

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(addr, portstr, &hints, &rs->ai) != 0 || !rs->ai)
        return -1;

    rs->sock = (int)socket(rs->ai->ai_family, rs->ai->ai_socktype, rs->ai->ai_protocol);
    if (rs->sock < 0) {
        freeaddrinfo(rs->ai);
        rs->ai = NULL;
        return -1;
    }

#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(rs->sock, FIONBIO, &nb);
#else
    int flags = fcntl(rs->sock, F_GETFL, 0);
    fcntl(rs->sock, F_SETFL, flags | O_NONBLOCK);
#endif

    rs->start_ms = cf_now_ms();
    int rc = connect(rs->sock, rs->ai->ai_addr, (int)rs->ai->ai_addrlen);
#ifdef _WIN32
    if (rc < 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        cf_sock_rst_close(rs->sock);
        rs->sock = -1;
        freeaddrinfo(rs->ai);
        rs->ai = NULL;
        return -1;
    }
#else
    if (rc < 0 && errno != EINPROGRESS) {
        cf_sock_rst_close(rs->sock);
        rs->sock = -1;
        freeaddrinfo(rs->ai);
        rs->ai = NULL;
        return -1;
    }
#endif
    return 0;
}

static void cf_race_sock_free(cf_race_sock *rs)
{
    if (!rs) return;
    if (rs->sock >= 0 && rs->state != CF_RACE_SOCK_WINNER)
        cf_sock_rst_close(rs->sock);
    rs->sock = -1;
    freeaddrinfo(rs->ai);
    rs->ai = NULL;
}

static int cf_race_rank_cmp(const void *a, const void *b)
{
    const cf_ip_rank *ra = a;
    const cf_ip_rank *rb = b;
    if (ra->bucket_ms < rb->bucket_ms) return -1;
    if (ra->bucket_ms > rb->bucket_ms) return 1;
    return 0;
}

static int cf_race_build_ranks(cf_race_result *race)
{
    size_t valid = 0;
    for (size_t i = 0; i < race->sock_count; i++) {
        cf_race_sock *rs = &race->socks[i];
        if (rs->state != CF_RACE_SOCK_WINNER && rs->state != CF_RACE_SOCK_LOSER)
            continue;
        if (valid >= race->rank_cap) {
            race->rank_cap *= 2;
            cf_ip_rank *nr = realloc(race->ranks, race->rank_cap * sizeof(cf_ip_rank));
            if (!nr) return -1;
            race->ranks = nr;
        }
        cf_ip_rank *r = &race->ranks[valid++];
        memset(r, 0, sizeof(*r));
        strncpy(r->addr, rs->addr, sizeof(r->addr) - 1);
        r->raw_ms = rs->raw_ms;
        r->bucket_ms = cf_round_bucket(rs->raw_ms, race->bucket_ms);
    }
    race->rank_count = valid;
    if (valid == 0) return -1;

    qsort(race->ranks, valid, sizeof(cf_ip_rank), cf_race_rank_cmp);
    for (size_t i = 0; i < valid; ) {
        size_t j = i + 1;
        while (j < valid && race->ranks[j].bucket_ms == race->ranks[i].bucket_ms)
            j++;
        if (j - i > 1)
            cf_shuffle_tied(&race->ranks[i], j - i, race->bucket_ms);
        i = j;
    }
    if (valid > race->top_n)
        valid = race->top_n;
    race->rank_count = valid;
    cf_log_race_ranking(race->cfg, race->ranks, valid);
    return 0;
}

static size_t cf_race_pending_count(cf_race_result *race)
{
    size_t n = 0;
    for (size_t i = 0; i < race->sock_count; i++) {
        if (race->socks[i].state == CF_RACE_SOCK_PENDING)
            n++;
    }
    return n;
}

static int cf_race_poll_once(cf_race_result *race, int wait_ms, int *have_winner)
{
    size_t pending = 0;
    for (size_t i = 0; i < race->sock_count; i++) {
        if (race->socks[i].state == CF_RACE_SOCK_PENDING && race->socks[i].sock >= 0)
            pending++;
    }
    if (pending == 0)
        return 0;

#ifdef _WIN32
    WSAPOLLFD *pfds = calloc(pending, sizeof(WSAPOLLFD));
    size_t *idxmap = calloc(pending, sizeof(size_t));
    if (!pfds || !idxmap) {
        free(pfds);
        free(idxmap);
        return -1;
    }
    size_t nfds = 0;
    for (size_t i = 0; i < race->sock_count; i++) {
        if (race->socks[i].state != CF_RACE_SOCK_PENDING || race->socks[i].sock < 0)
            continue;
        pfds[nfds].fd = (SOCKET)race->socks[i].sock;
        pfds[nfds].events = POLLOUT;
        idxmap[nfds] = i;
        nfds++;
    }
    int prc = WSAPoll(pfds, (ULONG)nfds, wait_ms);
    if (prc > 0) {
        for (size_t k = 0; k < nfds; k++) {
            if (!(pfds[k].revents & (POLLOUT | POLLERR | POLLHUP)))
                continue;
            cf_race_sock *rs = &race->socks[idxmap[k]];
            if (pfds[k].revents & (POLLERR | POLLHUP)) {
                cf_sock_rst_close(rs->sock);
                rs->sock = -1;
                rs->state = CF_RACE_SOCK_FAILED;
                continue;
            }
            unsigned raw = 0;
            if (cf_race_sock_check(rs, &raw) < 0) {
                cf_sock_rst_close(rs->sock);
                rs->sock = -1;
                rs->state = CF_RACE_SOCK_FAILED;
                continue;
            }
            rs->raw_ms = raw;
            if (!*have_winner) {
                *have_winner = 1;
                rs->state = CF_RACE_SOCK_WINNER;
                race->winner_fd = rs->sock;
                strncpy(race->winner_addr, rs->addr, sizeof(race->winner_addr) - 1);
                race->winner_raw_ms = raw;
                cf_log_race_winner(race->cfg, rs->addr, raw);
            } else {
                rs->state = CF_RACE_SOCK_LOSER;
                cf_log_race_loser_rst(race->cfg, rs->addr, raw);
                cf_sock_rst_close(rs->sock);
                rs->sock = -1;
            }
        }
    }
    free(pfds);
    free(idxmap);
    return prc;
#else
    struct pollfd *pfds = calloc(pending, sizeof(struct pollfd));
    size_t *idxmap = calloc(pending, sizeof(size_t));
    if (!pfds || !idxmap) {
        free(pfds);
        free(idxmap);
        return -1;
    }
    size_t nfds = 0;
    for (size_t i = 0; i < race->sock_count; i++) {
        if (race->socks[i].state != CF_RACE_SOCK_PENDING || race->socks[i].sock < 0)
            continue;
        pfds[nfds].fd = race->socks[i].sock;
        pfds[nfds].events = POLLOUT;
        idxmap[nfds] = i;
        nfds++;
    }
    int prc = poll(pfds, (nfds_t)nfds, wait_ms);
    if (prc > 0) {
        for (size_t k = 0; k < nfds; k++) {
            if (!(pfds[k].revents & (POLLOUT | POLLERR | POLLHUP)))
                continue;
            cf_race_sock *rs = &race->socks[idxmap[k]];
            if (pfds[k].revents & (POLLERR | POLLHUP)) {
                cf_sock_rst_close(rs->sock);
                rs->sock = -1;
                rs->state = CF_RACE_SOCK_FAILED;
                continue;
            }
            unsigned raw = 0;
            if (cf_race_sock_check(rs, &raw) < 0) {
                cf_sock_rst_close(rs->sock);
                rs->sock = -1;
                rs->state = CF_RACE_SOCK_FAILED;
                continue;
            }
            rs->raw_ms = raw;
            if (!*have_winner) {
                *have_winner = 1;
                rs->state = CF_RACE_SOCK_WINNER;
                race->winner_fd = rs->sock;
                strncpy(race->winner_addr, rs->addr, sizeof(race->winner_addr) - 1);
                race->winner_raw_ms = raw;
                cf_log_race_winner(race->cfg, rs->addr, raw);
            } else {
                rs->state = CF_RACE_SOCK_LOSER;
                cf_log_race_loser_rst(race->cfg, rs->addr, raw);
                cf_sock_rst_close(rs->sock);
                rs->sock = -1;
            }
        }
    }
    free(pfds);
    free(idxmap);
    return prc;
#endif
}

static void cf_race_drain_remaining(cf_race_result *race)
{
    uint64_t deadline = cf_now_ms() + (uint64_t)race->timeout_ms;
    int have_winner = (race->winner_fd >= 0);

    while (cf_race_pending_count(race) > 0 && cf_now_ms() < deadline) {
        int wait = (int)(deadline - cf_now_ms());
        if (wait <= 0)
            break;
        cf_race_poll_once(race, wait, &have_winner);
    }

    for (size_t i = 0; i < race->sock_count; i++) {
        cf_race_sock *rs = &race->socks[i];
        if (rs->state == CF_RACE_SOCK_PENDING && rs->sock >= 0) {
            cf_sock_rst_close(rs->sock);
            rs->sock = -1;
            rs->state = CF_RACE_SOCK_FAILED;
        }
    }
    cf_race_build_ranks(race);
}

#ifdef _WIN32
static unsigned __stdcall cf_race_drain_thread(void *arg)
#else
static void *cf_race_drain_thread(void *arg)
#endif
{
    cf_race_drain_args *args = arg;
    cf_race_drain_remaining(args->race);
    if (args->race->rank_count > 0)
        cf_cache_commit_ranks(args->ctx, args->host, args->port,
                              args->race->ranks, args->race->rank_count);
    cf_race_result_free(args->race);
    free(args);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

void cf_race_result_free(cf_race_result *race)
{
    if (!race) return;
    if (race->socks) {
        for (size_t i = 0; i < race->sock_count; i++) {
            if (race->socks[i].state == CF_RACE_SOCK_WINNER && race->socks[i].sock >= 0) {
                /* winner fd owned by libcurl */
                race->socks[i].sock = -1;
            }
            cf_race_sock_free(&race->socks[i]);
        }
        free(race->socks);
    }
    free(race->ranks);
    free(race);
}

int cf_tcp_race(char **addrs, size_t count, uint16_t port,
                cf_config *cfg, cf_race_result **out_race)
{
    if (!addrs || count == 0 || !out_race)
        return -1;

#ifdef _WIN32
    static int wsa_init;
    if (!wsa_init) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wsa_init = 1;
    }
#endif

    cf_race_result *race = calloc(1, sizeof(cf_race_result));
    if (!race) return -1;
    race->winner_fd = -1;
    race->cfg = cfg;
    race->timeout_ms = cf_race_timeout_ms(cfg);
    race->bucket_ms = cfg ? cfg->latency_bucket_ms : 10;
    race->top_n = cfg ? cfg->top_ips : 3;
    race->rank_cap = count;
    race->ranks = calloc(count, sizeof(cf_ip_rank));
    race->socks = calloc(count, sizeof(cf_race_sock));
    if (!race->ranks || !race->socks) {
        cf_race_result_free(race);
        return -1;
    }
    race->sock_count = count;

    cf_log_race_start(cfg, count, port, race->bucket_ms);

    size_t started = 0;
    for (size_t i = 0; i < count; i++) {
        if (cf_race_sock_start(&race->socks[i], addrs[i], port) == 0)
            started++;
        else
            race->socks[i].state = CF_RACE_SOCK_FAILED;
    }
    if (started == 0) {
        cf_race_result_free(race);
        return -1;
    }

    uint64_t deadline = cf_now_ms() + (uint64_t)race->timeout_ms;
    int have_winner = 0;

    while (!have_winner && cf_now_ms() < deadline) {
        int wait = (int)(deadline - cf_now_ms());
        if (wait <= 0)
            break;
        cf_race_poll_once(race, wait, &have_winner);
    }

    if (!have_winner) {
        for (size_t i = 0; i < race->sock_count; i++)
            cf_race_sock_free(&race->socks[i]);
        cf_race_result_free(race);
        return -1;
    }

    /* Catch any responders that completed in the same poll window */
    cf_race_poll_once(race, 0, &have_winner);

    *out_race = race;
    return 0;
}

void cf_race_sync_finish(cf_race_result *race)
{
    if (!race)
        return;
    cf_race_drain_remaining(race);
}

int cf_race_winner_fd(const cf_race_result *race)
{
    return race ? race->winner_fd : -1;
}

const char *cf_race_winner_addr(const cf_race_result *race)
{
    return race ? race->winner_addr : "";
}

size_t cf_race_rank_count(const cf_race_result *race)
{
    return race ? race->rank_count : 0;
}

void cf_race_drain_async(cf_ctx *ctx, const char *host, uint16_t port,
                         cf_race_result *race)
{
    if (!ctx || !host || !race)
        return;
    if (cf_race_pending_count(race) == 0) {
        cf_race_build_ranks(race);
        if (race->rank_count > 0)
            cf_cache_commit_ranks(ctx, host, port, race->ranks, race->rank_count);
        return;
    }

    cf_race_drain_args *args = calloc(1, sizeof(cf_race_drain_args));
    if (!args) {
        cf_race_drain_remaining(race);
        if (race->rank_count > 0)
            cf_cache_commit_ranks(ctx, host, port, race->ranks, race->rank_count);
        cf_race_result_free(race);
        return;
    }
    args->ctx = ctx;
    strncpy(args->host, host, sizeof(args->host) - 1);
    args->port = port;
    args->race = race;

#ifdef _WIN32
    uintptr_t h = _beginthreadex(NULL, 0, cf_race_drain_thread, args, 0, NULL);
    if (h)
        CloseHandle((HANDLE)h);
    else {
        cf_race_drain_remaining(race);
        if (race->rank_count > 0)
            cf_cache_commit_ranks(ctx, host, port, race->ranks, race->rank_count);
        cf_race_result_free(race);
        free(args);
    }
#else
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, cf_race_drain_thread, args) != 0) {
        cf_race_drain_remaining(race);
        if (race->rank_count > 0)
            cf_cache_commit_ranks(ctx, host, port, race->ranks, race->rank_count);
        cf_race_result_free(race);
        free(args);
    }
    pthread_attr_destroy(&attr);
#endif
}
