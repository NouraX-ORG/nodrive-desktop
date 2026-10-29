// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// "Not sent": changes that could not be sent because the item changed somewhere else (or the server said no).
// The other change was kept. Nothing leaves this list without a click: apply the change again, or keep the other one.

import QtQuick

Column {
    id: panel

    required property var edit
    required property var st

    objectName: "notSentPanel"
    visible: edit.notSent.length > 0
    spacing: 0

    Rectangle {
        width: panel.width
        height: header.implicitHeight + 16
        color: Qt.rgba(panel.st.warning.r, panel.st.warning.g, panel.st.warning.b, panel.st.dark ? 0.18 : 0.12)
        border.width: 1
        border.color: panel.st.warning
        Row {
            id: header
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Glyph {
                anchors.verticalCenter: parent.verticalCenter
                kind: "alert"
                tone: panel.st.warning
                size: 16
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Not sent")
                color: panel.st.text
                font.pixelSize: 14
                font.bold: true
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: panel.edit.notSent.length
                color: panel.st.textMuted
                font.pixelSize: 13
            }
        }
    }

    Repeater {
        model: panel.edit.notSent
        delegate: Rectangle {
            id: entry
            required property var modelData
            objectName: "notSent-" + modelData.id
            width: panel.width
            height: entryColumn.height + 20
            color: panel.st.surface
            border.width: 1
            border.color: panel.st.border

            Column {
                id: entryColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 6
                Text {
                    width: parent.width
                    text: entry.modelData.title + (entry.modelData.summary !== "" ? " - " + qsTr("your change: %1").arg(entry.modelData.summary) : "")
                    wrapMode: Text.Wrap
                    color: panel.st.text
                    font.pixelSize: 13
                    font.bold: true
                }
                Text {
                    objectName: "notSentMessage"
                    width: parent.width
                    text: entry.modelData.message
                    wrapMode: Text.Wrap
                    color: panel.st.textMuted
                    font.pixelSize: 13
                }
                Flow {
                    width: parent.width
                    spacing: 8
                    FlatButton {
                        objectName: "applyAgainButton"
                        visible: entry.modelData.canApply
                        st: panel.st
                        primary: true
                        label: qsTr("Apply my change again")
                        onClicked: panel.edit.applyAgain(entry.modelData.id)
                    }
                    FlatButton {
                        objectName: "keepOtherButton"
                        st: panel.st
                        label: qsTr("Keep the other change")
                        onClicked: panel.edit.keepOther(entry.modelData.id)
                    }
                    FlatButton {
                        objectName: "notSentOpenButton"
                        st: panel.st
                        flat: true
                        label: entry.modelData.openLabel
                        onClicked: panel.edit.openNotSent(entry.modelData.id)
                    }
                }
            }
        }
    }
}
