#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
"""The person's part of Login Flow v2 for the device calendar and contacts sync test (plans/noagenda-desktop-plan.md, 4.6).

NoDriveDeviceSyncServerTest writes the sign-in address of each flow to <out>/login-url-A.txt and login-url-B.txt and then
polls the throwaway server. This script waits for each file, opens the address in Chromium (Playwright for Python, CI
only, never shipped), signs in as the throwaway user and clicks "Grant access". It never prints or stores a password;
the screenshots of the pages are kept in <out>/grant-shots.

It runs only in GitHub Actions, against the throwaway server on 127.0.0.1. Environment: GRANT_USER, GRANT_PASSWORD,
GRANT_OUT (folder), GRANT_SERVER (http://127.0.0.1:8080). Exit code 0 when both grants were made.
"""
import os
import re
import sys
import time

USER = os.environ["GRANT_USER"]
PASSWORD = os.environ["GRANT_PASSWORD"]
OUT = os.environ["GRANT_OUT"]
SERVER = os.environ["GRANT_SERVER"].rstrip("/")
SHOTS = os.path.join(OUT, "grant-shots")
os.makedirs(SHOTS, exist_ok=True)


def wait_for_file(path, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if os.path.exists(path) and os.path.getsize(path) > 0:
            with open(path, "r", encoding="utf-8") as f:
                return f.read().strip()
        time.sleep(1)
    return None


def grant(browser, tag, url):
    """Opens the sign-in address and walks the pages until the server says the account is connected."""
    from playwright.sync_api import TimeoutError as PlaywrightTimeout

    # Only the throwaway server: refuse any other host.
    if not url.startswith(SERVER + "/"):
        print("grant %s: the address is not on the throwaway server" % tag)
        return False
    context = browser.new_context(viewport={"width": 1200, "height": 800}, locale="en-GB")
    page = context.new_page()
    page.set_default_timeout(30000)
    page.goto(url, wait_until="domcontentloaded")
    done = False
    for step in range(1, 16):
        page.wait_for_timeout(1000)
        try:
            body = page.inner_text("body", timeout=5000)
        except Exception:
            body = ""
        page.screenshot(path=os.path.join(SHOTS, "%s-step%02d.png" % (tag, step)))
        low = body.lower()
        if "account connected" in low or "you can close this window" in low or "close this window" in low:
            done = True
            break
        try:
            user_field = page.locator('input[name="user"]')
            if user_field.count() and user_field.first.is_visible():
                user_field.first.fill(USER)
                page.fill('input[name="password"]', PASSWORD)
                page.locator('button[type="submit"]').first.click()
                continue
            grant_button = page.get_by_role("button", name=re.compile(r"^\s*grant access\s*$", re.I))
            if grant_button.count() and grant_button.first.is_visible():
                grant_button.first.click()
                continue
            grant_link = page.get_by_role("link", name=re.compile(r"^\s*grant access\s*$", re.I))
            if grant_link.count() and grant_link.first.is_visible():
                grant_link.first.click()
                continue
            log_in = page.get_by_role("button", name=re.compile(r"^\s*log in\s*$", re.I))
            if log_in.count() and log_in.first.is_visible():
                log_in.first.click()
                continue
            log_in_link = page.get_by_role("link", name=re.compile(r"^\s*log in\s*$", re.I))
            if log_in_link.count() and log_in_link.first.is_visible():
                log_in_link.first.click()
                continue
        except PlaywrightTimeout:
            pass
        except Exception as e:  # a page changed while we looked at it: look again
            print("grant %s: step %d: %s" % (tag, step, type(e).__name__))
    print("grant %s: %s after %d step(s)" % (tag, "connected" if done else "NOT connected", step))
    context.close()
    return done


def main():
    from playwright.sync_api import sync_playwright

    ok = True
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for tag in ("A", "B"):
            url = wait_for_file(os.path.join(OUT, "login-url-%s.txt" % tag), 600)
            if not url:
                print("grant %s: the test did not write a sign-in address" % tag)
                ok = False
                break
            ok = grant(browser, tag, url) and ok
        browser.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
