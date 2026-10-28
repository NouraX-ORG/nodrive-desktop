"""Shared plugin for tools/rebrand.py: the small NoDrive hooks of the problem reports feature.

All new logic lives in src/gui/nourax/. The upstream files only get one marked line
each ("NoDrive: problem reports"). They are applied here, so that "rebrand.py --check"
fails loudly when an upstream merge drops one of them (plans/round3-desktop-plan.md, 1.2).
"""

LANE = "shared"


def apply(ctx):
    # src/gui/CMakeLists.txt: compile src/gui/nourax/ into nextcloudCore.
    ctx.replace(
        "src/gui/CMakeLists.txt",
        "  KDAB::kdsingleapplication\n  )\n",
        "  KDAB::kdsingleapplication\n  )\n"
        "\n"
        "include(${CMAKE_CURRENT_SOURCE_DIR}/nourax/nourax.cmake) # NoDrive: Nourax features (problem reports)\n",
        what="problem reports: include nourax/nourax.cmake",
    )

    # src/gui/application.cpp: start the run marker and the unclean-exit check.
    ctx.replace(
        "src/gui/application.cpp",
        '#include "version.h"\n',
        '#include "version.h"\n#include "nourax/problemreportmanager.h" // NoDrive: problem reports\n',
        what="problem reports: include the manager",
    )
    ctx.replace(
        "src/gui/application.cpp",
        "    connect(this, &QCoreApplication::aboutToQuit, this, &Application::slotCleanup);\n",
        "    connect(this, &QCoreApplication::aboutToQuit, this, &Application::slotCleanup);\n"
        "    Nourax::ProblemReportManager::instance()->start(); // NoDrive: problem reports\n",
        what="problem reports: start the manager after the clean-up hook",
    )

    # src/gui/advancedsettings.cpp: the switch and the "Report a problem" button.
    ctx.replace(
        "src/gui/advancedsettings.cpp",
        '#include "settingspanelstyle.h"\n',
        '#include "settingspanelstyle.h"\n#include "nourax/problemreportspanel.h" // NoDrive: problem reports\n',
        what="problem reports: include the panel",
    )
    ctx.replace(
        "src/gui/advancedsettings.cpp",
        "    connect(AccountManager::instance(), &AccountManager::capabilitiesChanged, this, &AdvancedSettings::updatePollIntervalVisibility);\n"
        "\n"
        "    customizeStyle();\n",
        "    connect(AccountManager::instance(), &AccountManager::capabilitiesChanged, this, &AdvancedSettings::updatePollIntervalVisibility);\n"
        "\n"
        "    Nourax::ProblemReportsPanel::install(this, _ui->pageLayout, _ui->advancedActionsGroupBox, _ui->advancedActionsLayout, _ui->debugArchiveButton); // NoDrive: problem reports\n"
        "    customizeStyle();\n",
        what="problem reports: add the switch and the button before styling",
    )

    # test/CMakeLists.txt: the tests.
    ctx.replace(
        "test/CMakeLists.txt",
        "nextcloud_add_test(NoDriveBranding) # NoDrive branding checks\n",
        "nextcloud_add_test(NoDriveBranding) # NoDrive branding checks\n"
        "nextcloud_add_test(NoDriveProblemReportScrubber) # NoDrive: problem reports\n"
        'target_compile_definitions(NoDriveProblemReportScrubberTest PRIVATE NODRIVE_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/test/nodrive/problemreports") # NoDrive: problem reports\n'
        "nextcloud_add_test(NoDriveProblemReport) # NoDrive: problem reports\n"
        "nextcloud_add_test(NoDriveProblemReportUpload) # NoDrive: problem reports\n"
        "nextcloud_add_test(NoDriveProblemReportUi) # NoDrive: problem reports\n"
        'target_compile_definitions(NoDriveProblemReportUiTest PRIVATE NODRIVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}") # NoDrive: problem reports\n'
        "nextcloud_add_test(NoDriveProblemReportServer) # NoDrive: problem reports\n",
        what="problem reports: register the tests",
    )
