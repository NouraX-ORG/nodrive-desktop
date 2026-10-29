# Testing NoDrive for Windows

How the Windows build is tested, and the results. CI file: `.github/workflows/windows.yml`.
How it is built: [build-windows.md](build-windows.md).

## What CI runs

| Job | What it does |
|---|---|
| Branding checks | `tools/rebrand.py --check` (the shared rebrand and `tools/rebrand.d/windows.py` are applied), `tools/check-branding.py --lane windows` (no unclassified Nextcloud name, link, colour or logo in shared and Windows files), `tools/make-icons.py --check` |
| Craft dependencies | KDE Craft and every library, from the cache (about 1 minute when the cache is warm) |
| Build, MSI and unit tests | Builds the client with Craft, makes the MSI (artifact `nodrive-windows-msi`), sweeps the collected app folder (`check-branding.py --artifact`: text files, compiled translations, URLs inside every binary), then runs `ctest`. Skipped tests and the reason for each: `tools/branding/ctest-skip-windows.txt` |
| Install, start, screenshots, uninstall | `tools/screenshots/windows.ps1` on a fresh `windows-2022` runner (below), then the same sweep over the files inside the MSI (`msiexec /a`). Artifact `nodrive-windows-screenshots` (screenshots, `results.tsv`, logs, DNS list) |

`tools/screenshots/windows.ps1`, step by step:

1. Reads the MSI tables (Windows Installer COM, read only): product name, maker, upgrade code, help links, shortcut names, and every string in every table, also with each embedded installer language applied (de, fr, it, es). Then opens the installer's first page in English and German, takes a screenshot and cancels before anything is installed.
2. Silent install: `msiexec /i NoDrive-….msi /qn /l*v install.log`. Checks the install folder, the licence folder, the registry keys, the "Apps & features" entry, the Start-menu and desktop shortcuts, the Explorer integration (context-menu handler and overlay names and CLSIDs), the `nc://` handler, the version resources of all 228 `.exe`/`.dll` files, the app icon and `nodrivecmd --version`/`--help`.
3. First start with no settings: the config folder, the autostart entry and the named pipe of the Explorer integration.
4. Four variants, each started twice with a fresh config: `en-light`, `de-light`, `ar-light` (Arabic, right to left), `en-dark` (Windows set to dark apps). Run 1 takes the wizard, tray-menu and proxy-settings screenshots and asks the running app for its Explorer context-menu texts. Run 2 clicks "Log in": a fake browser (Image File Execution Options "Debugger" for the browsers) records the URL the app hands to Windows, so no browser opens. Every text the windows show to UI Automation is checked for "Nextcloud".
5. Network: the Windows DNS client log (`Microsoft-Windows-DNS-Client/Operational`) for the whole test, the app's TCP connections and the URLs in its logs. Fails on any Nextcloud host.
6. Silent uninstall: `msiexec /x … /qn`, then checks that the program files, shortcuts, registry keys, Explorer handlers, overlays, `nc://` handler and "Apps & features" entry are gone.

A test-only run can reuse an MSI from an earlier run: *Actions → Windows → Run workflow → reuse_run = &lt;run id&gt;*.

## Results

Before sign-in: CI. After sign-in: the owner, at home (Cloudflare only lets the owner's home network reach the Drive login page).

Runs: the MSI `NoDrive-35.0.0.5-nourax.1-x64.msi` and the unit tests come from run [40169919167](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40169919167); the screenshots in `docs/screenshots/windows/` and the results below come from the test-only run [40174557319](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40174557319), which installed that MSI. The app code has not changed since that build; later commits changed only CI, test scripts and docs. Every push to `platform/windows` and `main` runs the whole workflow again.

| Feature | Who | Result | Evidence |
|---|---|---|---|
| Installs silently | CI | PASS | `msiexec /i /qn` exit 0; `C:\Program Files\NoDrive`, 2058 files, `nodrive.exe` and `nodrivecmd.exe` |
| Starts | CI | PASS | 9 starts (first start + 4 variants × 2 runs), wizard window "NoDrive" 722×452 every time |
| Shows NoDrive's name | CI | PASS | Window title "NoDrive", "Log in to NoDrive", tray menu "Exit NoDrive", Start menu `NoDrive.lnk`, "Apps & features" entry "NoDrive" by "Nourax", installer "Welcome to the NoDrive Setup Wizard", `nodrivecmd --version` → `NoDrive version 35.0.0-nourax.1 (build 5)`, all version resources (product "NoDrive", company "Nourax"; 0 of 228 binaries name Nextcloud) |
| Shows NoDrive's icon | CI | PASS | Nourax logo in the title bar, taskbar, tray and on the installer page (screenshots); the 32 px icon of `nodrive.exe` matches `theme/colored/32-NoDrive-icon.png` from `tools/make-icons.py` (124 opaque pixels, 0 differ) |
| Shows NoDrive's colours | CI | PASS | Pixel check finds the "Log in" button in `#2563EB` in every variant |
| Server fixed, no server-entry screen | CI | PASS | First page has only "Log in to NoDrive", the Log in button and Proxy settings: no address field, no "Sign up" / "Self-host" (screenshots `01-wizard`, UI Automation text lists) |
| Sign-in opens the browser at drive.nourax.org | CI | PASS | In all 4 variants the app handed `https://drive.nourax.org/login/v2/flow/<token>?user=runneradmin` to Windows (token redacted). The page itself is not loaded in CI |
| No Nextcloud service contacted | CI | PASS | DNS names during the whole test: `drive.nourax.org` (the app, only after "Log in") and the runner's own GitHub hosts; the app's only TCP peer was drive.nourax.org (Cloudflare) on port 443 (`net/dns.txt`, `net/network-report.md`) |
| Registry keys and config folder are NoDrive's | CI | PASS | `HKLM\SOFTWARE\WOW6432Node\Nourax\NoDrive` (install dir, product code), `HKLM\SOFTWARE\Nourax\NoDrive`, `HKCU\Software\Nourax\NoDrive`; config `%APPDATA%\NoDrive\nodrive.cfg`; autostart `HKCU\…\Run\NoDrive`; no `Nextcloud GmbH` keys and no `Nextcloud` folders in AppData |
| Start-menu entry name | CI | PASS | `C:\ProgramData\Microsoft\Windows\Start Menu\Programs\NoDrive.lnk` → `C:\Program Files\NoDrive\nodrive.exe`; desktop `NoDrive.lnk` |
| Explorer integration names | CI | PASS | Context-menu handler `NoDriveContextMenuHandler` → `{3D056A87-6CB2-47ED-BB26-88619E5F1310}` "NoDrive context menu handler"; overlays `NoDriveError`, `NoDriveOK`, `NoDriveOKShared`, `NoDriveSync`, `NoDriveWarning` with the new GUIDs; none of Nextcloud's CLSIDs; pipe `\\.\pipe\nodrive-<user>`; the context-menu title the app serves is "NoDrive" (with the other menu texts in en, de, ar) |
| Installer screens | CI | PASS | English and German first page (screenshots `installer/`); all MSI strings in en, de, fr, it, es free of Nextcloud (7505 rows) |
| Dark mode (before sign-in) | CI | PASS | Wizard background `#202124` with Windows apps set to dark |
| Second language: German | CI | PASS | Screenshots `de-light` |
| Right-to-left: Arabic | CI | PASS, with a note | Layout mirrored (buttons, dialogs). The new wizard texts have no Arabic translation upstream yet and show in English; the tray menu is Arabic |
| Uninstalls silently and completely | CI | PASS | `msiexec /x /qn` exit 0; nothing left (files, shortcuts, HKLM keys, Explorer handlers and overlays, `nc://`, "Apps & features"). Kept, as for any app: the user's settings folder and the autostart value |
| Unit tests | CI | PASS | `100% tests passed, 0 tests failed out of 101` (includes `NoDriveBrandingTest`); skipped: `UpdateChannelTest` (no update channels in branded builds; no updater) |
| Branding sweep of the packaged files | CI | PASS | Inside the MSI: 61 hits, 0 unclassified; kept: licence texts (legal), the Legal Notice copyright line in the compiled translations (legal, never shown), a stale never-shown translation (dead code); listed in `tools/branding/allow-windows.txt` |
| Problem reports: unit tests | CI | PASS | The five `NoDriveProblemReport*Test` tests passed (run [40736612246](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40736612246)); `NoDriveProblemReportServerTest` skips itself without a server (real server: Linux job only) |
| Problem reports: real app, killed run | CI | PASS | `results.tsv`: `prompt-`, `not-now-`, `ask-once-`, `marker-running-`, `quit-clean-`, `report-` PASS in `en-light` and `en-dark`; `texts-*-unclean-prompt`: 18 UI Automation texts, none names Nextcloud; zip `2026-10-28_18-15-38_windows_35.0.0-nourax.1.zip` (10499 bytes, 12 entries) clean |
| Sync root and Explorer sidebar name ("NoDrive") | owner | pending owner test | Needs a signed-in account with a sync folder |
| NoAgenda: unit tests and the window in light and dark (mouse clicks, pixel checks, screenshots) | CI | PASS | `NoDriveAgenda*Test`: `100% tests passed, 0 tests failed out of 111` (commit `46f8a71e0b`, run [40809977148](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40809977148)) |
| File list | owner | pending owner test | |
| Upload, download, rename, move, delete | owner | pending owner test | |
| Public link with expiry and password | owner | pending owner test | |
| Sync folder and a conflict | owner | pending owner test | |
| Files on demand (if it loads) | owner | pending owner test | |
| Sign out | owner | pending owner test | |
| Dark mode (after sign-in) | owner | pending owner test | |
| Second language (after sign-in) | owner | pending owner test | |
| Next to the real Nextcloud app (optional) | owner | pending owner test | |
| Device name in the web page (Settings → Security → Devices) | owner | pending owner test | |
| Problem reports: Settings > Advanced shows "Send problem reports to Nourax", switched off | owner | pending owner test | |
| Problem reports: "Report a problem" with a note: "Saved in your NoDrive > Problem reports." and the zip is in that folder on the Drive | owner | pending owner test | |
| Problem reports: no question after logging out or off; the question after a hard end; "Not now" is asked once | owner | pending owner test | |
| Problem reports: switch on, hard end: no question, "Problem report sent" | owner | pending owner test | |
| Problem reports: dark mode readable | owner | pending owner test | |
| NoAgenda: the tray menu (left- and right-click) shows "NoAgenda" and it opens the window; the personal-calendars question; Today; tasks with their agent; My team; offline banner; dark mode | owner | pending owner test | |

Offline files and automatic photo upload are phone features; they do not apply to the desktop app.

## Screenshots

Folder `docs/screenshots/windows/`. Variants: `en-light`, `de-light`, `ar-light` (Arabic, right to left), `en-dark`.

| File | Screen |
|---|---|
| `installer/01-welcome-en.png`, `installer/01-welcome-de.png` | Installer, first page |
| `first-start/00-first-start.png` | First start with no settings |
| `<variant>/01-wizard.png` | "Log in to NoDrive" with the Log in button only |
| `<variant>/02-tray-menu.png` | Tray icon menu without an account (Add account, Settings, Help, Exit NoDrive) |
| `<variant>/03-proxy-settings.png` | Proxy settings dialog of the wizard |
| `<variant>/04-sign-in.png` | After "Log in": "Switch to your browser" |
| `app-icon-32.png` | The icon Windows shows for `nodrive.exe` |
| `problem-reports/*.png` | Settings > Advanced with the new switch and button, the "Report a problem" dialog, "What's included", the question after an unclean exit (from the unit test and from the real app); see [testing.md](testing.md) section 3 |

These are all the screens the app shows without an account: "Settings" in the tray menu opens the same wizard until an account exists.

Notes:
- The desktop, taskbar and window frames are the runner's (Windows Server 2022, "Test Mode" watermark).
- In `en-dark`, the tray menu stays light: Qt's widget menus use their own light palette here. The NoDrive windows follow dark mode. The owner's dark-mode test after sign-in checks this on a real desktop.
- The app writes its own log to `%APPDATA%\NoDrive\logs\<date>_nextcloud.log.0` (file name from the shared logger code, classified internal by the Linux lane). It is not shown in the app.
