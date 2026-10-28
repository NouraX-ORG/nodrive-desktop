// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The detail of the selected item. Read only in this step: the one action is "Open in <app>".

import QtQuick

Rectangle {
    id: d

    required property var st
    property var info: ({})
    signal openRequested
    signal closeRequested

    color: st.surface
    border.width: 1
    border.color: st.border
    Accessible.role: Accessible.Pane
    Accessible.name: info.title ? info.title : ""

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

            Text {
                width: parent.width
                text: d.info.whenText ? d.info.whenText : ""
                wrapMode: Text.Wrap
                color: d.st.text
                font.pixelSize: 13
            }

            Column {
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
                visible: text !== ""
                text: [d.info.statusText, d.info.percentText].filter(function (s) { return s && s !== ""; }).join(" - ")
                color: d.st.text
                font.pixelSize: 13
            }

            Text {
                visible: text !== ""
                width: parent.width
                text: d.info.location ? d.info.location : ""
                wrapMode: Text.Wrap
                color: d.st.textMuted
                font.pixelSize: 13
            }

            Text {
                visible: text !== ""
                width: parent.width
                text: d.info.description ? d.info.description : ""
                wrapMode: Text.Wrap
                color: d.st.text
                font.pixelSize: 13
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

            FlatButton {
                objectName: "openInButton"
                st: d.st
                primary: true
                label: d.info.openLabel ? d.info.openLabel : ""
                onClicked: d.openRequested()
            }
        }
    }
}
