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
    # File name: <AppName>-<version>-<build>-x86_64.AppImage (plan section 2.3)
    # instead of upstream's <executable>-<branch>-<commit>-x86_64.AppImage.
    # MIRALL_HUMAN_VERSION_STRING is the version without " (build N)".
    ctx.replace(
        SCRIPT,
        "#move AppImage\n"
        "export COMMIT=${GITHUB_SHA:=${DRONE_COMMIT}}\n"
        "if [ ! -z \"$COMMIT\" ]\n"
        "then\n"
        "    export APPIMAGE_NAME=\"${EXECUTABLE_NAME}-${SUFFIX}-${COMMIT}-x86_64.AppImage\"\n"
        "else\n"
        "    export APPIMAGE_NAME=\"${EXECUTABLE_NAME}-${SUFFIX}-x86_64.AppImage\"\n"
        "fi\n",
        "#move AppImage\n"
        "# NoDrive: <AppName>-<version>-<build>-x86_64.AppImage\n"
        "NODRIVE_VERSION=$(sed -n 's/^#define MIRALL_HUMAN_VERSION_STRING \"\\(.*\\)\"$/\\1/p' ${DESKTOP_CLIENT_ROOT}/build-client/version.h)\n"
        "export APPIMAGE_NAME=\"${APPNAME}-${NODRIVE_VERSION:-unknown}-${BUILDNR}-x86_64.AppImage\"\n",
        what="AppImage file name NoDrive-<version>-<build>-x86_64.AppImage",
    )
    # appimagetool (the "continuous" release, newer than the first NoDrive builds) refuses an AppDir whose
    # folders are not readable for everyone (the Qt qml folders are mode 0700). Everyone must be able to run the AppImage.
    ctx.replace(
        SCRIPT,
        './"${APPIMAGE}" --appimage-extract\nrm ./"${APPIMAGE}"\n',
        './"${APPIMAGE}" --appimage-extract\nrm ./"${APPIMAGE}"\n'
        "# NoDrive: appimagetool wants every file readable (and every folder enterable) for all users\n"
        "chmod -R a+rX ./squashfs-root\n",
        what="AppImage: files readable for all users (appimagetool permission check)",
    )
    # KDE plugin metadata of the Dolphin overlay plugin (shown in plugin lists).
    ctx.replace(
        "shell_integration/dolphin/ownclouddolphinoverlayplugin.json",
        '"Description": "Overlay icon for owncloud"',
        f'"Description": "Sync status icons for {name}"',
        what="Dolphin overlay plugin description",
    )
