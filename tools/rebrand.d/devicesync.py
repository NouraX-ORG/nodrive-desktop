"""Shared plugin for tools/rebrand.py: the small NoDrive hooks of the device calendar and contacts sync.

All new logic lives in src/gui/nourax/devicesync/ (compiled by src/gui/nourax/nourax.cmake, so
src/gui/CMakeLists.txt gets no new line). The upstream files only get two marked lines in
src/gui/accountsettings.cpp and one in test/CMakeLists.txt ("NoDrive: device calendar and contacts").
They are applied here, so that "rebrand.py --check" fails loudly when an upstream merge drops one
(plans/noagenda-desktop-plan.md, section 4.2).
"""

LANE = "shared"


def apply(ctx):
    # src/gui/accountsettings.cpp: the "Calendar and contacts" panel of the account page.
    ctx.replace(
        "src/gui/accountsettings.cpp",
        '#include "account.h"\n\nnamespace {\nconstexpr auto propertyFolder = "folder";\n',
        '#include "account.h"\n#include "nourax/devicesync/devicesyncpanel.h" // NoDrive: device calendar and contacts\n\nnamespace {\nconstexpr auto propertyFolder = "folder";\n',
        what="device sync: include the panel",
    )
    ctx.replace(
        "src/gui/accountsettings.cpp",
        "    connect(&_userInfo, &UserInfo::quotaUpdated,\n"
        "        this, &AccountSettings::slotUpdateQuota);\n"
        "\n"
        "    customizeStyle();\n",
        "    connect(&_userInfo, &UserInfo::quotaUpdated,\n"
        "        this, &AccountSettings::slotUpdateQuota);\n"
        "\n"
        "    Nourax::DeviceSync::DeviceSyncPanel::install(this, _accountState, _ui->verticalLayout_2, _ui->accountActionsPanel); // NoDrive: device calendar and contacts\n"
        "    customizeStyle();\n",
        what="device sync: add the panel before the account actions",
    )

    # test/CMakeLists.txt: the tests. The anchor is a line that no other plugin touches.
    ctx.replace(
        "test/CMakeLists.txt",
        "nextcloud_add_test(IconUtils)\n",
        "include(${CMAKE_CURRENT_SOURCE_DIR}/nodrive/devicesync.cmake) # NoDrive: device calendar and contacts\n"
        "nextcloud_add_test(IconUtils)\n",
        what="device sync: register the tests (test/nodrive/devicesync.cmake)",
    )
