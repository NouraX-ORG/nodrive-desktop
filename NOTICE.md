# NOTICE

NoDrive for Windows, macOS and Linux is made by **Nourax**. It is based on the
**Nextcloud desktop client** (https://github.com/nextcloud/desktop), tag
`v35.0.0`. Thank you to Nextcloud GmbH, ownCloud GmbH and every contributor of
the Nextcloud desktop client.

## Licensed software

| Component | Licence | Licence text in this repository |
|---|---|---|
| NoDrive desktop app: the Nextcloud desktop client 35.0.0 and all Nourax changes | GPL-2.0-or-later | `COPYING` |
| Upstream documentation (`doc/`, not shipped) | as marked in `REUSE.toml` and the file headers (for example CC-BY-3.0) | `COPYING.documentation`, `LICENSES/` |
| Qt 6 | LGPL-3.0-only | `nodrive/licenses/LGPL-3.0-or-later.txt`, `GPL-3.0-or-later.txt` |
| KDE Frameworks (KArchive, KGuiAddons, KDBusAddons) and bundled KDE parts (Kirigami, KMessageWidget) | LGPL-2.0-or-later / LGPL-2.1-or-later | `nodrive/licenses/LGPL-2.0-or-later.txt`, `LGPL-2.1-or-later.txt` |
| QtKeychain | BSD-3-Clause | `nodrive/licenses/BSD-3-Clause.txt` |
| OpenSSL 3 | Apache-2.0 | `nodrive/licenses/Apache-2.0.txt` |
| KDSingleApplication | MIT | `nodrive/licenses/MIT.txt` |
| zlib | Zlib | `nodrive/licenses/Zlib.txt` |
| SQLite | public domain | (none needed) |
| libp11 | LGPL-2.1-or-later | `nodrive/licenses/LGPL-2.1-or-later.txt` |
| Other upstream files | as marked in their SPDX headers and `REUSE.toml` | `LICENSES/` |

- Upstream copyright headers, `COPYING`, `COPYING.documentation`, `LICENSES/`,
  `REUSE.toml` and `AUTHORS.md` are kept unchanged.
- `REUSE.toml` is upstream's file. It still marks some image paths as
  `LicenseRef-NextcloudTrademarks` (for example `theme/colored/wizard_logo.png`
  and the MSI banners). In NoDrive these paths hold the Nourax logo, made by
  `tools/make-icons.py`; every such file is listed in
  `tools/branding/icons-manifest.txt`.
- `nodrive/licenses/` (made by `tools/rebrand.py`) holds `COPYING.txt`, the
  licence texts above and `THIRD-PARTY-NOTICES.txt` (component, licence and
  copyright holders). Every package ships this folder (table at the end).
- The complete source code of every build is this repository, at the commit the
  build was made from. Test builds are made only by this repository's GitHub
  Actions workflows, so the source of each build is the run's commit.

## Trademarks

- "Nextcloud" and the Nextcloud logo are trademarks of **Nextcloud GmbH**
  (`LICENSES/LicenseRef-NextcloudTrademarks.txt`).
- **NoDrive is not made or endorsed by Nextcloud GmbH.**
- NoDrive does not use the Nextcloud name or logo in the app. Credits to
  Nextcloud are only in this repository (README, NOTICE, licence files, source
  headers), never on an app screen.
- Shipped pictures that showed the Nextcloud logo are replaced by the Nourax logo
  (`tools/make-icons.py`) or by neutral upstream glyphs (`tools/rebrand.py`).
  Upstream logo files that are not built into the app stay in the repository
  unchanged.
- "NoDrive" and the Nourax logo belong to Nourax.

## What Nourax changed

Every change is made by a tool in `tools/`, so it can be applied again after an
upstream update (`docs/upstream-updates.md`):

| Area | Change | Tool |
|---|---|---|
| Names and ids | App name NoDrive, maker Nourax, executable `nodrive`, every bundle id, D-Bus name, registry key, CLSID and folder name (`docs/branding.md`) | `tools/rebrand.py` renders `nodrive/NODRIVE.cmake` from `tools/branding/ids.json`; `CMakeLists.txt` includes it |
| Version | `35.0.0-nourax.1` | same |
| Server | Fixed to `https://drive.nourax.org`; no server-entry field, no "Sign up" or "Self-host" buttons; other servers from the command line or a config file are refused | `tools/rebrand.py` (wizard QML and controller, command-line and config-file checks) |
| Switched off | Updater, providers list, crash reporter, account import from the real Nextcloud client | `nodrive/NODRIVE.cmake` |
| Links | Help opens `https://drive.nourax.org`; "Built from Git revision" opens this repository; no link to a Nextcloud site; a privacy link appears only when `NOURAX_PRIVACY_URL` is set (empty today) | `tools/rebrand.py` |
| Texts | The four translatable messages that named Nextcloud, in every language; three English-only texts | `tools/rebrand.py` |
| Logo and colours | Nourax logo for every app, tray, installer and wizard image; Nourax blue `#2563EB` instead of Nextcloud's blues | `tools/make-icons.py`, `tools/rebrand.py` |
| Files in sync folders | Folder-watcher probe files are `.nodrivesync.log` / `.nodrivepermissions.log` | `tools/rebrand.py` |
| Side by side | New Windows CLSIDs and MSI upgrade code; the Explorer sidebar cleanup removes only NoDrive's own entries; own macOS bundle ids and app group | `ids.json`, `tools/rebrand.d/windows.py`, `tools/rebrand.d/macos.py` |
| Windows packaging | NoDrive version resources of the shell-extension DLLs, MSI file name | `tools/rebrand.d/windows.py` |
| macOS packaging | Info.plist texts, no Sparkle keys, Xcode fallback ids, Finder menu fallback name, a notification text | `tools/rebrand.d/macos.py` |
| Linux packaging | AppImage name, icon and licence folder | `tools/rebrand.d/linux.py` |
| Legal page | The "Legal Notice" button is hidden (decision below) | `tools/rebrand.py` |
| Tests | `test/testnodrivebranding.cpp` (names, fixed server, user agent, colours, probe files); provisioning tests expect other servers to be refused | `tools/rebrand.py` |
| CI | Upstream workflows moved to `.github/upstream-workflows/` (reference only); our own `linux.yml`, `windows.yml`, `macos.yml` | by hand |
| Checks | `tools/check-branding.py` fails the build when a user-visible Nextcloud name, link, logo or colour comes back | by hand |

## Legal notices in the app (decision)

**This is not legal advice.** It is our reading of the licences, written down so
that the owner can check it.

What upstream shows: Settings > Info has a "Legal Notice" button. Its dialog
(`src/gui/legalnotice.cpp`) shows "Copyright 2017-2026 Nextcloud GmbH /
Copyright 2012-2023 ownCloud GmbH", "Licensed under the GNU General Public
License (GPL) Version 2.0 or any later version" and a link to Nextcloud's
GitHub repository. Nothing is shown at start-up.

What the licences ask for (our reading):

- **GPL-2.0-or-later, section 2(c)**: an interactive program must show a
  notice only if the original program normally prints one when it starts.
  The upstream client prints none at start-up, so the modified program does
  not have to either. Sections 1 and 3 are met by shipping `COPYING` with
  every package and keeping the complete source public in this repository.
- **LGPL (Qt, KDE Frameworks, libp11)**: the licence texts must accompany each
  copy; dynamic linking keeps relinking possible. LGPL-3.0 section 4(c) only
  applies when the app itself displays copyright notices, which NoDrive does not.
- **Apache-2.0 (OpenSSL 3), MIT, BSD-3-Clause, zlib**: the notices and licence
  texts must be included with the distribution; a text file is enough.

**Decision: no in-app legal page.** Reason: no licence of the shipped
components requires the app itself to show a notice, and the spec asks for no
Nextcloud name on any app screen. So the "Legal Notice" button is hidden
(`src/gui/infosettings.cpp`, applied by `tools/rebrand.py`); the dialog code
stays in the source but nothing opens it. Instead, every package ships
`nodrive/licenses/` (`COPYING.txt`, the needed licence texts and
`THIRD-PARTY-NOTICES.txt`):

| Package | Where the licences are |
|---|---|
| AppImage (Linux) | `usr/share/doc/nodrive/` |
| MSI (Windows) | `C:\Program Files\NoDrive\licenses\` |
| DMG (macOS) | `NoDrive.app/Contents/Resources/licenses/` |

CI checks that each package contains this folder (`docs/testing.md`).

Fallback if the owner prefers more caution: show the button again with one
plain-text "Open-source licences" page reached from Settings > Info (no logos,
no links to nextcloud.com; it may name the copyright holders).
