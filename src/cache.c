#include "internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFu
#endif

static uint32_t cf_cache_key_hash(const char *host, uint16_t port)
{
    uint32_t h = 5381;
    for (const unsigned char *p = (const unsigned char *)host; *p; p++)
        h = ((h << 5) + h) + *p;
    h = ((h << 5) + h) + port;
    return h;
}

static void cf_entry_free(cf_dns_entry *e)
{
    if (!e) return;
    free(e->host);
    for (size_t i = 0; i < e->rank_count; i++)
        (void)e->ranks[i];
    free(e->ranks);
    if (e->all_addrs) {
        for (size_t i = 0; i < e->all_count; i++)
            free(e->all_addrs[i]);
        free(e->all_addrs);
    }
    free(e);
}

static void cf_cache_unlink(cf_ctx *ctx, cf_dns_entry *e)
{
    if (e->lru_prev)
        e->lru_prev->lru_next = e->lru_next;
    else
        ctx->lru_head = e->lru_next;

    if (e->lru_next)
        e->lru_next->lru_prev = e->lru_prev;
    else
        ctx->lru_tail = e->lru_prev;
}

static void cf_cache_link_head(cf_ctx *ctx, cf_dns_entry *e)
{
    e->lru_prev = NULL;
    e->lru_next = ctx->lru_head;
    if (ctx->lru_head)
        ctx->lru_head->lru_prev = e;
    ctx->lru_head = e;
    if (!ctx->lru_tail)
        ctx->lru_tail = e;
}

void cf_cache_touch(cf_ctx *ctx, cf_dns_entry *entry)
{
    if (ctx->lru_head == entry)
        return;
    cf_cache_unlink(ctx, entry);
    cf_cache_link_head(ctx, entry);
}

void cf_cache_remove(cf_ctx *ctx, cf_dns_entry *entry)
{
    uint32_t h = cf_cache_key_hash(entry->host, entry->port);
    size_t idx = h % ctx->bucket_count;
    cf_dns_entry **pp = &ctx->buckets[idx];
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->hash_next;
            break;
        }
        pp = &(*pp)->hash_next;
    }
    cf_cache_unlink(ctx, entry);
    ctx->entry_count--;
    if (entry->refs == 0)
        cf_entry_free(entry);
    else
        entry->evicted = true;
}

void cf_dns_entry_unref(cf_ctx *ctx, cf_dns_entry *entry)
{
    if (!ctx || !entry)
        return;
    cf_mutex_lock(ctx->cache_mutex);
    if (entry->refs > 0)
        entry->refs--;
    if (entry->refs == 0 && entry->evicted)
        cf_entry_free(entry);
    cf_mutex_unlock(ctx->cache_mutex);
}

/* hash_next field — add to struct via embedding in lru or separate */
/* We'll use host pointer in separate hash chain via index in buckets storing pointers */

/* Extend cf_dns_entry with hash_next - store in internal by redefining */

static cf_dns_entry *cf_cache_bucket_find(cf_ctx *ctx, const char *host,
                                          uint16_t port)
{
    uint32_t h = cf_cache_key_hash(host, port);
    size_t idx = h % ctx->bucket_count;
    for (cf_dns_entry *e = ctx->buckets[idx]; e; e = e->hash_next) {
        if (e->port == port && strcmp(e->host, host) == 0)
            return e;
    }
    return NULL;
}

cf_dns_entry *cf_cache_lookup(cf_ctx *ctx, const char *host, uint16_t port)
{
    cf_mutex_lock(ctx->cache_mutex);
    cf_dns_entry *e = cf_cache_bucket_find(ctx, host, port);
    if (e) {
        cf_cache_touch(ctx, e);
        e->refs++;
    }
    cf_mutex_unlock(ctx->cache_mutex);
    return e;
}

void cf_cache_insert(cf_ctx *ctx, cf_dns_entry *entry)
{
    cf_mutex_lock(ctx->cache_mutex);

    cf_dns_entry *existing = cf_cache_bucket_find(ctx, entry->host, entry->port);
    if (existing) {
        cf_cache_remove(ctx, existing);
    }

    while (ctx->entry_count >= ctx->cfg->lru_capacity && ctx->lru_tail) {
        cf_cache_remove(ctx, ctx->lru_tail);
    }

    uint32_t h = cf_cache_key_hash(entry->host, entry->port);
    size_t idx = h % ctx->bucket_count;
    entry->hash_next = ctx->buckets[idx];
    ctx->buckets[idx] = entry;
    cf_cache_link_head(ctx, entry);
    ctx->entry_count++;

    cf_mutex_unlock(ctx->cache_mutex);
}

static cf_dns_entry *cf_entry_create(const char *host, uint16_t port)
{
    cf_dns_entry *e = calloc(1, sizeof(cf_dns_entry));
    if (!e) return NULL;
    e->host = strdup(host);
    if (!e->host) {
        free(e);
        return NULL;
    }
    e->port = port;
    return e;
}

static int cf_entry_set_all_addrs(cf_dns_entry *e, char **addrs, size_t count)
{
    if (e->all_addrs) {
        for (size_t i = 0; i < e->all_count; i++)
            free(e->all_addrs[i]);
        free(e->all_addrs);
    }
    e->all_addrs = malloc(count * sizeof(char *));
    if (!e->all_addrs) return -1;
    e->all_count = count;
    for (size_t i = 0; i < count; i++) {
        e->all_addrs[i] = strdup(addrs[i]);
        if (!e->all_addrs[i]) return -1;
    }
    return 0;
}

static int cf_entry_set_ranks(cf_dns_entry *e, cf_ip_rank *ranks, size_t count)
{
    cf_ip_rank *new_ranks = malloc(count * sizeof(cf_ip_rank));
    if (!new_ranks) {
        free(e->ranks);
        e->ranks = NULL;
        e->rank_count = 0;
        e->rank_cap = 0;
        return -1;
    }
    free(e->ranks);
    e->ranks = new_ranks;
    memcpy(e->ranks, ranks, count * sizeof(cf_ip_rank));
    e->rank_count = count;
    e->rank_cap = count;
    return 0;
}

/**
 * TTL refresh: re-query A/AAAA only.
 * If top IP still present, keep ranking (drop removed IPs, append new at end).
 * If top IP gone, caller must re-probe.
 */
static int cf_entry_refresh_ttl(cf_ctx *ctx, cf_dns_entry *entry)
{
    cf_dns_result res;
    memset(&res, 0, sizeof(res));

    if (cf_dns_resolve(entry->host, &res, ctx->cfg->default_ttl_sec, ctx->cfg) < 0)
        return -1;

    char *top_ip = entry->rank_count > 0 ? entry->ranks[0].addr : NULL;
    int top_still_present = top_ip && cf_addr_in_list(top_ip, res.addrs, res.count);

    if (cf_entry_set_all_addrs(entry, res.addrs, res.count) < 0) {
        cf_dns_result_free(&res);
        return -1;
    }

    entry->ttl_sec = res.ttl_sec;
    entry->resolved_at_ms = cf_now_ms();
    entry->expires_at_ms = entry->resolved_at_ms + (uint64_t)res.ttl_sec * 1000ULL;
    entry->multi_ip = res.count > 1;

    if (!entry->multi_ip) {
        entry->rank_count = 0;
        entry->probed = 0;
        cf_dns_result_free(&res);
        return 0;
    }

    if (!entry->probed || !top_still_present) {
        cf_dns_result_free(&res);
        return 1; /* signal: need full probe */
    }

    /* Preserve ranking: keep existing order for IPs still present */
    cf_ip_rank *new_ranks = calloc(entry->rank_count + res.count, sizeof(cf_ip_rank));
    if (!new_ranks) {
        cf_dns_result_free(&res);
        return -1;
    }
    size_t nr = 0;

    for (size_t i = 0; i < entry->rank_count; i++) {
        if (cf_addr_in_list(entry->ranks[i].addr, res.addrs, res.count))
            new_ranks[nr++] = entry->ranks[i];
    }
    for (size_t i = 0; i < res.count; i++) {
        int already_ranked = 0;
        for (size_t k = 0; k < nr; k++) {
            if (strcmp(new_ranks[k].addr, res.addrs[i]) == 0) {
                already_ranked = 1;
                break;
            }
        }
        if (!already_ranked) {
            strncpy(new_ranks[nr].addr, res.addrs[i], sizeof(new_ranks[nr].addr) - 1);
            new_ranks[nr].bucket_ms = UINT32_MAX;
            new_ranks[nr].raw_ms = UINT32_MAX;
            nr++;
        }
    }

    size_t top_n = ctx->cfg->top_ips;
    if (nr > top_n) nr = top_n;

    if (cf_entry_set_ranks(entry, new_ranks, nr) < 0) {
        free(new_ranks);
        cf_dns_result_free(&res);
        return -1;
    }
    free(new_ranks);
    cf_dns_result_free(&res);
    return 0;
}

static int cf_entry_populate(cf_ctx *ctx, cf_dns_entry *entry)
{
    cf_dns_result res;
    memset(&res, 0, sizeof(res));

    if (cf_dns_resolve(entry->host, &res, ctx->cfg->default_ttl_sec, ctx->cfg) < 0)
        return -1;

    if (cf_entry_set_all_addrs(entry, res.addrs, res.count) < 0) {
        cf_dns_result_free(&res);
        return -1;
    }

    entry->ttl_sec = res.ttl_sec;
    entry->resolved_at_ms = cf_now_ms();
    entry->expires_at_ms = entry->resolved_at_ms + (uint64_t)res.ttl_sec * 1000ULL;
    entry->multi_ip = res.count > 1;

    if (!entry->multi_ip) {
        entry->probed = 0;
        entry->rank_count = 0;
        cf_dns_result_free(&res);
        if (entry->all_count == 1)
            cf_vlog(ctx->cfg, "single IP %s — failover not needed\n", entry->all_addrs[0]);
        return 0;
    }

    cf_ip_rank *ranks = NULL;
    size_t rank_count = 0;
    if (cf_probe_rank(entry->host, entry->port, res.addrs, res.count,
                      ctx->cfg->latency_bucket_ms, ctx->cfg->top_ips,
                      &ranks, &rank_count, ctx->cfg) < 0) {
        cf_dns_result_free(&res);
        return -1;
    }

    if (cf_entry_set_ranks(entry, ranks, rank_count) < 0) {
        free(ranks);
        cf_dns_result_free(&res);
        return -1;
    }
    entry->probed = 1;
    free(ranks);
    cf_dns_result_free(&res);
    return 0;
}

static void cf_snapshot_from_entry(cf_dns_entry *entry, cf_resolve_view *snap)
{
    memset(snap, 0, sizeof(*snap));
    snap->ok = 1;
    snap->multi_ip = entry->multi_ip ? 1 : 0;
    snap->all_count = entry->all_count;
    snap->rank_count = entry->rank_count;
    if (!entry->multi_ip && entry->all_count == 1 && entry->all_addrs[0])
        strncpy(snap->single_ip, entry->all_addrs[0], sizeof(snap->single_ip) - 1);
    for (size_t i = 0; i < entry->rank_count && i < CF_RESOLVE_MAX_IPS; i++)
        strncpy(snap->ranks[i], entry->ranks[i].addr, sizeof(snap->ranks[i]) - 1);
}

static int cf_entry_reprobe(cf_ctx *ctx, cf_dns_entry *entry)
{
    free(entry->ranks);
    entry->ranks = NULL;
    entry->rank_count = 0;
    cf_ip_rank *new_ranks = NULL;
    size_t new_count = 0;
    if (cf_probe_rank(entry->host, entry->port,
                      entry->all_addrs, entry->all_count,
                      ctx->cfg->latency_bucket_ms,
                      ctx->cfg->top_ips,
                      &new_ranks, &new_count, ctx->cfg) == 0) {
        entry->ranks = new_ranks;
        entry->rank_count = new_count;
        entry->probed = 1;
        return 0;
    }
    return -1;
}

static int cf_entry_refresh_expired(cf_ctx *ctx, cf_dns_entry *entry)
{
    int rc = cf_entry_refresh_ttl(ctx, entry);
    if (rc == 1) {
        cf_vlog(ctx->cfg, "top ranked IP gone — re-probing %s:%u\n",
                entry->host, entry->port);
        if (cf_entry_reprobe(ctx, entry) < 0)
            return -1;
    } else if (rc < 0) {
        return -1;
    }
    entry->expires_at_ms = cf_now_ms() +
        (uint64_t)entry->ttl_sec * 1000ULL;
    return 0;
}

static void cf_cache_insert_locked(cf_ctx *ctx, cf_dns_entry *entry)
{
    cf_dns_entry *existing = cf_cache_bucket_find(ctx, entry->host, entry->port);
    if (existing)
        cf_cache_remove(ctx, existing);

    while (ctx->entry_count >= ctx->cfg->lru_capacity && ctx->lru_tail)
        cf_cache_remove(ctx, ctx->lru_tail);

    uint32_t h = cf_cache_key_hash(entry->host, entry->port);
    size_t idx = h % ctx->bucket_count;
    entry->hash_next = ctx->buckets[idx];
    ctx->buckets[idx] = entry;
    cf_cache_link_head(ctx, entry);
    ctx->entry_count++;
}

/**
 * Resolve host:port and copy ranked IPs into snap (safe after return).
 * DNS/probe runs outside cache_mutex on miss/TTL refresh.
 */
int cf_resolve_snapshot(cf_ctx *ctx, const char *host, uint16_t port,
                        cf_resolve_view *snap)
{
    if (!ctx || !host || !snap)
        return -1;
    memset(snap, 0, sizeof(*snap));

    cf_mutex_lock(ctx->cache_mutex);
    cf_dns_entry *entry = cf_cache_bucket_find(ctx, host, port);
    uint64_t now = cf_now_ms();

    if (entry && now < entry->expires_at_ms) {
        cf_vlog(ctx->cfg, "cache hit %s:%u (%zu IP(s), %zu ranked)\n",
                host, port, entry->all_count, entry->rank_count);
        cf_cache_touch(ctx, entry);
        cf_snapshot_from_entry(entry, snap);
        cf_mutex_unlock(ctx->cache_mutex);
        return 0;
    }

    if (entry) {
        if (entry->refreshing) {
            cf_snapshot_from_entry(entry, snap);
            cf_mutex_unlock(ctx->cache_mutex);
            return 0;
        }
        entry->refreshing = true;
        cf_mutex_unlock(ctx->cache_mutex);

        cf_vlog(ctx->cfg, "TTL expired for %s:%u — refreshing\n", host, port);
        int refresh_rc = cf_entry_refresh_expired(ctx, entry);

        cf_mutex_lock(ctx->cache_mutex);
        entry = cf_cache_bucket_find(ctx, host, port);
        if (entry)
            entry->refreshing = false;
        if (refresh_rc < 0 || !entry) {
            cf_mutex_unlock(ctx->cache_mutex);
            return -1;
        }
        cf_cache_touch(ctx, entry);
        cf_snapshot_from_entry(entry, snap);
        cf_mutex_unlock(ctx->cache_mutex);
        return 0;
    }

    cf_mutex_unlock(ctx->cache_mutex);
    cf_vlog(ctx->cfg, "cache miss %s:%u — resolving\n", host, port);

    cf_dns_entry *fresh = cf_entry_create(host, port);
    if (!fresh)
        return -1;
    if (cf_entry_populate(ctx, fresh) < 0) {
        cf_entry_free(fresh);
        return -1;
    }

    cf_mutex_lock(ctx->cache_mutex);
    entry = cf_cache_bucket_find(ctx, host, port);
    if (entry) {
        cf_entry_free(fresh);
        cf_cache_touch(ctx, entry);
        cf_snapshot_from_entry(entry, snap);
        cf_mutex_unlock(ctx->cache_mutex);
        return 0;
    }

    cf_cache_insert_locked(ctx, fresh);
    cf_snapshot_from_entry(fresh, snap);
    cf_mutex_unlock(ctx->cache_mutex);
    return 0;
}

cf_ctx *cf_ctx_create_default(void)
{
    cf_config *cfg = cf_config_create();
    if (!cfg) return NULL;
    cf_config_load_env(cfg);
    cf_ctx *ctx = cf_ctx_create(cfg);
    if (!ctx)
        cf_config_destroy(cfg);
    return ctx;
}

cf_ctx *cf_ctx_create(cf_config *cfg)
{
    if (!cfg) return NULL;
    cf_ctx *ctx = calloc(1, sizeof(cf_ctx));
    if (!ctx) return NULL;
    ctx->cfg = cfg;
    ctx->bucket_count = 1024;
    ctx->buckets = calloc(ctx->bucket_count, sizeof(cf_dns_entry *));
    ctx->cache_mutex = cf_mutex_create();
    if (!ctx->buckets || !ctx->cache_mutex) {
        free(ctx->buckets);
        cf_mutex_destroy(ctx->cache_mutex);
        free(ctx);
        return NULL;
    }
    return ctx;
}

void cf_ctx_destroy(cf_ctx *ctx)
{
    if (!ctx) return;
    cf_mutex_lock(ctx->cache_mutex);
    while (ctx->lru_tail)
        cf_cache_remove(ctx, ctx->lru_tail);
    cf_mutex_unlock(ctx->cache_mutex);
    cf_mutex_destroy(ctx->cache_mutex);
    free(ctx->buckets);
    cf_config_destroy(ctx->cfg);
    free(ctx);
}

cf_config *cf_ctx_get_config(cf_ctx *ctx)
{
    return ctx ? ctx->cfg : NULL;
}

void cf_ctx_invalidate(cf_ctx *ctx, const char *host, uint16_t port)
{
    if (!ctx || !host) return;
    cf_mutex_lock(ctx->cache_mutex);
    cf_dns_entry *e = cf_cache_bucket_find(ctx, host, port);
    if (e)
        cf_cache_remove(ctx, e);
    cf_mutex_unlock(ctx->cache_mutex);
}

void cf_ctx_clear_cache(cf_ctx *ctx)
{
    if (!ctx) return;
    cf_mutex_lock(ctx->cache_mutex);
    while (ctx->lru_tail)
        cf_cache_remove(ctx, ctx->lru_tail);
    cf_mutex_unlock(ctx->cache_mutex);
}
