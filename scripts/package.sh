#!/usr/bin/env bash
# Package installed curl_fo artifacts for a triple.
set -euo pipefail

BUILD_DIR="${1:?build dir}"
TRIPLE="${2:?triple}"
VERSION="${3:-1.0.1}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"
STAGING="$DIST/staging-${TRIPLE}"
ARCHIVE_NAME="curl_fo-${VERSION}-${TRIPLE}"

rm -rf "$STAGING"
mkdir -p "$STAGING" "$DIST"
touch "$DIST/checksums.txt" 2>/dev/null || true

cmake --install "$BUILD_DIR" --prefix "$STAGING"

echo "$TRIPLE" > "$STAGING/triple.txt"
cat > "$STAGING/README-BINARY.txt" <<EOF
curl_fo ${VERSION} prebuilt for ${TRIPLE}

Contents:
  include/curl_fo.h   - public API header
  lib/                - libraries (curl_fo core + optional shim/wrapper)
  bin/                - curl-fo CLI (desktop targets only)

Runtime dependency:
  Desktop builds link against system libcurl dynamically.
  Mobile builds (android/ios) bundle static libcurl.

https://github.com/idrto/curl_fo
EOF

cd "$DIST"
if [[ "$TRIPLE" == *windows* ]]; then
    ARCHIVE="${ARCHIVE_NAME}.zip"
    rm -f "$ARCHIVE"
    (cd "$STAGING" && zip -r "../$ARCHIVE" .)
else
    ARCHIVE="${ARCHIVE_NAME}.tar.gz"
    rm -f "$ARCHIVE"
    tar czf "$ARCHIVE" -C "$STAGING" .
fi

if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$ARCHIVE" >> checksums.txt
elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$ARCHIVE" >> checksums.txt
fi

echo "Packaged: $DIST/$ARCHIVE"
