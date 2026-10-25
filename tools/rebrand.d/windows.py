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

1. ``src/libsync/vfs/cfapi/shellext/CfApiShellIntegration.rc``: the version
   resource of the CfApi shell extension DLL hard-codes "Nextcloud GmbH" and
   "Nextcloud ... shell extension" (shown in Explorer, file Properties ->
   Details). Use the product name and vendor instead, and "(c) <year>" like
   the main executable (``src/gui/version.rc.in``). The SPDX source header of
   the file is not touched.
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
import json
import re
import sys
from pathlib import Path

LANE = "windows"

# Fallbacks, used only while tools/branding/ids.json is not on this branch yet.
DEFAULT_NAME = "NoDrive"
DEFAULT_VENDOR = "Nourax"
COPYRIGHT_YEAR = "2026"

RC = "src/libsync/vfs/cfapi/shellext/CfApiShellIntegration.rc"
NAVPANE = "src/gui/navigationpanehelper.cpp"
MSI_CMAKE = "admin/win/msi/CMakeLists.txt"


def _product(ids: dict) -> tuple[str, str]:
    product = ids.get("product", {}) if isinstance(ids, dict) else {}
    cmake = ids.get("cmake", {}) if isinstance(ids, dict) else {}
    name = product.get("name") or cmake.get("APPLICATION_NAME") or DEFAULT_NAME
    vendor = product.get("vendor") or cmake.get("APPLICATION_VENDOR") or DEFAULT_VENDOR
    return name, vendor


def apply(ctx) -> None:
    name, vendor = _product(getattr(ctx, "ids", {}) or {})

    # 1. CfApi shell extension version resource.
    rc_values = [
        ('VALUE "CompanyName", "Nextcloud GmbH"', f'VALUE "CompanyName", "{vendor}"'),
        ('VALUE "FileDescription", "Nextcloud CfApi shell extension"',
         f'VALUE "FileDescription", "{name} CfApi shell extension"'),
        ('VALUE "LegalCopyright", "Copyright (C) 2023 Nextcloud GmbH"',
         f'VALUE "LegalCopyright", "(c) {COPYRIGHT_YEAR}"'),
        ('VALUE "ProductName", "Nextcloud shell extension"', f'VALUE "ProductName", "{name} shell extension"'),
    ]
    for old, new in rc_values:
        ctx.replace(RC, old, new, what=f"CfApi version resource: {new.split(',')[0]}")

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
# Stand-alone runner. Uses tools/rebrandlib.py when it is on this branch; else a
# small built-in context with the same semantics (see rebrandlib's docstring).


class _RebrandError(Exception):
    pass


class _MiniContext:
    def __init__(self, root: Path, check: bool, verbose: bool):
        self.root = root
        self.check = check
        self.verbose = verbose
        ids_path = root / "tools" / "branding" / "ids.json"
        self.ids = json.loads(ids_path.read_text(encoding="utf-8")) if ids_path.exists() else {}
        self.lane = LANE
        self.changes: list[tuple[str, str]] = []
        self._text: dict[str, str] = {}
        self._dirty: set[str] = set()

    def read(self, rel: str) -> str:
        if rel not in self._text:
            p = self.root / rel
            if not p.exists():
                raise _RebrandError(f"{rel}: file not found")
            self._text[rel] = p.read_bytes().decode("utf-8")
        return self._text[rel]

    def _set(self, rel: str, text: str, what: str) -> None:
        self._text[rel] = text
        self._dirty.add(rel)
        self.changes.append((rel, f"[{self.lane}] {what}"))
        if self.verbose:
            print(f"  {'would change' if self.check else 'changed'} {rel}: {what}")

    def replace(self, rel: str, old: str, new: str, count: int | None = 1, what: str | None = None) -> bool:
        text = self.read(rel)
        if new and new in text:
            return False
        n = text.count(old)
        if n == 0 and not new:
            return False
        if n == 0:
            raise _RebrandError(f"{rel}: anchor not found and replacement not present (upstream changed?):\n  old: {old[:160]!r}")
        if count is not None and n != count:
            raise _RebrandError(f"{rel}: anchor found {n} times, expected {count}:\n  old: {old[:160]!r}")
        self._set(rel, text.replace(old, new), what or f"replace {old[:60]!r}")
        return True

    def regex_sub(self, rel: str, pattern: str, repl, expect: int | None = 1, flags: int = 0, what: str | None = None) -> bool:
        text = self.read(rel)
        new_text, n = re.subn(pattern, repl, text, flags=flags)
        if n == 0 or new_text == text:
            return False
        if expect is not None and n != expect:
            raise _RebrandError(f"{rel}: pattern matched {n} times, expected {expect}: {pattern!r}")
        self._set(rel, new_text, what or f"regex {pattern[:60]!r} x{n}")
        return True

    def require(self, rel: str, text: str) -> None:
        if text not in self.read(rel):
            raise _RebrandError(f"{rel}: required text missing: {text[:160]!r}")

    def flush(self) -> None:
        if self.check:
            return
        for rel in sorted(self._dirty):
            (self.root / rel).write_bytes(self._text[rel].encode("utf-8"))
        self._dirty.clear()


def _make_context(root: Path, check: bool, verbose: bool):
    tools = root / "tools"
    if (tools / "rebrandlib.py").exists() and (tools / "branding" / "ids.json").exists():
        sys.path.insert(0, str(tools))
        import rebrandlib  # type: ignore

        ctx = rebrandlib.Context(root, check=check, verbose=verbose)
        ctx.lane = LANE
        return ctx, (rebrandlib.RebrandError, _RebrandError)
    return _MiniContext(root, check, verbose), (_RebrandError,)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="NoDrive rebrand: Windows-only changes")
    parser.add_argument("--check", action="store_true", help="no writes; exit 1 if anything would change")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2], help="repository root")
    parser.add_argument("-q", "--quiet", action="store_true")
    args = parser.parse_args(argv)

    ctx, errors = _make_context(args.root.resolve(), args.check, not args.quiet)
    try:
        apply(ctx)
        ctx.flush()
    except errors as exc:  # type: ignore[misc]
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
