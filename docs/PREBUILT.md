# Prebuilt binary distribution

## v1.0.1 downloads

**Release page:** https://github.com/idrto/curl_fo/releases/tag/v1.0.1

**Checksums:** `checksums.txt` and `manifest.json` attached to the release.

### Direct links

| Triple | Archive |
|--------|---------|
| Linux x64 | [curl_fo-1.0.1-x86_64-unknown-linux-gnu.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-x86_64-unknown-linux-gnu.tar.gz) |
| Linux arm64 | [curl_fo-1.0.1-aarch64-unknown-linux-gnu.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-aarch64-unknown-linux-gnu.tar.gz) |
| macOS arm64 | [curl_fo-1.0.1-aarch64-apple-darwin.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-aarch64-apple-darwin.tar.gz) |
| Windows x64 | [curl_fo-1.0.1-x86_64-pc-windows-msvc.zip](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-x86_64-pc-windows-msvc.zip) |
| Windows arm64 | [curl_fo-1.0.1-aarch64-pc-windows-msvc.zip](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-aarch64-pc-windows-msvc.zip) |
| Android arm64 | [curl_fo-1.0.1-aarch64-linux-android.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-aarch64-linux-android.tar.gz) |
| Android armv7 | [curl_fo-1.0.1-armv7-linux-androideabi.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-armv7-linux-androideabi.tar.gz) |
| Android x64 | [curl_fo-1.0.1-x86_64-linux-android.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-x86_64-linux-android.tar.gz) |
| iOS device | [curl_fo-1.0.1-aarch64-apple-ios.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-aarch64-apple-ios.tar.gz) |
| iOS sim arm64 | [curl_fo-1.0.1-aarch64-apple-ios-sim.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-aarch64-apple-ios-sim.tar.gz) |
| iOS sim x64 | [curl_fo-1.0.1-x86_64-apple-ios-sim.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.1/curl_fo-1.0.1-x86_64-apple-ios-sim.tar.gz) |

## v1.0.0 downloads

**Release page:** https://github.com/idrto/curl_fo/releases/tag/v1.0.0

**Checksums:** `checksums.txt` and `manifest.json` attached to the release.

### Direct links

| Triple | Archive |
|--------|---------|
| Linux x64 | [curl_fo-1.0.0-x86_64-unknown-linux-gnu.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-x86_64-unknown-linux-gnu.tar.gz) |
| Linux arm64 | [curl_fo-1.0.0-aarch64-unknown-linux-gnu.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-aarch64-unknown-linux-gnu.tar.gz) |
| macOS arm64 | [curl_fo-1.0.0-aarch64-apple-darwin.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-aarch64-apple-darwin.tar.gz) |
| Windows x64 | [curl_fo-1.0.0-x86_64-pc-windows-msvc.zip](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-x86_64-pc-windows-msvc.zip) |
| Windows arm64 | [curl_fo-1.0.0-aarch64-pc-windows-msvc.zip](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-aarch64-pc-windows-msvc.zip) |
| Android arm64 | [curl_fo-1.0.0-aarch64-linux-android.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-aarch64-linux-android.tar.gz) |
| Android armv7 | [curl_fo-1.0.0-armv7-linux-androideabi.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-armv7-linux-androideabi.tar.gz) |
| Android x64 | [curl_fo-1.0.0-x86_64-linux-android.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-x86_64-linux-android.tar.gz) |
| iOS device | [curl_fo-1.0.0-aarch64-apple-ios.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-aarch64-apple-ios.tar.gz) |
| iOS sim arm64 | [curl_fo-1.0.0-aarch64-apple-ios-sim.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-aarch64-apple-ios-sim.tar.gz) |
| iOS sim x64 | [curl_fo-1.0.0-x86_64-apple-ios-sim.tar.gz](https://github.com/idrto/curl_fo/releases/download/v1.0.0/curl_fo-1.0.0-x86_64-apple-ios-sim.tar.gz) |

## CI artifacts (main branch HEAD)

Every push to `main` also uploads the same archives as workflow artifacts:

https://github.com/idrto/curl_fo/actions/workflows/binaries.yml

## Contents

```
curl_fo-<version>-<triple>/
  include/curl_fo.h
  lib/libcurl_fo.{so,dylib,dll}
  lib/libcurl_fo_static.a
  lib/libcurl_fo_shim.*      # desktop only
  lib/libcurl_fo_wrapper.*   # desktop only
  bin/curl-fo                # desktop only
  triple.txt
  README-BINARY.txt
```

## macOS note

Intel macOS (`x86_64-apple-darwin`) is not published. Use the **aarch64-apple-darwin** build on Apple Silicon Macs, or on Intel Macs under Rosetta 2.

## Runtime notes

- **Desktop:** requires libcurl at runtime (not bundled).
- **Mobile:** static libcurl linked into `libcurl_fo`.
