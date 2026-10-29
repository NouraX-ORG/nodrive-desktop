// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The Timeline (design 8): my Gantt tasks across projects as bars under their project and phase, % done filling the bar,
// read only dependency arrows, my to-dos with a start and a due date as bars, milestones and dated cards as diamonds,
// the agent on each item, and my busy time shaded behind it. A fixed column of names on the left, the scale on top, and
// a chart that scrolls sideways and down. Drag a bar to move it (the editor's rules and queue are used).

import QtQuick
import QtQuick.Controls

Item {
    id: v

    required property var ctrl
    readonly property var st: ctrl.style
    readonly property var tl: ctrl.timeline
    readonly property real nameW: 230
    readonly property real scaleH: 52
    readonly property real toolbarH: 40
    readonly property bool dragActive: tl.ghost.show === true
    /// Counts the Esc presses that cancel a drag under way.
    property int cancelCount: 0

    function cancelDrag() {
        cancelCount++;
        tl.dragCancel();
    }
    function scrollTo(x) {
        flick.contentX = Math.max(0, Math.min(x, Math.max(0, flick.contentWidth - flick.width)));
    }

    Connections {
        target: v.tl
        function onScrollRequested(x) { Qt.callLater(function () { v.scrollTo(x); }); }
    }
    Component.onCompleted: Qt.callLater(function () { v.scrollTo(v.tl.scrollTargetX()); })
    onVisibleChanged: if (visible) Qt.callLater(function () { v.scrollTo(v.tl.scrollTargetX()); })

    // ---- the tool row ---------------------------------------------------------------------------------------------------
    Item {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: v.toolbarH

        Seg {
            id: zoomSeg
            objectName: "zoomSeg"
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 16
            st: v.st
            small: true
            namePrefix: "zoom-"
            current: v.tl.zoom
            options: [{ "value": "weeks", "label": qsTr("Weeks") }, { "value": "days", "label": qsTr("Days") }]
            onPicked: function (value) {
                v.tl.zoom = value;
                Qt.callLater(function () { v.scrollTo(v.tl.scrollTargetX()); });
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: zoomSeg.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 16
            text: qsTr("Drag a bar to move it. Left and Right move a selected bar by a day.")
            color: v.st.textMuted
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }

    // ---- the corner and the scale -----------------------------------------------------------------------------------------
    Rectangle {
        id: corner
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        width: v.nameW
        height: v.scaleH
        color: v.st.surface
        border.width: 1
        border.color: v.st.border
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 12
            text: qsTr("Project, phase and task")
            color: v.st.textMuted
            font.pixelSize: 12
            font.bold: true
        }
    }
    TimelineScaleHeader {
        id: scaleHeader
        objectName: "timelineScale"
        anchors.top: toolbar.bottom
        anchors.left: corner.right
        anchors.right: parent.right
        st: v.st
        topCells: v.tl.scaleTop
        bottomCells: v.tl.scaleBottom
        offset: flick.contentX
    }

    // ---- the names (fixed on the left) -----------------------------------------------------------------------------------
    Item {
        id: names
        objectName: "timelineNames"
        anchors.top: corner.bottom
        anchors.bottom: legend.top
        anchors.left: parent.left
        width: v.nameW
        clip: true

        Rectangle {
            anchors.fill: parent
            color: v.st.surface
        }
        Item {
            y: -flick.contentY
            width: parent.width
            height: flick.contentHeight

            Repeater {
                model: v.tl.entries
                delegate: Item {
                    id: nameRow
                    required property var modelData
                    readonly property bool isGroup: modelData.type === "group"
                    readonly property int indent: isGroup ? (modelData.level === 1 ? 16 : 0) : (modelData.groupKey.indexOf(":s") >= 0 ? 30 : 14)
                    objectName: isGroup ? "group-" + modelData.key : "name-" + modelData.key
                    y: modelData.row * v.tl.rowHeight
                    width: v.nameW
                    height: v.tl.rowHeight

                    Rectangle {
                        anchors.fill: parent
                        color: nameRow.isGroup ? v.st.surfaceAlt : (v.ctrl.selectedKey === nameRow.modelData.key ? v.st.selected : "transparent")
                        opacity: nameRow.isGroup ? 0.8 : 1
                    }
                    Rectangle {
                        visible: nameRow.isGroup && nameRow.modelData.level === 0
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 4
                        color: nameRow.modelData.color
                    }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12 + nameRow.indent
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        spacing: 6
                        Glyph {
                            visible: !nameRow.isGroup
                            anchors.verticalCenter: parent.verticalCenter
                            kind: nameRow.modelData.glyph ? nameRow.modelData.glyph : "task"
                            tone: nameRow.modelData.color
                            size: 13
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - (nameRow.isGroup ? 0 : 20)
                            text: nameRow.modelData.title
                            color: nameRow.isGroup && nameRow.modelData.level === 1 ? v.st.textMuted : v.st.text
                            font.pixelSize: nameRow.isGroup ? 12 : 12
                            font.bold: nameRow.isGroup
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !nameRow.isGroup
                        onClicked: v.ctrl.select(nameRow.modelData.key)
                    }
                }
            }
        }
        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: v.st.border
        }
    }

    // ---- the chart -------------------------------------------------------------------------------------------------------------
    Flickable {
        id: flick
        objectName: "timelineFlick"
        anchors.top: corner.bottom
        anchors.bottom: legend.top
        anchors.left: names.right
        anchors.right: parent.right
        clip: true
        // The mouse belongs to the bars (a drag moves a bar). Wheel and touchpad scroll it, and so do the scroll bars.
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, v.tl.contentWidth + 260)
        contentHeight: Math.max(height, v.tl.contentHeight + 12)
        ScrollBar.horizontal: ScrollBar {}
        ScrollBar.vertical: ScrollBar {}

        BusyShading {
            id: shading
            objectName: "busyShading"
            st: v.st
            shading: v.tl.shading
            todayX: v.tl.todayX
            contentH: flick.contentHeight
            width: v.tl.contentWidth
        }

        // The bands of the group rows, and a thin line under every row.
        Repeater {
            model: v.tl.entries
            delegate: Item {
                id: rowBg
                required property var modelData
                y: modelData.row * v.tl.rowHeight
                width: v.tl.contentWidth
                height: v.tl.rowHeight
                Rectangle {
                    anchors.fill: parent
                    visible: rowBg.modelData.type === "group"
                    color: v.st.surfaceAlt
                    opacity: 0.5
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: v.st.border
                    opacity: 0.45
                }
            }
        }

        DependencyArrows {
            id: arrows
            objectName: "dependencyArrows"
            st: v.st
            arrows: v.tl.arrows
            contentW: v.tl.contentWidth
            contentH: flick.contentHeight
        }

        Repeater {
            model: v.tl.entries
            delegate: Item {
                id: cell
                required property var modelData
                Loader {
                    active: cell.modelData.type === "bar"
                    sourceComponent: Component {
                        TimelineBar {
                            st: v.st
                            info: cell.modelData
                            ctrl: v.ctrl
                            view: v
                        }
                    }
                }
                Loader {
                    active: cell.modelData.type === "diamond"
                    sourceComponent: Component {
                        TimelineDiamond {
                            st: v.st
                            info: cell.modelData
                            ctrl: v.ctrl
                        }
                    }
                }
            }
        }

        // The ghost bar of a drag: where the bar would land, and the new dates.
        Rectangle {
            id: ghost
            objectName: "ghostBar"
            visible: v.dragActive
            x: v.tl.ghost.x !== undefined ? v.tl.ghost.x : 0
            y: v.tl.ghost.y !== undefined ? v.tl.ghost.y : 0
            width: v.tl.ghost.w !== undefined ? v.tl.ghost.w : 0
            height: v.tl.ghost.h !== undefined ? v.tl.ghost.h : 0
            radius: 5
            color: Qt.rgba(v.st.primary.r, v.st.primary.g, v.st.primary.b, 0.25)
            border.width: 2
            border.color: v.st.primary
            z: 20
            Rectangle {
                id: ghostLabel
                objectName: "ghostLabel"
                anchors.bottom: parent.top
                anchors.bottomMargin: 2
                anchors.left: parent.left
                width: ghostText.implicitWidth + 14
                height: 22
                radius: 6
                color: v.st.dark ? "#1E293B" : "#0F172A"
                Text {
                    id: ghostText
                    anchors.centerIn: parent
                    text: v.tl.ghost.text !== undefined ? v.tl.ghost.text : ""
                    color: "#FFFFFF"
                    font.pixelSize: 12
                }
            }
        }

        WheelHandler {
            id: wheel
            target: null
            onWheel: function (event) {
                var dx = event.pixelDelta.x !== 0 ? -event.pixelDelta.x : -event.angleDelta.x / 2;
                var dy = event.pixelDelta.y !== 0 ? -event.pixelDelta.y : -event.angleDelta.y / 2;
                if ((event.modifiers & Qt.ShiftModifier) && dx === 0) {
                    dx = dy;
                    dy = 0;
                }
                flick.contentX = Math.max(0, Math.min(flick.contentX + dx, Math.max(0, flick.contentWidth - flick.width)));
                flick.contentY = Math.max(0, Math.min(flick.contentY + dy, Math.max(0, flick.contentHeight - flick.height)));
            }
        }
        // A touch screen pans the chart with a finger.
        DragHandler {
            id: touchPan
            target: null
            acceptedDevices: PointerDevice.TouchScreen
            property real startX: 0
            property real startY: 0
            onActiveChanged: {
                if (active) {
                    startX = flick.contentX;
                    startY = flick.contentY;
                }
            }
            onCentroidChanged: {
                if (active) {
                    var dx = centroid.scenePosition.x - centroid.scenePressPosition.x;
                    var dy = centroid.scenePosition.y - centroid.scenePressPosition.y;
                    flick.contentX = Math.max(0, Math.min(startX - dx, Math.max(0, flick.contentWidth - flick.width)));
                    flick.contentY = Math.max(0, Math.min(startY - dy, Math.max(0, flick.contentHeight - flick.height)));
                }
            }
        }
    }

    Text {
        objectName: "timelineEmpty"
        visible: v.ctrl.emptyText !== "" && !v.ctrl.loadingFirstTime
        anchors.centerIn: flick
        width: Math.min(flick.width - 64, 480)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: v.ctrl.emptyText
        color: v.st.textMuted
        font.pixelSize: 15
    }

    // ---- the legend ---------------------------------------------------------------------------------------------------------------
    Item {
        id: legend
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: legendFlow.implicitHeight + 16
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: v.st.border
        }
        TimelineLegend {
            id: legendFlow
            objectName: "timelineLegend"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            st: v.st
        }
    }
}
