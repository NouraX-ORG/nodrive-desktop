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
  NoDrive runs, but only for accounts where NoAgenda was opened at least once.
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
| `accountagenda.*`, `agendaservice.*` | one account's readers, merge, status and back-off; the 5 minute timer |
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
  again. A calendar that fails keeps its old objects.
- **Registry**: GET of `remote.php/dav/files/<user>/NoAgenda/agent-registry.json` with `If-None-Match`.

If an endpoint refuses on a real server, that home turns into an "Open in ..." row and the status code is logged; no
credential ever goes to another host.

## Rules that are tested

- Defaults of design 6.1 (own calendars Personal until the question, shared Team, subscriptions and birthdays Info,
  board calendars and NoGantt's task copy hidden automatically, projects and boards by relation, server admins).
- Event rules: free events do not block time, declined invitations are hidden, tentative ones are soft, a shared team
  calendar shows only what I organise or am invited to, an invitation is always Self, the same invitation in two
  calendars is one row.
- Agents (4.2, 4.5): `X-NOURAX-AGENT-ID`, the registry `calendar_tag`, board labels, and NoGantt planning people
  (recognised by the registry's `nogantt_resource_ids`, or by the exact name `<robot> <agent> (<person>)`). Anything
  else is "unknown agent" and flagged, never guessed. An agent's hours never count for its person.
- Integrity flags (4.4): agent with no person, agent paired with someone else, two agents, unknown or retired agent,
  not linked to an account, not assigned to a person, a person in a project not shared with them. Each flag goes to its
  audience (the person, the manager, or both).
- Privacy between organisations: another person's agent is named only if its organisation is one of mine.
- Offline cache: no password, token or cookie is ever written; the cache folder is removed with the account (only
  `<config>/noagenda/<account>`, never anything else).

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
| `NoDriveAgendaRegistry`, `...ICal`, `...CalDav`, `...Gantt`, `...Deck`, `...Rules`, `...Cache` | unit tests with the fixtures in `test/nodrive/noagenda/` and a fake transport (all three systems) |
| `NoDriveAgendaUi` | the window in a QML engine, light and dark, screenshots in `nodrive-screenshots/noagenda/` (Day, Week, Month, My team, Sources, the personal-calendars question, detail, not linked, offline, registry missing); pixel checks of the background and the selected chip; no text with the server software's name |
| `NoDriveAgendaServer` | the real throwaway server (Linux CI job `agenda-server`): `auth-proof`, `contract`, `read-ali`, `read-mara`, `read-sara`, `read-olaf`, `guard` |

The job `agenda-server` (`tools/ci/agenda-server.sh`, `tools/ci/agenda-seed.py`) starts the pinned
`nextcloud:35.0.2-apache` and installs the pinned apps, with their SHA-256 checked: calendar 6.6.4, tasks 0.18.1,
Deck 1.19.1 and **nc_gantt 2.4.1** (release `v2.4.1-9a09070`, SHA-256
`9003904a4398f94f1870cc93e6a129d6c7e72298de05b3c5d0104efa976bcfb9`). The server contacts no app store. Users: Mara
(manager) and Ali and Sara (staff) in `org-a`, Olaf in `org-b`. The throwaway passwords are made at run time, masked
and never written to the artifact (the job greps for them).

## Server-side items (not done here; for the Workspace session)

See the notes at the end of `plans/noagenda-desktop-plan.md` (section 6): the Nourax agents app and its per-user
answer, the registry sync step (NoGantt planning people, board labels), the Workspace AI tools (person first, then
agent), a readable NoGantt version for normal users, and running these tests before updating NoGantt or Deck.
