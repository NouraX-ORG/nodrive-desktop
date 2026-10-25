#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoDrive macOS smoke test (macOS lane). Runs in CI (macos-latest), not on a developer Mac.
#
#   tools/screenshots/macos.sh <NoDrive-*.dmg> <output dir>
#
# 1. Mounts the .dmg, copies the ad-hoc signed NoDrive.app to /Applications and checks the
#    bundle: ids, names and versions in every Info.plist, entitlements, signature, icon, licences.
# 2. Starts the app (classic sync build) and takes screenshots of every screen that is
#    reachable without an account, in four variants: en-light, de-light, ar-light (Arabic,
#    right to left), en-dark. Per variant there are two runs:
#      screens: 00 whole screen, 01 wizard, 02 menu bar icon, 03 proxy settings dialog
#      login:   04 after clicking "Log in". A test-only library (admin/osx/ci/openurl-logger.m)
#               records the URL the app hands to the browser; no browser is started.
#    Without an account the menu bar icon only brings the wizard back (ownCloudGui::slotTrayClicked),
#    so there is no tray menu to capture on macOS.
# 3. Network: tcpdump logs every DNS query on the runner, lsof samples the app's own connections,
#    and tools/screenshots/netcheck.py fails the test if a Nextcloud host shows up.
#
# Output: <out>/screens/<variant>/NN-<screen>.png, <out>/net/, <out>/logs/, <out>/bundle/,
#         <out>/results.tsv (check, PASS|FAIL|INFO, evidence)
#
# Needs: Xcode command line tools (swiftc, clang), Python 3 with Pillow, sudo for tcpdump.
# The runner image grants screen recording, accessibility and Apple Events to the CI shell.

set -uo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
HERE="$REPO/tools/screenshots"
CI="$REPO/admin/osx/ci"
PRIMARY="#2563EB"

log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*" >&2; }
result() { printf '%s\t%s\t%s\n' "$1" "$2" "$3" >> "$OUT/results.tsv"; log "RESULT $1 $2 $3"; }
pl() { /usr/libexec/PlistBuddy -c "Print :$2" "$1" 2> /dev/null; }

# with_timeout <seconds> <command...>: macOS has no timeout(1).
with_timeout() {
    local t=$1 p k rc
    shift
    "$@" &
    p=$!
    ( sleep "$t"; kill -KILL "$p" 2> /dev/null ) &
    k=$!
    wait "$p"
    rc=$?
    kill "$k" 2> /dev/null
    wait "$k" 2> /dev/null
    return $rc
}

# "<id> <x> <y> <w> <h>" of the biggest normal window (layer 0, at least 300 pt wide) of a pid.
main_window() {
    "$UI" windows "$1" 2> /dev/null | awk '$2 == 0 && $5 >= 300 && $6 >= 150 { a = $5 * $6; if (a > best) { best = a; line = $1 " " $3 " " $4 " " $5 " " $6 } } END { if (line) print line }'
}

# Windows of a pid other than window $2, biggest first: "<id> <layer> <x> <y> <w> <h>"
other_windows() {
    "$UI" windows "$1" 2> /dev/null | awk -v skip="$2" '$1 != skip && $5 >= 120 && $6 >= 60 { print $5 * $6, $1, $2, $3, $4, $5, $6 }' | sort -rn | cut -d' ' -f2-
}

# capture_window <window id> <x> <y> <w> <h> <file>: window capture, whole-screen region as fallback.
capture_window() {
    local id=$1 x=$2 y=$3 w=$4 h=$5 file=$6 info
    mkdir -p "$(dirname "$file")"
    screencapture -x -o -l "$id" "$file" 2>> "$OUT/logs/screencapture.txt"
    info=$(python3 "$CI/shotcheck.py" info "$file" 2> /dev/null || echo "0 0 0 1")
    if [ "${info##* }" = 1 ]; then
        log "window capture of $id is blank ($info), using a screen region instead"
        screencapture -x -R "$x,$y,$w,$h" "$file" 2>> "$OUT/logs/screencapture.txt"
        info=$(python3 "$CI/shotcheck.py" info "$file" 2> /dev/null || echo "0 0 0 1")
    fi
    log "screenshot ${file#"$OUT"/} ($info)"
    echo "$info"
}

set_scheme() { # set_scheme light|dark
    local dark=false
    [ "$1" = dark ] && dark=true
    if ! osascript -e "tell application \"System Events\" to tell appearance preferences to set dark mode to $dark" \
        >> "$OUT/logs/appearance.txt" 2>&1; then
        log "System Events could not switch the appearance; writing AppleInterfaceStyle instead"
        if [ "$dark" = true ]; then defaults write -g AppleInterfaceStyle -string Dark
        else defaults delete -g AppleInterfaceStyle 2> /dev/null; fi
    fi
    echo "$1: AppleInterfaceStyle=$(defaults read -g AppleInterfaceStyle 2> /dev/null || echo Light)" >> "$OUT/logs/appearance.txt"
    sleep 2
}

stop_app() { # stop_app <pid> <tag>
    local pid=$1 i
    kill -TERM "$pid" 2> /dev/null
    for ((i = 0; i < 30; i++)); do kill -0 "$pid" 2> /dev/null || break; sleep 0.5; done
    kill -KILL "$pid" 2> /dev/null
    wait "$pid" 2> /dev/null
    pkill -KILL -f "$EXE" 2> /dev/null
    sleep 1
}

# ---------------------------------------------------------------------------------------------
# One app run
# ---------------------------------------------------------------------------------------------
app_run() { # app_run <variant> <screens|login>
    local variant=$1 run=$2 lang scheme
    case "$variant" in
        en-light) lang=en; scheme=light ;;
        de-light) lang=de; scheme=light ;;
        ar-light) lang=ar; scheme=light ;;
        en-dark) lang=en; scheme=dark ;;
        *) log "unknown variant $variant"; return 2 ;;
    esac
    local tag="$variant-$run" dir="$OUT/screens/$variant"
    mkdir -p "$dir"
    set_scheme "$scheme"

    # --set-language stores the language in the config file and exits (src/gui/application.cpp).
    with_timeout 60 "$EXE" --set-language "$lang" > "$OUT/logs/set-language-$tag.txt" 2>&1

    log "start $tag (language $lang, $scheme)"
    DYLD_INSERT_LIBRARIES="$LOGGER" "$EXE" --logdebug --logflush --logfile - > "$OUT/logs/app-$tag.log" 2>&1 &
    local pid=$! i w=""
    ( while kill -0 "$pid" 2> /dev/null; do
          { date +%H:%M:%S; lsof -nP -a -p "$pid" -i 2> /dev/null | tail -n +2; } >> "$OUT/net/lsof-$tag.txt"
          sleep 1
      done ) &
    local sampler=$!

    for ((i = 0; i < 240; i++)); do
        w=$(main_window "$pid")
        [ -n "$w" ] && break
        kill -0 "$pid" 2> /dev/null || break
        sleep 0.5
    done
    if [ -z "$w" ]; then
        "$UI" windows "$pid" > "$OUT/logs/windows-$tag.txt" 2>&1
        screencapture -x "$dir/00-no-wizard-$run.png"
        result "launch-$tag" FAIL "no NoDrive window within 120 s (logs/app-$tag.log)"
        stop_app "$pid" "$tag"; kill "$sampler" 2> /dev/null
        return 1
    fi
    sleep 6 # QML first frame, menu bar icon, system prompts
    "$UI" activate "$pid" > /dev/null 2>&1
    "$UI" move 5 $((SCREEN_H / 2))
    sleep 1
    local id x y ww wh
    read -r id x y ww wh <<< "$(main_window "$pid")"
    "$UI" windows "$pid" > "$OUT/logs/windows-$tag.txt" 2>&1
    result "launch-$tag" PASS "wizard window ${ww}x${wh} pt at $x,$y"
    if grep -q 'NODRIVE_OPEN_URL_LOGGER installed' "$OUT/logs/app-$tag.log"; then LOGGER_OK=1; fi

    local pre="$WORK/pre-$tag.png" bg btn
    capture_window "$id" "$x" "$y" "$ww" "$wh" "$pre" > /dev/null
    bg=$(python3 "$CI/shotcheck.py" bg "$pre")
    result "background-$tag" INFO "wizard background ${bg%% *} (luminance $(echo "$bg" | cut -d' ' -f2), $(echo "$bg" | cut -d' ' -f3) % of the window)"
    if [ "$scheme" = dark ] && [ "$run" = screens ]; then
        if (( $(echo "$bg" | cut -d' ' -f2) < 90 )); then result "dark-mode" PASS "wizard background ${bg%% *} is dark"
        else result "dark-mode" FAIL "wizard background ${bg%% *} is not dark"; fi
    fi
    if btn=$(python3 "$CI/shotcheck.py" primary "$pre" "$PRIMARY" 24); then
        local bcx bcy bn bbx bby bbw bbh bcol
        read -r bcx bcy bn bbx bby bbw bbh bcol <<< "$btn"
        result "colour-$tag" PASS "button in $PRIMARY (measured $bcol after sRGB conversion): ${bbw}x${bbh} px, $bn px"
    else
        result "colour-$tag" FAIL "no $PRIMARY button in the wizard"
        btn=""
    fi

    if [ "$run" = screens ]; then
        screencapture -x "$dir/00-screen.png"
        cp "$pre" "$dir/01-wizard.png"
        log "screenshot screens/$variant/01-wizard.png"

        # Menu bar icon (status item): crop the menu bar around it, then click it.
        local si sx sy sw sh src
        if si=$("$UI" statusitem "$pid" 2>> "$OUT/logs/statusitem.txt"); then
            read -r sx sy sw sh src <<< "$si"
            python3 "$CI/shotcheck.py" crop "$dir/00-screen.png" \
                "$(( (sx - 160) * SCALE ))" 0 "$(( (sw + 320) * SCALE ))" "$(( (sy + sh + 8) * SCALE ))" "$dir/02-menu-bar.png"
            result "menu-bar-icon-$tag" PASS "menu bar item ${sw}x${sh} pt at $sx,$sy (found by $src)"
            if [ "$variant" = en-light ]; then
                "$UI" click $((sx + sw / 2)) $((sy + sh / 2))
                sleep 3
                "$UI" windows "$pid" > "$OUT/logs/windows-after-icon-click.txt" 2>&1
                screencapture -x "$dir/02-after-icon-click.png"
                if [ -n "$(main_window "$pid")" ]; then
                    result "menu-bar-click" PASS "clicking the menu bar icon without an account keeps the wizard open (no tray menu before sign-in)"
                else
                    result "menu-bar-click" INFO "no wizard window after clicking the menu bar icon (see screens/en-light/02-after-icon-click.png)"
                fi
                "$UI" activate "$pid" > /dev/null 2>&1
                "$UI" move 5 $((SCREEN_H / 2))
            fi
        else
            result "menu-bar-icon-$tag" FAIL "no menu bar item found (logs/statusitem.txt)"
        fi

        # Proxy settings: the only control in the wizard footer on the first page.
        local fb fx fy
        if fb=$(python3 "$CI/shotcheck.py" footer "$pre" $((70 * SCALE))); then
            read -r fx fy <<< "$fb"
            "$UI" click $((x + fx / SCALE)) $((y + fy / SCALE))
            sleep 3
            "$UI" windows "$pid" > "$OUT/logs/windows-proxy-$tag.txt" 2>&1
            local ow oid ol ox oy ow2 oh2
            ow=$(other_windows "$pid" "$id" | head -n 1)
            if [ -n "$ow" ]; then
                read -r oid ol ox oy ow2 oh2 <<< "$ow"
                capture_window "$oid" "$ox" "$oy" "$ow2" "$oh2" "$dir/03-proxy-settings.png" > /dev/null
                result "proxy-$tag" PASS "proxy settings dialog ${ow2}x${oh2} pt"
            else
                capture_window "$id" "$x" "$y" "$ww" "$wh" "$dir/03-proxy-settings.png" > /dev/null
                result "proxy-$tag" INFO "no separate dialog window after clicking the footer; captured the wizard window"
            fi
            "$UI" key escape
        else
            result "proxy-$tag" FAIL "proxy settings button not found in the wizard footer"
        fi

        if [ "$variant" = en-light ]; then
            pluginkit -m -A -v -i "$REV_DOMAIN.FinderSyncExt" > "$OUT/bundle/pluginkit-findersync.txt" 2>&1
            if grep -q "$REV_DOMAIN.FinderSyncExt" "$OUT/bundle/pluginkit-findersync.txt"; then
                result "finder-extension" INFO "Finder extension registered with the system: $(head -n 1 "$OUT/bundle/pluginkit-findersync.txt" | tr -s ' \t' ' ')"
            else
                result "finder-extension" INFO "Finder extension not registered while the app runs (bundle/pluginkit-findersync.txt); ad-hoc signed extensions are not expected to load"
            fi
        fi
    else
        if [ -n "$btn" ]; then
            local bcx bcy rest
            read -r bcx bcy rest <<< "$btn"
            "$UI" click $((x + bcx / SCALE)) $((y + bcy / SCALE))
            for ((i = 0; i < 60; i++)); do grep -q '^NODRIVE_OPEN_URL ' "$OUT/logs/app-$tag.log" && break; sleep 0.5; done
            sleep 4
            local now
            now=$(main_window "$pid")
            [ -n "$now" ] && read -r id x y ww wh <<< "$now"
            capture_window "$id" "$x" "$y" "$ww" "$wh" "$dir/04-sign-in.png" > /dev/null
            screencapture -x "$WORK/screen-$tag.png"
            local url via="fake browser"
            url=$(grep -m1 '^NODRIVE_OPEN_URL ' "$OUT/logs/app-$tag.log" | cut -d' ' -f3- | sed -E 's/[A-Za-z0-9]{64,}/<token>/g')
            if [ -z "$url" ] && pgrep -x Safari > /dev/null; then
                # Fallback when the test library did not load: the system opened Safari.
                url=$(osascript -e 'tell application "Safari" to get URL of front document' 2>> "$OUT/logs/safari.txt" | sed -E 's/[A-Za-z0-9]{64,}/<token>/g')
                via="Safari (default browser)"
                screencapture -x "$dir/04-sign-in-safari.png"
                osascript -e 'tell application "Safari" to quit' >> "$OUT/logs/safari.txt" 2>&1
            fi
            if [ -z "$url" ]; then
                result "browser-$tag" FAIL "no browser hand-off within 30 s after clicking Log in (logs/app-$tag.log)"
            else
                case "$url" in
                    https://drive.nourax.org/*) result "browser-$tag" PASS "app opened the browser at $url (seen by the $via)" ;;
                    *) result "browser-$tag" FAIL "app opened an unexpected URL: $url" ;;
                esac
            fi
        else
            result "browser-$tag" FAIL "Log in button not found, not clicked"
        fi
    fi
    stop_app "$pid" "$tag"
    kill "$sampler" 2> /dev/null
    wait "$sampler" 2> /dev/null
    return 0
}

# ---------------------------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------------------------
[ $# -eq 2 ] || { echo "usage: $0 <NoDrive-*.dmg> <output dir>" >&2; exit 2; }
DMG=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
mkdir -p "$2"
OUT=$(cd "$2" && pwd)
WORK=$(mktemp -d "${RUNNER_TEMP:-/tmp}/nodrive-smoke.XXXXXX")
mkdir -p "$OUT/screens" "$OUT/net" "$OUT/logs" "$OUT/bundle"
: > "$OUT/results.tsv"
LOGGER_OK=0

ids() { python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d[sys.argv[2]][sys.argv[3]])' "$REPO/tools/branding/ids.json" "$1" "$2"; }
NAME=$(ids cmake APPLICATION_NAME)
REV_DOMAIN=$(ids cmake APPLICATION_REV_DOMAIN)
TEAM=$(ids cmake DEVELOPMENT_TEAM)
VERSION="$(ids product upstream_version)-nourax.$(ids product nourax_release)"

log "build the test helpers"
swiftc -O -o "$WORK/nodrive-ui" "$CI/nodrive-ui.swift" > "$OUT/logs/build-helpers.txt" 2>&1 || { cat "$OUT/logs/build-helpers.txt"; exit 1; }
clang -dynamiclib -fobjc-arc -framework AppKit -framework CoreServices -o "$WORK/openurl-logger.dylib" \
    "$CI/openurl-logger.m" >> "$OUT/logs/build-helpers.txt" 2>&1 || { cat "$OUT/logs/build-helpers.txt"; exit 1; }
UI="$WORK/nodrive-ui"
LOGGER="$WORK/openurl-logger.dylib"
"$UI" trust > "$OUT/logs/permissions.txt" 2>&1
read -r SCREEN_W SCREEN_H PIX_W PIX_H <<< "$("$UI" screen | head -n 1)"
SCALE=$(( PIX_W / SCREEN_W ))
(( SCALE < 1 )) && SCALE=1
"$UI" screen >> "$OUT/logs/permissions.txt"
log "display ${SCREEN_W}x${SCREEN_H} pt, ${PIX_W}x${PIX_H} px, scale $SCALE; $(tr '\n' ' ' < "$OUT/logs/permissions.txt")"
export SCREEN_W SCREEN_H SCALE

# --- 1. install from the .dmg --------------------------------------------------------------------
log "mount $DMG"
MNT="$WORK/mnt"
mkdir -p "$MNT"
hdiutil attach -nobrowse -readonly -noautoopen -mountpoint "$MNT" "$DMG" > "$OUT/logs/hdiutil.txt" 2>&1 || { cat "$OUT/logs/hdiutil.txt"; exit 1; }
ls -la "$MNT" > "$OUT/bundle/dmg-contents.txt"
VOLNAME=$(diskutil info "$MNT" 2> /dev/null | awk -F': *' '/Volume Name/ { print $2; exit }')
if [ -d "$MNT/$NAME.app" ] && [ -L "$MNT/Applications" ]; then
    result "dmg" PASS "$(basename "$DMG"): volume \"$VOLNAME\" holds $NAME.app and an Applications link"
else
    result "dmg" FAIL "$(basename "$DMG"): volume \"$VOLNAME\": $(ls "$MNT" | tr '\n' ' ')"
fi
APP="/Applications/$NAME.app"
rm -rf "$APP"
ditto "$MNT/$NAME.app" "$APP"
hdiutil detach "$MNT" >> "$OUT/logs/hdiutil.txt" 2>&1 || hdiutil detach -force "$MNT" >> "$OUT/logs/hdiutil.txt" 2>&1
EXE="$APP/Contents/MacOS/$(pl "$APP/Contents/Info.plist" CFBundleExecutable)"
if [ -x "$EXE" ]; then result "install" PASS "copied to $APP (executable $EXE)"; else result "install" FAIL "$EXE missing"; exit 1; fi

# --- 2. bundle checks ------------------------------------------------------------------------------
check_eq() { # check_eq <check> <actual> <expected> <where>
    if [ "$2" = "$3" ]; then result "$1" PASS "$4 = $2"; else result "$1" FAIL "$4 = '$2', expected '$3'"; fi
}
INFO="$APP/Contents/Info.plist"
check_eq bundle-id "$(pl "$INFO" CFBundleIdentifier)" "$REV_DOMAIN" "Info.plist CFBundleIdentifier"
check_eq app-name "$(pl "$INFO" CFBundleName)/$(pl "$INFO" CFBundleDisplayName)/$(pl "$INFO" CFBundleExecutable)" "$NAME/$NAME/$NAME" "CFBundleName/CFBundleDisplayName/CFBundleExecutable"
check_eq short-version "$(pl "$INFO" CFBundleShortVersionString)" "$(ids product upstream_version)" "CFBundleShortVersionString"
LONGV=$(pl "$INFO" CFBundleLongVersionString)
case "$LONGV" in "$NAME $VERSION"*) result long-version PASS "CFBundleLongVersionString = $LONGV" ;; *) result long-version FAIL "CFBundleLongVersionString = $LONGV" ;; esac
result copyright INFO "NSHumanReadableCopyright = $(pl "$INFO" NSHumanReadableCopyright)"
result local-network-text INFO "NSLocalNetworkUsageDescription = $(pl "$INFO" NSLocalNetworkUsageDescription)"
if plutil -convert xml1 -o - "$INFO" | grep -q '<key>SU'; then result no-updater-keys FAIL "Sparkle SU* keys in Info.plist"; else result no-updater-keys PASS "no Sparkle SU* keys in Info.plist"; fi
if [ -e "$APP/Contents/Frameworks/Sparkle.framework" ]; then result no-sparkle FAIL "Sparkle.framework is bundled"; else result no-sparkle PASS "no Sparkle.framework in the bundle (no built-in updater)"; fi

FSE="$APP/Contents/PlugIns/FinderSyncExt.appex/Contents/Info.plist"
if [ -f "$FSE" ]; then
    check_eq findersync-id "$(pl "$FSE" CFBundleIdentifier)" "$REV_DOMAIN.FinderSyncExt" "FinderSyncExt CFBundleIdentifier"
    check_eq findersync-name "$(pl "$FSE" CFBundleDisplayName)" "$NAME FinderSync" "FinderSyncExt CFBundleDisplayName (name in System Settings → Extensions)"
    check_eq findersync-group "$(pl "$FSE" NCApplicationGroupIdentifier)" "$TEAM.$REV_DOMAIN" "FinderSyncExt NCApplicationGroupIdentifier"
else
    result findersync-id FAIL "FinderSyncExt.appex missing"
fi
for li in "$APP"/Contents/Library/LoginItems/*.app; do
    [ -d "$li" ] || continue
    result login-item INFO "$(basename "$li"): CFBundleIdentifier = $(pl "$li/Contents/Info.plist" CFBundleIdentifier), CFBundleName = $(pl "$li/Contents/Info.plist" CFBundleName)"
    case "$(pl "$li/Contents/Info.plist" CFBundleIdentifier)" in
        *nextcloud*|*Nextcloud*) result login-item-id FAIL "$(basename "$li") has a Nextcloud bundle id" ;;
        *"$REV_DOMAIN"*) result login-item-id PASS "$(basename "$li") id $(pl "$li/Contents/Info.plist" CFBundleIdentifier)" ;;
    esac
done
if ls "$APP/Contents/PlugIns" | grep -q FileProvider; then
    result file-provider INFO "File Provider extensions are in the bundle: $(ls "$APP/Contents/PlugIns" | tr '\n' ' ')"
else
    result file-provider INFO "No File Provider extension in this build (classic sync build, nextcloud-client.buildFileProviderModule=False); files on demand is not tested in CI. PlugIns: $(ls "$APP/Contents/PlugIns" | tr '\n' ' ')"
fi
codesign -d --entitlements - --xml "$APP" > "$OUT/bundle/entitlements.plist" 2> /dev/null
if grep -q "$TEAM.$REV_DOMAIN" "$OUT/bundle/entitlements.plist" && ! grep -qi nextcloud "$OUT/bundle/entitlements.plist"; then
    result app-group PASS "application group $TEAM.$REV_DOMAIN in the app's entitlements"
else
    result app-group FAIL "application group not $TEAM.$REV_DOMAIN (bundle/entitlements.plist)"
fi
codesign -dv "$APP" > "$OUT/bundle/codesign.txt" 2>&1
if codesign --verify --deep --strict "$APP" >> "$OUT/bundle/codesign.txt" 2>&1; then
    result signature PASS "ad-hoc signature valid after copying to /Applications ($(grep -E '^(Signature|TeamIdentifier)' "$OUT/bundle/codesign.txt" | tr '\n' ' '))"
else
    result signature FAIL "codesign --verify failed (bundle/codesign.txt)"
fi
# Every Info.plist outside the Qt/third-party frameworks: no Nextcloud name.
bad=""
while IFS= read -r -d '' p; do
    plutil -convert xml1 -o - "$p" 2> /dev/null | grep -qi nextcloud && bad="$bad ${p#"$APP"/}"
done < <(find "$APP" -name Info.plist -not -path '*/Frameworks/*' -print0)
if [ -z "$bad" ]; then result plists PASS "no Info.plist in the app, Finder extension or login item names Nextcloud"
else result plists FAIL "Nextcloud in:$bad"; fi
ICON=$(pl "$INFO" CFBundleIconFile)
ICNS="$APP/Contents/Resources/${ICON%.icns}.icns"
if [ -f "$ICNS" ]; then
    iconutil -c iconset -o "$WORK/app.iconset" "$ICNS" > /dev/null 2>&1
    cp "$WORK/app.iconset/icon_512x512.png" "$OUT/bundle/app-icon-512.png" 2> /dev/null || true
    result app-icon INFO "CFBundleIconFile $ICON; $(ls "$WORK/app.iconset" 2> /dev/null | wc -l | tr -d ' ') sizes in the .icns; 512 px copy in bundle/app-icon-512.png"
else
    result app-icon FAIL "icon file $ICNS missing"
fi
if [ -f "$APP/Contents/Resources/licenses/THIRD-PARTY-NOTICES.txt" ] && ls "$APP/Contents/Resources/licenses" | grep -q COPYING; then
    result licences PASS "Contents/Resources/licenses: $(ls "$APP/Contents/Resources/licenses" | tr '\n' ' ')"
else
    result licences FAIL "licence files missing in Contents/Resources/licenses"
fi
ls -R "$APP" > "$OUT/bundle/app-tree.txt"

# --- 3. version and help output ----------------------------------------------------------------
with_timeout 60 "$EXE" --version > "$OUT/logs/version.txt" 2>&1
with_timeout 60 "$EXE" --help > "$OUT/logs/help.txt" 2>&1
if grep -q "^$NAME version " "$OUT/logs/version.txt" && ! grep -qi nextcloud "$OUT/logs/version.txt" "$OUT/logs/help.txt"; then
    result cli-version PASS "$(grep -m1 "^$NAME version " "$OUT/logs/version.txt"); --help names no Nextcloud"
else
    result cli-version FAIL "see logs/version.txt and logs/help.txt"
fi

# --- 4. network capture ------------------------------------------------------------------------
log "network capture (DNS)"
# Text output, line-buffered: Apple's tcpdump writes no pcap file on the PKTAP "any" interface.
TCPDUMP_IF=any
sudo tcpdump -i any -n -l port 53 > "$OUT/net/dns.txt" 2> "$OUT/logs/tcpdump.txt" &
sleep 3
if ! pgrep -x tcpdump > /dev/null; then
    TCPDUMP_IF=$(route -n get default 2> /dev/null | awk '/interface:/ { print $2 }')
    log "tcpdump -i any did not start; using ${TCPDUMP_IF:-en0}"
    sudo tcpdump -i "${TCPDUMP_IF:-en0}" -n -l port 53 > "$OUT/net/dns.txt" 2>> "$OUT/logs/tcpdump.txt" &
    sleep 3
fi
# A known lookup, so the report shows that the capture works.
dscacheutil -q host -a name github.com > /dev/null 2>&1
result dns-capture INFO "tcpdump on interface $TCPDUMP_IF, port 53, whole runner ($(head -n 1 "$OUT/logs/tcpdump.txt"))"

# --- 5. app runs -------------------------------------------------------------------------------
STATUS=0
for variant in en-light de-light ar-light en-dark; do
    for run in screens login; do
        app_run "$variant" "$run" || STATUS=1
    done
done
set_scheme light

sleep 2
sudo pkill -INT -x tcpdump
sleep 2

if [ "$LOGGER_OK" = 1 ]; then
    result fake-browser INFO "test library loaded into the app (DYLD_INSERT_LIBRARIES); it records browser hand-offs and starts no browser"
else
    result fake-browser FAIL "the URL logger was not loaded into the app (no 'NODRIVE_OPEN_URL_LOGGER installed' in logs/app-*.log)"
fi

# --- 6. config folder --------------------------------------------------------------------------
CONTAINER="$HOME/Library/Containers/$REV_DOMAIN/Data"
CFGDIR="$CONTAINER/Library/Preferences/$NAME"
ls -la "$CFGDIR" > "$OUT/bundle/config-folder.txt" 2>&1
if [ -f "$CFGDIR/$(ids cmake APPLICATION_CONFIG_NAME).cfg" ]; then
    result config-folder PASS "${CFGDIR/#$HOME/~}/$(ids cmake APPLICATION_CONFIG_NAME).cfg (app sandbox container $REV_DOMAIN)"
else
    result config-folder FAIL "no $(ids cmake APPLICATION_CONFIG_NAME).cfg in ${CFGDIR/#$HOME/~} (bundle/config-folder.txt)"
fi
other=$(ls -d "$HOME/Library/Preferences/Nextcloud" "$HOME/Library/Containers/com.nextcloud.desktopclient" 2> /dev/null | tr '\n' ' ')
if [ -z "$other" ]; then result no-nextcloud-folders PASS "no Nextcloud config folder or container was created"
else result no-nextcloud-folders FAIL "created: $other"; fi

# --- 7. network check ----------------------------------------------------------------------------
sed -E -i '' 's/[A-Za-z0-9]{64,}/<token>/g' "$OUT"/logs/app-*.log 2> /dev/null
cat "$OUT"/net/lsof-*.txt 2> /dev/null | awk '/->/ { split($9, a, "->"); print a[2] }' | sort | uniq -c | sort -rn > "$OUT/net/app-connections.txt"
args=(--dns "$OUT/net/dns.txt" --report "$OUT/net/network-report.md")
for f in "$OUT"/logs/app-*.log; do args+=(--log "$f"); done
if python3 "$HERE/netcheck.py" "${args[@]}" > "$OUT/logs/netcheck.txt"; then
    result "no-nextcloud-host" PASS "$(tail -n 1 "$OUT/logs/netcheck.txt"); app connections: $(awk '{ printf "%s ", $2 }' "$OUT/net/app-connections.txt")"
else
    result "no-nextcloud-host" FAIL "$(tail -n 1 "$OUT/logs/netcheck.txt")"
fi

rm -rf "$WORK"
if grep -q $'\tFAIL\t' "$OUT/results.tsv"; then STATUS=1; fi
log "done, status $STATUS"
exit $STATUS
