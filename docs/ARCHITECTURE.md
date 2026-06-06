# curl_fo Architecture

## Component diagram

```
┌──────────────┐  ┌──────────────┐  ┌────────────────┐
│  curl-fo CLI │  │ Flutter FFI  │  │  App + cf_* API│
└──────┬───────┘  └──────┬───────┘  └───────┬────────┘
       │                 │                   │
       └─────────────────┼───────────────────┘
                         ▼
              ┌─────────────────────┐
              │   libcurl_fo.so     │
              │  cache dns probe race│
              │  http ws handle     │
              └──────────┬──────────┘
                         ▼
              ┌─────────────────────┐
              │   libcurl (dynamic) │
              └─────────────────────┘

Phase 2: libcurl_fo_shim.so  ──intercepts──► curl_easy_*
Phase 3: libcurl_fo_wrapper.so ──replaces──► -lcurl at link time
```

## DNS pipeline

1. **LRU lookup** by `host:port`
2. If miss or **TTL expired**:
   - UDP DNS query for A + AAAA (reads TTL from RRs)
   - Fallback: `getaddrinfo` + `default_ttl_sec`
3. **Single IP** → transparent (optional RESOLVE pin)
4. **Multiple IPs (cold)** → parallel TCP handshake race on first request; winner socket reused for HTTP/TLS (no extra RTT); losers RST'd; latencies seed cache ranks in background
5. **Multiple IPs (warm)** → cached rank order + `CURLOPT_RESOLVE` failover

### TTL refresh (no unnecessary probing)

```
TTL expired?
  ├─ Re-query A/AAAA only
  ├─ Top ranked IP still in new set?
  │    ├─ YES → update address list, preserve rank order, skip probe
  │    └─ NO  → full latency re-probe
  └─ Update expires_at from new min TTL
```

## Cold-path TCP race (HTTP / WebSocket)

```
multi-IP, rank_count == 0:
  cf_tcp_race(all_addrs) → winner_fd
  cf_race_drain_async()   → background RST losers + cache ranks
  CURLOPT_OPENSOCKET      → reuse winner (CONNECTTIMEOUT_MS = 0)
  libcurl TLS/HTTP on existing TCP
```

## Failover state machine (HTTP, warm path)

```
for ip in ranked_ips:
    perform with CURLOPT_RESOLVE host:port:ip
    if http_code > 0: return (success or HTTP error — no failover)
    if transport OK: return
    else: try next ip
return last_error
```

## WebSocket reconnect

```
on_disconnect:
  if not retried_same: connect(current_ip); retried_same=true
  else: current_ip++; connect(next_ip); retried_same=false
```

## Build targets

| Target | Purpose |
|--------|---------|
| `curl_fo` | Core shared library (Phase 1) |
| `curl_fo_static` | Static archive |
| `curl_fo_shim` | LD_PRELOAD (Phase 2) |
| `curl_fo_wrapper` | libcurl replacement (Phase 3) |
| `curl-fo` | CLI |
