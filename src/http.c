#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cf_should_failover_cfg(cf_config *cfg, CURLcode code, long http_code)
{
    if (http_code > 0) {
        if (cfg && cfg->failover_gateway &&
            (http_code == 502 || http_code == 503 || http_code == 504))
            return 1;
        return 0;
    }
    return code != CURLE_OK;
}

static struct curl_slist *cf_append_idempotency_header(
    struct curl_slist *headers, const char *hdr_name, const char *uuid)
{
    char line[128];
    snprintf(line, sizeof(line), "%s: %s", hdr_name, uuid);
    return cf_curl_slist_append(headers, line);
}

static struct curl_slist *cf_build_resolve(const char *host, uint16_t port,
                                           const char *ip)
{
    char entry[320];
    snprintf(entry, sizeof(entry), "%s:%u:%s", host, port, ip);
    struct curl_slist *sl = NULL;
    return cf_curl_slist_append(sl, entry);
}

static int cf_has_proxy(CURL *curl)
{
    return cf_shadow_get_proxy(curl) != NULL;
}

static CURLcode cf_perform_prepared(cf_ctx *ctx, CURL *curl,
                                    const char *host, uint16_t port,
                                    const char *ip, const char *req_id,
                                    cf_method method, size_t attempt,
                                    size_t total, int preconnected)
{
    cf_config *cfg = cf_ctx_get_config(ctx);

    long timeout = (method == CF_METHOD_GET || method == CF_METHOD_HEAD)
        ? cf_config_get_get_timeout_ms(cfg)
        : cf_config_get_other_timeout_ms(cfg);
    long connect_to = preconnected ? 0L : cf_config_get_connect_timeout_ms(cfg);

    cf_vlog(cfg, "attempt %zu/%zu using IP %s (RESOLVE %s:%u:%s)%s\n",
            attempt, total, ip, host, port, ip,
            preconnected ? " [preconnected]" : "");

    cf_log_curl_replay(cfg, curl, host, port, ip, req_id, method,
                       timeout, connect_to, 0);

    struct curl_slist *resolve = cf_build_resolve(host, port, ip);

    const char *hdr = cf_config_get_idempotency_header(cfg);
    struct curl_slist *headers = cf_shadow_get_headers(curl);
    struct curl_slist *owned = cf_append_idempotency_header(NULL, hdr, req_id);
    struct curl_slist *merged = owned;
    for (struct curl_slist *h = headers; h; h = h->next)
        merged = cf_curl_slist_append(merged, h->data);

    void *shadow = NULL;
    cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &shadow);

    if (preconnected && shadow) {
        cf_curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION,
                            cf_shadow_opensocket_cb);
        cf_curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, shadow);
        cf_curl_easy_setopt(curl, CURLOPT_CLOSESOCKETFUNCTION,
                            cf_shadow_closesocket_cb);
        cf_curl_easy_setopt(curl, CURLOPT_CLOSESOCKETDATA, shadow);
    }

    cf_curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
    cf_curl_easy_setopt(curl, CURLOPT_HTTPHEADER, merged);

    cf_curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
    cf_curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_to);
    cf_curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    CURLcode rc = cf_curl_easy_perform(curl);

    long http_code = 0;
    cf_curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    cf_vlog(cfg, "result on %s: curl=%d (%s) http=%ld\n",
            ip, (int)rc, cf_curl_easy_strerror(rc), http_code);

    cf_curl_easy_setopt(curl, CURLOPT_RESOLVE, NULL);
    cf_curl_easy_setopt(curl, CURLOPT_HTTPHEADER, cf_shadow_get_headers(curl));
    if (preconnected) {
        cf_curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, NULL);
        cf_curl_easy_setopt(curl, CURLOPT_CLOSESOCKETFUNCTION, NULL);
    }
    cf_curl_slist_free_all(resolve);
    cf_curl_slist_free_all(owned);
    return rc;
}

static CURLcode cf_perform_with_ip(cf_ctx *ctx, CURL *curl,
                                   const char *host, uint16_t port,
                                   const char *ip, const char *req_id,
                                   cf_method method, size_t attempt,
                                   size_t total)
{
    return cf_perform_prepared(ctx, curl, host, port, ip, req_id, method,
                               attempt, total, 0);
}

static CURLcode cf_perform_with_raced_socket(cf_ctx *ctx, CURL *curl,
                                             const char *host, uint16_t port,
                                             const char *ip, int fd,
                                             const char *req_id,
                                             cf_method method)
{
    cf_shadow_set_preconnected(curl, fd, ip);
    return cf_perform_prepared(ctx, curl, host, port, ip, req_id, method,
                               1, 1, 1);
}

/**
 * Returns 1 if HTTP performed via race, 0 if skipped, -1 if race failed.
 * On return 1, winner_ip_out (size winner_ip_len) holds the winning IP.
 */
static int cf_try_tcp_race(cf_ctx *ctx, CURL *curl,
                           const char *host, uint16_t port,
                           cf_resolve_view *snap,
                           const char *req_id, cf_method method,
                           CURLcode *out_rc,
                           char *winner_ip_out, size_t winner_ip_len)
{
    cf_config *cfg = cf_ctx_get_config(ctx);
    if (!cf_config_get_tcp_race(cfg))
        return 0;

    char *addrs[CF_RESOLVE_MAX_IPS];
    size_t count = 0;
    for (size_t i = 0; i < snap->all_count && i < CF_RESOLVE_MAX_IPS; i++) {
        if (snap->all_addrs[i][0])
            addrs[count++] = snap->all_addrs[i];
    }
    if (count < 2)
        return 0;

    cf_race_result *race = NULL;
    if (cf_tcp_race(addrs, count, port, cfg, &race) < 0 || !race)
        return -1;

    int winner_fd = cf_race_winner_fd(race);
    char winner_ip[64] = {0};
    strncpy(winner_ip, cf_race_winner_addr(race), sizeof(winner_ip) - 1);
    if (winner_ip_out && winner_ip_len > 0)
        strncpy(winner_ip_out, winner_ip, winner_ip_len - 1);

    cf_race_drain_async(ctx, host, port, race);

    *out_rc = cf_perform_with_raced_socket(ctx, curl, host, port,
                                           winner_ip, winner_fd,
                                           req_id, method);
    return 1;
}

CURLcode cf_easy_perform(cf_ctx *ctx, CURL *curl)
{
    if (!ctx || !curl)
        return CURLE_BAD_FUNCTION_ARGUMENT;

    cf_config *cfg = cf_ctx_get_config(ctx);

    const char *url = cf_shadow_get_url(curl);
    if (!url)
        return CURLE_URL_MALFORMAT;

    char host[256];
    uint16_t port;
    int is_https = 0, is_ws = 0;
    if (cf_parse_url(url, host, sizeof(host), &port, &is_https, &is_ws) < 0)
        return CURLE_URL_MALFORMAT;
    if (is_ws)
        return CURLE_UNSUPPORTED_PROTOCOL;

    cf_vlog(cfg, "request %s → %s:%u\n", url, host, port);

    if (cf_has_proxy(curl)) {
        cf_vlog(cfg, "HTTP proxy configured — RESOLVE pinning skipped\n");
        return cf_curl_easy_perform(curl);
    }

    cf_resolve_view snap;
    if (cf_resolve_snapshot(ctx, host, port, &snap) < 0 || !snap.ok)
        return CURLE_COULDNT_RESOLVE_HOST;

    cf_method method = cf_shadow_get_method(curl);
    char req_id[48];
    cf_generate_uuid(req_id, sizeof(req_id));
    cf_vlog(cfg, "request id %s for idempotency header\n", req_id);

    if (!snap.multi_ip) {
        if (snap.all_count == 1) {
            cf_vlog(cfg, "chosen IP %s (only address)\n", snap.single_ip);
            return cf_perform_with_ip(ctx, curl, host, port,
                                      snap.single_ip, req_id, method, 1, 1);
        }
        cf_vlog(cfg, "multi-address host without ranking — passthrough\n");
        return cf_curl_easy_perform(curl);
    }

    if (snap.rank_count == 0 && snap.all_count > 1) {
        CURLcode race_rc = CURLE_OK;
        char race_winner[64] = {0};
        int raced = cf_try_tcp_race(ctx, curl, host, port, &snap,
                                    req_id, method, &race_rc,
                                    race_winner, sizeof(race_winner));
        if (raced == 1) {
            long http_code = 0;
            cf_curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (!cf_should_failover_cfg(cfg, race_rc, http_code))
                return race_rc;
            /*
             * Race winner failed (transport error or gateway 502/503/504 with
             * failover_gateway=on).  The async drain hasn't committed ranks yet
             * so snap.rank_count is still 0.  Retry sequentially over the
             * remaining all_addrs, skipping the winner that already failed.
             */
            cf_vlog(cfg, "race winner %s failed — retrying %zu sibling(s)\n",
                    race_winner, snap.all_count > 1 ? snap.all_count - 1 : 0);
            CURLcode last = race_rc;
            for (size_t i = 0; i < snap.all_count && i < CF_RESOLVE_MAX_IPS; i++) {
                if (!snap.all_addrs[i][0]) continue;
                if (strcmp(snap.all_addrs[i], race_winner) == 0) continue;
                last = cf_perform_with_ip(ctx, curl, host, port,
                                          snap.all_addrs[i], req_id, method,
                                          i + 1, snap.all_count);
                http_code = 0;
                cf_curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                if (!cf_should_failover_cfg(cfg, last, http_code)) {
                    cf_vlog(cfg, "cold fallback to %s succeeded\n",
                            snap.all_addrs[i]);
                    return last;
                }
                if (i + 1 < snap.all_count)
                    cf_vlog(cfg, "cold fallback: %s failed — trying next\n",
                            snap.all_addrs[i]);
            }
            return last;
        } else if (raced < 0) {
            cf_vlog(cfg, "TCP race failed — passthrough\n");
            return cf_curl_easy_perform(curl);
        }
    }

    CURLcode last = CURLE_OK;
    size_t attempts = snap.rank_count;
    if (attempts == 0) {
        cf_vlog(cfg, "no ranked IPs — passthrough\n");
        return cf_curl_easy_perform(curl);
    }

    cf_vlog(cfg, "failover enabled — trying up to %zu ranked IP(s)\n", attempts);

    for (size_t i = 0; i < attempts; i++) {
        last = cf_perform_with_ip(ctx, curl, host, port,
                                  snap.ranks[i], req_id, method,
                                  i + 1, attempts);
        long http_code = 0;
        cf_curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (!cf_should_failover_cfg(cfg, last, http_code)) {
            cf_vlog(cfg, "using IP %s (attempt %zu succeeded)\n",
                    snap.ranks[i], i + 1);
            return last;
        }

        if (http_code > 0)
            cf_vlog(cfg, "HTTP %ld received on %s — %s\n",
                    http_code, snap.ranks[i],
                    cfg->failover_gateway ? "retrying next IP" : "no failover");
        else if (i + 1 < attempts)
            cf_vlog(cfg, "network error on %s — retrying next ranked IP\n",
                    snap.ranks[i]);
    }

    cf_vlog(cfg, "all %zu ranked IP(s) failed — giving up (%s)\n",
            attempts, cf_curl_easy_strerror(last));
    return last;
}

CURLcode cf_request(cf_ctx *ctx, const char *url, cf_method method)
{
    if (!ctx || !url)
        return CURLE_BAD_FUNCTION_ARGUMENT;

    CURL *curl = cf_curl_easy_init();
    if (!curl)
        return CURLE_OUT_OF_MEMORY;

    cf_easy_attach(curl);
    cf_shadow_set_url(curl, url);
    cf_curl_easy_setopt(curl, CURLOPT_URL, url);
    if (method == CF_METHOD_GET)
        cf_curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    else if (method == CF_METHOD_HEAD)
        cf_curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

    CURLcode rc = cf_easy_perform(ctx, curl);
    cf_curl_easy_cleanup(curl);
    return rc;
}
