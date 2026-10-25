<!--
  - SPDX-FileCopyrightText: 2026 Nourax
  - SPDX-License-Identifier: GPL-2.0-or-later
-->
# NoDrive for Windows, macOS and Linux

NoDrive is the desktop sync app of **Nourax**. It keeps folders on your computer
in sync with your files in Nourax Workspace at `https://drive.nourax.org`.

- One server only: the app always connects to `https://drive.nourax.org`. There is
  no "enter server address" screen.
- Sign-in happens in your web browser ("Log in with Nourax").
- Folder sync with Explorer (Windows) and Finder (macOS) integration. Files on
  demand is built on Windows; it is part of the owner tests.
- No built-in updater, no crash reporter, no usage reporting.

**Status:** test builds only. The builds are not code-signed, and there are no
releases and no store versions yet.

## Based on the Nextcloud desktop client

NoDrive is the [Nextcloud desktop client](https://github.com/nextcloud/desktop),
version 35.0.0 (tag `v35.0.0`), with the Nourax name, logo, colours and a fixed
server. Thank you to Nextcloud GmbH, ownCloud GmbH and every contributor of the
Nextcloud desktop client: almost all of the code in this repository is their work.

- Upstream's own README is kept as [docs/upstream-README.md](docs/upstream-README.md).
- What we changed and why: [NOTICE.md](NOTICE.md) and [docs/branding.md](docs/branding.md).
- NoDrive is not made or endorsed by Nextcloud GmbH. "Nextcloud" is a trademark of
  Nextcloud GmbH.

## Licence

NoDrive is free software under the **GNU General Public License, version 2 or (at
your option) any later version** (GPL-2.0-or-later), the same licence as upstream.
See [COPYING](COPYING). Upstream copyright headers, `COPYING`, `LICENSES/` and
`AUTHORS.md` are unchanged. Bundled libraries (Qt, KDE Frameworks, OpenSSL and
others) keep their own licences; their texts ship with every package from
[nodrive/licenses/](nodrive/licenses/). Details: [NOTICE.md](NOTICE.md).

## How to build

The apps are built in **GitHub Actions**, from this repository, with no secrets:

| Platform | Workflow | Package |
|---|---|---|
| Linux | [`.github/workflows/linux.yml`](.github/workflows/linux.yml) | AppImage (x86_64) |
| Windows | [`.github/workflows/windows.yml`](.github/workflows/windows.yml) | MSI (x64), built with KDE Craft |
| macOS | [`.github/workflows/macos.yml`](.github/workflows/macos.yml) | `.dmg` (Apple silicon), ad-hoc signed, built with KDE Craft |

Every push to `main` (and to the platform branches) builds the package, runs the
unit tests, installs or starts the app, takes screenshots and runs the branding
check. You can also start a run by hand: *Actions → (workflow) → Run workflow*.

Step-by-step build notes, also for a local Linux build: [docs/build.md](docs/build.md).

The branding is applied by tools in this repository and must be re-run after
every upstream update:

```sh
python tools/rebrand.py           # names, ids, server, texts, colours (idempotent)
python tools/make-icons.py        # every icon from the Nourax logo (Pillow)
python tools/check-branding.py --lane all   # fails if a Nextcloud name, link, logo or colour shows
```

## Where the test builds are

Test builds are **Actions artifacts** (there are no GitHub releases):

1. Open the repository's **Actions** tab and pick the workflow (**Linux**,
   **Windows** or **macOS**).
2. Open the newest green run on `main`.
3. Under **Artifacts**, download:

| Platform | Artifact | File inside (example) |
|---|---|---|
| Linux | `nodrive-linux-appimage` | `NoDrive-35.0.0-nourax.1-<build>-x86_64.AppImage` |
| Windows | `nodrive-windows-msi` | `NoDrive-35.0.0.<build>-nourax.1-x64.msi` |
| macOS | `nodrive-macos-dmg` | `NoDrive-35.0.0-nourax.1-<build>-arm64.dmg` |

`<build>` is the run number of that workflow.

You must be signed in to GitHub to download artifacts. Artifacts are kept for
30 days; after that, run the workflow again. Every run also has a screenshots
artifact (`nodrive-<platform>-screenshots`) with the test results.

How to install them and what to test: [docs/testing.md](docs/testing.md).

## Documentation

| File | What it covers |
|---|---|
| [NOTICE.md](NOTICE.md) | Licences, trademarks, what Nourax changed, the in-app legal-notice decision |
| [docs/branding.md](docs/branding.md) | Every id, colours, icons, and how the branding tools work |
| [docs/branding-sweep.md](docs/branding-sweep.md) | The "no Nextcloud on any screen" sweep: every hit and why it is kept |
| [docs/build.md](docs/build.md) | How the three platforms are built |
| [docs/testing.md](docs/testing.md) | Test results per platform, owner tests, screenshot index |
| [docs/upstream-updates.md](docs/upstream-updates.md) | How to take a new Nextcloud desktop release |

## Reporting problems

This is Nourax's own build. Please do not report NoDrive problems to the Nextcloud
project; contact Nourax instead.
