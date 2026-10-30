# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoDrive: Nourax-only features. New logic lives in this folder so that upstream
# merges stay easy; src/gui/CMakeLists.txt only includes this file.
#
# Problem reports (plans/round3-contract.md, section B).
target_sources(nextcloudCore PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/problemreportsettings.h
    ${CMAKE_CURRENT_LIST_DIR}/problemreportsettings.cpp
    ${CMAKE_CURRENT_LIST_DIR}/problemreportscrubber.h
    ${CMAKE_CURRENT_LIST_DIR}/problemreportscrubber.cpp
    ${CMAKE_CURRENT_LIST_DIR}/runmarker.h
    ${CMAKE_CURRENT_LIST_DIR}/runmarker.cpp
    ${CMAKE_CURRENT_LIST_DIR}/problemreportbuilder.h
    ${CMAKE_CURRENT_LIST_DIR}/problemreportbuilder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/problemreportsharejob.h
    ${CMAKE_CURRENT_LIST_DIR}/problemreportsharejob.cpp
    ${CMAKE_CURRENT_LIST_DIR}/problemreportuploader.h
    ${CMAKE_CURRENT_LIST_DIR}/problemreportuploader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/problemreportmanager.h
    ${CMAKE_CURRENT_LIST_DIR}/problemreportmanager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/problemreportdialog.h
    ${CMAKE_CURRENT_LIST_DIR}/problemreportdialog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/problemreportspanel.h
    ${CMAKE_CURRENT_LIST_DIR}/problemreportspanel.cpp
)

# NoAgenda (plans/noagenda-decisions.md): the one screen with everything a person is responsible for.
include(${CMAKE_CURRENT_LIST_DIR}/noagenda/noagenda.cmake)

# Device calendar and contacts sync (plans/noagenda-desktop-plan.md, section 4).
include(${CMAKE_CURRENT_LIST_DIR}/devicesync/devicesync.cmake)
