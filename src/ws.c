#include "internal.h"

#include <stdlib.h>
#include <string.h>

struct cf_ws {
    cf_ctx        *ctx;
    CURL          *curl;
    char           host[256];
    uint16_t       port;
    char           url[4096];
    cf_dns_entry  *entry;
    size_t         current_ip;
    int            retry_same;
    int            connected;
};

static int cf_ws_has_websockets(void)
{
    curl_version_info_data *vi = cf_curl_version_info(CURLVERSION_NOW);
    return vi && vi->version_num >= 0x075600;
}

static CURLcode cf_ws_try_connect(cf_ws *ws, size_t ip_index)
{
    if (!ws->entry || ip_index >= ws->entry->rank_count)
        return CURLE_COULDNT_CONNECT;

    char entry[320];
    snprintf(entry, sizeof(entry), "%s:%u:%s",
             ws->host, ws->port, ws->entry->ranks[ip_index].addr);

    struct curl_slist *resolve = cf_curl_slist_append(NULL, entry);
    cf_curl_easy_setopt(ws->curl, CURLOPT_RESOLVE, resolve);
    cf_curl_easy_setopt(ws->curl, CURLOPT_URL, ws->url);
    cf_curl_easy_setopt(ws->curl, CURLOPT_CONNECT_ONLY, 2L);
    cf_curl_easy_setopt(ws->curl, CURLOPT_TIMEOUT_MS,
                     (long)cf_config_get_other_timeout_ms(ws->ctx->cfg));
    cf_curl_easy_setopt(ws->curl, CURLOPT_CONNECTTIMEOUT_MS,
                     (long)cf_config_get_connect_timeout_ms(ws->ctx->cfg));

    CURLcode rc = cf_curl_easy_perform(ws->curl);
    cf_curl_easy_setopt(ws->curl, CURLOPT_RESOLVE, NULL);
    cf_curl_slist_free_all(resolve);
    return rc;
}

cf_ws *cf_ws_connect(cf_ctx *ctx, const char *url)
{
    if (!ctx || !url)
        return NULL;
    if (!cf_ws_has_websockets())
        return NULL;

    char host[256];
    uint16_t port;
    int is_https = 0, is_ws = 0;
    if (cf_parse_url(url, host, sizeof(host), &port, &is_https, &is_ws) < 0 || !is_ws)
        return NULL;

    cf_dns_entry *entry = cf_resolve_host(ctx, host, port);
    if (!entry)
        return NULL;

    cf_ws *ws = calloc(1, sizeof(cf_ws));
    if (!ws) return NULL;

    ws->ctx = ctx;
    ws->entry = entry;
    ws->curl = cf_curl_easy_init();
    if (!ws->curl) {
        free(ws);
        return NULL;
    }

    strncpy(ws->host, host, sizeof(ws->host) - 1);
    strncpy(ws->url, url, sizeof(ws->url) - 1);
    ws->port = port;
    ws->current_ip = 0;
    ws->retry_same = 0;

    if (!entry->multi_ip && entry->all_count == 1) {
        char resolve_entry[320];
        snprintf(resolve_entry, sizeof(resolve_entry), "%s:%u:%s",
                 host, port, entry->all_addrs[0]);
        struct curl_slist *sl = cf_curl_slist_append(NULL, resolve_entry);
        cf_curl_easy_setopt(ws->curl, CURLOPT_RESOLVE, sl);
        cf_curl_easy_setopt(ws->curl, CURLOPT_URL, url);
        cf_curl_easy_setopt(ws->curl, CURLOPT_CONNECT_ONLY, 2L);
        CURLcode rc = cf_curl_easy_perform(ws->curl);
        cf_curl_easy_setopt(ws->curl, CURLOPT_RESOLVE, NULL);
        cf_curl_slist_free_all(sl);
        if (rc != CURLE_OK) {
            cf_ws_close(ws);
            return NULL;
        }
        ws->connected = 1;
        return ws;
    }

    CURLcode rc = cf_ws_try_connect(ws, 0);
    if (rc != CURLE_OK && entry->rank_count > 1)
        rc = cf_ws_try_connect(ws, 1);
    if (rc != CURLE_OK && entry->rank_count > 2)
        rc = cf_ws_try_connect(ws, 2);

    if (rc != CURLE_OK) {
        cf_ws_close(ws);
        return NULL;
    }

    ws->connected = 1;
    return ws;
}

static CURLcode cf_ws_reconnect(cf_ws *ws)
{
    if (!ws->entry || !ws->entry->multi_ip)
        return CURLE_OK;

    if (!ws->retry_same) {
        ws->retry_same = 1;
        return cf_ws_try_connect(ws, ws->current_ip);
    }

    ws->retry_same = 0;
    ws->current_ip++;
    if (ws->current_ip >= ws->entry->rank_count)
        return CURLE_COULDNT_CONNECT;

    CURLcode rc = cf_ws_try_connect(ws, ws->current_ip);
    if (rc == CURLE_OK)
        ws->connected = 1;
    return rc;
}

cf_ws_result cf_ws_send(cf_ws *ws, const void *data, size_t len,
                        size_t *sent, unsigned flags)
{
    if (!ws || !ws->connected)
        return CF_WS_ERR;
    CURLcode rc = cf_curl_ws_send(ws->curl, data, len, sent, 0, flags);
    if (rc == CURLE_OK)
        return CF_WS_OK;
    if (rc == CURLE_AGAIN)
        return CF_WS_ERR;
    if (cf_ws_reconnect(ws) == CURLE_OK) {
        rc = cf_curl_ws_send(ws->curl, data, len, sent, 0, flags);
        return rc == CURLE_OK ? CF_WS_OK : CF_WS_ERR;
    }
    ws->connected = 0;
    return CF_WS_CLOSED;
}

cf_ws_result cf_ws_recv(cf_ws *ws, void *buf, size_t buflen,
                        size_t *received, const struct curl_ws_frame **meta)
{
    if (!ws || !ws->connected)
        return CF_WS_ERR;
    CURLcode rc = cf_curl_ws_recv(ws->curl, buf, buflen, received, meta);
    if (rc == CURLE_OK)
        return CF_WS_OK;
    if (rc == CURLE_AGAIN)
        return CF_WS_ERR;
    if (cf_ws_reconnect(ws) == CURLE_OK)
        return CF_WS_ERR;
    ws->connected = 0;
    return CF_WS_CLOSED;
}

void cf_ws_close(cf_ws *ws)
{
    if (!ws) return;
    if (ws->curl)
        cf_curl_easy_cleanup(ws->curl);
    free(ws);
}

CURL *cf_ws_get_curl(cf_ws *ws)
{
    return ws ? ws->curl : NULL;
}
