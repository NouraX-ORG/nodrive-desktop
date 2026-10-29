# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoDrive: the tests of NoAgenda (plans/noagenda-desktop-plan.md, 1.9). Included from test/CMakeLists.txt
# by a marked line (tools/rebrand.d/noagenda.py). Fixtures: test/nodrive/noagenda/.
set(NODRIVE_AGENDA_TESTS
    NoDriveAgendaRegistry
    NoDriveAgendaICal
    NoDriveAgendaCalDav
    NoDriveAgendaGantt
    NoDriveAgendaDeck
    NoDriveAgendaRules
    NoDriveAgendaCache
    NoDriveAgendaService
    NoDriveAgendaICalPatch
    NoDriveAgendaEdit
    NoDriveAgendaGanttAgent
    NoDriveAgendaQueue
    NoDriveAgendaConflicts
    NoDriveAgendaUi
    NoDriveAgendaServer
)
foreach(agenda_test ${NODRIVE_AGENDA_TESTS})
    nextcloud_add_test(${agenda_test})
    target_compile_definitions(${agenda_test}Test PRIVATE
        NODRIVE_AGENDA_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/test/nodrive/noagenda"
        NODRIVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endforeach()
