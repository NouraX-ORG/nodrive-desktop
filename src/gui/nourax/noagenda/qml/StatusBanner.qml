// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick

Rectangle {
    id: b

    required property var st
    property var info: ({})
    signal actionRequested(string action)

    implicitHeight: Math.max(38, textRow.implicitHeight + 16)
    color: info.kind === "warn" ? Qt.rgba(st.warning.r, st.warning.g, st.warning.b, st.dark ? 0.18 : 0.12) : st.primarySoft
    border.width: 1
    border.color: info.kind === "warn" ? st.warning : st.primary

    Accessible.role: Accessible.AlertMessage
    Accessible.name: info.text ? info.text : ""

    Row {
        id: textRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        Glyph {
            anchors.verticalCenter: parent.verticalCenter
            kind: "alert"
            tone: b.info.kind === "warn" ? b.st.warning : b.st.primary
            size: 16
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 26 - (actionButton.visible ? actionButton.width + 10 : 0)
            text: b.info.text ? b.info.text : ""
            wrapMode: Text.Wrap
            color: b.st.text
            font.pixelSize: 13
        }
        FlatButton {
            id: actionButton
            anchors.verticalCenter: parent.verticalCenter
            visible: b.info.action !== undefined && b.info.action !== ""
            st: b.st
            label: b.info.actionText ? b.info.actionText : ""
            onClicked: b.actionRequested(b.info.action)
        }
    }
}
