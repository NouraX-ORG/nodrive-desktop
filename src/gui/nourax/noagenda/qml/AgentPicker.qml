// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// Who does it (design 4 and 9): the person first, then exactly one agent: Self or one of my agents. A choice that
// cannot be taken says why in plain words under its name (paused, not in this project yet, invited so always Self).

import QtQuick

Column {
    id: p

    required property var st
    property var choices: []
    property string current: ""
    signal picked(string id)

    spacing: 4

    Repeater {
        model: p.choices
        delegate: Rectangle {
            id: row
            required property var modelData
            objectName: "agentChoice-" + modelData.id
            width: p.width
            height: choiceColumn.height + 12
            radius: 6
            color: modelData.id === p.current ? p.st.selected : (area.containsMouse && modelData.enabled ? p.st.hover : "transparent")
            border.width: modelData.id === p.current ? 1 : 0
            border.color: p.st.primary
            opacity: modelData.enabled ? 1 : 0.65
            Accessible.role: Accessible.RadioButton
            Accessible.name: modelData.label
            Accessible.checked: modelData.id === p.current

            Column {
                id: choiceColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 2
                Row {
                    spacing: 8
                    Rectangle {
                        width: 14
                        height: 14
                        radius: 7
                        anchors.verticalCenter: parent.verticalCenter
                        color: "transparent"
                        border.width: 1.5
                        border.color: row.modelData.id === p.current ? p.st.primary : p.st.textMuted
                        Rectangle {
                            visible: row.modelData.id === p.current
                            anchors.centerIn: parent
                            width: 6
                            height: 6
                            radius: 3
                            color: p.st.primary
                        }
                    }
                    AgentLabel {
                        st: p.st
                        text: row.modelData.label
                        agentKind: row.modelData.kind
                        pixelSize: 13
                    }
                }
                Text {
                    visible: row.modelData.note !== undefined && row.modelData.note !== ""
                    width: parent.width
                    leftPadding: 22
                    text: row.modelData.note ? row.modelData.note : ""
                    wrapMode: Text.Wrap
                    color: p.st.textMuted
                    font.pixelSize: 12
                }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                enabled: row.modelData.enabled
                cursorShape: row.modelData.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: p.picked(row.modelData.id)
            }
        }
    }
}
