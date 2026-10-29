// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// A short message after a change ("Saved. Also moved: ..."), with "Undo" for ten seconds after a move in NoGantt.

import QtQuick

Rectangle {
    id: t

    required property var edit
    required property var st

    objectName: "undoToast"
    visible: edit.toastText !== ""
    width: Math.min(560, parent.width - 32)
    height: Math.max(44, row.implicitHeight + 20)
    radius: 10
    color: st.dark ? "#1E293B" : "#0F172A"
    border.width: edit.toastTone === "warn" ? 1 : 0
    border.color: st.warning
    z: 90
    Accessible.role: Accessible.AlertMessage
    Accessible.name: edit.toastText

    Row {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        spacing: 10
        Glyph {
            anchors.verticalCenter: parent.verticalCenter
            kind: "alert"
            tone: t.edit.toastTone === "warn" ? t.st.warning : "#93C5FD"
            size: 16
        }
        Text {
            objectName: "toastText"
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 26 - undoButton.width - closeButton.width - 30
            text: t.edit.toastText
            wrapMode: Text.Wrap
            color: "#F8FAFC"
            font.pixelSize: 13
        }
        FlatButton {
            id: undoButton
            objectName: "undoButton"
            anchors.verticalCenter: parent.verticalCenter
            visible: t.edit.toastCanUndo
            width: visible ? implicitWidth : 0
            st: t.st
            primary: true
            label: qsTr("Undo")
            onClicked: t.edit.undo()
        }
        FlatButton {
            id: closeButton
            anchors.verticalCenter: parent.verticalCenter
            st: t.st
            flat: true
            label: "x"
            Accessible.name: qsTr("Close")
            onClicked: t.edit.dismissToast()
        }
    }
}
