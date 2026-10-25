#!/usr/bin/env python3
"""NoDrive branding sweep and gate (spec section 6, plan section 11.1).

Searches the shipped parts of the desktop source tree for Nextcloud names,
links, store ids and colours (tools/branding/patterns.txt) and classifies
every hit:

  user-visible  -> must be fixed (the gate fails)
  internal      -> kept: identifier the server, OS or build depends on
                   (sub-classes: internal, server-required, not-shipped, dead-code)
  legal         -> kept: copyright / licence text

A hit is classified by the first matching line in tools/branding/allow-*.txt:

  path-glob<TAB>regex<TAB>class<TAB>reason

path-glob uses fnmatch ('*' also crosses '/'); regex is searched
case-insensitively in the hit text ('*' alone = any hit in those files);
class is one of internal, server-required, not-shipped, dead-code, legal;
the reason must not be empty. allow-shared.txt is owned by the Linux lane;
its rules (mostly generic internal identifiers) apply to every file.
allow-windows.txt and allow-macos.txt may only classify their own lane's
files (tools/branding/lanes.json); an entry that tries more is an error.
Comments in packaging files and SVGs are skipped (never shown).

Where it searches (spec section 6, step 2):
  ts          translations/*.ts: <source>, <translation>, <numerusform>
  qml         string literals in QML files (src/, theme/)
  ui          <string> texts in Qt Designer .ui files
  code        string literals in shipped source (src/, shell_integration/)
  strings     Apple .strings / .stringsdict / .xcstrings files
  packaging   installer, packaging and template files (admin/, systemd/,
              *.desktop.in, cmake/modules/*.in, *.plist, *.wxs, theme/*.xml,
              theme.qrc.in, NEXTCLOUD.cmake, nodrive/ ...), every line
  svg         text inside SVG images
  image       image file names, upstream trademarked image hashes and our
              generated images (tools/branding/icons-manifest.txt)
Not searched (not shipped): doc/, test/, .github/, README/CHANGELOG files.

Usage:
  python tools/check-branding.py [--lane linux|windows|macos|all]
         [--report FILE.md] [--write-sweep] [--root DIR] [--list]
  python tools/check-branding.py --artifact DIR --lane linux
Exit code 0 = no unclassified hit and no error, 1 = gate failed.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import html
import json
import os
import re
import shutil
import subprocess
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
CFG = TOOLS / "branding"

CLASSES = {"internal", "server-required", "not-shipped", "dead-code", "legal"}
LANE_OF_ALLOW = {"shared": {"shared", "linux", "windows", "macos"}, "linux": {"shared", "linux"}, "windows": {"windows"}, "macos": {"macos"}}
LANES_FOR = {"linux": {"shared", "linux"}, "windows": {"shared", "windows"}, "macos": {"shared", "macos"},
             "all": {"shared", "linux", "windows", "macos"}}

IMAGE_EXT = {".png", ".svg", ".ico", ".icns", ".bmp", ".jpg", ".jpeg", ".gif", ".webp", ".pdf"}
CODE_EXT = {".cpp", ".h", ".hpp", ".c", ".cc", ".mm", ".m", ".swift", ".rc", ".py", ".java", ".js"}
STRINGS_EXT = {".strings", ".stringsdict", ".xcstrings"}
PACKAGING_EXT = {".in", ".plist", ".xml", ".json", ".desktop", ".service", ".wxs", ".wxi", ".wxl", ".nsi", ".nsh",
                 ".iss", ".bat", ".cmd", ".ps1", ".sh", ".vbs", ".ini", ".entitlements", ".xcconfig", ".pbxproj",
                 ".txt", ".md", ".cfg", ".conf", ".manifest", ".yml", ".yaml", ".pkgproj", ".sdef", ".iconset",
                 ".storyboard", ".xib", ".cmake"}
SKIP_DIRS = {".git", "build", "build-client", ".mac-crafter", "__pycache__", "node_modules"}
NOT_SHIPPED_PREFIXES = ("doc/", "test/", ".github/", ".tx/", "tools/", "docs/", "plans/")
# Files at the top level that are packaging or the upstream theme hook.
TOP_LEVEL_PACKAGING = {"mirall.desktop.in", "theme.qrc.in", "NEXTCLOUD.cmake", "NextcloudCPack.cmake",
                       "CPackOptions.cmake.in", "craftmaster.ini", "sync-exclude.lst"}
SOURCE_TEMPLATE_SUFFIXES = (".h.in", ".c.in", ".cpp.in", ".hpp.in")
TEST_DIR_RE = re.compile(r"(^|/)(Tests?|tests?|UITests|[A-Za-z]*Tests)(/|$)")


@dataclass
class Hit:
    path: str
    line: int
    scope: str
    text: str
    keywords: list[str]
    lane: str = "shared"
    cls: str = ""          # "" = unclassified (user-visible until proven otherwise)
    reason: str = ""
    rule: str = ""


@dataclass
class Allow:
    source: str
    lineno: int
    glob: str
    regex: re.Pattern | None
    cls: str
    reason: str
    owner: str
    used: int = 0


@dataclass
class Result:
    hits: list[Hit] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    files_scanned: Counter = field(default_factory=Counter)


# ---------------------------------------------------------------------------
# configuration
# ---------------------------------------------------------------------------

def load_patterns() -> list[tuple[str, re.Pattern]]:
    out = []
    for raw in (CFG / "patterns.txt").read_text(encoding="utf-8").splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        label, _, rx = raw.partition("\t")
        out.append((label.strip(), re.compile(rx.strip(), re.I)))
    return out


def load_lanes() -> dict:
    return json.loads((CFG / "lanes.json").read_text(encoding="utf-8"))


def lane_of(path: str, lanes: dict) -> str:
    for g in lanes.get("shared_overrides", []):
        if fnmatch.fnmatchcase(path, g):
            return "shared"
    for lane in ("windows", "macos", "linux"):
        for g in lanes.get(lane, []):
            if fnmatch.fnmatchcase(path, g):
                return lane
    return "shared"


def load_allow(res: Result) -> list[Allow]:
    entries = []
    for f in sorted(CFG.glob("allow-*.txt")):
        owner = f.stem[len("allow-"):]
        if owner not in LANE_OF_ALLOW:
            res.errors.append(f"{f.name}: unknown lane '{owner}' (use allow-shared/-windows/-macos.txt)")
            continue
        for n, raw in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
            if not raw.strip() or raw.lstrip().startswith("#"):
                continue
            parts = raw.split("\t")
            if len(parts) != 4:
                res.errors.append(f"{f.name}:{n}: need 4 tab-separated fields (glob, regex, class, reason)")
                continue
            glob, rx, cls, reason = (p.strip() for p in parts)
            if cls not in CLASSES:
                res.errors.append(f"{f.name}:{n}: class '{cls}' not in {sorted(CLASSES)}")
                continue
            if not reason:
                res.errors.append(f"{f.name}:{n}: empty reason")
                continue
            try:
                regex = None if rx in ("", "*") else re.compile(rx, re.I)
            except re.error as e:
                res.errors.append(f"{f.name}:{n}: bad regex: {e}")
                continue
            entries.append(Allow(f.name, n, glob, regex, cls, reason, owner))
    return entries


# ---------------------------------------------------------------------------
# file selection
# ---------------------------------------------------------------------------

def walk(root: Path):
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = Path(dirpath).relative_to(root).as_posix()
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS and not (rel_dir == "." and d.startswith("build"))]
        for fn in filenames:
            rel = (Path(rel_dir) / fn).as_posix() if rel_dir != "." else fn
            yield rel


def scope_of(rel: str) -> str | None:
    if rel.startswith(NOT_SHIPPED_PREFIXES) or rel.startswith("src/3rdparty/"):
        return None
    name = rel.rsplit("/", 1)[-1]
    ext = os.path.splitext(name)[1].lower()
    if rel.startswith("translations/") and ext == ".ts":
        return "ts"
    if ext in IMAGE_EXT:
        if rel.startswith(("theme/", "admin/", "shell_integration/", "src/", "nodrive/")) or "/" not in rel:
            return "image"
        return None
    if "/" not in rel:
        return "packaging" if name in TOP_LEVEL_PACKAGING else None
    if name.endswith(SOURCE_TEMPLATE_SUFFIXES):
        return None  # C/C++ header templates: macro names only
    if rel.startswith("shell_integration/") and TEST_DIR_RE.search(rel):
        return None
    if ext == ".qml" and rel.startswith(("src/", "theme/")):
        return "qml"
    if ext == ".ui" and rel.startswith("src/"):
        return "ui"
    if ext in STRINGS_EXT:
        return "strings"
    if ext in CODE_EXT and rel.startswith(("src/", "shell_integration/")):
        return "code"
    if rel.startswith(("admin/", "systemd/", "nodrive/")):
        return "packaging" if (ext in PACKAGING_EXT or ext == "" or name.endswith(".in")) else None
    if rel.startswith("cmake/modules/") and name.endswith(".in"):
        return "packaging"
    if rel.startswith("theme/") and ext in {".xml", ".json", ".in"}:
        return "packaging"
    if rel.startswith(("src/", "shell_integration/")) and ext in PACKAGING_EXT and ext not in {".txt", ".md", ".cmake"}:
        return "packaging"
    return None


# ---------------------------------------------------------------------------
# extractors: yield (line, text)
# ---------------------------------------------------------------------------

def _line_at(text: str, pos: int) -> int:
    return text.count("\n", 0, pos) + 1


def c_literals(text: str, hash_comments: bool = False, slash_comments: bool = True):
    """String literals of C-like code (C++, ObjC, Swift, QML, JS, RC, Python), comments skipped."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if not slash_comments and c == "/":
            i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif hash_comments and c == "#":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r'R"([^(\s]{0,16})\(', text[i:])
            if not m:
                i += 1
                continue
            end = text.find(")" + m.group(1) + '"', i + m.end())
            end = n if end < 0 else end
            yield _line_at(text, i), text[i + m.end():end]
            i = end + len(m.group(1)) + 2
        elif c in "\"'":
            q = c
            if text.startswith(q * 3, i):  # Swift / Python triple-quoted
                end = text.find(q * 3, i + 3)
                end = n if end < 0 else end
                yield _line_at(text, i), text[i + 3:end]
                i = end + 3
                continue
            j = i + 1
            while j < n and text[j] != q and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            line_start = text.rfind("\n", 0, i) + 1
            directive = text[line_start:i].lstrip().startswith(("#include", "#import"))
            if not directive and (q == '"' or j - i > 2):
                yield _line_at(text, i), text[i + 1:j]
            i = j + 1
        else:
            i += 1


def py_literals(text: str):
    yield from c_literals(text, hash_comments=True, slash_comments=False)


def ui_strings(text: str):
    for m in re.finditer(r"<string(?:\s[^>]*)?>(.*?)</string>", text, re.S):
        yield _line_at(text, m.start()), html.unescape(m.group(1))


_TS_CTX = re.compile(r"<context>\s*<name>(.*?)</name>(.*?)</context>", re.S)
_TS_MSG = re.compile(r"<message(?P<attrs>[^>]*)>(?P<body>.*?)</message>", re.S)
_TS_TEXT = re.compile(r"<(source|translation|numerusform)\b[^>]*>(.*?)</\1>", re.S)


def ts_texts(text: str):
    """(line, element text, display prefix, message source) for every message text."""
    for cm in _TS_CTX.finditer(text):
        ctx_name = cm.group(1)
        base = cm.start(2)
        for mm in _TS_MSG.finditer(cm.group(2)):
            sm = re.search(r"<source>(.*?)</source>", mm.group("body"), re.S)
            source = html.unescape(sm.group(1)) if sm else ""
            for tm in _TS_TEXT.finditer(mm.group("body")):
                pos = base + mm.start("body") + tm.start()
                yield _line_at(text, pos), html.unescape(tm.group(2)), f"[{ctx_name}] <{tm.group(1)}> ", source


class SourceHaystack:
    """All translatable-source files of src/, to tell live .ts messages from stale ones."""

    def __init__(self, root: Path):
        parts = []
        src = root / "src"
        if src.exists():
            for p in src.rglob("*"):
                if p.suffix.lower() in {".cpp", ".h", ".mm", ".qml", ".ui", ".js"} and p.is_file():
                    parts.append(p.read_text(encoding="utf-8", errors="replace"))
        self.text = "\n".join(parts)
        self.cache: dict[str, bool] = {}

    def is_live(self, source: str) -> bool:
        if source in self.cache:
            return self.cache[source]
        # A short prefix, because C++ often splits long texts into adjacent
        # literals. A false "live" only means the hit needs a manual class.
        probe = source.split("\n", 1)[0][:28]
        variants = {probe, html.escape(probe, quote=True), html.escape(probe, quote=False),
                    probe.replace("\\", "\\\\").replace('"', '\\"')}
        variants |= {v.replace("&#x27;", "&apos;") for v in variants}
        live = any(v and v in self.text for v in variants)
        self.cache[source] = live
        return live


def all_lines(text: str):
    for n, line in enumerate(text.splitlines(), 1):
        yield n, line


XML_LIKE = {".xml", ".svg", ".qrc", ".plist", ".wxs", ".wxi", ".wxl", ".xib", ".storyboard", ".entitlements", ".ts", ".html"}
HASH_COMMENT = {".sh", ".cmake", ".yml", ".yaml", ".ini", ".conf", ".cfg", ".desktop", ".service", ".ps1", ".txt", ".lst", ""}


def effective_ext(rel: str) -> str:
    name = rel.rsplit("/", 1)[-1]
    if name.endswith(".in"):
        name = name[:-3]
    if name == "CMakeLists.txt":
        return ".cmake"
    return os.path.splitext(name)[1].lower()


def text_lines(text: str, rel: str):
    """Lines of a packaging/text file with comments removed (comments are never shown)."""
    ext = effective_ext(rel)
    if ext in XML_LIKE:
        text = re.sub(r"<!--.*?-->", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
        yield from all_lines(text)
    elif ext in HASH_COMMENT:
        for n, line in all_lines(text):
            if not line.lstrip().startswith("#"):
                yield n, line
    elif ext in {".bat", ".cmd"}:
        for n, line in all_lines(text):
            s = line.lstrip().lower()
            if not (s.startswith("rem ") or s.startswith("::")):
                yield n, line
    elif ext in {".nsi", ".nsh", ".iss"}:
        for n, line in all_lines(text):
            if not line.lstrip().startswith((";", "#")):
                yield n, line
    else:
        yield from all_lines(text)


def cmake_lines(text: str):
    for n, line in enumerate(text.splitlines(), 1):
        stripped = line.lstrip()
        if stripped.startswith("#"):
            continue
        yield n, line


# ---------------------------------------------------------------------------
# scanning
# ---------------------------------------------------------------------------

def match_keywords(text: str, patterns) -> list[str]:
    return [label for label, rx in patterns if rx.search(text)]


def read_text(p: Path) -> str | None:
    try:
        data = p.read_bytes()
    except OSError:
        return None
    if b"\x00" in data[:4096]:
        return None
    return data.decode("utf-8", errors="replace")


def load_manifest(root: Path) -> dict[str, str]:
    # Only the scanned tree's own manifest counts (an upstream tree has none).
    p = root / "tools" / "branding" / "icons-manifest.txt"
    out = {}
    if p.exists():
        for line in p.read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or not line.strip():
                continue
            h, _, rel = line.partition("  ")
            out[rel.strip()] = h.strip()
    return out


def load_trademark_hashes() -> dict[str, str]:
    p = CFG / "upstream-trademark-hashes.txt"
    out = {}
    if p.exists():
        for line in p.read_text(encoding="utf-8").splitlines():
            if line.startswith("#") or not line.strip():
                continue
            h, _, rel = line.partition("  ")
            out[h.strip()] = rel.strip()
    return out


def shipped_theme_files(root: Path, ids: dict) -> set[str] | None:
    """theme/ files that the build compiles into the app (theme.qrc.in) or installs."""
    p = root / "theme.qrc.in"
    if not p.exists():
        return None
    icon = ids.get("cmake", {}).get("APPLICATION_ICON_NAME", "NoDrive")
    text = p.read_text(encoding="utf-8").replace("@APPLICATION_ICON_NAME@", icon).replace("@STATE_SUBDIR@", "")
    files = set(re.findall(r"<file(?:\s[^>]*)?>\s*([^<\s]+)\s*</file>", text))
    return files


def scan_tree(root: Path, lanes_wanted: set[str], patterns, res: Result) -> None:
    lanes = load_lanes()
    manifest = load_manifest(root)
    trademark = load_trademark_hashes()
    ids_path = root / "tools" / "branding" / "ids.json"
    ids = json.loads(ids_path.read_text(encoding="utf-8")) if ids_path.exists() else json.loads((CFG / "ids.json").read_text(encoding="utf-8"))
    icon = ids["cmake"]["APPLICATION_ICON_NAME"]
    shipped_theme = shipped_theme_files(root, ids)
    overlay_vars = set()
    overlay = root / "nodrive" / "NODRIVE.cmake"
    if overlay.exists():
        overlay_vars = set(re.findall(r"^\s*set\(\s*([A-Z0-9_]+)", overlay.read_text(encoding="utf-8"), re.M))

    haystack = None
    for rel in sorted(walk(root)):
        scope = scope_of(rel)
        if not scope:
            continue
        lane = lane_of(rel, lanes)
        if lane not in lanes_wanted:
            continue
        p = root / rel
        res.files_scanned[scope] += 1
        ext = os.path.splitext(rel)[1].lower()

        if scope == "image":
            data = p.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            if rel in manifest:
                if manifest[rel] != digest:
                    res.errors.append(f"{rel}: differs from tools/branding/icons-manifest.txt - run python tools/make-icons.py")
            if digest in trademark:
                res.hits.append(Hit(rel, 0, "image", f"upstream trademarked image ({trademark[digest]})", ["trademark image"], lane))
            kws = match_keywords(rel, patterns)
            if kws:
                h = Hit(rel, 0, "image", f"file name {rel}", kws, lane)
                if rel in manifest and manifest[rel] == digest:
                    h.cls, h.reason, h.rule = "internal", "file name only; content is the NoDrive logo made by tools/make-icons.py (icons-manifest)", "auto:manifest"
                res.hits.append(h)
            if ext == ".svg" and rel not in manifest:
                text = read_text(p) or ""
                for n, line in text_lines(text, rel):
                    kws = match_keywords(line, patterns)
                    if kws:
                        res.hits.append(Hit(rel, n, "svg", line.strip()[:200], kws, lane))
            continue

        text = read_text(p)
        if text is None:
            continue
        if scope == "ts":
            items = ts_texts(text)
        elif scope == "ui":
            items = ui_strings(text)
        elif scope in ("qml",):
            items = c_literals(text)
        elif scope == "code":
            items = py_literals(text) if ext == ".py" else c_literals(text)
        else:
            items = text_lines(text, rel)
        for entry in items:
            n, item = entry[0], entry[1]
            prefix = entry[2] if len(entry) > 2 else ""
            kws = match_keywords(item, patterns)
            if not kws:
                continue
            snippet = (prefix + " ".join(item.split()))[:240]
            h = Hit(rel, n, scope, snippet, kws, lane)
            if scope == "ts":
                if haystack is None:
                    haystack = SourceHaystack(root)
                if not haystack.is_live(entry[3]):
                    h.cls, h.reason, h.rule = "dead-code", "stale translation: its source text is no longer in src/, so it is never shown", "auto:stale-ts"
            if rel == "NEXTCLOUD.cmake":
                m = re.match(r"\s*set\(\s*([A-Z0-9_]+)", item)
                if m and m.group(1) in overlay_vars:
                    h.cls, h.reason, h.rule = "internal", f"upstream default of {m.group(1)}; overridden by nodrive/NODRIVE.cmake", "auto:overlay"
            res.hits.append(h)

    # theme images that are neither in theme.qrc nor installed are not shipped
    if shipped_theme is not None:
        for h in res.hits:
            if h.cls or h.scope not in ("image", "svg") or not h.path.startswith("theme/"):
                continue
            installed = fnmatch.fnmatchcase(h.path, f"theme/colored/*-{icon}-icon*") or fnmatch.fnmatchcase(
                h.path, f"theme/colored/*-{icon}-w10startmenu*")
            if h.path not in shipped_theme and not installed:
                h.cls, h.reason, h.rule = "not-shipped", "not listed in theme.qrc.in (with NoDrive values) and not installed by CMake", "auto:qrc"


def classify(res: Result, allow: list[Allow], lanes: dict) -> None:
    for h in res.hits:
        if h.cls:
            continue
        for a in allow:
            if not fnmatch.fnmatchcase(h.path, a.glob):
                continue
            if a.regex is not None and not a.regex.search(h.text):
                continue
            # Package (artifact) paths are not lane-owned source files.
            if h.lane != "artifact" and h.lane not in LANE_OF_ALLOW[a.owner]:
                res.errors.append(f"{a.source}:{a.lineno}: lane '{a.owner}' may not classify {h.path} (owned by {h.lane})")
                continue
            a.used += 1
            h.cls, h.reason, h.rule = a.cls, a.reason, f"{a.source}:{a.lineno}"
            break


# ---------------------------------------------------------------------------
# artifact mode (packaged app)
# ---------------------------------------------------------------------------

URL_RE = re.compile(rb"https?://[A-Za-z0-9._~:/?#\[\]@!$&'()*+,;=%-]{4,}")
ALLOWED_URLS = re.compile(r"^https?://(own|next)cloud\.org/ns\b", re.I)


QM_MAGIC = bytes.fromhex("3CB86418CAEF9C95CD211CBF60A1BDDD")


def qm_texts(data: bytes):
    """(context, text) for every translation and source text in a compiled Qt .qm file.

    Used when lconvert is not installed. Format (qttools src/linguist/shared/qm.cpp):
    16-byte magic, then blocks <tag:1><length:4 BE><data>; the Messages block (0x69)
    holds items <tag:1>...: 1 end of message, 3 translation (UTF-16BE, length 0xFFFFFFFF
    = empty), 6 source text, 7 context, 8 comment (UTF-8, length-prefixed), 5 obsolete
    (4 bytes).
    """
    if not data.startswith(QM_MAGIC):
        return
    pos = len(QM_MAGIC)
    while pos + 5 <= len(data):
        tag, length = data[pos], int.from_bytes(data[pos + 1:pos + 5], "big")
        block = data[pos + 5:pos + 5 + length]
        pos += 5 + length
        if tag != 0x69:
            continue
        i, context, texts = 0, "", []
        while i < len(block):
            t = block[i]
            i += 1
            if t == 1:  # end of message
                for text in texts:
                    yield context, text
                context, texts = "", []
            elif t == 5:
                i += 4
            elif t in (2, 3, 4, 6, 7, 8):
                n = int.from_bytes(block[i:i + 4], "big")
                i += 4
                if n == 0xFFFFFFFF:
                    continue
                raw = block[i:i + n]
                i += n
                if t in (2, 3, 4):
                    value = raw.decode("utf-16-be", "replace")
                else:
                    value = raw.decode("utf-8", "replace")
                if t in (4, 7):
                    context = value
                elif t in (2, 3, 6):
                    texts.append(value)
            else:
                break  # unknown tag: stop reading this block


def scan_artifact(art: Path, patterns, res: Result) -> None:
    lconvert = shutil.which("lconvert") or shutil.which("lconvert-qt6") or shutil.which("lconvert6")
    for p in sorted(art.rglob("*")):
        if not p.is_file() or p.is_symlink():
            continue
        rel = p.relative_to(art).as_posix()
        ext = p.suffix.lower()
        kws = match_keywords(p.name, patterns)
        if kws:
            res.hits.append(Hit(rel, 0, "artifact-name", f"file name {p.name}", kws, "artifact"))
        if ext in {".desktop", ".plist", ".xml", ".json", ".ini", ".service", ".txt", ".strings", ".qml", ".conf"}:
            text = read_text(p) or ""
            for n, line in all_lines(text):
                kws = match_keywords(line, patterns)
                if kws:
                    res.hits.append(Hit(rel, n, "artifact-text", " ".join(line.split())[:200], kws, "artifact"))
            res.files_scanned["artifact-text"] += 1
        elif ext == ".qm" and lconvert:
            out = subprocess.run([lconvert, "-of", "ts", "-o", "-", str(p)], capture_output=True, text=True)
            for n, item, prefix, _src in ts_texts(out.stdout):
                kws = match_keywords(item, patterns)
                if kws:
                    res.hits.append(Hit(rel, n, "artifact-qm", (prefix + " ".join(item.split()))[:240], kws, "artifact"))
            res.files_scanned["artifact-qm"] += 1
        elif ext == ".qm":
            for n, (ctx_name, item) in enumerate(qm_texts(p.read_bytes()), 1):
                kws = match_keywords(item, patterns)
                if kws:
                    prefix = f"[{ctx_name}] " if ctx_name else ""
                    res.hits.append(Hit(rel, n, "artifact-qm", (prefix + " ".join(item.split()))[:240], kws, "artifact"))
            res.files_scanned["artifact-qm"] += 1
        else:
            try:
                data = p.read_bytes()
            except OSError:
                continue
            res.files_scanned["artifact-binary"] += 1
            for m in URL_RE.finditer(data):
                url = m.group(0).decode("ascii", "replace")
                if match_keywords(url, patterns) and not ALLOWED_URLS.match(url):
                    res.hits.append(Hit(rel, 0, "artifact-url", url[:200], match_keywords(url, patterns), "artifact"))


# ---------------------------------------------------------------------------
# reports
# ---------------------------------------------------------------------------

def bucket(h: Hit) -> str:
    if not h.cls:
        return "unclassified"
    return "legal" if h.cls == "legal" else "internal"


def summary_lines(res: Result, label: str) -> list[str]:
    by = Counter(bucket(h) for h in res.hits)
    sub = Counter(h.cls or "unclassified" for h in res.hits)
    scopes = Counter(h.scope for h in res.hits)
    return [
        f"check-branding [{label}]: {len(res.hits)} hits "
        f"(unclassified {by['unclassified']}, internal {by['internal']}, legal {by['legal']}); "
        f"files scanned {sum(res.files_scanned.values())} {dict(sorted(res.files_scanned.items()))}",
        f"  by class: {dict(sorted(sub.items()))}",
        f"  by scope: {dict(sorted(scopes.items()))}",
    ]


def md_escape(s: str) -> str:
    return s.replace("|", "\\|").replace("`", "'")


def write_report(path: Path, res: Result, label: str, allow: list[Allow]) -> None:
    lines = [f"# Branding sweep ({label})", "", "Generated by `python tools/check-branding.py`. Classes: "
             "unclassified = user-visible until fixed (gate fails); internal (internal, server-required, "
             "not-shipped, dead-code) = kept with a reason; legal = kept.", ""]
    lines += ["```"] + summary_lines(res, label) + ["```", ""]
    if res.errors:
        lines += ["## Errors", ""] + [f"- {md_escape(e)}" for e in res.errors] + [""]
    unc = [h for h in res.hits if not h.cls]
    lines += ["## Unclassified hits", ""]
    if unc:
        lines += ["| File | Line | Scope | Text |", "|---|---|---|---|"]
        lines += [f"| `{h.path}` | {h.line} | {h.scope} | {md_escape(h.text)} |" for h in unc]
    else:
        lines.append("None.")
    lines += ["", "## Kept hits by rule", "", "| Class | Rule | Hits | Reason | Example |", "|---|---|---|---|---|"]
    groups = defaultdict(list)
    for h in res.hits:
        if h.cls:
            groups[(h.cls, h.rule, h.reason)].append(h)
    for (cls, rule, reason), hs in sorted(groups.items(), key=lambda kv: (kv[0][0], -len(kv[1]))):
        ex = hs[0]
        lines.append(f"| {cls} | {rule} | {len(hs)} | {md_escape(reason)} | `{ex.path}`:{ex.line} {md_escape(ex.text[:80])} |")
    unused = [a for a in allow if a.used == 0]
    if unused:
        lines += ["", "## Allowlist entries with no hit in this run", ""]
        lines += [f"- {a.source}:{a.lineno} `{md_escape(a.glob)}` {a.cls}" for a in unused]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def update_trademark_hashes(root: Path, ref: str) -> int:
    """Write upstream-trademark-hashes.txt: SHA-256 of every file that upstream's
    REUSE.toml marks LicenseRef-NextcloudTrademarks at ``ref`` (e.g. v35.0.0)."""
    import tomllib

    def git(*a, text=True):
        return subprocess.run(["git", "-C", str(root), *a], capture_output=True, text=text, check=True).stdout

    reuse = tomllib.loads(git("show", f"{ref}:REUSE.toml"))
    globs = []
    for ann in reuse.get("annotations", []):
        if "Trademark" in str(ann.get("SPDX-License-Identifier", "")):
            paths = ann["path"] if isinstance(ann["path"], list) else [ann["path"]]
            globs += paths
    files = git("ls-tree", "-r", "--name-only", ref).splitlines()
    lines = [f"# SHA-256 of upstream files marked LicenseRef-NextcloudTrademarks in REUSE.toml at {ref}.",
             f"# Generated by: python tools/check-branding.py --update-trademark-hashes {ref}"]
    rows = []
    for f in files:
        if any(fnmatch.fnmatchcase(f, g) for g in globs):
            data = git("cat-file", "blob", f"{ref}:{f}", text=False)
            rows.append(f"{hashlib.sha256(data).hexdigest()}  {f}")
    (CFG / "upstream-trademark-hashes.txt").write_text("\n".join(lines + sorted(rows, key=lambda r: r[66:])) + "\n", encoding="utf-8")
    print(f"wrote {len(rows)} trademarked file hashes from {ref}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--update-trademark-hashes", metavar="REF", help="regenerate tools/branding/upstream-trademark-hashes.txt from git REF")
    ap.add_argument("--lane", default="linux", choices=sorted(LANES_FOR), help="which lanes' files to check (default linux = shared + linux)")
    ap.add_argument("--root", default=str(TOOLS.parent), help="tree to scan (default: this checkout)")
    ap.add_argument("--artifact", help="scan an extracted package directory instead of the source tree")
    ap.add_argument("--report", help="write a Markdown report to this file")
    ap.add_argument("--write-sweep", action="store_true", help="write docs/branding-sweep.md (use with --lane all)")
    ap.add_argument("--list", action="store_true", help="print every unclassified hit")
    ap.add_argument("--list-all", action="store_true", help="print every hit with its class")
    ap.add_argument("--no-allow", action="store_true", help="ignore the allowlists (raw hit count)")
    args = ap.parse_args()

    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    if args.update_trademark_hashes:
        return update_trademark_hashes(Path(args.root), args.update_trademark_hashes)
    patterns = load_patterns()
    res = Result()
    allow = [] if args.no_allow else load_allow(res)
    lanes = load_lanes()
    if args.artifact:
        label = f"artifact {args.artifact}, lane {args.lane}"
        scan_artifact(Path(args.artifact), patterns, res)
    else:
        label = f"lane {args.lane}"
        scan_tree(Path(args.root), LANES_FOR[args.lane], patterns, res)
    if not args.no_allow:
        classify(res, allow, lanes)

    unc = [h for h in res.hits if not h.cls]
    if args.list or args.list_all:
        for h in (res.hits if args.list_all else unc):
            print(f"{h.cls or 'UNCLASSIFIED':15} {h.path}:{h.line} [{h.scope}] {h.text[:150]}")
    for line in summary_lines(res, label):
        print(line)
    for w in res.warnings:
        print(f"warning: {w}")
    for e in res.errors:
        print(f"ERROR: {e}")
    if args.report:
        write_report(Path(args.report), res, label, allow)
    if args.write_sweep:
        write_report(Path(args.root) / "docs" / "branding-sweep.md", res, label, allow)
    if unc or res.errors:
        print(f"check-branding: FAIL ({len(unc)} unclassified hit(s), {len(res.errors)} error(s))")
        return 1
    print("check-branding: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
