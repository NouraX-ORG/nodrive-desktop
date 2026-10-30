#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Device calendar and contacts sync against a throwaway server (plans/noagenda-desktop-plan.md, 4.6, phase "device-sync").
#
# Runs only in GitHub Actions (Linux runner with Docker). It starts a real Nextcloud 35 in a Docker container, creates
# the person "ali", builds NoDriveDeviceSyncServerTest in the Qt image and runs it. Chromium (Playwright for Python,
# tools/ci/devicesync-grant.py) plays the person: it signs in and clicks "Grant access" on the Login Flow v2 pages. The
# test then checks that the new password does a CalDAV and a CardDAV PROPFIND (207), that the profile is built for the
# host, and that "It didn't work" revokes the password (the same PROPFIND is 401). Afterwards "occ user:auth-tokens:list"
# must show exactly one token named "NoDrive Calendar & Contacts (ci)" (the kept one). The server's own Apple profile
# endpoint is called once and its reply recorded as evidence for plan 0.3.
#
# No secret is in this file. The throwaway passwords come from "openssl rand", are masked with ::add-mask::, are never
# echoed, and travel only in 0600 env files in $RUNNER_TEMP. The server is reachable only on 127.0.0.1. It contacts no
# app store and no other host. The Drive of Nourax is never contacted.
set -euo pipefail

OUT="${OUT:-$PWD/devicesync-server-out}"
TMP="${RUNNER_TEMP:-/tmp}/devicesync-server"
QT_IMAGE="ghcr.io/nextcloud/continuous-integration-client-qt6:client-sid-6.10.2-4"
SERVER_TAG="nextcloud:35.0.2-apache"
SERVER_PINNED="${SERVER_TAG}@sha256:6f514aae04ad16917124064f7df851720c82b5ccb180b56cb787bed2ed8a7246"
SERVER=nodrive-ci-devicesync
BUILD_NUMBER="${GITHUB_RUN_NUMBER:-0}"
PORT_URL="http://127.0.0.1:8080"

rm -rf "$OUT" "$TMP"
mkdir -p "$OUT" "$TMP"
chmod 700 "$TMP"
REPORT="$OUT/report.md"
FAILED=0

say() { printf '%s\n' "$*" | tee -a "$REPORT"; }
pass() { say "| $1 | PASS | $2 |"; }
fail() { say "| $1 | FAIL | $2 |"; FAILED=1; }
check() { if [ "$2" = 0 ]; then pass "$1" "$3"; else fail "$1" "$3"; fi; }

{
  echo "# Device calendar and contacts sync against a throwaway server"
  echo
  echo "Server image: $SERVER_TAG. Names, status codes and counts only; no passwords."
  echo
  echo "| Check | Result | Detail |"
  echo "|---|---|---|"
} > "$REPORT"

new_secret() { openssl rand -hex 24; }
PASS_ADMIN=$(new_secret); PASS_ALI=$(new_secret)
for v in "$PASS_ADMIN" "$PASS_ALI"; do echo "::add-mask::$v"; done
umask 077
printf 'NEXTCLOUD_ADMIN_PASSWORD=%s\n' "$PASS_ADMIN" > "$TMP/server.env"
printf '%s\n' "$PASS_ADMIN" "$PASS_ALI" > "$TMP/forbidden.txt"
umask 022

# ---- the server --------------------------------------------------------------------------------------------------------------
if docker pull -q "$SERVER_PINNED" > /dev/null 2>&1; then
  SERVER_IMAGE="$SERVER_PINNED"
else
  echo "pinned digest not available, using the tag"
  docker pull -q "$SERVER_TAG" > /dev/null
  SERVER_IMAGE="$SERVER_TAG"
fi
docker run -d --name "$SERVER" -p 127.0.0.1:8080:80 \
  -e SQLITE_DATABASE=nc -e NEXTCLOUD_ADMIN_USER=ciadmin \
  --env-file "$TMP/server.env" \
  -e NEXTCLOUD_TRUSTED_DOMAINS="localhost 127.0.0.1" "$SERVER_IMAGE" > /dev/null
for i in $(seq 1 90); do
  if curl -fsS "$PORT_URL/status.php" 2> /dev/null | grep -q '"installed":true'; then break; fi
  sleep 5
done
VERSION=$(curl -fsS "$PORT_URL/status.php" | python3 -c 'import json,sys; print(json.load(sys.stdin)["versionstring"])')
check server-version "$([ "$VERSION" = 35.0.2 ] && echo 0 || echo 1)" "versionstring $VERSION"

occ() { docker exec -u www-data "$SERVER" php occ "$@"; }
occ config:system:set appstoreenabled --value=false --type=boolean > /dev/null
occ config:system:set has_internet_connection --value=false --type=boolean > /dev/null
occ app:disable firstrunwizard > /dev/null 2>&1 || true

OC_PASS="$PASS_ALI" NC_PASS="$PASS_ALI" docker exec -u www-data -e OC_PASS -e NC_PASS "$SERVER" php occ user:add --password-from-env --display-name Ali ali > /dev/null
check user "0" "the person ali (throwaway)"

# ---- build the test ----------------------------------------------------------------------------------------------------------
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/src" -w /src "$QT_IMAGE" bash -c "
  cmake -S . -B build-devicesync -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_BUILD_TYPE=Debug -DQT_MAJOR_VERSION=6 -DQUICK_COMPILER=ON -DBUILD_TESTING=1 \
    -DMIRALL_VERSION_BUILD=$BUILD_NUMBER > /tmp/configure.log 2>&1 || { tail -n 40 /tmp/configure.log; exit 1; }
  cmake --build build-devicesync --target NoDriveDeviceSyncServerTest 2>&1 | tail -n 5"
test -x build-devicesync/bin/NoDriveDeviceSyncServerTest
pass build-test "NoDriveDeviceSyncServerTest built in $QT_IMAGE"

# ---- the person's part (Chromium) --------------------------------------------------------------------------------------------
PLAYWRIGHT_VERSION=1.49.1
GRANT_PID=""
if python3 -m venv "$TMP/pw" > /dev/null 2>&1 \
    && "$TMP/pw/bin/pip" install --quiet --disable-pip-version-check "playwright==$PLAYWRIGHT_VERSION" > "$OUT/playwright-install.log" 2>&1 \
    && "$TMP/pw/bin/python" -m playwright install --with-deps chromium >> "$OUT/playwright-install.log" 2>&1; then
  pass playwright-install "playwright $PLAYWRIGHT_VERSION with Chromium"
  GRANT_USER=ali GRANT_PASSWORD="$PASS_ALI" GRANT_OUT="$OUT" GRANT_SERVER="$PORT_URL" \
    "$TMP/pw/bin/python" tools/ci/devicesync-grant.py > "$OUT/grant-log.txt" 2>&1 &
  GRANT_PID=$!
else
  fail playwright-install "see playwright-install.log"
fi

# ---- the test: on the host network, so the sign-in addresses (127.0.0.1:8080) work for the test and for Chromium ----------------------
printf 'NODRIVE_TEST_USER=ali\nNODRIVE_TEST_PASSWORD=%s\n' "$PASS_ALI" > "$TMP/test.env"
: > "$OUT/phase-report.md"
if docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp --network host --env-file "$TMP/test.env" \
    -e NODRIVE_TEST_SERVER_URL="$PORT_URL" -e NODRIVE_TEST_COMPUTER=ci \
    -e NODRIVE_TEST_REPORT=/out/phase-report.md -e NODRIVE_TEST_OUTDIR=/out -e QT_QPA_PLATFORM=offscreen \
    -v "$PWD:/src" -v "$OUT:/out" -w /src/build-devicesync/bin "$QT_IMAGE" \
    ./NoDriveDeviceSyncServerTest > "$TMP/test.log" 2>&1; then
  pass test-run "$(grep -E '^Totals' "$TMP/test.log" || echo ok)"
else
  fail test-run "see the log in the artifact"
  sed -E 's/(password=)[^ ]*/\1<hidden>/' "$TMP/test.log" | tail -n 60 || true
fi
sed -E 's/(password=)[^ ]*/\1<hidden>/' "$TMP/test.log" > "$OUT/log-test.txt" || true
cat "$OUT/phase-report.md" >> "$REPORT"
if grep -q '| FAIL |' "$OUT/phase-report.md"; then FAILED=1; fi

if [ -n "$GRANT_PID" ]; then
  if wait "$GRANT_PID"; then pass grant-script "both grants were made in the browser (grant-log.txt)"; else fail grant-script "see grant-log.txt and grant-shots/"; fi
fi

# ---- what the server knows now ---------------------------------------------------------------------------------------------------
tokens=$(occ user:auth-tokens:list ali --output=json 2> /dev/null || echo '[]')
printf '%s' "$tokens" | python3 -c '
import json,sys
rows=json.load(sys.stdin)
names=[r.get("name","") if isinstance(r,dict) else "" for r in rows]
print("\n".join(names))' > "$OUT/token-names.txt" || true
kept=$(grep -cFx "NoDrive Calendar & Contacts (ci)" "$OUT/token-names.txt" || true)
check token-named-and-revoked "$([ "$kept" = 1 ] && echo 0 || echo 1)" "tokens named \"NoDrive Calendar & Contacts (ci)\" after both flows: $kept (the kept one stays, the revoked one is gone; expected 1)"

# the server's reply for its own Apple profile endpoint (plan 0.3): recorded by the test in server-apple-profile.txt
[ -f "$OUT/server-apple-profile.txt" ] && sed 's/^/    /' "$OUT/server-apple-profile.txt" || true

# ---- no throwaway password anywhere in what is uploaded -----------------------------------------------------------------------------
docker logs "$SERVER" 2>&1 | grep -E '"(GET|POST|PROPFIND|DELETE) [^"]*(login/v2|apppassword|apple-provisioning|dav/(calendars|addressbooks))[^"]*"' \
  | sed -E 's/^[^"]*"([A-Z]+) ([^ ?]*)[^"]*" ([0-9]+).*/\1 \2 -> \3/' | sed -E 's#/remote.php/dav/(calendars|addressbooks/users)/[a-z]+/#/remote.php/dav/\1/<user>/#' | sort | uniq -c | sort -rn | head -n 60 > "$OUT/server-requests.txt" || true
LEAK=0
while IFS= read -r value; do
  [ -n "$value" ] || continue
  if grep -rqF -- "$value" "$OUT"; then LEAK=1; fi
done < "$TMP/forbidden.txt"
check no-secret-in-artifact "$LEAK" "the throwaway passwords were searched in the artifact folder"

docker rm -f "$SERVER" > /dev/null 2>&1 || true
cat "$REPORT" >> "${GITHUB_STEP_SUMMARY:-/dev/null}" || true
exit "$FAILED"
