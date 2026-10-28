# Building NoDrive (Windows, macOS, Linux)

NoDrive is built in **GitHub Actions** from this repository. The workflows use no
secrets, sign nothing with a real certificate, and publish nothing: every result
is an Actions artifact. Platform details: [build-windows.md](build-windows.md),
[build-macos.md](build-macos.md); test results: [testing.md](testing.md).

## 1. Before any build: the branding

The branding is committed, so a fresh checkout builds NoDrive as it is. After you
change `tools/branding/ids.json`, a tool, or merge a new upstream release, run:

```sh
python tools/rebrand.py                     # apply (idempotent)
pip install Pillow && python tools/make-icons.py
python tools/check-branding.py --lane all   # must say PASS
```

Every workflow's first job checks this (`rebrand.py --check`,
`check-branding.py --lane <os>`, and on Linux and Windows `make-icons.py --check`).
A branch where a tool was not re-run fails there. How the tools work:
[branding.md](branding.md).

## 2. The three workflows

All three run on every push to `main` and to their platform branch, and by hand
(*Actions → Linux / Windows / macOS → Run workflow*). Actions are pinned to
current major versions (`actions/checkout@v7`, `actions/setup-python@v7`,
`actions/cache/restore@v6` and `actions/cache/save@v6`, `actions/upload-artifact@v7`,
`actions/download-artifact@v8`, `maxim-lobanov/setup-xcode@v1`).

| Workflow | Runner | Build | Package | Time (warm cache) |
|---|---|---|---|---|
| `.github/workflows/linux.yml` | `ubuntu-latest`, upstream's public Qt CI containers | CMake + Ninja, Debug with tests; AppImage with `admin/linux/build-appimage.sh` | `NoDrive-35.0.0-nourax.1-<build>-x86_64.AppImage` | about 25 min |
| `.github/workflows/windows.yml` | `windows-2022` | KDE Craft (`windows-msvc2022_64-cl`), Craft tree cached | `NoDrive-35.0.0.<build>-nourax.1-x64.msi` (WiX 3) | about 35 min |
| `.github/workflows/macos.yml` | `macos-latest` (Apple silicon) | KDE Craft (`macos-clang-arm64`), classic sync only (no File Provider), Craft tree cached | `NoDrive-35.0.0-nourax.1-<build>-arm64.dmg`, ad-hoc signed | about 35 min |

`<build>` is the workflow's run number (`MIRALL_VERSION_BUILD`). The app then
reports `35.0.0-nourax.1 (build <build>)`.

Artifacts (kept 30 days):

| Workflow | Artifacts |
|---|---|
| Linux | `nodrive-linux-appimage`, `nodrive-linux-screenshots`, `nodrive-linux-test-results` (also the unit test's screenshots), `nodrive-linux-report-server` (job `report-server`: real-server check of the problem reports), `nodrive-linux-branding-report` |
| Windows | `nodrive-windows-msi`, `nodrive-windows-screenshots`, `nodrive-windows-build-logs`, `branding-windows` |
| macOS | `nodrive-macos-dmg`, `nodrive-macos-app`, `nodrive-macos-screenshots`, `nodrive-macos-test-results`, `nodrive-macos-branding-report` |

Each workflow has a `reuse_run` input: give it the id of an earlier run and only
the test part runs, with that run's package.

## 3. Linux

Jobs in `linux.yml`:

| Job | What it does |
|---|---|
| Branding gate | `rebrand.py --check`, `check-branding.py --lane linux`, `make-icons.py --check` |
| Build and unit tests | container `ghcr.io/nextcloud/continuous-integration-client-qt6:client-sid-6.10.2-4`; configure, build, `ctest` under Xvfb as a normal user (skip list `tools/branding/ctest-skip-linux.txt`) |
| AppImage | container `ghcr.io/nextcloud/continuous-integration-client-appimage-qt6:client-appimage-el9-6.11.2-4`; `admin/linux/build-appimage.sh` with NoDrive names, no update information inside the AppImage |
| Smoke test | `tools/screenshots/linux.sh`: starts the AppImage under Xvfb in four variants, screenshots, fake browser, DNS and connection log, package checks, `check-branding.py --artifact` |

The containers are upstream's public CI images (the same tags as
`.github/upstream-workflows/linux-gcc-compile-tests.yml` and `linux-appimage.yml`).

### Building on a Linux PC (optional)

Only on a Linux machine with Docker or Podman and enough disk (about 10 GB). These
are the same commands as CI:

```sh
git clone https://github.com/NouraX-ORG/nodrive-desktop.git && cd nodrive-desktop

# Build (Debug, with tests)
docker run --rm -it -v "$PWD":/src -w /src \
  ghcr.io/nextcloud/continuous-integration-client-qt6:client-sid-6.10.2-4 bash -c '
    cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DCMAKE_BUILD_TYPE=Debug -DQT_MAJOR_VERSION=6 -DQUICK_COMPILER=ON -DBUILD_TESTING=1 &&
    cmake --build build'

# AppImage (written to the current folder)
docker run --rm -it -v "$PWD":/src -w /src \
  ghcr.io/nextcloud/continuous-integration-client-appimage-qt6:client-appimage-el9-6.11.2-4 bash -c '
    BUILDNR=0 BUILD_UPDATER=OFF DESKTOP_CLIENT_ROOT=/src EXECUTABLE_NAME=nodrive APPNAME=NoDrive \
      QT_BASE_DIR=/root/linux-gcc-x86_64 ./admin/linux/build-appimage.sh'
```

For the unit tests, do what the "Unit tests (ctest under Xvfb)" step of
`linux.yml` does: create a normal user in the container, give it the `build`
folder, and run `xvfb-run -a ctest --output-on-failure -E "^UpdateChannelTest$"`
as that user (as upstream's CI does). The AppImage script also creates
`build-client/` in the source folder and downloads `linuxdeploy` and
`appimagetool` from GitHub.

## 4. Windows

Summary (full details: [build-windows.md](build-windows.md)):

- Runner `windows-2022`, because Craft's MSVC 2022 target does not find a
  toolset on `windows-latest` (Visual Studio 2026 only, checked on 2026-10-25).
- KDE Craft with upstream's `craftmaster.ini` plus our override
  `admin/win/nodrive-craftmaster.ini` (portable packager instead of NSIS).
- The Craft tree is cached; a cold start can take hours, so the dependency step
  stops in time to save the cache, and the next run carries on.
- `admin/win/ci/make-msi-ci.ps1` builds the MSI from Craft's files with WiX 3.14
  and the generated `make-msi.bat`; German, French, Italian and Spanish installer
  languages are embedded when the SDK scripts are found.
- Then `ctest`, a silent install on a fresh runner, start, screenshots, DNS log and
  silent uninstall (`tools/screenshots/windows.ps1`).

The MSI is **not signed**. Windows shows a SmartScreen warning when it is opened.

## 5. macOS

Summary (full details: [build-macos.md](build-macos.md)):

- Runner `macos-latest` (Apple silicon, arm64 only).
- KDE Craft with the blueprints `stable-35.0` and our override
  `admin/osx/nodrive-craftmaster.ini`; Craft tree cached.
- Classic sync only (`buildFileProviderModule=False`): the File Provider needs real
  signing.
- `admin/osx/nodrive-package.sh`: licence files, **ad-hoc** signature
  (`codesign --sign -`, no certificate, no notarisation), `.dmg` with `hdiutil`.
- Then `ctest` and the smoke test (`tools/screenshots/macos.sh`).

The app group uses the placeholder Team ID `NOURAXTEAM` until there is an Apple
Developer account.

## 6. Build-time downloads

The app itself never contacts a Nextcloud service. The *build* uses public
upstream resources, as upstream's own CI does:

- upstream's CI container images on `ghcr.io/nextcloud/`, and the `linuxdeploy`
  and `appimagetool` releases on GitHub (Linux);
- KDE Craft, CraftMaster and the `nextcloud/craft-blueprints-*` repositories,
  and Craft's prebuilt package cache (Windows, macOS).

If upstream removes one of them, the build must switch to a source build or our
own images; the first place to look is the matching file in
`.github/upstream-workflows/`.

## 7. Not done yet (owner to-dos)

| Item | Needs |
|---|---|
| Windows code signing | a code-signing certificate |
| macOS signing, notarisation, File Provider | an Apple Developer account and its Team ID (replaces `NOURAXTEAM` in `tools/branding/ids.json`) |
| Intel Macs | a second macOS job (`macos-15-intel`) |
| Releases and a download page | the owner's yes; today builds are Actions artifacts only |
