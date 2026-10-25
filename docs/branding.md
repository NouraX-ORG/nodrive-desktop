# NoDrive desktop branding

How the Nextcloud desktop client becomes **NoDrive** by **Nourax**: every id, the
colours, the icons, and the tools that apply all of it. Everything here is made by
committed tools, so it can be applied again after an upstream update
([upstream-updates.md](upstream-updates.md)).

## 1. Where the branding comes from

| Layer | What it does | File |
|---|---|---|
| Ids | One file holds every name, id, colour, GUID and switch | `tools/branding/ids.json` |
| CMake overlay | Generated from `ids.json`. `CMakeLists.txt` includes it right after `NEXTCLOUD.cmake` and the CfApi block. Plain `set()` makes normal variables, which hide cache values, so a `-D` from KDE Craft or a script cannot undo a product fact | `nodrive/NODRIVE.cmake` |
| Theme class | Upstream's `THEME_CLASS` hook. `NoDriveTheme` only removes the trial-server hint; everything else in `Theme` reads `config.h` values from the overlay | `nodrive/nodrivetheme.h` |
| Privacy URL | `NOURAX_PRIVACY_URL` as a C++ define (empty: the link is hidden) | `nodrive/nodriveconfig.h.in` |
| Code patches | Only where no hook exists (wizard, server lock, a few texts, colours in QML/C++, probe files, legal button). Exact anchors; the tool stops with an error if upstream changed the text | `tools/rebrand.py`, `tools/rebrand.d/*.py` |
| Translations | The four messages that named Nextcloud, in all 64 `translations/client_*.ts` files | `tools/rebrand.py` |
| Images | Every icon from the one Nourax logo | `tools/make-icons.py` |
| Licences | `nodrive/licenses/` (licence texts, `THIRD-PARTY-NOTICES.txt`) | `tools/rebrand.py` |
| Gate | Fails CI when a user-visible Nextcloud name, link, logo or colour comes back | `tools/check-branding.py` |

Upstream files that are never edited: `NEXTCLOUD.cmake`, `VERSION.cmake`,
`config.h.in`, `theme.qrc.in`, the root `craftmaster.ini`, `COPYING*`, `LICENSES/`,
`AUTHORS.md`, `REUSE.toml`, `.github/upstream-workflows/`, source copyright headers.

## 2. Every id

The GUIDs were generated once with Python `uuid.uuid4()` on 2026-10-25 and never
change. "CI" in the last column means a CI run read the value from the built or
installed app; "derived" means upstream code builds it from the values above.

### 2.1 Shared (all platforms)

| What | Upstream | NoDrive | Checked |
|---|---|---|---|
| App name (`APPLICATION_NAME`, `APPLICATION_SHORTNAME`) | Nextcloud | `NoDrive` | CI (window titles, `--version`) |
| Maker (`APPLICATION_VENDOR`) | Nextcloud GmbH | `Nourax` | CI (MSI, version resources) |
| Executables (`APPLICATION_EXECUTABLE`) | nextcloud, nextcloudcmd | `nodrive`, `nodrivecmd` (libraries `nodrivesync`, `nodrive_csync`, plugins `nodrivesync_vfs_*`) | CI |
| Config file (`APPLICATION_CONFIG_NAME`) | nextcloud.cfg | `nodrive.cfg` | CI |
| Icon name (`APPLICATION_ICON_NAME`) | Nextcloud | `NoDrive` | CI |
| Domain (`APPLICATION_DOMAIN`) | nextcloud.com | `drive.nourax.org` (installer help links only) | CI (MSI) |
| Reverse domain (`APPLICATION_REV_DOMAIN`, `..._INSTALLER`) | com.nextcloud.desktopclient | `org.nourax.nodrive.desktop` | CI |
| D-Bus domain (`APPLICATION_REV_DOMAIN_DBUS`) | desktopclient.nextcloud.com | `desktop.nodrive.nourax.org` | derived |
| Linux package name (`LINUX_PACKAGE_SHORTNAME`) | nextcloud | `nodrive` | CI |
| Linux app id (`LINUX_APPLICATION_ID`) | com.nextcloud.desktopclient.nextcloud | `org.nourax.nodrive.desktop.nodrive` | CI (`.desktop` file name) |
| Virtual-file suffix (`APPLICATION_VIRTUALFILE_SUFFIX`) | nextcloud | `nodrive` | derived |
| URL scheme (`APPLICATION_URI_HANDLER_SCHEME`) | nc | `nc` **kept**: the server's web "Open locally" action builds `nc://open/...` (server 35 `openLocallyAction.ts`) | CI (Windows `nc://` handler) |
| Theme class (`THEME_CLASS`) | NextcloudTheme | `NoDriveTheme` | build |
| Apple Team (`DEVELOPMENT_TEAM`) | NKUJUXUJ3B | `NOURAXTEAM` (placeholder until there is an Apple Developer account) | CI (app group) |
| Server | (none) | `https://drive.nourax.org`, enforced (`APPLICATION_SERVER_URL_ENFORCE ON`) | CI (unit test, wizard) |
| Help link (`APPLICATION_HELP_URL`) | docs.nextcloud.com | `https://drive.nourax.org` | unit test |
| Privacy link (`NOURAX_PRIVACY_URL`) | (none) | empty: link hidden (owner to-do) | build |
| Version | 35.0.0 | `35.0.0-nourax.1` (`MIRALL_VERSION_SUFFIX` = `-nourax.<nourax_release>`), CI adds ` (build <run number>)` | CI |
| Keychain service | Nextcloud | `NoDrive` | derived |
| Default sync folder | `~/Nextcloud` | `~/NoDrive` (Windows `%USERPROFILE%\NoDrive`) | derived |
| Folder-watcher probe files | `.nextcloudsync.log`, `.nextcloudpermissions.log` | `.nodrivesync.log`, `.nodrivepermissions.log` (old names stay excluded from sync) | unit test |

### 2.2 Config and data folders

| OS | Config file | Logs | Other |
|---|---|---|---|
| Linux | `~/.config/NoDrive/nodrive.cfg` (derived) | `~/.config/NoDrive/logs/` | socket `$XDG_RUNTIME_DIR/NoDrive/socket`; temp logs `/tmp/NoDrive-logdir` |
| Windows | `%APPDATA%\NoDrive\nodrive.cfg` (CI) | `%APPDATA%\NoDrive\logs\` | named pipe `\\.\pipe\nodrive-<user>` (CI) |
| macOS | `~/Library/Containers/org.nourax.nodrive.desktop/Data/Library/Preferences/NoDrive/nodrive.cfg` (CI; the app is sandboxed) | `.../Preferences/NoDrive/logs/` | app group container `NOURAXTEAM.org.nourax.nodrive.desktop` |

Log files keep upstream's internal file name `<date>_nextcloud.log` (classified
internal in [branding-sweep.md](branding-sweep.md), item B1).

### 2.3 Linux

| What | Value | Checked |
|---|---|---|
| `.desktop` file | `org.nourax.nodrive.desktop.nodrive.desktop` (`Name=NoDrive Desktop`, `Icon=NoDrive`, `Exec=nodrive %u`) | CI |
| MIME types | `application/vnd.nodrive`, `x-scheme-handler/nc` | CI |
| Icon | `usr/share/icons/hicolor/512x512/apps/NoDrive.png` | CI (same SHA-256 as `theme/colored/512-NoDrive-icon.png`) |
| systemd user unit | `org.nourax.nodrive.desktop.nodrive.service` | derived |
| D-Bus single-instance name | `org.nourax.nodrive.desktop.nodrive` | derived |
| libcloudproviders bus / path | `org.nourax.NoDrive` / `/org/nourax/NoDrive` (`DBUS_PREFIX org`) | derived |
| AppImage | `NoDrive-35.0.0-nourax.1-<build>-x86_64.AppImage` | CI |
| Licences | `usr/share/doc/nodrive/` | CI |

### 2.4 Windows

| What | Upstream | NoDrive | Checked |
|---|---|---|---|
| Context-menu handler CLSID (`WIN_SHELLEXT_CONTEXT_MENU_GUID`) | {BC6988AB-ACE2-4B81-84DC-DC34F9B24401} | `{3D056A87-6CB2-47ED-BB26-88619E5F1310}` | CI |
| Overlay CLSID error | {E0342B74-...} | `{2735AE03-F851-41BD-8A7D-9990874280A1}` | CI |
| Overlay CLSID OK | {E1094E94-...} | `{7FDD1DD4-BFAA-40E0-BC3C-820B4ACE2094}` | CI |
| Overlay CLSID OK shared | {E243AD85-...} | `{C29F966A-75FB-4A4F-9C21-A81FE8BE7860}` | CI |
| Overlay CLSID sync | {E3D6DB20-...} | `{8D7C3782-C1B0-4A62-8248-E452BDA833F6}` | CI |
| Overlay CLSID warning | {E4977F33-...} | `{7BBC37AC-82DF-4EAB-8BE0-58CEF2DF42E1}` | CI |
| MSI upgrade code (`WIN_MSI_UPGRADE_CODE`) | FD2FCCA9-BB8F-4485-8F70-A0621B84A7F4 | `128D34E5-4AC9-4F86-B692-48CCEBDD8E80` | CI |
| MSI product code | new per build | new per build (upstream design) | CI |
| CfApi shell-extension AppID (`CFAPI_SHELLEXT_APPID_REG`) | {E314A650-DCA4-416E-974E-18EA37C213EA} | `{D0DEF841-F1C6-408F-A61A-5648FF369364}` | build |
| CfApi custom-state handler CLSID | 1E62D59A-6EA4-476C-B707-4A32E88ED822 | `E83B042C-9E22-4595-A329-165EDD0CE33A` | build |
| CfApi thumbnail handler CLSID | 6FF9B5B6-389F-444A-9FDD-A286C36EA079 | `925951D6-1412-4A58-9A02-EF15798AC23C` | build |
| CfApi display names | Nextcloud ... | `NoDrive CfApi Shell Extensions`, `NoDrive Custom State Handler`, `NoDrive Thumbnail Handler` | derived |
| Context-menu / overlay registry names | NextcloudContextMenuHandler, "  NextcloudOK" ... | `NoDriveContextMenuHandler`; `  NoDriveError`, `  NoDriveOK`, `  NoDriveOKShared`, `  NoDriveSync`, `  NoDriveWarning` | CI |
| Install folder | C:\Program Files\Nextcloud | `C:\Program Files\NoDrive` | CI |
| Registry | HKLM\Software\Nextcloud GmbH\Nextcloud | `HKLM\SOFTWARE\Nourax\NoDrive`, `HKLM\SOFTWARE\WOW6432Node\Nourax\NoDrive`, `HKCU\Software\Nourax\NoDrive` | CI |
| Policy keys | Software\Policies\Nextcloud GmbH\Nextcloud | `HKCU` / `HKLM\Software\Policies\Nourax\NoDrive` | derived |
| Autostart | Run value "Nextcloud" | `HKCU\...\Run\NoDrive` | CI |
| Start menu / desktop shortcut | Nextcloud.lnk | `NoDrive.lnk` | CI |
| Start tile manifest | `nextcloud.VisualElementsManifest.xml` | `nodrive.VisualElementsManifest.xml` (written by `tools/rebrand.py`) | CI |
| Cloud Files sync-root id | Nextcloud!SID!account!alias | `NoDrive!<SID>!<account>!<alias>` | derived (owner test) |
| Explorer sidebar name | Nextcloud | `NoDrive` (folder or app name) | derived (owner test) |
| MSI help links | https://nextcloud.com/ | `https://drive.nourax.org/` | CI |
| MSI file | Nextcloud-... | `NoDrive-35.0.0.<build>-nourax.1-x64.msi` | CI |

New CfApi GUIDs matter: upstream's uninstaller deletes its CLSID keys, so shared
GUIDs would let one app's uninstall break the other.

### 2.5 macOS

| What | Upstream | NoDrive | Checked |
|---|---|---|---|
| App bundle | Nextcloud.app, com.nextcloud.desktopclient | `NoDrive.app`, `org.nourax.nodrive.desktop` | CI |
| App group | NKUJUXUJ3B.com.nextcloud.desktopclient | `NOURAXTEAM.org.nourax.nodrive.desktop` | CI |
| Finder extension | com.nextcloud.desktopclient.FinderSyncExt | `org.nourax.nodrive.desktop.FinderSyncExt` ("NoDrive FinderSync") | CI |
| Finder login item | NKUJUXUJ3B.com.nextcloud.desktopclient.FinderSyncBroker | `NOURAXTEAM.org.nourax.nodrive.desktop.FinderSyncBroker` ("NoDrive Finder Integration") | CI |
| File Provider extensions (not built yet) | com.nextcloud.desktopclient.FileProviderExt / UIExt | `org.nourax.nodrive.desktop.FileProviderExt` / `.FileProviderUIExt` | derived |
| File Provider domain name | account / "Nextcloud" | account / `NoDrive` | derived |
| UTTypes | com.nextcloud.desktopclient.VIRTUALFILE ... | `org.nourax.nodrive.desktop.VIRTUALFILE`, `org.nourax.nodrive.desktop.decoration.keep-downloaded` | derived |
| Managed preferences | /Library/Preferences/com.nextcloud.desktopclient.plist | `/Library/Preferences/org.nourax.nodrive.desktop.plist` | derived |
| URL type name | Nextcloud Edit Locally | `NoDrive Edit Locally` | CI |
| Copyright line | (C) ... Nextcloud GmbH | `NoDrive by Nourax. Open-source software, GPL-2.0-or-later.` | CI |
| DMG | - | `NoDrive-35.0.0-nourax.1-<build>-arm64.dmg`, volume "NoDrive" | CI |
| Licences | - | `NoDrive.app/Contents/Resources/licenses/` | CI |

### 2.6 User agent

Code: `src/common/utility.cpp` `Utility::userAgentString()`, sent on every request
and on the Login Flow v2 start. The app-name part is `APPLICATION_SHORTNAME`, so
no code change was needed.

| OS | User agent |
|---|---|
| Windows | `Mozilla/5.0 (Windows) mirall/35.0.0-nourax.1 (NoDrive, windows-<kernel> ClientArchitecture: x86_64 OsArchitecture: x86_64)` |
| macOS | `Mozilla/5.0 (Macintosh) mirall/35.0.0-nourax.1 (NoDrive, macos-<kernel> ClientArchitecture: arm64 OsArchitecture: arm64)` |
| Linux | `Mozilla/5.0 (Linux) mirall/35.0.0-nourax.1 (NoDrive, <distro>-<kernel> ClientArchitecture: x86_64 OsArchitecture: x86_64)` |

CI builds put ` (build <run number>)` after the version, for example
`mirall/35.0.0-nourax.1 (build 19) (NoDrive, ...)`.

- Server 35 recognises desktop clients with
  `/^Mozilla\/5\.0 \([A-Za-z ]+\) (?:mirall|csyncoC)\/([^ ]*).*$/`
  (`lib/public/IRequest.php`, `USER_AGENT_CLIENT_DESKTOP`, checked at v35.0.2).
  The unit test `NoDriveBrandingTest::testUserAgentMatchesServerRule` runs that
  rule against the real string. The captured version `35.0.0-nourax.1` is inside
  the server's allowed range (minimum 3.3.50).
- The server's web page Settings > Security lists the device as
  **"Sync client - 35.0.0 (Windows|Macintosh|Linux)"** (`AuthToken.vue` at v35.0.2).

## 3. Colours

From `nourax/docs/THEME_BIBLE.md`, stored in `ids.json` (`colors`).

| Use | Colour | Where it goes |
|---|---|---|
| Primary (cobalt) | `#2563EB` | `NEXTCLOUD_BACKGROUND_COLOR` and `APPLICATION_WIZARD_HEADER_BACKGROUND_COLOR` (`Style.ncBlue`, `Theme::defaultColor()`); wizard primary button and radio accent (`theme/Style/Style.qml`); Settings on/off switches (`src/gui/settingsswitch.cpp`); info-box tint `Qt.rgba(0.145, 0.388, 0.922, ...)`; recoloured upstream glyphs (SVG and PNG); DMG arrow |
| Primary light | `#3B82F6` | dark-mode radio accent, selected border, pressed button, link colour on dark backgrounds (`src/libsync/theme.cpp`), dark selection tint `Qt.rgba(0.231, 0.510, 0.965, 0.18)` |
| Accent | `#F97316` / `#EA580C` | in `ids.json` for later use; not used by the desktop app today |
| Light background | `#F8FAFC` | DMG background, MSI and NSIS side panels |
| Dark background | `#0B1220` | in `ids.json`; the desktop app follows the system's dark palette |
| Dark surface | `#0F172A` | in `ids.json`; not used today |
| Wizard header title | `#FFFFFF` | `APPLICATION_WIZARD_HEADER_TITLE_COLOR` |

Nextcloud's blues (`#0082C9`, `#00679E`, `#0071AD`, `#1CAFFF`, and the same colours
written as `rgb(0,130,201)`, `QColor(0, 103, 158)` or `Qt.rgba(0, 0.51, 0.79, ...)`)
are replaced. `tools/check-branding.py` searches for all of these forms.
Fonts: each platform's system font (unchanged).

## 4. Icons

Made only by `tools/make-icons.py` (Pillow) from `nodrive/brand/nourax-logo.png`, a
byte-for-byte copy of `nourax/assets/brand/nourax-logo.png` (500 x 500 RGBA).

Rules:
- The logo is never redrawn, recoloured, cropped or stretched: only the whole
  500 x 500 canvas is scaled proportionally (LANCZOS).
- Up to 500 px: transparent canvas. Above 500 px (512, 1024): proportional
  enlargement onto a plain white square (approved by the owner on 2026-10-25).
- Wide images (wizard logo, installer banners): the logo is centred and padded.
- SVG files the build needs are thin wrappers that embed the unchanged logo PNG
  (no vector redraw).
- **No monochrome variant**: the "monochrome" tray sets (`theme/black`,
  `theme/white`) also use the full-colour logo. Real monochrome or template icons
  need the owner's approval (to-do).

| Output | Sizes | Used for |
|---|---|---|
| `theme/colored/<size>-NoDrive-icon.png`, `NoDrive-icon.svg` | 16, 20, 24, 32, 40, 48, 64, 128, 256, 512, 1024 | app icon: Windows `.ico`, macOS `.icns`, Linux hicolor, in-app |
| `theme/colored/NoDrive-w10startmenu.svg`, `70-`/`150-NoDrive-w10startmenu.png` | 70, 150 | Windows Start tile |
| `theme/{colored,white,black}/state-*.svg` and `state-*-<size>.png` | 16, 32, 64, 128, 256 | tray icon for every sync state (full-colour logo in every state) |
| `theme/colored/wizard_logo.svg`, `wizard_logo.png`, `wizard_logo@2x.png` | 132 x 63, 266 x 126 | wizard and About logo |
| `theme/*/wizard-nextcloud.*`, `theme/*/wizard-talk.png` / `@2x` | various | upstream trademarked pictures still listed in the resource file, overwritten with the Nourax logo |
| `theme/colored/{external,folder,wizard-files,wizard-groupware}{,@2x}.png` | as upstream | upstream glyphs: only Nextcloud-blue pixels moved to `#2563EB`, alpha kept |
| `admin/win/msi/gui/banner.bmp`, `banner.svg`, `dialog.bmp` | 493 x 58, 493 x 314 | MSI installer pages |
| `admin/win/nsi/welcome.bmp`, `page_header.bmp`, `installer.ico` | NSIS sizes | NSIS (not used for NoDrive packages) |
| `admin/osx/DMGBackground.png`, `installer-background*.png`, `installer-background.svg` | 501 x 351, 320 x 200, 640 x 400 | DMG window, pkg installer (pkg not used) |

171 images in total. Each is listed with its SHA-256 in
`tools/branding/icons-manifest.txt`; `make-icons.py --check` (in CI) fails when a
committed image differs from what the script makes.

Trademarked Nextcloud glyphs that are still used in the app are replaced by
`tools/rebrand.py` with copies of neutral upstream glyphs (a file copy, not a new
drawing): `theme/talk-app.svg`, `theme/white/talk-app.svg`, `theme/*/wizard-talk.svg`
and `theme/colored/talk-bordered.svg` become the speech bubble `comment.svg`;
`theme/{black,white}/deck.svg` become `more-apps.svg`.

Not regenerated, because they are not built into any package: `theme/colored/Nextcloud-*.svg`,
`Nextcloud-w10starttile.png`, `theme/colored/icons/Nextcloud-icon-win-folder.svg`,
`theme/*/nextcloud/state-*`, `theme/colored/AppIcon.icon/`, the File Provider
images. They stay as upstream files.

## 5. Switched off and fixed

| Item | How |
|---|---|
| Updater (and Sparkle on macOS) | `BUILD_UPDATER OFF`; Sparkle keys removed from Info.plist. `APPLICATION_UPDATE_URL` must be non-empty for upstream code to compile, so it is `https://drive.nourax.org/`; no updater code is built, so it is never contacted |
| Providers list, crash reporter | `WITH_PROVIDERS OFF`, `WITH_CRASHREPORTER OFF` |
| Import of the real Nextcloud client's accounts | `DISABLE_ACCOUNT_MIGRATION ON`, `APPLICATION_DISPLAY_LEGACY_IMPORT_DIALOG OFF` |
| News, "what's new", telemetry, app-store links | none exist in desktop 35 |
| Server | fixed and enforced; wizard shows no address field and no "Sign up" / "Self-host"; `--serverurl`, `--overrideserverurl` and config-file overrides that differ are refused |
| Legal Notice button | hidden ([NOTICE.md](../NOTICE.md)) |
| Client status reporting | kept: it reports only to the account's own server, and only when that server asks for it |

## 6. The tools

The tools are plain Python 3 scripts (CI uses Python 3.12; `--update-trademark-hashes` needs 3.11 or newer). Only `make-icons.py` needs a package (Pillow).

### 6.1 `tools/rebrand.py`

```sh
python tools/rebrand.py            # apply all changes
python tools/rebrand.py --check    # change nothing; exit 1 if a run would change a file
python tools/rebrand.py -v         # list every change
```

Idempotent: a second run reports `0 change(s)`. CI runs `--check` in every
workflow's branding job, so a branch where the rebrand was not re-run fails.

What it does, in order (the `step_*` functions):

| Step | Files | Change |
|---|---|---|
| `step_overlay` | `nodrive/NODRIVE.cmake`, `CMakeLists.txt`, `NextcloudCPack.cmake` | render the overlay from `ids.json`; include it after the CfApi block; include it again after CPack re-includes `NEXTCLOUD.cmake` |
| `step_theme_cpp` | `src/libsync/theme.cpp` | "Built from Git revision" link to this repository; link colour on dark backgrounds `#3B82F6` |
| `step_unused_theme` | `src/libsync/nextcloudtheme.cpp` | remove the trial-server URL from the unused upstream theme class |
| `step_infosettings` | `src/gui/infosettings.cpp` | hide the Legal Notice button; a privacy button only when `NOURAX_PRIVACY_URL` is set |
| `step_wizard` | `ServerPage.qml`, `AccountWizardWindow.qml`, `accountwizardcontroller.cpp` | with a fixed server: hide the description, the address field and its label, the "Sign up" and "Self-host" buttons; their links go to our server |
| `step_server_lock` | `accountwizardcontroller.cpp`, `accountsetupcommandlinemanager.cpp` | refuse other servers from the config file and the command line |
| `step_strings` | `AssistantMessageList.qml`, `advancedsettings.ui`, `ocsassistantconnector.cpp`, `application.cpp`, `cmd.cpp` | texts without "Nextcloud" and without the github.com/nextcloud link |
| `step_style` | `theme/Style/Style.qml`, six `theme/colored/*.svg` glyphs | Nourax colours |
| `step_settings_switch` | `src/gui/settingsswitch.cpp` | switch colour `#2563EB` |
| `step_probe_files` | `folder.cpp`, `csync_exclude.cpp`, `customstateprovider.cpp` | NoDrive probe file names; old names stay excluded |
| `step_translations` | `translations/client_*.ts` | rewrite the four messages in every language (below) |
| `step_glyphs` | `theme/**/talk-app.svg`, `deck.svg`, `wizard-talk.svg`, `talk-bordered.svg` | copy neutral upstream glyphs over trademarked ones |
| `step_visual_elements` | `theme/nodrive.VisualElementsManifest.xml` | Windows tile manifest for the new executable name |
| `step_licenses` | `nodrive/licenses/` | licence texts and `THIRD-PARTY-NOTICES.txt` |
| `step_tests` | `test/CMakeLists.txt`, `test/testnodrivebranding.cpp`, `test/testnextcloudcmdprovisioning.cpp` | the NoDrive branding test; provisioning tests expect `nodrivecmd` and refused servers |
| plugins | `tools/rebrand.d/*.py` | every platform plugin, in file-name order |

The four translatable messages (in every `.ts` file, translations included):

| Old source | New source | Rule for translations |
|---|---|---|
| Start a conversation with Nextcloud Assistant. | Start a conversation with the Assistant. | remove "Nextcloud" and the space or hyphen that joins it |
| Nextcloud Assistant (obsolete entry) | Assistant | same |
| `seconds (if <a href="https://github.com/nextcloud/notify_push">Client Push</a> is unavailable)` | seconds (if Client Push is unavailable) | remove the link tags, keep the text |
| Copyright 2017-2026 Nextcloud GmbH / 2012-2023 ownCloud GmbH (Legal Notice) | unchanged | legal text; the dialog cannot be opened |

The `.ts` files are edited message by message; everything else stays byte for byte,
so the files still open in Qt Linguist. If an old source text is no longer found
(upstream changed it), the tool stops with an error.

### 6.2 `tools/rebrand.d/` (platform plugins)

| Plugin | Lane | Changes |
|---|---|---|
| `linux.py` | Linux | `admin/linux/build-appimage.sh`: NoDrive defaults, icon `NoDrive.png`, licences in `usr/share/doc/nodrive`, file name `NoDrive-<version>-<build>-x86_64.AppImage`; Dolphin overlay plugin description |
| `windows.py` | Windows | version resources of the shell-extension DLLs (`CfApiShellIntegration.rc`, `NCContextMenu.rc`, `NCOverlay.rc`, `NCUtil.rc`); `navigationpanehelper.cpp` removes only NoDrive's own sidebar entries; MSI file name |
| `macos.py` | macOS | `MacOSXBundleInfo.plist.in` (short version, copyright, local-network text, no Sparkle keys); Xcode fallback team and ids (`Build.xcconfig`, `project.pbxproj`); Finder menu fallback name (`FinderSync.m`); "Send a Talk reply" (`systray_mac_usernotifications.mm`) |

A plugin is a Python file with `LANE = "<lane>"` and `def apply(ctx)`. It may only
use the helpers of `tools/rebrandlib.py` (`ctx.replace`, `ctx.regex_sub`,
`ctx.write`, `ctx.copy`, `ctx.require`, `ctx.read`) and read ids from `ctx.ids`, so
`--check` works and a second run changes nothing. Details:
`tools/rebrand.d/README.md`.

### 6.3 `tools/make-icons.py`

```sh
pip install Pillow
python tools/make-icons.py          # write every image and tools/branding/icons-manifest.txt
python tools/make-icons.py --check  # write nothing; exit 1 if an image would change
```

Same rules and outputs as section 4. If an image differs only in its PNG encoding
(another zlib build) but has the same pixels, the committed file is kept, so
`--check` gives the same answer on Linux, Windows and macOS.

### 6.4 `tools/check-branding.py`

```sh
python tools/check-branding.py --lane linux|windows|macos|all   # the gate (exit 1 = fail)
python tools/check-branding.py --lane all --list                # print unclassified hits
python tools/check-branding.py --lane all --list-all            # print every hit with its class
python tools/check-branding.py --lane all --write-sweep         # refresh the generated part of docs/branding-sweep.md
python tools/check-branding.py --artifact <unpacked package> --lane linux   # sweep a built package
python tools/check-branding.py --update-trademark-hashes v35.0.0
```

- **Patterns** (`tools/branding/patterns.txt`): the names, links, store ids and
  colours of spec section 6, case-insensitive, also colours written as numbers.
- **Where it looks**: `.ts` translations (source, translation, plural forms),
  string literals in QML, `.ui` and shipped C++/ObjC/Swift/Python code, colour
  values in code outside comments, Apple `.strings`/`.xcstrings`, packaging and
  template files (`admin/`, `systemd/`, `*.desktop.in`, `cmake/modules/*.in`,
  Info.plists, WiX files, `theme/*.xml`, `theme.qrc.in`, `NEXTCLOUD.cmake`,
  `nodrive/`), text inside SVGs, image file names, upstream trademarked images (by
  SHA-256, `tools/branding/upstream-trademark-hashes.txt`) and our generated
  images (`icons-manifest.txt`). Not searched, because not shipped: `doc/`, `docs/`,
  `test/`, `tools/`, `.github/`, top-level README, NOTICE and CHANGELOG files.
- **`--artifact`**: text files of an unpacked package, compiled `.qm`
  translations, and URLs inside every binary.
- **Classes**: every hit is unclassified (user-visible: the gate fails) until a
  rule in `tools/branding/allow-*.txt` classifies it as `internal`,
  `server-required`, `not-shipped`, `dead-code` or `legal`. Rule format:
  `path-glob<TAB>regex<TAB>class<TAB>reason`; an empty reason is an error.
  Stale `.ts` messages (their source text is no longer in the code) are found
  automatically.
- **Lanes**: `tools/branding/lanes.json` says which lane owns which path.
  `allow-shared.txt` (Linux lane) may classify any file; `allow-windows.txt` and
  `allow-macos.txt` only their own lane's files.
- **CI**: every workflow's branding job runs it for its lane, and the package
  jobs run `--artifact` on the built package.

Other files in `tools/branding/`: `ctest-skip-<lane>.txt` (unit tests skipped,
with the reason for each), `upstream-trademark-hashes.txt`.

### 6.5 `tools/screenshots/`

Test scripts used by CI only: `linux.sh`, `windows.ps1`, `macos.sh` (start the
app, screenshots, network capture), `locate.py` (pixel checks), `netcheck.py`
(fails on a Nextcloud host), `fake-portal.py` (Linux dark-mode portal).

## 7. Who owns which file

| Lane | Files |
|---|---|
| Linux (shared branding) | `nodrive/`, `tools/` except the two platform plugins, allowlists and scripts below, `CMakeLists.txt` (one include line), `src/` except the platform files below, `theme/`, `translations/`, `test/`, `admin/linux/`, `mirall.desktop.in`, `systemd/`, `.github/workflows/linux.yml`, `README.md`, `NOTICE.md`, `docs/` (except the platform docs below) |
| Windows | `tools/rebrand.d/windows.py`, `tools/branding/allow-windows.txt`, `ctest-skip-windows.txt`, `tools/screenshots/windows.ps1`, `admin/win/` (except the generated images), `shell_integration/windows/`, `CfApiShellIntegration.rc`, `navigationpanehelper.*`, `utility_win.cpp`, `.github/workflows/windows.yml`, `docs/build-windows.md`, `docs/testing-windows.md`, `docs/screenshots/windows/` |
| macOS | `tools/rebrand.d/macos.py`, `tools/branding/allow-macos.txt`, `ctest-skip-macos.txt`, `tools/screenshots/macos.sh`, `admin/osx/` (except the generated images), `MacOSXBundleInfo.plist.in`, `shell_integration/MacOSX/`, `src/gui/macOS/`, `src/gui/*_mac*`, `theme_mac.*`, `.github/workflows/macos.yml`, `docs/build-macos.md`, `docs/testing-macos.md`, `docs/screenshots/macos/` |

The machine-readable version is `tools/branding/lanes.json`.
