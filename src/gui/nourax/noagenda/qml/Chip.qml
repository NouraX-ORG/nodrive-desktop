// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// A filter chip: on = filled with the primary colour.

import QtQuick

Rectangle {
    id: c

    required property var st
    property string label: ""
    property string glyph: ""
    property bool on: true
    signal toggled(bool value)

    implicitHeight: 30
    implicitWidth: row.implicitWidth + 24
    radius: height / 2
    color: on ? st.primary : st.surface
    border.width: on ? 0 : 1
    border.color: st.border

    Accessible.role: Accessible.CheckBox
    Accessible.name: label
    Accessible.checked: on
    Accessible.onToggleAction: c.toggled(!c.on)
    activeFocusOnTab: true
    Keys.onSpacePressed: c.toggled(!c.on)
    Keys.onReturnPressed: c.toggled(!c.on)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Glyph {
            visible: c.glyph !== ""
            anchors.verticalCenter: parent.verticalCenter
            kind: c.glyph
            tone: c.on ? c.st.onPrimary : c.st.textMuted
            size: 14
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: c.label
            color: c.on ? c.st.onPrimary : c.st.text
            font.pixelSize: 13
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: c.activeFocus ? 2 : 0
        border.color: c.st.warning
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: c.toggled(!c.on)
    }
}
