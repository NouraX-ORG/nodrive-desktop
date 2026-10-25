#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoDrive macOS CI packaging (used by .github/workflows/macos.yml).
#
#   admin/osx/nodrive-package.sh --app <image dir>/<Name>.app --build-dir <cmake build dir> \
#                                --out <dir> [--build-number N] [--repo <repo root>]
#
# 1. copies the built .app to a staging folder (ditto keeps framework symlinks),
# 2. adds the licence texts under Contents/Resources/licenses,
#    and makes the bundle self-contained (admin/osx/nodrive-bundle-fixup.py --fix, then --check),
# 3. signs it AD-HOC only (codesign --sign -), inside-out, without hardened runtime and without a
#    timestamp. This is not real signing: there is no certificate, no Team ID, no notarisation.
#    arm64 macOS needs at least an ad-hoc signature to run the code at all.
# 4. verifies the signature (codesign --verify --deep --strict),
# 5. writes <Name>-<version>-<build>-<arch>.dmg (UDZO, with an Applications link) and a .app.zip.
#
# Names and versions are read from the built Info.plist, so the script works for the unbranded
# and the branded tree alike. Outputs are also written to $GITHUB_OUTPUT when it is set.

set -euo pipefail

APP=""
BUILD_DIR=""
OUT=""
BUILD_NUMBER="0"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"

while [ $# -gt 0 ]; do
    case "$1" in
        --app) APP="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --build-number) BUILD_NUMBER="$2"; shift 2 ;;
        --repo) REPO="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

[ -d "$APP" ] || { echo "error: --app '$APP' is not a directory" >&2; exit 2; }
[ -d "$BUILD_DIR" ] || { echo "error: --build-dir '$BUILD_DIR' is not a directory" >&2; exit 2; }
[ -n "$OUT" ] || { echo "error: --out is required" >&2; exit 2; }
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

plist_get() { /usr/libexec/PlistBuddy -c "Print :$2" "$1/Contents/Info.plist" 2>/dev/null || true; }

NAME="$(plist_get "$APP" CFBundleName)"
EXE="$(plist_get "$APP" CFBundleExecutable)"
LONG_VERSION="$(plist_get "$APP" CFBundleLongVersionString)"
SHORT_VERSION="$(plist_get "$APP" CFBundleShortVersionString)"
[ -n "$NAME" ] && [ -n "$EXE" ] || { echo "error: cannot read CFBundleName/CFBundleExecutable from $APP" >&2; exit 1; }

# "NoDrive 35.0.0-nourax.1 (build 12)" -> "35.0.0-nourax.1"
VERSION="${LONG_VERSION#"$NAME "}"
VERSION="$(printf '%s' "$VERSION" | sed -E 's/ \(build [0-9]+\)$//')"
if [ -z "$VERSION" ] || [ "$VERSION" = "$LONG_VERSION" ]; then
    VERSION="$(printf '%s' "$SHORT_VERSION" | sed -E 's/ \(build [0-9]+\)$//')"
fi
VERSION="$(printf '%s' "$VERSION" | tr ' /' '__')"
ARCH="$(lipo -archs "$APP/Contents/MacOS/$EXE" | tr ' ' '-')"
BASENAME="$NAME-$VERSION-$BUILD_NUMBER-$ARCH"

echo "App:      $APP"
echo "Name:     $NAME (executable $EXE)"
echo "Version:  $VERSION (short '$SHORT_VERSION', long '$LONG_VERSION')"
echo "Arch:     $ARCH"
echo "Output:   $OUT/$BASENAME.{dmg,app.zip}"

STAGE_ROOT="$(mktemp -d "${RUNNER_TEMP:-/tmp}/nodrive-dmg.XXXXXX")"
STAGE="$STAGE_ROOT/$NAME"
mkdir -p "$STAGE"
STAGED_APP="$STAGE/$NAME.app"
ditto "$APP" "$STAGED_APP"

# --- licence texts ---------------------------------------------------------------------------
LIC="$STAGED_APP/Contents/Resources/licenses"
mkdir -p "$LIC"
if [ -d "$REPO/nodrive/licenses" ]; then
    # Curated folder from the shared rebrand (COPYING, needed licence texts, THIRD-PARTY-NOTICES).
    ditto "$REPO/nodrive/licenses" "$LIC"
else
    cp "$REPO/COPYING" "$LIC/COPYING"
    for f in "$REPO"/LICENSES/*.txt; do
        case "$(basename "$f")" in
            LicenseRef-*) ;;  # trademark/commercial notes, not licences of shipped code
            *) cp "$f" "$LIC/" ;;
        esac
    done
fi
echo "Licence files: $(ls "$LIC" | wc -l | tr -d ' ')"

# --- self-contained bundle ---------------------------------------------------------------------
# Our own libraries sit in Contents/MacOS and the build's macdeployqt step cannot resolve them;
# add bundle-relative run paths, drop the build machine's, then check every dependency.
echo "Making the bundle self-contained..."
python3 "$REPO/admin/osx/nodrive-bundle-fixup.py" --fix "$STAGED_APP"
python3 "$REPO/admin/osx/nodrive-bundle-fixup.py" --check "$STAGED_APP"

# --- ad-hoc signing, inside-out ----------------------------------------------------------------
xattr -cr "$STAGED_APP"

sign() {
    # $1 = path, $2 = optional entitlements file
    if [ -n "${2:-}" ]; then
        codesign --force --sign - --timestamp=none --entitlements "$2" "$1"
    else
        codesign --force --sign - --timestamp=none "$1"
    fi
}

is_bundle_main_executable() {
    # True for <bundle>/Contents/MacOS/<CFBundleExecutable> and <X>.framework/Versions/<v>/<X>;
    # those are signed together with their bundle.
    local f="$1" dir base
    dir="$(dirname "$f")"
    base="$(basename "$f")"
    if [ "$(basename "$dir")" = "MacOS" ] && [ -f "$dir/../Info.plist" ]; then
        local main
        main="$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$dir/../Info.plist" 2>/dev/null || true)"
        [ "$main" = "$base" ] && return 0
    fi
    case "$f" in
        *.framework/Versions/*/"$base")
            local fw
            fw="$(printf '%s' "$f" | sed -E 's#^.*/([^/]+)\.framework/Versions/[^/]+/[^/]+$#\1#')"
            [ "$fw" = "$base" ] && return 0 ;;
    esac
    return 1
}

entitlements_for() {
    # Entitlements generated by CMake into the build tree (same mapping as admin/osx/mac-crafter).
    local bundle="$1" name
    name="$(basename "$bundle")"
    case "$name" in
        FinderSyncExt.appex) echo "$BUILD_DIR/shell_integration/MacOSX/FinderSyncExt.entitlements" ;;
        FileProviderExt.appex) echo "$BUILD_DIR/shell_integration/MacOSX/FileProviderExt.entitlements" ;;
        FileProviderUIExt.appex) echo "$BUILD_DIR/shell_integration/MacOSX/FileProviderUIExt.entitlements" ;;
        *.app)
            case "$bundle" in
                */Contents/Library/LoginItems/*) echo "$BUILD_DIR/shell_integration/MacOSX/FinderSyncBroker.entitlements" ;;
            esac ;;
    esac
}

echo "Signing loose Mach-O files..."
count=0
while IFS= read -r -d '' f; do
    case "$(file -b "$f")" in
        *Mach-O*)
            if ! is_bundle_main_executable "$f"; then
                sign "$f"
                count=$((count + 1))
            fi ;;
    esac
done < <(find "$STAGED_APP" -type f -not -path '*.dSYM/*' -print0)
echo "  signed $count files"

echo "Signing nested bundles (deepest first)..."
while IFS= read -r -d '' b; do
    case "$b" in
        *.bundle|*.plugin) [ -d "$b/Contents/MacOS" ] || continue ;;
    esac
    ent="$(entitlements_for "$b")"
    if [ -n "$ent" ] && [ ! -f "$ent" ]; then
        echo "  warning: entitlements $ent not found, signing $(basename "$b") without them"
        ent=""
    fi
    echo "  $(basename "$b")${ent:+ (entitlements $(basename "$ent"))}"
    sign "$b" "$ent"
done < <(find "$STAGED_APP" -depth -mindepth 1 -type d \( -name '*.framework' -o -name '*.appex' -o -name '*.app' -o -name '*.xpc' -o -name '*.bundle' -o -name '*.plugin' \) -print0)

APP_ENT="$BUILD_DIR/admin/osx/macosx.entitlements"
if [ -f "$APP_ENT" ]; then
    echo "Signing $NAME.app (entitlements macosx.entitlements)"
    sign "$STAGED_APP" "$APP_ENT"
else
    echo "warning: $APP_ENT not found, signing $NAME.app without entitlements"
    sign "$STAGED_APP"
fi

echo "Verifying signature..."
codesign --verify --deep --strict --verbose=2 "$STAGED_APP"
codesign -dv "$STAGED_APP" 2>&1 | grep -E '^(Identifier|Format|Signature|TeamIdentifier|CodeDirectory)' || true

# --- archives ------------------------------------------------------------------------------------
ZIP="$OUT/$BASENAME.app.zip"
rm -f "$ZIP"
ditto -c -k --sequesterRsrc --keepParent "$STAGED_APP" "$ZIP"

ln -s /Applications "$STAGE/Applications"
DMG="$OUT/$BASENAME.dmg"
rm -f "$DMG"
ok=0
for attempt in 1 2 3; do
    if hdiutil create -volname "$NAME" -srcfolder "$STAGE" -ov -format UDZO "$DMG"; then
        ok=1
        break
    fi
    echo "hdiutil create failed (attempt $attempt), retrying..."
    sleep 15
done
[ "$ok" = 1 ] || { echo "error: hdiutil create failed" >&2; exit 1; }
hdiutil verify "$DMG"

ls -l "$DMG" "$ZIP"
shasum -a 256 "$DMG" "$ZIP"

if [ -n "${GITHUB_OUTPUT:-}" ]; then
    {
        echo "app-name=$NAME"
        echo "version=$VERSION"
        echo "arch=$ARCH"
        echo "basename=$BASENAME"
        echo "dmg=$DMG"
        echo "zip=$ZIP"
        echo "staged-app=$STAGED_APP"
    } >> "$GITHUB_OUTPUT"
fi
