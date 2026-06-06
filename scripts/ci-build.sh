#!/usr/bin/env bash
# CI entry: build curl_fo for a given triple.
set -euo pipefail

TRIPLE="${1:?triple required}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-${TRIPLE}"
VERSION="1.0.0"

CMAKE_COMMON=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCURL_FO_BUILD_TESTS=OFF
    -DCURL_FO_BUILD_EXAMPLE=OFF
)

build_cmake() {
    cmake -S "$ROOT" -B "$BUILD" "$@"
    cmake --build "$BUILD" --parallel
}

vcpkg_build() {
    local triplet="$1"
    shift
    local overlay="$ROOT/triplets"
    "$VCPKG_ROOT/vcpkg" install "curl:${triplet}" --overlay-triplets="$overlay"
    build_cmake "${CMAKE_COMMON[@]}" \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET="${triplet}" \
        "$@"
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
        build_cmake "${CMAKE_COMMON[@]}"
        ;;
    x86_64-apple-darwin)
        PREFIX="$(macos_curl_prefix)"
        build_cmake "${CMAKE_COMMON[@]}" \
            -DCMAKE_OSX_ARCHITECTURES=x86_64 \
            -DCMAKE_PREFIX_PATH="${PREFIX}" \
            -DCURL_ROOT="${PREFIX}"
        ;;
    aarch64-apple-darwin)
        PREFIX="$(macos_curl_prefix)"
        build_cmake "${CMAKE_COMMON[@]}" \
            -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DCMAKE_PREFIX_PATH="${PREFIX}" \
            -DCURL_ROOT="${PREFIX}"
        ;;
    x86_64-pc-windows-msvc)
        vcpkg_build x64-windows
        ;;
    aarch64-pc-windows-msvc)
        vcpkg_build arm64-windows
        ;;
    aarch64-linux-android)
        vcpkg_build arm64-android -DCURL_FO_MOBILE_BUILD=ON
        ;;
    armv7-linux-androideabi)
        vcpkg_build arm-neon-android -DCURL_FO_MOBILE_BUILD=ON
        ;;
    x86_64-linux-android)
        vcpkg_build x64-android -DCURL_FO_MOBILE_BUILD=ON
        ;;
    aarch64-apple-ios)
        vcpkg_build arm64-ios -DCURL_FO_MOBILE_BUILD=ON
        ;;
    aarch64-apple-ios-sim)
        vcpkg_build arm64-ios-simulator -DCURL_FO_MOBILE_BUILD=ON
        ;;
    x86_64-apple-ios-sim)
        vcpkg_build x64-ios-simulator -DCURL_FO_MOBILE_BUILD=ON
        ;;
    *)
        echo "Unknown triple: $TRIPLE"; exit 1 ;;
esac

bash "$ROOT/scripts/package.sh" "$BUILD" "$TRIPLE" "$VERSION"
