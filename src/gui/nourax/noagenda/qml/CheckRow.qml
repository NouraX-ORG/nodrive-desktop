// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// A check box with its label.

import QtQuick

Row {
    id: c

    required property var st
    property string label: ""
    property bool checked: false
    signal toggled(bool value)

    spacing: 8
    Accessible.role: Accessible.CheckBox
    Accessible.name: label
    Accessible.checked: checked
    Accessible.onToggleAction: c.toggled(!c.checked)
    activeFocusOnTab: true
    Keys.onSpacePressed: c.toggled(!c.checked)

    Rectangle {
        width: 18
        height: 18
        radius: 4
        anchors.verticalCenter: parent.verticalCenter
        color: c.checked ? c.st.primary : c.st.surface
        border.width: 1
        border.color: c.checked ? c.st.primary : c.st.border
        Rectangle {
            visible: c.checked
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 2
            color: c.st.onPrimary
        }
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: c.label
        color: c.st.text
        font.pixelSize: 13
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: c.toggled(!c.checked)
    }
}
