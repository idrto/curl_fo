/**
 * curl_fo — HTTP/WebSocket failover library over libcurl
 *
 * Provides DNS-aware IP failover with latency-ranked backend selection,
 * LRU caching with TTL respect, and idempotent retry headers.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CURL_FO_H
#define CURL_FO_H

#include <curl/curl.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Export macro — define CURL_FO_BUILDING when compiling the library */
#ifndef CF_EXPORT
#  ifdef _WIN32
#    ifdef CURL_FO_BUILDING
#      define CF_EXPORT __declspec(dllexport)
#    else
#      define CF_EXPORT __declspec(dllimport)
#    endif
#  else
#    define CF_EXPORT __attribute__((visibility("default")))
#  endif
#endif

/* ── Version ─────────────────────────────────────────────────────────── */

#define CURL_FO_VERSION_MAJOR 1
#define CURL_FO_VERSION_MINOR 0
#define CURL_FO_VERSION_PATCH 0
#define CURL_FO_VERSION_STRING "1.0.0"

/* ── Error codes (library-specific, beyond CURLcode) ─────────────────── */

typedef enum {
    CF_OK = 0,
    CF_ERR_NOMEM = -1,
    CF_ERR_DNS = -2,
    CF_ERR_CONFIG = -3,
    CF_ERR_WS_UNSUPPORTED = -4,
    CF_ERR_INVALID_URL = -5,
    CF_ERR_ALL_IPS_FAILED = -6,
} cf_error;

/* ── HTTP method classification for timeout selection ────────────────── */

typedef enum {
    CF_METHOD_UNKNOWN = 0,
    CF_METHOD_GET,
    CF_METHOD_HEAD,
    CF_METHOD_OTHER,
} cf_method;

/* ── Configuration ───────────────────────────────────────────────────── */

typedef struct cf_config cf_config;

/**
 * Create a configuration with library defaults.
 *
 * Defaults:
 *   lru_capacity       = 500
 *   top_ips            = 3
 *   get_timeout_ms     = 15000
 *   other_timeout_ms   = 60000
 *   connect_timeout_ms = 3000
 *   idempotency_header = "X-Curl-FO-Id"
 *   latency_bucket_ms  = 10
 *   default_ttl_sec    = 300  (used only when DNS TTL unavailable)
 */
CF_EXPORT cf_config *cf_config_create(void);
CF_EXPORT void       cf_config_destroy(cf_config *cfg);

CF_EXPORT void cf_config_set_lru_capacity(cf_config *cfg, size_t capacity);
CF_EXPORT void cf_config_set_top_ips(cf_config *cfg, size_t top_ips);
CF_EXPORT void cf_config_set_get_timeout_ms(cf_config *cfg, long ms);
CF_EXPORT void cf_config_set_other_timeout_ms(cf_config *cfg, long ms);
CF_EXPORT void cf_config_set_connect_timeout_ms(cf_config *cfg, long ms);
CF_EXPORT void cf_config_set_idempotency_header(cf_config *cfg, const char *name);
CF_EXPORT void cf_config_set_latency_bucket_ms(cf_config *cfg, unsigned bucket_ms);
CF_EXPORT void cf_config_set_default_ttl_sec(cf_config *cfg, unsigned ttl_sec);
/** Enable curl_fo activity logging to stderr (DNS, probe, failover, replay curl). */
CF_EXPORT void cf_config_set_verbose(cf_config *cfg, int on);
CF_EXPORT int  cf_config_get_verbose(const cf_config *cfg);

CF_EXPORT size_t   cf_config_get_lru_capacity(const cf_config *cfg);
CF_EXPORT size_t   cf_config_get_top_ips(const cf_config *cfg);
CF_EXPORT long     cf_config_get_get_timeout_ms(const cf_config *cfg);
CF_EXPORT long     cf_config_get_other_timeout_ms(const cf_config *cfg);
CF_EXPORT long     cf_config_get_connect_timeout_ms(const cf_config *cfg);
CF_EXPORT const char *cf_config_get_idempotency_header(const cf_config *cfg);

/* ── Context (owns shared DNS LRU cache) ─────────────────────────────── */

typedef struct cf_ctx cf_ctx;

CF_EXPORT cf_ctx *cf_ctx_create(cf_config *cfg);
/** Create context with default configuration (convenience for FFI/CLI). */
CF_EXPORT cf_ctx *cf_ctx_create_default(void);
CF_EXPORT void    cf_ctx_destroy(cf_ctx *ctx);
CF_EXPORT cf_config *cf_ctx_get_config(cf_ctx *ctx);

/**
 * Invalidate a cached domain entry (forces re-resolve on next request).
 */
CF_EXPORT void cf_ctx_invalidate(cf_ctx *ctx, const char *host, uint16_t port);

/**
 * Clear the entire DNS LRU cache.
 */
CF_EXPORT void cf_ctx_clear_cache(cf_ctx *ctx);

/* ── Failover policy helpers ─────────────────────────────────────────── */

/**
 * Returns true when the library should try the next ranked IP.
 * Never true when an HTTP response code was received (even 4xx/5xx).
 */
CF_EXPORT int cf_should_failover(CURLcode code, long http_code);

/**
 * Detect HTTP method category from a configured CURL handle.
 */
CF_EXPORT cf_method cf_detect_method(CURL *curl);

/* ── Phase 1: HTTP/HTTPS failover ────────────────────────────────────── */

/**
 * Perform an HTTP/HTTPS transfer with automatic DNS resolution, latency
 * ranking, IP failover, idempotency header injection, and method-aware
 * timeouts.
 *
 * The caller retains ownership of @p curl and may set any libcurl options
 * before calling this function.  curl_fo injects CURLOPT_RESOLVE,
 * CURLOPT_TIMEOUT_MS, and the idempotency header without removing
 * caller-supplied options.
 *
 * @param ctx   Shared context (DNS cache).
 * @param curl  Caller-owned easy handle (must have CURLOPT_URL set).
 * @return CURLE_OK on success (including HTTP 4xx/5xx — those are not
 *         transport failures).  Returns the last CURLE_* on total failure.
 */
CF_EXPORT CURLcode cf_easy_perform(cf_ctx *ctx, CURL *curl);

/**
 * Convenience: create a temporary easy handle, set URL, and perform.
 */
CF_EXPORT CURLcode cf_request(cf_ctx *ctx, const char *url, cf_method method);

/**
 * Attach per-handle shadow state (URL/method/header tracking).
 * Called automatically by shim/wrapper on curl_easy_init().
 */
CF_EXPORT void cf_easy_attach(CURL *curl);
/** Track POST body for verbose replay logging (shim/wrapper set this automatically). */
CF_EXPORT void cf_shadow_set_postfields(CURL *curl, const char *data);

/* ── Phase 1: WebSocket failover ─────────────────────────────────────── */

typedef struct cf_ws cf_ws;

typedef enum {
    CF_WS_OK = 0,
    CF_WS_ERR = -1,
    CF_WS_CLOSED = -2,
    CF_WS_UNSUPPORTED = -3,
} cf_ws_result;

/**
 * Connect a WebSocket (ws:// or wss://) using latency-ranked IP failover.
 * Requires libcurl >= 7.86 with WebSocket support enabled.
 */
CF_EXPORT cf_ws *cf_ws_connect(cf_ctx *ctx, const char *url);

CF_EXPORT cf_ws_result cf_ws_send(cf_ws *ws, const void *data, size_t len,
                                  size_t *sent, unsigned flags);
CF_EXPORT cf_ws_result cf_ws_recv(cf_ws *ws, void *buf, size_t buflen,
                                  size_t *received, const struct curl_ws_frame **meta);

/** Close and free a WebSocket connection. */
CF_EXPORT void cf_ws_close(cf_ws *ws);

/** Returns the underlying CURL handle (read-only use). */
CF_EXPORT CURL *cf_ws_get_curl(cf_ws *ws);

/* ── Phase 2/3: Shim initialisation ──────────────────────────────────── */

/**
 * Load configuration from environment variables (called automatically by
 * shim/wrapper, but may be called explicitly):
 *
 *   CURL_FO_LRU_SIZE, CURL_FO_TOP_IPS, CURL_FO_GET_TIMEOUT_MS,
 *   CURL_FO_OTHER_TIMEOUT_MS, CURL_FO_CONNECT_TIMEOUT_MS,
 *   CURL_FO_IDEMPOTENCY_HEADER, CURL_FO_LATENCY_BUCKET_MS,
 *   CURL_FO_DEFAULT_TTL_SEC, CURL_FO_LIBCURL_PATH
 */
CF_EXPORT void cf_config_load_env(cf_config *cfg);

/**
 * Global shim context used by LD_PRELOAD / libcurl wrapper layers.
 * Thread-safe lazy initialisation.
 */
CF_EXPORT cf_ctx *cf_shim_ctx(void);

#ifdef __cplusplus
}
#endif

#endif /* CURL_FO_H */
