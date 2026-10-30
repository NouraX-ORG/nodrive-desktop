# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoDrive: device calendar and contacts sync (plans/noagenda-desktop-plan.md, section 4). Included from
# src/gui/nourax/nourax.cmake.
target_sources(nextcloudCore PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/urlopener.h
    ${CMAKE_CURRENT_LIST_DIR}/appleprofilebuilder.h
    ${CMAKE_CURRENT_LIST_DIR}/appleprofilebuilder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/deviceapppasswordflow.h
    ${CMAKE_CURRENT_LIST_DIR}/deviceapppasswordflow.cpp
    ${CMAKE_CURRENT_LIST_DIR}/macprofileinstaller.h
    ${CMAKE_CURRENT_LIST_DIR}/macprofileinstaller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/devicesyncsettings.h
    ${CMAKE_CURRENT_LIST_DIR}/devicesyncsettings.cpp
    ${CMAKE_CURRENT_LIST_DIR}/devicesyncservice.h
    ${CMAKE_CURRENT_LIST_DIR}/devicesyncservice.cpp
    ${CMAKE_CURRENT_LIST_DIR}/thunderbirdguidedialog.h
    ${CMAKE_CURRENT_LIST_DIR}/thunderbirdguidedialog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/macsetupdialog.h
    ${CMAKE_CURRENT_LIST_DIR}/macsetupdialog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/devicesyncpanel.h
    ${CMAKE_CURRENT_LIST_DIR}/devicesyncpanel.cpp
)
