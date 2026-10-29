#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoAgenda against a throwaway server (plans/noagenda-desktop-plan.md, 1.10).
#
# Runs only in GitHub Actions (Linux runner with Docker). It starts a real Nextcloud 35 in a Docker network,
# installs the pinned calendar, tasks, Deck and nc_gantt 2.4.1 apps (SHA-256 checked), seeds it with
# tools/ci/agenda-seed.py, builds NoDriveAgendaServerTest and NoDriveAgendaUiTest in the Qt image and runs the
# phases auth-proof, contract, read-ali, read-mara, read-sara, read-olaf and guard (read only). The live screenshots of
# Ali's seeded day come from the same data. Then comes the edit step: tools/ci/agenda-webedit.py (does the agent id
# survive an edit in the Calendar and Tasks web pages?) and the phases edit-calendar, conflict, edit-gantt, edit-deck
# and offline, which change the throwaway server and check its state directly.
#
# No secret is in this file. The throwaway passwords come from "openssl rand", are masked with ::add-mask::,
# are never echoed, and travel only in 0600 env files in $RUNNER_TEMP. The server is reachable only on
# 127.0.0.1 and in the Docker network, and the test container maps the real Drive host name to 127.0.0.1, so
# the tests can never reach it. The server contacts no app store and no other host (see the occ settings).
set -euo pipefail

OUT="${OUT:-$PWD/agenda-server-out}"
TMP="${RUNNER_TEMP:-/tmp}/agenda-server"
QT_IMAGE="ghcr.io/nextcloud/continuous-integration-client-qt6:client-sid-6.10.2-4"
SERVER_TAG="nextcloud:35.0.2-apache"
SERVER_PINNED="${SERVER_TAG}@sha256:6f514aae04ad16917124064f7df851720c82b5ccb180b56cb787bed2ed8a7246"
SERVER=nodrive-ci-server
NET=nodrive-ci-agenda
BUILD_NUMBER="${GITHUB_RUN_NUMBER:-0}"

# Pinned apps (SHA-256 of the tarballs, computed on 2026-10-28).
GANTT_URL="https://codeberg.org/fds2610/NC-Gantt/releases/download/v2.4.1-9a09070/nc_gantt-2.4.1-9a09070.tar.gz"
GANTT_SHA="9003904a4398f94f1870cc93e6a129d6c7e72298de05b3c5d0104efa976bcfb9"
DECK_URL="https://github.com/nextcloud-releases/deck/releases/download/v1.19.1/deck-v1.19.1.tar.gz"
DECK_SHA="382e11139ee399444bdfa17f018aa9109376928f9bd36090ead8ea340b4d3f0e"
CALENDAR_URL="https://github.com/nextcloud-releases/calendar/releases/download/v6.6.4/calendar-v6.6.4.tar.gz"
CALENDAR_SHA="0557b6b5852a467cf3f746e59d56478d227f78bf7a66357b30dfc10cae4c7e3c"
TASKS_URL="https://github.com/nextcloud/tasks/releases/download/v0.18.1/tasks.tar.gz"
TASKS_SHA="0c988d50531c9bfa246e6c31f3f95369adde1629ba70d15194915ab56e24687b"

rm -rf "$OUT" "$TMP"
mkdir -p "$OUT/shots" "$TMP/apps" "$TMP/work"
chmod 700 "$TMP"
REPORT="$OUT/report.md"
FAILED=0

say() { printf '%s\n' "$*" | tee -a "$REPORT"; }
pass() { say "| $1 | PASS | $2 |"; }
fail() { say "| $1 | FAIL | $2 |"; FAILED=1; }
check() { # name, condition result (0 = ok), detail
  if [ "$2" = 0 ]; then pass "$1" "$3"; else fail "$1" "$3"; fi
}

{
  echo "# NoAgenda against a throwaway server"
  echo
  echo "Server image: $SERVER_TAG. Names, status codes and counts only; no passwords, no values of private fields."
  echo
  echo "| Check | Result | Detail |"
  echo "|---|---|---|"
} > "$REPORT"

# ---- throwaway secrets (never printed) ----------------------------------------------------------------------------
new_secret() { openssl rand -hex 24; }
PASS_ADMIN=$(new_secret); PASS_MARA=$(new_secret); PASS_ALI=$(new_secret); PASS_SARA=$(new_secret); PASS_OLAF=$(new_secret)
for v in "$PASS_ADMIN" "$PASS_MARA" "$PASS_ALI" "$PASS_SARA" "$PASS_OLAF"; do echo "::add-mask::$v"; done
umask 077
printf 'NEXTCLOUD_ADMIN_PASSWORD=%s\n' "$PASS_ADMIN" > "$TMP/server.env"
printf '%s\n' "$PASS_ADMIN" "$PASS_MARA" "$PASS_ALI" "$PASS_SARA" "$PASS_OLAF" > "$TMP/forbidden.txt"
umask 022

# ---- the apps (downloaded on the runner, checked, then copied into the container) ------------------------------------------
fetch_app() { # name url sha
  local name=$1 url=$2 sha=$3
  local file="$TMP/apps/$name.tar.gz"
  curl -fsSL --retry 3 -o "$file" "$url"
  local got
  got=$(sha256sum "$file" | cut -d' ' -f1)
  if [ "$got" != "$sha" ]; then
    fail "app-$name-sha256" "expected $sha, got $got"
    exit 1
  fi
  mkdir -p "$TMP/apps/$name"
  tar -xzf "$file" -C "$TMP/apps/$name"
  pass "app-$name-sha256" "$sha"
}
fetch_app nc_gantt "$GANTT_URL" "$GANTT_SHA"
fetch_app deck "$DECK_URL" "$DECK_SHA"
fetch_app calendar "$CALENDAR_URL" "$CALENDAR_SHA"
fetch_app tasks "$TASKS_URL" "$TASKS_SHA"

# ---- the server ------------------------------------------------------------------------------------------------------------
docker network create "$NET" > /dev/null
if docker pull -q "$SERVER_PINNED" > /dev/null 2>&1; then
  SERVER_IMAGE="$SERVER_PINNED"
else
  echo "pinned digest not available, using the tag"
  docker pull -q "$SERVER_TAG" > /dev/null
  SERVER_IMAGE="$SERVER_TAG"
fi
docker run -d --name "$SERVER" --network "$NET" -p 127.0.0.1:8080:80 \
  -e SQLITE_DATABASE=nc -e NEXTCLOUD_ADMIN_USER=ciadmin \
  --env-file "$TMP/server.env" \
  -e NEXTCLOUD_TRUSTED_DOMAINS="$SERVER localhost 127.0.0.1" "$SERVER_IMAGE" > /dev/null

for i in $(seq 1 90); do
  if curl -fsS http://127.0.0.1:8080/status.php 2> /dev/null | grep -q '"installed":true'; then break; fi
  sleep 5
done
VERSION=$(curl -fsS http://127.0.0.1:8080/status.php | python3 -c 'import json,sys; print(json.load(sys.stdin)["versionstring"])')
check server-version "$([ "$VERSION" = 35.0.2 ] && echo 0 || echo 1)" "versionstring $VERSION"

occ() { docker exec -u www-data "$SERVER" php occ "$@"; }
# The throwaway server contacts no app store and no other host.
occ config:system:set appstoreenabled --value=false --type=boolean > /dev/null
occ config:system:set has_internet_connection --value=false --type=boolean > /dev/null
occ config:system:set debug --value=true --type=boolean > /dev/null

for app in nc_gantt deck calendar tasks; do
  docker cp "$TMP/apps/$app/$app" "$SERVER:/var/www/html/custom_apps/$app" > /dev/null
  docker exec "$SERVER" chown -R www-data:www-data "/var/www/html/custom_apps/$app"
done
occ app:enable calendar tasks deck nc_gantt > /dev/null
# The first-run wizard of the server would cover the web pages that tools/ci/agenda-webedit.py drives.
occ app:disable firstrunwizard > /dev/null 2>&1 || true
apps_json=$(occ app:list --output=json)
versions=$(printf '%s' "$apps_json" | python3 -c '
import json,sys
d=json.load(sys.stdin)["enabled"]
print(" ".join("%s=%s" % (a, d.get(a, "missing")) for a in ("calendar","tasks","deck","nc_gantt")))')
want="calendar=6.6.4 tasks=0.18.1 deck=1.19.1 nc_gantt=2.4.1"
check pinned-app-versions "$([ "$versions" = "$want" ] && echo 0 || echo 1)" "$versions"

# ---- users and calendars -----------------------------------------------------------------------------------------------------
add_user() { # uid password display
  OC_PASS="$2" NC_PASS="$2" docker exec -u www-data -e OC_PASS -e NC_PASS "$SERVER" php occ user:add --password-from-env --display-name "$3" "$1" > /dev/null
  occ user:setting "$1" settings email "$1@example.test" > /dev/null
}
add_user mara "$PASS_MARA" Mara
add_user ali "$PASS_ALI" Ali
add_user sara "$PASS_SARA" Sara
add_user olaf "$PASS_OLAF" Olaf
occ group:add org-a > /dev/null
occ group:add org-b > /dev/null
for u in mara ali sara; do occ group:adduser org-a "$u" > /dev/null; done
occ group:adduser org-b olaf > /dev/null
check users "0" "mara (manager), ali and sara (staff) in org-a, olaf in org-b"
occ dav:create-calendar ali personal > /dev/null 2>&1 || true
occ dav:create-calendar ali work > /dev/null
occ dav:create-calendar mara team > /dev/null

# ---- the seed ------------------------------------------------------------------------------------------------------------------
export SERVER_URL=http://127.0.0.1:8080 PASS_ADMIN PASS_MARA PASS_ALI PASS_SARA PASS_OLAF
python3 tools/ci/agenda-seed.py "$TMP/work" | tee -a "$TMP/seed.log"
cp "$TMP/work/seed.json" "$OUT/seed.json"
pass seed "$(tail -n 1 "$TMP/seed.log")"

# A subscription calendar and a contact with a birthday (information calendars).
code=$(curl -sS -o /dev/null -w '%{http_code}' -u "ali:$PASS_ALI" -X MKCOL -H 'Content-Type: application/xml' \
  --data '<d:mkcol xmlns:d="DAV:" xmlns:cs="http://calendarserver.org/ns/" xmlns:ic="http://apple.com/ns/ical/"><d:set><d:prop><d:resourcetype><d:collection/><cs:subscribed/></d:resourcetype><d:displayname>Local holidays</d:displayname><cs:source><d:href>http://holidays-feed.invalid/holidays.ics</d:href></cs:source></d:prop></d:set></d:mkcol>' \
  http://127.0.0.1:8080/remote.php/dav/calendars/ali/holidays || true)
check subscription-calendar "$([ "$code" = 201 ] && echo 0 || echo 1)" "MKCOL subscription: HTTP $code"
bday=$(date -u +%m%d)
code=$(curl -sS -o /dev/null -w '%{http_code}' -u "ali:$PASS_ALI" -X PUT -H 'Content-Type: text/vcard' \
  --data "$(printf 'BEGIN:VCARD\r\nVERSION:3.0\r\nUID:ci-contact-1\r\nFN:Sara Contact\r\nN:Contact;Sara;;;\r\nBDAY:1990%s\r\nEND:VCARD\r\n' "$bday")" \
  http://127.0.0.1:8080/remote.php/dav/addressbooks/users/ali/contacts/ci-contact-1.vcf || true)
check contact "$([ "$code" = 201 ] && echo 0 || echo 1)" "PUT contact with a birthday: HTTP $code"
occ dav:sync-birthday-calendar ali > /dev/null 2>&1 || true

# ---- app passwords, as the desktop client gets them from Login Flow v2 --------------------------------------------------------
app_password() { # uid password
  OC_PASS="$2" NC_PASS="$2" docker exec -u www-data -e OC_PASS -e NC_PASS "$SERVER" php occ user:auth-tokens:add --password-from-env --name nodrive-ci "$1" | tail -n 1 | tr -d '\r\n '
}
TOK_ALI=$(app_password ali "$PASS_ALI"); TOK_MARA=$(app_password mara "$PASS_MARA"); TOK_SARA=$(app_password sara "$PASS_SARA"); TOK_OLAF=$(app_password olaf "$PASS_OLAF")
for v in "$TOK_ALI" "$TOK_MARA" "$TOK_SARA" "$TOK_OLAF"; do
  [ -n "$v" ] && echo "::add-mask::$v" && printf '%s\n' "$v" >> "$TMP/forbidden.txt"
done
check app-passwords "$([ -n "$TOK_ALI" ] && [ -n "$TOK_MARA" ] && [ -n "$TOK_SARA" ] && [ -n "$TOK_OLAF" ] && echo 0 || echo 1)" "four app passwords made by occ"

# ---- build the tests ------------------------------------------------------------------------------------------------------------
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/src" -w /src "$QT_IMAGE" bash -c "
  cmake -S . -B build-agenda -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_BUILD_TYPE=Debug -DQT_MAJOR_VERSION=6 -DQUICK_COMPILER=ON -DBUILD_TESTING=1 \
    -DMIRALL_VERSION_BUILD=$BUILD_NUMBER > /tmp/configure.log 2>&1 || { tail -n 40 /tmp/configure.log; exit 1; }
  cmake --build build-agenda --target NoDriveAgendaServerTest NoDriveAgendaUiTest 2>&1 | tail -n 5"
test -x build-agenda/bin/NoDriveAgendaServerTest
pass build-test "NoDriveAgendaServerTest and NoDriveAgendaUiTest built in $QT_IMAGE"

# ---- run the phases --------------------------------------------------------------------------------------------------------------
run_phase() { # mode user token
  printf 'NODRIVE_TEST_USER=%s\nNODRIVE_TEST_PASSWORD=%s\n' "$2" "$3" > "$TMP/test.env"
  if docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp --network "$NET" \
      --add-host drive.nourax.org:127.0.0.1 --env-file "$TMP/test.env" \
      -e NODRIVE_TEST_SERVER_URL="http://$SERVER" -e NODRIVE_TEST_MODE="$1" \
      -e NODRIVE_TEST_REPORT=/out/phase-report.md -e NODRIVE_TEST_SEED=/out/seed.json -e NODRIVE_TEST_OUTDIR=/out \
      -e NODRIVE_AGENDA_FIXTURES=/src/test/nodrive/noagenda -e QT_QPA_PLATFORM=offscreen \
      -v "$PWD:/src" -v "$OUT:/out" -w /src/build-agenda/bin "$QT_IMAGE" \
      ./NoDriveAgendaServerTest > "$TMP/test-$1-$2.log" 2>&1; then
    pass "phase-$1-$2" "$(grep -E '^Totals' "$TMP/test-$1-$2.log" || echo ok)"
  else
    fail "phase-$1-$2" "see the log in the artifact"
    sed -E 's/(password=)[^ ]*/\1<hidden>/' "$TMP/test-$1-$2.log" | tail -n 60 || true
  fi
  sed -E 's/(password=)[^ ]*/\1<hidden>/' "$TMP/test-$1-$2.log" > "$OUT/log-$1-$2.txt" || true
}
: > "$OUT/phase-report.md"
run_phase auth-proof ali "$TOK_ALI"
run_phase contract mara "$TOK_MARA"
run_phase read-ali ali "$TOK_ALI"
run_phase read-mara mara "$TOK_MARA"
run_phase read-sara sara "$TOK_SARA"
run_phase read-olaf olaf "$TOK_OLAF"
run_phase guard ali "$TOK_ALI"
cat "$OUT/phase-report.md" >> "$REPORT"
if grep -q '| FAIL |' "$OUT/phase-report.md"; then FAILED=1; fi

# ---- live screenshots of Ali's seeded day ----------------------------------------------------------------------------------------
if docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -e NODRIVE_AGENDA_LIVE_DIR=/out/live-ali-cache -e NODRIVE_SCREENSHOT_DIR=/out/shots -e QT_QPA_PLATFORM=offscreen \
    -e NODRIVE_AGENDA_FIXTURES=/src/test/nodrive/noagenda \
    -v "$PWD:/src" -v "$OUT:/out" -w /src/build-agenda/bin "$QT_IMAGE" \
    ./NoDriveAgendaUiTest liveDataFromTheServerTest > "$OUT/log-live-screenshots.txt" 2>&1; then
  pass live-screenshots "$(ls "$OUT/shots/noagenda" 2> /dev/null | tr '\n' ' ')"
else
  fail live-screenshots "see log-live-screenshots.txt"
fi

# ---- what the server saw ---------------------------------------------------------------------------------------------------------------
docker logs "$SERVER" 2>&1 | grep -E '"(GET|PROPFIND|REPORT|PUT|POST|MKCOL|DELETE|MOVE|COPY) [^"]*(nc_gantt|deck|calendars|NoAgenda)[^"]*"' \
  | sed -E 's/^[^"]*"([A-Z]+) ([^ ?]*)[^"]*" ([0-9]+).*/\1 \2 -> \3/' | sed -E 's#/remote.php/dav/calendars/[a-z]+/#/remote.php/dav/calendars/<user>/#' | sort | uniq -c | sort -rn | head -n 200 > "$OUT/server-requests.txt" || true
# The read phases only read: no request of theirs deletes, moves or copies anything.
check no-destructive-requests-in-read-steps "$(grep -Eq ' (DELETE|MOVE|COPY) ' "$OUT/server-requests.txt" && echo 1 || echo 0)" "no DELETE, MOVE or COPY after the seed (read phases)"

# ---- the agent id in the web pages (design 4.2) --------------------------------------------------------------------------------------
# Playwright for Python and Chromium are CI-only tools (Apache-2.0 and BSD-3-Clause): they are not shipped.
PLAYWRIGHT_VERSION=1.49.1
if python3 -m venv "$TMP/pw" > /dev/null 2>&1 \
    && "$TMP/pw/bin/pip" install --quiet --disable-pip-version-check "playwright==$PLAYWRIGHT_VERSION" > "$OUT/playwright-install.log" 2>&1 \
    && "$TMP/pw/bin/python" -m playwright install --with-deps chromium >> "$OUT/playwright-install.log" 2>&1; then
  pass playwright-install "playwright $PLAYWRIGHT_VERSION with Chromium"
  mkdir -p "$OUT/webedit"
  WEBEDIT_SERVER=http://127.0.0.1:8080 WEBEDIT_USER=ali WEBEDIT_PASSWORD="$PASS_ALI" WEBEDIT_OUT="$OUT/webedit" \
    "$TMP/pw/bin/python" tools/ci/agenda-webedit.py > "$OUT/webedit-log.txt" 2>&1 && web_rc=0 || web_rc=$?
  if [ -f "$OUT/webedit/webedit.md" ]; then
    sed -n '/^| App/,/^$/p' "$OUT/webedit/webedit.md" | tee -a "$REPORT" > /dev/null
  fi
  check webedit-agent-id-survives "$web_rc" "exit $web_rc (1 only when both agent properties were dropped by a web page); see webedit/webedit.md"
else
  fail playwright-install "see playwright-install.log"
fi

# ---- the edit phases: the server is changed from here on, and its state is checked directly --------------------------------------------
: > "$OUT/phase-report.md"
run_phase edit-calendar ali "$TOK_ALI"
run_phase conflict ali "$TOK_ALI"
run_phase edit-gantt ali "$TOK_ALI"
run_phase edit-deck ali "$TOK_ALI"
run_phase offline ali "$TOK_ALI"
cat "$OUT/phase-report.md" >> "$REPORT"
if grep -q '| FAIL |' "$OUT/phase-report.md"; then FAILED=1; fi
docker logs "$SERVER" 2>&1 | grep -E '"(DELETE|MOVE|COPY) [^"]*"' \
  | sed -E 's/^[^"]*"([A-Z]+) ([^ ?]*)[^"]*" ([0-9]+).*/\1 \2 -> \3/' | sed -E 's#/remote.php/dav/calendars/[a-z]+/#/remote.php/dav/calendars/<user>/#' | sort | uniq -c > "$OUT/server-requests-edit-deletes.txt" || true
# The edit phases delete exactly one thing: the event that they made themselves. Nothing is moved or copied.
check edit-deletes-only-its-own-event "$( { grep -Ev 'ci-edit-created' "$OUT/server-requests-edit-deletes.txt" | grep -Eq ' (DELETE|MOVE|COPY) ' && echo 1; } || echo 0)" "the only DELETE is the event that the edit phase created; no MOVE or COPY"

# ---- no throwaway password anywhere in what is uploaded -----------------------------------------------------------------------------
LEAK=0
while IFS= read -r value; do
  [ -n "$value" ] || continue
  if grep -rqF -- "$value" "$OUT"; then LEAK=1; fi
done < "$TMP/forbidden.txt"
check no-secret-in-artifact "$LEAK" "throwaway passwords and app passwords were searched in the artifact folder"

docker rm -f "$SERVER" > /dev/null 2>&1 || true
docker network rm "$NET" > /dev/null 2>&1 || true
cat "$REPORT" >> "${GITHUB_STEP_SUMMARY:-/dev/null}" || true
exit "$FAILED"
