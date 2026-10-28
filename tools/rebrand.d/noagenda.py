"""Shared plugin for tools/rebrand.py: the small NoDrive hooks of NoAgenda (agenda-core).

All new logic lives in src/gui/nourax/noagenda/ (compiled by src/gui/nourax/nourax.cmake, so
src/gui/CMakeLists.txt gets no new line). The upstream files only get a few marked lines
("NoDrive: NoAgenda"). They are applied here, so that "rebrand.py --check" fails loudly when an
upstream merge drops one (plans/noagenda-desktop-plan.md, 1.2).

Every anchor is an upstream line that no other plugin uses.
"""

LANE = "shared"


def apply(ctx):
    # --- the tray menu of Windows and Linux (a Qt popup menu) --------------------------------------
    ctx.replace(
        "src/gui/trayaccountpopup_qt.cpp",
        '#include "tray/usermodel.h"\n',
        '#include "tray/usermodel.h"\n#include "nourax/noagenda/ui/noagendalauncher.h" // NoDrive: NoAgenda\n',
        what="NoAgenda: include the launcher (Qt tray popup)",
    )
    ctx.replace(
        "src/gui/trayaccountpopup_qt.cpp",
        "    if (systray->enableAddAccount()) {\n",
        "    if (userModel && userModel->rowCount() > 0) { // NoDrive: NoAgenda\n"
        "        const auto noAgendaAction = addMenuAction(menu, templateThemeIcon(QStringLiteral(\"calendar.svg\"), menuIconSize, menuIconPalette), Nourax::Agenda::NoAgendaLauncher::trayEntryText()); // NoDrive: NoAgenda\n"
        "        QObject::connect(noAgendaAction, &QAction::triggered, noAgendaAction, [] { closeTrayPopup(); Nourax::Agenda::NoAgendaLauncher::openForCurrentUser(); }); // NoDrive: NoAgenda\n"
        "    } // NoDrive: NoAgenda\n"
        "\n"
        "    if (systray->enableAddAccount()) {\n",
        what="NoAgenda: the tray menu entry (Qt tray popup)",
    )

    # --- the tray popup of macOS (AppKit) -----------------------------------------------------------
    ctx.replace(
        "src/gui/macOS/trayaccountpopup/nctraypopup.mm",
        '#include "tray/usermodel.h"\n',
        '#include "tray/usermodel.h"\n#include "nourax/noagenda/ui/noagendalauncher.h" // NoDrive: NoAgenda\n',
        what="NoAgenda: include the launcher (macOS popup)",
    )
    ctx.replace(
        "src/gui/macOS/trayaccountpopup/nctraypopup.mm",
        "    __unsafe_unretained NCTrayPopup *weakSelf = self;\n    if (OCC::Systray::instance()->enableAddAccount()) {\n",
        "    __unsafe_unretained NCTrayPopup *weakSelf = self;\n"
        "    if (model->rowCount() > 0) { // NoDrive: NoAgenda\n"
        "        addOwnedArrangedSubview(_stack, [[NCActionRow alloc] initWithTitle:OCC::Nourax::Agenda::NoAgendaLauncher::trayEntryText().toNSString() width:kPopupWidth enabled:YES action:^{ // NoDrive: NoAgenda\n"
        "            [weakSelf orderOut:nil]; // NoDrive: NoAgenda\n"
        "            OCC::Systray::instance()->setIsOpen(false); // NoDrive: NoAgenda\n"
        "            OCC::Nourax::Agenda::NoAgendaLauncher::openForCurrentUser(); // NoDrive: NoAgenda\n"
        "        } hoverAction:^(NSView *) { // NoDrive: NoAgenda\n"
        "            [weakSelf closeAccountActionsPopup]; // NoDrive: NoAgenda\n"
        "        }]); // NoDrive: NoAgenda\n"
        "    } // NoDrive: NoAgenda\n"
        "    if (OCC::Systray::instance()->enableAddAccount()) {\n",
        what="NoAgenda: the tray menu entry (macOS popup)",
    )

    # --- the right-click menu of Windows -------------------------------------------------------------
    ctx.replace(
        "src/gui/systray.cpp",
        '#include "wheelhandler.h"\n',
        '#include "wheelhandler.h"\n#include "nourax/noagenda/ui/noagendalauncher.h" // NoDrive: NoAgenda\n',
        what="NoAgenda: include the launcher (systray)",
    )
    ctx.replace(
        "src/gui/systray.cpp",
        '    auto pauseAction = _contextMenu->addAction(tr("Pause sync"), this, &Systray::slotPauseAllFolders);\n',
        '    Nourax::Agenda::NoAgendaLauncher::addContextMenuEntry(_contextMenu); // NoDrive: NoAgenda\n'
        '    auto pauseAction = _contextMenu->addAction(tr("Pause sync"), this, &Systray::slotPauseAllFolders);\n',
        what="NoAgenda: the entry of the right-click menu",
    )

    # --- tests ------------------------------------------------------------------------------------------
    ctx.replace(
        "test/CMakeLists.txt",
        "nextcloud_add_test(SettingsDialog)\n",
        "include(${CMAKE_CURRENT_SOURCE_DIR}/nodrive/noagenda.cmake) # NoDrive: NoAgenda\n"
        "nextcloud_add_test(SettingsDialog)\n",
        what="NoAgenda: register the tests (test/nodrive/noagenda.cmake)",
    )
