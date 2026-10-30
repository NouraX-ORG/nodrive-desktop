# SPDX-FileCopyrightText: 2026 Nourax
# SPDX-License-Identifier: GPL-2.0-or-later
#
# NoDrive: the tests of the device calendar and contacts sync (plans/noagenda-desktop-plan.md, 4.6). Included from
# test/CMakeLists.txt by a marked line (tools/rebrand.d/devicesync.py).
foreach(devicesync_test NoDriveDeviceSyncProfile NoDriveDeviceSyncFlow NoDriveDeviceSyncUi NoDriveDeviceSyncServer)
    nextcloud_add_test(${devicesync_test})
    target_compile_definitions(${devicesync_test}Test PRIVATE NODRIVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endforeach()
