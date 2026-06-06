# curl_fo Public API Reference

## Overview

`curl_fo` is a lightweight C library that wraps **dynamically linked libcurl** and adds:

- DNS resolution with **TTL-aware LRU caching**
- **TCP-connect latency ranking** when multiple A/AAAA records exist
- **Sequential IP failover** for transport failures only
- **Idempotency request headers** for safe retries
- **Method-aware timeouts** (shorter for GET/HEAD)
- **WebSocket** connect failover and reconnect policy

Include:

```c
#include <curl_fo.h>
```

Link: `-lcurl_fo -lcurl`

---

## Version

| Macro | Value |
|-------|-------|
| `CURL_FO_VERSION_STRING` | `"1.0.0"` |

---

## Configuration (`cf_config`)

### `cf_config *cf_config_create(void)`

Allocates configuration with defaults:

| Setting | Default |
|---------|---------|
| LRU capacity | 500 domains |
| Top ranked IPs | 3 |
| GET/HEAD timeout | 15000 ms |
| Other methods timeout | 60000 ms |
| Connect timeout | 3000 ms |
| Idempotency header | `X-Curl-FO-Id` |
| Latency bucket | 10 ms |
| Fallback DNS TTL | 300 s |

### Setters / getters

```c
void cf_config_set_lru_capacity(cf_config *, size_t);
void cf_config_set_top_ips(cf_config *, size_t);
void cf_config_set_get_timeout_ms(cf_config *, long);
void cf_config_set_other_timeout_ms(cf_config *, long);
void cf_config_set_connect_timeout_ms(cf_config *, long);
void cf_config_set_idempotency_header(cf_config *, const char *name);
void cf_config_set_latency_bucket_ms(cf_config *, unsigned);
void cf_config_set_default_ttl_sec(cf_config *, unsigned);
```

### Environment variables

```c
void cf_config_load_env(cf_config *);
```

| Variable | Maps to |
|----------|---------|
| `CURL_FO_LRU_SIZE` | LRU capacity |
| `CURL_FO_TOP_IPS` | Top ranked IPs |
| `CURL_FO_GET_TIMEOUT_MS` | GET timeout |
| `CURL_FO_OTHER_TIMEOUT_MS` | Other timeout |
| `CURL_FO_CONNECT_TIMEOUT_MS` | Connect timeout |
| `CURL_FO_IDEMPOTENCY_HEADER` | Header name |
| `CURL_FO_LATENCY_BUCKET_MS` | Latency bucket |
| `CURL_FO_DEFAULT_TTL_SEC` | Fallback TTL |
| `CURL_FO_LIBCURL_PATH` | Real libcurl path (shim/wrapper) |

---

## Context (`cf_ctx`)

Owns the shared DNS LRU cache. **Takes ownership of `cf_config`.**

```c
cf_ctx *cf_ctx_create(cf_config *cfg);
cf_ctx *cf_ctx_create_default(void);  /* defaults + env */
void    cf_ctx_destroy(cf_ctx *ctx);
cf_config *cf_ctx_get_config(cf_ctx *ctx);
void    cf_ctx_invalidate(cf_ctx *, const char *host, uint16_t port);
void    cf_ctx_clear_cache(cf_ctx *);
```

---

## HTTP failover

### `CURLcode cf_easy_perform(cf_ctx *ctx, CURL *curl)`

Primary integration point. Caller owns `CURL *` and sets libcurl options as usual.

**curl_fo automatically:**

1. Parses hostname from tracked URL (`cf_easy_attach` / shim)
2. Resolves DNS (cache → UDP DNS with TTL → `getaddrinfo` fallback)
3. If **one IP**: transparent mode, optional `CURLOPT_RESOLVE` pin
4. If **multiple IPs**: probes TCP latency, ranks top N
5. Adds idempotency header (same UUID across retries)
6. Sets method-aware timeouts
7. Tries ranked IPs sequentially on **transport failure only**

```c
curl_global_init(CURL_GLOBAL_DEFAULT);
cf_ctx *ctx = cf_ctx_create_default();

CURL *curl = curl_easy_init();
cf_easy_attach(curl);
curl_easy_setopt(curl, CURLOPT_URL, "https://api.example.com/data");

CURLcode rc = cf_easy_perform(ctx, curl);
long code = 0;
curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

curl_easy_cleanup(curl);
cf_ctx_destroy(ctx);
curl_global_cleanup();
```

### `CURLcode cf_request(cf_ctx *, const char *url, cf_method method)`

Convenience one-shot request.

---

## Failover policy

### `int cf_should_failover(CURLcode code, long http_code)`

| Condition | Failover? |
|-----------|-----------|
| `http_code > 0` (any HTTP response) | **No** |
| `CURLE_COULDNT_CONNECT`, timeout, TLS handshake failure, etc. | **Yes** |

### `cf_method cf_detect_method(CURL *curl)`

Returns `CF_METHOD_GET`, `CF_METHOD_HEAD`, `CF_METHOD_OTHER`, or `CF_METHOD_UNKNOWN`.

---

## DNS TTL behaviour

1. Initial resolve stores **minimum TTL** from DNS A/AAAA responses.
2. While TTL valid → serve from LRU cache (no DNS query).
3. On TTL expiry → **refresh A/AAAA only** (no latency probe) if the **current top-ranked IP** is still present.
4. If top IP removed from DNS → **full latency re-probe** on new address set.

---

## WebSocket API

Requires libcurl ≥ 7.86 with WebSockets enabled.

```c
cf_ws *cf_ws_connect(cf_ctx *, const char *wss_url);
cf_ws_result cf_ws_send(cf_ws *, const void *, size_t, size_t *, unsigned flags);
cf_ws_result cf_ws_recv(cf_ws *, void *, size_t, const struct curl_ws_frame **);
void cf_ws_close(cf_ws *);
CURL *cf_ws_get_curl(cf_ws *);
```

**Reconnect policy on disconnect:**

1. Retry **same IP** once
2. Then try **next ranked IP**
3. Repeat until exhausted

---

## Drop-in integration

### Phase 1 — Explicit API

Replace `curl_easy_perform(curl)` → `cf_easy_perform(ctx, curl)`.

### Phase 2 — LD_PRELOAD shim (`libcurl_fo_shim`)

```bash
LD_PRELOAD=./libcurl_fo_shim.so ./your_app
```

Zero source changes for dynamically linked libcurl apps.

### Phase 3 — libcurl wrapper (`libcurl_fo_wrapper`)

Link `-lcurl_fo_wrapper` instead of `-lcurl`. Forwards un-hooked symbols to real libcurl at runtime.

---

## Thread safety

- DNS LRU cache: mutex-protected
- `cf_ctx`: safe for concurrent `cf_easy_perform` from multiple threads
- Per-handle shadow state: tied to `CURL *` (do not share handles across threads without libcurl rules)

---

## Limitations

- `CURLOPT_RESOLVE` is **ignored when HTTP proxy is configured** (libcurl behaviour)
- ICMP ping is **not** used; latency is **TCP connect** time
- Shim `curl_easy_setopt` uses pointer-sized forwarding for unknown options (see docs/ARCHITECTURE.md)
