#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
"""Network proof for the Linux smoke test: no Nextcloud GmbH host is contacted.

Inputs (all optional, any number of each):
  --dns FILE      text output of "tcpdump -nn -r capture.pcap" (port 53, whole runner)
  --strace FILE   "strace -f -e trace=connect" output of one app run
  --log FILE      app log (--logdebug --logfile)
  --report FILE   Markdown report to write

Exit 1 when a DNS query, or a URL host in an app log, names a forbidden host:
nextcloud.com, nextcloud.org or any *.nextcloud.* name (spec section 3 and 6).
The WebDAV namespace URIs http://nextcloud.org/ns and http://owncloud.org/ns
are XML names, not hosts that are contacted, so they are reported but allowed.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

FORBIDDEN = re.compile(r"(^|\.)nextcloud(\.|$)", re.I)
QUERY_RE = re.compile(r"\s(\d+)\+?\s+(?:\[[^\]]*\]\s+)?(A|AAAA|HTTPS|SVCB|CNAME|PTR|TXT|SRV|MX|NS|SOA|ANY)\?\s+(\S+?)\.?\s+\(")
ANSWER_RE = re.compile(r"\s(\d+)\*?-?\|?\$?\s+(?:q: \S+ \S+\.?\s+)?\d+/\d+/\d+\s+(.*?)\s+\(\d+\)\s*$")
ADDR_RE = re.compile(r"\b(?:A|AAAA)\s+([0-9a-fA-F:.]+)")
CONNECT_RE = re.compile(
    r"connect\(\d+, \{sa_family=(AF_INET6?), sin6?_port=htons\((\d+)\),.*?(?:inet_addr\(\"([^\"]+)\"\)|inet_pton\(AF_INET6, \"([^\"]+)\")"
)
URL_HOST_RE = re.compile(r"\bhttps?://([A-Za-z0-9.-]+)(:\d+)?(/[^\s\"'<>)]*)?")
NAMESPACE_OK = re.compile(r"^https?://(next|own)cloud\.org/ns\b", re.I)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dns", action="append", default=[])
    ap.add_argument("--strace", action="append", default=[])
    ap.add_argument("--log", action="append", default=[])
    ap.add_argument("--report")
    a = ap.parse_args()

    names: Counter[str] = Counter()
    id_name: dict[str, str] = {}
    name_ips: dict[str, set[str]] = defaultdict(set)
    for f in a.dns:
        for line in Path(f).read_text(errors="replace").splitlines():
            m = QUERY_RE.search(line)
            if m:
                qid, _qtype, name = m.groups()
                name = name.rstrip(".").lower()
                names[name] += 1
                id_name[qid] = name
                continue
            m = ANSWER_RE.search(line)
            if m and m.group(1) in id_name:
                for ip in ADDR_RE.findall(m.group(2)):
                    name_ips[id_name[m.group(1)]].add(ip)
    ip_names: dict[str, set[str]] = defaultdict(set)
    for n, ips in name_ips.items():
        for ip in ips:
            ip_names[ip].add(n)

    connects: Counter[tuple[str, str, str]] = Counter()
    for f in a.strace:
        run = Path(f).stem
        for line in Path(f).read_text(errors="replace").splitlines():
            m = CONNECT_RE.search(line)
            if m:
                fam, port, ip4, ip6 = m.groups()
                connects[(run, ip4 or ip6, port)] += 1

    url_hosts: Counter[str] = Counter()
    bad_urls: list[str] = []
    for f in a.log:
        for line in Path(f).read_text(errors="replace").splitlines():
            for m in URL_HOST_RE.finditer(line):
                url = m.group(0)
                host = m.group(1).lower()
                url_hosts[host] += 1
                if FORBIDDEN.search(host) and not NAMESPACE_OK.match(url):
                    bad_urls.append(f"{Path(f).name}: {url[:120]}")

    bad_names = sorted(n for n in names if FORBIDDEN.search(n))
    ok = not bad_names and not bad_urls

    lines = ["# Network check (Linux smoke test)", ""]
    lines.append(f"Result: **{'PASS' if ok else 'FAIL'}**. Forbidden host pattern: `{FORBIDDEN.pattern}`.")
    lines.append("")
    lines.append(f"## DNS queries seen on the runner during the test ({len(names)} names)")
    lines.append("")
    lines.append("The capture covers the whole runner (port 53 on every interface), so it also shows the runner's own traffic.")
    lines.append("")
    lines.append("| Name | Queries | Answers |")
    lines.append("|---|---|---|")
    for n, c in sorted(names.items()):
        lines.append(f"| `{n}` | {c} | {', '.join(sorted(name_ips.get(n, [])))[:120]} |")
    lines.append("")
    lines.append("## TCP/UDP connect() calls made by the app (strace)")
    lines.append("")
    if connects:
        lines.append("| App run | Address | Port | Name (from DNS) | Calls |")
        lines.append("|---|---|---|---|---|")
        for (run, ip, port), c in sorted(connects.items()):
            lines.append(f"| {run} | `{ip}` | {port} | {', '.join(sorted(ip_names.get(ip, []))) or '-'} | {c} |")
    else:
        lines.append("None.")
    lines.append("")
    lines.append("## URL hosts mentioned in the app logs")
    lines.append("")
    for h, c in sorted(url_hosts.items()):
        lines.append(f"- `{h}` ({c})")
    if not url_hosts:
        lines.append("None.")
    if bad_names or bad_urls:
        lines.append("")
        lines.append("## FORBIDDEN")
        for n in bad_names:
            lines.append(f"- DNS query: `{n}`")
        for u in bad_urls:
            lines.append(f"- log URL: `{u}`")
    text = "\n".join(lines) + "\n"
    if a.report:
        Path(a.report).write_text(text, encoding="utf-8")
    print(text)
    print(f"netcheck: {'PASS' if ok else 'FAIL'} ({len(names)} DNS names, {len(connects)} app connect targets, "
          f"{len(bad_names)} forbidden DNS names, {len(bad_urls)} forbidden log URLs)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
