# NoAgenda (desktop): the core, the edit step and the Timeline

Binding spec: `plans/noagenda-decisions.md` (approved 2026-10-28). Platform plan: `plans/noagenda-desktop-plan.md`.
This page describes what is built in the first step ("NoAgenda core", read only), in the second step ("Editing and
conflicts", the section further down), in the third step ("The Timeline", further down) and how they are tested. The
computer's own Calendar and Contacts apps come in the next step.

## What the user gets

- A **NoAgenda** entry in the tray menu (Windows: left- and right-click menus; macOS: the menu-bar popup; Linux: the
  tray menu). It opens one NoAgenda window per account.
- **Day, Week and Month** lists of everything the person is responsible for: calendar events, to-dos, NoGantt tasks
  and milestones, and NoBoard cards.
- The three kinds of work: do it myself, supervise my agent, oversee my team. The chips **Me / My agents / My team**
  and the **All / Work / Personal** switch filter them. Every row names its agent, or "Self".
- **My team**: only the problems in what the person runs (late, due today, blocked, someone overbooked, not assigned
  to a person, and the agent and sharing problems of design 4.4), with a switch "Show all team tasks".
- **Sources** screen with the defaults of design 6.1, the working hours, and the agents of the registry. The one-time
  question "Which of your calendars are personal?" comes once after the first read.
- The last data stays readable offline ("showing what was saved at 10:42"). It refreshes every 5 minutes while
  NoDrive runs, but only for accounts where NoAgenda was opened at least once. This also holds after a restart:
  at start NoDrive loads the saved data of those accounts (no request, no window), and the timer takes it from there.
  The settings (Sources, the personal-calendars answer, working hours) and the offline cache survive a normal quit.
- **Problems with your agents**: when one of your agents sits on a NoGantt task or a NoBoard card that nobody has (the
  task or card is not yours), you get a row of its own, "Writer-Bot is on "X" without you", with "Open in NoGantt" /
  "Open in NoBoard". It only reports the problem: it has no date and is not your work (no workload, no clash, not in
  "No date"). When you run the project or board, the problem is on the team row instead.
- The detail panel has **Open in NoCalendar / NoTasks / NoGantt / NoBoard** (the default browser, the account's own
  server). Since the edit step it also changes things: see "Editing and conflicts" below.

## Where the code is

`src/gui/nourax/noagenda/` (namespace `OCC::Nourax::Agenda`), compiled into `nextcloudCore` by
`src/gui/nourax/nourax.cmake` (no new line in `src/gui/CMakeLists.txt`). The QML files are a Qt resource
(`noagenda.qrc`), started by `initNoAgendaResources()`.

| Folder | What |
|---|---|
| `net/` | `HttpTransport` (the one way to talk to the server) and `AccountTransport` (through `Account::sendRequest()`: the account's own connection, credentials and proxy; it refuses any other host) |
| `registry/` | the agent registry (schema `nourax-agent-registry` v1) and `RegistrySource`, **the one swappable source**: now the user's own file `/NoAgenda/agent-registry.json`, later the agents app (one constant in `registrysourcefactory.cpp`) |
| `ical/`, `caldav/` | a small iCalendar reader, CalDAV request bodies and multistatus parsing, the calendar reader |
| `ocs/`, `gantt/`, `deck/` | OCS envelope, server info, the NoGantt 2.4.1 client and its contract check (version guard), the Deck client |
| `logic/` | source settings and defaults, and the mappers that turn raw data into rows (agents, integrity flags, dedupe, problems) |
| `store/` | the offline cache (JSON per account, written atomically) |
| `accountagenda.*`, `agendaservice.*` | one account's readers, merge, status and back-off; the 5 minute timer. `AgendaService` deletes an account's cache only on `AccountManager::accountSyncConnectionRemoved` (the user removed the account), never on `accountRemoved`, which `AccountManager::shutdown()` also emits for every account when NoDrive quits |
| `ui/`, `qml/` | the controller, the launcher (tray entries) and the QML window (light and dark) |

Upstream hooks (marked `// NoDrive: NoAgenda`, applied by `tools/rebrand.d/noagenda.py`, which `rebrand.py --check`
verifies): `trayaccountpopup_qt.cpp` (Windows and Linux popup menu), `macOS/trayaccountpopup/nctraypopup.mm`,
`systray.cpp` (Windows right-click menu) and `test/CMakeLists.txt`. The service starts from one marked line at the end
of our own `ProblemReportManager::start()`.

## How it reads (design section 5)

Every request goes through the account's own connection. Nothing else is contacted.

- **NoGantt** routes are OCS routes: `ocs/v2.php/apps/nc_gantt/api/v1/...` with `OCS-APIRequest: true` (the design's
  table says `/apps/nc_gantt/api/v1`; that is not the real path). NoGantt shows no version to a normal user, so the
  **version guard** is a contract check: every answer must have the keys and types of 2.4.1
  (`gantt/ganttcontract.cpp`). A mismatch shows the NoGantt rows read only, with "Open in NoGantt".
- **Deck** (REST `index.php/apps/deck/api/v1.0`): the guard is `capabilities.deck.version`; only 1.19 is read-write.
  The request carries `OCS-APIRequest: true`, so the server's CORS check does not log the session out.
- **CalDAV**: PROPFIND of the principal and the calendar home; per calendar whose sync token (or ctag) changed one
  `calendar-query` for the events of the sync window (expanded by the server, answers in UTC) and one for the
  not-completed to-dos (never expanded: the server drops every VTODO on expand). Unchanged calendars are not read
  again. A calendar that fails keeps its old objects: per calendar and per component (events, to-dos), once, so a
  calendar that keeps failing never grows in memory or in `calendars.json`.
- **Registry**: GET of `remote.php/dav/files/<user>/NoAgenda/agent-registry.json` with `If-None-Match`.

If NoGantt or NoBoard answers 401 or 403 while the account is connected, that source is marked **refused**: a banner
says "NoGantt didn't accept NoDrive's sign-in on your server" (with what was saved, if any) and offers "Open in
NoGantt" / "Open in NoBoard"; the status code is logged and no credential ever goes to another host. The account's
sign-in state stays with `AccountState` ("Sign in to NoDrive to see new changes" only comes from the server itself,
CalDAV or the user info, or from the account being signed out).

## Rules that are tested

- Defaults of design 6.1 (own calendars Personal until the question, shared Team, subscriptions and birthdays Info,
  board calendars and NoGantt's task copy hidden automatically, projects and boards by relation, server admins).
- Event rules: free events do not block time, declined invitations are hidden, tentative ones are soft, an event of a
  shared team calendar shows only if I organise it or am invited, an invitation is always Self, the same invitation
  in two calendars is one row. (To-dos of a shared list with no organiser or attendee show as mine: see the open
  questions below.)
- Agents (4.2, 4.5): `X-NOURAX-AGENT-ID`, the registry `calendar_tag`, board labels, and NoGantt planning people
  (recognised by the registry's `nogantt_resource_ids`, or by the exact name `<robot> <agent> (<person>)`). Anything
  else is "unknown agent" and flagged, never guessed. An agent's hours never count for its person.
- Integrity flags (4.4): agent with no person, agent paired with someone else, two agents, unknown or retired agent,
  not linked to an account, not assigned to a person, a person in a project not shared with them. Each flag goes to its
  audience (the person, the manager, or both): "agent with no person" reaches the person as the row "Writer-Bot is on
  "X" without you" and the manager on the team row.
- A board label is matched to the registry among the agents of the people the card is assigned to (the same label can
  exist in two organisations); only when none of them has it, the label may name another person's agent ("belongs to").
- Privacy between organisations: another person's agent is named only if its organisation is one of mine. This holds
  for the agent column and for the problem sentences ("Another organisation's agent belongs to Sara, not Ali"). A
  sentence never shows a user id: the owner's name comes from the planning people, the board members or the assigned
  users, and is left out when it is not there ("Writer-Bot belongs to someone else, not Sara").
- Offline cache: no password, token or cookie is ever written; the cache folder is removed when the user removes the
  account (only `<config>/noagenda/<account>`, never anything else) and kept when NoDrive quits.
- The Sources screen keeps every board, calendar and project, also the ones switched to Hidden, so they can be shown
  again. A hidden project keeps its shares (read once a day), so a server admin does not fetch them on every refresh.

## Editing and conflicts (edit step)

Design sections 7 and 9. Everything is new files (`edit/`, `ical/icalpatch.*`, `logic/conflicts|workload|agentload.*`,
`ui/editcontroller.*`, QML); the only upstream-visible change is that the core mappers fill a few more fields of a row.

### What the person can do

| Change | Saved to | Notes |
|---|---|---|
| Event: title, start, end, all day, description, my agent; create ("+"); delete | its calendar, CalDAV | `If-Match` ETag; create `If-None-Match: *`; delete asks "Delete this event? It moves to the calendar's trash bin." |
| To-do: title, start, due, description, tick done, my agent; create; delete | its list, CalDAV | done = `STATUS:COMPLETED` + `COMPLETED` + `PERCENT-COMPLETE:100` |
| NoGantt task: start and end date, % done, status, done | `PUT ocs/v2.php/apps/nc_gantt/api/v1/tasks/{id}` | read fresh first; `updated_at` is the version; "Also moved: ..." and **Undo** for 10 s (`changes/{op}/revert`); `deck_confirmed` is always false |
| NoGantt task: **my agent** (Self or one of my agents) | the same PUT with `resources` | the list is replaced as a whole, so see "My agent on a task" below |
| Card: due date, done, my agent label | Deck REST | the PUT is built from the **fresh** card and always sends every field (Deck clears what is missing); label created only where the person manages the board |
| Delete a NoGantt task or a card | not allowed | "Open in NoGantt" / "Open in NoBoard" |
| Repeating event or to-do, invitations from someone else | not changed here | "This repeats. Change it in NoCalendar." (NoTasks) with the Open button; an invitation is always Self, the picker says "You were invited, so this is always Self." |

- **"+"** (header, or Ctrl+N / Ctrl+Shift+N) makes a new event (choose the calendar) or a new to-do (choose the list).
  Only the person's **own** calendars and lists (Personal and Work) are offered: in a shared team calendar a new item
  would have no person, would vanish at the next refresh and would show to the team. An event of a shared team
  calendar can be changed only by its organiser.
  The **agent is required** and **Self is preselected**; paused agents are shown greyed, retired ones are not listed.
  Times are written in UTC (`Z`), so no time zone definition has to be made. `PRODID:-//Nourax//NoAgenda//EN`.
- **Who does it**: person first (it is my calendar, list, task or card), then exactly one agent: Self or one of my agents.
  An agent of a calendar item is `X-NOURAX-AGENT-ID` plus the registry's calendar tag in `CATEGORIES`; Self removes both.
- **A row that can't be changed says why in plain words** (lock icon): a viewer of a NoGantt project, a card on a board
  where the person can only look, a read-only calendar, a newer app version (the version guard), a repeating item.
- **A long description** (over 2,000 characters) is shown cut, so it is not offered for changes: the field is locked with
  "This description is too long to change here. Open in NoCalendar." and nothing after character 2000 can be lost.
- **Delete** exists only for events and to-dos (CalDAV objects, not files), only after the confirmation, with `If-Match`.
  The server keeps them in the calendar's trash bin. Nothing on the device or on the Drive is deleted by this step.

### My agent on a NoGantt task

`PUT tasks/{id}` with `resources` replaces the whole list of people. `edit/ganttagentchange.cpp` builds the new list from
the fresh task: every other row keeps its resource and its work, in its order; my own row is never touched; the rows of
my other agents are removed (**every** planning person that stands for one of my agents counts: the registry lists one id
per manager pool, and an exact name match counts too, so no old row of an agent stays behind); the chosen agent's row
(work empty) is added at the end. Of the chosen agent's planning people the one already on the task is kept, otherwise one
that belongs to this project is used (it sits on a task of the project, or it is in the pool of the project's owner).
**Nothing is sent** when:

- the list would have more than 4 rows ("This task already has 4 people or agents. Ask your project manager."),
- I am not on the task, or a row on the task is not visible to me (the server would drop it silently),
- the recomputed total work would differ from the task's work (the estimate would change or be erased),
- the first (main) row would change,
- the chosen agent's planning person is not visible to me in NoGantt (design 4.5): the picker says
  "Ask your project manager to add [robot] Writer-Bot to this project" and offers "Open in NoGantt". NoAgenda never creates planning people.

After the PUT the returned `task.resources` must be the list that was sent; if not, the toast says "Check this task in NoGantt"
and the ids of both lists (numbers only) go to the log. A project that allows roles only is not checked on the device: the
server answers `roles_only_assignment`, the change is not applied, and the person reads "This project only allows roles. Ask your
project manager." in "Not sent".

### The offline queue and edit conflicts

- Every edit becomes a command in `queue.json` (account folder; no password or token, only the semantic change and the version
  the person saw). It is drawn on top of the rows at once with a clock mark, a banner says "Waiting to send %n changes", and
  the commands go out oldest first, **one at a time**. An edit of an item that is still waiting is folded into the waiting command.
- No connection: the commands wait, survive a restart, and are sent when NoAgenda can reach the server again.
- A failed precondition (ETag mismatch or 412, another `updated_at`, another `lastModified`) is an **edit conflict**: the
  other change wins, nothing is written, and the command goes to the **Not sent** panel with "This changed somewhere else while you
  were editing. NoAgenda kept the other change." Buttons: **Apply my change again** (re-reads the item and sends the same change
  on top of its new version), **Keep the other change** (drops the command), and Open in ... . A command leaves the list only
  with a click or when it was sent. A refusal of the server (403, 400) is kept the same way, in plain words.
- **The version guard also holds queued edits.** NoGantt and NoBoard commands are not sent until a read after the last
  (re)connection has run the guard of that app (design 5: never writes blind). If the guard says read only, the command goes
  to "Not sent" with the read-only sentence and "Open in ...". Calendar commands are not held up by this.
- **A 401 or 403 from NoGantt or NoBoard while editing is a refusal by that app**, not a sign-in problem of the account: the
  command goes to "Not sent" with "NoGantt does not accept this sign-in. Open in NoGantt." and the next edits, calendar edits
  included, go on. Only CalDAV answers can show the "Sign in to NoDrive" banner.
- **Undo** says "Undone." only when NoGantt has answered. With no connection, or when NoGantt refuses the revert, the toast
  says so and offers "Open in NoGantt".
- A card's agent label switch that loses the connection after the old label was taken off is not a conflict at the retry:
  NoAgenda knows its own part of the change. New board labels for agents are created in the Nourax cobalt (`2563EB`).

### The four conflict kinds, the workload bar and the agents' load

All computed on the device from rows the person can already see (`logic/conflicts.cpp`, `workload.cpp`, `agentload.cpp`):

- **Clash**: two timed items of mine overlap: events, cards that have both `startdate` and `duedate` on one day, and NoGantt tasks
  that start and end on one day with real times (not midnight; read in the person's time zone). A tentative one makes it a soft
  clash, drawn fainter. Free time, declined invitations, information calendars and everything whose agent is not Self never clash
  with mine.
- **Overbooked**: planned = my own share of `work` of the NoGantt tasks active that day (on a task my agent does, my own share is
  my review or supervision time and counts; the agent's hours do not), spread evenly over the task's working days
  (my pattern; holidays of my planning person's calendar and my all-day busy events are days off); free = the working hours minus the
  busy events and timed cards inside them (tentative counts as busy). The Day view starts with a workload bar "6 h planned / 4 h free" with a warning
  sign; Week shows it per day. A task without `work` adds nothing and is counted as "No estimate".
- **Deadline on a day off**: a due or end date on a non-working day, a NoGantt holiday or an all-day busy event.
- **Blocked**: a predecessor is late (decided by the NoGantt mapper of the core step).
- The header has a **Conflicts** chip: only rows with a clash, a block, a late mark, a day off or an overbooked agent.
- **Agents**: an agent's hours never count for its person. Each agent of mine (and of the people I run) has a capacity per day
  (`capacity_hours_per_week` over the days of its weekly pattern, in its time zone, minus `days_off`); its load is the work of its
  own planning person on the NoGantt tasks plus its timed events and timed cards. Over capacity gives the problem row
  "[robot] Writer-Bot is overbooked on Thursday" (in "Problems with your agents", or in My team for the agents of other people).

### Choices where the design is silent (builder's calls)

- The editor is a form in the detail panel (own fields, no Controls style); dates are written like 2026-11-04 and times like 09:30.
- A change to an item that is waiting is folded into the waiting command; a change of another kind for the same item waits behind it.
- "Apply my change again" sends the change without a base version: the writer reads the item fresh and the PUT carries the fresh ETag.
- Marking done on NoGantt tasks and cards, and on to-dos, uses the same "Mark done" button; items that are done leave the day lists
  (as in the core step), so "not done" exists in the writers but is not offered in the window.
- The 5 minute refresh and every edit refresh the data; a sent change stays drawn until a refresh has read everything back.
- The agent overbooking is looked for from today to 28 days ahead.

## Choices where the design is silent (builder's calls)

- **Name rule (b) of 4.5** recognises a name-only agent planning person also when it sits next to someone who is not
  its person, so that "Design-Bot belongs to Sara, not Ali" can be said (the name still has to be exact and the agent
  has to be in the registry).
- Without a readable registry an agent planning person shows as "<name> (unknown agent)", as 4.1 says for unknown ids.
- A section **"In progress"** (items that span the day but are not due) and **"Late"** (today only) are added to the
  Day view; the Week and Month lists show an item on its due day.
- The window is built from QML and a controller with plain variant lists instead of list models and registered types.
- CalDAV uses the sync token (or ctag) to decide whether to read a calendar again, then reads that calendar in full;
  there is no `sync-collection` REPORT yet.
- Icons in our own screens are drawn shapes (no emoji font is needed); text from the server is shown as it is.

## Tests

| Test | What |
|---|---|
| `NoDriveAgendaRegistry`, `...ICal`, `...CalDav`, `...Gantt`, `...Deck`, `...Rules`, `...Cache`, `...Service` | unit tests with the fixtures in `test/nodrive/noagenda/` and a fake transport (all three systems) |
| `NoDriveAgendaUi` | the window in a QML engine, light and dark, screenshots in `nodrive-screenshots/noagenda/` (Day, Week, Month, My team, Sources, the personal-calendars question, detail, not linked, offline, registry missing); pixel checks of the background and the selected chip; no text with the server software's name |
| `NoDriveAgendaServer` | the real throwaway server (Linux CI job `agenda-server`): `auth-proof`, `contract`, `read-ali`, `read-mara`, `read-sara`, `read-olaf`, `guard`, and the edit phases `edit-calendar`, `conflict`, `edit-gantt`, `edit-deck`, `offline` |
| `NoDriveAgendaICalPatch` | only the named properties change; alarms, attendees, unknown `X-` lines and time zones stay byte for byte; categories add and remove with escaping; done and undone; repeating refused; the text of a new item passes the strict reader |
| `NoDriveAgendaEdit` | every edit of design 9 sends exactly the expected verb, path, headers (`If-Match`, `If-None-Match`, `OCS-APIRequest`) and body; Deck PUT always has every field; conflicts, refusals, viewers, repeating and invitations |
| `NoDriveAgendaGanttAgent` | "my agent" on a task: other rows unchanged, order and main row kept, the 4-row limit, work-total guard (hours and person days), invisible rows, the answer check, an agent with two planning people (Self and another agent remove both), the planning person must belong to the project |
| `NoDriveAgendaQueue` | order, persistence across a restart, no parallel sends, folding, conflicts kept until a click, offline then online, "Apply my change again", the optimistic overlay |
| `NoDriveAgendaConflicts` | clash and soft clash (events, timed cards, tasks with real times), spread of work, my review time on an agent's task, holidays and days off, busy time, no estimate, deadline on a day off (three sources), blocked, an agent's capacity, days off and time zone, agent overbooking (events and timed cards), the text of a card or task time |
| `NoDriveAgendaTimelineLayout` | the pure Timeline maths: date and x round trips at both zooms, the half-day snap (0.49 stays, 0.51 moves), groups (project, phase, NoTasks list, NoBoard board) and their order, bar and diamond rectangles, a cut bar, the four dependency types (end points and direction of every arrow), the stub for an outside predecessor, the lag in the tooltip, the shading of my busy time and the days off, drag to dates, locked bars give no drag, my own work only |
| `NoDriveAgendaUi` (Timeline) | the Timeline in light and dark (bars, % fill, groups with a phase, diamonds, the agent, arrows drawn on screen, shading, today), the Weeks / Days zoom, a real mouse drag with the ghost bar, the saved `PUT` (dates, `adjust_lag`, `deck_confirmed`), "Moved to ... Also moved: ..." and Undo, a drag under half a day, Esc, the keys, a locked bar, a to-do bar, a move made offline, the empty state, the chips |
| `NoDriveAgendaServer` phase `timeline` | on the real throwaway server: the layout of the seeded data, a drag by two days of one of Ali's tasks, the dependent task moves (`propagated`), the toast, Undo gives both tasks their dates back |

The window test also covers the Timeline with screenshots in light and dark: `timeline`, `timeline-drag`, `timeline-undo`,
`timeline-readonly`, plus `timeline-days-light` and `timeline-empty-light`; the server job adds `live-timeline-{light,dark}.png`
(Ali's seeded data on the real throwaway server).

The window test also covers the edit step with screenshots in light and dark: `day-clash-overbooked`, `edit-agent-gantt`,
`edit-agent-not-in-project`, `new-event`, `edit-conflict`, `pending-offline`, `delete-confirm`, `repeating-locked`, `viewer-locked`.

The job `agenda-server` (`tools/ci/agenda-server.sh`, `tools/ci/agenda-seed.py`) starts the pinned
`nextcloud:35.0.2-apache` and installs the pinned apps, with their SHA-256 checked: calendar 6.6.4, tasks 0.18.1,
Deck 1.19.1 and **nc_gantt 2.4.1** (release `v2.4.1-9a09070`, SHA-256
`9003904a4398f94f1870cc93e6a129d6c7e72298de05b3c5d0104efa976bcfb9`). The server contacts no app store. Users: Mara
(manager) and Ali and Sara (staff) in `org-a`, Olaf in `org-b`. The throwaway passwords are made at run time, masked
and never written to the artifact (the job greps for them).

## Auth proof (design section 5)

Measured in the job `agenda-server` (Linux run [40809977468](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40809977468), commit `46f8a71e0b`,
artifact `nodrive-linux-agenda-server`) against a throwaway Nextcloud 35.0.2 with nc_gantt 2.4.1, Deck 1.19.1,
Calendar 6.6.4 and Tasks 0.18.1. The credential is an app password made by `occ user:auth-tokens:add`, the same kind
of credential the client gets from Login Flow v2, used through the client's own `Account` (Basic authentication).

| Call | Answer |
|---|---|
| NoGantt `my-tasks`, `projects`, `resources`, `capacity-check`, `projects/{id}/tasks`, `milestones`, `task-sync` with `OCS-APIRequest: true` | 200, OCS JSON |
| The same NoGantt call **without** `OCS-APIRequest` | **412**: the header is required (NoAgenda always sends it) |
| Deck `boards?details=true`, `boards/{id}/stacks` (with `OCS-APIRequest: true`) | 200 |
| CalDAV PROPFIND of the principal, REPORT `calendar-query` | 207 |
| `remote.php/dav/files/<user>/NoAgenda/agent-registry.json` | 200 |

So no part of NoAgenda has to fall back to an "Open in NoGantt" / "Open in NoBoard" banner on this server. The fallback
is built and has a unit test with a fake 401 and 403 (`aRefusingAppTurnsIntoAnOpenInButtonNotIntoASignInRequest` and the
window test `aRefusingAppOffersToBeOpenedInsteadOfAskingToSignIn`): banner with "Open in ...", status code logged, never a
credential to another host.
The key sets of every live answer are in the artifact (`contract-keys.txt`) and are compared with the committed
fixtures (a missing key or another type fails the job).

## Found against the real server

- NoGantt writes a dependency's predecessor as `"t-<task id>"` (or `"m-<milestone id>"`), not as a number.
- The server's calendar scheduling puts an invitation into each attendee's **default calendar**, so the same event is
  in the team calendar and in the person's own calendar. NoAgenda shows it once (same UID and recurrence, the person's
  own calendar wins).
- Deck creates a "welcome" board with sample cards for every new user. The cards assigned to the person show in the
  day list like any other card (the board's calendar is hidden automatically).
- A daily repeating event is about 400 rows in the 14 month sync window (the server expands it).

## Edit step: the agent id in the web pages, server phases and open points

### Does the agent id survive an edit in the web pages? (design 4.2)

`tools/ci/agenda-webedit.py` (Playwright for Python 1.49.1 with Chromium: CI only, never shipped; Apache-2.0 and BSD-3-Clause) signs in
to the throwaway server, opens a tagged event in the Calendar app and a tagged to-do in the Tasks app, changes the title in the page,
saves, reads the raw item through CalDAV and reports `X-NOURAX-AGENT-ID` and the `CATEGORIES` tag separately as "survived" or "dropped".
The job fails only when both are dropped for one app. The before and after items are in the artifact with every value replaced by a
hash, except the two agent properties. Result of the CI run on the last commit of this branch (Calendar 6.6.4, Tasks 0.18.1):

| App | Driven by the script | X-NOURAX-AGENT-ID | CATEGORIES tag |
|---|---|---|---|
| Calendar (event "Writer-Bot review") | yes | survived | survived |
| Tasks (to-do "Draft report") | yes | **dropped** | survived |

The iPhone and Android calendar apps and macOS Calendar cannot be driven in CI: they are owner test rows (below).
**What this means:** the Tasks page removes the `X-NOURAX-AGENT-ID` line but keeps the `CATEGORIES` tag, and the Calendar page keeps both. NoAgenda reads
either one (the id first, then the registry's calendar tag), so an agent set on an item survives an edit in both web pages; only the copy that the
Tasks page drops is lost. That is exactly why the tag is written as well (design 4.2, "keep the CATEGORIES copy as the fallback"). A to-do that was edited in the
Tasks page has the tag only; NoAgenda writes the id again the next time the person changes the agent of that item.

### What the real throwaway server confirmed (job `agenda-server`)

About 180 checks of the phases below passed on the last run; the job reads the state of the server directly (as the same user), not only
what the client says. The read phases (`auth-proof` to `guard`) are unchanged and still prove that nothing is deleted, moved or copied.

| Phase | Checked on the server |
|---|---|
| `edit-calendar` | an event with an alarm and an unknown property: title and time change, **alarm, unknown property and UID stay**, the ETag changes; my agent on and off (id and tag, then both removed); a to-do ticked done (`STATUS`, `COMPLETED`, `PERCENT-COMPLETE:100`) and not done again; a new event with an agent and a new to-do; the event deleted (404 afterwards, the only DELETE of the whole job); a repeating event is refused and its object is untouched; an invitation is always Self |
| `conflict` | the web page changes a to-do between the person's read and write: conflict found, the server keeps only the other change; "Apply my change again": both are on the server |
| `edit-gantt` | a move with a successor: the dependent task moved and is named, **Undo** puts the date back; % done and status; done; **my agent Self, Writer-Bot, Self** (rows and total work untouched); the refusals (agent row with work, paused agent, somebody else's agent, 4 rows); a viewer is refused with the plain sentence and the task is unchanged; the conflict by `updated_at` and "Apply my change again" |
| `edit-deck` | due date and done with the description and start date kept (Deck clears what is not sent); the agent label Writer-Bot, Self, Writer-Bot; a missing label is refused on a board that the person may only edit, and created on a board that the person manages; the conflict by the card's version |
| `offline` | three edits wait while the server can't be reached, survive a restart, and are sent in the order they were made |

Found against the real server:

- NoGantt moves a date that falls on a weekend to the next working day. The editor sends the date as typed; the row shows what the server kept after the refresh.
- One PUT was answered with HTTP 500 by the busy throwaway server (it uses SQLite). The command stayed waiting and went out at the next try, as designed:
  a server error is tried again (three times), then it moves to "Not sent". The CI phases do the same.
- A refused change (for example a viewer, or the 4-row limit) stays in "Not sent" until a click, like a conflict.
- After a delete the event is gone from the calendar (404). That the server keeps it in the calendar's trash bin is the server's default (design 9) and is **not
  verified by the job**: the places that answer a plain PROPFIND (`remote.php/dav/calendars/<user>/trashbin/`, `remote.php/dav/trashbin/<user>/`) do not list the deleted
  objects (the Calendar app lists them with its own request). The job reports this as "not verified". Owner test row 9 covers it.

### Owner test rows (real devices, plain English)

1. Change an event's time in NoAgenda, then look at it in the Calendar page of the Drive: the new time is there, alarms and attendees are unchanged.
2. Change the same event in the Calendar page, then change its title in NoAgenda without refreshing: "changed somewhere else" with "Apply my change again".
3. Tick a to-do in NoAgenda, then look at it in the Tasks page.
4. Move a NoGantt task's end date: "Also moved: ..." appears and **Undo** puts it back (within 10 seconds).
5. Choose "my agent" on a NoGantt task and on a card; set Self again.
6. Switch Wi-Fi off, change something, switch it on: "Waiting to send 1 change" goes away and the change is on the Drive.
7. Put a dentist visit and a client call at 10:00: both rows show the clash mark. Plan 6 hours of project work on a day with 4 free hours: the workload bar warns.
8. Set an agent on an event in NoAgenda, then edit that event in the iPhone or Android calendar app and look at it again in NoAgenda: is the agent still there? (Decides whether the device apps keep `X-NOURAX-AGENT-ID`; the tag is the fallback.)
9. Delete an event in NoAgenda (after the confirmation), then open the Trash bin in the Calendar page: the event is there and can be restored.

### Open points of the edit step

- A NoGantt project that the person only views can't be changed, not even the person's own task (NoGantt refuses with 403); NoAgenda locks it with a plain sentence.
- NoGantt and Deck have no conditional write: the item is read fresh right before the write and its `updated_at` or `lastModified` is compared, so a change that lands in
  the few milliseconds between the read and the write can still be overwritten (documented risk B2).
- "My agent" on a NoGantt task is refused whenever the agent's own row holds work (its removal would change the task's estimate). That is by design (the estimate is never changed here).
- The editor writes times of new events in UTC; a time zone definition is never created. Existing events keep theirs unless their time is changed.
- Repeating events and to-dos are not changed here (design 9); a repeating to-do still shows only its first due date (the server does not expand to-dos).
- Texts are English; translations are a to-do.

Small points left open by the independent review of the edit step (none loses data; all are to-dos):

- A queued NoGantt or NoBoard edit that is moved to "Not sent" because the app is gone (404) says "Read only: this version of the app on the server has not been checked yet." The sentence should name the real reason.
- While every read of NoGantt or NoBoard since the reconnection fails or is refused, the guard stays unknown and queued edits for that app say "Waiting to send" with no reason. They should move to "Not sent" with the refusal sentence.
- A 401 from NoGantt or NoBoard goes to "Not sent" without "Apply my change again"; once the sign-in works again the edit has to be made again by hand.
- After an agent label switch on a card, "The old agent was put back." is shown without checking that the put-back worked.
- A 412 on a create reads the item first; with no connection at that moment the answer should be "Offline", not "An item with this id already exists", and the sentence says "Open in NoCalendar" for to-dos too.
- Without a known project owner the agent's first visible planning person is used without checking that it belongs to the project (the server then drops it and the answer check says "Check this task in NoGantt"). Safer: refuse with "Ask your project manager ..." and log it.
- The times of NoGantt tasks are read in the person's time zone, not in NoGantt's own `settings.timezone`.

### Server-side items found in this step (for the Workspace session; nothing here was changed on any server)

1. NoGantt: let a viewer change the progress of their own task, or say so in the API (today `PUT tasks/{id}` and `progress/sync` need editor rights).
2. NoGantt and Deck: a conditional write (`If-Match` or an `updated_at` parameter) would close the small read-write race (risk B2).
3. NoGantt: an API that adds or removes one person of a task without replacing the whole list would make "my agent" safe for tasks whose agent rows hold work.
4. NoGantt: a readable version for normal users (the version guard is a shape check today).
5. The sync step of design section 11 (agents app, planning people, board labels) still decides how often "Ask your project manager to add ..." appears.

## The Timeline (third step)

Plan: `plans/noagenda-desktop-plan.md`, section 3. The header has a fourth view, **Timeline** (key `L`), next to Day, Week
and Month. The chips (Me, My agents), the All / Work / Personal switch and the Conflicts filter apply to it too. My team
is not drawn here (the Timeline is about my own work).

### What the person sees

- **Bars** for my NoGantt tasks across all projects, grouped under their **project** and **phase** (NoGantt subproject),
  with **% done filling the bar**. My to-dos that have both a start and a due date are bars too (not repeating ones, which
  are changed in NoTasks), grouped under "NoTasks > list name".
- **Diamonds** for the milestones of the projects where I have tasks, and for my dated cards (grouped under
  "NoBoard > board name").
- **The agent** after every bar and diamond (robot marker and name, or the person marker and "Self"), the late and blocked
  marks, and "Waiting to send" / "Not sent" for a move that has not reached the server.
- **Dependency arrows**, read only, for the four NoGantt types (finish to start `EA`, start to start `AA`, finish to finish
  `EE`, start to finish `AE`). Hover shows "First to Second (Finish to start), lag 2 days". A predecessor that is not on the
  screen (somebody else's task, or hidden by a chip) is a short stub into the bar with the tooltip "After: Banner design
  (Sara)".
- **My busy time shaded** behind the bars: each day is tinted by the share of my working hours that my own busy events
  take (0 to 35 % of the primary colour); days off (weekend, holiday, all-day busy event) are hatched. Today has a line.
  An agent's events and "free" events never shade the day.
- **Weeks** (12 px a day) and **Days** (40 px a day). The range is from 7 days before the date shown to 8 weeks after it;
  the arrows of the header move it by four weeks, **Today** comes back and scrolls to today.
- A fixed column of names on the left, the scale (months, weeks or days) on top, the chart scrolls sideways and down
  (scroll bars, mouse wheel and touchpad; a finger on a touch screen).

### Moving a bar

- **Drag** a bar (mouse or pen) to move start and end together by whole days. A ghost bar shows the new dates
  ("31 Oct - 2 Nov") while the bar is held; **Esc** cancels; release saves. Less than half a day moves nothing (0.49 stays,
  0.51 moves). The detail panel's date editor does the same through the same rules.
- **Keyboard** (a bar has the focus): Left and Right move it one day, Shift seven days, **Enter** saves, **Esc** cancels.
- The drop goes through the **edit step**, not around it: `EditController::moveByDays` builds the same `ganttUpdate` /
  `calendarPatch` command, so the rules, the offline queue, the conflict handling ("Apply my change again") and **Undo**
  are the editor's. A NoGantt task sends `start_date` and `end_date` (the time of day NoGantt had is kept), `adjust_lag=true`
  (NoGantt's own drag behaviour) and `deck_confirmed=false`. The toast says "Moved to 2 Nov - 4 Nov. Also moved: ..." and
  offers **Undo** for 10 seconds. The dates in the sentence are the ones **NoGantt saved** (read from its answer), not the
  dates of the drop: NoGantt puts a moved task on working days and works out its end from its length in working days, so a
  drop across a weekend is saved on other days. When they differ the toast adds "NoGantt kept it on working days.". (The
  `move` object in the command holds the dates of the drop, so the sentence is also right for a move that waited offline or
  was merged with a second move of the same bar.) A to-do patches `DTSTART` and `DUE` and keeps their times of day.
- **Locked bars** (a project I only view, a read-only calendar, the version guard, a change that is in "Not sent") show a
  lock, do not move, and say why in a message. No command is created for them. Milestones and cards are not dragged (a
  card's due date is in the detail panel, a milestone is changed in NoGantt).
- Nothing in the Timeline deletes anything, and a NoGantt task or card is never created or deleted from it.

### Where the code is

| File (all under `src/gui/nourax/noagenda/`) | Job |
|---|---|
| `logic/timelinelayout.h/.cpp` | The pure maths: date to x and back, groups, rows, rectangles, arrow routes, shading, drag to dates |
| `ui/timelinecontroller.h/.cpp` | What QML needs: rows with geometry, scale, arrows, shading, zoom, ghost, drop |
| `ui/editcontroller.*` | `moveByDays` (the one new entry of the edit step) and the "Moved to ..." sentence (`movedText`, written from the dates the server saved) |
| `model/agendaitem.h`, `logic/ganttmapper.cpp`, `logic/calendarmapper.cpp` | The row now carries its phase, its dependencies (type, lag, title and owner of the predecessor) and the start time of a to-do |
| `qml/Timeline*.qml`, `qml/DependencyArrows.qml`, `qml/BusyShading.qml` | The view, the bars, the diamonds, the arrows (scene-graph rectangles, `DependencyArrows.qml`), the shading, the scale, the legend |

No new library and **no new line in an upstream file**: the three small hooks of the core step are the only ones.

### Choices where the plan was silent (builder's calls)

- The chart `Flickable` is not interactive (the mouse belongs to the bars); wheel, touchpad, scroll bars and a touch-only
  `DragHandler` scroll it. A `DragHandler` with `target: null` measures the distance from the press in scene pixels
  (`centroid.scenePosition - centroid.scenePressPosition`), so the drag threshold counts too. It accepts mouse and pen only.
- One row per item (a group header also has a row). Items without a phase come first in start order, then the phases by their
  first start.
- The model and the controller of the plan are one class (`TimelineController`): the rows are small maps for QML.
- Done tasks are not drawn (the core step does not read them as my work).
- A move of a to-do changes `DTSTART` to a date-time only when it already was one.
- Context7 was out of quota during this step ("Monthly quota reached"), so the Qt Quick items used (`DragHandler`,
  `HoverHandler`, `TapHandler`, `WheelHandler`, `Flickable`; `Canvas` for the hatching only) were taken from the official Qt 6 documentation as
  known from the plan's earlier check; the UI tests drive them with real mouse and key events.

### Owner test rows (Timeline, real computers, plain English)

1. Open NoAgenda, choose **Timeline**: your NoGantt tasks are bars under their project and phase, the % done fills the bar, and the agent (or Self) is next to each bar.
2. Drag a bar two days to the right: a ghost bar shows the new dates, and when you let go NoGantt has the new dates and the toast says "Moved to ... Also moved: ..." if other tasks moved. Press **Undo** within 10 seconds: everything is back.
3. Start a drag and press **Esc**: nothing is saved.
4. Click a bar, press Right and then Enter: the bar moves by one day.
5. In a project where you are only a viewer, the bars show a lock, do not move, and say why.
6. The arrows match the dependencies you see in NoGantt; a task that waits for somebody else's task shows a short line with "After: ... (name)" on hover.
7. A day with a long meeting is shaded; weekends and holidays are hatched.
8. Switch the Wi-Fi off and drag a bar: "Waiting to send" shows next to it; after switching it on the change reaches NoGantt.

### Open points of the Timeline step

- A refresh that comes while a bar is held (the 5-minute refresh, or another saved edit) waits: the chart is rebuilt when the bar
  is dropped or the drag is cancelled, so the drag is not lost. Switching to another view lets the bar go.
- The ghost shows whole-day moves only; NoGantt may place the result on working days (`adjust_lag`). The toast then names the
  dates that were saved and adds "NoGantt kept it on working days."; the bar jumps to them at the next refresh.
- The hatching of days off is drawn with one small `Canvas` per day (painted again when the theme switches); with very many rows
  this is memory-heavy (about 4 bytes per pixel of column).
- A NoGantt task that has only an end date is drawn as a one-day bar with a lock ("This task has no dates to move."): it can't be dragged.
- "My team" and "Show all team tasks" are dimmed on the Timeline, because team work is not drawn there.
- The agent label leaves out the robot emoji that a planning person's name starts with (the label draws its own robot icon).
- Dependencies of a milestone (milestone as the successor) are not drawn: only tasks have arrows into them.
- Texts are English; translations are a to-do ("Timeline", "Weeks", "Days", "After: %1 (%2)", "No dated work in this
  range.", "%1 - %2", "Moved to %1 - %2", "lag %1 days" and the legend).

## Known limits of the core step

- No account picker in the header: the tray entry opens the window of the current account (one window per account).
- No separate date picker for Day and Week (previous, next, Today); Month has a small calendar for jumping.
- Views outside the sync window (previous month to 12 months ahead) are not read live and not marked
  "Not saved for offline use" yet.
- `calendar-multiget` and `sync-collection` are not used: a calendar whose token changed is read in full.
- Repeating to-dos are not expanded (the server drops every VTODO on expand): a recurring to-do shows only its first
  due date and may stay "late".
- A symbol that the server puts in a title (for example the birthday cake) is drawn as an empty box where the system
  has no emoji font. Text from the server is shown as it is.
- Rule 4.5 (b) recognises an agent by its exact name even when its person is not on the task (design 4.5 would call that
  "unknown agent"); the result is only a flag ("belongs to Sara, not Ali"), nobody's work changes.
- A person is not told about an agent of theirs that sits on a task in a project that is Hidden by default (a project
  where they have no task of their own): its tasks are not read, so only the project's manager is told.
- A single calendar that keeps failing keeps its old records (no growth) but still shows as updated; the banner does
  not name that calendar yet.
- In My team, a NoBoard card with several assignees words its row by the first assignee only.
- Rule 4.5 (b) accepts either the person's display name or the name of the person's planning person
  when it checks the exact name of an agent's planning person (a little wider than "the person's display name").

## Open questions for the owner

1. **"My organisations"** is the set of organisations of my own agents (the registry has no person-to-organisation
   link yet). A manager who has no agent of their own therefore sees "Agent of Sara" instead of "Design-Bot" in My team,
   and the problem sentences say "Another organisation's agent". It is correct privacy-wise (nothing leaks) but less
   useful. Fix when the agents app can say which organisation a person belongs to.
2. **To-dos in a shared (Team) list** without an organiser or attendee show as my own work (design 6.2 taken literally:
   "to-dos with a due or start date in lists I keep visible"). Design 6.1 and anti-goal 1 say a team calendar should show
   only what I organise or am invited to. Events follow 6.1 today; to-dos follow 6.2. Say which one you want and the
   other will be changed.
- A card or task time is a clash only when both ends are on one day; a longer span is a due or end date only.
- The time of a NoGantt task is read as the time of the person's time zone (NoGantt stores no zone).
- A new card due date keeps the card's time of day; a card without a time is due at local midnight.
- The computer's own Calendar and Contacts apps (step 4) are not here.
- The new texts are English; translations are a to-do.

## Server-side items (not done here; for the Workspace session)

See the notes at the end of `plans/noagenda-desktop-plan.md` (section 6): the Nourax agents app and its per-user
answer, the registry sync step (NoGantt planning people, board labels), the Workspace AI tools (person first, then
agent), a readable NoGantt version for normal users, and running these tests before updating NoGantt or Deck.
