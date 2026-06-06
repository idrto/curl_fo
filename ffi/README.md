# curl_fo Flutter FFI

## Prebuilt binaries (v1.0.0)

Download from [GitHub Releases](https://github.com/idrto/curl_fo/releases/tag/v1.0.0) or latest [workflow artifacts](https://github.com/idrto/curl_fo/actions/workflows/binaries.yml).

### Triple → Flutter platform

| Triple | Flutter target |
|--------|----------------|
| `x86_64-unknown-linux-gnu` | Linux desktop x64 |
| `aarch64-unknown-linux-gnu` | Linux desktop arm64 |
| `aarch64-apple-darwin` | macOS desktop arm64 |
| `x86_64-pc-windows-msvc` | Windows desktop x64 |
| `aarch64-pc-windows-msvc` | Windows desktop arm64 |
| `aarch64-linux-android` | Android arm64-v8a |
| `armv7-linux-androideabi` | Android armeabi-v7a |
| `x86_64-linux-android` | Android x86_64 |
| `aarch64-apple-ios` | iOS device |
| `aarch64-apple-ios-sim` | iOS simulator (Apple Silicon) |
| `x86_64-apple-ios-sim` | iOS simulator (Intel) |

### Download URL pattern

```
https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-<triple>.tar.gz
https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-<triple>.zip   # Windows
```

Verify with `checksums.txt` from the release.

### Usage

See [`curl_fo_bindings.dart`](curl_fo_bindings.dart).
