#!/usr/bin/env bash
# CI entry: build curl_fo for a given triple.
set -euo pipefail

TRIPLE="${1:?triple required}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-${TRIPLE}"
VERSION="1.0.0"
VCPKG_ROOT="$(cd "${VCPKG_ROOT:-$ROOT/vcpkg}" && pwd)"
TOOLCHAIN="$ROOT/cmake/vcpkg-init.cmake"
OVERLAY="$ROOT/triplets"

export VCPKG_ROOT
unset CMAKE_TOOLCHAIN_FILE

CMAKE_NATIVE=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCURL_FO_BUILD_TESTS=OFF
    -DCURL_FO_BUILD_EXAMPLE=OFF
)

build_native() {
    rm -rf "$BUILD"
    cmake -S "$ROOT" -B "$BUILD" "${CMAKE_NATIVE[@]}" "$@"
    cmake --build "$BUILD" --parallel
}

build_vcpkg() {
    local triplet="$1"
    shift
    [ -f "$TOOLCHAIN" ] || { echo "Missing toolchain: $TOOLCHAIN"; exit 1; }
    rm -rf "$BUILD"
    cmake -S "$ROOT" -B "$BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DVCPKG_TARGET_TRIPLET="$triplet" \
        -DVCPKG_OVERLAY_TRIPLETS="$OVERLAY" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCURL_FO_BUILD_TESTS=OFF \
        -DCURL_FO_BUILD_EXAMPLE=OFF \
        "$@"
    cmake --build "$BUILD" --parallel
}

macos_curl_prefix() {
    if [ -d /opt/homebrew/opt/curl ]; then
        echo /opt/homebrew/opt/curl
    else
        brew --prefix curl
    fi
}

case "$TRIPLE" in
    x86_64-unknown-linux-gnu|aarch64-unknown-linux-gnu)
        build_native
        ;;
    x86_64-apple-darwin)
        PREFIX="$(macos_curl_prefix)"
        build_native \
            -DCMAKE_OSX_ARCHITECTURES=x86_64 \
            -DCMAKE_PREFIX_PATH="${PREFIX}" \
            -DCURL_ROOT="${PREFIX}"
        ;;
    aarch64-apple-darwin)
        PREFIX="$(macos_curl_prefix)"
        build_native \
            -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DCMAKE_PREFIX_PATH="${PREFIX}" \
            -DCURL_ROOT="${PREFIX}"
        ;;
    x86_64-pc-windows-msvc)
        build_vcpkg x64-windows
        ;;
    aarch64-pc-windows-msvc)
        build_vcpkg arm64-windows
        ;;
    aarch64-linux-android)
        build_vcpkg arm64-android -DCURL_FO_MOBILE_BUILD=ON
        ;;
    armv7-linux-androideabi)
        build_vcpkg arm-neon-android -DCURL_FO_MOBILE_BUILD=ON
        ;;
    x86_64-linux-android)
        build_vcpkg x64-android -DCURL_FO_MOBILE_BUILD=ON
        ;;
    aarch64-apple-ios)
        build_vcpkg arm64-ios -DCURL_FO_MOBILE_BUILD=ON
        ;;
    aarch64-apple-ios-sim)
        build_vcpkg arm64-ios-simulator -DCURL_FO_MOBILE_BUILD=ON
        ;;
    x86_64-apple-ios-sim)
        build_vcpkg x64-ios-simulator -DCURL_FO_MOBILE_BUILD=ON
        ;;
    *)
        echo "Unknown triple: $TRIPLE"; exit 1 ;;
esac

bash "$ROOT/scripts/package.sh" "$BUILD" "$TRIPLE" "$VERSION"
