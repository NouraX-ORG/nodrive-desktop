// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The workload of a day (design 7 and 8): planned against free, "6 h planned / 4 h free", with a warning when it is too much.

import QtQuick

Rectangle {
    id: w

    required property var st
    property var row: ({})

    height: 40
    radius: 8
    color: row.overbooked ? Qt.rgba(st.warning.r, st.warning.g, st.warning.b, st.dark ? 0.16 : 0.1) : st.surfaceAlt
    border.width: row.overbooked ? 1 : 0
    border.color: st.warning
    Accessible.role: Accessible.StaticText
    Accessible.name: (row.text ? row.text : "") + (row.overbooked ? ", too much for this day" : "")

    readonly property real total: Math.max(0.1, (row.planned ? row.planned : 0) + (row.free ? row.free : 0))

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        Glyph {
            visible: w.row.overbooked === true
            anchors.verticalCenter: parent.verticalCenter
            kind: "alert"
            tone: w.st.warning
            size: 16
        }
        Text {
            objectName: "workloadText"
            anchors.verticalCenter: parent.verticalCenter
            text: w.row.text ? w.row.text : ""
            color: w.row.overbooked ? w.st.warning : w.st.text
            font.pixelSize: 13
            font.bold: true
        }
        Text {
            visible: text !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: w.row.noEstimateText ? w.row.noEstimateText : ""
            color: w.st.textMuted
            font.pixelSize: 12
        }
    }

    Rectangle {
        id: track
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(160, parent.width * 0.3)
        height: 8
        radius: 4
        color: w.st.border
        clip: true
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * Math.min(1, (w.row.planned ? w.row.planned : 0) / w.total)
            color: w.row.overbooked ? w.st.warning : w.st.primary
        }
    }
}
