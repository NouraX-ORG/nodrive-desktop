# Building NoDrive for macOS

NoDrive for macOS is built only in GitHub Actions: `.github/workflows/macos.yml`. Nothing is built on a developer PC for CI, and nothing is published: the results are Actions artifacts.

## What the workflow does

| Job | Runner | What it does |
|---|---|---|
| Branding gate | `ubuntu-latest` | `python3 tools/rebrand.py --check` (the rebrand, including `tools/rebrand.d/macos.py`, is fully applied) and `python3 tools/check-branding.py --lane macos` (no unclassified Nextcloud name, link, colour or logo in shared and macOS files) |
| Craft dependencies | `macos-latest` (arm64) | KDE Craft, as upstream, with the blueprints `nextcloud/craft-blueprints-kde` and `nextcloud/craft-blueprints-nextcloud` at `stable-35.0`. The Craft root is cached. The cache is saved even when the step fails or times out, so the next run resumes |
| Build, test and package | `macos-latest` | `craft --compile --install nextcloud-client` from this checkout, classic sync only (`buildFileProviderModule=False`), unit tests with `ctest`, then `admin/osx/nodrive-package.sh` |
| Smoke test | `macos-latest` | `tools/screenshots/macos.sh`: installs from the `.dmg`, starts the app, takes screenshots, checks the bundle and the network. See `docs/testing-macos.md` |

## Artifacts

| Artifact | Contents |
|---|---|
| `nodrive-macos-dmg` | `NoDrive-<version>-<build>-arm64.dmg` (volume "NoDrive": `NoDrive.app` and an Applications link) |
| `nodrive-macos-app` | the same app as `.app.zip` |
| `nodrive-macos-test-results` | ctest JUnit XML and `LastTest.log` |
| `nodrive-macos-screenshots` | screenshots, `results.tsv`, network logs, bundle checks, sweep of the installed app |
| `nodrive-macos-branding-report` | the branding sweep report of the source tree |

## Signing

The app is signed **ad hoc** only (`codesign --sign -`): no certificate, no Team ID, no hardened runtime, no notarisation. Apple-silicon Macs need at least an ad-hoc signature to run arm64 code. Because the app is not notarised, macOS blocks a normal double-click the first time: right-click `NoDrive.app` and choose **Open**.

`admin/osx/nodrive-package.sh` signs inside-out (loose Mach-O files, frameworks, the Finder extension, the login item, then the app) with the entitlements CMake generated, checks the result with `codesign --verify --deep --strict`, and builds the `.dmg` with `hdiutil`.

The app group and the Finder-extension ids use the placeholder Team ID `NOURAXTEAM` (`tools/branding/ids.json`). Real signing needs an Apple Developer account; then the real Team ID replaces the placeholder (owner to-do).

## Files (macOS lane)

| File | Purpose |
|---|---|
| `.github/workflows/macos.yml` | the workflow |
| `admin/osx/nodrive-craftmaster.ini` | our CraftMaster overrides (Python path from `actions/setup-python`; clean dependency build trees); the root `craftmaster.ini` stays upstream |
| `admin/osx/nodrive-craft.sh` | runs one Craft command through CraftMaster with both config files |
| `admin/osx/nodrive-package.sh` | licence files, ad-hoc signing, `.dmg` and `.app.zip` |
| `admin/osx/ci/` | test-only helpers for the smoke test, built on the runner and never shipped |
| `tools/rebrand.d/macos.py` | macOS part of the rebrand (Info.plist template, Xcode defaults, Finder extension fallback name, a notification text) |
| `tools/branding/allow-macos.txt` | Nextcloud hits kept on purpose in macOS files, each with a reason |
| `tools/branding/ctest-skip-macos.txt` | unit tests skipped on macOS, each with a reason |
| `tools/screenshots/macos.sh` | the smoke test |

## Running a smoke test again without a new build

*Actions → macOS → Run workflow*, and set `reuse_run` to the id of a run that has a `nodrive-macos-dmg` artifact. The build jobs are skipped and the smoke test uses that `.dmg`.

## Not built yet

- **File Provider ("files on demand")**: switched off (`buildFileProviderModule=False`). It needs real signing and its own rebrand (texts in `FileProviderUIExt`, the domain symbol). Owner to-do.
- **Intel Macs**: only arm64 is built (`macos-latest`). An Intel build (`macos-15-intel`) is an owner to-do.
