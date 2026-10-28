# Testing NoDrive (Windows, macOS, Linux)

Two kinds of tests:

- **CI** (GitHub Actions): everything that works without signing in. Builds,
  unit tests, install, start, screenshots in four variants, network capture and
  branding sweeps of the packages. CI cannot sign in: the Drive's login pages only
  open from the owner's home network (Cloudflare rule).
- **Owner tests**: sign-in and everything after it, at home, with the owner's
  normal Nourax account ("Log in with Nourax"), not the emergency admin account.

Full evidence per platform: [testing-linux.md](testing-linux.md),
[testing-windows.md](testing-windows.md), [testing-macos.md](testing-macos.md).
How the builds are made: [build.md](build.md).

## 1. Results

CI result on `main` at commit `bd3bd8a412` (all jobs green): Linux run
[40189812444](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40189812444),
Windows run [40189812456](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40189812456),
macOS run [40189812446](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40189812446).
Every later push to `main` runs the same tests again.

### 1.1 Before sign-in (CI)

| Feature | Linux | Windows | macOS |
|---|---|---|---|
| Installs | n/a (the AppImage runs as it is) | PASS: silent MSI install, `C:\Program Files\NoDrive` | PASS: from the `.dmg` to `/Applications`, signature still valid |
| Starts | PASS: 8 starts | PASS: 9 starts | PASS: 8 starts |
| Shows NoDrive's name | PASS: title "NoDrive", "Log in to NoDrive", `--version`, `.desktop` file | PASS: title, tray "Exit NoDrive", Start menu, Apps & features, installer, version resources of 228 files | PASS: menu bar "NoDrive", Info.plists, `--version` |
| Shows NoDrive's icon | PASS: tray, title bar; packaged icon = `make-icons.py` output (SHA-256) | PASS: title bar, taskbar, tray, installer; exe icon = `make-icons.py` output (0 pixels differ) | PASS: menu bar, Dock; `NoDrive.icns` |
| Shows NoDrive's colours | PASS: "Log in" button `#2563EB` (pixel check); Settings switch `#2563EB` (unit test) | PASS: "Log in" button `#2563EB` | PASS: "Log in" button `#2563EB` |
| Server fixed, no server-entry screen | PASS: no address field, no "Sign up" / "Self-host"; unit and provisioning tests | PASS: same, also by UI Automation text | PASS: same; unit test |
| Sign-in opens the browser at drive.nourax.org | PASS: `https://drive.nourax.org/login/v2/flow/<token>` in 4 variants | PASS: same | PASS: same |
| No Nextcloud service contacted | PASS: DNS and `connect()` log: only drive.nourax.org | PASS: DNS client log, TCP connections: only drive.nourax.org | PASS: DNS (`tcpdump`) and `lsof`: only drive.nourax.org |
| Dark mode | PASS: wizard background `#202124` | PASS: same | PASS: same |
| Second language (German) | PASS | PASS | PASS |
| Right-to-left (Arabic) | PASS, note: mirrored; some new wizard texts are not translated upstream yet | PASS, same note | PASS, same note |
| Ids, folders, registry | PASS: `.desktop` id, MIME types | PASS: registry `Nourax\NoDrive`, `%APPDATA%\NoDrive`, new CLSIDs, no Nextcloud keys | PASS: bundle ids, app group, sandbox config folder |
| No updater | PASS (not built) | PASS (not built) | PASS: no Sparkle keys or framework |
| Licence files in the package | PASS: `usr/share/doc/nodrive/` | PASS: `C:\Program Files\NoDrive\licenses\` | PASS: `NoDrive.app/Contents/Resources/licenses/` |
| Uninstall | n/a | PASS: silent, nothing left except the user's settings | n/a (drag to Trash) |
| Unit tests (`ctest`) | PASS: 99 of 99 | PASS: 96 of 96 | PASS: 93 of 93 |
| Branding sweep of the package | PASS: 61 hits, 0 unclassified | PASS: 61 hits, 0 unclassified | PASS: 61 hits, 0 unclassified |
| Finder extension | n/a | n/a | INFO: registered (`pluginkit`); badges need a sync folder (owner) |
| Files on demand | not tested | owner test | not built (needs real signing) |

Skipped unit test on all three: `UpdateChannelTest` (branded builds have no update
channels and no updater; `tools/branding/ctest-skip-<os>.txt`).

The 61 hits in each package sweep are kept on purpose: licence files and the
Legal Notice copyright line in the compiled translations (legal; the dialog cannot
be opened) and one stale translation that is never looked up (dead code). See
[branding-sweep.md](branding-sweep.md).

### 1.2 After sign-in (owner)

| Feature | Linux | Windows | macOS |
|---|---|---|---|
| Sign in with "Log in with Nourax" | pending owner test | pending owner test | pending owner test |
| File list | pending owner test | pending owner test | pending owner test |
| Upload, download, rename, move, delete | pending owner test | pending owner test | pending owner test |
| Public link with expiry and password | pending owner test | pending owner test | pending owner test |
| Sync folder and a conflict | pending owner test | pending owner test | pending owner test |
| Files on demand (if it loads) | pending owner test | pending owner test | not in this build |
| Explorer sidebar "NoDrive" / Finder badges | n/a | pending owner test | pending owner test |
| Settings (General, Network, Info/About) and Activity show no Nextcloud name, logo or link | pending owner test | pending owner test | pending owner test |
| Sign out | pending owner test | pending owner test | pending owner test |
| Dark mode | pending owner test | pending owner test | pending owner test |
| Second language | pending owner test | pending owner test | pending owner test |
| Next to the real Nextcloud app (optional) | pending owner test | pending owner test | pending owner test |
| Device name in Settings > Security | pending owner test | pending owner test | pending owner test |
| Problem reports: switch off by default; "Report a problem" saves a zip in "Problem reports" ([problem-reports.md](problem-reports.md)) | pending owner test | pending owner test | pending owner test |
| Problem reports: no question after logout/logoff, the question after ending the app in the task manager, "Not now" is asked once | pending owner test | pending owner test | pending owner test |

Offline files and automatic photo upload are phone features; they do not apply to
the desktop app.

## 2. Owner tests: how to get and install the builds

Get the build (any platform):

1. Open `https://github.com/NouraX-ORG/nodrive-desktop/actions` while signed in to
   GitHub.
2. On the left, click the workflow: **Windows**, **macOS** or **Linux**.
3. Click the newest run on `main` with a green tick.
4. Scroll to **Artifacts** and download the package artifact from the table below.
   It downloads as a `.zip`; unzip it.

| Platform | Where to test | Artifact | File inside |
|---|---|---|---|
| Windows | this Windows 11 PC | `nodrive-windows-msi` | `NoDrive-35.0.0.<build>-nourax.1-x64.msi` |
| macOS | the owner's Mac (Apple silicon) | `nodrive-macos-dmg` | `NoDrive-35.0.0-nourax.1-<build>-arm64.dmg` |
| Linux | a Linux PC (x86_64), or WSLg on this PC | `nodrive-linux-appimage` | `NoDrive-35.0.0-nourax.1-<build>-x86_64.AppImage` |

### Windows

1. Double-click the `.msi`. The build is not signed, so Windows may show
   "Windows protected your PC": click **More info**, then **Run anyway**.
2. Follow the installer (Next, Install). Allow the change when Windows asks.
3. Start **NoDrive** from the Start menu. The NoDrive window opens with
   "Log in to NoDrive".
4. Click **Log in**. The browser opens `drive.nourax.org`: choose
   **Log in with Nourax** and allow access.
5. To remove it later: *Settings → Apps → Installed apps → NoDrive → Uninstall*.

### macOS

1. Double-click the `.dmg` and drag **NoDrive** onto **Applications**.
2. The app is not notarised, so macOS blocks a normal double-click the first time:
   - in Finder, open Applications, right-click **NoDrive** and choose **Open**,
     then **Open** again;
   - on macOS 15 (Sequoia) or newer that is not enough: double-click NoDrive once,
     close the warning, open *System Settings → Privacy & Security*, scroll down to
     the message about NoDrive and click **Open Anyway**, then confirm.
   - If there is no **Open Anyway** button, or macOS says the app is damaged,
     run this once in Terminal and open the app again:
     `xattr -dr com.apple.quarantine /Applications/NoDrive.app`
3. The NoDrive icon appears in the menu bar and the window "Log in to NoDrive"
   opens. Click **Log in**, choose **Log in with Nourax** in the browser and allow
   access.
4. For Finder badges, NoDrive's Finder extension ("NoDrive FinderSync") must be
   switched on in System Settings (extensions).

### Linux

1. Use a desktop with a tray area (KDE Plasma; or GNOME with the AppIndicator
   extension) and a password keyring (GNOME Keyring or KWallet; NoDrive stores its
   app password there).
2. In a terminal, in the folder with the file:
   `chmod +x NoDrive-*-x86_64.AppImage` and `./NoDrive-*-x86_64.AppImage`
3. If it reports a FUSE error, install FUSE 2 (Ubuntu 24.04:
   `sudo apt install libfuse2t64`) or start it with
   `./NoDrive-*-x86_64.AppImage --appimage-extract-and-run`.
4. Click **Log in**, choose **Log in with Nourax** in the browser and allow access.

WSLg on this PC works for the screens, but WSL has no tray area and usually no
keyring, so a real Linux machine is better for the sign-in and sync tests.

### What to try, and what to send back

1. Sign in. Then open `https://drive.nourax.org` in the browser, go to
   *Settings → Security*, and read out the name shown for the new device.
   Server 35 should show **"Sync client - 35.0.0 (Windows)"** (or `Macintosh`,
   `Linux`).
2. Read out the text of the browser page that asked to allow access (the server
   shows the app's full user agent there, for example
   `Mozilla/5.0 (Windows) mirall/35.0.0-nourax.1 ... (NoDrive, ...)`).
3. Work through the table in 1.2 and report PASS or what went wrong (a screenshot
   helps). Look for the word "Nextcloud" or a Nextcloud logo on every screen.
4. Once signed in, syncing also works away from home (WebDAV with the app password
   is not limited to the home network).

## 3. Screenshot index

All screenshots are in `docs/screenshots/` (58 files). Each was looked at for
Nextcloud names, logos and colours: none found ([branding-sweep.md](branding-sweep.md),
section 5). Variants: `en-light` (English), `de-light` (German), `ar-light`
(Arabic, right to left), `en-dark` (English, dark mode).

| Folder | Files | Screens |
|---|---|---|
| `linux/<variant>/` (4 variants) | `01-wizard.png`, `02-tray-menu.png`, `03-proxy-settings.png`, `04-sign-in.png` | first start "Log in to NoDrive"; tray menu without an account; proxy dialog; "Switch to your browser" after Log in |
| `windows/<variant>/` (4 variants) | `01-wizard.png`, `02-tray-menu.png`, `03-proxy-settings.png`, `04-sign-in.png` | same screens on Windows |
| `windows/installer/` | `01-welcome-en.png`, `01-welcome-de.png` | MSI installer, first page |
| `windows/first-start/` | `00-first-start.png` | first start with no settings |
| `windows/` | `app-icon-32.png` | the icon Windows shows for `nodrive.exe` |
| `macos/<variant>/` (4 variants) | `00-screen.png`, `01-wizard.png`, `02-menu-bar.png`, `03-proxy-settings.png`, `04-sign-in.png` | whole screen with menu bar and Dock; wizard; menu bar icon; proxy dialog; "Switch to your browser" |
| `macos/en-light/` | `02-after-icon-click.png` | menu bar icon clicked without an account: brings the wizard back |
| `macos/` | `app-icon-512.png` | 512 px image from `NoDrive.icns` |

Count: Linux 16, Windows 20, macOS 22.

Not captured yet: the Settings window (General, Network, Info/About). "Settings"
in the tray menu opens it even without an account (only a click on the tray icon
itself brings the wizard back), but the smoke tests do not take it yet. These
screens were reviewed in code instead ([branding-sweep.md](branding-sweep.md),
section 5): Info shows "NoDrive Desktop Client Version ...", the Legal Notice
button is hidden, "Usage Documentation" opens `https://drive.nourax.org`, and the
switch colour is `#2563EB`. Adding them to the three smoke tests is a to-do; until
then, Settings, Activity and About are part of the owner tests (look at each
page once, before and after sign-in).
