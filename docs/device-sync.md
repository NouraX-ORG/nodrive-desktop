# Calendar and contacts on this computer (device sync)

Binding spec: `plans/noagenda-decisions.md`, section 10 (approved 2026-10-28). Platform plan: `plans/noagenda-desktop-plan.md`,
section 4. This page says what is built, where it is, and how it is tested. It is the fourth NoAgenda step; it needs no
NoGantt, Deck or registry.

## What the user gets

Settings, then the account page, shows a panel **Calendar and contacts** directly above "Log out / Remove account".

| System | What the panel offers |
|---|---|
| **macOS** | **Add to my Mac's Calendar & Contacts**, and a link **Use with Thunderbird...**. The first one installs a configuration profile with a CalDAV and a CardDAV account, so Apple's own sync does the rest (two-way, with alarms; to-do lists appear in Reminders) |
| **Windows and Linux** | **Use with Thunderbird...**: a short guide with buttons that copy the calendar address and the contacts address. No credential is ever copied |

The panel is disabled with "Connect to NoDrive first." while the account is not connected.

### macOS in three steps

1. "NoDrive will open your browser. Allow access there to create a separate password for this Mac's Calendar and Contacts.
   You won't see it, and NoDrive doesn't keep it." **Continue** / Cancel.
2. "Waiting for you to allow access in the browser..." with **Open the browser again** and Cancel. This is Login Flow v2:
   the person is normally already signed in there and clicks "Grant access".
3. "Now install the profile: open System Settings > General > Device Management (macOS 15 and newer; "Privacy & Security >
   Profiles" before), double-click "NoDrive Calendar & Contacts" in Downloaded, then click Install." Buttons
   **Installed** and **It didn't work**.

When the person removes the NoDrive account on a Mac where the profile was offered, a note shows how to remove the
profile again and mentions the password in the web Security settings.

## The separate password

- Made by Login Flow v2 (`POST /index.php/login/v2`, poll every 5 s, stop after 20 minutes) on a **plain
  `QNetworkAccessManager`** (not the client's `AccessManager`, so `HttpLogger` never sees a body), with the user agent
  `NoDrive Calendar & Contacts (<computer name>)`, which becomes the token's name in the web Security settings.
- The sign-in and poll addresses must lie on the account's own server, and the answer must name that server, or it is
  dropped with an error.
- Every request sets `ManualRedirectPolicy` (as the client's `AccessManager` does) and a redirect is a failure: Qt's default
  would follow an https to https redirect to any host and copy every header, so the revoke's `Authorization` or the poll
  token could have travelled to another server. The account's proxy is copied onto the flow's own manager.
- The browser may be signed in as somebody else. Before a profile is built, the granted login name must equal (case
  does not matter) the account's login or its DAV user; otherwise the new password is revoked, no profile is written and
  the dialog says "You allowed access as X in the browser. Sign in there as Y and try again." (An account that signed in
  with its e-mail address and a browser that signed in with the user id is accepted too, as long as one of the two
  names matches.)
- The password is held in a `QByteArray` in memory only and overwritten with zeros after use. It is never logged
  (the flow logs status codes only), never shown and never saved. It goes straight into the profile.
- **It didn't work** (and every failure after the password was made: another person in the browser, a profile that could
  not be written or opened) revokes it: `DELETE /ocs/v2.php/core/apppassword`, signed in with that new password. The
  flow, not the dialog, owns the request: it outlives the dialog until the DELETE has an answer (10 s at most, then it is
  aborted and counts as not done), and the window cannot be closed meanwhile. If the revoke is refused, redirected or
  unanswered, the dialog shows "NoDrive couldn't remove the separate password. Remove "NoDrive Calendar & Contacts
  (<computer>)" in Security settings on the web." with an **Open Security settings** button. **Installed**, closing the
  window or quitting just forget it (the Mac keeps the password in its profile). "It didn't work" and the failures
  leave the "profile offered" flag as it is: a profile from an earlier run, or one installed just before the button was
  pressed, may still be on the Mac, so removing the account later still shows the removal note. (A note when nothing is
  installed costs nothing.)
- The profile file is written into a fresh private folder (0700, file 0600), handed to the system, overwritten with zeros
  and deleted after the answer, after 10 minutes, or when NoDrive quits.

## The profile

NoDrive builds its own profile. The server's `/remote.php/dav/provisioning/apple-provisioning.mobileconfig` is not used:
it has no password keys, takes its name from theming and is plain text on http (the CI phase records its reply as evidence,
`server-apple-profile.txt` in the artifact). The keys are exactly those of Apple's documentation: `CalDAV` and `CardDAV`
`AccountDescription, HostName, Password, Port, PrincipalURL, UseSSL, Username`, plus the payload keys. The
`PayloadIdentifier` is `<bundle id>.davprofile.<hash of the account id>`, stable per account, so installing again replaces
the old profile. No "Nextcloud" anywhere. The profile is **unsigned**, so System Settings shows "Unverified" (owner to-do:
signing needs a real certificate).

## Where the code is

`src/gui/nourax/devicesync/` (namespace `OCC::Nourax::DeviceSync`), compiled by `src/gui/nourax/nourax.cmake`.

| File | Job |
|---|---|
| `appleprofilebuilder.*` | the `.mobileconfig` XML (pure) |
| `deviceapppasswordflow.*` | Login Flow v2 and the revoke |
| `macprofileinstaller.*` | the private profile file and its cleanup |
| `macsetupdialog.*` | the three macOS steps |
| `thunderbirdguidedialog.*` | the Thunderbird guide |
| `devicesyncpanel.*` | the panel on the account page |
| `devicesyncsettings.*`, `devicesyncservice.*` | the one remembered flag (profile offered) and the removal note, on `AccountManager::accountSyncConnectionRemoved` (not `accountRemoved`, which also fires when the app quits) |

Upstream hooks (marked `// NoDrive: device calendar and contacts`, applied by `tools/rebrand.d/devicesync.py`, checked by
`rebrand.py --check`): two lines in `src/gui/accountsettings.cpp` and one in `test/CMakeLists.txt`.

## Tests

| Test | Covers |
|---|---|
| `NoDriveDeviceSyncProfileTest` (all three systems) | the removal note through the real `AccountManager::accountSyncConnectionRemoved` signal (not on `accountRemoved`), valid XML with exactly the Apple keys and types, principal URLs, stable identifier and fresh UUIDs, no "Nextcloud", ports and path prefix, incomplete input, the private file (0600 in 0700) and its removal, the removal note; **macOS: `plutil -lint`** |
| `NoDriveDeviceSyncFlowTest` | our user agent, polling over 404, a matching server, a mismatching server refused, the start answer pointing elsewhere refused, timeout, cancel, revoke with Basic auth of the new password; **redirects are never followed (a real loopback server redirects every request to another host: nothing reaches it, with a plain manager as the control that does)**, a revoke without an answer, the flow deleting itself after a detached revoke; the planted password in no log line, no setting, no file |
| `NoDriveDeviceSyncUiTest` | the panel on the real account page (macOS and the other variant), disabled state, the three Mac steps end to end against a scripted server (also: a refused revoke and a revoke without an answer are shown with "Open Security settings" and the window stays open meanwhile; another person's grant is revoked and no profile is written), the Thunderbird guide texts, **the clipboard after each copy button holds exactly the address**, the removal note; screenshots in light and dark in `nodrive-screenshots/devicesync/` (`account-panel-mac`, `account-panel-other`, `mac-step1`, `mac-step3`, `thunderbird-guide`, `removal-note`) |
| `NoDriveDeviceSyncServerTest` (Linux CI job "Device calendar and contacts sync against a throwaway server", `tools/ci/devicesync-server.sh`) | a real Nextcloud 35 in Docker; Chromium signs in as the throwaway person and grants access; the poll returns a password; `occ user:auth-tokens:list` shows a token named "NoDrive Calendar & Contacts (ci)"; that password does a CalDAV and a CardDAV PROPFIND (207); the profile is built for the host; "It didn't work" revokes it and the same PROPFIND is 401 |

## Owner test rows

macOS:
1. Settings > account > **Add to my Mac's Calendar & Contacts** > allow access in the browser > install the profile in System Settings (check both paths: macOS 13/14 "Privacy & Security > Profiles", macOS 15 and newer "General > Device Management").
2. Calendar, Reminders and Contacts show the NoDrive calendars, lists and contacts.
3. An event made on the Mac appears in NoAgenda and on the web, and the other way round.
4. An alarm fires.
5. Edit a tagged event in macOS Calendar, then check its agent in NoAgenda.
6. Remove the NoDrive account: the note about removing the profile appears.
7. Start the setup, install the profile, then press **It didn't work**: the "NoDrive Calendar & Contacts (<computer>)" token disappears from Security settings on the web (and removing the NoDrive account afterwards still shows the removal note).
8. In the browser, sign in as another account than the one in NoDrive and allow access: NoDrive refuses it ("You allowed access as ... Sign in there as ..."), writes no profile, and the token disappears from that account's Security settings.

Windows and Linux: follow the Thunderbird guide (Thunderbird 102 or newer); calendars and contacts appear; the clipboard never held a password from NoDrive.

## Open points

- Quitting NoDrive during the revoke (10 s at most) destroys the flow and aborts the DELETE without telling the user; unlikely, because the window-modal dialog is open meanwhile. The token can then be removed in Security settings on the web.
- The identity check compares the login names (case-insensitive). An account added with its user id and a browser signed in with the e-mail address is refused although it is the same person; it fails safe (the password is revoked and the text names the login). Under Nourax sign-in the login is the user id. A later improvement: one `GET /ocs/v2.php/cloud/user` with the new password, comparing its id with the account's.
- **Seen while testing, not caused by this feature:** with Qt 6.11.2 (macOS CI) the client's `HttpLogger` (`src/libsync/httplogger.cpp`, category `sync.httplogger`) printed the `Authorization: Basic ...` header of a request unredacted in the test network manager; the cause was not investigated (the redaction compares the header name with `Authorization`, so the case of the name is the first thing to check). The device sync is not affected (its requests do not go through `HttpLogger`, and a test checks that its own log lines hold no credentials), but upstream's logs and the problem reports' scrubber should be looked at. The file was not changed here (upstream file).
- The account's approved certificates are not copied onto the flow's plain manager (the server is the fixed public one, so there are none to approve); the account's proxy is.
- Thunderbird's menus can change; the guide names them plainly (no automated test is possible).
- The profile is unsigned ("Unverified"): signing needs a real certificate.
- New texts are English only; translations are a to-do.
- The panel's service is created when an account page is built. A remote wipe (`src/gui/remotewipe.cpp`) in a run where Settings was never opened shows no removal note and leaves the flag in the config; account ids are reused, so a later account with the same id may show one harmless extra note. Fix when wanted: create the service at start-up from the Nourax-owned start point, as `AgendaService` is created (review round 3, N2).
- The CI artifact of the device-sync server phase keeps the Login Flow login tokens of the throwaway server (`login-url-A.txt`, `login-url-B.txt`, `server-requests.txt`). They are single-use and the server is gone; they could be redacted like the poll token (review round 3, N4).
- Server side (not done here): login flow v2 and app-password creation must stay enabled for normal users (plan server note 13).
