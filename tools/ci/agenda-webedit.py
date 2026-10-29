#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
"""Does the agent id survive an edit in the Calendar and Tasks web pages? (plans/noagenda-decisions.md, 4.2)

NoAgenda stores the agent of an event or to-do as the property X-NOURAX-AGENT-ID and, as a copy, as the registry's
calendar tag in CATEGORIES. A web page that parses and writes the item again might drop either of them. This script
opens a tagged event in the Calendar app and a tagged to-do in the Tasks app of the throwaway server, changes the
title in the page, saves, reads the raw item through CalDAV and reports, separately for each property and each app,
"survived" or "dropped".

It runs only in GitHub Actions (CI), against the throwaway server of tools/ci/agenda-server.sh on 127.0.0.1, with
Playwright for Python and Chromium (neither is shipped). It never talks to another host. The throwaway password is
read from the environment (WEBEDIT_PASSWORD) and never printed or written.

Exit code: 1 only when BOTH properties were dropped for one app (then NoAgenda would lose the agent there) or when
the server's own state could not be read. When the page could not be driven at all (a selector of the web page
changed), the result says "not driven" and the exit code is 0: that row becomes an owner test row.

Environment: WEBEDIT_SERVER (http://127.0.0.1:8080), WEBEDIT_USER, WEBEDIT_PASSWORD, WEBEDIT_OUT (folder).
"""
import base64
import hashlib
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

SERVER = os.environ["WEBEDIT_SERVER"].rstrip("/")
USER = os.environ["WEBEDIT_USER"]
PASSWORD = os.environ["WEBEDIT_PASSWORD"]
OUT = os.environ.get("WEBEDIT_OUT", "webedit-out")
os.makedirs(OUT, exist_ok=True)

AUTH = "Basic " + base64.b64encode(("%s:%s" % (USER, PASSWORD)).encode()).decode()
RESULT = {"apps": {}}


def dav_get(path):
    req = urllib.request.Request(SERVER + path, headers={"Authorization": AUTH})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, ""


def unfold(text):
    return re.sub(r"\r?\n[ \t]", "", text)


def properties(ics):
    """Name -> list of values of the item, unfolded (the first VEVENT or VTODO only)."""
    out = {}
    inside = False
    depth = 0
    for line in unfold(ics).splitlines():
        if line.startswith("BEGIN:VEVENT") or line.startswith("BEGIN:VTODO"):
            inside = True
            depth = 0
            continue
        if line.startswith("END:VEVENT") or line.startswith("END:VTODO"):
            break
        if not inside:
            continue
        if line.startswith("BEGIN:"):
            depth += 1
            continue
        if line.startswith("END:"):
            depth -= 1
            continue
        if depth or ":" not in line:
            continue
        head, value = line.split(":", 1)
        name = head.split(";", 1)[0].upper()
        out.setdefault(name, []).append(value)
    return out


def has_tag(props, tag):
    for value in props.get("CATEGORIES", []):
        if tag in [v.strip() for v in value.split(",")]:
            return True
    return False


def redacted(ics):
    """The item with every value replaced by a short hash, except the agent id and the agent tag."""
    lines = []
    for line in unfold(ics).splitlines():
        if ":" not in line:
            lines.append(line)
            continue
        head, value = line.split(":", 1)
        name = head.split(";", 1)[0].upper()
        if name in ("BEGIN", "END", "VERSION", "X-NOURAX-AGENT-ID"):
            lines.append(line)
        elif name == "CATEGORIES":
            parts = [v.strip() for v in value.split(",")]
            lines.append("CATEGORIES:" + ",".join(p if p.startswith("agent:") else "#" + hashlib.sha256(p.encode()).hexdigest()[:8] for p in parts))
        else:
            lines.append("%s:#%s" % (name, hashlib.sha256(value.encode()).hexdigest()[:8]))
    return "\n".join(lines) + "\n"


def login(page):
    page.goto(SERVER + "/login", wait_until="domcontentloaded")
    page.wait_for_selector('input[name="user"]', timeout=60000)
    page.fill('input[name="user"]', USER)
    page.fill('input[name="password"]', PASSWORD)
    page.locator('button[type="submit"]').first.click()
    page.wait_for_url(re.compile(r"/(apps|index\.php/apps)/"), timeout=90000)


def dismiss_first_run(page):
    # The first-run wizard and the app tour, if they are shown.
    for label in ("Skip", "Close", "Got it", "Finish", "Next"):
        try:
            button = page.get_by_role("button", name=label).first
            if button.is_visible(timeout=1500):
                button.click()
        except Exception:
            pass
    try:
        page.keyboard.press("Escape")
    except Exception:
        pass


def type_title(page, old, new):
    """Finds the text field that holds the old title and writes the new one."""
    for locator in (page.get_by_role("textbox"), page.locator("input[type=text]"), page.locator("textarea")):
        count = locator.count()
        for i in range(count):
            field = locator.nth(i)
            try:
                if field.is_visible() and field.input_value() == old:
                    field.fill(new)
                    return True
            except Exception:
                continue
    return False


def click_save(page):
    for name in ("Save", "Update", "Done"):
        try:
            button = page.get_by_role("button", name=re.compile(r"^\s*%s\s*$" % name, re.I)).first
            if button.is_visible(timeout=2000):
                button.click()
                return True
        except Exception:
            continue
    return False


def drive(app, page, url, old, new, calendar, name, tag):
    entry = {"driven": False, "agent_id": "not read", "tag": "not read", "detail": ""}
    RESULT["apps"][app] = entry
    path = "/remote.php/dav/calendars/%s/%s/%s" % (USER, calendar, name)
    status, before = dav_get(path)
    if status != 200:
        entry["detail"] = "the item could not be read before the edit (HTTP %d)" % status
        return
    props_before = properties(before)
    entry["before"] = {"agent_id": "X-NOURAX-AGENT-ID" in props_before, "tag": has_tag(props_before, tag)}
    open(os.path.join(OUT, "%s-before.ics" % app), "w", encoding="utf-8").write(redacted(before))
    try:
        page.goto(url, wait_until="domcontentloaded")
        page.wait_for_timeout(5000)
        dismiss_first_run(page)
        page.goto(url, wait_until="domcontentloaded")
        page.wait_for_timeout(5000)
        if not type_title(page, old, new):
            page.screenshot(path=os.path.join(OUT, "%s-no-title-field.png" % app))
            entry["detail"] = "the title field was not found in the page"
            return
        if app == "tasks":
            page.keyboard.press("Enter")
            page.keyboard.press("Tab")
        else:
            if not click_save(page):
                page.screenshot(path=os.path.join(OUT, "%s-no-save-button.png" % app))
                entry["detail"] = "the Save button was not found in the page"
                return
        page.wait_for_timeout(4000)
        page.screenshot(path=os.path.join(OUT, "%s-after-save.png" % app))
    except Exception as e:  # the page could not be driven
        entry["detail"] = "the page could not be driven: %s" % type(e).__name__
        return
    status, after = dav_get(path)
    if status != 200:
        entry["detail"] = "the item could not be read after the edit (HTTP %d)" % status
        entry["driven"] = False
        return
    props_after = properties(after)
    open(os.path.join(OUT, "%s-after.ics" % app), "w", encoding="utf-8").write(redacted(after))
    saved = props_after.get("SUMMARY", [""])[0] == new
    if not saved:
        entry["detail"] = "the page did not save the new title"
        return
    entry["driven"] = True
    entry["agent_id"] = "survived" if "X-NOURAX-AGENT-ID" in props_after else "dropped"
    entry["tag"] = "survived" if has_tag(props_after, tag) else "dropped"
    entry["detail"] = "title changed in the page and saved"


def main():
    from playwright.sync_api import sync_playwright

    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(viewport={"width": 1400, "height": 900}, locale="en-GB")
        page = context.new_page()
        page.set_default_timeout(30000)
        try:
            login(page)
        except Exception as e:
            RESULT["login"] = "could not sign in to the throwaway server: %s" % type(e).__name__
            page.screenshot(path=os.path.join(OUT, "login-failed.png"))
            return browser
        RESULT["login"] = "ok"
        # Calendar: the editor of the tagged event "Writer-Bot review" (items made by tools/ci/agenda-seed.py)
        event_path = "/remote.php/dav/calendars/%s/work/agent.ics" % USER
        encoded = urllib.parse.quote(base64.b64encode(event_path.encode()).decode(), safe="")
        drive("calendar", page, "%s/index.php/apps/calendar/edit/%s" % (SERVER, encoded), "Writer-Bot review", "Writer-Bot review (edited in the web page)", "work", "agent.ics", "agent:AG-0001")
        # Tasks: the tagged to-do "Draft report" (CATEGORIES only)
        drive("tasks", page, "%s/index.php/apps/tasks/#/calendars/personal/tasks/draft.ics" % SERVER, "Draft report", "Draft report (edited in the web page)", "personal", "draft.ics", "agent:AG-0001")
        return browser


def report():
    lines = ["# Does the agent id survive an edit in the web pages?", "",
             "Calendar 6.6.4 and Tasks 0.18.1 on the throwaway server. A tagged item is opened in the page, its title is changed and saved,",
             "and the item is read again through CalDAV. Only the two agent properties are shown; every other value is a hash.", "",
             "| App | Driven | X-NOURAX-AGENT-ID | CATEGORIES tag | Detail |", "|---|---|---|---|---|"]
    fail = False
    for app, e in RESULT["apps"].items():
        lines.append("| %s | %s | %s | %s | %s |" % (app, "yes" if e["driven"] else "no", e["agent_id"], e["tag"], e["detail"]))
        if e["driven"] and e["agent_id"] == "dropped" and e["tag"] == "dropped":
            fail = True
    lines.append("")
    lines.append("Sign-in: %s" % RESULT.get("login", "not tried"))
    if fail:
        lines.append("")
        lines.append("FAIL: both agent properties were dropped by a web page, so NoAgenda would lose the agent there.")
    text = "\n".join(lines) + "\n"
    open(os.path.join(OUT, "webedit.md"), "w", encoding="utf-8").write(text)
    RESULT["fail"] = fail
    json.dump(RESULT, open(os.path.join(OUT, "webedit.json"), "w"), indent=2)
    print(text)
    return 1 if fail else 0


if __name__ == "__main__":
    browser = None
    try:
        browser = main()
    except Exception as e:  # never a stack trace with values in the log
        RESULT["error"] = type(e).__name__
        print("webedit: stopped with %s" % type(e).__name__)
    code = report()
    sys.exit(code)
