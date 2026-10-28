# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoDrive: NoAgenda (plans/noagenda-decisions.md, plans/noagenda-desktop-plan.md).
# Included from src/gui/nourax/nourax.cmake. New code only; no upstream file lists these sources.
set(NOAGENDA_DIR ${CMAKE_CURRENT_LIST_DIR})

target_sources(nextcloudCore PRIVATE
    ${NOAGENDA_DIR}/model/agendaitem.h
    ${NOAGENDA_DIR}/model/agendaitem.cpp
    ${NOAGENDA_DIR}/model/agendacolors.h
    ${NOAGENDA_DIR}/model/agendacolors.cpp
    ${NOAGENDA_DIR}/net/httptransport.h
    ${NOAGENDA_DIR}/net/httptransport.cpp
    ${NOAGENDA_DIR}/net/accounttransport.h
    ${NOAGENDA_DIR}/net/accounttransport.cpp
    ${NOAGENDA_DIR}/registry/agentregistry.h
    ${NOAGENDA_DIR}/registry/agentregistry.cpp
    ${NOAGENDA_DIR}/registry/registrysource.h
    ${NOAGENDA_DIR}/registry/driveregistrysource.h
    ${NOAGENDA_DIR}/registry/driveregistrysource.cpp
    ${NOAGENDA_DIR}/registry/registrysourcefactory.cpp
    ${NOAGENDA_DIR}/ical/icaltext.h
    ${NOAGENDA_DIR}/ical/icaltext.cpp
    ${NOAGENDA_DIR}/ical/icaltime.h
    ${NOAGENDA_DIR}/ical/icaltime.cpp
    ${NOAGENDA_DIR}/caldav/caldavxml.h
    ${NOAGENDA_DIR}/caldav/caldavxml.cpp
    ${NOAGENDA_DIR}/caldav/calendardata.h
    ${NOAGENDA_DIR}/caldav/calendardata.cpp
    ${NOAGENDA_DIR}/caldav/calendarreader.h
    ${NOAGENDA_DIR}/caldav/calendarreader.cpp
    ${NOAGENDA_DIR}/ocs/ocsjson.h
    ${NOAGENDA_DIR}/ocs/ocsjson.cpp
    ${NOAGENDA_DIR}/ocs/serverinfo.h
    ${NOAGENDA_DIR}/ocs/serverinfo.cpp
    ${NOAGENDA_DIR}/gantt/ganttdata.h
    ${NOAGENDA_DIR}/gantt/ganttdata.cpp
    ${NOAGENDA_DIR}/gantt/ganttcontract.h
    ${NOAGENDA_DIR}/gantt/ganttcontract.cpp
    ${NOAGENDA_DIR}/gantt/ganttclient.h
    ${NOAGENDA_DIR}/gantt/ganttclient.cpp
    ${NOAGENDA_DIR}/deck/deckclient.h
    ${NOAGENDA_DIR}/deck/deckclient.cpp
    ${NOAGENDA_DIR}/logic/sourcesettings.h
    ${NOAGENDA_DIR}/logic/sourcesettings.cpp
    ${NOAGENDA_DIR}/logic/buildcontext.h
    ${NOAGENDA_DIR}/logic/buildcontext.cpp
    ${NOAGENDA_DIR}/logic/calendarmapper.h
    ${NOAGENDA_DIR}/logic/calendarmapper.cpp
    ${NOAGENDA_DIR}/logic/ganttmapper.h
    ${NOAGENDA_DIR}/logic/ganttmapper.cpp
    ${NOAGENDA_DIR}/logic/deckmapper.h
    ${NOAGENDA_DIR}/logic/deckmapper.cpp
    ${NOAGENDA_DIR}/logic/agendabuilder.h
    ${NOAGENDA_DIR}/logic/agendabuilder.cpp
    ${NOAGENDA_DIR}/store/agendacache.h
    ${NOAGENDA_DIR}/store/agendacache.cpp
    ${NOAGENDA_DIR}/accountagenda.h
    ${NOAGENDA_DIR}/accountagenda.cpp
    ${NOAGENDA_DIR}/agendaservice.h
    ${NOAGENDA_DIR}/agendaservice.cpp
    ${NOAGENDA_DIR}/ui/agendastyle.h
    ${NOAGENDA_DIR}/ui/agendacontroller.h
    ${NOAGENDA_DIR}/ui/agendacontroller.cpp
    ${NOAGENDA_DIR}/ui/noagendawindow.h
    ${NOAGENDA_DIR}/ui/noagendawindow.cpp
    ${NOAGENDA_DIR}/ui/noagendalauncher.h
    ${NOAGENDA_DIR}/ui/noagendalauncher.cpp
)

# The QML files of the window, as a Qt resource (initialised by initNoAgendaResources()).
qt_add_resources(NOAGENDA_QRC_SRCS ${NOAGENDA_DIR}/noagenda.qrc)
target_sources(nextcloudCore PRIVATE ${NOAGENDA_QRC_SRCS})
