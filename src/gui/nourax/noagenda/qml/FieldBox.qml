// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// A labelled text field. Our own (QtQuick only), so the window does not depend on a Controls style.

import QtQuick

Column {
    id: f

    required property var st
    property string label: ""
    property string text: ""
    property bool multiline: false
    property string placeholder: ""
    property bool enabledField: true
    signal accepted

    spacing: 2

    onTextChanged: {
        if (line.text !== text) {
            line.text = text;
        }
        if (area.text !== text) {
            area.text = text;
        }
    }

    Text {
        visible: f.label !== ""
        text: f.label
        color: f.st.textMuted
        font.pixelSize: 12
    }

    Rectangle {
        width: parent.width
        height: f.multiline ? 72 : 30
        radius: 6
        color: f.enabledField ? f.st.surface : f.st.surfaceAlt
        border.width: (line.activeFocus || area.activeFocus) ? 2 : 1
        border.color: (line.activeFocus || area.activeFocus) ? f.st.primary : f.st.border
        clip: true

        TextInput {
            id: line
            visible: !f.multiline
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: TextInput.AlignVCenter
            color: f.st.text
            selectionColor: f.st.primary
            selectedTextColor: f.st.onPrimary
            font.pixelSize: 13
            readOnly: !f.enabledField
            clip: true
            Accessible.name: f.label
            Accessible.role: Accessible.EditableText
            onTextChanged: if (f.text !== text) f.text = text
            onAccepted: f.accepted()
            Text {
                visible: line.text === "" && f.placeholder !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: f.placeholder
                color: f.st.textMuted
                font.pixelSize: 13
            }
        }

        TextEdit {
            id: area
            visible: f.multiline
            anchors.fill: parent
            anchors.margins: 6
            color: f.st.text
            selectionColor: f.st.primary
            selectedTextColor: f.st.onPrimary
            font.pixelSize: 13
            wrapMode: TextEdit.Wrap
            readOnly: !f.enabledField
            Accessible.name: f.label
            Accessible.role: Accessible.EditableText
            onTextChanged: if (f.text !== text) f.text = text
        }
    }
}
