// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// "Delete this event? It moves to the calendar's trash bin." Nothing is deleted without this click.

import QtQuick

Rectangle {
    id: dlg

    required property var st
    property bool open: false
    property bool todo: false
    property string itemTitle: ""
    signal confirmed
    signal cancelled

    objectName: "confirmDeleteDialog"
    visible: open
    color: "#99000000"
    z: 100

    MouseArea {
        anchors.fill: parent
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(420, dlg.width - 32)
        height: column.height + 40
        radius: 12
        color: dlg.st.surface
        border.width: 1
        border.color: dlg.st.border

        Column {
            id: column
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 20
            spacing: 12
            Text {
                width: parent.width
                text: dlg.todo ? qsTr("Delete this to-do? It moves to the calendar's trash bin.") : qsTr("Delete this event? It moves to the calendar's trash bin.")
                wrapMode: Text.Wrap
                color: dlg.st.text
                font.pixelSize: 16
                font.bold: true
            }
            Text {
                width: parent.width
                text: dlg.itemTitle
                wrapMode: Text.Wrap
                color: dlg.st.textMuted
                font.pixelSize: 13
            }
            Row {
                spacing: 8
                FlatButton {
                    objectName: "cancelDeleteButton"
                    st: dlg.st
                    label: qsTr("Cancel")
                    onClicked: dlg.cancelled()
                }
                FlatButton {
                    objectName: "confirmDeleteButton"
                    st: dlg.st
                    primary: true
                    label: qsTr("Delete")
                    onClicked: dlg.confirmed()
                }
            }
        }
    }
}
