#!/usr/bin/env bash
# CI entry: build curl_fo for a given triple.
set -euo pipefail

TRIPLE="${1:?triple required}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-${TRIPLE}"
VERSION="1.0.0"

CMAKE_COMMON=(
    -DCMAKE_BUILD_TYPE=Release
    -DCURL_FO_BUILD_TESTS=OFF
    -DCURL_FO_BUILD_EXAMPLE=OFF
)

vcpkg_build() {
    local triplet="$1"
    shift
    vcpkg install "curl:${triplet}"
    build_cmake "${CMAKE_COMMON[@]}" \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET="${triplet}" \
        "$@"
}

build_cmake() {
    cmake -S "$ROOT" -B "$BUILD" "$@"
    cmake --build "$BUILD" --config Release --parallel
}

case "$TRIPLE" in
    x86_64-unknown-linux-gnu)
        build_cmake "${CMAKE_COMMON[@]}"
        ;;
    aarch64-unknown-linux-gnu)
        sudo dpkg --add-architecture arm64 2>/dev/null || true
        sudo apt-get update -qq
        sudo apt-get install -y -qq gcc-aarch64-linux-gnu \
            libcurl4-openssl-dev:arm64
        build_cmake "${CMAKE_COMMON[@]}" \
            -DCMAKE_SYSTEM_NAME=Linux \
            -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
            -DCMAKE_FIND_ROOT_PATH=/usr/aarch64-linux-gnu \
            -DCURL_INCLUDE_DIR=/usr/include/aarch64-linux-gnu \
            -DCURL_LIBRARY=/usr/lib/aarch64-linux-gnu/libcurl.so
        ;;
    x86_64-apple-darwin)
        brew install curl
        build_cmake "${CMAKE_COMMON[@]}" \
            -DCMAKE_OSX_ARCHITECTURES=x86_64 \
            -DCMAKE_IGNORE_PATH="/opt/homebrew" \
            -DCURL_ROOT="$(brew --prefix curl)"
        ;;
    aarch64-apple-darwin)
        brew install curl
        build_cmake "${CMAKE_COMMON[@]}" \
            -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DCURL_ROOT="$(brew --prefix curl)"
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
