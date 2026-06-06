#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char *proxy = NULL;
    if (cf_curl_easy_getinfo(curl, CURLINFO_PRIVATE, &proxy) == CURLE_OK)
        (void)proxy;
    return 0;
}

static CURLcode cf_perform_with_ip(cf_ctx *ctx, CURL *curl,
                                   const char *host, uint16_t port,
                                   const char *ip, const char *req_id,
                                   cf_method method, size_t attempt,
                                   size_t total)
{
    cf_config *cfg = cf_ctx_get_config(ctx);

    long timeout = (method == CF_METHOD_GET || method == CF_METHOD_HEAD)
        ? cf_config_get_get_timeout_ms(cfg)
        : cf_config_get_other_timeout_ms(cfg);
    long connect_to = cf_config_get_connect_timeout_ms(cfg);

    cf_vlog(cfg, "attempt %zu/%zu using IP %s (RESOLVE %s:%u:%s)\n",
            attempt, total, ip, host, port, ip);

    cf_log_curl_replay(cfg, curl, host, port, ip, req_id, method,
                       timeout, connect_to, 0);

    struct curl_slist *resolve = cf_build_resolve(host, port, ip);

    const char *hdr = cf_config_get_idempotency_header(cfg);
    struct curl_slist *headers = cf_shadow_get_headers(curl);
    struct curl_slist *owned = cf_append_idempotency_header(NULL, hdr, req_id);
    struct curl_slist *merged = owned;
    for (struct curl_slist *h = headers; h; h = h->next)
        merged = cf_curl_slist_append(merged, h->data);

    cf_curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
    cf_curl_easy_setopt(curl, CURLOPT_HTTPHEADER, merged);

    cf_curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
    cf_curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_to);
    cf_curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    CURLcode rc = cf_curl_easy_perform(curl);

    long http_code = 0;
    cf_curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    cf_vlog(cfg, "result on %s: curl=%d (%s) http=%ld\n",
            ip, (int)rc, curl_easy_strerror(rc), http_code);

    cf_curl_easy_setopt(curl, CURLOPT_RESOLVE, NULL);
    cf_curl_easy_setopt(curl, CURLOPT_HTTPHEADER, cf_shadow_get_headers(curl));
    cf_curl_slist_free_all(resolve);
    cf_curl_slist_free_all(owned);
    (void)cf_has_proxy(curl);
    return rc;
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

    cf_dns_entry *entry = cf_resolve_host(ctx, host, port);
    if (!entry)
        return CURLE_COULDNT_RESOLVE_HOST;

    cf_method method = cf_shadow_get_method(curl);
    char req_id[48];
    cf_generate_uuid(req_id, sizeof(req_id));
    cf_vlog(cfg, "request id %s for idempotency header\n", req_id);

    if (!entry->multi_ip) {
        if (entry->all_count == 1) {
            cf_vlog(cfg, "chosen IP %s (only address)\n", entry->all_addrs[0]);
            return cf_perform_with_ip(ctx, curl, host, port,
                                      entry->all_addrs[0], req_id, method, 1, 1);
        }
        cf_vlog(cfg, "multi-address host without ranking — passthrough\n");
        return cf_curl_easy_perform(curl);
    }

    CURLcode last = CURLE_OK;
    size_t attempts = entry->rank_count;
    if (attempts == 0) {
        cf_vlog(cfg, "no ranked IPs — passthrough\n");
        return cf_curl_easy_perform(curl);
    }

    cf_vlog(cfg, "failover enabled — trying up to %zu ranked IP(s)\n", attempts);

    for (size_t i = 0; i < attempts; i++) {
        last = cf_perform_with_ip(ctx, curl, host, port,
                                  entry->ranks[i].addr, req_id, method,
                                  i + 1, attempts);
        long http_code = 0;
        cf_curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (!cf_should_failover(last, http_code)) {
            cf_vlog(cfg, "using IP %s (attempt %zu succeeded)\n",
                    entry->ranks[i].addr, i + 1);
            return last;
        }

        if (http_code > 0)
            cf_vlog(cfg, "HTTP %ld received on %s — no failover\n",
                    http_code, entry->ranks[i].addr);
        else if (i + 1 < attempts)
            cf_vlog(cfg, "network error on %s — retrying next ranked IP\n",
                    entry->ranks[i].addr);
    }

    cf_vlog(cfg, "all %zu ranked IP(s) failed — giving up (%s)\n",
            attempts, curl_easy_strerror(last));
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
