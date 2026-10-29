// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// "+": a new event (choose the calendar) or a new to-do (choose the list). The person is me; the agent is required
// and Self is preselected (design 8).

import QtQuick

Rectangle {
    id: dlg

    required property var ctrl
    readonly property var st: ctrl.style
    readonly property var edit: ctrl.edit
    readonly property string dayIso: ctrl.selectedDateIso

    property bool open: false
    property string kind: "event"
    property string calendarUri: ""
    property string agentId: "SELF"
    property bool allDay: false
    property string errorText: ""
    readonly property var calendars: kind === "event" ? edit.newChoices.events : edit.newChoices.lists

    objectName: "newItemDialog"
    visible: open
    color: "#99000000"
    z: 100

    function reset(kindName) {
        kind = kindName;
        calendarUri = kind === "event" ? edit.newChoices.defaultEvent : edit.newChoices.defaultList;
        agentId = "SELF";
        allDay = false;
        errorText = "";
        fTitle.text = "";
        fDescription.text = "";
        fStartDate.text = kind === "event" ? dayIso : "";
        fStartTime.text = "09:00";
        fEndDate.text = dayIso;
        fEndTime.text = "10:00";
        fDueDate.text = kind === "todo" ? dayIso : "";
        fDueTime.text = "";
    }
    function show(kindName) {
        reset(kindName);
        open = true;
    }
    function pickKind(kindName) {
        var title = fTitle.text;
        var description = fDescription.text;
        reset(kindName);
        fTitle.text = title;
        fDescription.text = description;
    }
    function save() {
        var err = edit.create({
            "kind": kind,
            "calendarUri": calendarUri,
            "title": fTitle.text,
            "description": fDescription.text,
            "allDay": allDay,
            "startDate": fStartDate.text,
            "startTime": fStartTime.text,
            "endDate": fEndDate.text,
            "endTime": fEndTime.text,
            "dueDate": fDueDate.text,
            "dueTime": fDueTime.text,
            "agentId": agentId
        });
        if (err === "") {
            open = false;
        } else {
            errorText = err;
        }
    }

    MouseArea {
        anchors.fill: parent
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(520, dlg.width - 32)
        height: Math.min(dlg.height - 32, column.height + footer.height + 48)
        radius: 12
        color: dlg.st.surface
        border.width: 1
        border.color: dlg.st.border
        clip: true

        Flickable {
            id: flick
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footer.top
            anchors.margins: 20
            anchors.bottomMargin: 8
            contentWidth: width
            contentHeight: column.height
            clip: true

            Column {
                id: column
                width: flick.width
                spacing: 10

                Text {
                    text: dlg.kind === "event" ? qsTr("New event") : qsTr("New to-do")
                    color: dlg.st.text
                    font.pixelSize: 18
                    font.bold: true
                }

                Seg {
                    objectName: "newKindSeg"
                    st: dlg.st
                    namePrefix: "newkind-"
                    current: dlg.kind
                    options: [{ "value": "event", "label": qsTr("New event") }, { "value": "todo", "label": qsTr("New to-do") }]
                    onPicked: function (value) { dlg.pickKind(value); }
                }

                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: dlg.kind === "event" ? qsTr("Calendar") : qsTr("List")
                        color: dlg.st.textMuted
                        font.pixelSize: 12
                    }
                    Flow {
                        objectName: "calendarChoices"
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: dlg.calendars
                            delegate: Chip {
                                required property var modelData
                                objectName: "calendarChoice-" + modelData.uri
                                st: dlg.st
                                label: modelData.name
                                on: dlg.calendarUri === modelData.uri
                                onToggled: dlg.calendarUri = modelData.uri
                            }
                        }
                    }
                    Text {
                        visible: dlg.calendars.length === 0
                        width: parent.width
                        text: dlg.kind === "event" ? qsTr("There is no calendar you can add events to.") : qsTr("There is no list you can add to-dos to.")
                        wrapMode: Text.Wrap
                        color: dlg.st.warning
                        font.pixelSize: 13
                    }
                }

                FieldBox {
                    id: fTitle
                    objectName: "newFieldTitle"
                    width: parent.width
                    st: dlg.st
                    label: qsTr("Title")
                    onAccepted: dlg.save()
                }

                CheckRow {
                    objectName: "newAllDayCheck"
                    visible: dlg.kind === "event"
                    st: dlg.st
                    label: qsTr("All day")
                    checked: dlg.allDay
                    onToggled: function (value) { dlg.allDay = value; }
                }

                Row {
                    width: parent.width
                    spacing: 8
                    FieldBox {
                        id: fStartDate
                        objectName: "newFieldStartDate"
                        width: dlg.kind === "event" && !dlg.allDay ? parent.width * 0.6 - 4 : parent.width
                        st: dlg.st
                        label: qsTr("Start")
                        placeholder: "2026-11-04"
                    }
                    FieldBox {
                        id: fStartTime
                        objectName: "newFieldStartTime"
                        visible: dlg.kind === "event" && !dlg.allDay
                        width: parent.width * 0.4 - 4
                        st: dlg.st
                        label: " "
                        placeholder: "09:00"
                    }
                }

                Row {
                    visible: dlg.kind === "event"
                    width: parent.width
                    spacing: 8
                    FieldBox {
                        id: fEndDate
                        objectName: "newFieldEndDate"
                        width: !dlg.allDay ? parent.width * 0.6 - 4 : parent.width
                        st: dlg.st
                        label: qsTr("End")
                        placeholder: "2026-11-04"
                    }
                    FieldBox {
                        id: fEndTime
                        objectName: "newFieldEndTime"
                        visible: !dlg.allDay
                        width: parent.width * 0.4 - 4
                        st: dlg.st
                        label: " "
                        placeholder: "10:00"
                    }
                }

                Row {
                    visible: dlg.kind === "todo"
                    width: parent.width
                    spacing: 8
                    FieldBox {
                        id: fDueDate
                        objectName: "newFieldDueDate"
                        width: parent.width * 0.6 - 4
                        st: dlg.st
                        label: qsTr("Due")
                        placeholder: "2026-11-04"
                    }
                    FieldBox {
                        id: fDueTime
                        objectName: "newFieldDueTime"
                        width: parent.width * 0.4 - 4
                        st: dlg.st
                        label: " "
                        placeholder: "17:00"
                    }
                }

                FieldBox {
                    id: fDescription
                    objectName: "newFieldDescription"
                    width: parent.width
                    st: dlg.st
                    label: qsTr("Description")
                    multiline: true
                }

                Column {
                    width: parent.width
                    spacing: 4
                    Text {
                        text: qsTr("Agent")
                        color: dlg.st.textMuted
                        font.pixelSize: 12
                    }
                    AgentPicker {
                        objectName: "newAgentPicker"
                        width: parent.width
                        st: dlg.st
                        choices: dlg.edit.newChoices.agents
                        current: dlg.agentId
                        onPicked: function (id) { dlg.agentId = id; }
                    }
                }
            }
        }

        // The error and the buttons stay in view, also where the form has to scroll.
        Column {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 20
            spacing: 8

            Text {
                objectName: "newItemError"
                visible: dlg.errorText !== ""
                width: parent.width
                text: dlg.errorText
                wrapMode: Text.Wrap
                color: dlg.st.danger
                font.pixelSize: 13
            }

            Row {
                spacing: 8
                FlatButton {
                    objectName: "newCancelButton"
                    st: dlg.st
                    label: qsTr("Cancel")
                    onClicked: dlg.open = false
                }
                FlatButton {
                    objectName: "newSaveButton"
                    st: dlg.st
                    primary: true
                    label: qsTr("Save")
                    onClicked: dlg.save()
                }
            }
        }
    }
}
