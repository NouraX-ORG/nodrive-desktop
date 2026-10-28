#!/usr/bin/env python3
"""Checks one problem report zip made by the desktop app (plans/round3-contract.md, section B).

  check-report-zip.py REPORT.zip (--server-host HOST | --empty-host) --forbid FILE
                      [--max-bytes 5000000] [--kind KIND] [--expect-text TEXT ...]

FILE has one forbidden value per line (planted fake secrets and the throwaway passwords).
It is read from a path outside the artifact folder; no value is printed, only its line number.
Exit code 0 = the zip is fine.
"""

import argparse
import json
import re
import sys
import zipfile

FIELDS = {
    "schema": int, "kind": str, "app": str, "app_version": str, "build": str, "platform": str,
    "os_version": str, "device_model": str, "locale": str, "created_utc": str, "server_host": str, "user_note": str,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("zip")
    ap.add_argument("--server-host")
    ap.add_argument("--empty-host", action="store_true", help="server_host must be empty (nobody was signed in)")
    ap.add_argument("--kind")
    ap.add_argument("--expect-text", action="append", default=[])
    ap.add_argument("--forbid", required=True)
    ap.add_argument("--max-bytes", type=int, default=5_000_000)
    args = ap.parse_args()
    if not args.empty_host and not args.server_host:
        ap.error("give --server-host or --empty-host")

    problems = []
    import os
    size = os.path.getsize(args.zip)
    if size > args.max_bytes:
        problems.append(f"zip is {size} bytes, more than {args.max_bytes}")
    name = os.path.basename(args.zip)
    if not re.match(r"^\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}_(windows|macos|linux)_[0-9A-Za-z.\-]+(_\d)?\.zip$", name):
        problems.append(f"file name {name!r} does not match the contract")

    forbidden = [l.rstrip("\n") for l in open(args.forbid, encoding="utf-8") if l.strip()]
    with zipfile.ZipFile(args.zip) as z:
        names = z.namelist()
        if "report.json" not in names:
            problems.append("report.json is missing")
        else:
            report = json.loads(z.read("report.json"))
            for key, kind in FIELDS.items():
                if key not in report:
                    problems.append(f"report.json has no {key}")
                elif not isinstance(report[key], kind):
                    problems.append(f"report.json {key} has the wrong type")
            if set(report) != set(FIELDS):
                problems.append(f"report.json has other fields: {sorted(set(report) - set(FIELDS))}")
            if report.get("schema") != 1:
                problems.append("schema is not 1")
            if report.get("app") != "NoDrive Desktop":
                problems.append("app is not NoDrive Desktop")
            if report.get("platform") != "linux":
                problems.append("platform is not linux")
            want_host = "" if args.empty_host else args.server_host
            if report.get("server_host") != want_host:
                problems.append(f"server_host is {report.get('server_host')!r}, expected {want_host!r}")
            if args.kind and report.get("kind") != args.kind:
                problems.append(f"kind is {report.get('kind')!r}, expected {args.kind!r}")
            if not re.match(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$", str(report.get("created_utc", ""))):
                problems.append("created_utc is not ISO 8601 UTC")
        if not any(n.startswith("logs/") for n in names):
            problems.append("no logs/ entry")
        alltext = b""
        for n in names:
            data = z.read(n)
            alltext += data
            for i, value in enumerate(forbidden, 1):
                if value.encode("utf-8") in data:
                    problems.append(f"{n} contains forbidden value #{i}")
        if b"<email>" not in alltext:
            problems.append("no <email> marker: the planted address was not replaced")
        # The scrubbed logs keep non-secret facts.
        for text in args.expect_text:
            if text.encode("utf-8") not in alltext:
                problems.append(f"the text {text!r} is missing from the report")

    if problems:
        for p in problems:
            print("FAIL:", p)
        return 1
    print(f"OK: {name} ({size} bytes, {len(names)} entries)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
