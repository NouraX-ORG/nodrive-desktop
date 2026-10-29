// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// A milestone of a project or a dated card of a board: a diamond on its day. It is not dragged (its date is changed in
// the detail panel, or in NoGantt for a milestone).

import QtQuick
import QtQuick.Controls

Item {
    id: d

    required property var st
    required property var info
    required property var ctrl
    readonly property bool selected: ctrl.selectedKey === info.key

    x: info.x
    y: info.y
    width: info.w
    height: info.h
    objectName: "diamond-" + info.key
    activeFocusOnTab: true
    Keys.onReturnPressed: ctrl.select(info.key)

    Accessible.role: Accessible.Button
    Accessible.name: info.title + ", " + info.dates + ", " + info.agentText

    Rectangle {
        anchors.centerIn: parent
        width: 11
        height: 11
        rotation: 45
        color: d.info.color
        border.width: d.selected || d.activeFocus ? 2 : 1
        border.color: d.selected || d.activeFocus ? d.st.text : Qt.darker(d.info.color, 1.3)
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.right
        anchors.leftMargin: 6
        spacing: 6
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: d.info.title
            color: d.st.text
            font.pixelSize: 11
        }
        AgentLabel {
            visible: d.info.glyph === "card"
            st: d.st
            text: d.info.agentText
            agentKind: d.info.agentKind
            pixelSize: 11
        }
    }

    HoverHandler {
        id: hover
    }
    ToolTip.visible: hover.hovered
    ToolTip.delay: 500
    ToolTip.text: d.info.title + "\n" + d.info.dates

    TapHandler {
        onTapped: {
            d.forceActiveFocus();
            d.ctrl.select(d.info.key);
        }
    }
}
