// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick

Rectangle {
    id: b

    required property var st
    property string label: ""
    property bool primary: false
    property bool flat: false
    signal clicked

    implicitWidth: text.implicitWidth + 24
    implicitHeight: 30
    radius: 6
    color: primary ? (area.pressed ? Qt.darker(st.primary, 1.15) : st.primary)
                   : (area.containsMouse ? st.hover : (flat ? "transparent" : st.surface))
    border.width: (primary || flat) ? 0 : 1
    border.color: st.border

    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.onPressAction: b.clicked()
    activeFocusOnTab: true
    Keys.onReturnPressed: b.clicked()
    Keys.onSpacePressed: b.clicked()

    Text {
        id: text
        anchors.centerIn: parent
        text: b.label
        color: b.primary ? b.st.onPrimary : b.st.text
        font.pixelSize: 13
    }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: b.activeFocus ? 2 : 0
        border.color: b.st.primary
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: b.clicked()
    }
}
