# Testing NoDrive for macOS

How the macOS build is tested, and the results. CI file: `.github/workflows/macos.yml`. How it is built: `docs/build-macos.md`.

## What CI runs

| Job | What it does |
|---|---|
| Branding gate | `tools/rebrand.py --check` (the rebrand, including `tools/rebrand.d/macos.py`, is complete) and `tools/check-branding.py --lane macos` (no unclassified Nextcloud name, link, colour or logo in shared and macOS files) |
| Build, test and package | KDE Craft build (classic sync, arm64), unit tests with `ctest`, then `admin/osx/nodrive-package.sh`: licence files, bundle fix-up and dependency check, ad-hoc signature, `.dmg`. Skipped tests and the reason for each: `tools/branding/ctest-skip-macos.txt` |
| Smoke test | `tools/screenshots/macos.sh`: mounts the `.dmg`, copies `NoDrive.app` to `/Applications`, checks every Info.plist, the entitlements, the signature, the icon and the licence files, then starts the app in four variants and takes the screenshots below. "Log in" is clicked with a fake browser (`admin/osx/ci/openurl-logger.m`, loaded only in the test) that records the URL and opens nothing. DNS is logged with `tcpdump` and the app's connections with `lsof`; `tools/screenshots/netcheck.py` fails the test on a Nextcloud host. Finally `check-branding.py --artifact` sweeps the installed app (text files, compiled translations, URLs inside every binary). Artifact `nodrive-macos-screenshots` |

A smoke-only run can reuse a `.dmg` from an earlier run: *Actions → macOS → Run workflow → reuse_run = <run id>*.

## Results

Before sign-in: CI. After sign-in: the owner, at home (Cloudflare only lets the owner's home network reach the Drive login page).

| Feature | Who | Result | Evidence |
|---|---|---|---|
| Installs from the .dmg | CI | PASS | Volume "NoDrive" holds `NoDrive.app` and an Applications link; copied to `/Applications`; ad-hoc signature still valid there (`codesign --verify --deep --strict`) |
| Starts | CI | PASS | 8 starts (4 variants × 2 runs), wizard window 720×452 pt every time (`results.tsv`: `launch-*`) |
| Shows NoDrive's name | CI | PASS | Menu bar "NoDrive", wizard "Log in to NoDrive"; `CFBundleName`/`CFBundleDisplayName`/`CFBundleExecutable` = NoDrive; `--version` prints `NoDrive version 35.0.0-nourax.1 (build N)` |
| Shows NoDrive's icon | CI | PASS | Nourax logo in the menu bar and the Dock (screenshots `00-screen`, `02-menu-bar`); `NoDrive.icns` with 10 sizes, 512 px copy in `docs/screenshots/macos/app-icon-512.png` (logo on white, as approved above 500 px) |
| Shows NoDrive's colours | CI | PASS | Pixel check finds the "Log in" button in `#2563EB` in every variant (measured `#2563EB` after converting the screenshot to sRGB) |
| Server fixed, no server-entry screen | CI | PASS | First page has no address field and no "Sign up" / "Self-host" (screenshots `01-wizard`); unit test `NoDriveBrandingTest` passed on macOS |
| Sign-in opens the browser at drive.nourax.org | CI | PASS | The app handed `https://drive.nourax.org/login/v2/flow/<token>` to the browser in all 4 variants (token hidden in the logs). The page itself is not loaded in CI |
| No Nextcloud service contacted | CI | PASS | DNS during the whole test: `drive.nourax.org` (after "Log in") and the runner's own Apple and GitHub traffic; no Nextcloud name. The app connected only to drive.nourax.org (`net/network-report.md`, `net/app-connections.txt`) |
| Dark mode (before sign-in) | CI | PASS | Wizard background `#202124` with the system appearance set to dark |
| Second language: German | CI | PASS | Screenshots `de-light` |
| Right-to-left: Arabic | CI | PASS, with a note | The app loads the Arabic catalogue ("Using ar translation") and mirrors the layout (buttons). The wizard texts have no Arabic translation upstream yet (empty "unfinished" entries in `client_ar.ts`) and show in English |
| Bundle ids and names | CI | PASS | App `org.nourax.nodrive.desktop`; Finder extension `org.nourax.nodrive.desktop.FinderSyncExt`, shown as "NoDrive FinderSync"; login item `NOURAXTEAM.org.nourax.nodrive.desktop.FinderSyncBroker` ("NoDrive Finder Integration"); app group `NOURAXTEAM.org.nourax.nodrive.desktop`; no Info.plist in the app names Nextcloud |
| Config folder | CI | PASS | `~/Library/Containers/org.nourax.nodrive.desktop/Data/Library/Preferences/NoDrive/nodrive.cfg` (the app is sandboxed); no Nextcloud folder or container was created |
| No updater | CI | PASS | No Sparkle keys in Info.plist, no Sparkle.framework |
| Licence files | CI | PASS | `NoDrive.app/Contents/Resources/licenses/` (COPYING, licence texts, THIRD-PARTY-NOTICES.txt) |
| Finder extension | CI | INFO | Registered with the system after the first start (`pluginkit`: `org.nourax.nodrive.desktop.FinderSyncExt(35.0.0)`). Whether Finder shows its badges needs a sync folder: owner test |
| Files on demand (File Provider) | CI | not built | Classic sync build (`buildFileProviderModule=False`). File Provider needs real signing; owner to-do |
| Unit tests | CI | PASS | `100% tests passed out of 93`; skipped: `UpdateChannelTest` (no update channels in branded builds; no updater) |
| Branding sweep of the installed app | CI | PASS | 61 hits, 0 unclassified: licence files and the Legal Notice copyright line in the translations (legal), one stale never-shown translation (dead code); listed in `tools/branding/allow-macos.txt` |
| File list | owner | pending owner test | |
| Upload, download, rename, move, delete | owner | pending owner test | |
| Public link with expiry and password | owner | pending owner test | |
| Sync folder and a conflict | owner | pending owner test | |
| Finder badges and context menu in the sync folder | owner | pending owner test | |
| Files on demand (if it loads) | owner | pending owner test | Not in this build (see above) |
| Sign out | owner | pending owner test | |
| Dark mode (after sign-in) | owner | pending owner test | |
| Second language (after sign-in) | owner | pending owner test | |
| Next to the real Nextcloud app (optional) | owner | pending owner test | |
| Device name in the web page (Settings → Security → Devices) | owner | pending owner test | |

Offline files and automatic photo upload are phone features; they do not apply to the desktop app.

## Screenshots

Folder `docs/screenshots/macos/<variant>/`. Variants: `en-light`, `de-light`, `ar-light` (Arabic, right to left), `en-dark`.

| File | Screen |
|---|---|
| `00-screen.png` | Whole screen: menu bar with "NoDrive" and the menu bar icon, the wizard, the Dock icon |
| `01-wizard.png` | First start: "Log in to NoDrive" with the Log in button only |
| `02-menu-bar.png` | The NoDrive icon in the menu bar |
| `02-after-icon-click.png` (en-light) | After clicking the menu bar icon: without an account it only brings the wizard back |
| `03-proxy-settings.png` | Proxy settings dialog of the wizard |
| `04-sign-in.png` | After "Log in": "Switch to your browser" |
| `app-icon-512.png` | The 512 px image from `NoDrive.icns` |

These are all the screens the app shows without an account. On macOS the menu bar icon has no menu before sign-in: it opens the wizard (`ownCloudGui::slotTrayClicked`).

Notes:
- The window frame, the menu bar and the Dock are macOS's own. The selected radio button in the proxy dialog uses the system accent colour.
- In Arabic the heading "Log in to NoDrive" stays at the left edge while the buttons move to the right; this comes from upstream's wizard layout (the Linux build shows the same).
- The screenshots were taken in run 40173101958 (commit d28db89835). Later commits changed only the smoke-test script, the test helpers and docs; the app is built from the same code.

## Network and privacy notes

- Before "Log in" the app makes no network connection. After "Log in" it contacts only `drive.nourax.org` (`status.php`, then the Login Flow v2 start and its polling).
- `nodrive --version` from a terminal also prints a few debug lines that start with internal logging category names (for example `nextcloud.gui.singleinstance:`). These are internal identifiers (`tools/branding/allow-shared.txt`), not app screens.
