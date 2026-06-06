# curl_fo Test Cases

Production test matrix for `curl_fo`. Automated tests live in `tests/`; manual/integration cases below.

## Automated unit tests (`curl_fo_tests`)

| ID | File | Case | Expected |
|----|------|------|----------|
| U-01 | test_util.c | Parse `https://example.com/path` | host=`example.com`, port=443 |
| U-02 | test_util.c | Parse `http://api:8080/` | host=`api`, port=8080 |
| U-03 | test_util.c | Parse `wss://ws.example/socket` | ws=1, port=443 |
| U-04 | test_util.c | Config defaults | LRU=500, top_ips=3 |
| U-05 | test_failover.c | `CURLE_COULDNT_CONNECT`, http=0 | failover=1 |
| U-06 | test_failover.c | `CURLE_OK`, http=401 | failover=0 |
| U-07 | test_failover.c | `CURLE_OK`, http=500 | failover=0 |
| U-08 | test_failover.c | `CURLE_RECV_ERROR`, http=500 | failover=0 |
| U-09 | test_probe.c | Bucket round 11ms → 20ms | bucket=20 |
| U-10 | test_probe.c | Probe localhost:80 | count≥1 if reachable |
| U-11 | test_dns.c | Resolve localhost | count≥1, ttl>0 |
| U-12 | test_cache.c | LRU capacity 3, clear | no crash |
| U-13 | test_ttl.c | Default TTL config | ttl=60 after set |

Run:

```bash
cmake -B build -DCURL_FO_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## DNS & cache integration

| ID | Case | Steps | Expected |
|----|------|-------|----------|
| D-01 | Single A record | Resolve host with 1 IP | Transparent mode, no probe |
| D-02 | Multiple A records | Resolve CDN host | Probe all, store top 3 by latency |
| D-03 | LRU eviction | Resolve 501 unique hosts, capacity=500 | Oldest evicted |
| D-04 | Cache hit | Same host twice within TTL | Second call: no DNS UDP query |
| D-05 | TTL expiry, top IP remains | Wait TTL+1s, query again | A/AAAA refreshed, **no re-probe**, ranking preserved |
| D-06 | TTL expiry, top IP removed | Remove top IP from DNS, wait TTL | Full latency re-probe |
| D-07 | TTL from DNS | Compare cached ttl vs dig TTL | min(A,AAAA) TTL used |
| D-08 | getaddrinfo fallback | Block UDP 53 | Falls back to getaddrinfo + default TTL |

---

## Latency probe

| ID | Case | Expected |
|----|------|----------|
| P-01 | 3 IPs: 5ms, 12ms, 12ms | Buckets: 10, 20, 20; tied 20ms shuffled |
| P-02 | Dead IP in set | Skipped; ranked list excludes unreachable |
| P-03 | top_ips=2 | Only 2 stored despite more addresses |
| P-04 | IPv6 AAAA | Probed on correct port |

---

## HTTP failover

| ID | Case | Expected |
|----|------|----------|
| H-01 | First IP down, second up | Request succeeds on 2nd IP |
| H-02 | All IPs down | `CURLE_COULDNT_CONNECT` (or last error) |
| H-03 | First IP returns HTTP 500 | **No** retry; returns 500 |
| H-04 | First IP returns HTTP 403 | **No** retry |
| H-05 | GET timeout | Fails faster than POST (configured ms) |
| H-06 | POST timeout | Uses higher `other_timeout_ms` |
| H-07 | Idempotency header | Same `X-Curl-FO-Id` on all IP attempts |
| H-08 | HTTPS SNI | Certificate validates against hostname, not IP |
| H-09 | HTTP→HTTPS redirect | Both :80 and :443 RESOLVE if needed |

---

## WebSocket

| ID | Case | Expected |
|----|------|----------|
| W-01 | `wss://` multi-IP connect | Lowest latency IP used |
| W-02 | First IP WS handshake fails | Tries next ranked IP |
| W-03 | Connection drop | Retry same IP once |
| W-04 | Second drop on same IP | Connect to next ranked IP |
| W-05 | libcurl without WS support | `cf_ws_connect` returns NULL |

---

## Drop-in shim (Phase 2)

| ID | Case | Expected |
|----|------|----------|
| S-01 | `LD_PRELOAD=libcurl_fo_shim.so app` | No source change, failover active |
| S-02 | `CURL_FO_TOP_IPS=5` | 5 IPs ranked |
| S-03 | Static binary | Shim not applied (documented) |

---

## Wrapper (Phase 3)

| ID | Case | Expected |
|----|------|----------|
| R-01 | Link `-lcurl_fo_wrapper` | `curl_easy_perform` intercepted |
| R-02 | `CURL_FO_LIBCURL_PATH` | Loads custom libcurl |
| R-03 | `curl_version()` | Forwards to real libcurl |

---

## Flutter FFI

| ID | Case | Expected |
|----|------|----------|
| F-01 | `CurlFoLibrary.open()` on Linux/Android | Loads `libcurl_fo.so` |
| F-02 | `cf_ctx_create_default()` + `cf_easy_perform` | Transfer completes |

---

## CLI (`curl-fo`)

| ID | Case | Command | Expected |
|----|------|---------|----------|
| C-01 | GET request | `curl-fo https://example.com` | exit 0 |
| C-02 | POST with body | `curl-fo -X POST -d '{}' URL` | uses longer timeout |
| C-03 | Custom header | `curl-fo -H 'Auth: x' URL` | header preserved + idempotency added |

---

## Performance / load

| ID | Case | Expected |
|----|------|----------|
| L-01 | 1000 concurrent hosts | LRU bounds memory |
| L-02 | Repeated same host 10k req | Cache hit, no probe storm |

---

## Security

| ID | Case | Expected |
|----|------|----------|
| X-01 | Idempotency UUID | Unique per logical request, stable across retries |
| X-02 | No TLS verification bypass | Cert checks unchanged |
