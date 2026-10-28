# Problem reports (NoDrive Desktop)

Round 3, contract section B (`plans/round3-contract.md`), plan `plans/round3-desktop-plan.md`
section 1. Works the same on Windows, macOS and Linux.

## 1. What the user sees

**Settings > Advanced**, directly above the row with "Edit Ignored Files" and "Create Debug Archive":

- a switch **"Send problem reports to Nourax"**, **off by default**, with the text
  "If the app has a problem, send a report to Nourax Support so we can fix it. Reports are saved in
  your NoDrive and shared with Nourax Support. They never include passwords.";
- a button **"Report a problem"** right after "Create Debug Archive". It always works, with the
  switch off and before sign-in. The dialog has an optional box "What went wrong?", the link
  "What's included" (a plain-words list) and the buttons Send and Cancel.

**After a crash or an unclean exit**, the report is built at once at start-up (before any log
rotation) into `<config folder>/problem-reports/pending/awaiting-answer/`. About 3 seconds after the
start:

- switch **on**: the held report is moved to `pending/` and sent, then a tray message "Problem report
  sent" (or "Saved in your NoDrive > Problem reports." if the share to Support failed);
- switch **off**: the question "NoDrive closed unexpectedly. Send a problem report?" with Send
  (default) and Not now (Escape). It is asked once: either answer closes the case. Send moves the held
  report to `pending/` and sends it; Not now removes the held copy. Nothing in `awaiting-answer/` is
  ever sent by a reconnect, so a report the user has not agreed to cannot leave the computer.

Other results: "Saved on this computer. NoDrive sends it after you sign in." (nobody is signed in) and
"Could not send the problem report now. NoDrive will try again when it is connected." (the network is
down, or the account is signed in but not connected yet). For an automatic report (switch on, or Send
on the question) there is no interim message while the account is signed in but not connected yet
(start-up, offline, maintenance): the report waits in `pending/` and the one message "Problem report
sent" appears when the account connects.
All texts are English; German and Arabic show English until they are translated (to-do).

## 2. Where it goes (the account's own connection only)

1. `MKCOL` of `Problem reports` (201 = created, 405 = exists) in the user's own files.
2. `PUT` of `Problem reports/<YYYY-MM-DD_HH-MM-SS>_<platform>_<app_version>.zip` with
   `If-None-Match: *`. An existing file makes the server answer 412; the next free name
   (`..._2.zip` to `..._9.zip`) is used. **Nothing on the Drive is ever deleted, moved or replaced.**
3. `GET` the shares of the folder. If the user `nourax-support` already has one: done.
4. Otherwise one `POST` to the OCS Share API: `shareType=0`, `shareWith=nourax-support`,
   `permissions=1` (read only). If it fails (for example the account does not exist yet), the report
   stays saved, the app says "Saved in your NoDrive > Problem reports." and there is no retry loop;
   the next report tries once more. When several waiting reports are sent together, the share is
   checked or created once for the whole batch.

All requests use the account's own network manager, credentials and host. Nothing else is contacted:
no analytics, no telemetry, no background pings, no timer. With nobody signed in, the zip waits in
`<config folder>/problem-reports/pending/` (newest 5 kept) and is sent when an account becomes
connected. `OcsShareJob::createShare()` ignores its `permissions` argument, so a small job of our own
(`ProblemReportShareJob`) sends `permissions=1`.

## 3. The report

One zip, at most **5,000,000 bytes** (the oldest log lines are dropped first):

| Entry | Content |
|---|---|
| `report.json` | `schema` 1, `kind` (`crash`, `manual`, `unclean-exit`), `app` "NoDrive Desktop", `app_version`, `build`, `platform` (`windows`, `macos`, `linux`), `os_version`, `device_model`, `locale`, `created_utc`, `server_host`, `user_note` |
| `crash.txt` | only for a crash: the crash marker line, the qFatal log and the last 200 lines of the previous session's log |
| `logs/` | every file of the log folder (`.gz` files of earlier sessions are read), newest first, at most 40 MB of text read |
| `config/nodrive.cfg`, `_client_buildinfo.txt`, `_client_parameters.txt` | the same sources as "Create Debug Archive" |

The sync journal databases and the macOS File Provider `*.realm` files are left out on purpose: they
are binary, cannot be scrubbed and can be very large. "Create Debug Archive" still includes them.
`server_host` is the host of the signed-in account at the time the report is made (also when the
account is not connected yet). It is empty only when no account exists; the report is not rewritten
when it is sent, because the folder it lands in already belongs to the account's own server.

## 4. Scrubbing (always, before zipping)

`src/gui/nourax/problemreportscrubber.cpp`, tested with fixtures in `test/nodrive/problemreports/`
and with 200 random secrets of each kind. Removed: PEM private keys, e2ee fields (`mnemonic`,
`privateKey` / `private-key`, `metadataKey` / `metadataKeys` (also as an object), `passphrase`, ...;
names may use `-` or `_`; an unquoted `mnemonic` or `passphrase` is removed up to the next separator,
not just its first word), `Authorization`, `Proxy-Authorization`, `Cookie` and
`Set-Cookie` values (also a Set-Cookie value that a log printed on its own line, `name=value; path=/;
HttpOnly`), `Basic` and `Bearer` values, session and instance cookies by name
(`oc_sessionPassphrase`, `nc_session_id`, `__Host-*`, `oc<10-14 characters>`), URL query and form
values named `token`, `password`, `pass`, `secret`, `key`, `sig`, `code`, `state`, `user:password@`
in URLs, JSON and key=value fields for passwords and tokens, `--apppassword <value>`, JWTs, any run
of 32 or more letters and digits that has upper case, lower case and a digit, and the grouped form
`XXXXX-XXXXX-XXXXX-XXXXX-XXXXX`. Email addresses become `<email>` (retina image names such as `icon@2x.png` stay). In `nodrive.cfg` every key that
contains `pass`, `token`, `secret`, `privatekey`, `mnemonic` or `cookie` loses its value.
**File and folder names are kept** because they are needed to find the problem; the "What's
included" text says so.

## 5. How an unclean exit is found

`<config folder>/problem-reports/run.marker` (JSON) says `running` while the app runs and `clean`
after it quits (`aboutToQuit`). Markers are only overwritten, never deleted.

| Old marker | Result |
|---|---|
| none, or `clean` | nothing |
| `running` from another build | nothing (an update ends the app: the MSI kills it) |
| `running`, same build, with a crash marker or qFatal log newer than the start | crash |
| `running`, same build, no evidence | unclean exit (power loss, killed, ...) |

SIGTERM, SIGINT and SIGHUP (Unix, for example logout) rewrite the marker as `clean` and then run the
default action. Fatal signals (Unix) and unhandled exceptions (Windows) write `crash.marker` and then
let the operating system's own crash handling run. The crash reporter of upstream stays off.

## 6. Code map

| File (`src/gui/nourax/`) | Job |
|---|---|
| `problemreportsettings` | the switch (`[NoDrive] problemReportsEnabled` in `nodrive.cfg`, missing = off), names, folders |
| `problemreportscrubber` | scrubbing |
| `runmarker` | run marker, crash and signal handlers |
| `problemreportbuilder` | the zip, the 5 MB cap, newest 5 kept |
| `problemreportsharejob`, `problemreportuploader` | the OCS share with `permissions=1`, the upload |
| `problemreportmanager` | start-up check, prompt, queue, sending when an account connects |
| `problemreportdialog`, `problemreportspanel` | the dialog, the switch and the button |

Hooks in upstream files (one marked line each, kept by `tools/rebrand.d/problemreports.py`, which
`rebrand.py --check` runs in CI): `src/gui/CMakeLists.txt`, `application.cpp`, `advancedsettings.cpp`,
`test/CMakeLists.txt`. The Settings dialog styles its panels by object name, so the new panel shares
the object name `advancedGroupBox`; after an upstream change to that style sheet the UI test fails.

## 7. Tests

CI results and the screenshots (`docs/screenshots/<os>/problem-reports/`) are in [testing.md](testing.md) section 1.1a; the independent review (rounds 1 and 2) approved the change.

| Test | What it covers | Where |
|---|---|---|
| `NoDriveProblemReportScrubber` | one fixture per rule, "keep" fixture, idempotence, 200 random secrets of each kind | ctest, 3 OSes |
| `NoDriveProblemReport` | `report.json` fields, file name, `.gz` logs, 5 MB cap with the newest line kept, newest 5 kept, `_2` names, all rows of the run marker table, "ask once", real crashes of a helper process (segmentation fault, abort, SIGTERM) | ctest, 3 OSes |
| `NoDriveProblemReportUpload` | the upload against upstream's in-process fake server: MKCOL, `If-None-Match`, 412, share body, one share attempt per batch, pending queue, an account that is signed in but not connected (no interim message, one "Problem report sent" later), the held report of a crash (built at once, sent only on Send), only own host and MKCOL/PUT/GET/POST | ctest, 3 OSes |
| `NoDriveProblemReportUi` | exact texts, switch off by default and saved, button position, no "Nextcloud" text, screenshots light and dark with pixel checks | ctest, 3 OSes |
| `NoDriveProblemReportServer` | the same against a real Nextcloud 35.0.2 (job `report-server` in `linux.yml`, four phases, `tools/ci/report-server.sh`) | Linux only (no Docker on the Windows and macOS runners) |
| Smoke scripts (`tools/screenshots/*`) | the real packaged app: question after a killed run, Not now, no second question, Send, a scrubbed report in `pending/` | 3 OSes, light and dark |

## 8. Known limits and to-dos

- If the app is closed within 3 seconds of a start that follows an unclean exit, the case is dropped
  without a question.
- A crash during start-up, before the manager runs, is not detected.
- The folder `Problem reports` may already exist as a user folder; then it is shared with Support as
  it is (flagged in the plan, risk R3).
- The HTTP log in its quoted form: if `sync.httplogger` is switched on by hand (`QT_LOGGING_RULES` or
  `qtlogging.ini`; no app setting does it), `HttpLogger` writes JSON bodies with escaped quotes
  (`\"private-key\":\"...\"`). The scrubber does not yet remove short values in that form (the
  initialisation vector, the salt, short base64 pieces, short `password` / `token` values); long app
  passwords and login tokens are still caught by the long-run rule. Do not send a report made with
  manual HTTP logging on. To-do: accept an optional backslash before the quotes and add a fixture from a
  real `HttpLogger` line.
- A report the user has not answered stays on disk: if the app ends while the question is open, or
  within 3 seconds of the start, the scrubbed held copy stays in `pending/awaiting-answer/` (never sent,
  inside the app's own folder) until the next unclean exit. To-do: clear `awaiting-answer/` in `start()`
  when no case is open.
- "Report a problem" after the user signed out, or when the account needs its password again, says
  "will try again when it is connected" instead of the sign-in text. The report is still sent after the
  next sign-in; only the wording is a to-do.
- Like "Create Debug Archive", the report reads every file of the log folder. A user who points
  `--logdir` at a general folder would have its files (scrubbed) uploaded. The default and temporary log
  folders are dedicated, so this is unlikely. To-do: read only `*.log` and `*.log.gz`.
- No test drives the start-up path "switch on, unclean exit, sent without a question" end to end (the
  unit tests call the answer directly, the smoke scripts do not seed the switch). To-do: a smoke block.
- The unit test's screenshots were not collected by CI on macOS (the other two systems upload them).
- Translations (German, Arabic) are a to-do. The folder name `Problem reports` stays English.
- The Windows and macOS runners cannot run the real-server test; the fake-server tests and the owner
  tests cover them.
- Owner tests: sign in; Settings > Advanced shows the switch off; "Report a problem" with a note says
  "Saved in your NoDrive > Problem reports." until Workspace creates `nourax-support`, and the zip is
  in the web UI; log out and in with NoDrive running gives no question; ending NoDrive in Task Manager
  gives the question at the next start; Not now is not asked again; with the switch on, "Problem
  report sent".
