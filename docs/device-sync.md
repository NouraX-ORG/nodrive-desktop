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
- The password is held in a `QByteArray` in memory only and overwritten with zeros after use. It is never logged
  (the flow logs status codes only), never shown and never saved. It goes straight into the profile.
- **It didn't work** revokes it: `DELETE /ocs/v2.php/core/apppassword`, signed in with that new password. **Installed**,
  closing the window, a timeout or quitting just forget it (the Mac keeps it in its profile).
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
| `NoDriveDeviceSyncProfileTest` (all three systems) | valid XML with exactly the Apple keys and types, principal URLs, stable identifier and fresh UUIDs, no "Nextcloud", ports and path prefix, incomplete input, the private file (0600 in 0700) and its removal, the removal note; **macOS: `plutil -lint`** |
| `NoDriveDeviceSyncFlowTest` | our user agent, polling over 404, a matching server, a mismatching server refused, the start answer pointing elsewhere refused, timeout, cancel, revoke with Basic auth of the new password; the planted password in no log line, no setting, no file |
| `NoDriveDeviceSyncUiTest` | the panel on the real account page (macOS and the other variant), disabled state, the three Mac steps end to end against a scripted server, the Thunderbird guide texts, **the clipboard after each copy button holds exactly the address**, the removal note; screenshots in light and dark in `nodrive-screenshots/devicesync/` (`account-panel-mac`, `account-panel-other`, `mac-step1`, `mac-step3`, `thunderbird-guide`, `removal-note`) |
| `NoDriveDeviceSyncServerTest` (Linux CI job "Device calendar and contacts sync against a throwaway server", `tools/ci/devicesync-server.sh`) | a real Nextcloud 35 in Docker; Chromium signs in as the throwaway person and grants access; the poll returns a password; `occ user:auth-tokens:list` shows a token named "NoDrive Calendar & Contacts (ci)"; that password does a CalDAV and a CardDAV PROPFIND (207); the profile is built for the host; "It didn't work" revokes it and the same PROPFIND is 401 |

## Owner test rows

macOS:
1. Settings > account > **Add to my Mac's Calendar & Contacts** > allow access in the browser > install the profile in System Settings (check both paths: macOS 13/14 "Privacy & Security > Profiles", macOS 15 and newer "General > Device Management").
2. Calendar, Reminders and Contacts show the NoDrive calendars, lists and contacts.
3. An event made on the Mac appears in NoAgenda and on the web, and the other way round.
4. An alarm fires.
5. Edit a tagged event in macOS Calendar, then check its agent in NoAgenda.
6. Remove the NoDrive account: the note about removing the profile appears.

Windows and Linux: follow the Thunderbird guide (Thunderbird 102 or newer); calendars and contacts appear; the clipboard never held a password from NoDrive.

## Open points

- Thunderbird's menus can change; the guide names them plainly (no automated test is possible).
- The profile is unsigned ("Unverified"): signing needs a real certificate.
- New texts are English only; translations are a to-do.
- Server side (not done here): login flow v2 and app-password creation must stay enabled for normal users (plan server note 13).
