#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Problem reports against a throwaway Nextcloud server (plans/round3-desktop-plan.md, 1.9).
#
# Runs only in GitHub Actions (Linux runner with Docker). It starts a real server in a Docker
# network, builds NoDriveProblemReportServerTest in the Qt image and runs it in four phases:
#   1  no "nourax-support" user: the report is saved, the share fails quietly (no share exists)
#   2  the user exists: the folder is shared read-only (permissions=1), the first zip is unchanged
#   3  more reports: still exactly one share; a name that already exists gets _2, nothing is replaced
#   4  a report made with no account stays pending and is sent when the account is connected
#
# No secret is in this file. The throwaway passwords come from "openssl rand", are masked with
# ::add-mask::, are never echoed, and travel only in 0600 env files and curl config files in
# $RUNNER_TEMP. The container is reachable only on 127.0.0.1 and in the Docker network, and the
# test container maps the real Drive host name to 127.0.0.1, so the test can never reach it.
set -euo pipefail

OUT="${OUT:-$PWD/report-server-out}"
TMP="${RUNNER_TEMP:-/tmp}/report-server"
QT_IMAGE="ghcr.io/nextcloud/continuous-integration-client-qt6:client-sid-6.10.2-4"
SERVER_TAG="nextcloud:35.0.2-apache"
SERVER_PINNED="${SERVER_TAG}@sha256:6f514aae04ad16917124064f7df851720c82b5ccb180b56cb787bed2ed8a7246"
SERVER=nodrive-ci-server
NET=nodrive-ci
BUILD_NUMBER="${GITHUB_RUN_NUMBER:-0}"

rm -rf "$OUT" "$TMP"
mkdir -p "$OUT/zips" "$TMP/work" "$TMP/curl"
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
  echo "# Problem reports against a throwaway server"
  echo
  echo "Server image: $SERVER_TAG (pinned by digest when available). Names, status codes and hashes only."
  echo
  echo "| Check | Result | Detail |"
  echo "|---|---|---|"
} > "$REPORT"

# ---- throwaway secrets (never printed) -------------------------------------------------------
new_secret() { openssl rand -hex 24; }
ADMIN_PW=$(new_secret); USER_PW=$(new_secret); SUPPORT_PW=$(new_secret)
for v in "$ADMIN_PW" "$USER_PW" "$SUPPORT_PW"; do echo "::add-mask::$v"; done
umask 077
printf 'NEXTCLOUD_ADMIN_PASSWORD=%s\n' "$ADMIN_PW" > "$TMP/server.env"
printf 'user = "citest:%s"\n' "$USER_PW" > "$TMP/curl/citest.cfg"
printf 'user = "nourax-support:%s"\n' "$SUPPORT_PW" > "$TMP/curl/support.cfg"
# What may never appear in a report (the planted fakes of the test, plus the throwaway password).
{
  printf '%s\n' "$USER_PW" "$ADMIN_PW" "$SUPPORT_PW"
  printf '%s\n' "planted.user@example.org" "Fake9Session8Passphrase7Value6Zz5Yy4Xx3" "Fk4pA-Pp9wD-Zz2Qq-Rr7Tt-Uu1Vv"
  printf '%s' "citest:$USER_PW" | base64 -w0; echo
} > "$TMP/forbidden.txt"
umask 022

# ---- the server ---------------------------------------------------------------------------------
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
  -e NEXTCLOUD_TRUSTED_DOMAINS="$SERVER localhost" "$SERVER_IMAGE" > /dev/null

for i in $(seq 1 90); do
  if curl -fsS http://127.0.0.1:8080/status.php 2> /dev/null | grep -q '"installed":true'; then break; fi
  sleep 5
done
VERSION=$(curl -fsS http://127.0.0.1:8080/status.php | python3 -c 'import json,sys; print(json.load(sys.stdin)["versionstring"])')
check server-version "$([ "$VERSION" = 35.0.2 ] && echo 0 || echo 1)" "versionstring $VERSION"

occ() { docker exec -u www-data "$SERVER" php occ "$@"; }
OC_PASS="$USER_PW" docker exec -u www-data -e OC_PASS "$SERVER" php occ user:add --password-from-env --display-name "CI Test" citest > /dev/null
check user-citest 0 "created; nourax-support does NOT exist yet"

# ---- the test ------------------------------------------------------------------------------------
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/src" -w /src "$QT_IMAGE" bash -c "
  cmake -S . -B build-report -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_BUILD_TYPE=Debug -DQT_MAJOR_VERSION=6 -DQUICK_COMPILER=ON -DBUILD_TESTING=1 \
    -DMIRALL_VERSION_BUILD=$BUILD_NUMBER > /tmp/configure.log 2>&1 || { tail -n 40 /tmp/configure.log; exit 1; }
  cmake --build build-report --target NoDriveProblemReportServerTest 2>&1 | tail -n 5"
test -x build-report/bin/NoDriveProblemReportServerTest
pass build-test "NoDriveProblemReportServerTest built in $QT_IMAGE"

printf 'NODRIVE_TEST_USER=citest\nNODRIVE_TEST_PASSWORD=%s\n' "$USER_PW" > "$TMP/test.env"

run_test() { # mode expect
  rm -rf "$TMP/work/pending"
  docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp --network "$NET" \
    --add-host drive.nourax.org:127.0.0.1 --env-file "$TMP/test.env" \
    -e NODRIVE_TEST_SERVER_URL="http://$SERVER" -e NODRIVE_TEST_WORKDIR=/work \
    -e NODRIVE_TEST_MODE="$1" -e NODRIVE_TEST_EXPECT="$2" -e QT_QPA_PLATFORM=offscreen \
    -v "$PWD:/src" -v "$TMP/work:/work" -w /src/build-report/bin "$QT_IMAGE" \
    ./NoDriveProblemReportServerTest > "$TMP/test-$1.log" 2>&1 || {
      echo "test mode $1 failed:"; sed -E 's/(password=)[^ ]*/\1<hidden>/' "$TMP/test-$1.log" | tail -n 60; return 1; }
  grep -E '^(PASS|FAIL|SKIP|Totals)' "$TMP/test-$1.log" || true
}

DATA="/var/www/html/data/citest/files/Problem reports"
list_zips() { docker exec "$SERVER" sh -c "ls -1 '$DATA' 2>/dev/null | grep -c '\.zip\$' || true"; }
shares_of_folder() { # prints the number of shares of the folder as seen by citest
  curl -fsS -K "$TMP/curl/citest.cfg" -H 'OCS-APIRequest: true' \
    'http://127.0.0.1:8080/ocs/v2.php/apps/files_sharing/api/v1/shares?path=/Problem%20reports&format=json' \
    | python3 -c 'import json,sys; d=json.load(sys.stdin)["ocs"]["data"]; print(len(d))'
}
share_details() {
  curl -fsS -K "$TMP/curl/citest.cfg" -H 'OCS-APIRequest: true' \
    'http://127.0.0.1:8080/ocs/v2.php/apps/files_sharing/api/v1/shares?path=/Problem%20reports&format=json' \
    | python3 -c '
import json,sys
d=json.load(sys.stdin)["ocs"]["data"]
print(";".join("type=%s with=%s perm=%s" % (s["share_type"], s["share_with"], s["permissions"]) for s in d))'
}
check_zip() { # file name inside the server folder
  docker cp "$SERVER:$DATA/$1" "$OUT/zips/$1" > /dev/null
  python3 tools/ci/check-report-zip.py "$OUT/zips/$1" --server-host "$SERVER" --forbid "$TMP/forbidden.txt" --expect-text "session start"
}
sha_of() { docker exec "$SERVER" sha256sum "$DATA/$1" | cut -d' ' -f1; }

# ---- phase 1: no Support user ---------------------------------------------------------------------
say; say "## Phase 1: nourax-support does not exist"; say
run_test manual not-shared
check p1-one-zip "$([ "$(list_zips)" = 1 ] && echo 0 || echo 1)" "zips in /Problem reports: $(list_zips)"
FIRST=$(docker exec "$SERVER" sh -c "ls -1 '$DATA' | grep '\.zip\$' | head -n1")
case "$FIRST" in *_linux_*.zip) check p1-file-name 0 "$FIRST" ;; *) check p1-file-name 1 "$FIRST" ;; esac
if out=$(check_zip "$FIRST" 2>&1); then pass p1-zip-content "$out"; else fail p1-zip-content "$out"; fi
check p1-no-share "$([ "$(shares_of_folder)" = 0 ] && echo 0 || echo 1)" "shares of the folder: $(shares_of_folder)"
FIRST_SHA=$(sha_of "$FIRST")
say; say "first zip sha256: $FIRST_SHA"

# ---- phase 2: Support user exists ---------------------------------------------------------------
say; say "## Phase 2: nourax-support exists"; say
OC_PASS="$SUPPORT_PW" docker exec -u www-data -e OC_PASS "$SERVER" php occ user:add --password-from-env --display-name "Nourax Support" nourax-support > /dev/null
run_test manual shared
check p2-two-zips "$([ "$(list_zips)" = 2 ] && echo 0 || echo 1)" "zips: $(list_zips)"
check p2-first-unchanged "$([ "$(sha_of "$FIRST")" = "$FIRST_SHA" ] && echo 0 || echo 1)" "sha256 of the first zip is the same"
check p2-one-share "$([ "$(shares_of_folder)" = 1 ] && echo 0 || echo 1)" "$(share_details)"
check p2-read-only "$([ "$(share_details)" = "type=0 with=nourax-support perm=1" ] && echo 0 || echo 1)" "share_type 0, share_with nourax-support, permissions 1"
code=$(curl -sS -o /dev/null -w '%{http_code}' -K "$TMP/curl/support.cfg" -X PROPFIND -H 'Depth: 1' 'http://127.0.0.1:8080/remote.php/dav/files/nourax-support/Problem%20reports/')
check p2-support-can-list "$([ "$code" = 207 ] && echo 0 || echo 1)" "PROPFIND as nourax-support: HTTP $code"
code=$(curl -sS -o /dev/null -w '%{http_code}' -K "$TMP/curl/support.cfg" -X PUT --data 'not allowed' 'http://127.0.0.1:8080/remote.php/dav/files/nourax-support/Problem%20reports/support-write-test.txt')
check p2-support-cannot-write "$([ "$code" != 201 ] && [ "$code" != 204 ] && echo 0 || echo 1)" "PUT as nourax-support: HTTP $code"
for z in $(docker exec "$SERVER" sh -c "ls -1 '$DATA' | grep '\.zip\$'"); do
  if out=$(check_zip "$z" 2>&1); then pass "p2-zip-$z" "$out"; else fail "p2-zip-$z" "$out"; fi
done

# ---- phase 3: more reports, no duplicate share, no overwrite -------------------------------------
say; say "## Phase 3: no duplicate share, nothing replaced"; say
run_test manual shared
run_test same-second shared
SAME=$(docker exec "$SERVER" sh -c "ls -1 '$DATA' | grep '^2026-10-28_10-00-00_' | head -n1")
SAME_SHA=$(sha_of "$SAME")
run_test same-second shared
SECOND_NAME="${SAME%.zip}_2.zip"
check p3-numbered-name "$(docker exec "$SERVER" test -f "$DATA/$SECOND_NAME" && echo 0 || echo 1)" "$SECOND_NAME exists"
check p3-same-second-unchanged "$([ "$(sha_of "$SAME")" = "$SAME_SHA" ] && echo 0 || echo 1)" "the zip with the forced name was not replaced"
check p3-first-unchanged "$([ "$(sha_of "$FIRST")" = "$FIRST_SHA" ] && echo 0 || echo 1)" "sha256 of the first zip is still the same"
check p3-five-zips "$([ "$(list_zips)" = 5 ] && echo 0 || echo 1)" "zips: $(list_zips)"
check p3-one-share "$([ "$(shares_of_folder)" = 1 ] && echo 0 || echo 1)" "shares of the folder: $(shares_of_folder)"

# ---- phase 4: no account, then the account connects ----------------------------------------------
say; say "## Phase 4: a report made with nobody signed in waits and is sent later"; say
run_test offline-then-online shared
check p4-six-zips "$([ "$(list_zips)" = 6 ] && echo 0 || echo 1)" "zips: $(list_zips)"
cat "$TMP/work/result.txt" | tee -a "$REPORT" > /dev/null

# ---- what the server saw ---------------------------------------------------------------------------
docker logs "$SERVER" 2>&1 | grep -E '"(MKCOL|PUT|PROPFIND|POST|GET) [^"]*(Problem|files_sharing)[^"]*"' \
  | sed -E 's/^[^"]*"([A-Z]+) ([^ ?]*)[^"]*" ([0-9]+).*/\1 \2 -> \3/' | sort | uniq -c > "$OUT/server-requests.txt" || true
say; say "Requests the server saw (method, path, status; counts in server-requests.txt)."
check no-other-methods "$(grep -Eq ' (DELETE|MOVE|COPY|PROPPATCH) ' "$OUT/server-requests.txt" && echo 1 || echo 0)" "no DELETE, MOVE or COPY on the report folder"

# ---- no throwaway password anywhere in what is uploaded -------------------------------------------
LEAK=0
while IFS= read -r value; do
  [ -n "$value" ] || continue
  if grep -rqF -- "$value" "$OUT"; then LEAK=1; fi
done < "$TMP/forbidden.txt"
check no-secret-in-artifact "$LEAK" "throwaway passwords and planted fakes were searched in the artifact folder"

docker rm -f "$SERVER" > /dev/null 2>&1 || true
docker network rm "$NET" > /dev/null 2>&1 || true
cat "$REPORT" >> "${GITHUB_STEP_SUMMARY:-/dev/null}" || true
exit "$FAILED"
