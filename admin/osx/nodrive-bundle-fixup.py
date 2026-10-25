#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
"""Make a built NoDrive.app self-contained and check it (used by admin/osx/nodrive-package.sh).

Why: in bundle builds CMake installs libnodrivesync and libnodrive_csync into Contents/MacOS,
but the executables only search Contents/Frameworks and the Craft root of the build machine.
The macdeployqt step of the build cannot resolve those @rpath references either ("Cannot resolve
rpath"), so the bundle from the Craft image does not start on another Mac ("Library not loaded").

  nodrive-bundle-fixup.py --fix   APP   1. delete LC_RPATH entries that point outside the bundle
                                           (the build machine's folders),
                                        2. for every @rpath dependency that does not resolve, add a
                                           run path @loader_path/<folder of that file in the bundle>.
  nodrive-bundle-fixup.py --check APP   every non-system dependency of every Mach-O file in the
                                        bundle resolves inside the bundle; exit 1 otherwise.

Must run before signing: install_name_tool invalidates signatures.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

SYSTEM_PREFIXES = ("/System/", "/usr/lib/", "/Library/Apple/")


def run(*cmd: str) -> str:
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def is_macho(p: Path) -> bool:
    if p.is_symlink() or not p.is_file():
        return False
    with p.open("rb") as f:
        magic = f.read(4)
    return magic in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def load_commands(p: Path) -> tuple[str | None, list[str], list[str]]:
    """(install name, dependencies, run paths) from otool -l."""
    ident, deps, rpaths = None, [], []
    cmd = None
    for line in run("otool", "-l", str(p)).splitlines():
        s = line.strip()
        if s.startswith("cmd "):
            cmd = s.split()[1]
        elif s.startswith("name ") and cmd in ("LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LAZY_LOAD_DYLIB"):
            deps.append(s[5:].rsplit(" (offset", 1)[0])
        elif s.startswith("name ") and cmd == "LC_ID_DYLIB":
            ident = s[5:].rsplit(" (offset", 1)[0]
        elif s.startswith("path ") and cmd == "LC_RPATH":
            rpaths.append(s[5:].rsplit(" (offset", 1)[0])
    return ident, deps, rpaths


def main_executable(app: Path, f: Path) -> Path:
    """The executable whose @executable_path applies to f: the innermost .app/.appex around f."""
    for parent in f.parents:
        if parent.suffix in (".app", ".appex") and (parent / "Contents" / "MacOS").is_dir():
            plist = parent / "Contents" / "Info.plist"
            exe = subprocess.run(["/usr/libexec/PlistBuddy", "-c", "Print :CFBundleExecutable", str(plist)],
                                 capture_output=True, text=True).stdout.strip()
            if exe:
                return parent / "Contents" / "MacOS" / exe
        if parent == app:
            break
    return app / "Contents" / "MacOS" / "NoDrive"


def expand(path: str, f: Path, exe: Path) -> str:
    return path.replace("@loader_path", str(f.parent)).replace("@executable_path", str(exe.parent))


def resolve(dep: str, f: Path, rpaths: list[str], exe: Path, exe_rpaths: list[str]) -> Path | None:
    if dep.startswith("@rpath/"):
        tail = dep[len("@rpath/"):]
        for rp in rpaths + exe_rpaths:  # own run paths, then the main executable's (load chain)
            base = expand(rp, f, exe) if rp.startswith("@loader_path") or rp.startswith("@executable_path") else rp
            candidate = Path(base) / tail
            if candidate.exists():
                return candidate
        return None
    candidate = Path(expand(dep, f, exe))
    return candidate if candidate.exists() else None


def machos(app: Path) -> list[Path]:
    return sorted(p for p in app.rglob("*") if ".dSYM" not in p.parts and is_macho(p))


def fix(app: Path) -> int:
    app = app.resolve()
    changed = 0
    files = machos(app)
    # 1. run paths that point outside the bundle (build machine folders)
    for f in files:
        _, _, rpaths = load_commands(f)
        for rp in rpaths:
            if rp.startswith("@"):
                continue
            if not Path(rp).resolve().is_relative_to(app):
                run("install_name_tool", "-delete_rpath", rp, str(f))
                print(f"  {f.relative_to(app)}: removed run path {rp}")
                changed += 1
    # 2. unresolved @rpath dependencies: add a run path to the folder that holds the library
    for f in files:
        exe = main_executable(app, f)
        _, deps, rpaths = load_commands(f)
        _, _, exe_rpaths = load_commands(exe) if exe.exists() and exe != f else (None, [], [])
        added: set[str] = set()
        for dep in deps:
            if not dep.startswith("@rpath/") or resolve(dep, f, rpaths + sorted(added), exe, exe_rpaths):
                continue
            tail = dep[len("@rpath/"):]
            # folder that holds <tail>: Frameworks first, then MacOS, then anywhere in the bundle
            bases = [d for d in (app / "Contents" / "Frameworks", app / "Contents" / "MacOS") if (d / tail).exists()]
            if not bases:
                bases = sorted({Path(str(p)[: -len(tail) - 1]) for p in app.rglob(Path(tail).name)
                                if p.as_posix().endswith("/" + tail)})
            if not bases:
                print(f"  {f.relative_to(app)}: {dep} is not in the bundle", file=sys.stderr)
                continue
            rel = os.path.relpath(bases[0], f.parent)
            rp = "@loader_path" if rel == "." else f"@loader_path/{rel}"
            if rp not in rpaths and rp not in added:
                run("install_name_tool", "-add_rpath", rp, str(f))
                added.add(rp)
                print(f"  {f.relative_to(app)}: added run path {rp} (for {dep})")
                changed += 1
    print(f"bundle fix-up: {changed} change(s) in {len(files)} Mach-O files")
    return 0


def check(app: Path) -> int:
    app = app.resolve()
    files = machos(app)
    problems = []
    deps_total = 0
    for f in files:
        exe = main_executable(app, f)
        ident, deps, rpaths = load_commands(f)
        _, _, exe_rpaths = load_commands(exe) if exe.exists() and exe != f else (None, [], [])
        for dep in deps:
            if dep == ident or dep.startswith(SYSTEM_PREFIXES):
                continue
            deps_total += 1
            target = resolve(dep, f, rpaths, exe, exe_rpaths)
            if target is None:
                problems.append(f"{f.relative_to(app)}: {dep} does not resolve")
            elif not target.resolve().is_relative_to(app):
                problems.append(f"{f.relative_to(app)}: {dep} resolves outside the bundle ({target})")
        for rp in rpaths:
            if not rp.startswith("@") and not Path(rp).resolve().is_relative_to(app):
                problems.append(f"{f.relative_to(app)}: run path outside the bundle: {rp}")
    for p in problems:
        print("  " + p)
    print(f"bundle check: {len(files)} Mach-O files, {deps_total} bundled dependencies, {len(problems)} problem(s)")
    return 1 if problems else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--fix", action="store_true")
    mode.add_argument("--check", action="store_true")
    ap.add_argument("app", type=Path)
    a = ap.parse_args()
    return fix(a.app) if a.fix else check(a.app)


if __name__ == "__main__":
    sys.exit(main())
