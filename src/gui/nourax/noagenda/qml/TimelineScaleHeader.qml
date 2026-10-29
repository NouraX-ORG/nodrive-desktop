// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The top of the Timeline: months, and under them the weeks (12 px a day) or the days (40 px a day).
// `offset` is the sideways scroll of the chart.

import QtQuick

Item {
    id: h

    required property var st
    required property var topCells
    required property var bottomCells
    property real offset: 0

    clip: true
    height: 52

    Item {
        x: -h.offset
        width: parent.width + h.offset
        height: parent.height

        Repeater {
            model: h.topCells
            delegate: Item {
                id: month
                required property var modelData
                x: modelData.x
                width: modelData.w
                height: 26
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: month.modelData.text
                    color: h.st.text
                    font.pixelSize: 12
                    font.bold: true
                    elide: Text.ElideRight
                    width: month.width - 8
                }
                Rectangle {
                    width: 1
                    height: parent.height
                    color: h.st.border
                }
            }
        }
        Repeater {
            model: h.bottomCells
            delegate: Item {
                id: cell
                required property var modelData
                x: modelData.x
                y: 26
                width: modelData.w
                height: 26
                Rectangle {
                    anchors.fill: parent
                    visible: cell.modelData.today === true
                    color: h.st.primary
                    opacity: 0.18
                }
                Rectangle {
                    anchors.fill: parent
                    visible: cell.modelData.dayOff === true
                    color: h.st.surfaceAlt
                    opacity: 0.8
                }
                Column {
                    anchors.centerIn: parent
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cell.modelData.text
                        color: h.st.textMuted
                        font.pixelSize: 11
                    }
                    Text {
                        visible: cell.modelData.sub !== ""
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cell.modelData.sub
                        color: h.st.textMuted
                        font.pixelSize: 9
                    }
                }
                Rectangle {
                    width: 1
                    height: parent.height
                    color: h.st.border
                    opacity: 0.6
                }
            }
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: h.st.border
    }
}
