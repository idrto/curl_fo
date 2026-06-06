#!/usr/bin/env bash
# CI entry: build curl_fo for a given triple.
set -euo pipefail

TRIPLE="${1:?triple required}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-${TRIPLE}"
VERSION="1.0.1"
TOOLCHAIN="$ROOT/cmake/vcpkg-init.cmake"
OVERLAY="$ROOT/triplets"

unset CMAKE_TOOLCHAIN_FILE

vcpkg_root() {
    if [ -n "${VCPKG_ROOT:-}" ] && [ -d "$VCPKG_ROOT" ]; then
        cd "$VCPKG_ROOT" && pwd
    elif [ -d "$ROOT/vcpkg" ]; then
        cd "$ROOT/vcpkg" && pwd
    else
        echo "vcpkg directory not found (set VCPKG_ROOT or run Setup vcpkg)" >&2
        return 1
    fi
}

vcpkg_install() {
    local triplet="$1"
    "$VCPKG_ROOT/vcpkg" install "curl:${triplet}" --overlay-triplets="$OVERLAY"
}

CMAKE_RELEASE=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCURL_FO_BUILD_TESTS=OFF
    -DCURL_FO_BUILD_EXAMPLE=OFF
)

build_native() {
    rm -rf "$BUILD"
    cmake -S "$ROOT" -B "$BUILD" "${CMAKE_RELEASE[@]}" "$@"
    cmake --build "$BUILD" --parallel
}

build_vcpkg() {
    local triplet="$1"
    shift
    VCPKG_ROOT="$(vcpkg_root)"
    export VCPKG_ROOT
    vcpkg_install "$triplet"
    rm -rf "$BUILD"
    cmake -S "$ROOT" -B "$BUILD" "${CMAKE_RELEASE[@]}" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DVCPKG_TARGET_TRIPLET="$triplet" \
        "$@"
    cmake --build "$BUILD" --parallel
}

build_android() {
    local triplet="$1"
    local abi="$2"
    shift 2
    VCPKG_ROOT="$(vcpkg_root)"
    export VCPKG_ROOT
    vcpkg_install "$triplet"
    local prefix="$VCPKG_ROOT/installed/${triplet}"
    local ndk="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME is required}"
    rm -rf "$BUILD"
    cmake -S "$ROOT" -B "$BUILD" "${CMAKE_RELEASE[@]}" \
        -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM=android-24 \
        -DCURL_ROOT="$prefix" \
        -DCURL_FO_MOBILE_BUILD=ON \
        "$@"
    cmake --build "$BUILD" --parallel
}

build_ios() {
    local triplet="$1"
    local arch="$2"
    local sysroot="$3"
    shift 3
    VCPKG_ROOT="$(vcpkg_root)"
    export VCPKG_ROOT
    vcpkg_install "$triplet"
    local prefix="$VCPKG_ROOT/installed/${triplet}"
    rm -rf "$BUILD"
    cmake -S "$ROOT" -B "$BUILD" "${CMAKE_RELEASE[@]}" \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES="$arch" \
        -DCMAKE_OSX_SYSROOT="$sysroot" \
        -DCURL_ROOT="$prefix" \
        -DCURL_FO_MOBILE_BUILD=ON \
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
        build_android arm64-android arm64-v8a
        ;;
    armv7-linux-androideabi)
        build_android arm-neon-android armeabi-v7a
        ;;
    x86_64-linux-android)
        build_android x64-android x86_64
        ;;
    aarch64-apple-ios)
        build_ios arm64-ios arm64 iphoneos
        ;;
    aarch64-apple-ios-sim)
        build_ios arm64-ios-simulator arm64 iphonesimulator
        ;;
    x86_64-apple-ios-sim)
        build_ios x64-ios-simulator x86_64 iphonesimulator
        ;;
    *)
        echo "Unknown triple: $TRIPLE"; exit 1 ;;
esac

bash "$ROOT/scripts/package.sh" "$BUILD" "$TRIPLE" "$VERSION"
