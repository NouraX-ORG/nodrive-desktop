# Branding sweep: no Nextcloud on any screen

The owner's rule (spec section 6): no user screen may show Nextcloud's name, logo,
colours, links or assets, in any language the app ships. Credits to Nextcloud live
only in this repository. This file is the sweep table: every hit of the keyword
search, and why each one that is kept is allowed to stay.

| Item | Value |
|---|---|
| Sweep | independent final review ("round 2"), 2026-10-25, of `main` = `bd3bd8a412` |
| Base for "before" | upstream tag `v35.0.0` (`ce977e3181`) |
| CI on `bd3bd8a412` | Linux [40189812444](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40189812444), macOS [40189812446](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40189812446), Windows [40189812456](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40189812456): all jobs passed |
| **Result** | **PASS**: 0 unclassified user-visible hits, all screenshots clean, `check-branding.py` passes in CI for all three platforms |

Classes: **user-visible** (must be fixed), **internal** (identifier the server, OS
or build depends on; sub-classes server-required, not-shipped, dead-code) and
**legal** (copyright and licence text). The gate `tools/check-branding.py` fails
CI on any unclassified hit; how it works: [branding.md](branding.md), section 6.4.

## 1. Hit counts, before and after

Pattern (case-insensitive, every language):
`nextcloud|next[ _-]?cloud|owncloud|own[ _-]cloud|mirall|it\.niedermann|1125420102|0082c9|00679e|0, ?130, ?201|0, ?103, ?158`.
It also covers `nextcloud hub`, `nextcloud gmbh`, every `*.nextcloud.*` link and
`com.nextcloud`.

| Measure | v35.0.0 (before) | main `bd3bd8a412` (after) |
|---|---|---|
| Raw sweep of every tracked text file (`git grep -I -i -n -E`) | 10152 lines, 1580 files | 10316 lines, 1604 files |
| Shipped scope (src, translations, theme, qrc, Windows and Finder shell integration, MSI/WiX, macOS/Linux packaging, `.desktop`, CMake, systemd, `nodrive/`) | 7543 lines (6312 without SPDX headers) | 7301 lines (6070 without SPDX headers) |
| `.ts` source/translation/plural lines with a hit | 367 | 152 |
| Live translatable messages that show Nextcloud | 4 messages, 174 `.ts` entries | 1 message, 56 entries: the Legal Notice copyright line, whose dialog cannot be opened (legal) |
| `.ts` messages where only the translation names Nextcloud | 0 | 0 |
| String literals in shipped code with a keyword (comments excluded) | 523 | 505, all classified in section 3 (0 user-visible) |
| `tools/check-branding.py --lane all` | (round 1: 1131 hits, 310 unclassified) | 848 hits (unclassified 0, internal 759, legal 89): PASS |
| **User-visible leftovers** | n/a | **0** |

The raw count went up because the rebrand added credits and tooling (README,
NOTICE, docs, tools, our CI). None of these files ship. The two docs commits that
added this file (`304720e206` and the docs fix after it) add 256 more lines with a
keyword, all in `README.md`, `NOTICE.md` and `docs/` (class N1); the raw sweep
after them gives 10572 lines.

Other checks:
- Transliterations and misspellings (Cyrillic, Arabic, Persian, Hebrew, Japanese,
  Korean and Greek forms, `nextclud`, `nexcloud` and similar) in all 64 `.ts`
  files: 0.
- Binary files in the repository (UTF-8 and UTF-16 scan of every tracked file
  with NUL bytes): 3 files, none shipped (`admin/osx/DS_Store.in`,
  `NextcloudFileProviderKit.pxd`, `test/test_journal.db`).

### Fixed in the sweep rounds

| Round 1 finding | Fix | Evidence |
|---|---|---|
| F1 Settings on/off switch drawn in Nextcloud's `#00679E` | `step_settings_switch`: `QColor(0x25, 0x63, 0xEB)` | `src/gui/settingsswitch.cpp`; unit test `testSettingsSwitchColour` PASS on all three platforms |
| F2 `theme/colored/wizard-files.svg` in `rgb(0,130,201)` | `step_style` also recolours `rgb()` values | `fill:#2563EB` twice in the file |
| F3 `.nextcloudsync.log` probe file in every sync folder | `step_probe_files`: `.nodrivesync.log`, `.nodrivepermissions.log`; old names stay excluded | unit test `testFolderWatcherProbeFilesAreExcluded` PASS |
| Gate could not see colours written as numbers | colour patterns for decimal, `0x` and 0-1 forms; scope `code-colour` | `tools/branding/patterns.txt`, `tools/check-branding.py` |

## 2. User-visible leftovers

**None.** Every hit in shipped code, translations, packaging, images and the built
packages is classified in section 3.

Two borderline items are kept as internal:

| # | Where | Text | Why it is kept | Possible clean-up |
|---|---|---|---|---|
| B1 | `src/gui/application.cpp`, `src/libsync/logger.cpp`, `src/gui/logbrowser.cpp`, `src/gui/owncloudgui.cpp` | log file names `<yyyyMMdd_HHmm>_nextcloud.log`, cleanup pattern `*owncloud.log.*` | Diagnostic files in the app's own folders (`<config>/logs`, or the `NoDrive-logdir` temp folder). Never in a sync folder, never on an app screen; a user sees them only by browsing the app's data folder, through "Open folder" in the hidden Log Output window (F12 or Ctrl+L in Settings), or in a debug archive. Their lines carry the internal `nextcloud.*` logging categories, which may stay (module names). Rule: `tools/branding/allow-shared.txt` (reason reworded in the docs commit) | rename the base name to `nodrive.log` in `tools/rebrand.py` and keep the old patterns in the cleanup list |
| B2 | console output of `NoDrive --version` / `--help` (seen on macOS) | `nextcloud.gui.singleinstance: Primary instance listening on "..."` | a Qt info line on stderr with the internal logging category; only visible when the app is started from a terminal | lower the message to debug level, or hide that category's info output in release builds |

## 3. Kept hits, grouped

Each of the 10316 raw lines on `main` (`bd3bd8a412`) falls into exactly one row.
A script with ordered rules assigned them; the one line no rule caught (a comment
in `configfile.h`) was checked by hand and counted in C1.

| # | Pattern / where | Lines | Class | Reason it is kept |
|---|---|---|---|---|
| L1 | `SPDX-FileCopyrightText: ... Nextcloud GmbH and Nextcloud contributors`, `ownCloud GmbH`, author e-mails in source headers | 1831 | legal | Upstream copyright headers must stay |
| L2 | `COPYING`, `LICENSES/`, `AUTHORS.md`, `REUSE.toml`, `*.license`, `nodrive/licenses/*` (THIRD-PARTY-NOTICES: "based on the Nextcloud desktop client") | 52 | legal | Licence and credit files; they ship only as plain files next to the app, never on a screen |
| L3 | `src/gui/legalnotice.cpp` `tr("<p>Copyright 2017-2026 Nextcloud GmbH<br />Copyright 2012-2023 ownCloud GmbH</p>")` and its `.ts` entries (also in every `.qm`) | 94 | legal | The only live translatable Nextcloud text. The dialog opens only from `legalNoticeButton`, which `src/gui/infosettings.cpp` hides; no other caller. Decision in [NOTICE.md](../NOTICE.md) |
| N1 | Repo docs: `CHANGELOG.md`, `ChangeLog - Legacy.md`, `README.md`, `NOTICE.md`, `CONTRIBUTING.md`, `AGENTS.md`, `docs/`, upstream `doc/`, `src/csync/ChangeLog`, `.github/ISSUE_TEMPLATE` | 890 | not-shipped | Not packaged. Credits to Nextcloud belong in the repo |
| N2 | CI and tooling: `.github/workflows`, `.github/upstream-workflows`, `tools/`, `.tx/`, `.clang-*`, `codecov.yml`, `sonar-project.properties`, `craftmaster.ini`, `admin/*/ci/`, `admin/*/nodrive-craft*` | 441 | not-shipped | Build-time only |
| N3 | Tests: `test/`, SwiftPM/File Provider tests, QML test mocks | 600 | not-shipped | Not packaged |
| N4 | Unused build recipes: `admin/nix`, `admin/linux/flatpak`, `admin/osx/mac-crafter`, `admin/osx/sparkle`, `macosx.pkgproj.cmake`, `post_install.sh.cmake`, `admin/win/nsi`, `cmake/modules/NSIS*`, Xcode project/scheme/workspace files, `nextcloud.client-desktop/` (Transifex sources), `DS_Store.in` | 143 | not-shipped | Our CI ships only the AppImage (`admin/linux/build-appimage.sh`, no update information), the MSI (`admin/win/msi`) and the DMG (`admin/osx/nodrive-package.sh`: `NoDrive.app` + Applications link) |
| N5 | macOS File Provider: `shell_integration/MacOSX/NextcloudFileProviderKit/`, `FileProviderExt/`, `FileProviderUIExt/` (`Localizable.xcstrings`, `FileProviderDomainSymbol.svg`, user agent `Nextcloud-macOS/FileProviderUIExt`) | 229 | not-shipped | Not built: the macOS bundle has only `FinderSyncExt.appex`. **To do before the File Provider is switched on:** rebrand these strings and the domain symbol |
| N6 | Linux shell integration: `shell_integration/nautilus`, `dolphin`, `icons` (`oC_*.png`, `#0071AD` emblem), `libcloudproviders` | 70 | not-shipped | Not in the AppImage (0 such files, 0 file names with a keyword) |
| T1 | `.ts` metadata: `<location filename="../src/.../owncloudgui.cpp">`, context names `OCC::OwncloudSetupWizard`, `<comment>`, `<extracomment>` | 4195 | internal | Source paths, class names and translator notes; none reaches a screen |
| T2 | Stale `.ts` messages ("No Nextcloud account configured", "Open Nextcloud Talk/Assistant in browser", "creating folder on Nextcloud: %1", the notify_push link, provider sign-up text, old copyright years) | 59 | dead-code | 14 (context, source) pairs whose source text no longer exists in `src/`, so Qt never looks them up |
| C1 | Source comments (issue links to github.com/nextcloud and github.com/owncloud, `// ownCloud connect page`, ...) | 374 | internal | Compiled out |
| C2 | `tr("Open %1 Desktop", "Open Nextcloud main window. ...")` (`systray.cpp`, `cloudproviderwrapper.cpp`) | 2 | internal | The second argument is a translator hint; the user sees "Open NoDrive Desktop" |
| C3 | Debug log messages (`"** Application: Nextcloud found: "`, `possibleUpgradeFromMirall...`, `"Can only handle file ending in .owncloud"`, `"Retrying through owncloud"`) | 15 | internal | Log output only |
| I1 | Logging categories `nextcloud.gui.*` etc. (199), the rule `nextcloud.*.debug=true`, log file names (B1), QML/XPC ids `com.nextcloud.desktopclient.search`, `com.nextcloud.FinderSync.XPCQueue` | 254 | internal | Module and category names; only in logs |
| I2 | `#include "owncloudgui.h"`, `#import ".../NextcloudFileProviderXPC.h"`, QML `import com.nextcloud.desktopclient` | 61 | internal | Include paths and the QML module URI |
| S1 | `http://owncloud.org/ns`, `http://nextcloud.org/ns`, `xmlns:oc` | 53 | server-required | WebDAV namespaces defined by the server API |
| S2 | `src/common/utility.cpp` `Mozilla/5.0 (%1) mirall/%2 (%3, ...)` (`%3` = `NoDrive`) | 1 | server-required | Server 35 `IRequest::USER_AGENT_CLIENT_DESKTOP` matches the `mirall/` token |
| I3 | `OWNCLOUD_*` / `NEXTCLOUD_*` environment variables, CMake options and macros (`OWNCLOUD_TIMEOUT`, `BUILD_OWNCLOUD_OSX_BUNDLE`, `NEXTCLOUD_BACKGROUND_COLOR` = `#2563EB`, `NEXTCLOUD_DEV`) | 140 | internal | Developer and build switches, never displayed |
| I4 | `MIRALL_VERSION_*` macros, `mirall.desktop.in` | 83 | internal | Historic build macro names; the values are `35.0.0-nourax.1` |
| I5 | Migration constants and name comparisons (`unbrandedAppName`, `legacyAppName`, `/Nextcloud/nextcloud.cfg`, `/ownCloud/owncloud.cfg`, mono-icon default in `configfile.cpp`, prefix check in `utility_win.cpp`, unused `navigationpanehelper.h` constant) | 8 | internal | Compared or looked up only, never shown; account migration is off |
| I6 | `advancedsettings.cpp` `nextcloud-debug-archive-temp.zip` | 2 | internal | A temporary file, renamed to the name the user picks |
| I7 | `customstateprovider.cpp` `.owncloudsync.log` (plus `csync_exclude.cpp`, counted under C1) | 1 | internal | Legacy probe names kept excluded so they are never uploaded |
| I8 | `owncloud-authentication-failed`, `owncloud-need-retry`, `owncloud-custom-soft-error-string` | 4 | internal | Network request attribute keys |
| I9 | `clientsideencryption.cpp` end-to-end-encryption certificate request `O=Nextcloud` | 1 | internal | Sent only to drive.nourax.org, never displayed (optional: change to Nourax) |
| I10 | `networkjobs.cpp` `"nextcloud/" + status.php`; `src/gui/CMakeLists.txt` `STATE_SUBDIR "nextcloud/"` (only for the unbranded name) | 3 | internal | Path probe on our own server; an unused branch |
| D1 | Unreachable code: `theme.cpp` `docs.nextcloud.com` fallback (`APPLICATION_HELP_URL` is set), `wizard-nextcloud.png` resource names (the files hold the Nourax logo), updater code, `sparkleupdater_mac.mm` | 9 | dead-code | `BUILD_UPDATER OFF`; the built macOS binaries contain no `docs.nextcloud`, `updates.nextcloud` or `nextcloud.com` host |
| I11 | `xattrwrapper_linux.cpp` `user.nextcloud.hydrate_exec` | 1 | internal | Extended-attribute key, never displayed |
| I12 | `org.owncloud.PluginFactory`, `owncloud://` scheme mapping in `cmd.cpp`, `owncloud.exe.manifest-mingw`, a macOS install rule file name | 9 | internal | Plugin interface ids and internal file names |
| I13 | CMake targets, library and template names (`nextcloudCore`, `Nextcloud::sync`, `nextcloudsync_vfs_*` with output name `nodrivesync_vfs_*`, `Nextcloud.wxs`, `NextcloudCPack.cmake`, `systemd/nextcloud-desktop.service.in` with output `${LINUX_APPLICATION_ID}.service`, `theme/nextcloud.VisualElementsManifest.xml` (not installed)), and the `NEXTCLOUD.cmake` defaults | 183 | internal | Library names. `nodrive/NODRIVE.cmake` sets every product value after `NEXTCLOUD.cmake`. Packaged file names with a keyword: AppImage 0, macOS app 0, Windows install (2058 files) 0 |
| I14 | Class, function and type names (`ownCloudGui`, `OwncloudPropagator`, `OWNCLOUDSYNC_EXPORT`, `NextcloudSslCertificate`, ...) | 489 | internal | Class and module names, never rendered as text |
| I15 | Markup of unshipped upstream logo files (`theme/colored/Nextcloud-icon.svg`, `Nextcloud-w10startmenu.svg`, `icons/Nextcloud-icon-win-folder.svg`, `AppIcon.icon/`, `colored/nextcloud/state-*.svg`) | 18 | internal (not-shipped) | Not in the resolved resource file, not installed; kept as upstream files |
| | **Total** | **10316** | | |

## 4. Images

All 534 tracked image files were listed with their status against `v35.0.0` (174
added, 70 modified, 290 unchanged), whether they ship, and their share of
Nextcloud-blue pixels; then every shipped image and every image that could show a
logo or Nextcloud colours was looked at (rasters on contact sheets, all 169 SVGs
rendered in a browser): 533 of 534 files (the last one, `AppIcon.icon/icon.json`,
is not an image).

| Image set | Ships? | Result |
|---|---|---|
| 274 Qt-resource images (`theme.qrc.in` resolved for NoDrive): 137 rasters, 137 SVGs | yes, inside the binary | Clean. App and state icons are the Nourax "N"; 512 and 1024 px on a white square (approved). 22 shipped SVG wrappers embed the logo byte for byte. 115 glyph SVGs are generic icons in black, white, grey, status colours or `#2563EB`. 0 shipped images contain Nextcloud blue. Talk and Deck icons replaced with generic glyphs |
| Monochrome tray sets `theme/black|white/state-*` | yes | Full-colour Nourax logo (monochrome variant is an owner to-do) |
| Windows: `NoDrive.ico`, Start tile PNGs, MSI `banner.bmp` / `dialog.bmp`, CfApi state icons, `NCOverlays/ico/*.ico` | yes | Nourax logo, or generic glyphs (lock, person, check, X, !, share, sync arrows), none in the Nextcloud palette |
| macOS: `NoDrive.icns`, FinderSync badge `.icns` files | yes | Nourax logo and generic badges |
| Linux: `NoDrive.png` (hicolor 512 px and AppImage root) | yes | Nourax logo (CI: SHA-256 = `theme/colored/512-NoDrive-icon.png`) |
| Upstream Nextcloud logos (`Nextcloud-w10starttile.png`, `Nextcloud-icon*.svg`, `Nextcloud-macOS-icon.svg`, `AppIcon.icon/`, `Nextcloud-icon-win-folder.svg`, `*/nextcloud/state-*.svg`, `NextcloudFileProviderKit.*`, `shell_integration/icons/Sidebar/*`) | **no** | Not in the resolved resource file and not installed (install, app and AppImage trees checked); kept as upstream files |

## 5. Screenshot pass

57 screenshots from the CI runs on `bd3bd8a412` were looked at, including dark
mode, Arabic and German. The reviewed sets are committed in `docs/screenshots/`
(index: [testing.md](testing.md), section 3).

| Platform (run) | Images | Result |
|---|---|---|
| Linux (40189812444) | 16: en-light, de-light, ar-light, en-dark x wizard, tray menu, proxy dialog, sign-in | Clean: title "NoDrive", "Log in to NoDrive" / "Anmelden bei NoDrive", N logo in the tray, `#2563EB` buttons, mirrored layout in Arabic |
| Windows (40189812456) | 20: installer en/de, first start, 4 variants x 4 screens, app icon | Clean: "NoDrive Setup" with the N logo, tray "Exit NoDrive" / "NoDrive beenden" / Arabic, N logo in taskbar and tray |
| macOS (40189812446) | 21: 4 variants x 5 screens, icon click | Clean: menu bar "NoDrive", N logo in Dock and menu bar, `#2563EB` buttons, dark mode |

Gap (not a failure): no platform has a screenshot of Settings (General, Advanced,
Network), Info/About, the Log Output window or the Activity window. Settings opens
from the tray menu even before sign-in (`Systray::openSettings` ->
`ownCloudGui::slotShowSettings`), but the smoke tests do not capture it yet.
These screens were reviewed in code instead: Info shows
"NoDrive Desktop Client Version ...", the legal button is hidden, the updater
section is compiled out, "Usage Documentation" opens `https://drive.nourax.org`,
the switch colour is `#2563EB` (unit test), and the global ignore list
(`sync-exclude.lst`) has no keyword. They are part of the owner tests.

## 6. Contact with Nextcloud services

| Check | Result |
|---|---|
| Hosts in shipped code and config (update, push, crash, news, providers, app store) | Only the overridden `NEXTCLOUD.cmake` default (I13) and the dead `docs.nextcloud.com` fallback (D1). `BUILD_UPDATER OFF`, `WITH_PROVIDERS OFF`, `WITH_CRASHREPORTER OFF`. The only github.com link in the app is the About "Git revision" link to `github.com/NouraX-ORG/nodrive-desktop` |
| URL hosts compiled into the macOS binaries | `drive.nourax.org` plus schema and WebDAV namespace hosts; no `nextcloud.com`, `updates.nextcloud`, `push-notifications.nextcloud` or `apps.nextcloud`; no Sparkle |
| Linux CI network log | PASS: DNS only `drive.nourax.org` and runner GitHub hosts; the app connected only to drive.nourax.org and the local resolver |
| macOS CI network log | PASS: app connections only to drive.nourax.org |
| Windows CI network log | PASS: names looked up by the app: `drive.nourax.org`; no Nextcloud names or URLs |
| AppImage update information | none (`appimagetool -n` without `-u`) |

## 7. Packages

| Check | Result |
|---|---|
| MSI `NoDrive-35.0.0.18-nourax.1-x64.msi`, all 38 tables read | 32255 string cells, 0 keyword hits. ProductName NoDrive, Manufacturer Nourax, help links `https://drive.nourax.org/`; embedded languages de, fr, it, es also clean |
| macOS `.app`, every file scanned (UTF-8, UTF-16LE, UTF-16BE) | Info.plists: "NoDrive", "NoDrive FinderSync", "NoDrive Finder Integration", "NoDrive Edit Locally", "NoDrive by Nourax..."; 0 file names with a keyword; other hits are internal symbols, logging categories, QML module paths, L3, T2 and L2 |
| Linux `.desktop` in the AppImage | `Name=NoDrive Desktop`, `Icon=NoDrive`, 138 translated lines, 0 keyword hits |
| Windows install | Program Files `NoDrive`, `NoDrive.lnk`, "NoDrive" by "Nourax" in Apps & features, registry `Nourax\NoDrive`, Explorer handler "NoDrive context menu handler", overlays `NoDrive*`, 228 version resources with 0 hits |
| CI package sweeps (`check-branding.py --artifact`) | Linux, macOS and Windows: 61 hits each = 57 legal (Legal Notice line in `.qm` files, THIRD-PARTY-NOTICES) + 4 dead-code (stale `.qm` text), 0 unclassified: PASS |

## 8. The gate in CI (main `bd3bd8a412`)

| Run | Job | Summary |
|---|---|---|
| Linux 40189812444 | Branding gate: success | `check-branding [lane linux]: 653 hits (unclassified 0, internal 567, legal 86)` |
| macOS 40189812446 | Branding gate: success | `check-branding [lane macos]: 784 hits (unclassified 0, internal 697, legal 87)` |
| Windows 40189812456 | Branding checks: success | `check-branding [lane windows]: 665 hits (unclassified 0, internal 577, legal 88)` |

## 9. Follow-ups (none blocks the gate)

| # | Item | Lane |
|---|---|---|
| 1 | B1: optionally rename the log base name to `nodrive.log` (the allowlist reason was reworded in the docs commit) | shared |
| 2 | B2: optionally hide the `nextcloud.gui.singleinstance` info line in release console output | shared |
| 3 | Add Settings (General, Advanced, Network) and Info screenshots, in light, dark, German and Arabic, to the three smoke tests (reachable from the tray menu before sign-in); until then the owner looks at them | shared + windows + macos |
| 4 | Optional: end-to-end-encryption certificate request `O=Nextcloud` -> Nourax (I9) | shared |
| 5 | Optional: the Windows-only fallback dark palette uses `#2A82DA` (from a third-party dark style, not a Nextcloud colour) for highlights; consider `#3B82F6` | shared |
| 6 | `conflictHelpUrl()` builds `https://drive.nourax.org/conflicts.html`, which probably does not exist (not a branding issue) | shared |
| 7 | The macOS `net/network-report.md` title says "(Linux smoke test)" (label in a reused script) | macos |
| 8 | Before the File Provider is switched on: rebrand `FileProviderUIExt` strings, its user agent and `FileProviderDomainSymbol.svg` (N5) | macos |
| 9 | The Windows install folder contains Craft build leftovers (`gettext*`, `*-config`, `qt-cmake*.bat`); no Nextcloud text, but clutter | windows |
| 10 | For the Workspace session: the server shows the device as "Sync client - 35.0.0 (OS)"; ask the owner to read it back from Settings > Security | server |

## 10. How to repeat the sweep

```sh
P='nextcloud|next[ _-]?cloud|owncloud|own[ _-]cloud|mirall|it\.niedermann|1125420102|0082c9|00679e|0, ?130, ?201|0, ?103, ?158'
git grep -I -i -n -E "$P" v35.0.0 -- . | wc -l      # before
git grep -I -i -n -E "$P" HEAD -- . | wc -l         # after
python tools/check-branding.py --lane all           # the gate (must PASS)
python tools/check-branding.py --lane all --list-all > hits.txt   # every hit with its class
python tools/check-branding.py --lane all --write-sweep           # refresh section 11
```

Then download the screenshots of the newest runs on `main`
(`gh run download <run id> -n nodrive-<os>-screenshots`) and look at every image.

## 11. Gate report (generated)

<!-- check-branding:begin (generated by python tools/check-branding.py --lane all --write-sweep; do not edit by hand) -->

### Branding sweep (lane all)

Generated by `python tools/check-branding.py`. Classes: unclassified = user-visible until fixed (gate fails); internal (internal, server-required, not-shipped, dead-code) = kept with a reason; legal = kept.

```
check-branding [lane all]: 848 hits (unclassified 0, internal 759, legal 89); files scanned 1660 {'code': 854, 'image': 474, 'packaging': 136, 'qml': 110, 'strings': 3, 'ts': 64, 'ui': 19}
  by class: {'dead-code': 74, 'internal': 440, 'legal': 89, 'not-shipped': 184, 'server-required': 61}
  by scope: {'code': 430, 'image': 47, 'packaging': 171, 'strings': 22, 'svg': 26, 'ts': 152}
```

#### Unclassified hits

None.

#### Kept hits by rule

| Class | Rule | Hits | Reason | Example |
|---|---|---|---|---|
| dead-code | auto:stale-ts | 69 | stale translation: its source text is no longer in src/, so it is never shown | `translations/client_af.ts`:798 [OCC::ConnectionValidator] <source> No Nextcloud account configured |
| dead-code | allow-shared.txt:59 | 3 | Updater code; not built (BUILD_UPDATER OFF in nodrive/NODRIVE.cmake). | `src/gui/updater/sparkleupdater_mac.mm`:273 Auto-updating has been set to skip in nextcloud.cfg, won't check for update. |
| dead-code | allow-windows.txt:15 | 1 | unbrandedApplicationName is no longer used: tools/rebrand.d/windows.py makes navigationpanehelper.cpp remove only NoDrive's own Explorer entries. | `src/gui/navigationpanehelper.h`:34 Nextcloud |
| dead-code | allow-shared.txt:60 | 1 | #else branch for an empty APPLICATION_HELP_URL; NODRIVE.cmake stops the configure when it is empty, so this is never compiled. | `src/libsync/theme.cpp`:498 https://docs.nextcloud.com/server/latest/user_manual/en/desktop/index.html |
| internal | allow-shared.txt:18 | 252 | Qt logging category name; appears only in debug logs and --logdebug rules. | `src/common/checksumcalculator.cpp`:20 nextcloud.common.checksumcalculator |
| internal | allow-shared.txt:23 | 29 | Name of an environment variable read for debugging or tuning; never shown. | `src/common/checksums.cpp`:164 OWNCLOUD_DISABLE_CHECKSUM_UPLOAD |
| internal | allow-macos.txt:20 | 26 | Xcode target, group, path and Swift package names inside the build project; never shown. The built extensions take their names and ids from OC_APPLICATION_NAME and OC_APPLICATION_REV_DOMAIN (NoDrive values, passed by CMake). | `shell_integration/MacOSX/NextcloudIntegration/NextcloudIntegration.xcodeproj/project.pbxproj`:29 AA5FB9E62EF54F410018BDAE /* NextcloudFileProviderKit in Frameworks */ = {isa = P |
| internal | allow-windows.txt:10 | 15 | CMake version variables (MIRALL_VERSION_*); configure replaces them with the version number. | `admin/win/msi/CMakeLists.txt`:11 set(VERSION "${MIRALL_VERSION_MAJOR}.${MIRALL_VERSION_MINOR}.${MIRALL_VERSION_PA |
| internal | auto:manifest | 9 | file name only; content is the NoDrive logo made by tools/make-icons.py (icons-manifest) | `theme/black/wizard-nextcloud.png`:0 file name theme/black/wizard-nextcloud.png |
| internal | allow-shared.txt:50 | 6 | CMake variable/file names in the CPack setup (CPack is not used for NoDrive packages). | `NextcloudCPack.cmake`:8 include("${CMAKE_SOURCE_DIR}/NEXTCLOUD.cmake") |
| internal | allow-shared.txt:56 | 6 | Build-time KDE Craft settings (blueprints, binary cache) used only on the CI build machine, never by the app. | `craftmaster.ini`:5 CraftUrl = https://github.com/nextcloud/craft.git |
| internal | allow-shared.txt:21 | 6 | Qt plugin interface id of the sync plugins (internal identifier). | `src/common/plugin.h`:47 org.owncloud.PluginFactory |
| internal | allow-shared.txt:55 | 6 | Resource file names; the files now hold the NoDrive logo (tools/make-icons.py, icons-manifest). | `theme.qrc.in`:160 <file>theme/colored/wizard-nextcloud.png</file> |
| internal | allow-shared.txt:54 | 5 | CMake variable names (filled with the NoDrive version). | `src/gui/version.rc.in`:3 #define VER_FILEVERSION @MIRALL_VERSION_MAJOR@,@MIRALL_VERSION_MINOR@,@MIRALL_VE |
| internal | allow-shared.txt:51 | 4 | CMake variable/file names in the CPack setup (CPack is not used for NoDrive packages). | `CPackOptions.cmake.in`:17 include("${CMAKE_SOURCE_DIR}/NEXTCLOUD.cmake") |
| internal | allow-shared.txt:48 | 4 | Upstream build logic that tests the unbranded name; NoDrive's values are set afterwards by nodrive/NODRIVE.cmake. | `NEXTCLOUD.cmake`:7 if(NEXTCLOUD_DEV) |
| internal | allow-shared.txt:52 | 3 | CMake variable and version.h macro names used by the build script (the value is the NoDrive version). | `admin/linux/build-appimage.sh`:47 -DMIRALL_VERSION_BUILD=$BUILDNR \ |
| internal | allow-macos.txt:27 | 3 | CMake variable names (NoDrive version, path of NoDrive.app). | `admin/osx/CMakeLists.txt`:52 DESTINATION ${OWNCLOUD_OSX_BUNDLE}/Contents/Resources) |
| internal | allow-macos.txt:26 | 3 | CMake variable names, filled with the NoDrive version at configure time. | `cmake/modules/MacOSXBundleInfo.plist.in`:33 <string>@APPLICATION_NAME_XML_ESCAPED@ @MIRALL_VERSION_STRING@</string> |
| internal | allow-shared.txt:49 | 3 | Upstream CMake variable names that NoDrive sets. | `nodrive/NODRIVE.cmake`:33 set(MIRALL_VERSION_SUFFIX "-nourax.${NODRIVE_RELEASE}") |
| internal | allow-shared.txt:24 | 3 | Probe file names of upstream builds, kept on the exclude list so such a file is never uploaded. NoDrive itself creates .nodrivesync.log and .nodrivepermissions.log in the sync folder (tools/rebrand.py); any other use of the old names fails this gate. | `src/csync/csync_exclude.cpp`:145 .owncloudsync.log |
| internal | allow-shared.txt:33 | 3 | QNetworkRequest attribute keys (internal). | `src/libsync/creds/httpcredentials.cpp`:37 owncloud-authentication-failed |
| internal | allow-shared.txt:26 | 3 | Legacy config paths used only by account migration, which NoDrive switches off (DISABLE_ACCOUNT_MIGRATION ON). | `src/libsync/settings/migration.cpp`:19 owncloud.cfg |
| internal | auto:overlay | 2 | upstream default of APPLICATION_NAME; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:8 set( APPLICATION_NAME "NextcloudDev" ) |
| internal | auto:overlay | 2 | upstream default of APPLICATION_SHORTNAME; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:9 set( APPLICATION_SHORTNAME "NextcloudDev" ) |
| internal | auto:overlay | 2 | upstream default of APPLICATION_EXECUTABLE; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:10 set( APPLICATION_EXECUTABLE "nextclouddev" ) |
| internal | auto:overlay | 2 | upstream default of APPLICATION_ICON_NAME; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:11 set( APPLICATION_ICON_NAME "Nextcloud" ) |
| internal | allow-windows.txt:12 | 2 | WiX source and object file names used only while building the MSI; not shipped, not shown. | `admin/win/msi/make-msi.bat.in`:110 "%WIX%\bin\candle.exe" -dcodepage=!codepage! -dProductCode=%PRODUCTCODE% -dPlatf |
| internal | allow-shared.txt:22 | 2 | KDE plugin id and metadata file name of the Dolphin overlay plugin (internal identifiers). | `shell_integration/dolphin/ownclouddolphinoverlayplugin.cpp`:17 com.owncloud.ovarlayiconplugin |
| internal | allow-shared.txt:25 | 2 | Internal log file names in the app's own folders (<config>/logs, or the NoDrive-logdir temp folder when temporary logging is on); never in a sync folder and never on an app screen. A user sees them only by browsing the app's data folder, through "Open folder" in the hidden Log Output window (F12 or Ctrl+L in Settings), or inside a debug archive made for support. | `src/gui/advancedsettings.cpp`:130 nextcloud-debug-archive-temp.zip |
| internal | allow-shared.txt:27 | 2 | Unbranded/legacy app names used to find old config folders for migration (switched off). | `src/libsync/configfile.h`:327 Nextcloud |
| internal | allow-shared.txt:34 | 2 | Request attribute key and a debug log message. | `src/libsync/propagatedownload.cpp`:809 owncloud-custom-soft-error-string |
| internal | auto:overlay | 1 | upstream default of APPLICATION_DOMAIN; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:20 set( APPLICATION_DOMAIN "nextcloud.com" ) |
| internal | auto:overlay | 1 | upstream default of APPLICATION_VENDOR; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:21 set( APPLICATION_VENDOR "Nextcloud GmbH" ) |
| internal | auto:overlay | 1 | upstream default of APPLICATION_UPDATE_URL; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:22 set( APPLICATION_UPDATE_URL "https://updates.nextcloud.org/client/" CACHE STRING |
| internal | auto:overlay | 1 | upstream default of APPLICATION_REV_DOMAIN; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:84 set( APPLICATION_REV_DOMAIN "com.nextcloud.desktopclient" ) |
| internal | auto:overlay | 1 | upstream default of APPLICATION_REV_DOMAIN_DBUS; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:85 set( APPLICATION_REV_DOMAIN_DBUS "desktopclient.nextcloud.com" ) |
| internal | auto:overlay | 1 | upstream default of APPLICATION_VIRTUALFILE_SUFFIX; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:87 set( APPLICATION_VIRTUALFILE_SUFFIX "nextcloud" CACHE STRING "Virtual file suffi |
| internal | auto:overlay | 1 | upstream default of LINUX_PACKAGE_SHORTNAME; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:91 set( LINUX_PACKAGE_SHORTNAME "nextcloud" ) |
| internal | auto:overlay | 1 | upstream default of THEME_CLASS; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:94 set( THEME_CLASS "NextcloudTheme" ) |
| internal | auto:overlay | 1 | upstream default of NEXTCLOUD_BACKGROUND_COLOR; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:117 set(NEXTCLOUD_BACKGROUND_COLOR "#0082c9" CACHE STRING "Default Nextcloud backgro |
| internal | auto:overlay | 1 | upstream default of APPLICATION_WIZARD_HEADER_BACKGROUND_COLOR; overridden by nodrive/NODRIVE.cmake | `NEXTCLOUD.cmake`:118 set( APPLICATION_WIZARD_HEADER_BACKGROUND_COLOR ${NEXTCLOUD_BACKGROUND_COLOR} CA |
| internal | allow-windows.txt:13 | 1 | Name of the upstream Craft blueprint (package) that builds the client; CI only. | `admin/win/ci/make-msi-ci.ps1`:23 [string] $PackageName = 'nextcloud-client' |
| internal | allow-windows.txt:11 | 1 | Name of the WiX source file that builds the MSI; used only while building, not shipped, not shown. | `admin/win/msi/CMakeLists.txt`:35 Nextcloud.wxs |
| internal | allow-macos.txt:23 | 1 | Name of an internal dispatch queue inside the Finder extension (shows only in debuggers and crash logs). | `shell_integration/MacOSX/NextcloudIntegration/FinderSyncExt/FinderSyncXPCManager.m`:85 com.nextcloud.FinderSync.XPCQueue |
| internal | allow-macos.txt:21 | 1 | Xcode project attribute used only by Xcode's new-file templates; not part of any built product. | `shell_integration/MacOSX/NextcloudIntegration/NextcloudIntegration.xcodeproj/project.pbxproj`:503 ORGANIZATIONNAME = "Nextcloud GmbH"; |
| internal | allow-shared.txt:45 | 1 | Build hook that replaces the placeholder name with APPLICATION_SHORTNAME. | `shell_integration/nautilus/setappname.sh`:12 sed -i.org -e "s/Nextcloud/$1/g" syncstate.py |
| internal | allow-shared.txt:44 | 1 | Placeholder that shell_integration/nautilus/setappname.sh replaces with APPLICATION_SHORTNAME (NoDrive) at configure time. | `shell_integration/nautilus/syncstate.py`:24 Nextcloud |
| internal | allow-shared.txt:40 | 1 | Legacy owncloud:// URL scheme accepted on the command line (mapped to http). | `src/cmd/cmd.cpp`:557 owncloud |
| internal | allow-shared.txt:37 | 1 | Debug log message. | `src/common/syncjournaldb.cpp`:608 possibleUpgradeFromMirall_1_8_0_or_1 detected! |
| internal | allow-windows.txt:14 | 1 | Utility::syncFolderDisplayName() recognises old sync folders named "Nextcloud<N>" and gives them the app name (NoDrive) in Explorer; the literal itself is never shown. | `src/common/utility_win.cpp`:191 Nextcloud |
| internal | allow-shared.txt:53 | 1 | CMake variable name (filled with the NoDrive version). | `src/common/vfspluginmetadata.json.in`:3 "version": "@MIRALL_VERSION_STRING@" |
| internal | allow-shared.txt:38 | 1 | Warning in the debug log about legacy .owncloud link files. | `src/gui/application.cpp`:1364 Can only handle file ending in .owncloud. Unable to open |
| internal | allow-shared.txt:42 | 1 | tr() disambiguation comment for translators; not displayed. | `src/gui/cloudproviders/cloudproviderwrapper.cpp`:239 Open Nextcloud main window. Placeholer will be the application name. Please keep |
| internal | allow-shared.txt:39 | 1 | Debug log message. | `src/gui/connectionvalidator.cpp`:132 ** Application: Nextcloud found: |
| internal | allow-shared.txt:43 | 1 | Resource file name, used only by MinGW builds. | `src/gui/manifest-mingw.rc`:3 owncloud.exe.manifest-mingw |
| internal | allow-shared.txt:41 | 1 | tr() disambiguation comment for translators; not displayed. | `src/gui/systray.cpp`:576 Open Nextcloud main window. Placeholer will be the application name. Please keep |
| internal | allow-shared.txt:32 | 1 | Organisation (O) field of the end-to-end-encryption certificate request sent to the server; not shown in the app. | `src/libsync/clientsideencryption.cpp`:1983 Nextcloud |
| internal | allow-shared.txt:28 | 1 | Compares APPLICATION_NAME with the unbranded name to pick a default; never shown. | `src/libsync/configfile.cpp`:1237 Nextcloud |
| internal | allow-shared.txt:19 | 1 | Logging rule that enables the internal nextcloud.* logging categories. | `src/libsync/logger.cpp`:225 nextcloud.*.debug=true |
| internal | allow-shared.txt:29 | 1 | Theme::isBranded() compares the app name with the unbranded name; never shown. | `src/libsync/theme.cpp`:149 Nextcloud |
| internal | allow-shared.txt:30 | 1 | Translation context name, not displayed. | `src/libsync/theme.cpp`:638 nextcloudTheme::aboutInfo() |
| internal | allow-shared.txt:31 | 1 | Resource file name; the file now holds the NoDrive logo (tools/make-icons.py, icons-manifest). | `src/libsync/theme.cpp`:807 colored/wizard-nextcloud.png |
| internal | allow-shared.txt:36 | 1 | Legacy probe file name that the Explorer extension ignores. | `src/libsync/vfs/cfapi/shellext/customstateprovider.cpp`:44 .owncloudsync.log |
| internal | allow-shared.txt:35 | 1 | Extended file attribute name used by the openvfs hydration helper (OS-level identifier). | `src/libsync/vfs/xattr/xattrwrapper_linux.cpp`:17 user.nextcloud.hydrate_exec |
| legal | allow-shared.txt:8 | 83 | Upstream copyright line of the Legal Notice dialog, kept as legal text. NoDrive hides the dialog's button (NOTICE.md); the line is never shown. | `translations/client_ar.ts`:4299 [OCC::LegalNotice] <source> <p>Copyright 2017-2026 Nextcloud GmbH<br />Copyright |
| legal | allow-windows.txt:7 | 2 | Upstream copyright header of the MSI script templates, kept unchanged (spec section 5); not part of any installer text. | `admin/win/msi/RegistryCleanup.vbs.in`:1 ' SPDX-FileCopyrightText: 2022 Nextcloud GmbH and Nextcloud contributors |
| legal | allow-shared.txt:9 | 2 | Upstream copyright line in the (unreachable) Legal Notice dialog; legal text is kept unchanged. | `src/gui/legalnotice.cpp`:47 <p>Copyright 2017-2026 Nextcloud GmbH<br /> |
| legal | allow-shared.txt:10 | 1 | Licence texts and third-party notices shipped as files next to the app; naming the copyright holders is required by the licences. | `nodrive/licenses/THIRD-PARTY-NOTICES.txt`:15 NoDrive desktop app (based on the Nextcloud desktop client 35.0.0) \| GPL-2.0-or- |
| legal | allow-macos.txt:8 | 1 | Upstream copyright header, kept unchanged. | `shell_integration/MacOSX/NextcloudIntegration/NextcloudDev/Build.xcconfig`:1 // SPDX-FileCopyrightText: 2025 Nextcloud GmbH and Nextcloud contributors |
| not-shipped | auto:qrc | 54 | not listed in theme.qrc.in (with NoDrive values) and not installed by CMake | `theme/black/nextcloud/state-error.svg`:0 file name theme/black/nextcloud/state-error.svg |
| not-shipped | allow-macos.txt:15 | 41 | NextcloudFileProviderKit Swift package (and its docs, logo and tests): used only by the File Provider extensions, which this build does not compile. | `shell_integration/MacOSX/NextcloudFileProviderKit/NextcloudFileProviderKit.png`:0 file name shell_integration/MacOSX/NextcloudFileProviderKit/NextcloudFileProvide |
| not-shipped | allow-macos.txt:17 | 35 | File Provider UI extension (share/lock dialogs and their texts): not built while BUILD_FILE_PROVIDER_MODULE is OFF. | `shell_integration/MacOSX/NextcloudIntegration/FileProviderUIExt/Assets.xcassets/FileProviderDomainSymbol.symbolset/FileProviderDomainSymbol.svg`:0 file name shell_integration/MacOSX/NextcloudIntegration/FileProviderUIExt/Assets |
| not-shipped | allow-shared.txt:61 | 19 | Upstream Flatpak manifest and metainfo; NoDrive ships no Flatpak. | `admin/linux/flatpak/com.nextcloud.desktopclient.nextcloud.metainfo.xml`:3 <id>com.nextcloud.desktopclient.nextcloud</id> |
| not-shipped | allow-windows.txt:20 | 11 | CPack NSIS installer template; NoDrive ships an MSI and never builds the NSIS installer (Packager/PackageType = PortablePackager in admin/win/nodrive-craftmaster.ini). | `cmake/modules/NSIS.template.in`:1 ;ownCloud installer script. |
| not-shipped | allow-macos.txt:16 | 10 | File Provider extension: not built while BUILD_FILE_PROVIDER_MODULE is OFF (classic sync build). | `shell_integration/MacOSX/NextcloudIntegration/FileProviderExt/Assets.xcassets/FileProviderDomainSymbol.symbolset/FileProviderDomainSymbol.svg`:0 file name shell_integration/MacOSX/NextcloudIntegration/FileProviderExt/Assets.x |
| not-shipped | allow-macos.txt:29 | 4 | mac-crafter, upstream's developer build tool, and its README; NoDrive CI builds with KDE Craft (admin/osx/nodrive-craft.sh) and does not ship it. | `admin/osx/mac-crafter/README.md`:2 - SPDX-FileCopyrightText: 2024 Nextcloud GmbH and Nextcloud contributors |
| not-shipped | allow-windows.txt:18 | 3 | NSIS installer notes; NoDrive ships an MSI (docs/build-windows.md) and nothing from admin/win/nsi is packaged. | `admin/win/nsi/README.md`:2 - SPDX-FileCopyrightText: 2014 ownCloud GmbH |
| not-shipped | allow-macos.txt:22 | 2 | Bundle id of the Xcode-only "desktopclient" host app used when developing the extensions in Xcode; CMake never builds this target. | `shell_integration/MacOSX/NextcloudIntegration/NextcloudIntegration.xcodeproj/project.pbxproj`:1298 PRODUCT_BUNDLE_IDENTIFIER = "com.nextcloud.$(PRODUCT_NAME:rfc1034identifier)"; |
| not-shipped | allow-shared.txt:63 | 2 | Icon Composer icon; not used (MACOS_USE_ICON_COMPOSER OFF in NODRIVE.cmake). | `theme/colored/AppIcon.icon/icon.json`:22 "image-name" : "Nextcloud-logo-white.svg", |
| not-shipped | allow-shared.txt:64 | 2 | Upstream tile manifest; the build installs theme/nodrive.VisualElementsManifest.xml (${APPLICATION_EXECUTABLE}). | `theme/nextcloud.VisualElementsManifest.xml`:5 Square150x150Logo="visualelements\Nextcloud-w10starttile.png" |
| not-shipped | allow-macos.txt:28 | 1 | Post-install script of upstream's .pkg installer; NoDrive ships only a .dmg (admin/osx/nodrive-package.sh), so it never runs. | `admin/osx/post_install.sh.cmake`:17 LOG_PREFIX="Nextcloud FinderSync:" |
| server-required | allow-shared.txt:13 | 59 | WebDAV XML namespace of the server (oc: / nc: properties); the server only answers to these exact URIs. | `src/gui/editlocallyjob.cpp`:106 http://owncloud.org/ns:size |
| server-required | allow-shared.txt:14 | 1 | User agent: Nextcloud server 35 recognises desktop clients by "mirall/<version>" (IRequest::USER_AGENT_CLIENT_DESKTOP). The app-name part says NoDrive. | `src/common/utility.cpp`:157 Mozilla/5.0 (%1) mirall/%2 (%3, %4-%5 ClientArchitecture: %6 OsArchitecture: %7) |
| server-required | allow-shared.txt:15 | 1 | Path segment used to detect servers installed under /nextcloud/ (URL handling), not shown. | `src/libsync/networkjobs.cpp`:579 nextcloud/ |

#### Allowlist entries with no hit in this run

- allow-macos.txt:32 `Contents/Resources/licenses/*` legal
- allow-macos.txt:33 `Contents/Resources/Translations/client_*.qm` legal
- allow-macos.txt:34 `Contents/Resources/Translations/client_*.qm` dead-code
- allow-shared.txt:20 `src/*` internal
- allow-shared.txt:62 `admin/test/*` not-shipped
- allow-shared.txt:67 `usr/share/doc/nodrive/*` legal
- allow-shared.txt:68 `usr/share/nodrive/i18n/client_*.qm` legal
- allow-shared.txt:69 `usr/share/nodrive/i18n/client_*.qm` dead-code
- allow-windows.txt:19 `admin/win/tools/NCNavRemove/*` not-shipped
- allow-windows.txt:23 `licenses/*` legal
- allow-windows.txt:24 `i18n/client_*.qm` legal
- allow-windows.txt:25 `i18n/client_*.qm` dead-code

<!-- check-branding:end -->
