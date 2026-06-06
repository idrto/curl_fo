#!/usr/bin/env bash
# Build static libcurl for Android or iOS into deps/install
set -euo pipefail

PLATFORM="${1:?platform: android|ios}"
ARCH="${2:?arch}"
CURL_VER="8.12.1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/deps"
SRC="$DEPS/curl-${CURL_VER}"
INSTALL="$DEPS/install"
BUILD="$DEPS/build-curl-${PLATFORM}-${ARCH}"

mkdir -p "$DEPS"
if [ ! -d "$SRC" ]; then
    curl -fsSL "https://curl.se/download/curl-${CURL_VER}.tar.gz" -o "$DEPS/curl.tar.gz"
    tar xzf "$DEPS/curl.tar.gz" -C "$DEPS"
fi

rm -rf "$BUILD"
mkdir -p "$BUILD"

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_CURL_EXE=OFF
    -DCURL_DISABLE_LDAP=ON
    -DCURL_USE_OPENSSL=ON
    -DCMAKE_INSTALL_PREFIX="$INSTALL"
)

case "$PLATFORM" in
    android)
        : "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME required}"
        case "$ARCH" in
            aarch64-linux-android)    ABI=arm64-v8a ;;
            armv7-linux-androideabi)  ABI=armeabi-v7a ;;
            x86_64-linux-android)     ABI=x86_64 ;;
            *) echo "unknown android arch: $ARCH"; exit 1 ;;
        esac
        CMAKE_ARGS+=(
            -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
            -DANDROID_ABI="$ABI"
            -DANDROID_PLATFORM=android-24
            -DANDROID_NDK="$ANDROID_NDK_HOME"
        )
        ;;
    ios)
        case "$ARCH" in
            aarch64-apple-ios)     SYS=iOS; OSX_ARCH=arm64; SIM=OFF ;;
            aarch64-apple-ios-sim) SYS=iOS; OSX_ARCH=arm64; SIM=ON ;;
            x86_64-apple-ios-sim)  SYS=iOS; OSX_ARCH=x86_64; SIM=ON ;;
            *) echo "unknown ios arch: $ARCH"; exit 1 ;;
        esac
        CMAKE_ARGS+=(
            -DCMAKE_SYSTEM_NAME=iOS
            -DCMAKE_OSX_ARCHITECTURES="$OSX_ARCH"
            -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
            -DCMAKE_OSX_SYSROOT=iphoneos
        )
        if [ "$SIM" = "ON" ]; then
            CMAKE_ARGS+=(-DCMAKE_OSX_SYSROOT=iphonesimulator)
        fi
        ;;
    *)
        echo "unknown platform: $PLATFORM"; exit 1 ;;
esac

cmake -S "$SRC" -B "$BUILD" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD" --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
cmake --install "$BUILD"

echo "libcurl installed to $INSTALL"
