#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoDrive macOS CI: run a KDE Craft command through CraftMaster (used by .github/workflows/macos.yml).
#
#   admin/osx/nodrive-craft.sh <craft arguments...>
#   e.g. admin/osx/nodrive-craft.sh --install-deps nextcloud-client
#
# Layout (same as upstream macos-craft-ci.yml): CraftMaster is cloned to <repo>/CraftMaster, so the
# Craft root is <repo>/<target> and Craft itself is cloned to <repo>/craft-clone.
# Config: the upstream craftmaster.ini plus our admin/osx/nodrive-craftmaster.ini override.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TARGET="${CRAFT_TARGET:-macos-clang-arm64}"

exec python3 "$ROOT/CraftMaster/CraftMaster.py" \
    --config "$ROOT/craftmaster.ini" \
    --config-override "$ROOT/admin/osx/nodrive-craftmaster.ini" \
    --target "$TARGET" \
    -c "$@"
