// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// NoAgenda: one screen with everything a person is responsible for (Day, Week, Month).
// Colours come from the controller's style object (NoDrive themes, light and dark).

import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: win

    required property var ctrl
    readonly property var st: ctrl.style

    title: qsTr("NoAgenda")
    width: 1040
    height: 720
    minimumWidth: 720
    minimumHeight: 520
    visible: false
    color: st.bg

    Shortcut {
        sequence: "Ctrl+R"
        onActivated: win.ctrl.refresh()
    }
    Shortcut {
        sequence: "F5"
        onActivated: win.ctrl.refresh()
    }
    Shortcut {
        sequence: "T"
        onActivated: win.ctrl.goToday()
    }
    Shortcut {
        sequence: "D"
        onActivated: win.ctrl.viewMode = "day"
    }
    Shortcut {
        sequence: "W"
        onActivated: win.ctrl.viewMode = "week"
    }
    Shortcut {
        sequence: "M"
        onActivated: win.ctrl.viewMode = "month"
    }
    Shortcut {
        sequence: "Escape"
        onActivated: win.ctrl.clearSelection()
    }

    Item {
        anchors.fill: parent

        AgendaHeader {
            id: header
            visible: win.ctrl.page === "agenda"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: visible ? implicitHeight : 0
            ctrl: win.ctrl
            onSourcesRequested: win.ctrl.page = "sources"
        }

        Column {
            id: banners
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            visible: win.ctrl.page === "agenda"
            Repeater {
                model: win.ctrl.banners
                delegate: StatusBanner {
                    required property var modelData
                    objectName: "banner"
                    width: banners.width
                    st: win.st
                    info: modelData
                    onActionRequested: function (action) { win.ctrl.bannerAction(action); }
                }
            }
        }

        Item {
            id: body
            visible: win.ctrl.page === "agenda"
            anchors.top: banners.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right

            MonthGrid {
                id: grid
                visible: win.ctrl.viewMode === "month"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 16
                width: visible ? implicitWidth : 0
                height: implicitHeight
                ctrl: win.ctrl
                onDayPicked: function (iso) { win.ctrl.setDate(iso); }
            }

            Item {
                id: listArea
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: grid.visible ? grid.right : parent.left
                anchors.right: detail.visible ? detail.left : parent.right

                ListView {
                    id: list
                    objectName: "dayList"
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 6
                    clip: true
                    cacheBuffer: 6000
                    model: win.ctrl.rows
                    focus: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    Keys.onDownPressed: win.ctrl.moveSelection(1)
                    Keys.onUpPressed: win.ctrl.moveSelection(-1)

                    delegate: Item {
                        id: del
                        required property var modelData
                        required property int index
                        width: ListView.view ? ListView.view.width - 12 : 0
                        height: loader.item ? loader.item.height : 0

                        Loader {
                            id: loader
                            width: del.width
                            sourceComponent: del.modelData.type === "item" ? itemC
                                : (del.modelData.type === "day" ? dayC
                                : (del.modelData.type === "group" ? groupC
                                : (del.modelData.type === "noDate" ? noDateC : quietC)))
                        }

                        Component {
                            id: itemC
                            AgendaRow {
                                objectName: "row-" + del.modelData.key
                                st: win.st
                                row: del.modelData
                                selected: win.ctrl.selectedKey === del.modelData.key
                                onActivated: win.ctrl.select(del.modelData.key)
                            }
                        }
                        Component {
                            id: dayC
                            Item {
                                height: 40
                                Row {
                                    anchors.left: parent.left
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 4
                                    spacing: 10
                                    Text {
                                        text: del.modelData.text
                                        color: win.st.text
                                        font.pixelSize: 16
                                        font.bold: true
                                    }
                                    Rectangle {
                                        visible: del.modelData.today === true
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: todayText.implicitWidth + 14
                                        height: 20
                                        radius: 10
                                        color: win.st.primary
                                        Text {
                                            id: todayText
                                            anchors.centerIn: parent
                                            text: qsTr("Today")
                                            color: win.st.onPrimary
                                            font.pixelSize: 11
                                        }
                                    }
                                }
                            }
                        }
                        Component {
                            id: groupC
                            Item {
                                height: 30
                                Row {
                                    anchors.left: parent.left
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 2
                                    spacing: 8
                                    Glyph {
                                        visible: del.modelData.text === qsTr("My team")
                                        anchors.verticalCenter: parent.verticalCenter
                                        kind: "people"
                                        tone: win.st.textMuted
                                        size: 14
                                    }
                                    Text {
                                        text: del.modelData.text
                                        color: win.st.textMuted
                                        font.pixelSize: 13
                                        font.bold: true
                                    }
                                    Text {
                                        visible: del.modelData.count > 0
                                        text: del.modelData.count
                                        color: win.st.textMuted
                                        font.pixelSize: 13
                                    }
                                }
                            }
                        }
                        Component {
                            id: noDateC
                            Item {
                                height: 34
                                Row {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 8
                                    Text {
                                        text: del.modelData.expanded ? "v" : ">"
                                        color: win.st.textMuted
                                        font.pixelSize: 13
                                    }
                                    Text {
                                        text: del.modelData.text + " (" + del.modelData.count + ")"
                                        color: win.st.textMuted
                                        font.pixelSize: 13
                                        font.bold: true
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: win.ctrl.toggleNoDate()
                                }
                            }
                        }
                        Component {
                            id: quietC
                            Text {
                                height: implicitHeight + 8
                                text: del.modelData.text ? del.modelData.text : ""
                                color: win.st.textMuted
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                // First load: grey bars and a sentence.
                Column {
                    objectName: "loadingState"
                    visible: win.ctrl.loadingFirstTime
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 24
                    spacing: 10
                    Text {
                        text: qsTr("Loading your agenda...")
                        color: win.st.textMuted
                        font.pixelSize: 14
                    }
                    Repeater {
                        model: 4
                        delegate: Rectangle {
                            width: listArea.width - 48
                            height: 48
                            radius: 8
                            color: win.st.surfaceAlt
                        }
                    }
                }

                Text {
                    objectName: "emptyText"
                    visible: !win.ctrl.loadingFirstTime && win.ctrl.emptyText !== ""
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 64, 480)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: win.ctrl.emptyText
                    color: win.st.textMuted
                    font.pixelSize: 15
                }
            }

            DetailPanel {
                id: detail
                objectName: "detailPanel"
                visible: win.ctrl.selectedKey !== ""
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: visible ? 360 : 0
                st: win.st
                info: win.ctrl.detail
                onOpenRequested: win.ctrl.openSelected()
                onCloseRequested: win.ctrl.clearSelection()
            }
        }

        SourcesPage {
            visible: win.ctrl.page === "sources"
            anchors.fill: parent
            ctrl: win.ctrl
            onCloseRequested: win.ctrl.page = "agenda"
        }

        PersonalCalendarsDialog {
            anchors.fill: parent
            ctrl: win.ctrl
        }
    }
}
