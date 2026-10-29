// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// What the shapes of the Timeline mean.

import QtQuick

Flow {
    id: l

    required property var st
    spacing: 16

    Row {
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            height: 12
            radius: 3
            color: Qt.rgba(l.st.primary.r, l.st.primary.g, l.st.primary.b, 0.25)
            border.width: 1
            border.color: l.st.primary
            Rectangle {
                width: 14
                height: parent.height
                radius: 3
                color: l.st.primary
            }
        }
        Text {
            text: qsTr("Task or to-do, filled by % done")
            color: l.st.textMuted
            font.pixelSize: 11
        }
    }
    Row {
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 9
            height: 9
            rotation: 45
            color: l.st.primary
        }
        Text {
            text: qsTr("Milestone or dated card")
            color: l.st.textMuted
            font.pixelSize: 11
        }
    }
    Row {
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 12
            color: Qt.rgba(l.st.primary.r, l.st.primary.g, l.st.primary.b, 0.3)
        }
        Text {
            text: qsTr("Your busy time")
            color: l.st.textMuted
            font.pixelSize: 11
        }
    }
    Row {
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 12
            color: l.st.surfaceAlt
            border.width: 1
            border.color: l.st.border
        }
        Text {
            text: qsTr("Day off")
            color: l.st.textMuted
            font.pixelSize: 11
        }
    }
    Row {
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            height: 2
            color: l.st.textMuted
        }
        Text {
            text: qsTr("Depends on (change it in NoGantt)")
            color: l.st.textMuted
            font.pixelSize: 11
        }
    }
}
