#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
"""NoDrive rebrand plugin: Windows-only changes (Windows lane).

Called by ``tools/rebrand.py`` (``apply(ctx)``), or run on its own::

    python tools/rebrand.d/windows.py            # apply the changes
    python tools/rebrand.d/windows.py --check    # no writes; exit 1 if anything would change

Every step is idempotent: a second run changes nothing. A step fails loudly
when its anchor text is gone (upstream changed the file), so an upstream merge
can never silently drop a change. See docs/upstream-updates.md.

What it changes (plan section 2.4, 5 and 11.3, Windows lane):

1. Version resources of the shell extension DLLs. Explorer shows them in the
   file's Properties -> Details ("Company", "File description", "Product
   name", "Copyright"). Upstream hard-codes "Nextcloud GmbH" and "Nextcloud ...
   shell extension". They get the product name and vendor instead, and
   "(c) <year>" like the main executable (``src/gui/version.rc.in``).
   - ``src/libsync/vfs/cfapi/shellext/CfApiShellIntegration.rc`` (UTF-8)
   - ``shell_integration/windows/NCContextMenu/NCContextMenu.rc``,
     ``NCOverlays/NCOverlay.rc``, ``NCUtil/NCUtil.rc`` (UTF-16 LE with BOM,
     as upstream keeps them; the encoding and line endings stay as they are)
   The SPDX source headers of these files are not touched.
2. ``src/gui/navigationpanehelper.cpp``: a branded build deletes every Explorer
   navigation pane entry whose ApplicationName is "Nextcloud", which removes the
   real Nextcloud client's sidebar entries. NoDrive must live side by side with
   it, so only our own entries (current app name) are removed.
3. ``admin/win/msi/CMakeLists.txt``: the MSI file name always put a "-" in front
   of MIRALL_VERSION_SUFFIX. NoDrive's suffix already starts with "-"
   ("-nourax.1"), and Craft passes an empty suffix; both gave "--" or a trailing
   "-". Add the dash only when the suffix is non-empty and has none.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

LANE = "windows"

COPYRIGHT_YEAR = "2026"

RC = "src/libsync/vfs/cfapi/shellext/CfApiShellIntegration.rc"
RC_CONTEXT_MENU = "shell_integration/windows/NCContextMenu/NCContextMenu.rc"
RC_OVERLAYS = "shell_integration/windows/NCOverlays/NCOverlay.rc"
RC_UTIL = "shell_integration/windows/NCUtil/NCUtil.rc"
NAVPANE = "src/gui/navigationpanehelper.cpp"
MSI_CMAKE = "admin/win/msi/CMakeLists.txt"

UTF16_LE_BOM = b"\xff\xfe"


def _product(ids: dict) -> tuple[str, str]:
    product = ids.get("product", {})
    return product["name"], product["vendor"]


def _error(message: str) -> Exception:
    rebrandlib = sys.modules.get("rebrandlib")
    return rebrandlib.RebrandError(message) if rebrandlib else RuntimeError(message)


def _replace_utf16(ctx, rel: str, pairs: list[tuple[str, str]], what: str) -> bool:
    """Exact replacements in a UTF-16 LE file with BOM (same rules as ctx.replace)."""
    data = ctx.read_bytes(rel)
    if data is None:
        raise _error(f"{rel}: file not found")
    if not data.startswith(UTF16_LE_BOM):
        raise _error(f"{rel}: expected UTF-16 LE with BOM (upstream changed the encoding?)")
    text = data[len(UTF16_LE_BOM):].decode("utf-16-le")
    new_text = text
    for old, new in pairs:
        if new in new_text:
            continue
        n = new_text.count(old)
        if n != 1:
            raise _error(f"{rel}: anchor found {n} times, expected 1 (upstream changed?):\n  old: {old!r}")
        new_text = new_text.replace(old, new)
    if new_text == text:
        return False
    return ctx.write(rel, UTF16_LE_BOM + new_text.encode("utf-16-le"), what=what)


def apply(ctx) -> None:
    name, vendor = _product(ctx.ids)
    company = ('VALUE "CompanyName", "Nextcloud GmbH"', f'VALUE "CompanyName", "{vendor}"')

    def copyright_of(year: str) -> tuple[str, str]:
        return (f'VALUE "LegalCopyright", "Copyright (C) {year} Nextcloud GmbH"',
                f'VALUE "LegalCopyright", "(c) {COPYRIGHT_YEAR}"')

    # 1. Version resources of the shell extension DLLs.
    rc_values = [
        company,
        ('VALUE "FileDescription", "Nextcloud CfApi shell extension"',
         f'VALUE "FileDescription", "{name} CfApi shell extension"'),
        copyright_of("2023"),
        ('VALUE "ProductName", "Nextcloud shell extension"', f'VALUE "ProductName", "{name} shell extension"'),
    ]
    for old, new in rc_values:
        ctx.replace(RC, old, new, what=f"CfApi version resource: {new.split(',')[0]}")

    _replace_utf16(ctx, RC_CONTEXT_MENU, [company, copyright_of("2023")],
                   what="context menu DLL version resource: company, copyright")
    _replace_utf16(ctx, RC_OVERLAYS, [
        company,
        ('VALUE "FileDescription", "Nextcloud shell extension"', f'VALUE "FileDescription", "{name} shell extension"'),
        copyright_of("2023"),
        ('VALUE "ProductName", "Nextcloud shell extension"', f'VALUE "ProductName", "{name} shell extension"'),
    ], what="overlay DLL version resource: company, description, copyright, product")
    _replace_utf16(ctx, RC_UTIL, [
        company,
        ('VALUE "FileDescription", "Nextcloud shell extension util"',
         f'VALUE "FileDescription", "{name} shell extension util"'),
        copyright_of("2025"),
        ('VALUE "ProductName", "Nextcloud shell extension"', f'VALUE "ProductName", "{name} shell extension"'),
    ], what="shell extension util version resource: company, description, copyright, product")

    # 2. Explorer navigation pane: never remove the real Nextcloud client's entries.
    ctx.replace(
        NAVPANE,
        "if (appName.toString() == currentAppName || appName.toString() == unbrandedApplicationName) {",
        "if (appName.toString() == currentAppName) { // NoDrive: keep other apps' entries (side by side)",
        what="navigation pane: only remove our own entries",
    )

    # 3. MSI file name suffix.
    ctx.replace(
        MSI_CMAKE,
        'if (NOT DEFINED MIRALL_VERSION_SUFFIX OR MIRALL_VERSION_SUFFIX MATCHES "git")\n'
        '    set(VERSION_SUFFIX "")\n'
        'else()\n',
        'if (NOT DEFINED MIRALL_VERSION_SUFFIX OR MIRALL_VERSION_SUFFIX STREQUAL "" OR MIRALL_VERSION_SUFFIX MATCHES "git")\n'
        '    set(VERSION_SUFFIX "")\n'
        'elseif (MIRALL_VERSION_SUFFIX MATCHES "^-")\n'
        '    # NoDrive: the suffix already starts with a dash ("-nourax.1")\n'
        '    set(VERSION_SUFFIX "${MIRALL_VERSION_SUFFIX}")\n'
        'else()\n',
        what="MSI file name: no double dash",
    )


# --------------------------------------------------------------------------
# Stand-alone runner (tools/rebrand.py runs this plugin together with the others).


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="NoDrive rebrand: Windows-only changes")
    parser.add_argument("--check", action="store_true", help="no writes; exit 1 if anything would change")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2], help="repository root")
    parser.add_argument("-q", "--quiet", action="store_true")
    args = parser.parse_args(argv)

    root = args.root.resolve()
    sys.path.insert(0, str(root / "tools"))
    import rebrandlib  # noqa: E402  (tools/rebrandlib.py)

    ctx = rebrandlib.Context(root, check=args.check, verbose=not args.quiet)
    ctx.lane = LANE
    try:
        apply(ctx)
        ctx.flush()
    except rebrandlib.RebrandError as exc:
        print(f"windows.py: ERROR: {exc}", file=sys.stderr)
        return 2

    changes = ctx.changes
    if args.check:
        if changes:
            print(f"windows.py --check: {len(changes)} change(s) not applied yet; run python tools/rebrand.d/windows.py")
            return 1
        print("windows.py --check: OK, nothing to change")
        return 0
    print(f"windows.py: {len(changes)} change(s) applied" if changes else "windows.py: nothing to change")
    return 0


if __name__ == "__main__":
    sys.exit(main())
