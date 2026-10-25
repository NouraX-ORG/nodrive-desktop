# Building NoDrive for Windows

Windows builds run only in GitHub Actions (`.github/workflows/windows.yml`). The
result is an unsigned MSI, uploaded as the Actions artifact `windows-msi`. There
are no releases and no code signing yet.

## How the workflow works

| Job | Runner | What it does |
|---|---|---|
| `branding` | `ubuntu-latest` | `python3 tools/rebrand.d/windows.py --check`; `tools/rebrand.py --check` and `tools/check-branding.py --lane windows` as soon as they are on the branch |
| `deps` | `windows-2022` | KDE Craft (CraftMaster pinned in the workflow) and every client dependency (`craft --install-deps nextcloud-client`). Most packages come from the prebuilt Craft cache that upstream publishes for `stable-35.0-qt-6.11.2` |
| `build` | `windows-2022` | Builds the client from the checkout with Craft (`-DBUILD_WIN_MSI=ON`, `-DMIRALL_VERSION_BUILD=<run number>`), collects the files (`craft --package`, portable packager), builds the MSI with `admin/win/ci/make-msi-ci.ps1`, uploads it, then runs `ctest` |

Craft settings: the upstream `craftmaster.ini` is used as it is, and
`admin/win/nodrive-craftmaster.ini` is read after it (`CraftMaster --config-override`)
with our changes only (portable packager instead of upstream's NSIS installer).

### The Craft cache

The Craft tree (`CraftMaster`, `craft-clone`, `windows-msvc2022_64-cl` without its
`build`, `download` and `tmp` folders) is cached with `actions/cache/restore` and
`actions/cache/save`:

- the key is `win-craft-<version>-<target>-py<python>-cm<CraftMaster>-ini<hash of both ini files>-<Craft rev>-<KDE blueprints rev>-<Nextcloud blueprints rev>-<run id>-<attempt>`;
- every run restores the newest cache with the same prefix (or, after a blueprint
  update, the newest one with the same ini files) and Craft only installs what is
  missing or outdated;
- the dependency step stops after 290 minutes, so the job always has time to save
  the tree before GitHub's 6-hour job limit, also after a failure or a time-out;
  the next run carries on from there;
- a complete tree gets the marker `windows-msvc2022_64-cl/etc/nodrive-craft-deps.txt`;
  a run that restores a complete tree skips the install and saves nothing new;
- to start from scratch, bump `CRAFT_CACHE_VERSION` in the workflow.

Caches made on a branch are only visible to that branch; `main` builds its own
the first time the workflow runs there.

### Why `windows-2022` and not `windows-latest`

`windows-latest` is Windows Server 2025 with Visual Studio 2026 only. Craft's
`windows-msvc2022_64-cl` target looks for Visual Studio 2022 (or a newer Visual
Studio with the `Microsoft.VisualStudio.Component.VC.v143.x86.x64` component), and
the prebuilt Craft packages are built with MSVC 2022. Upstream uses `windows-2022`
for the same reason. The workflow's `toolchain-probe` job records what
`windows-latest` offers.

### The MSI

`admin/win/ci/make-msi-ci.ps1` takes Craft's collected files
(`build\nextcloud-client\archive`), puts the app into one folder (executable, DLLs,
Qt plugins, QML, `i18n\`, `sync-exclude.lst`, the app icon and `licenses\`), finds
WiX 3.14 (on the runner image) and `uuidgen.exe` (Windows SDK), and runs the
generated `make-msi.bat` from `admin/win/msi`. When the Windows SDK's
`WiSubStg.vbs` and `WiLangId.vbs` are found, the MSI also carries the German,
French, Italian and Spanish installer languages.

The upstream Craft blueprint's package blacklist keeps only `nextcloud.exe` and
`nextcloudcmd.exe` in `bin\`. The workflow adds `nodrive.exe` and `nodrivecmd.exe`
to that list in its own copy of the blueprint (nothing upstream is changed).

## Windows-only rebrand steps

`tools/rebrand.d/windows.py` (run by `tools/rebrand.py`, or on its own; `--check`
makes no changes and exits 1 if something is missing):

| File | Change |
|---|---|
| `src/libsync/vfs/cfapi/shellext/CfApiShellIntegration.rc` | version resource: company `Nourax`, product `NoDrive shell extension`, description `NoDrive CfApi shell extension`, copyright `(c) 2026` (like the main executable) |
| `src/gui/navigationpanehelper.cpp` | only remove our own Explorer navigation pane entries, never the real Nextcloud client's (side by side) |
| `admin/win/msi/CMakeLists.txt` | MSI file name: no `--` when the version suffix already starts with `-` (`-nourax.1`), no trailing `-` when it is empty |
