// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// A segmented choice. `options` is a list of {value, label}. The one with `current` is filled.

import QtQuick

Rectangle {
    id: s

    required property var st
    property var options: []
    property string current: ""
    property string namePrefix: ""
    property bool small: false
    signal picked(string value)

    implicitHeight: small ? 24 : 30
    implicitWidth: row.implicitWidth + 2
    radius: 6
    color: st.surface
    border.width: 1
    border.color: st.border

    Row {
        id: row
        anchors.centerIn: parent
        Repeater {
            model: s.options
            delegate: Rectangle {
                id: seg
                required property var modelData
                required property int index
                objectName: s.namePrefix + modelData.value
                height: s.height - 4
                width: label.implicitWidth + (s.small ? 14 : 22)
                radius: 5
                color: s.current === modelData.value ? s.st.primary : (area.containsMouse ? s.st.hover : "transparent")

                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.label
                Accessible.checked: s.current === modelData.value
                Accessible.onPressAction: s.picked(modelData.value)
                activeFocusOnTab: true
                Keys.onSpacePressed: s.picked(modelData.value)
                Keys.onReturnPressed: s.picked(modelData.value)

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: seg.modelData.label
                    color: s.current === seg.modelData.value ? s.st.onPrimary : s.st.text
                    font.pixelSize: s.small ? 12 : 13
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: s.picked(seg.modelData.value)
                }
            }
        }
    }
}
