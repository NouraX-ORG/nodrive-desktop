// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The Sources screen: every calendar, project and board with its setting (design 6.1), the working hours
// and the agents of the registry.

import QtQuick

Rectangle {
    id: p

    required property var ctrl
    readonly property var st: ctrl.style
    objectName: "sourcesPage"
    color: st.bg

    signal closeRequested

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: width
        contentHeight: column.height + 24
        clip: true

        Column {
            id: column
            width: flick.width
            spacing: 10

            Row {
                spacing: 12
                FlatButton {
                    st: p.st
                    label: qsTr("Back")
                    onClicked: p.closeRequested()
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Sources")
                    color: p.st.text
                    font.pixelSize: 20
                    font.bold: true
                }
            }

            Repeater {
                model: p.ctrl.sources
                delegate: Loader {
                    id: loader
                    required property var modelData
                    width: column.width
                    sourceComponent: modelData.type === "header" ? headerC : (modelData.type === "note" ? noteC : entryC)

                    Component {
                        id: headerC
                        Text {
                            width: column.width
                            topPadding: 10
                            text: loader.modelData.text
                            color: p.st.text
                            font.pixelSize: 15
                            font.bold: true
                        }
                    }
                    Component {
                        id: noteC
                        Text {
                            width: column.width
                            text: loader.modelData.text
                            wrapMode: Text.Wrap
                            color: p.st.textMuted
                            font.pixelSize: 13
                        }
                    }
                    Component {
                        id: entryC
                        Rectangle {
                            width: column.width
                            height: Math.max(52, entryRow.implicitHeight + 16)
                            radius: 8
                            color: p.st.surface
                            border.width: 1
                            border.color: p.st.border
                            opacity: loader.modelData.autoHidden ? 0.6 : 1
                            objectName: "source-" + loader.modelData.type + "-" + loader.modelData.key

                            Row {
                                id: entryRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: 12
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 12
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 12
                                    height: 12
                                    radius: 6
                                    color: loader.modelData.color
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 36 - optionsSeg.width
                                    Text {
                                        width: parent.width
                                        text: loader.modelData.name
                                        elide: Text.ElideRight
                                        color: p.st.text
                                        font.pixelSize: 14
                                    }
                                    Text {
                                        visible: text !== ""
                                        width: parent.width
                                        text: loader.modelData.note ? loader.modelData.note : ""
                                        elide: Text.ElideRight
                                        color: p.st.textMuted
                                        font.pixelSize: 12
                                    }
                                }
                                Seg {
                                    id: optionsSeg
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: !loader.modelData.autoHidden
                                    st: p.st
                                    small: true
                                    options: loader.modelData.options
                                    current: loader.modelData.mode
                                    namePrefix: "mode-" + loader.modelData.key + "-"
                                    onPicked: function (value) { p.ctrl.setSourceMode(loader.modelData.type, loader.modelData.key, value); }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                topPadding: 10
                text: qsTr("Working hours")
                color: p.st.text
                font.pixelSize: 15
                font.bold: true
            }
            Text {
                width: column.width
                text: qsTr("Used when NoGantt has no schedule for you.")
                wrapMode: Text.Wrap
                color: p.st.textMuted
                font.pixelSize: 13
            }
            Row {
                spacing: 6
                Repeater {
                    model: [qsTr("Mon"), qsTr("Tue"), qsTr("Wed"), qsTr("Thu"), qsTr("Fri"), qsTr("Sat"), qsTr("Sun")]
                    delegate: Chip {
                        required property var modelData
                        required property int index
                        st: p.st
                        label: modelData
                        on: ((p.ctrl.hours.daysMask >> index) & 1) === 1
                        onToggled: function (value) {
                            var mask = p.ctrl.hours.daysMask;
                            mask = value ? (mask | (1 << index)) : (mask & ~(1 << index));
                            p.ctrl.setWorkingHours(mask, p.ctrl.hours.from, p.ctrl.hours.to);
                        }
                    }
                }
            }
            Text {
                text: qsTr("From %1 to %2").arg(p.ctrl.hours.from).arg(p.ctrl.hours.to)
                color: p.st.text
                font.pixelSize: 13
            }

            Text {
                topPadding: 10
                text: qsTr("Agents")
                color: p.st.text
                font.pixelSize: 15
                font.bold: true
            }
            Text {
                objectName: "registryMessage"
                visible: text !== ""
                width: column.width
                text: p.ctrl.registryMessage
                wrapMode: Text.Wrap
                color: p.st.textMuted
                font.pixelSize: 13
            }
            Repeater {
                model: p.ctrl.agents
                delegate: Row {
                    required property var modelData
                    spacing: 8
                    AgentLabel {
                        st: p.st
                        text: modelData.name
                        agentKind: modelData.status === "paused" ? "paused" : (modelData.status === "retired" ? "retired" : "agent")
                        pixelSize: 14
                    }
                    Text {
                        text: "(" + modelData.statusText + ")"
                        color: p.st.textMuted
                        font.pixelSize: 13
                    }
                }
            }
        }
    }
}
