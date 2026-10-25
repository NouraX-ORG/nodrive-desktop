"""Shared helpers for tools/rebrand.py and the lane plugins in tools/rebrand.d/.

Every helper is idempotent: running the rebrand twice changes nothing the
second time. In check mode (``ctx.check``) nothing is written; the planned
changes are only recorded, so ``rebrand.py --check`` can fail when the tree
is not fully rebranded.

Plugin contract (tools/rebrand.d/<lane>.py)::

    LANE = "windows"            # module constant
    def apply(ctx): ...          # called by rebrand.py, in file-name order

    ctx.root   (Path)  repository root
    ctx.check  (bool)  True for --check (no writes)
    ctx.ids    (dict)  tools/branding/ids.json, read only
    ctx.replace(path, old, new, count=1)
        Exact text replace. No-op when ``new`` is already present.
        Error when neither ``old`` nor ``new`` is found (upstream changed),
        or when ``old`` occurs a different number of times than ``count``
        (pass count=None to replace every occurrence).
    ctx.regex_sub(path, pattern, repl, expect=1)
        re.subn on the text. Zero matches = already applied (no-op).
        Error when the number of matches is not ``expect`` (None = any).
    ctx.write(path, text_or_bytes)   writes only when the content differs
    ctx.copy(src, dst)               byte copy, only when different
    ctx.require(path, text)          error unless ``text`` is in the file
    ctx.read(path) -> str            current text (including pending edits)

Paths are relative to the repository root and use forward slashes.
Text files are read and written as UTF-8 with their line endings untouched.
"""

from __future__ import annotations

import json
import re
from pathlib import Path


class RebrandError(Exception):
    """An anchor was not found or a file is not in the expected state."""


class Context:
    def __init__(self, root: Path, check: bool, verbose: bool = False):
        self.root = Path(root)
        self.check = check
        self.verbose = verbose
        ids_path = self.root / "tools" / "branding" / "ids.json"
        self.ids = json.loads(ids_path.read_text(encoding="utf-8"))
        self.changes: list[tuple[str, str]] = []
        self.lane = "shared"
        self._text: dict[str, str] = {}
        self._bytes: dict[str, bytes] = {}
        self._dirty: set[str] = set()

    # -- low level -----------------------------------------------------
    def path(self, rel: str) -> Path:
        return self.root / rel

    def exists(self, rel: str) -> bool:
        return rel in self._bytes or rel in self._text or self.path(rel).exists()

    def read_bytes(self, rel: str) -> bytes | None:
        if rel in self._bytes:
            return self._bytes[rel]
        if rel in self._text:
            return self._text[rel].encode("utf-8")
        p = self.path(rel)
        if not p.exists():
            return None
        return p.read_bytes()

    def read(self, rel: str) -> str:
        if rel in self._text:
            return self._text[rel]
        data = self.read_bytes(rel)
        if data is None:
            raise RebrandError(f"{rel}: file not found")
        text = data.decode("utf-8")
        self._text[rel] = text
        return text

    def _record(self, rel: str, what: str) -> None:
        self.changes.append((rel, f"[{self.lane}] {what}"))
        if self.verbose:
            print(f"  {'would change' if self.check else 'changed'} {rel}: {what}")

    def _set_text(self, rel: str, text: str, what: str) -> None:
        self._text[rel] = text
        self._bytes.pop(rel, None)
        self._dirty.add(rel)
        self._record(rel, what)

    # -- public helpers --------------------------------------------------
    def replace(self, rel: str, old: str, new: str, count: int | None = 1, what: str | None = None) -> bool:
        text = self.read(rel)
        if new and new in text:
            return False
        n = text.count(old)
        if n == 0 and not new:
            return False  # deletion already applied
        if n == 0:
            raise RebrandError(
                f"{rel}: anchor not found and replacement not present (upstream changed?):\n  old: {old[:160]!r}"
            )
        if count is not None and n != count:
            raise RebrandError(f"{rel}: anchor found {n} times, expected {count}:\n  old: {old[:160]!r}")
        self._set_text(rel, text.replace(old, new), what or f"replace {old[:60]!r}")
        return True

    def regex_sub(self, rel: str, pattern: str, repl, expect: int | None = 1, flags: int = 0, what: str | None = None) -> bool:
        text = self.read(rel)
        new_text, n = re.subn(pattern, repl, text, flags=flags)
        if n == 0 or new_text == text:
            return False
        if expect is not None and n != expect:
            raise RebrandError(f"{rel}: pattern matched {n} times, expected {expect}: {pattern!r}")
        self._set_text(rel, new_text, what or f"regex {pattern[:60]!r} x{n}")
        return True

    def write(self, rel: str, content, what: str | None = None) -> bool:
        data = content.encode("utf-8") if isinstance(content, str) else bytes(content)
        if self.read_bytes(rel) == data:
            return False
        self._bytes[rel] = data
        self._text.pop(rel, None)
        self._dirty.add(rel)
        self._record(rel, what or "write")
        return True

    def copy(self, src: str, dst: str, what: str | None = None) -> bool:
        data = self.read_bytes(src)
        if data is None:
            raise RebrandError(f"copy: source {src} not found")
        return self.write(dst, data, what or f"copy from {src}")

    def require(self, rel: str, text: str) -> None:
        if text not in self.read(rel):
            raise RebrandError(f"{rel}: required text missing: {text[:160]!r}")

    # -- flush -------------------------------------------------------------
    def flush(self) -> None:
        if self.check:
            return
        for rel in sorted(self._dirty):
            p = self.path(rel)
            p.parent.mkdir(parents=True, exist_ok=True)
            if rel in self._bytes:
                p.write_bytes(self._bytes[rel])
            else:
                p.write_bytes(self._text[rel].encode("utf-8"))
        self._dirty.clear()
