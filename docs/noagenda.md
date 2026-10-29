# NoAgenda (desktop): the core, read only

Binding spec: `plans/noagenda-decisions.md` (approved 2026-10-28). Platform plan: `plans/noagenda-desktop-plan.md`.
This page describes what is built in the first step ("NoAgenda core") and how it is tested. Editing, conflicts,
the Timeline and the computer's own Calendar and Contacts apps come in the next steps.

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
- Nothing can be changed in this step. The detail panel has one action: **Open in NoCalendar / NoTasks / NoGantt /
  NoBoard** (the default browser, the account's own server).

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
| `NoDriveAgendaServer` | the real throwaway server (Linux CI job `agenda-server`): `auth-proof`, `contract`, `read-ali`, `read-mara`, `read-sara`, `read-olaf`, `guard` |

The job `agenda-server` (`tools/ci/agenda-server.sh`, `tools/ci/agenda-seed.py`) starts the pinned
`nextcloud:35.0.2-apache` and installs the pinned apps, with their SHA-256 checked: calendar 6.6.4, tasks 0.18.1,
Deck 1.19.1 and **nc_gantt 2.4.1** (release `v2.4.1-9a09070`, SHA-256
`9003904a4398f94f1870cc93e6a129d6c7e72298de05b3c5d0104efa976bcfb9`). The server contacts no app store. Users: Mara
(manager) and Ali and Sara (staff) in `org-a`, Olaf in `org-b`. The throwaway passwords are made at run time, masked
and never written to the artifact (the job greps for them).

## Auth proof (design section 5)

Measured in the job `agenda-server` (Linux run [40804574745](https://github.com/NouraX-ORG/nodrive-desktop/actions/runs/40804574745), commit `654db7e45e`,
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

## Known limits of this step

- No account picker in the header: the tray entry opens the window of the current account (one window per account).
- No separate date picker for Day and Week (previous, next, Today); Month has a small calendar for jumping.
- Views outside the sync window (previous month to 12 months ahead) are not read live and not marked
  "Not saved for offline use" yet.
- `calendar-multiget` and `sync-collection` are not used: a calendar whose token changed is read in full.
- Repeating to-dos are not expanded (the server drops every VTODO on expand): a recurring to-do shows only its first
  due date and may stay "late".
- A symbol that the server puts in a title (for example the birthday cake) is drawn as an empty box where the system
  has no emoji font. Text from the server is shown as it is.
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
- Edit, conflicts, the workload bar and the clash badge (design steps 2 and 3) are not here.
- The new texts are English; translations are a to-do.

## Server-side items (not done here; for the Workspace session)

See the notes at the end of `plans/noagenda-desktop-plan.md` (section 6): the Nourax agents app and its per-user
answer, the registry sync step (NoGantt planning people, board labels), the Workspace AI tools (person first, then
agent), a readable NoGantt version for normal users, and running these tests before updating NoGantt or Deck.
