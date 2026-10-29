# Testing NoDrive (Windows, macOS, Linux)

Two kinds of tests:

- **CI** (GitHub Actions): everything that works without signing in. Builds,
  unit tests, install, start, screenshots in four variants, network capture and
  branding sweeps of the packages. CI cannot sign in: the Drive's login pages only
  open from the owner's home network (Cloudflare rule).
- **Owner tests**: sign-in and everything after it, at home, with the owner's
  normal Nourax account ("Log in with Nourax"), not the emergency admin account.

Full evidence per platform: [testing-linux.md](testing-linux.md),
[testing-windows.md](testing-windows.md), [testing-macos.md](testing-macos.md).
How the builds are made: [build.md](build.md).

## 1. Results

CI result of the problem-reports change at commit `14ebedb5c4` (branch
`feature/problem-reports`, all jobs green): Linux run [40736612257](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40736612257),
Windows run [40736612246](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40736612246), macOS run [40736612254](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40736612254). The merge into `main`
adds only documents on top, and `main` runs the same tests again on every push.
The packaging, install and branding rows below were first measured at `bd3bd8a412`
(Linux run [40189812444](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40189812444),
Windows run [40189812456](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40189812456),
macOS run [40189812446](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40189812446));
the same smoke checks were green again at `14ebedb5c4`.

### 1.1 Before sign-in (CI)

| Feature | Linux | Windows | macOS |
|---|---|---|---|
| Installs | n/a (the AppImage runs as it is) | PASS: silent MSI install, `C:\Program Files\NoDrive` | PASS: from the `.dmg` to `/Applications`, signature still valid |
| Starts | PASS: 8 starts | PASS: 9 starts | PASS: 8 starts |
| Shows NoDrive's name | PASS: title "NoDrive", "Log in to NoDrive", `--version`, `.desktop` file | PASS: title, tray "Exit NoDrive", Start menu, Apps & features, installer, version resources of 228 files | PASS: menu bar "NoDrive", Info.plists, `--version` |
| Shows NoDrive's icon | PASS: tray, title bar; packaged icon = `make-icons.py` output (SHA-256) | PASS: title bar, taskbar, tray, installer; exe icon = `make-icons.py` output (0 pixels differ) | PASS: menu bar, Dock; `NoDrive.icns` |
| Shows NoDrive's colours | PASS: "Log in" button `#2563EB` (pixel check); Settings switch `#2563EB` (unit test) | PASS: "Log in" button `#2563EB` | PASS: "Log in" button `#2563EB` |
| Server fixed, no server-entry screen | PASS: no address field, no "Sign up" / "Self-host"; unit and provisioning tests | PASS: same, also by UI Automation text | PASS: same; unit test |
| Sign-in opens the browser at drive.nourax.org | PASS: `https://drive.nourax.org/login/v2/flow/<token>` in 4 variants | PASS: same | PASS: same |
| No Nextcloud service contacted | PASS: DNS and `connect()` log: only drive.nourax.org | PASS: DNS client log, TCP connections: only drive.nourax.org | PASS: DNS (`tcpdump`) and `lsof`: only drive.nourax.org |
| Dark mode | PASS: wizard background `#202124` | PASS: same | PASS: same |
| Second language (German) | PASS | PASS | PASS |
| Right-to-left (Arabic) | PASS, note: mirrored; some new wizard texts are not translated upstream yet | PASS, same note | PASS, same note |
| Ids, folders, registry | PASS: `.desktop` id, MIME types | PASS: registry `Nourax\NoDrive`, `%APPDATA%\NoDrive`, new CLSIDs, no Nextcloud keys | PASS: bundle ids, app group, sandbox config folder |
| No updater | PASS (not built) | PASS (not built) | PASS: no Sparkle keys or framework |
| Licence files in the package | PASS: `usr/share/doc/nodrive/` | PASS: `C:\Program Files\NoDrive\licenses\` | PASS: `NoDrive.app/Contents/Resources/licenses/` |
| Uninstall | n/a | PASS: silent, nothing left except the user's settings | n/a (drag to Trash) |
| Unit tests (`ctest`) | PASS: 104 of 104 | PASS: 101 of 101 | PASS: 98 of 98 |
| Branding sweep of the package | PASS: 61 hits, 0 unclassified | PASS: 61 hits, 0 unclassified | PASS: 61 hits, 0 unclassified |
| Finder extension | n/a | n/a | INFO: registered (`pluginkit`); badges need a sync folder (owner) |
| Files on demand | not tested | owner test | not built (needs real signing) |

Skipped unit test on all three: `UpdateChannelTest` (branded builds have no update
channels and no updater; `tools/branding/ctest-skip-<os>.txt`).

The 61 hits in each package sweep are kept on purpose: licence files and the
Legal Notice copyright line in the compiled translations (legal; the dialog cannot
be opened) and one stale translation that is never looked up (dead code). See
[branding-sweep.md](branding-sweep.md).

### 1.1a Problem reports (CI, [problem-reports.md](problem-reports.md))

All rows at commit `14ebedb5c4`: Linux run [40736612257](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40736612257), Windows run
[40736612246](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40736612246), macOS run [40736612254](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40736612254). CI never contacts
`drive.nourax.org`. The upload tests use an in-process fake server; the Linux job
`report-server` uses a throwaway Nextcloud 35.0.2 in Docker with throwaway accounts.

| Feature | Linux | Windows | macOS |
|---|---|---|---|
| Unit tests: scrubber (a fixture per rule, 200 random secrets of each kind), report contents and 5 MB cap, run marker table, real crashes of a helper process, upload to a fake server (MKCOL, `If-None-Match`, 412 and `_2` name, read-only share, one share attempt per batch, waiting queue, held crash report), Settings texts | PASS: 5 tests, within 104 of 104 | PASS: 5 tests, within 101 of 101 | PASS: 5 tests, within 98 of 98 |
| Settings > Advanced: switch off by default and saved, "Report a problem" after "Create Debug Archive", exact texts, no "Nextcloud" text, light and dark screenshots with pixel checks | PASS (screenshots `linux/problem-reports/`) | PASS (screenshots `windows/problem-reports/`) | PASS in the unit test; its screenshots were not collected by CI (to-do) |
| Real app after a killed run: the question "NoDrive closed unexpectedly. Send a problem report?" appears, Escape (Not now) closes it, no second question, `run.marker` is `running` while the app runs and `clean` after a normal end | PASS, light and dark (`results.tsv`: `prompt-`, `not-now-`, `ask-once-`, `marker-running-`, `sigterm-clean-`) | PASS, light and dark; the question's UI Automation texts name no Nextcloud | PASS, light and dark |
| Real app: "Send" builds a zip in `problem-reports/pending/` with the scrubbed files (secrets planted in the logs are gone) | PASS: 10 entries, 8.9 KB | PASS: 12 entries, 10.5 KB | PASS: 11 entries, 11.3 KB |
| Real Nextcloud 35.0.2 (throwaway): the folder "Problem reports" is created, one zip each, an existing name is never replaced (`_2`), no share while `nourax-support` does not exist, then one read-only share (`permissions=1`), Support can list but not write, no DELETE / MOVE / COPY, a report made offline is sent later, no secret in the artifact | PASS: all 4 phases (job `report-server`, artifact `nodrive-linux-report-server`) | not run (no Docker on the runner) | not run (no Docker on the runner) |
| Branding gate stays green and `rebrand.py --check` is clean (the marked lines of `tools/rebrand.d/problemreports.py`) | PASS: lane linux, 653 hits, 0 unclassified | PASS | PASS |

### 1.1b NoAgenda core (CI, [noagenda.md](noagenda.md))

Commit `46f8a71e0b` (branch `feature/noagenda-core`, approved by the independent review 2; the code is the same as in `654db7e45e`, the last commit is docs only): Linux run [40809977468](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40809977468),
macOS run [40809977074](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40809977074), Windows run [40809977148](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40809977148). All three are green. CI never contacts `drive.nourax.org`.
The unit tests use canned answers; the Linux job `agenda-server` uses a throwaway Nextcloud 35.0.2 in Docker with the
pinned apps calendar 6.6.4, tasks 0.18.1, Deck 1.19.1 and nc_gantt 2.4.1 and throwaway accounts.

| Feature | Linux | Windows | macOS |
|---|---|---|---|
| Unit tests `NoDriveAgendaRegistry`, `...ICal`, `...CalDav`, `...Gantt`, `...Deck`, `...Rules`, `...Cache`, `...Service`: registry parsing, iCalendar and CalDAV reading (a calendar that keeps failing does not grow), NoGantt contract and agent recognition, Deck (a Hidden board stays on Sources), the defaults, event rules, agents, integrity flags (the person is told about an agent without its person; no user id and no other organisation's agent name in a sentence), dedupe, team problems, 401/403 from NoGantt or NoBoard becomes "Open in ...", privacy between organisations, offline cache, back-off, settings and cache kept on quit and deleted only when the account is removed | PASS (`100% tests passed, 0 tests failed out of 114`) | PASS (`100% tests passed, 0 tests failed out of 111`) | PASS (`100% tests passed out of 108`) |
| `NoDriveAgendaUiTest`: the window in light and dark (Day, Week, Month, My team, Sources, the personal-calendars question, detail, not linked, offline, registry missing), chips and switches by mouse clicks, pixel checks (background, selected chip `#2563EB` / `#3B82F6`), no text with the server software's name | PASS, screenshots `nodrive-screenshots/noagenda/` in the artifact `nodrive-linux-test-results` (20 files) | PASS, screenshots in the ctest artifact (`nodrive-screenshots/noagenda/`) | PASS, screenshots in the ctest artifact (`nodrive-screenshots/noagenda/`) |
| Real Nextcloud 35.0.2 (throwaway) with the four pinned apps (SHA-256 checked): auth proof (app password, Basic, `OCS-APIRequest`), contract (live key sets against the fixtures), Ali (staff), Mara (manager), Sara (invalid registry, viewer), Olaf (other organisation), version guard | PASS: 146 checks, 0 failed (job `agenda-server`, artifact `nodrive-linux-agenda-server` with the live screenshots of Ali's day) | not run (no Docker on the runner) | not run (no Docker on the runner) |
| Branding gate stays green and `rebrand.py --check` is clean (the marked lines of `tools/rebrand.d/noagenda.py`) | PASS | PASS | PASS |

### 1.1c NoAgenda editing and conflicts (CI, [noagenda.md](noagenda.md))

Commit `d33c7090fe` (branch `feature/noagenda-edit`, approved by the independent review 2; the numbers below are from this commit): Linux run [40929677778](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40929677778),
macOS run [40929677752](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40929677752), Windows run [40929677748](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40929677748). All three are green. The head `b35465c03c` (these documents and one build fix: `chmod -R a+rX` before `appimagetool`, because a new `continuous` appimagetool began to refuse folders of mode 0700) is green too: Linux [40943442964](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40943442964), macOS [40943443408](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40943443408), Windows [40943442984](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40943442984). CI never contacts `drive.nourax.org`. The unit tests use canned answers; the Linux job
`agenda-server` uses the same throwaway Nextcloud 35.0.2 in Docker with the pinned apps (nc_gantt 2.4.1 SHA-256 `9003904a...cfb9` checked) and throwaway accounts.

| Feature | Linux | Windows | macOS |
|---|---|---|---|
| Unit tests of the edit step: `NoDriveAgendaICalPatch` (only the named properties change; alarms, guests, unknown `X-` lines and time zones stay byte for byte), `...Edit` (verb, path, headers `If-Match` / `If-None-Match` / `OCS-APIRequest` and body of every edit; conflicts, refusals, viewers, repeating items, invitations), `...GanttAgent` ("my agent": other rows unchanged, 4-row limit, work total kept, one agent with two planning people), `...Queue` (order, restart, no parallel sends, folding, conflicts kept until a click, version guard holds queued NoGantt and NoBoard edits, a 401 of an app is that app's refusal, three tries then "Not sent"), `...Conflicts` (clash, overbooked, deadline on a day off, blocked, an agent's capacity and load) | PASS (`100% tests passed, 0 tests failed out of 119`) | PASS (`100% tests passed, 0 tests failed out of 116`) | PASS (`100% tests passed out of 113`) |
| `NoDriveAgendaUiTest`, edit step: the window in light and dark with the clash and workload bar, the agent picker, an agent not in the project, a new event, an edit conflict, waiting changes, the delete question, a repeating item and a viewer (locked with the reason); no text with the server software's name | PASS, screenshots `nodrive-screenshots/noagenda/` in the artifact `nodrive-linux-test-results` | PASS, in the ctest artifact | PASS, in the ctest artifact |
| Real Nextcloud 35.0.2 (throwaway) with the four pinned apps: the phases `edit-calendar` (alarm, unknown property and UID stay; agent on and off; to-do done and not done; a new event and a new to-do; the one delete), `conflict` (the other change wins, "Apply my change again" keeps both), `edit-gantt` (a move with a successor, Undo, % done, status, my agent Self / Writer-Bot / Self with the rows and the total work untouched, refusals, a viewer refused, conflict), `edit-deck` (due date and done keep the other fields, the agent label, a missing label refused or created by who may manage the board, conflict), `offline` (three edits wait, survive a restart, go out in order) | PASS: 30 PASS rows, 0 FAIL (job `agenda-server`, artifact `nodrive-linux-agenda-server`) | not run (no Docker on the runner) | not run (no Docker on the runner) |
| The only DELETE after the seed is the event the edit phase created itself; no MOVE or COPY; the read phases send no DELETE, MOVE or COPY; no throwaway password in the artifact | PASS (`edit-deletes-only-its-own-event`, `no-destructive-requests-in-read-steps`, `no-secret-in-artifact`) | n/a | n/a |
| Does the agent id survive an edit in the web pages (Playwright with Chromium, CI only)? | Calendar page: id and tag survived. Tasks page: the id was dropped, the tag survived (so NoAgenda still finds the agent; `webedit-agent-id-survives` PASS) | n/a | n/a |
| Branding gate stays green and `rebrand.py --check` is clean; nothing in the upstream files changed | PASS | PASS | PASS |

Not verified by CI: that the calendar's trash bin keeps a deleted event (the job reports "not verified"; owner row "Delete an event"), and what the iPhone, Android and
macOS calendar apps do to the agent id (owner row). Known limits and the review's nits are listed in [noagenda.md](noagenda.md), "Open points of the edit step".

### 1.2 After sign-in (owner)

| Feature | Linux | Windows | macOS |
|---|---|---|---|
| Sign in with "Log in with Nourax" | pending owner test | pending owner test | pending owner test |
| File list | pending owner test | pending owner test | pending owner test |
| Upload, download, rename, move, delete | pending owner test | pending owner test | pending owner test |
| Public link with expiry and password | pending owner test | pending owner test | pending owner test |
| Sync folder and a conflict | pending owner test | pending owner test | pending owner test |
| Files on demand (if it loads) | pending owner test | pending owner test | not in this build |
| Explorer sidebar "NoDrive" / Finder badges | n/a | pending owner test | pending owner test |
| Settings (General, Network, Info/About) and Activity show no Nextcloud name, logo or link | pending owner test | pending owner test | pending owner test |
| Sign out | pending owner test | pending owner test | pending owner test |
| Dark mode | pending owner test | pending owner test | pending owner test |
| Second language | pending owner test | pending owner test | pending owner test |
| Next to the real Nextcloud app (optional) | pending owner test | pending owner test | pending owner test |
| Device name in Settings > Security | pending owner test | pending owner test | pending owner test |
| Problem reports ([problem-reports.md](problem-reports.md)): Settings > Advanced shows "Send problem reports to Nourax", switched off | pending owner test | pending owner test | pending owner test |
| Problem reports: "Report a problem" with a note says "Saved in your NoDrive > Problem reports." and the zip is in the folder "Problem reports" on the Drive (web page) | pending owner test | pending owner test | pending owner test |
| Problem reports: logging out (Linux, macOS) or logging off (Windows) while NoDrive runs gives no question at the next start | pending owner test | pending owner test | pending owner test |
| Problem reports: ending NoDrive hard (Task Manager, Activity Monitor or `kill -9`) gives the question at the next start; "Not now" is not asked again | pending owner test | pending owner test | pending owner test |
| Problem reports: with the switch on, ending NoDrive hard sends the report without a question and shows "Problem report sent" | pending owner test | pending owner test | pending owner test |
| Problem reports: dark mode (Settings > Advanced, the dialog, the question) is readable | pending owner test | pending owner test | pending owner test |
| NoAgenda ([noagenda.md](noagenda.md)): the tray shows "NoAgenda" (Windows: left- and right-click menus; macOS: the menu-bar popup; Linux: the tray menu) and it opens the window | pending owner test | pending owner test | pending owner test |
| NoAgenda: the question "Which of your calendars are personal?" appears once, after the first read | pending owner test | pending owner test | pending owner test |
| NoAgenda: Today shows the dentist and the client call from the real calendars | pending owner test | pending owner test | pending owner test |
| NoAgenda: NoGantt tasks show with the right agent or Self | pending owner test | pending owner test | pending owner test |
| NoAgenda: My team shows a late task in a project the owner runs | pending owner test | pending owner test | pending owner test |
| NoAgenda: switching off Wi-Fi shows "showing what was saved at ..." | pending owner test | pending owner test | pending owner test |
| NoAgenda: dark mode is readable | pending owner test | pending owner test | pending owner test |
| NoAgenda editing ([noagenda.md](noagenda.md)): change an event's time in NoAgenda; the Calendar page of the Drive shows the new time and keeps its alarms and guests | pending owner test | pending owner test | pending owner test |
| NoAgenda editing: change an event in the Calendar page, then its title in NoAgenda without refreshing: "changed somewhere else" with "Apply my change again" | pending owner test | pending owner test | pending owner test |
| NoAgenda editing: tick a to-do in NoAgenda; the Tasks page shows it done | pending owner test | pending owner test | pending owner test |
| NoAgenda editing: move a NoGantt task's end date; "Also moved: ..." appears and **Undo** puts it back within 10 seconds | pending owner test | pending owner test | pending owner test |
| NoAgenda editing: choose "my agent" on a NoGantt task and on a card, then Self again | pending owner test | pending owner test | pending owner test |
| NoAgenda editing: Wi-Fi off, change something, Wi-Fi on; "Waiting to send 1 change" goes away and the change is on the Drive | pending owner test | pending owner test | pending owner test |
| NoAgenda editing: a dentist visit and a client call at the same time both show the clash mark; 6 hours of work on a day with 4 free hours shows the workload warning | pending owner test | pending owner test | pending owner test |
| NoAgenda editing: set an agent on an event, then edit that event in the iPhone, Android or macOS calendar app; is the agent still there in NoAgenda? | pending owner test | pending owner test | pending owner test |
| NoAgenda editing: delete an event in NoAgenda (after the question); it is in the Trash bin of the Calendar page and can be restored | pending owner test | pending owner test | pending owner test |

Offline files and automatic photo upload are phone features; they do not apply to
the desktop app.

## 2. Owner tests: how to get and install the builds

Get the build (any platform):

1. Open `https://github.com/NouraX-ORG/nodrive-desktop/actions` while signed in to
   GitHub.
2. On the left, click the workflow: **Windows**, **macOS** or **Linux**.
3. Click the newest run on `main` with a green tick.
4. Scroll to **Artifacts** and download the package artifact from the table below.
   It downloads as a `.zip`; unzip it.

| Platform | Where to test | Artifact | File inside |
|---|---|---|---|
| Windows | this Windows 11 PC | `nodrive-windows-msi` | `NoDrive-35.0.0.<build>-nourax.1-x64.msi` |
| macOS | the owner's Mac (Apple silicon) | `nodrive-macos-dmg` | `NoDrive-35.0.0-nourax.1-<build>-arm64.dmg` |
| Linux | a Linux PC (x86_64), or WSLg on this PC | `nodrive-linux-appimage` | `NoDrive-35.0.0-nourax.1-<build>-x86_64.AppImage` |

### Windows

1. Double-click the `.msi`. The build is not signed, so Windows may show
   "Windows protected your PC": click **More info**, then **Run anyway**.
2. Follow the installer (Next, Install). Allow the change when Windows asks.
3. Start **NoDrive** from the Start menu. The NoDrive window opens with
   "Log in to NoDrive".
4. Click **Log in**. The browser opens `drive.nourax.org`: choose
   **Log in with Nourax** and allow access.
5. To remove it later: *Settings → Apps → Installed apps → NoDrive → Uninstall*.

### macOS

1. Double-click the `.dmg` and drag **NoDrive** onto **Applications**.
2. The app is not notarised, so macOS blocks a normal double-click the first time:
   - in Finder, open Applications, right-click **NoDrive** and choose **Open**,
     then **Open** again;
   - on macOS 15 (Sequoia) or newer that is not enough: double-click NoDrive once,
     close the warning, open *System Settings → Privacy & Security*, scroll down to
     the message about NoDrive and click **Open Anyway**, then confirm.
   - If there is no **Open Anyway** button, or macOS says the app is damaged,
     run this once in Terminal and open the app again:
     `xattr -dr com.apple.quarantine /Applications/NoDrive.app`
3. The NoDrive icon appears in the menu bar and the window "Log in to NoDrive"
   opens. Click **Log in**, choose **Log in with Nourax** in the browser and allow
   access.
4. For Finder badges, NoDrive's Finder extension ("NoDrive FinderSync") must be
   switched on in System Settings (extensions).

### Linux

1. Use a desktop with a tray area (KDE Plasma; or GNOME with the AppIndicator
   extension) and a password keyring (GNOME Keyring or KWallet; NoDrive stores its
   app password there).
2. In a terminal, in the folder with the file:
   `chmod +x NoDrive-*-x86_64.AppImage` and `./NoDrive-*-x86_64.AppImage`
3. If it reports a FUSE error, install FUSE 2 (Ubuntu 24.04:
   `sudo apt install libfuse2t64`) or start it with
   `./NoDrive-*-x86_64.AppImage --appimage-extract-and-run`.
4. Click **Log in**, choose **Log in with Nourax** in the browser and allow access.

WSLg on this PC works for the screens, but WSL has no tray area and usually no
keyring, so a real Linux machine is better for the sign-in and sync tests.

### What to try, and what to send back

1. Sign in. Then open `https://drive.nourax.org` in the browser, go to
   *Settings → Security*, and read out the name shown for the new device.
   Server 35 should show **"Sync client - 35.0.0 (Windows)"** (or `Macintosh`,
   `Linux`).
2. Read out the text of the browser page that asked to allow access (the server
   shows the app's full user agent there, for example
   `Mozilla/5.0 (Windows) mirall/35.0.0-nourax.1 ... (NoDrive, ...)`).
3. Work through the table in 1.2 and report PASS or what went wrong (a screenshot
   helps). Look for the word "Nextcloud" or a Nextcloud logo on every screen.
4. Once signed in, syncing also works away from home (WebDAV with the app password
   is not limited to the home network).

### NoAgenda: what to try (after sign-in)

NoAgenda reads your calendars, to-dos, NoGantt projects and NoBoard boards. Steps 1 to 7 only look; step 8 changes things (use a test event and a test task). If the file
`NoAgenda/agent-registry.json` is not in your NoDrive yet, everything shows as "Self" (the Sources screen says so).

1. Open the tray menu and click **NoAgenda**. A window opens with today's list. The first time, the question
   "Which of your calendars are personal?" appears once: tick the personal ones, **Save** (or **Skip**).
2. Today shows your events and to-dos from NoCalendar and NoTasks, your NoGantt tasks and NoBoard cards. Each row names its
   agent or "Self". Try **Day / Week / Month**, **All / Work / Personal** and the chips **Me / My agents / My team**.
3. Click a row: the panel on the right has one button, **Open in NoCalendar / NoTasks / NoGantt / NoBoard**; it opens the
   page in your browser.
4. If you run projects or boards, **My team** lists only the problems (late, due today, blocked, not assigned). **Show
   all team tasks** lists everything.
5. **Sources** lists every calendar, project and board with its setting. Switch one to Hidden and back.
6. Switch off Wi-Fi and click **Refresh**: the banner says "showing what was saved at ...", the list stays.
7. Switch the system to dark mode: all text is readable. No screen says "Nextcloud".
8. Editing (use items you can lose, not real appointments). Click a row: the panel has fields and **Save**.
   - Change an event's time and **Save**. Open the Calendar page of the Drive: the new time is there; alarms and guests are unchanged.
   - Change the same event in the Calendar page, then in NoAgenda change its title without refreshing and Save: you read "This changed somewhere else
     while you were editing. NoAgenda kept the other change." with **Apply my change again** in the Not sent panel.
   - Tick a to-do; look at it in the Tasks page.
   - Move a NoGantt task's end date: "Also moved: ..." appears and **Undo** puts it back within 10 seconds.
   - Choose "my agent" on a NoGantt task and on a card, then choose Self again. A row that can't be changed has a lock and says why.
   - Switch Wi-Fi off, change something (a clock mark and "Waiting to send 1 change" show), switch Wi-Fi on: the banner goes away and the change is on the Drive.
   - Put two items at the same time: both show the clash mark. Plan more work in a day than it has free hours: the workload bar warns. The **Conflicts** chip lists only the problems.
   - **+** makes a new event or to-do (Self is preselected as the agent). Delete the test event (after the question): it is in the Trash bin of the Calendar page.
   - Nothing on your computer or in your files is ever deleted by NoAgenda; NoGantt tasks and cards can't be deleted here.
9. Send back PASS or what you saw for each point.

### Problem reports: what to try (after sign-in)

The "Nourax Support" account may not exist on the Drive yet. Until it does, the
app still saves the zip in your own files and says "Saved in your NoDrive >
Problem reports." That is the expected result for now.

1. Open **Settings > Advanced**. Above "Edit Ignored Files" there is **Send problem
   reports to Nourax**. It must be switched **off**. The button **Report a problem**
   is right of "Create Debug Archive".
2. Click **Report a problem**, type a short note ("test"), click **What's included**
   to read the list, then **Send**. Expected: a message "Saved in your NoDrive >
   Problem reports." (or "Problem report sent" once Support exists). On
   `https://drive.nourax.org` open your files: a folder **Problem reports** with one
   zip named like `2026-10-28_12-00-00_windows_35.0.0-nourax.1.zip`. Download it
   and open it: it has `report.json`, `logs/` and `config/`, and it must not
   contain a password. Send back the file name and whether anything private is
   visible.
3. Log out (Linux, macOS) or log off (Windows) while NoDrive is running, log in
   again, start NoDrive. Expected: **no** question.
4. End NoDrive hard: Windows Task Manager > NoDrive > End task; macOS Activity
   Monitor > NoDrive > Quit > Force Quit; Linux `pkill -9 nodrive`. Start NoDrive
   again. Expected: after a few seconds the question "NoDrive closed unexpectedly.
   Send a problem report?". Click **Not now**. End NoDrive hard and start it a
   second time: the old case must not be asked about again.
5. Switch **Send problem reports to Nourax** on. End NoDrive hard again and start
   it. Expected: no question; a tray message "Problem report sent" (or "Saved in
   your NoDrive > Problem reports.") and a new zip in the folder.
6. Switch the system to dark mode and open the dialog and the switch: all text is
   readable.
7. Report PASS or what you saw (a screenshot helps) for each step. Also check that
   no screen says "Nextcloud".

## 3. Screenshot index

All screenshots are in `docs/screenshots/` (84 files). Each was looked at for
Nextcloud names, logos and colours: none found ([branding-sweep.md](branding-sweep.md),
section 5). Variants: `en-light` (English), `de-light` (German), `ar-light`
(Arabic, right to left), `en-dark` (English, dark mode).

| Folder | Files | Screens |
|---|---|---|
| `linux/<variant>/` (4 variants) | `01-wizard.png`, `02-tray-menu.png`, `03-proxy-settings.png`, `04-sign-in.png` | first start "Log in to NoDrive"; tray menu without an account; proxy dialog; "Switch to your browser" after Log in |
| `windows/<variant>/` (4 variants) | `01-wizard.png`, `02-tray-menu.png`, `03-proxy-settings.png`, `04-sign-in.png` | same screens on Windows |
| `windows/installer/` | `01-welcome-en.png`, `01-welcome-de.png` | MSI installer, first page |
| `windows/first-start/` | `00-first-start.png` | first start with no settings |
| `windows/` | `app-icon-32.png` | the icon Windows shows for `nodrive.exe` |
| `macos/<variant>/` (4 variants) | `00-screen.png`, `01-wizard.png`, `02-menu-bar.png`, `03-proxy-settings.png`, `04-sign-in.png` | whole screen with menu bar and Dock; wizard; menu bar icon; proxy dialog; "Switch to your browser" |
| `macos/en-light/` | `02-after-icon-click.png` | menu bar icon clicked without an account: brings the wizard back |
| `macos/` | `app-icon-512.png` | 512 px image from `NoDrive.icns` |
| `linux/problem-reports/`, `windows/problem-reports/` | `advanced-{light,dark}.png`, `advanced-switch-on-{light,dark}.png`, `report-dialog-{light,dark}.png`, `whats-included-{light,dark}.png`, `unclean-exit-prompt-{light,dark}.png` (from the unit test), `smoke-unclean-exit-prompt-{light,dark}.png` (from the real app) | Settings > Advanced with the new switch and button (off and on), the "Report a problem" dialog, the "What's included" text, the question after an unclean exit |
| `macos/problem-reports/` | `smoke-unclean-exit-prompt-{light,dark}.png` | the question after an unclean exit in the real app |

Count: Linux 28, Windows 32, macOS 24 (12, 12 and 2 of them in `problem-reports/`).

Not captured yet: the Settings window (General, Network, Info/About). "Settings"
in the tray menu opens it even without an account (only a click on the tray icon
itself brings the wizard back), but the smoke tests do not take it yet. These
screens were reviewed in code instead ([branding-sweep.md](branding-sweep.md),
section 5): Info shows "NoDrive Desktop Client Version ...", the Legal Notice
button is hidden, "Usage Documentation" opens `https://drive.nourax.org`, and the
switch colour is `#2563EB`. Adding them to the three smoke tests is a to-do; until
then, Settings, Activity and About are part of the owner tests (look at each
page once, before and after sign-in).
