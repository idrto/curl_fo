# curl_fo

**HTTP and WebSocket failover for libcurl** — lightweight DNS-aware client-side load balancing with latency-ranked IP selection, TTL-respecting LRU cache, and transport-only retry semantics.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Features

| Feature | Description |
|---------|-------------|
| **Smart DNS** | Resolves A/AAAA via DNS UDP; respects **TTL from records** |
| **LRU cache** | 500 domains by default (configurable) |
| **Transparent mode** | Single IP → passes through to libcurl (optional RESOLVE pin) |
| **Latency ranking** | Multi-IP → TCP connect probe, round to 10ms buckets, random tie-break |
| **Top-N IPs** | Stores 3 lowest-latency IPs by default |
| **HTTP failover** | Tries next IP only on **transport failure** (not 401/403/500) |
| **Idempotency header** | `X-Curl-FO-Id` (configurable) — same UUID across retries |
| **Method timeouts** | Shorter for GET/HEAD, longer for POST/PUT/… |
| **WebSocket** | `ws://` / `wss://` with connect + reconnect failover |
| **Cross-platform** | Linux, Windows, macOS |
| **Flutter FFI** | Dart bindings in `ffi/` |
| **Three integration modes** | Explicit API · LD_PRELOAD shim · libcurl wrapper |

## Quick start

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build   # optional
```

**Requirements:** CMake 3.16+, C11 compiler, libcurl development package.

| Platform | libcurl package |
|----------|-----------------|
| Ubuntu/Debian | `libcurl4-openssl-dev` |
| Fedora | `libcurl-devel` |
| macOS | `brew install curl` |
| Windows | vcpkg `curl` or official curl SDK |

### Phase 1 — Explicit API (recommended)

```c
#include <curl/curl.h>
#include <curl_fo.h>

int main(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    cf_ctx *ctx = cf_ctx_create_default();

    CURL *curl = curl_easy_init();
    cf_easy_attach(curl);
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.example.com/v1/users");

    CURLcode rc = cf_easy_perform(ctx, curl);

    curl_easy_cleanup(curl);
    cf_ctx_destroy(ctx);
    curl_global_cleanup();
    return (int)rc;
}
```

Compile:

```bash
gcc -o app app.c -lcurl_fo -lcurl
```

### CLI

```bash
./build/curl-fo https://example.com
./build/curl-fo -X POST -d '{"k":"v"}' -H 'Content-Type: application/json' https://api.example.com
```

### Phase 2 — LD_PRELOAD (zero source changes)

For apps that **dynamically link** libcurl:

```bash
# Linux / macOS
LD_PRELOAD=./build/libcurl_fo_shim.so ./your_existing_app

# Environment configuration
export CURL_FO_TOP_IPS=3
export CURL_FO_LRU_SIZE=500
export CURL_FO_GET_TIMEOUT_MS=15000
```

### Phase 3 — libcurl wrapper (link-time replacement)

Link against the wrapper instead of libcurl (no source changes):

```bash
gcc -o app app.c -lcurl_fo_wrapper -lcurl_fo
# Real libcurl loaded at runtime (default: libcurl.so.4 / libcurl.dll)
export CURL_FO_LIBCURL_PATH=/usr/lib/x86_64-linux-gnu/libcurl.so.4
```

## Use cases

| Scenario | How curl_fo helps |
|----------|-------------------|
| **CDN / anycast backends** | Multiple A records → pick lowest-latency edge |
| **Multi-region API** | Fail over on network partition without app changes |
| **Mobile / Flutter apps** | FFI bindings; resilient HTTP on flaky networks |
| **Microservices sidecars** | LD_PRELOAD shim for legacy libcurl binaries |
| **IoT / CLI tools** | `curl-fo` drop-in with failover |
| **WebSocket clients** | Ranked `wss://` connect + auto-reconnect |
| **Idempotent retries** | Dedup header lets servers collapse duplicate attempts |

## DNS TTL behaviour

```
First resolve  → query DNS, read TTL, probe if multi-IP
Within TTL     → serve from LRU cache (no DNS, no probe)
TTL expired    → refresh A/AAAA only
                 ├─ top IP still present? keep ranking (no probe)
                 └─ top IP gone? re-run latency probe
```

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `CURL_FO_LRU_SIZE` | 500 | Max cached domains |
| `CURL_FO_TOP_IPS` | 3 | Ranked IPs for failover |
| `CURL_FO_GET_TIMEOUT_MS` | 15000 | GET/HEAD total timeout |
| `CURL_FO_OTHER_TIMEOUT_MS` | 60000 | Other methods timeout |
| `CURL_FO_CONNECT_TIMEOUT_MS` | 3000 | Per-attempt connect timeout |
| `CURL_FO_IDEMPOTENCY_HEADER` | X-Curl-FO-Id | Dedup header name |
| `CURL_FO_LATENCY_BUCKET_MS` | 10 | Latency round-up bucket |
| `CURL_FO_DEFAULT_TTL_SEC` | 300 | Fallback TTL if DNS UDP fails |
| `CURL_FO_LIBCURL_PATH` | (system) | Real libcurl for shim/wrapper |

Programmatic API: see [docs/API.md](docs/API.md).

## Flutter integration

```dart
import 'ffi/curl_fo_bindings.dart';

final fo = CurlFoLibrary.open();
final ctx = fo.ctxCreate();
// Use with libcurl Dart bindings: fo.easyPerform(ctx, curlHandle);
fo.ctxDestroy(ctx);
```

Ship `libcurl_fo.so` / `.dll` / `.dylib` in your app bundle.

## Testing

```bash
cmake -B build -DCURL_FO_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Full matrix: [docs/TEST_CASES.md](docs/TEST_CASES.md).

## Documentation

| Document | Contents |
|----------|----------|
| [docs/API.md](docs/API.md) | Public API reference |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Internals & diagrams |
| [docs/TEST_CASES.md](docs/TEST_CASES.md) | Production test matrix |

## Limitations

- HTTP **proxy** disables `CURLOPT_RESOLVE` (libcurl limitation) — failover pinning skipped
- **Statically linked** libcurl binaries cannot use LD_PRELOAD — use Phase 1 or 3
- Latency uses **TCP connect** time, not ICMP ping
- WebSocket requires libcurl **≥ 7.86** built with WebSocket support

## License

MIT — see [LICENSE](LICENSE).
