// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The one-time question "Which of your calendars are personal?" (design 6.1).

import QtQuick

Rectangle {
    id: dlg

    required property var ctrl
    readonly property var st: ctrl.style
    property var ticked: ({})

    objectName: "personalDialog"
    visible: ctrl.personalQuestionVisible
    color: "#99000000"
    z: 100

    function resetTicks() {
        var t = {};
        var list = ctrl.personalCalendars;
        for (var i = 0; i < list.length; ++i) {
            t[list[i].uri] = true;
        }
        ticked = t;
    }
    onVisibleChanged: if (visible) resetTicks()
    Component.onCompleted: resetTicks()

    MouseArea {
        anchors.fill: parent
        // swallow clicks behind the dialog
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(460, dlg.width - 32)
        height: cardColumn.height + 40
        radius: 12
        color: dlg.st.surface
        border.width: 1
        border.color: dlg.st.border

        Column {
            id: cardColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 20
            spacing: 12

            Text {
                width: parent.width
                text: qsTr("Which of your calendars are personal?")
                wrapMode: Text.Wrap
                color: dlg.st.text
                font.pixelSize: 17
                font.bold: true
            }
            Text {
                width: parent.width
                text: qsTr("Personal events are only ever shown to you. The others count as Work.")
                wrapMode: Text.Wrap
                color: dlg.st.textMuted
                font.pixelSize: 13
            }
            Repeater {
                model: dlg.ctrl.personalCalendars
                delegate: Rectangle {
                    id: line
                    required property var modelData
                    objectName: "personalCalendar-" + modelData.uri
                    width: cardColumn.width
                    height: 34
                    radius: 6
                    color: area.containsMouse ? dlg.st.hover : "transparent"
                    readonly property bool isOn: dlg.ticked[modelData.uri] === true

                    Accessible.role: Accessible.CheckBox
                    Accessible.name: modelData.name
                    Accessible.checked: isOn

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        spacing: 10
                        Rectangle {
                            width: 18
                            height: 18
                            radius: 4
                            color: line.isOn ? dlg.st.primary : "transparent"
                            border.width: 1.5
                            border.color: line.isOn ? dlg.st.primary : dlg.st.textMuted
                            Text {
                                anchors.centerIn: parent
                                visible: line.isOn
                                text: "✓"
                                color: dlg.st.onPrimary
                                font.pixelSize: 13
                                font.bold: true
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: line.modelData.name
                            color: dlg.st.text
                            font.pixelSize: 14
                        }
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            var t = dlg.ticked;
                            t[line.modelData.uri] = !(t[line.modelData.uri] === true);
                            dlg.ticked = t;
                            dlg.ticked = Object.assign({}, t);
                        }
                    }
                }
            }
            Row {
                spacing: 10
                layoutDirection: Qt.RightToLeft
                width: parent.width
                FlatButton {
                    objectName: "personalSave"
                    st: dlg.st
                    primary: true
                    label: qsTr("Save")
                    onClicked: {
                        var uris = [];
                        for (var key in dlg.ticked) {
                            if (dlg.ticked[key] === true) {
                                uris.push(key);
                            }
                        }
                        dlg.ctrl.answerPersonal(uris);
                    }
                }
                FlatButton {
                    objectName: "personalSkip"
                    st: dlg.st
                    label: qsTr("Skip")
                    onClicked: dlg.ctrl.skipPersonal()
                }
            }
        }
    }
}
