"""Linux lane plugin for tools/rebrand.py: the AppImage build script."""

LANE = "linux"

SCRIPT = "admin/linux/build-appimage.sh"


def apply(ctx):
    c = ctx.ids["cmake"]
    name = c["APPLICATION_NAME"]
    exe = c["APPLICATION_EXECUTABLE"]
    icon = c["APPLICATION_ICON_NAME"]

    ctx.replace(
        SCRIPT,
        "export APPNAME=${APPNAME:-Nextcloud}\nexport EXECUTABLE_NAME=${EXECUTABLE_NAME:-nextcloud}\n",
        f"export APPNAME=${{APPNAME:-{name}}}\n"
        f"export EXECUTABLE_NAME=${{EXECUTABLE_NAME:-{exe}}}\n"
        f"export APPLICATION_ICON_NAME=${{APPLICATION_ICON_NAME:-{icon}}}\n",
        what="NoDrive defaults for the AppImage script",
    )
    # The hicolor icon is installed as ${APPLICATION_ICON_NAME}.png by src/gui/CMakeLists.txt.
    ctx.replace(
        SCRIPT,
        "--icon-file=usr/share/icons/hicolor/512x512/apps/Nextcloud.png",
        "--icon-file=usr/share/icons/hicolor/512x512/apps/${APPLICATION_ICON_NAME}.png",
        count=2,
        what="AppImage icon -> NoDrive.png",
    )
    # Licence texts and third-party notices travel with the AppImage (NOTICE.md).
    ctx.replace(
        SCRIPT,
        "mv usr/share/${EXECUTABLE_NAME} AppDir/usr/share/${EXECUTABLE_NAME}\n",
        "mv usr/share/${EXECUTABLE_NAME} AppDir/usr/share/${EXECUTABLE_NAME}\n"
        "\n"
        "# NoDrive: licence texts and third-party notices (see NOTICE.md)\n"
        "mkdir -p AppDir/usr/share/doc/${EXECUTABLE_NAME}\n"
        "cp ${DESKTOP_CLIENT_ROOT}/nodrive/licenses/* AppDir/usr/share/doc/${EXECUTABLE_NAME}/\n",
        what="ship nodrive/licenses in usr/share/doc",
    )
    # KDE plugin metadata of the Dolphin overlay plugin (shown in plugin lists).
    ctx.replace(
        "shell_integration/dolphin/ownclouddolphinoverlayplugin.json",
        '"Description": "Overlay icon for owncloud"',
        f'"Description": "Sync status icons for {name}"',
        what="Dolphin overlay plugin description",
    )
