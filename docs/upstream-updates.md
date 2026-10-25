# Taking a new upstream release

NoDrive follows **stable releases** of the Nextcloud desktop client
(`https://github.com/nextcloud/desktop`, tags like `v35.0.1`), never `master` or a
release candidate. Today's base is `v35.0.0`.

All branding is re-applied by tools, so an update is: merge the tag, take
upstream's version of every file the tools patch, re-run the tools, re-run the
sweep, let CI test all three platforms.

## 1. Before you start

- A clone with two remotes: `origin` = `NouraX-ORG/nodrive-desktop`, `upstream` =
  `nextcloud/desktop` (`git remote -v`). Settings used here:
  `git config core.autocrlf false` and `git config core.longpaths true` (Windows).
- Python 3 and Pillow (`pip install Pillow`), and `gh` signed in with the `repo`
  and `workflow` scopes (a push that touches `.github/workflows/` needs `workflow`).
- Read upstream's release notes (`CHANGELOG.md` in the new tag) for new features
  that talk to the network (news, telemetry, update or recommendation links).

## 2. Step by step

The shared branding belongs to the Linux lane, so the update is made on
`platform/linux`.

```sh
git fetch upstream --tags
git tag -l "v35.*"                                  # pick the newest stable tag, e.g. v35.0.1
git merge-base --is-ancestor v35.0.0 v35.0.1 && echo "same release line"

git switch platform/linux
git merge origin/main                               # start from the current main
git merge --no-ff --no-commit v35.0.1
```

### 2.1 Resolve conflicts

| Files | What to do |
|---|---|
| `README.md` | keep ours: `git checkout --ours README.md`, then refresh the copy of upstream's: `git show v35.0.1:README.md > docs/upstream-README.md` |
| Files that `tools/rebrand.py` or a `tools/rebrand.d/` plugin patches (list in 2.4) | take upstream's version, `git checkout --theirs -- <file>`; the tool puts our change back in step 2.2 |
| `translations/client_*.ts` | take upstream's (`git checkout --theirs -- translations/`); the tool rewrites the four messages again |
| Images that `tools/make-icons.py` makes (`tools/branding/icons-manifest.txt`) | take either side; the script writes them again in step 2.3 |
| `nodrive/`, `tools/`, `docs/`, `NOTICE.md`, `.github/workflows/linux.yml`, `windows.yml`, `macos.yml` | ours only (upstream has no such files); no conflict expected |

Then two checks git does not do for you:

1. **New upstream workflows.** Upstream's workflows were moved to
   `.github/upstream-workflows/`. Changes to those files follow the move, but a
   *new* upstream workflow lands in `.github/workflows/` and would run in our CI
   (and may need Nextcloud's secrets). Move every such file:

   ```sh
   git ls-files .github/workflows            # must list only linux.yml, macos.yml, windows.yml
   git mv .github/workflows/<new-file>.yml .github/upstream-workflows/
   ```

2. **New images.** List the pictures upstream added and look at each one:

   ```sh
   git diff --name-only --diff-filter=A v35.0.0 v35.0.1 -- theme admin shell_integration src | grep -Ei "\.(png|svg|ico|icns|bmp)$"
   ```

   A new picture that shows the Nextcloud logo and is built into the app must be
   made by `tools/make-icons.py` (or replaced by a neutral upstream glyph in
   `tools/rebrand.py`).

### 2.2 Re-apply the rebrand

```sh
python tools/rebrand.py -v          # apply; lists every change
python tools/rebrand.py --check     # must print "0 change(s)"
```

If the tool stops with an error such as "anchor not found", upstream changed the
code around one of our patches. Read upstream's new code, then update that step's
old and new text in `tools/rebrand.py` (or the platform plugin in
`tools/rebrand.d/`). Keep the exact-anchor style: the tool must fail loudly, never
skip silently. For a `.ts` message whose English source changed, update its rule
in `TS_RULES` in `tools/rebrand.py`.

Version numbers:
- `VERSION.cmake` comes from upstream (for example 35.0.1); do not edit it.
- In `tools/branding/ids.json` set `product.upstream_version` to the new version
  and `product.nourax_release` back to `1` (raise it instead when you rebuild the
  same upstream version with new Nourax changes). The version becomes
  `<upstream>-nourax.<n>`.
- Update the user-agent examples in `ids.json` (`user_agent`) and the version
  mentions in `README.md`, `NOTICE.md` and `docs/branding.md`.
- Run `python tools/rebrand.py` again (it renders `nodrive/NODRIVE.cmake` and
  `nodrive/licenses/THIRD-PARTY-NOTICES.txt` from `ids.json`).

### 2.3 Regenerate the icons

```sh
python tools/make-icons.py
python tools/make-icons.py --check  # must print "0 would change"
```

It also covers new tray state icons (`theme/*/state-*.svg`) automatically.

### 2.4 Re-run the sweep

```sh
python tools/check-branding.py --update-trademark-hashes v35.0.1   # upstream's trademarked files at the new tag
python tools/check-branding.py --lane all --list                   # every unclassified hit
```

For every unclassified hit:
- **user-visible** (a text, link, logo or colour a user can see, in any language):
  fix it with a new step in `tools/rebrand.py` (shared files) or in the platform
  plugin (platform files);
- **internal** identifier, server-required value, not shipped, dead code, or
  **legal** text: add a rule with a reason to `tools/branding/allow-shared.txt`
  (or `allow-windows.txt` / `allow-macos.txt` for that lane's files).

Then:

```sh
python tools/check-branding.py --lane all          # must end with "check-branding: PASS"
python tools/check-branding.py --lane all --write-sweep
```

`--write-sweep` refreshes only the generated part of
[branding-sweep.md](branding-sweep.md). Update the hand-written tables above it
(counts before and after, kept hits) with the commands in its last section.

Also check upstream's CI references against ours, because the tags move with each
release:

| What | Ours | Upstream reference |
|---|---|---|
| Linux build and AppImage containers | `container:` lines in `.github/workflows/linux.yml` | `.github/upstream-workflows/linux-gcc-compile-tests.yml`, `linux-appimage.yml` |
| Windows Craft revisions | the root `craftmaster.ini` (upstream's file, read by `windows.yml`) | updated by the merge |
| macOS Craft blueprints | `BLUEPRINTS_BRANCH` in `.github/workflows/macos.yml` (`stable-35.0`) | `.github/upstream-workflows/macos-*.yml`; change it for a new major version |
| Craft caches | the cache keys include the ini files and blueprint revisions, so a new revision starts a new cache by itself; to start from scratch, bump `CRAFT_CACHE_VERSION` in `windows.yml` or the `macos-craft-v1` family name in `macos.yml` | - |

### 2.5 Commit, test in CI, merge

```sh
git commit                                  # merge commit: "Merge upstream v35.0.1"
git push origin HEAD:platform/linux         # runs the Linux workflow
gh workflow run windows.yml --ref platform/linux -R NouraX-ORG/nodrive-desktop
gh workflow run macos.yml   --ref platform/linux -R NouraX-ORG/nodrive-desktop
```

- Unit tests that newly fail because of the branding: fix them in `step_tests` of
  `tools/rebrand.py`, or skip them in `tools/branding/ctest-skip-<os>.txt` with a
  reason.
- When all three runs are green on the branch head, fast-forward `main`:
  `git merge-base --is-ancestor origin/main HEAD && git push origin HEAD:main`.
  Never force-push `main`.
- Watch the three runs on `main`.
- Screenshots: download `nodrive-<os>-screenshots` from the runs, look at every
  image (dark mode, German, Arabic included), and copy changed screens into
  `docs/screenshots/<os>/`; update `docs/testing-<os>.md` and
  [testing.md](testing.md).
- Ask the owner to repeat the after-sign-in tests for anything that changed.

The Windows and macOS lanes then merge `main` into `platform/windows` and
`platform/macos` and re-run their workflows.

## 3. Files that will conflict

Compared with `v35.0.0`, NoDrive changes 159 upstream files and moves 24 workflow
files; everything else is new files in our own folders.

| How often | Files | Why |
|---|---|---|
| Every release | `README.md` | ours replaces upstream's |
| Every release | `translations/client_*.ts` (55 files) | upstream's Transifex updates touch them; we rewrite four messages |
| Often | `src/gui/wizard/qml/ServerPage.qml`, `src/gui/wizard/qml/AccountWizardWindow.qml`, `src/gui/wizard/accountwizardcontroller.cpp` | the sign-in wizard is under active development |
| Often | `src/gui/application.cpp`, `src/gui/folder.cpp`, `src/gui/infosettings.cpp`, `src/libsync/theme.cpp`, `theme/Style/Style.qml`, `CMakeLists.txt`, `test/CMakeLists.txt` | busy files with one or two of our patches each |
| Sometimes | `src/gui/accountsetupcommandlinemanager.cpp`, `src/gui/settingsswitch.cpp`, `src/gui/advancedsettings.ui`, `src/gui/assistant/ocsassistantconnector.cpp`, `src/gui/assistant/qml/AssistantMessageList.qml`, `src/cmd/cmd.cpp`, `src/csync/csync_exclude.cpp`, `src/libsync/nextcloudtheme.cpp`, `src/libsync/vfs/cfapi/shellext/customstateprovider.cpp`, `test/testnextcloudcmdprovisioning.cpp`, `NextcloudCPack.cmake`, `admin/linux/build-appimage.sh`, `shell_integration/dolphin/ownclouddolphinoverlayplugin.json` | shared patches (`tools/rebrand.py`, `tools/rebrand.d/linux.py`) |
| Sometimes (Windows) | `admin/win/msi/CMakeLists.txt`, `src/gui/navigationpanehelper.cpp`, `src/libsync/vfs/cfapi/shellext/CfApiShellIntegration.rc`, `shell_integration/windows/NCContextMenu/NCContextMenu.rc`, `NCOverlays/NCOverlay.rc`, `NCUtil/NCUtil.rc` | `tools/rebrand.d/windows.py` |
| Sometimes (macOS) | `cmake/modules/MacOSXBundleInfo.plist.in`, `shell_integration/MacOSX/NextcloudIntegration/NextcloudIntegration.xcodeproj/project.pbxproj`, `NextcloudDev/Build.xcconfig`, `FinderSyncExt/FinderSync.m`, `src/gui/systray_mac_usernotifications.mm` | `tools/rebrand.d/macos.py` (`project.pbxproj` changes often upstream) |
| When upstream changes an image | 56 upstream images rewritten by `tools/make-icons.py` (app, tray, wizard, installer pictures) and 14 SVG glyphs recoloured or replaced by `tools/rebrand.py` (`theme/**/talk-app.svg`, `deck.svg`, `wizard-talk.svg`, `talk-bordered.svg`, `external.svg`, `folder.svg`, `info.svg`, `sync.svg`, `wizard-files.svg`, `wizard-groupware.svg`) | regenerated by the tools |
| Rarely | `.github/upstream-workflows/*` | moved upstream workflows; changes follow the move |
| Never | `nodrive/`, `tools/`, `docs/`, `NOTICE.md`, `.github/workflows/*.yml` | our own files |

To list them yourself: `git diff --name-status v35.0.0 origin/main | grep -v "^A"`.

Changes that do **not** conflict but need a look:
- new options or URLs in `NEXTCLOUD.cmake` (our overlay sets values after it;
  the gate scans the file);
- new user-visible texts that name Nextcloud (the gate finds them in every
  language);
- new network features (see section 1);
- new upstream workflows and images (section 2.1).
