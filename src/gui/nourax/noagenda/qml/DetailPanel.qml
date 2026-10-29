// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The detail of the selected item, and its editor (design 9): change the time or the dates, tick it done, choose
// who does it (Self or one of my agents), delete an event or a to-do. A row that can't be changed says why in plain
// words and offers "Open in <app>".

import QtQuick

Rectangle {
    id: d

    required property var st
    property var info: ({})
    property var edit: null
    readonly property var form: edit && edit.form ? edit.form : ({})
    readonly property var fields: form.fields ? form.fields : ({})
    readonly property string formKey: form.key ? form.key : ""
    property bool editing: false
    property string errorText: ""
    property bool allDayValue: false
    property string statusValue: ""
    signal openRequested
    signal closeRequested
    signal deleteRequested

    color: st.surface
    border.width: 1
    border.color: st.border
    Accessible.role: Accessible.Pane
    Accessible.name: info.title ? info.title : ""

    onFormKeyChanged: {
        editing = false;
        errorText = "";
    }

    function startEdit() {
        var f = d.fields;
        fTitle.text = f.title ? f.title : "";
        fDescription.text = f.description ? f.description : "";
        fStartDate.text = f.startDate ? f.startDate : "";
        fStartTime.text = f.startTime ? f.startTime : "";
        fEndDate.text = f.endDate ? f.endDate : "";
        fEndTime.text = f.endTime ? f.endTime : "";
        fDueDate.text = f.dueDate ? f.dueDate : "";
        fDueTime.text = f.dueTime ? f.dueTime : "";
        fPercent.text = String(f.percent !== undefined ? f.percent : 0);
        allDayValue = f.allDay === true;
        statusValue = f.status ? f.status : "";
        errorText = "";
        editing = true;
    }
    function saveEdit() {
        var err = d.edit.save({
            "title": fTitle.text,
            "description": fDescription.text,
            "allDay": d.allDayValue,
            "startDate": fStartDate.text,
            "startTime": fStartTime.text,
            "endDate": fEndDate.text,
            "endTime": fEndTime.text,
            "dueDate": fDueDate.text,
            "dueTime": fDueTime.text,
            "percent": fPercent.text,
            "status": d.statusValue
        });
        if (err === "") {
            d.editing = false;
            d.errorText = "";
        } else {
            d.errorText = err;
        }
    }
    function currentAgent() {
        var list = d.form.agentChoices ? d.form.agentChoices : [];
        for (var i = 0; i < list.length; ++i) {
            if (list[i].selected) {
                return list[i].id;
            }
        }
        return "";
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: width
        contentHeight: column.height
        clip: true

        Column {
            id: column
            width: flick.width
            spacing: 10

            Row {
                width: parent.width
                spacing: 8
                Text {
                    objectName: "detailTitle"
                    width: parent.width - closeButton.width - 8
                    text: d.info.title ? d.info.title : ""
                    wrapMode: Text.Wrap
                    color: d.st.text
                    font.pixelSize: 17
                    font.bold: true
                }
                FlatButton {
                    id: closeButton
                    st: d.st
                    label: "x"
                    flat: true
                    Accessible.name: qsTr("Close")
                    onClicked: d.closeRequested()
                }
            }

            Text {
                text: (d.info.kindText ? d.info.kindText : "") + " - " + (d.info.homeText ? d.info.homeText : "")
                color: d.st.textMuted
                font.pixelSize: 13
            }

            Row {
                spacing: 8
                visible: d.info.sourceName !== undefined && d.info.sourceName !== ""
                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    anchors.verticalCenter: parent.verticalCenter
                    color: d.info.sourceColor ? d.info.sourceColor : d.st.primary
                }
                Text {
                    text: d.info.sourceName ? d.info.sourceName : ""
                    color: d.st.text
                    font.pixelSize: 13
                }
            }

            // ---- reading ------------------------------------------------------------------------------------------
            Text {
                visible: !d.editing
                width: parent.width
                text: d.info.whenText ? d.info.whenText : ""
                wrapMode: Text.Wrap
                color: d.st.text
                font.pixelSize: 13
            }

            Column {
                visible: !d.editing && !(d.form.showAgentPicker === true)
                spacing: 2
                Text {
                    text: qsTr("Agent")
                    color: d.st.textMuted
                    font.pixelSize: 12
                }
                AgentLabel {
                    st: d.st
                    text: d.info.agentText ? d.info.agentText : ""
                    agentKind: d.info.agentKind ? d.info.agentKind : "self"
                    pixelSize: 14
                }
            }

            Column {
                visible: d.info.personName !== undefined && d.info.personName !== ""
                spacing: 2
                Text {
                    text: qsTr("Person")
                    color: d.st.textMuted
                    font.pixelSize: 12
                }
                Text {
                    text: d.info.personName ? d.info.personName : ""
                    color: d.st.text
                    font.pixelSize: 14
                }
            }

            Text {
                visible: !d.editing && text !== ""
                text: [d.info.statusText, d.info.percentText].filter(function (s) { return s && s !== ""; }).join(" - ")
                color: d.st.text
                font.pixelSize: 13
            }

            Text {
                visible: !d.editing && text !== ""
                width: parent.width
                text: d.info.location ? d.info.location : ""
                wrapMode: Text.Wrap
                color: d.st.textMuted
                font.pixelSize: 13
            }

            Text {
                visible: !d.editing && text !== ""
                width: parent.width
                text: d.info.description ? d.info.description : ""
                wrapMode: Text.Wrap
                color: d.st.text
                font.pixelSize: 13
            }

            // ---- the editor --------------------------------------------------------------------------------------------
            Column {
                id: editor
                objectName: "editor"
                visible: d.editing
                width: parent.width
                spacing: 8

                FieldBox {
                    id: fTitle
                    objectName: "fieldTitle"
                    visible: d.form.kind === "event" || d.form.kind === "todo"
                    width: parent.width
                    st: d.st
                    label: qsTr("Title")
                    onAccepted: d.saveEdit()
                }

                CheckRow {
                    objectName: "allDayCheck"
                    visible: d.form.kind === "event"
                    st: d.st
                    label: qsTr("All day")
                    checked: d.allDayValue
                    onToggled: function (value) { d.allDayValue = value; }
                }

                Row {
                    visible: d.form.kind === "event" || d.form.kind === "gantt" || d.form.kind === "todo"
                    width: parent.width
                    spacing: 8
                    FieldBox {
                        id: fStartDate
                        objectName: "fieldStartDate"
                        width: d.form.kind === "event" && !d.allDayValue ? parent.width * 0.6 - 4 : parent.width
                        st: d.st
                        label: qsTr("Start")
                        placeholder: "2026-11-04"
                        onAccepted: d.saveEdit()
                    }
                    FieldBox {
                        id: fStartTime
                        objectName: "fieldStartTime"
                        visible: d.form.kind === "event" && !d.allDayValue
                        width: parent.width * 0.4 - 4
                        st: d.st
                        label: " "
                        placeholder: "09:00"
                        onAccepted: d.saveEdit()
                    }
                }

                Row {
                    visible: d.form.kind === "event" || d.form.kind === "gantt"
                    width: parent.width
                    spacing: 8
                    FieldBox {
                        id: fEndDate
                        objectName: "fieldEndDate"
                        width: d.form.kind === "event" && !d.allDayValue ? parent.width * 0.6 - 4 : parent.width
                        st: d.st
                        label: qsTr("End")
                        placeholder: "2026-11-04"
                        onAccepted: d.saveEdit()
                    }
                    FieldBox {
                        id: fEndTime
                        objectName: "fieldEndTime"
                        visible: d.form.kind === "event" && !d.allDayValue
                        width: parent.width * 0.4 - 4
                        st: d.st
                        label: " "
                        placeholder: "10:00"
                        onAccepted: d.saveEdit()
                    }
                }

                Row {
                    visible: d.form.kind === "todo" || d.form.kind === "card"
                    width: parent.width
                    spacing: 8
                    FieldBox {
                        id: fDueDate
                        objectName: "fieldDueDate"
                        width: d.form.kind === "todo" ? parent.width * 0.6 - 4 : parent.width
                        st: d.st
                        label: qsTr("Due")
                        placeholder: "2026-11-04"
                        onAccepted: d.saveEdit()
                    }
                    FieldBox {
                        id: fDueTime
                        objectName: "fieldDueTime"
                        visible: d.form.kind === "todo"
                        width: parent.width * 0.4 - 4
                        st: d.st
                        label: " "
                        placeholder: "17:00"
                        onAccepted: d.saveEdit()
                    }
                }

                Column {
                    visible: d.form.kind === "gantt"
                    width: parent.width
                    spacing: 6
                    FieldBox {
                        id: fPercent
                        objectName: "fieldPercent"
                        width: parent.width
                        st: d.st
                        label: qsTr("% done")
                        enabledField: d.form.canProgress === true
                        onAccepted: d.saveEdit()
                    }
                    Text {
                        visible: d.form.canProgress !== true && d.form.progressReason !== undefined && d.form.progressReason !== ""
                        width: parent.width
                        text: d.form.progressReason ? d.form.progressReason : ""
                        wrapMode: Text.Wrap
                        color: d.st.textMuted
                        font.pixelSize: 12
                    }
                    Text {
                        visible: d.form.canStatus === true
                        text: qsTr("Status")
                        color: d.st.textMuted
                        font.pixelSize: 12
                    }
                    Flow {
                        visible: d.form.canStatus === true
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: d.form.statuses ? d.form.statuses : []
                            delegate: Chip {
                                required property var modelData
                                objectName: "status-" + modelData.value
                                st: d.st
                                label: modelData.label
                                on: d.statusValue === modelData.value
                                onToggled: d.statusValue = modelData.value
                            }
                        }
                    }
                }

                FieldBox {
                    id: fDescription
                    objectName: "fieldDescription"
                    visible: d.form.kind === "event" || d.form.kind === "todo"
                    width: parent.width
                    st: d.st
                    label: qsTr("Description")
                    multiline: true
                    enabledField: d.form.descriptionLocked !== true
                }
                Text {
                    objectName: "descriptionReason"
                    visible: d.form.descriptionLocked === true
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: d.form.descriptionReason ? d.form.descriptionReason : ""
                    color: d.st.textMuted
                    font.pixelSize: 12
                }

                Row {
                    spacing: 8
                    FlatButton {
                        objectName: "saveButton"
                        st: d.st
                        primary: true
                        label: qsTr("Save")
                        onClicked: d.saveEdit()
                    }
                    FlatButton {
                        objectName: "cancelEditButton"
                        st: d.st
                        label: qsTr("Cancel")
                        onClicked: {
                            d.editing = false;
                            d.errorText = "";
                        }
                    }
                }
            }

            Text {
                objectName: "editError"
                visible: d.errorText !== ""
                width: parent.width
                text: d.errorText
                wrapMode: Text.Wrap
                color: d.st.danger
                font.pixelSize: 13
            }

            // ---- who does it ------------------------------------------------------------------------------------------------
            Column {
                visible: d.form.showAgentPicker === true
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Agent")
                    color: d.st.textMuted
                    font.pixelSize: 12
                }
                AgentPicker {
                    objectName: "agentPicker"
                    width: parent.width
                    st: d.st
                    choices: d.form.agentChoices ? d.form.agentChoices : []
                    current: d.currentAgent()
                    onPicked: function (id) {
                        d.errorText = d.edit.changeAgent(id);
                    }
                }
            }

            // ---- conflicts and flags -------------------------------------------------------------------------------------
            Repeater {
                model: d.form.conflicts ? d.form.conflicts : []
                delegate: Row {
                    required property var modelData
                    spacing: 8
                    width: column.width
                    Glyph {
                        anchors.top: parent.top
                        anchors.topMargin: 1
                        kind: "clash"
                        tone: d.st.warning
                        size: 14
                    }
                    Text {
                        objectName: "conflictText"
                        width: parent.width - 30
                        text: modelData
                        wrapMode: Text.Wrap
                        color: d.st.text
                        font.pixelSize: 13
                    }
                }
            }

            Repeater {
                model: d.info.flags ? d.info.flags : []
                delegate: Row {
                    required property var modelData
                    spacing: 8
                    width: column.width
                    Glyph {
                        anchors.top: parent.top
                        anchors.topMargin: 1
                        kind: "alert"
                        tone: d.st.warning
                        size: 14
                    }
                    Text {
                        width: parent.width - 30
                        text: modelData
                        wrapMode: Text.Wrap
                        color: d.st.text
                        font.pixelSize: 13
                    }
                }
            }

            Row {
                visible: d.form.locked === true && !d.editing
                width: parent.width
                spacing: 8
                Glyph {
                    anchors.top: parent.top
                    anchors.topMargin: 1
                    kind: "lock"
                    tone: d.st.textMuted
                    size: 14
                }
                Text {
                    objectName: "lockReason"
                    width: parent.width - 30
                    text: d.form.reason ? d.form.reason : ""
                    wrapMode: Text.Wrap
                    color: d.st.text
                    font.pixelSize: 13
                }
            }

            Text {
                objectName: "pendingNote"
                visible: d.form.pending === true
                width: parent.width
                text: qsTr("Waiting to send")
                color: d.st.textMuted
                font.pixelSize: 12
            }

            // ---- actions ---------------------------------------------------------------------------------------------------
            Flow {
                width: parent.width
                spacing: 8
                visible: !d.editing
                FlatButton {
                    objectName: "editButton"
                    visible: d.form.canEdit === true
                    st: d.st
                    primary: true
                    label: qsTr("Edit")
                    onClicked: d.startEdit()
                }
                FlatButton {
                    objectName: "doneButton"
                    visible: d.form.canDone === true
                    st: d.st
                    label: qsTr("Mark done")
                    onClicked: d.errorText = d.edit.markDone()
                }
                FlatButton {
                    objectName: "deleteButton"
                    visible: d.form.canDelete === true
                    st: d.st
                    label: qsTr("Delete")
                    onClicked: d.deleteRequested()
                }
                FlatButton {
                    objectName: "openInButton"
                    st: d.st
                    primary: d.form.canEdit !== true
                    label: d.info.openLabel ? d.info.openLabel : ""
                    onClicked: d.openRequested()
                }
            }
        }
    }
}
