#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoDrive Linux smoke test. Runs in CI (ubuntu-latest), not on a developer PC.
#
#   tools/screenshots/linux.sh <NoDrive-*.AppImage> <output dir>
#
# Starts the AppImage under Xvfb (openbox window manager, stalonetray as the
# system tray) with a fresh HOME for every run and takes screenshots of every
# screen that is reachable without an account, in four variants:
#   en-light, de-light, ar-light (Arabic, right to left), en-dark.
# Per variant there are two runs:
#   screens: 01 wizard, 02 tray menu, 03 proxy settings dialog
#   login:   04 after clicking "Log in" (a fake xdg-open records the URL the
#            app hands to the browser; the browser itself is never started)
# Network: tcpdump logs every DNS query on the runner and strace logs the
# app's connect() calls; tools/screenshots/netcheck.py fails on Nextcloud hosts.
#
# A third run per light and dark variant ("unclean") covers the problem reports: 05 prompt after a
# killed run, Not now, no second question, and a scrubbed report in problem-reports/pending.
#
# Output: <out>/screens/<variant>/NN-<screen>.png, <out>/net/, <out>/logs/,
#         <out>/results.tsv (check, PASS|FAIL|INFO, evidence)
#
# Packages (Ubuntu): xvfb openbox stalonetray xdotool wmctrl imagemagick
# x11-utils x11-xserver-utils tcpdump strace dbus python3-dbus python3-gi
# python3-pil, fonts with Arabic glyphs (fonts-noto-core).

set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PRIMARY="#2563EB"

log() { printf '[%(%H:%M:%S)T] %s\n' -1 "$*" >&2; }
result() { printf '%s\t%s\t%s\n' "$1" "$2" "$3" >> "$OUT/results.tsv"; log "RESULT $1 $2 $3"; }

shot() { # shot <variant> <name>
    mkdir -p "$OUT/screens/$1"
    import -display "$DISPLAY" -window root "$OUT/screens/$1/$2.png"
    log "screenshot $1/$2.png"
}

windows() { wmctrl -lpG 2>/dev/null || true; }

# Prints "id x y w h" of the biggest visible NoDrive window (the wizard).
find_wizard() {
    local id best="" area=0 WINDOW X Y WIDTH HEIGHT SCREEN
    for id in $( { xdotool search --onlyvisible --classname '^nodrive$' 2>/dev/null
                   xdotool search --onlyvisible --class '^NoDrive$' 2>/dev/null; } | sort -u); do
        eval "$(xdotool getwindowgeometry --shell "$id" 2>/dev/null)" || continue
        if (( WIDTH >= 300 && WIDTH * HEIGHT > area )); then
            area=$((WIDTH * HEIGHT))
            best="$id $X $Y $WIDTH $HEIGHT"
        fi
    done
    echo "$best"
}

wait_wizard() { # wait_wizard <seconds>
    local i w
    for ((i = 0; i < $1 * 2; i++)); do
        w=$(find_wizard)
        if [ -n "$w" ]; then echo "$w"; return 0; fi
        sleep 0.5
    done
    return 1
}

tray_icon_centre() {
    local id WINDOW X Y WIDTH HEIGHT SCREEN
    id=$( { xdotool search --onlyvisible --classname '^stalonetray$' 2>/dev/null
            xdotool search --onlyvisible --class '^stalonetray$' 2>/dev/null; } | head -n 1)
    [ -n "$id" ] || return 1
    eval "$(xdotool getwindowgeometry --shell "$id")"
    echo "$((X + WIDTH / 2)) $((Y + HEIGHT / 2))"
}

click() { xdotool mousemove --sync "$1" "$2" sleep 0.3 click "${3:-1}"; sleep 0.3; xdotool mousemove 2 2; }

descendants() { local c; for c in $(ps -o pid= --ppid "$1" 2>/dev/null); do echo "$c"; descendants "$c"; done; }

# stop_app <strace pid> <tag>: SIGTERM the app (all processes strace started),
# then SIGKILL whatever still runs with this run's log file in its command line
# (strace itself blocks SIGTERM when it starts the program with -o FILE).
stop_app() {
    ps -eo pid,ppid,comm,args --forest > "$OUT/logs/ps-$2.txt" 2>&1
    local i pids
    pids=$(descendants "$1")
    [ -n "$pids" ] && kill -TERM $pids 2>/dev/null
    for ((i = 0; i < 30; i++)); do kill -0 "$1" 2>/dev/null || break; sleep 0.5; done
    pkill -KILL -f "logs/app-$2[.]log" 2>/dev/null
    wait "$1" 2>/dev/null
    for ((i = 0; i < 20; i++)); do [ -z "$(find_wizard)" ] && break; sleep 0.5; done
}


# ---------------------------------------------------------------------------
# Problem reports: unclean exit prompt, "ask once", and a scrubbed report in pending/
# (plans/round3-desktop-plan.md, 1.8). No account is signed in, so a report waits on disk;
# nothing is sent anywhere. All secrets below are made up.
# ---------------------------------------------------------------------------
find_prompt() { # id of a visible window titled "NoDrive" that is not the wizard (the prompt)
    local id wiz
    wiz=$(find_wizard | cut -d' ' -f1)
    for id in $(xdotool search --onlyvisible --name '^NoDrive$' 2>/dev/null); do
        [ "$id" = "$wiz" ] && continue
        echo "$id"; return 0
    done
    return 1
}

wait_prompt() { # wait_prompt <seconds>
    local i id
    for ((i = 0; i < $1 * 2; i++)); do
        if id=$(find_prompt); then echo "$id"; return 0; fi
        sleep 0.5
    done
    return 1
}

kill_app() { # kill_app <strace pid> <log dir>: the app dies without any clean-up
    local pids
    pids=$(descendants "$1")
    [ -n "$pids" ] && kill -KILL $pids 2>/dev/null
    kill -KILL "$1" 2>/dev/null
    pkill -KILL -f -- "--logdir $2" 2>/dev/null
    wait "$1" 2>/dev/null
    sleep 1
}

press_in_prompt() { # press_in_prompt <window id> <key>: focus the prompt with a click, then press the key
    local id=$1 WINDOW X Y WIDTH HEIGHT SCREEN
    eval "$(xdotool getwindowgeometry --shell "$id")"
    click $((X + WIDTH / 2)) $((Y + HEIGHT / 3))
    xdotool key "$2"
    sleep 1
}

marker_state() { # marker_state <home>: state of run.marker
    local f
    f=$(find "$1/.config" -name run.marker 2>/dev/null | head -n 1)
    if [ -n "$f" ]; then
        python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["state"])' "$f"
    else
        echo none
    fi
}

unclean_flow() { # unclean_flow <variant> <tag> <home>
    local variant=$1 tag=$2 H=$3 LOGDIR="$3/logs" app id i n=0 verdict
    mkdir -p "$LOGDIR"
    start_unclean() {
        n=$((n + 1))
        strace -f -qq -tt -e trace=connect -o "$OUT/net/strace-$tag-$n.txt" \
            "$APPIMAGE" --logdebug --logflush --logdir "$LOGDIR" > "$OUT/logs/stdout-$tag-$n.txt" 2>&1 &
        app=$!
    }

    # 1. A run that never quits cleanly (killed).
    start_unclean
    if ! wait_wizard 120 > /dev/null; then
        result "unclean-$tag" FAIL "the app did not start"
        kill_app "$app" "$LOGDIR"
        return 1
    fi
    sleep 6
    verdict=FAIL; [ "$(marker_state "$H")" = running ] && verdict=PASS
    result "marker-running-$tag" "$verdict" "run.marker is '$(marker_state "$H")' while the app runs"
    kill_app "$app" "$LOGDIR"
    sleep 2

    # 2. Next start: the question appears (the switch is off). Escape = "Not now".
    start_unclean
    if id=$(wait_prompt 90); then
        sleep 1
        shot "$variant" 05-unclean-exit-prompt
        result "prompt-$tag" PASS "the question appeared after a killed run"
        press_in_prompt "$id" Escape
        if find_prompt > /dev/null; then
            result "not-now-$tag" FAIL "the question is still open after Escape"
        else
            result "not-now-$tag" PASS "Escape (Not now) closed the question"
        fi
    else
        shot "$variant" 05-no-prompt
        windows > "$OUT/logs/windows-$tag-prompt.txt"
        result "prompt-$tag" FAIL "no question 90 s after the start"
    fi
    sleep 2
    stop_app "$app" "$tag-a"
    verdict=FAIL; [ "$(marker_state "$H")" = clean ] && verdict=PASS
    result "sigterm-clean-$tag" "$verdict" "run.marker is '$(marker_state "$H")' after SIGTERM (a normal end)"

    # 3. Asked once: the next start does not ask again.
    start_unclean
    if wait_wizard 120 > /dev/null; then
        sleep 9
        if find_prompt > /dev/null; then
            result "ask-once-$tag" FAIL "the question came again after a normal end"
        else
            result "ask-once-$tag" PASS "no question after a normal end"
        fi
    else
        result "ask-once-$tag" FAIL "the app did not start"
    fi
    stop_app "$app" "$tag-b"

    # 4. Another killed run, with a fake log that holds made-up secrets.
    printf '%s\n' \
        '2026-10-28 15:00:01 [ info ] PLANTED-LOG-LINE-KEEP /Photos/Holiday 2026/IMG_0001.jpg' \
        '2026-10-28 15:00:02 [ info sync.httplogger ] Header: { Authorization: Basic YWRtaW46ZmFrZXBhc3N3b3Jk, Cookie: oc_sessionPassphrase=Fake9Session8Passphrase7Value6Zz5Yy4Xx3; nc_session_id=fake1234, }' \
        '2026-10-28 15:00:03 [ info ] app password Fk4pA-Pp9wD-Zz2Qq-Rr7Tt-Uu1Vv created for planted.user@example.org' \
        '2026-10-28 15:00:04 [ info ] appPassword: Qa7Zp3LmN9xV2cTb8RyHs4WdKf6JgEu1XoPiAnMq5ZtCvBlYr0SwDhGk3UeF7jTa2Nc9VbXz' \
        > "$LOGDIR/20261028_1500_planted.log"
    printf '%s\n' YWRtaW46ZmFrZXBhc3N3b3Jk Fake9Session8Passphrase7Value6Zz5Yy4Xx3 Fk4pA-Pp9wD-Zz2Qq-Rr7Tt-Uu1Vv \
        planted.user@example.org Qa7Zp3LmN9xV2cTb8RyHs4WdKf6JgEu1XoPiAnMq5ZtCvBlYr0SwDhGk3UeF7jTa2Nc9VbXz > "$WORK/forbidden-$tag.txt"
    start_unclean
    wait_wizard 120 > /dev/null
    sleep 6
    kill_app "$app" "$LOGDIR"
    sleep 2

    # 5. Send: Enter = Send. Nobody is signed in, so the report is made and waits in pending/.
    start_unclean
    if id=$(wait_prompt 90); then
        press_in_prompt "$id" Return
        local zip=""
        for ((i = 0; i < 80; i++)); do
            zip=$(find "$H/.config" -path '*problem-reports/pending/*.zip' 2>/dev/null | head -n 1)
            [ -n "$zip" ] && break
            sleep 0.5
        done
        if [ -n "$zip" ]; then
            mkdir -p "$OUT/reports"
            cp "$zip" "$OUT/reports/$tag.zip"
            local out
            if out=$(python3 "$HERE/../ci/check-report-zip.py" "$OUT/reports/$tag.zip" --empty-host --kind unclean-exit \
                    --forbid "$WORK/forbidden-$tag.txt" --expect-text PLANTED-LOG-LINE-KEEP --expect-text IMG_0001.jpg 2>&1); then
                result "report-$tag" PASS "$out"
            else
                result "report-$tag" FAIL "$(printf '%s' "$out" | tr '\n' ' ')"
            fi
        else
            result "report-$tag" FAIL "no report in problem-reports/pending 40 s after Send"
        fi
    else
        result "report-$tag" FAIL "no question after the second killed run"
    fi
    stop_app "$app" "$tag-c"
    rm -f "$LOGDIR/20261028_1500_planted.log"
    return 0
}

# ---------------------------------------------------------------------------
# One app run, inside its own D-Bus session (re-executed by dbus-run-session)
# ---------------------------------------------------------------------------
inner() {
    local variant=$1 run=$2 lang language scheme extra=()
    case "$variant" in
        en-light) lang=en_US.UTF-8; language=en; scheme=light ;;
        de-light) lang=de_DE.UTF-8; language=de; scheme=light ;;
        # No --reverse: Qt XORs -reverse with the translation's own direction, and
        # the Arabic Qt catalogue already switches the layout to right-to-left.
        ar-light) lang=ar_EG.UTF-8; language=ar; scheme=light ;;
        en-dark) lang=en_US.UTF-8; language=en; scheme=dark ;;
        *) log "unknown variant $variant"; return 2 ;;
    esac
    local tag="$variant-$run" H="$WORK/home-$variant-$run"
    mkdir -p "$H/.config" "$H/.local/share" "$H/.cache" "$H/tmp" "$H/run"
    chmod 700 "$H/run"
    export HOME="$H" XDG_CONFIG_HOME="$H/.config" XDG_DATA_HOME="$H/.local/share" \
        XDG_CACHE_HOME="$H/.cache" XDG_RUNTIME_DIR="$H/run" TMPDIR="$H/tmp"
    export LANG="$lang" LANGUAGE="$language"
    unset LC_ALL
    export XDG_CURRENT_DESKTOP=GNOME XDG_SESSION_TYPE=x11
    if [ "$scheme" = dark ]; then export GTK_THEME=Adwaita:dark; else export GTK_THEME=Adwaita; fi
    export PATH="$FAKEBIN:$PATH" NODRIVE_OPENED_URLS="$OUT/net/opened-urls-$tag.txt"
    export APPIMAGE_EXTRACT_AND_RUN=1

    python3 "$HERE/fake-portal.py" "$scheme" > "$OUT/logs/portal-$tag.txt" 2>&1 &
    local portal=$! i
    for ((i = 0; i < 50; i++)); do
        dbus-send --session --print-reply --dest=org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop \
            org.freedesktop.portal.Settings.ReadOne string:org.freedesktop.appearance string:color-scheme \
            > "$OUT/logs/portal-check-$tag.txt" 2>&1 && break
        sleep 0.2
    done

    if [ "$run" = unclean ]; then
        unclean_flow "$variant" "$tag" "$H"
        local flow=$?
        kill "$portal" 2>/dev/null
        rm -rf "$H/tmp"
        return $flow
    fi

    if [ "$run" = cli ]; then
        timeout 120 "$APPIMAGE" --version > "$OUT/logs/version.txt" 2>&1
        timeout 120 "$APPIMAGE" --help > "$OUT/logs/help.txt" 2>&1
        kill "$portal" 2>/dev/null
        return 0
    fi

    log "start $tag ($LANG, $scheme${extra[*]:+, ${extra[*]}})"
    strace -f -qq -tt -e trace=connect -o "$OUT/net/strace-$tag.txt" \
        "$APPIMAGE" --logdebug --logflush --logfile "$OUT/logs/app-$tag.log" "${extra[@]}" \
        > "$OUT/logs/stdout-$tag.txt" 2>&1 &
    local app=$! w id x y ww wh
    if ! w=$(wait_wizard 120); then
        windows > "$OUT/logs/windows-$tag.txt"
        shot "$variant" "00-no-wizard-$run"
        result "launch-$tag" FAIL "no NoDrive window within 120 s (see logs/stdout-$tag.txt)"
        stop_app "$app" "$tag"; kill "$portal" 2>/dev/null
        return 1
    fi
    xdotool mousemove 2 2   # no hover effects in the screenshots
    sleep 8   # QML first frame, portal colour scheme, tray icon
    w=$(find_wizard); read -r id x y ww wh <<< "$w"
    windows > "$OUT/logs/windows-$tag.txt"
    result "launch-$tag" PASS "wizard window ${ww}x${wh} at ${x},${y}"
    local pre="$WORK/pre-$tag.png"
    import -display "$DISPLAY" -window root "$pre"

    local bg
    bg=$(python3 "$HERE/locate.py" pixel "$pre" $((x + 12)) $((y + 12)))
    result "background-$tag" INFO "wizard background ${bg% *} (luminance ${bg#* })"
    if [ "$scheme" = dark ]; then
        if (( ${bg#* } < 90 )); then result "dark-mode" PASS "wizard background ${bg% *} is dark"
        else result "dark-mode" FAIL "wizard background ${bg% *} is not dark"; fi
    fi
    local btn
    if btn=$(python3 "$HERE/locate.py" primary "$pre" "$x" "$y" "$ww" "$wh" "$PRIMARY"); then
        local bcx bcy barea bbx bby bbw bbh
        read -r bcx bcy barea bbx bby bbw bbh <<< "$btn"
        result "colour-$tag" PASS "button in $PRIMARY: ${bbw}x${bbh} px at ${bbx},${bby} ($barea px of $PRIMARY)"
    else
        result "colour-$tag" FAIL "no $PRIMARY button in the wizard"
    fi

    if [ "$run" = screens ]; then
        shot "$variant" 01-wizard
        local tc
        if tc=$(tray_icon_centre); then
            click ${tc} 3
            sleep 2
            shot "$variant" 02-tray-menu
            windows >> "$OUT/logs/windows-$tag.txt"
            click 6 $((SCREEN_H - 6)) 1   # close the menu by clicking the empty desktop
            sleep 1
        else
            result "tray-$tag" FAIL "no system tray window"
        fi
        local fb
        if fb=$(python3 "$HERE/locate.py" footer "$pre" "$x" "$y" "$ww" "$wh"); then
            click ${fb}
            sleep 2
            shot "$variant" 03-proxy-settings
        else
            result "proxy-$tag" FAIL "proxy settings button not found"
        fi
    else
        if [ -n "${btn:-}" ]; then
            local bx by
            read -r bx by _ <<< "$btn"
            click "$bx" "$by"
            for ((i = 0; i < 60; i++)); do [ -s "$NODRIVE_OPENED_URLS" ] && break; sleep 0.5; done
            sleep 4
            shot "$variant" 04-sign-in
            windows >> "$OUT/logs/windows-$tag.txt"
            if [ -s "$NODRIVE_OPENED_URLS" ]; then
                sed -E -i 's/[A-Za-z0-9]{64,}/<redacted>/g' "$NODRIVE_OPENED_URLS"
                local url
                url=$(head -n 1 "$NODRIVE_OPENED_URLS" | cut -d' ' -f2-)
                case "$url" in
                    https://drive.nourax.org/*) result "browser-$tag" PASS "app opened the browser at $url" ;;
                    *) result "browser-$tag" FAIL "app opened an unexpected URL: $url" ;;
                esac
            else
                result "browser-$tag" INFO "no browser hand-off within 30 s; see screens/$variant/04-sign-in.png and logs/app-$tag.log"
            fi
        fi
    fi
    stop_app "$app" "$tag"
    kill "$portal" 2>/dev/null
    rm -rf "$H/tmp"
    return 0
}

if [ "${1:-}" = --inner ]; then
    shift
    inner "$@"
    exit $?
fi

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
[ $# -eq 2 ] || { echo "usage: $0 <AppImage> <output dir>" >&2; exit 2; }
APPIMAGE=$(readlink -f "$1")
mkdir -p "$2"
OUT=$(cd "$2" && pwd)
WORK=$(mktemp -d)
FAKEBIN="$WORK/fakebin"
mkdir -p "$OUT/screens" "$OUT/net" "$OUT/logs" "$FAKEBIN"
: > "$OUT/results.tsv"
chmod +x "$APPIMAGE"
export APPIMAGE OUT WORK FAKEBIN
export SCREEN_W=1280 SCREEN_H=800 DISPLAY=:99

# Fake browser: the app calls xdg-open (Qt QDesktopServices on Linux); we only log the URL.
cat > "$FAKEBIN/xdg-open" <<'EOF'
#!/bin/sh
printf '%s %s\n' "$(date +%H:%M:%S)" "$*" >> "${NODRIVE_OPENED_URLS:-/dev/null}"
exit 0
EOF
chmod +x "$FAKEBIN/xdg-open"

log "network capture (DNS, all interfaces)"
sudo tcpdump -i any -nn -U -w "$OUT/net/dns.pcap" port 53 > "$OUT/logs/tcpdump.txt" 2>&1 &
sleep 2

log "Xvfb ${SCREEN_W}x${SCREEN_H}, openbox, stalonetray"
Xvfb :99 -screen 0 "${SCREEN_W}x${SCREEN_H}x24" -nolisten tcp > "$OUT/logs/xvfb.txt" 2>&1 &
XVFB=$!
for ((i = 0; i < 50; i++)); do xdpyinfo -display :99 > /dev/null 2>&1 && break; sleep 0.2; done
# Neutral window decorations (openbox's default theme has a blue title bar,
# which is the window manager's, not the app's).
sed 's#<name>Clearlooks</name>#<name>Onyx</name>#' /etc/xdg/openbox/rc.xml > "$WORK/openbox-rc.xml" 2> /dev/null
if grep -q '<name>Onyx</name>' "$WORK/openbox-rc.xml" 2> /dev/null && [ -d /usr/share/themes/Onyx ]; then
    openbox --config-file "$WORK/openbox-rc.xml" > "$OUT/logs/openbox.txt" 2>&1 &
else
    openbox > "$OUT/logs/openbox.txt" 2>&1 &
fi
sleep 1
xsetroot -solid '#4B5563'
stalonetray --geometry 1x1-0+0 --icon-size 32 --kludges force_icons_size --window-type dock -bg '#1F2937' \
    > "$OUT/logs/stalonetray.txt" 2>&1 &
sleep 1

timeout -k 10 300 dbus-run-session -- bash "$0" --inner en-light cli

STATUS=0
for variant in en-light de-light ar-light en-dark; do
    for run in screens login; do
        timeout -k 10 300 dbus-run-session -- bash "$0" --inner "$variant" "$run" || STATUS=1
        pkill -KILL -f "logs/app-$variant-$run[.]log" 2>/dev/null
    done
done
# Problem reports (unclean exit prompt, ask once, scrubbed report in pending/): light and dark.
for variant in en-light en-dark; do
    timeout -k 10 900 dbus-run-session -- bash "$0" --inner "$variant" unclean || STATUS=1
done

sleep 2
sudo pkill -INT tcpdump
sleep 2
tcpdump -nn -r "$OUT/net/dns.pcap" > "$OUT/net/dns.txt" 2> /dev/null || sudo tcpdump -nn -r "$OUT/net/dns.pcap" > "$OUT/net/dns.txt"
sudo chown "$(id -u):$(id -g)" "$OUT/net/dns.pcap" 2> /dev/null

# Version / help output: must name NoDrive, never Nextcloud.
if grep -q "NoDrive" "$OUT/logs/version.txt" && ! grep -qi "nextcloud" "$OUT/logs/version.txt" "$OUT/logs/help.txt"; then
    result "cli-version" PASS "$(grep -m1 'NoDrive version' "$OUT/logs/version.txt"); --help names no Nextcloud"
else
    result "cli-version" FAIL "see logs/version.txt and logs/help.txt"
    STATUS=1
fi

# Redact login-flow tokens (128 characters) before the logs are uploaded.
sed -E -i 's/[A-Za-z0-9]{64,}/<redacted>/g' "$OUT"/logs/app-*.log "$OUT"/logs/stdout-*.txt 2> /dev/null

args=(--dns "$OUT/net/dns.txt" --report "$OUT/net/network-report.md")
for f in "$OUT"/net/strace-*.txt; do args+=(--strace "$f"); done
for f in "$OUT"/logs/app-*.log; do args+=(--log "$f"); done
if python3 "$HERE/netcheck.py" "${args[@]}" > "$OUT/logs/netcheck.txt"; then
    result "no-nextcloud-host" PASS "$(tail -n 1 "$OUT/logs/netcheck.txt")"
else
    result "no-nextcloud-host" FAIL "$(tail -n 1 "$OUT/logs/netcheck.txt")"
    STATUS=1
fi
# Strace files are large; keep only the network calls.
for f in "$OUT"/net/strace-*.txt; do grep -E 'AF_INET' "$f" > "$f.tmp"; mv "$f.tmp" "$f"; done

kill "$XVFB" 2> /dev/null
pkill openbox 2> /dev/null
pkill stalonetray 2> /dev/null
rm -rf "$WORK"
if grep -q $'\tFAIL\t' "$OUT/results.tsv"; then STATUS=1; fi
log "done, status $STATUS"
exit $STATUS
