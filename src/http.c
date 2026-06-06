#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct curl_slist *cf_append_idempotency_header(
    struct curl_slist *headers, const char *hdr_name, const char *uuid)
{
    char line[128];
    snprintf(line, sizeof(line), "%s: %s", hdr_name, uuid);
    return curl_slist_append(headers, line);
}

static struct curl_slist *cf_build_resolve(const char *host, uint16_t port,
                                           const char *ip)
{
    char entry[320];
    snprintf(entry, sizeof(entry), "%s:%u:%s", host, port, ip);
    struct curl_slist *sl = NULL;
    return curl_slist_append(sl, entry);
}

static int cf_has_proxy(CURL *curl)
{
    char *proxy = NULL;
    if (curl_easy_getinfo(curl, CURLINFO_PRIVATE, &proxy) == CURLE_OK)
        (void)proxy;
    /* Detect proxy via effective URL not possible; check common option */
    return 0; /* Caller CURLOPT_PROXY disables RESOLVE — documented */
}

static CURLcode cf_perform_with_ip(cf_ctx *ctx, CURL *curl,
                                   const char *host, uint16_t port,
                                   const char *ip, const char *req_id,
                                   cf_method method)
{
    (void)ctx;
    struct curl_slist *resolve = cf_build_resolve(host, port, ip);

    const char *hdr = cf_config_get_idempotency_header(
        cf_ctx_get_config(ctx));
    struct curl_slist *headers = cf_shadow_get_headers(curl);
    struct curl_slist *owned = cf_append_idempotency_header(NULL, hdr, req_id);
    struct curl_slist *merged = owned;
    for (struct curl_slist *h = headers; h; h = h->next)
        merged = curl_slist_append(merged, h->data);

    curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, merged);

    long timeout = (method == CF_METHOD_GET || method == CF_METHOD_HEAD)
        ? cf_config_get_get_timeout_ms(cf_ctx_get_config(ctx))
        : cf_config_get_other_timeout_ms(cf_ctx_get_config(ctx));
    long connect_to = cf_config_get_connect_timeout_ms(cf_ctx_get_config(ctx));

    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_to);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    CURLcode rc = curl_easy_perform(curl);

    curl_easy_setopt(curl, CURLOPT_RESOLVE, NULL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, cf_shadow_get_headers(curl));
    curl_slist_free_all(resolve);
    curl_slist_free_all(owned);
    (void)cf_has_proxy(curl);
    return rc;
}

CURLcode cf_easy_perform(cf_ctx *ctx, CURL *curl)
{
    if (!ctx || !curl)
        return CURLE_BAD_FUNCTION_ARGUMENT;

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

    cf_dns_entry *entry = cf_resolve_host(ctx, host, port);
    if (!entry)
        return CURLE_COULDNT_RESOLVE_HOST;

    cf_method method = cf_shadow_get_method(curl);
    char req_id[48];
    cf_generate_uuid(req_id, sizeof(req_id));

    /* Transparent single-IP mode */
    if (!entry->multi_ip) {
        if (entry->all_count == 1) {
            return cf_perform_with_ip(ctx, curl, host, port,
                                      entry->all_addrs[0], req_id, method);
        }
        return curl_easy_perform(curl);
    }

    CURLcode last = CURLE_OK;
    size_t attempts = entry->rank_count;
    if (attempts == 0)
        return curl_easy_perform(curl);

    for (size_t i = 0; i < attempts; i++) {
        last = cf_perform_with_ip(ctx, curl, host, port,
                                  entry->ranks[i].addr, req_id, method);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (!cf_should_failover(last, http_code))
            return last;
    }
    return last;
}

CURLcode cf_request(cf_ctx *ctx, const char *url, cf_method method)
{
    if (!ctx || !url)
        return CURLE_BAD_FUNCTION_ARGUMENT;

    CURL *curl = curl_easy_init();
    if (!curl)
        return CURLE_OUT_OF_MEMORY;

    cf_easy_attach(curl);
    cf_shadow_set_url(curl, url);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    if (method == CF_METHOD_GET)
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    else if (method == CF_METHOD_HEAD)
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

    CURLcode rc = cf_easy_perform(ctx, curl);
    curl_easy_cleanup(curl);
    return rc;
}
