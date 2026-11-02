#!/bin/bash
# Builds macos/EP-DJ3000.app: a release build of the SwiftUI app, wrapped in a bundle and ad-hoc signed.
#
#   scripts/make-app.sh              native architecture
#   UNIVERSAL=1 scripts/make-app.sh  arm64 + x86_64 in one binary
#
# Needs macOS 13+ with Xcode (or its command line tools). The result is signed with an ad-hoc identity, which is enough
# to run on the Mac that built it; there is no notarisation, so a copy sent to another Mac needs right-click > Open.
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
    echo "make-app.sh builds a macOS app and has to run on macOS." >&2
    exit 1
fi

cd "$(dirname "$0")/.."   # macos/

NAME="EP-DJ3000"          # CFBundleExecutable in Resources/Info.plist
PRODUCT="EPDJ3000App"     # the SwiftPM executable
APP="$PWD/$NAME.app"

ARCHS=""
if [ "${UNIVERSAL:-0}" = "1" ]; then
    ARCHS="--arch arm64 --arch x86_64"
fi

echo "Building $PRODUCT (release)..."
# shellcheck disable=SC2086  # ARCHS is a list of flags
swift build -c release --product "$PRODUCT" $ARCHS
# shellcheck disable=SC2086
BIN_DIR="$(swift build -c release $ARCHS --show-bin-path)"

echo "Assembling $APP..."
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN_DIR/$PRODUCT" "$APP/Contents/MacOS/$NAME"
cp Resources/Info.plist "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"
# SwiftPM resource bundles, should the targets ever get resources.
for bundle in "$BIN_DIR"/*.bundle; do
    if [ -e "$bundle" ]; then cp -R "$bundle" "$APP/Contents/Resources/"; fi
done

echo "Signing (ad hoc)..."
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

echo "Done: $APP"
echo "Run it with: open \"$APP\""
