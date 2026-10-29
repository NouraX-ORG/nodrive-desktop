// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The top of the window: account, Day / Week / Month, Today, All / Work / Personal, the three chips.

import QtQuick

Rectangle {
    id: h

    required property var ctrl
    readonly property var st: ctrl.style
    signal sourcesRequested
    signal newMenuRequested

    color: st.surface
    implicitHeight: column.height + 24
    border.width: 1
    border.color: st.border

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 10

        Item {
            width: parent.width
            height: 32

            Text {
                id: accountText
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                text: h.ctrl.accountName !== "" ? h.ctrl.accountName : qsTr("NoAgenda")
                color: h.st.text
                font.pixelSize: 17
                font.bold: true
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                spacing: 10
                Text {
                    objectName: "updatedText"
                    anchors.verticalCenter: parent.verticalCenter
                    text: h.ctrl.updatedText
                    color: h.st.textMuted
                    font.pixelSize: 12
                }
                FlatButton {
                    objectName: "newButton"
                    st: h.st
                    primary: true
                    label: "+ " + qsTr("New")
                    Accessible.name: qsTr("New event or to-do")
                    onClicked: h.newMenuRequested()
                }
                FlatButton {
                    objectName: "refreshButton"
                    st: h.st
                    label: h.ctrl.refreshing ? qsTr("Refreshing...") : qsTr("Refresh")
                    onClicked: h.ctrl.refresh()
                }
                FlatButton {
                    objectName: "sourcesButton"
                    st: h.st
                    label: qsTr("Sources")
                    onClicked: h.sourcesRequested()
                }
            }
        }

        Flow {
            width: parent.width
            spacing: 10

            Seg {
                objectName: "viewSeg"
                st: h.st
                namePrefix: "view-"
                current: h.ctrl.viewMode
                options: [{ "value": "day", "label": qsTr("Day") }, { "value": "week", "label": qsTr("Week") }, { "value": "month", "label": qsTr("Month") }, { "value": "timeline", "label": qsTr("Timeline") }]
                onPicked: function (value) { h.ctrl.viewMode = value; }
            }
            Row {
                spacing: 4
                FlatButton {
                    objectName: "previousButton"
                    st: h.st
                    label: "<"
                    Accessible.name: qsTr("Previous")
                    onClicked: h.ctrl.goPrevious()
                }
                FlatButton {
                    objectName: "todayButton"
                    st: h.st
                    label: qsTr("Today")
                    onClicked: h.ctrl.goToday()
                }
                FlatButton {
                    objectName: "nextButton"
                    st: h.st
                    label: ">"
                    Accessible.name: qsTr("Next")
                    onClicked: h.ctrl.goNext()
                }
            }
            Text {
                objectName: "rangeText"
                height: 30
                verticalAlignment: Text.AlignVCenter
                text: h.ctrl.rangeText
                color: h.st.text
                font.pixelSize: 15
                font.bold: true
            }
        }

        Flow {
            width: parent.width
            spacing: 10

            Seg {
                st: h.st
                namePrefix: "category-"
                current: h.ctrl.category
                options: [{ "value": "all", "label": qsTr("All") }, { "value": "work", "label": qsTr("Work") }, { "value": "personal", "label": qsTr("Personal") }]
                onPicked: function (value) { h.ctrl.category = value; }
            }
            Chip {
                objectName: "chipMe"
                st: h.st
                glyph: "person"
                label: qsTr("Me")
                on: h.ctrl.chipMe
                onToggled: function (value) { h.ctrl.chooseChip("me", value); }
            }
            Chip {
                objectName: "chipAgents"
                st: h.st
                glyph: "robot"
                label: qsTr("My agents")
                on: h.ctrl.chipAgents
                onToggled: function (value) { h.ctrl.chooseChip("agents", value); }
            }
            Chip {
                objectName: "chipTeam"
                st: h.st
                glyph: "people"
                label: qsTr("My team")
                on: h.ctrl.chipTeam
                onToggled: function (value) { h.ctrl.chooseChip("team", value); }
            }
            Chip {
                objectName: "chipConflicts"
                st: h.st
                glyph: "clash"
                label: qsTr("Conflicts")
                on: h.ctrl.conflictsOnly
                onToggled: function (value) { h.ctrl.conflictsOnly = value; }
            }
            Chip {
                objectName: "showAllTeam"
                visible: h.ctrl.hasTeamSection && h.ctrl.chipTeam
                st: h.st
                label: qsTr("Show all team tasks")
                on: h.ctrl.showAllTeam
                onToggled: function (value) { h.ctrl.showAllTeam = value; }
            }
        }
    }
}
