# Testing NoDrive for Linux

How the Linux build is tested, and the results. CI file: `.github/workflows/linux.yml`.

## What CI runs

| Job | What it does |
|---|---|
| Branding gate | `tools/rebrand.py --check` (the rebrand is complete) and `tools/check-branding.py --lane linux` (no unclassified Nextcloud name, link, colour or logo in shared and Linux files; colours are also found when written as numbers, for example `rgb(0,130,201)` in an SVG or `QColor(0, 103, 158)` in code) |
| Build and unit tests | Debug build with tests in upstream's public Qt CI image, then `ctest` under Xvfb as a normal user. Skipped tests and the reason for each: `tools/branding/ctest-skip-linux.txt` |
| AppImage | `admin/linux/build-appimage.sh` (as upstream, NoDrive names) → artifact `nodrive-linux-appimage` |
| Smoke test | `tools/screenshots/linux.sh`: starts the AppImage under Xvfb with a fresh HOME, takes the screenshots below, clicks "Log in" with a fake browser that only records the URL, and logs DNS (tcpdump) and the app's `connect()` calls (strace). Then checks the package: `.desktop` file, icon, licence files, and `check-branding.py --artifact` over the unpacked AppImage (text files, compiled translations, URLs inside the binaries). Artifact `nodrive-linux-screenshots` |

A smoke-only run can reuse an AppImage from an earlier run: *Actions → Linux → Run workflow → reuse_run = <run id>*.

## Results

Before sign-in: CI. After sign-in: the owner, at home (Cloudflare only lets the owner's home network reach the Drive login page).

| Feature | Who | Result | Evidence |
|---|---|---|---|
| Starts | CI | PASS | 8 starts (4 variants × 2 runs), wizard window 720×420 every time (`results.tsv`: `launch-*`) |
| Shows NoDrive's name | CI | PASS | Window title "NoDrive", "Log in to NoDrive", `NoDrive version 35.0.0-nourax.1 (build N)`, `--help` without "Nextcloud"; `.desktop` file `org.nourax.nodrive.desktop.nodrive.desktop` with `Name=NoDrive Desktop` |
| Shows NoDrive's icon | CI | PASS | Nourax logo in the tray and the title bar (screenshots); `usr/share/icons/hicolor/512x512/apps/NoDrive.png` has the same SHA-256 as `theme/colored/512-NoDrive-icon.png` from `tools/make-icons.py` |
| Shows NoDrive's colours | CI | PASS | Pixel check finds the "Log in" button in `#2563EB` in every variant; unit test `NoDriveBrandingTest` draws a switched-on Settings switch and finds `#2563EB` (upstream: Nextcloud's `#00679E`) |
| Server fixed, no server-entry screen | CI | PASS | First page has no address field, no "Sign up" / "Self-host" (screenshots 01); unit test `NoDriveBrandingTest` (`serverUrlEditable() == false`, server `https://drive.nourax.org`); provisioning tests check that another `--serverurl` is refused |
| Sign-in opens the browser at drive.nourax.org | CI | PASS | The app handed `https://drive.nourax.org/login/v2/flow/<token>` to the browser in all 4 variants (token redacted in the logs). The page itself is not loaded in CI |
| No Nextcloud service contacted | CI | PASS | DNS names during the whole test: only `drive.nourax.org` and the runner's own GitHub hosts. The app connected only to drive.nourax.org (after "Log in") and the local DNS resolver (`net/network-report.md`) |
| No Nextcloud file in a sync folder | CI | PASS | The folder watcher's probe files are `.nodrivesync.log` and `.nodrivepermissions.log` (upstream: `.nextcloudsync.log`, which stayed hidden in every sync folder). `NoDriveBrandingTest` checks that the new and the old names are excluded from sync; `check-branding.py` fails if the old names appear anywhere except the exclude list |
| Dark mode (before sign-in) | CI | PASS | Wizard background `#202124` with the desktop set to dark (XDG portal `color-scheme` = prefer dark) |
| Second language: German | CI | PASS | Screenshots `de-light` |
| Right-to-left: Arabic | CI | PASS, with a note | Layout is mirrored (buttons, menus). Several new wizard texts have no Arabic translation upstream yet and show in English |
| Unit tests | CI | PASS | `100% tests passed, 0 tests failed out of 104` (commit `14ebedb5c4`, run [40736612257](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40736612257)); skipped: `UpdateChannelTest` (no update channels in branded builds; no updater) |
| Branding sweep of the AppImage | CI | PASS | Only legal text (licence files, Legal Notice copyright line in the translations) and one stale, never-shown translation are kept, all listed in `tools/branding/allow-shared.txt` |
| Problem reports: unit tests | CI | PASS | `NoDriveProblemReportScrubberTest`, `NoDriveProblemReportTest`, `NoDriveProblemReportUploadTest`, `NoDriveProblemReportUiTest`, `NoDriveProblemReportServerTest` all passed (run [40736612257](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40736612257)); details in [problem-reports.md](problem-reports.md) section 7 |
| Problem reports: real app, killed run | CI | PASS | `results.tsv`: `prompt-`, `not-now-`, `ask-once-`, `marker-running-`, `sigterm-clean-`, `report-` PASS in `en-light` and `en-dark`; zip `2026-10-28_17-42-18_linux_35.0.0-nourax.1.zip` (8763 bytes, 10 entries) passed `tools/ci/check-report-zip.py` (no planted secret left) |
| Problem reports: against a real Nextcloud 35.0.2 (throwaway) | CI | PASS | Job `report-server`, artifact `nodrive-linux-report-server`: 4 phases; folder and zip created, nothing replaced, no share until `nourax-support` exists, then one share with `permissions=1`; Support lists but cannot write (HTTP 207 / 403); no DELETE, MOVE or COPY seen; an offline report is sent later |
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

Offline files and automatic photo upload are phone features; they do not apply to the desktop app.

## Screenshots

Folder `docs/screenshots/linux/<variant>/`. Variants: `en-light`, `de-light`, `ar-light` (Arabic, right to left), `en-dark`.

| File | Screen |
|---|---|
| `01-wizard.png` | First start: "Log in to NoDrive" with the Log in button only |
| `02-tray-menu.png` | Tray icon menu without an account (Add account, Settings, Quit) |
| `03-proxy-settings.png` | Proxy settings dialog of the wizard |
| `04-sign-in.png` | After "Log in": "Switch to your browser" |
| `problem-reports/*.png` | Settings > Advanced with the new switch and button, the "Report a problem" dialog, "What's included", the question after an unclean exit (from the unit test and from the real app); see [testing.md](testing.md) section 3 |

Not captured yet: the Settings window (General, Network, Info). "Settings" in the tray menu opens it even without an account (`Systray::openSettings` -> `ownCloudGui::slotShowSettings`); only a click on the tray icon itself brings the wizard back. These screens were reviewed in code instead ([branding-sweep.md](branding-sweep.md), section 5), and adding them to the smoke test is a to-do. Until then they are part of the owner tests.

Notes:
- The window frame and title bar come from the test's window manager (openbox, theme Onyx), not from NoDrive.
- In `en-dark`, the tray menu stays light: the test desktop gives Qt's widget menus a light palette. The NoDrive windows follow dark mode. The owner's dark-mode test after sign-in checks this on a real desktop.
- The screenshots were taken in run 40163272940 from the AppImage of run 40162847566 (commit 8050d10ce6). The app's code has not changed since then; later commits changed only CI, tools and docs.
