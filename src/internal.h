#ifndef CURL_FO_INTERNAL_H
#define CURL_FO_INTERNAL_H

#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "curl_fo.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Ranked IP entry ─────────────────────────────────────────────────── */

typedef struct cf_ip_rank {
    char    addr[64];       /* textual IP (v4 or v6) */
    int     family;         /* AF_INET or AF_INET6 */
    unsigned bucket_ms;     /* latency rounded up to bucket */
    unsigned raw_ms;        /* raw measured latency */
} cf_ip_rank;

typedef struct cf_dns_entry {
    char          *host;
    uint16_t       port;
    cf_ip_rank    *ranks;
    size_t         rank_count;
    size_t         rank_cap;
    char         **all_addrs;   /* full resolved set (for TTL refresh) */
    size_t         all_count;
    bool           multi_ip;
    bool           probed;      /* true if latency probe was run */
    uint32_t       ttl_sec;
    uint64_t       resolved_at_ms;
    uint64_t       expires_at_ms;
    /* LRU + hash linkage */
    struct cf_dns_entry *lru_prev;
    struct cf_dns_entry *lru_next;
    struct cf_dns_entry *hash_next;
} cf_dns_entry;

/* ── Internal config struct ──────────────────────────────────────────── */

struct cf_config {
    size_t   lru_capacity;
    size_t   top_ips;
    long     get_timeout_ms;
    long     other_timeout_ms;
    long     connect_timeout_ms;
    char    *idempotency_header;
    unsigned latency_bucket_ms;
    unsigned default_ttl_sec;
};

/* ── DNS resolution result ───────────────────────────────────────────── */

typedef struct cf_dns_result {
    char       **addrs;
    size_t       count;
    uint32_t     ttl_sec;
    int          family_mixed; /* 1 if both v4 and v6 present */
} cf_dns_result;

void cf_dns_result_free(cf_dns_result *r);

/**
 * Resolve A and AAAA records via DNS UDP, respecting TTL from responses.
 * Falls back to getaddrinfo with default_ttl on failure.
 */
int cf_dns_resolve(const char *host, cf_dns_result *out,
                   unsigned default_ttl_sec);

/* ── Cache ───────────────────────────────────────────────────────────── */

struct cf_ctx {
    cf_config    *cfg;
    cf_dns_entry *lru_head;
    cf_dns_entry *lru_tail;
    cf_dns_entry **buckets;
    size_t          bucket_count;
    size_t          entry_count;
    void           *cache_mutex; /* cf_mutex */
};

cf_dns_entry *cf_cache_lookup(cf_ctx *ctx, const char *host, uint16_t port);
void          cf_cache_insert(cf_ctx *ctx, cf_dns_entry *entry);
void          cf_cache_remove(cf_ctx *ctx, cf_dns_entry *entry);
void          cf_cache_touch(cf_ctx *ctx, cf_dns_entry *entry);
cf_dns_entry *cf_resolve_host(cf_ctx *ctx, const char *host, uint16_t port);

/* ── Probe ───────────────────────────────────────────────────────────── */

int cf_probe_rank(const char *host, uint16_t port,
                  char **addrs, size_t count,
                  unsigned bucket_ms, size_t top_n,
                  cf_ip_rank **out_ranks, size_t *out_count);

/* ── Utility ─────────────────────────────────────────────────────────── */

uint64_t cf_now_ms(void);
void     cf_generate_uuid(char *buf, size_t buflen);
int      cf_parse_url(const char *url, char *host, size_t hostlen,
                      uint16_t *port, int *is_https, int *is_ws);
int      cf_addr_in_list(const char *addr, char **list, size_t count);
void     cf_shuffle_tied(cf_ip_rank *ranks, size_t count, unsigned bucket_ms);

/* Mutex abstraction */
void *cf_mutex_create(void);
void  cf_mutex_lock(void *m);
void  cf_mutex_unlock(void *m);
void  cf_mutex_destroy(void *m);

/* Port from scheme */
uint16_t cf_default_port(int is_https, int is_ws);

/* Per-handle shadow state (URL tracking for shim + API) */
void          cf_shadow_set_url(CURL *curl, const char *url);
const char   *cf_shadow_get_url(CURL *curl);
void          cf_shadow_set_method(CURL *curl, cf_method m);
cf_method     cf_shadow_get_method(CURL *curl);
void          cf_shadow_free(CURL *curl);
void          cf_easy_attach(CURL *curl);
void          cf_shadow_set_headers(CURL *curl, struct curl_slist *headers);
struct curl_slist *cf_shadow_get_headers(CURL *curl);

/* libcurl dispatch — uses dlsym when CURL_FO_SHIM_BUILD is set */
CURLcode cf_curl_easy_perform(CURL *curl);
CURLcode cf_curl_easy_setopt(CURL *curl, CURLoption opt, ...);
CURLcode cf_curl_easy_getinfo(CURL *curl, CURLINFO info, void *param);
CURL    *cf_curl_easy_init(void);
void     cf_curl_easy_cleanup(CURL *curl);
struct curl_slist *cf_curl_slist_append(struct curl_slist *list, const char *s);
void cf_curl_slist_free_all(struct curl_slist *list);
CURLcode cf_curl_ws_send(CURL *curl, const void *buf, size_t len, size_t *sent,
                         curl_off_t fragsize, unsigned flags);
CURLcode cf_curl_ws_recv(CURL *curl, void *buf, size_t len, size_t *recvd,
                         const struct curl_ws_frame **meta);
curl_version_info_data *cf_curl_version_info(CURLversion ver);

#endif /* CURL_FO_INTERNAL_H */
