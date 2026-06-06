#include "internal.h"

#include <stdlib.h>
#include <string.h>

struct cf_ws {
    cf_ctx               *ctx;
    CURL                 *curl;
    char                  host[256];
    uint16_t              port;
    char                  url[4096];
    cf_resolve_view       snap;
    size_t                current_ip;
    int                   retry_same;
    int                   connected;
};

static int cf_ws_has_websockets(void)
{
    curl_version_info_data *vi = cf_curl_version_info(CURLVERSION_NOW);
    return vi && vi->version_num >= 0x075600;
}

static CURLcode cf_ws_connect_ip(cf_ws *ws, const char *ip, size_t ip_index,
                                 const char *phase, int preconnected)
{
    cf_config *cfg = ws->ctx->cfg;

    cf_vlog(cfg, "WebSocket %s connect attempt #%zu to %s (rank index %zu)\n",
            phase ? phase : "initial", ip_index + 1, ip, ip_index);

    char entry[384];
    snprintf(entry, sizeof(entry), "%s:%u:%s", ws->host, ws->port, ip);

    long timeout = (long)cf_config_get_other_timeout_ms(cfg);
    long connect_to = preconnected ? 0L
        : (long)cf_config_get_connect_timeout_ms(cfg);

    cf_log_curl_replay(cfg, ws->curl, ws->host, ws->port, ip, "(ws-connect)",
                       CF_METHOD_OTHER, timeout, connect_to, 1);

    void *shadow = NULL;
    cf_curl_easy_getinfo(ws->curl, CURLINFO_PRIVATE, &shadow);
    if (preconnected && shadow) {
        cf_curl_easy_setopt(ws->curl, CURLOPT_OPENSOCKETFUNCTION,
                            cf_shadow_opensocket_cb);
        cf_curl_easy_setopt(ws->curl, CURLOPT_OPENSOCKETDATA, shadow);
        cf_curl_easy_setopt(ws->curl, CURLOPT_CLOSESOCKETFUNCTION,
                            cf_shadow_closesocket_cb);
        cf_curl_easy_setopt(ws->curl, CURLOPT_CLOSESOCKETDATA, shadow);
    }

    struct curl_slist *resolve = cf_curl_slist_append(NULL, entry);
    cf_curl_easy_setopt(ws->curl, CURLOPT_RESOLVE, resolve);
    cf_curl_easy_setopt(ws->curl, CURLOPT_URL, ws->url);
    cf_curl_easy_setopt(ws->curl, CURLOPT_CONNECT_ONLY, 2L);
    cf_curl_easy_setopt(ws->curl, CURLOPT_TIMEOUT_MS, timeout);
    cf_curl_easy_setopt(ws->curl, CURLOPT_CONNECTTIMEOUT_MS, connect_to);

    CURLcode rc = cf_curl_easy_perform(ws->curl);
    cf_curl_easy_setopt(ws->curl, CURLOPT_RESOLVE, NULL);
    if (preconnected) {
        cf_curl_easy_setopt(ws->curl, CURLOPT_OPENSOCKETFUNCTION, NULL);
        cf_curl_easy_setopt(ws->curl, CURLOPT_CLOSESOCKETFUNCTION, NULL);
    }
    cf_curl_slist_free_all(resolve);

    cf_vlog(cfg, "WebSocket connect to %s: %s\n", ip, cf_curl_easy_strerror(rc));
    if (rc == CURLE_OK)
        ws->current_ip = ip_index;
    return rc;
}

static CURLcode cf_ws_try_connect(cf_ws *ws, size_t ip_index, const char *phase)
{
    if (ip_index >= ws->snap.rank_count)
        return CURLE_COULDNT_CONNECT;
    return cf_ws_connect_ip(ws, ws->snap.ranks[ip_index], ip_index, phase, 0);
}

static CURLcode cf_ws_try_tcp_race(cf_ws *ws)
{
    cf_config *cfg = ws->ctx->cfg;
    if (!cf_config_get_tcp_race(cfg))
        return CURLE_COULDNT_CONNECT;

    char *addrs[CF_RESOLVE_MAX_IPS];
    size_t count = 0;
    for (size_t i = 0; i < ws->snap.all_count && i < CF_RESOLVE_MAX_IPS; i++) {
        if (ws->snap.all_addrs[i][0])
            addrs[count++] = ws->snap.all_addrs[i];
    }
    if (count < 2)
        return CURLE_COULDNT_CONNECT;

    cf_race_result *race = NULL;
    if (cf_tcp_race(addrs, count, ws->port, cfg, &race) < 0 || !race)
        return CURLE_COULDNT_CONNECT;

    char winner_ip[64];
    strncpy(winner_ip, cf_race_winner_addr(race), sizeof(winner_ip) - 1);
    int winner_fd = cf_race_winner_fd(race);

    cf_race_drain_async(ws->ctx, ws->host, ws->port, race);

    ws->snap.rank_count = 1;
    strncpy(ws->snap.ranks[0], winner_ip, sizeof(ws->snap.ranks[0]) - 1);

    cf_shadow_set_preconnected(ws->curl, winner_fd, winner_ip);
    return cf_ws_connect_ip(ws, winner_ip, 0, "race", 1);
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

    cf_vlog(ctx->cfg, "WebSocket connect %s\n", url);

    cf_resolve_view snap;
    if (cf_resolve_snapshot(ctx, host, port, &snap) < 0 || !snap.ok)
        return NULL;

    cf_ws *ws = calloc(1, sizeof(cf_ws));
    if (!ws)
        return NULL;

    ws->ctx = ctx;
    ws->snap = snap;
    ws->curl = cf_curl_easy_init();
    if (!ws->curl) {
        free(ws);
        return NULL;
    }
    cf_easy_attach(ws->curl);

    strncpy(ws->host, host, sizeof(ws->host) - 1);
    strncpy(ws->url, url, sizeof(ws->url) - 1);
    ws->port = port;
    ws->current_ip = 0;
    ws->retry_same = 0;

    if (!snap.multi_ip && snap.all_count == 1) {
        cf_vlog(ctx->cfg, "WebSocket single IP %s\n", snap.single_ip);
        char resolve_entry[320];
        snprintf(resolve_entry, sizeof(resolve_entry), "%s:%u:%s",
                 host, port, snap.single_ip);
        cf_log_curl_replay(ctx->cfg, ws->curl, host, port, snap.single_ip,
                           "(ws-connect)", CF_METHOD_OTHER,
                           cf_config_get_other_timeout_ms(ctx->cfg),
                           cf_config_get_connect_timeout_ms(ctx->cfg), 1);
        struct curl_slist *sl = cf_curl_slist_append(NULL, resolve_entry);
        cf_curl_easy_setopt(ws->curl, CURLOPT_RESOLVE, sl);
        cf_curl_easy_setopt(ws->curl, CURLOPT_URL, url);
        cf_curl_easy_setopt(ws->curl, CURLOPT_CONNECT_ONLY, 2L);
        CURLcode rc = cf_curl_easy_perform(ws->curl);
        cf_curl_easy_setopt(ws->curl, CURLOPT_RESOLVE, NULL);
        cf_curl_slist_free_all(sl);
        if (rc != CURLE_OK) {
            cf_vlog(ctx->cfg, "WebSocket connect failed: %s\n", cf_curl_easy_strerror(rc));
            cf_ws_close(ws);
            return NULL;
        }
        ws->connected = 1;
        cf_vlog(ctx->cfg, "WebSocket connected on %s\n", snap.single_ip);
        return ws;
    }

    CURLcode rc = CURLE_COULDNT_CONNECT;

    if (snap.rank_count == 0 && snap.all_count > 1)
        rc = cf_ws_try_tcp_race(ws);
    else {
        for (size_t i = 0; i < snap.rank_count && rc != CURLE_OK; i++)
            rc = cf_ws_try_connect(ws, i, "initial");
    }

    if (rc != CURLE_OK) {
        cf_vlog(ctx->cfg, "WebSocket: all %zu ranked IP(s) failed — giving up\n",
                snap.rank_count);
        cf_ws_close(ws);
        return NULL;
    }

    ws->connected = 1;
    cf_vlog(ctx->cfg, "WebSocket connected on %s (rank #%zu)\n",
            ws->snap.ranks[ws->current_ip], ws->current_ip + 1);
    return ws;
}

/**
 * Attempt to refresh ws->snap from the cache if the async race drain has
 * committed more ranks since the original connect.  This matters for the cold
 * path: cf_ws_try_tcp_race sets rank_count=1 (winner only) locally, while the
 * background drain later commits the full sorted rank list to the cache.  On
 * reconnect we want to see those extra IPs.
 */
static void cf_ws_refresh_snap(cf_ws *ws)
{
    /* Only worth refreshing when the local snap is incomplete (cold-race
     * wrote only the winner) and the cache may have more. */
    if (ws->snap.rank_count >= ws->snap.all_count || ws->snap.all_count < 2)
        return;

    cf_resolve_view fresh;
    if (cf_resolve_snapshot(ws->ctx, ws->host, ws->port, &fresh) != 0
            || !fresh.ok || fresh.rank_count <= ws->snap.rank_count)
        return;

    /* Copy current IP string before overwriting the snap. */
    char cur_ip[64] = {0};
    if (ws->current_ip < ws->snap.rank_count)
        strncpy(cur_ip, ws->snap.ranks[ws->current_ip], sizeof(cur_ip) - 1);

    ws->snap = fresh;

    /* Re-anchor current_ip to the same IP in the refreshed rank list. */
    ws->current_ip = 0;
    for (size_t i = 0; i < fresh.rank_count; i++) {
        if (strcmp(fresh.ranks[i], cur_ip) == 0) {
            ws->current_ip = i;
            break;
        }
    }

    cf_vlog(ws->ctx->cfg,
            "WebSocket reconnect: snap refreshed to %zu ranked IP(s)\n",
            fresh.rank_count);
}

static CURLcode cf_ws_reconnect(cf_ws *ws)
{
    if (!ws->snap.multi_ip)
        return CURLE_OK;

    cf_config *cfg = ws->ctx->cfg;

    /* Pull in any ranks the async drain committed after the initial connect. */
    cf_ws_refresh_snap(ws);

    if (!ws->retry_same) {
        ws->retry_same = 1;
        cf_vlog(cfg, "WebSocket broken — re-attempting same IP %s\n",
                ws->snap.ranks[ws->current_ip]);
        return cf_ws_try_connect(ws, ws->current_ip, "reconnect-same");
    }

    ws->retry_same = 0;
    ws->current_ip++;
    if (ws->current_ip >= ws->snap.rank_count) {
        cf_vlog(cfg, "WebSocket: no more ranked IPs after reconnect failures\n");
        return CURLE_COULDNT_CONNECT;
    }

    cf_vlog(cfg, "WebSocket broken — trying next ranked IP %s (#%zu)\n",
            ws->snap.ranks[ws->current_ip], ws->current_ip + 1);
    CURLcode rc = cf_ws_try_connect(ws, ws->current_ip, "reconnect-next");
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
    cf_vlog(ws->ctx->cfg, "WebSocket send error: %s\n", cf_curl_easy_strerror(rc));
    if (cf_ws_reconnect(ws) == CURLE_OK) {
        cf_vlog(ws->ctx->cfg, "WebSocket send retry after reconnect\n");
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
    cf_vlog(ws->ctx->cfg, "WebSocket recv error: %s\n", cf_curl_easy_strerror(rc));
    if (cf_ws_reconnect(ws) == CURLE_OK) {
        cf_vlog(ws->ctx->cfg, "WebSocket reconnected — caller should recv again\n");
        return CF_WS_ERR;
    }
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
