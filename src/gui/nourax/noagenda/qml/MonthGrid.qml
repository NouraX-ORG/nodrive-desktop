// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// A small month calendar for jumping to a day (dots on days that have items).

import QtQuick

Rectangle {
    id: g

    required property var ctrl
    readonly property var st: ctrl.style
    signal dayPicked(string iso)

    implicitWidth: 7 * 34 + 16
    implicitHeight: 6 * 30 + 16
    color: st.surface
    radius: 8
    border.width: 1
    border.color: st.border

    Grid {
        anchors.centerIn: parent
        columns: 7
        spacing: 0
        Repeater {
            model: g.ctrl.monthGrid
            delegate: Rectangle {
                id: cell
                required property var modelData
                width: 34
                height: 30
                radius: 6
                color: modelData.selected ? g.st.primary : (area.containsMouse ? g.st.hover : "transparent")
                border.width: (modelData.today && !modelData.selected) ? 1 : 0
                border.color: g.st.primary
                Accessible.role: Accessible.Button
                Accessible.name: modelData.iso

                Text {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -2
                    text: cell.modelData.day
                    font.pixelSize: 12
                    color: cell.modelData.selected ? g.st.onPrimary : (cell.modelData.inMonth ? g.st.text : g.st.textMuted)
                    opacity: cell.modelData.inMonth ? 1 : 0.5
                }
                Rectangle {
                    visible: cell.modelData.busy
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 3
                    width: 4
                    height: 4
                    radius: 2
                    color: cell.modelData.selected ? g.st.onPrimary : g.st.primary
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: g.dayPicked(cell.modelData.iso)
                }
            }
        }
    }
}
