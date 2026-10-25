#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
"""NoDrive rebrand plugin for the macOS-only files (macOS lane).

Called by tools/rebrand.py through the plugin contract of tools/rebrandlib.py::

    LANE = "macos"
    def apply(ctx): ...

It also runs on its own (before tools/rebrand.py exists, or to work on the macOS files only)::

    python3 tools/rebrand.d/macos.py            # apply the changes
    python3 tools/rebrand.d/macos.py --check    # change nothing; exit 1 if something would change

Idempotent: a second run changes nothing. Every change uses an exact anchor and fails loudly
when upstream changed the text, so a new upstream release cannot be half-rebranded silently.

What it changes (plans/desktop-plan.md sections 1.4, 2.5 and 5; ids from tools/branding/ids.json):

cmake/modules/MacOSXBundleInfo.plist.in (the app's Info.plist template)
  - CFBundleShortVersionString = @MIRALL_VERSION@ (plain x.y.z, as Apple requires); the full
    "35.0.0-nourax.1 (build N)" string stays in CFBundleLongVersionString.
  - NSHumanReadableCopyright: "<app> by <vendor>. Open-source software, GPL-2.0-or-later."
  - NSLocalNetworkUsageDescription (shown by macOS in a system prompt): no "Nextcloud".
  - Sparkle updater keys SU* removed (no built-in updater; one of them is Nextcloud's update key).
shell_integration/MacOSX/NextcloudIntegration/NextcloudDev/Build.xcconfig
  - fallback DEVELOPMENT_TEAM, OC_APPLICATION_NAME, OC_APPLICATION_REV_DOMAIN (CMake passes the
    real values; the fallbacks must never point at Nextcloud's team or bundle ids).
shell_integration/MacOSX/NextcloudIntegration/NextcloudIntegration.xcodeproj/project.pbxproj
  - project-level DEVELOPMENT_TEAM.
shell_integration/MacOSX/NextcloudIntegration/FinderSyncExt/FinderSync.m
  - fallback app name in the Finder menu ("<app> - not connected").
src/gui/systray_mac_usernotifications.mm
  - notification reply placeholder "Send a Nextcloud Talk reply" -> "Send a Talk reply".

Not changed here: names and ids that come from CMake variables (they follow ids.json through the
shared overlay), internal identifiers (XPC service names, error domains, log categories, the
NCFPKAppGroupIdentifier key) and the File Provider module files (not shipped while it is off).
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path

LANE = "macos"

# Used only when tools/branding/ids.json does not provide the value (plan section 2).
DEFAULT_IDS = {
    "APPLICATION_NAME": "NoDrive",
    "APPLICATION_VENDOR": "Nourax",
    "APPLICATION_REV_DOMAIN": "org.nourax.nodrive.desktop",
    "DEVELOPMENT_TEAM": "NOURAXTEAM",
}

PLIST = "cmake/modules/MacOSXBundleInfo.plist.in"
XCCONFIG = "shell_integration/MacOSX/NextcloudIntegration/NextcloudDev/Build.xcconfig"
PBXPROJ = "shell_integration/MacOSX/NextcloudIntegration/NextcloudIntegration.xcodeproj/project.pbxproj"
FINDERSYNC_M = "shell_integration/MacOSX/NextcloudIntegration/FinderSyncExt/FinderSync.m"
NOTIFICATIONS_MM = "src/gui/systray_mac_usernotifications.mm"

COPYRIGHT_TEXT = "@APPLICATION_NAME_XML_ESCAPED@ by @APPLICATION_VENDOR_XML_ESCAPED@. Open-source software, GPL-2.0-or-later."
LOCAL_NETWORK_TEXT = "@APPLICATION_NAME_XML_ESCAPED@ needs access to your local network to reach your server."
SPARKLE_KEYS = ("SUShowReleaseNotes", "SUPublicDSAKeyFile", "SUPublicEDKey", "SUEnableInstallerLauncherService")


class PluginError(Exception):
    """The file is not in a state this plugin knows (upstream changed?)."""


def _id(ctx, key: str) -> str:
    ids = getattr(ctx, "ids", None) or {}
    for section in (ids.get("macos"), ids.get("cmake"), ids):
        if isinstance(section, dict):
            value = section.get(key)
            if isinstance(value, str) and value:
                return value
    return DEFAULT_IDS[key]


def _ensure(ctx, rel: str, pattern: str, repl, done: str, what: str, expect: int | None = 1) -> None:
    """Apply ``pattern -> repl`` unless the file already matches ``done``; then require ``done``."""
    if re.search(done, ctx.read(rel)) is None:
        ctx.regex_sub(rel, pattern, repl, expect=expect, what=what)
    if re.search(done, ctx.read(rel)) is None:
        raise PluginError(f"{rel}: {what}: anchor not found and result not present (upstream changed?)")


def _plist_string(key: str, value_regex: str) -> str:
    return rf"(<key>{key}</key>\s*<string>){value_regex}(</string>)"


def _keep_groups(middle: str):
    """Replacement callable: group 1 + ``middle`` + group 2 (no backslash processing)."""
    return lambda m: m.group(1) + middle + m.group(2)


def _xml_text(value: str) -> str:
    return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def _objc_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def apply(ctx) -> None:
    name = _id(ctx, "APPLICATION_NAME")
    rev_domain = _id(ctx, "APPLICATION_REV_DOMAIN")
    team = _id(ctx, "DEVELOPMENT_TEAM")
    if not re.fullmatch(r"[A-Za-z0-9]+", team):
        raise PluginError(f"DEVELOPMENT_TEAM {team!r} must be alphanumeric")
    if not re.fullmatch(r"[A-Za-z0-9.-]+", rev_domain):
        raise PluginError(f"APPLICATION_REV_DOMAIN {rev_domain!r} is not a reverse domain")

    # --- Info.plist template -----------------------------------------------------------------
    _ensure(ctx, PLIST,
            _plist_string("CFBundleShortVersionString", r"@MIRALL_VERSION_STRING@"),
            _keep_groups("@MIRALL_VERSION@"),
            _plist_string("CFBundleShortVersionString", r"@MIRALL_VERSION@"),
            "CFBundleShortVersionString = @MIRALL_VERSION@")
    for key, text in (("NSHumanReadableCopyright", COPYRIGHT_TEXT),
                      ("NSLocalNetworkUsageDescription", LOCAL_NETWORK_TEXT)):
        _ensure(ctx, PLIST,
                _plist_string(key, r"[^<]*"),
                _keep_groups(_xml_text(text)),
                _plist_string(key, re.escape(_xml_text(text))),
                key)
    sparkle = r"\r?\n[ \t]*<key>(?:" + "|".join(SPARKLE_KEYS) + r")</key>\s*(?:<true/>|<false/>|<string>[^<]*</string>)"
    ctx.regex_sub(PLIST, sparkle, "", expect=None, what="remove Sparkle SU* keys")
    if re.search(r"<key>SU[A-Za-z]+</key>", ctx.read(PLIST)):
        raise PluginError(f"{PLIST}: a Sparkle SU* key is left that this plugin does not know")
    if re.search(r"nextcloud", ctx.read(PLIST), re.IGNORECASE):
        raise PluginError(f"{PLIST}: still names Nextcloud after the rebrand")

    # --- Xcode project defaults ----------------------------------------------------------------
    for key, value in (("DEVELOPMENT_TEAM", team), ("OC_APPLICATION_NAME", name), ("OC_APPLICATION_REV_DOMAIN", rev_domain)):
        _ensure(ctx, XCCONFIG,
                rf"(?m)^({key}=\$\({key}:default=)[^)\r\n]*(\))",
                _keep_groups(value),
                rf"(?m)^{key}=\$\({key}:default={re.escape(value)}\)",
                f"Build.xcconfig default {key}")

    other_team = rf"(DEVELOPMENT_TEAM = )(?!{re.escape(team)};)[A-Za-z0-9]+;"
    ctx.regex_sub(PBXPROJ, other_team, lambda m: m.group(1) + team + ";", expect=None, what="project DEVELOPMENT_TEAM")
    pbx = ctx.read(PBXPROJ)
    if re.search(other_team, pbx) or f"DEVELOPMENT_TEAM = {team};" not in pbx:
        raise PluginError(f"{PBXPROJ}: DEVELOPMENT_TEAM is not {team} everywhere (upstream changed?)")

    # --- FinderSync extension: fallback name in the Finder menu ---------------------------------
    _ensure(ctx, FINDERSYNC_M,
            r'(objectForInfoDictionaryKey:@"CFBundleName"\] \?: @")[^"]*(";)',
            _keep_groups(_objc_string(name)),
            r'objectForInfoDictionaryKey:@"CFBundleName"\] \?: @"' + re.escape(_objc_string(name)) + '";',
            "FinderSync fallback app name")

    # --- notification reply placeholder --------------------------------------------------------
    old, new = 'QObject::tr("Send a Nextcloud Talk reply")', 'QObject::tr("Send a Talk reply")'
    text = ctx.read(NOTIFICATIONS_MM)
    if old in text:
        ctx.replace(NOTIFICATIONS_MM, old, new, what="Talk reply placeholder")
    elif "Nextcloud Talk" in text:
        raise PluginError(f"{NOTIFICATIONS_MM}: 'Nextcloud Talk' found in an unknown form (upstream changed?)")


# ---------------------------------------------------------------------------------------------
# Standalone mode
# ---------------------------------------------------------------------------------------------

class _MiniContext:
    """The part of the tools/rebrandlib.py Context API that this plugin uses."""

    def __init__(self, root: Path, check: bool, verbose: bool, ids: dict):
        self.root = root
        self.check = check
        self.verbose = verbose
        self.ids = ids
        self.lane = LANE
        self.changes: list[tuple[str, str]] = []
        self._text: dict[str, str] = {}
        self._dirty: set[str] = set()

    def read(self, rel: str) -> str:
        if rel not in self._text:
            path = self.root / rel
            if not path.exists():
                raise PluginError(f"{rel}: file not found")
            self._text[rel] = path.read_bytes().decode("utf-8")
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
        if n == 0 or (count is not None and n != count):
            raise PluginError(f"{rel}: anchor found {n} times, expected {count}: {old[:120]!r}")
        self._set(rel, text.replace(old, new), what or f"replace {old[:60]!r}")
        return True

    def regex_sub(self, rel: str, pattern: str, repl, expect: int | None = 1, flags: int = 0, what: str | None = None) -> bool:
        text = self.read(rel)
        new_text, n = re.subn(pattern, repl, text, flags=flags)
        if n == 0 or new_text == text:
            return False
        if expect is not None and n != expect:
            raise PluginError(f"{rel}: pattern matched {n} times, expected {expect}: {pattern!r}")
        self._set(rel, new_text, what or f"regex {pattern[:60]!r} x{n}")
        return True

    def flush(self) -> None:
        if self.check:
            return
        for rel in sorted(self._dirty):
            (self.root / rel).write_bytes(self._text[rel].encode("utf-8"))
        self._dirty.clear()


def _make_context(root: Path, check: bool, verbose: bool):
    ids_path = root / "tools" / "branding" / "ids.json"
    lib_path = root / "tools" / "rebrandlib.py"
    if lib_path.exists() and ids_path.exists():
        spec = importlib.util.spec_from_file_location("rebrandlib", lib_path)
        lib = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(lib)
        ctx = lib.Context(root, check, verbose)
        ctx.lane = LANE
        return ctx
    ids = json.loads(ids_path.read_text(encoding="utf-8")) if ids_path.exists() else {}
    return _MiniContext(root, check, verbose, ids)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="NoDrive rebrand: macOS-only files")
    parser.add_argument("--check", action="store_true", help="change nothing; exit 1 if anything would change")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2], help="repository root")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)

    root = args.root.resolve()
    ctx = _make_context(root, args.check, args.verbose)
    try:
        apply(ctx)
    except Exception as exc:  # RebrandError from rebrandlib or our PluginError
        print(f"macos rebrand: ERROR: {exc}", file=sys.stderr)
        return 2
    changed = sorted({rel for rel, _ in ctx.changes})
    if args.check:
        if changed:
            print("macos rebrand: these files are not rebranded yet:")
            for rel in changed:
                print(f"  {rel}")
            return 1
        print("macos rebrand: up to date")
        return 0
    ctx.flush()
    print(f"macos rebrand: {len(changed)} file(s) changed" + "".join(f"\n  {rel}" for rel in changed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
