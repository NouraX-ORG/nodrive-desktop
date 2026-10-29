// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// One bar of the Timeline: a NoGantt task, or a to-do with a start and a due date. The inner fill is % done. A bar that
// may be moved follows the mouse by whole days (a ghost bar shows the new dates); release saves, Esc cancels. The keys
// do the same: Left / Right one day, Shift seven days, Enter saves, Esc cancels. A locked bar shows a lock and does not move.

import QtQuick
import QtQuick.Controls

Item {
    id: b

    required property var st
    required property var info // the entry of the controller
    required property var ctrl
    required property var view // the TimelineView (cancel counter)
    readonly property var tl: ctrl.timeline
    readonly property bool selected: ctrl.selectedKey === info.key
    property bool dragging: false
    property bool cancelled: false
    property real dragDx: 0
    property int keyDays: 0

    x: info.x
    y: info.y
    width: info.w
    height: info.h
    objectName: "bar-" + info.key
    activeFocusOnTab: true
    opacity: dragging || keyDays !== 0 ? 0.55 : 1

    Accessible.role: Accessible.Button
    Accessible.name: info.title + ", " + info.dates + ", " + info.agentText + (info.locked ? ", " + qsTr("locked") : "")

    function preview(dx) {
        tl.dragPreview(info.key, dx);
    }
    function finishKeys(save) {
        var days = keyDays;
        keyDays = 0;
        if (save && days !== 0) {
            tl.moveByKeys(info.key, days);
        } else {
            tl.dragCancel();
        }
    }

    Keys.onPressed: function (event) {
        var step = (event.modifiers & Qt.ShiftModifier) ? 7 : 1;
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            if (!info.canMove) {
                tl.dragPreview(info.key, 0);
                b.ctrl.edit.showToast(info.lockReason, "warn");
            } else {
                keyDays += event.key === Qt.Key_Left ? -step : step;
                tl.dragPreview(info.key, keyDays * tl.pxPerDay);
            }
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (keyDays !== 0) {
                finishKeys(true);
            } else {
                ctrl.select(info.key);
            }
            event.accepted = true;
        } else if (event.key === Qt.Key_Escape && keyDays !== 0) {
            finishKeys(false);
            event.accepted = true;
        } else if (event.key === Qt.Key_Escape && dragging) {
            view.cancelDrag();
            event.accepted = true;
        }
    }
    Keys.onShortcutOverride: function (event) {
        // Esc cancels a move that is under way instead of closing the detail panel.
        if (event.key === Qt.Key_Escape && (keyDays !== 0 || dragging)) {
            event.accepted = true;
        }
    }
    onActiveFocusChanged: if (!activeFocus && keyDays !== 0) finishKeys(false)

    Connections {
        target: b.view
        function onCancelCountChanged() {
            if (b.dragging) {
                b.cancelled = true;
                b.tl.dragCancel();
            }
        }
    }

    // The track and the % done fill.
    Rectangle {
        id: track
        anchors.fill: parent
        radius: 5
        color: Qt.rgba(Qt.color(b.info.color).r, Qt.color(b.info.color).g, Qt.color(b.info.color).b, b.st.dark ? 0.30 : 0.22)
        border.width: b.selected || b.activeFocus ? 2 : 1
        border.color: b.selected || b.activeFocus ? b.st.text : b.info.color
        // A bar that starts before the range or ends after it has a straight edge there.
        Rectangle {
            visible: b.info.clippedLeft
            anchors.left: parent.left
            width: 6
            height: parent.height
            color: b.info.color
            opacity: 0.5
        }
        Rectangle {
            visible: b.info.clippedRight
            anchors.right: parent.right
            width: 6
            height: parent.height
            color: b.info.color
            opacity: 0.5
        }
    }
    Rectangle {
        objectName: "fill"
        visible: b.info.percent > 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.min(parent.width, parent.width * Math.max(0, b.info.percent) / 100)
        radius: 5
        color: b.info.color
        opacity: 0.9
    }
    Text {
        visible: b.width > 44 && b.info.percent >= 0
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 6
        text: b.info.percent + "%"
        color: b.info.percent > 40 ? "#FFFFFF" : b.st.text
        font.pixelSize: 11
        font.bold: true
    }
    Glyph {
        visible: b.info.locked
        objectName: "lockGlyph"
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 4
        kind: "lock"
        tone: b.st.textMuted
        size: 12
    }

    // After the bar: the agent, and the marks.
    Row {
        id: after
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.right
        anchors.leftMargin: 8
        spacing: 6
        AgentLabel {
            objectName: "barAgent"
            st: b.st
            text: b.info.agentText
            agentKind: b.info.agentKind
            pixelSize: 11
        }
        Glyph {
            visible: b.info.late
            objectName: "badgeLate"
            anchors.verticalCenter: parent.verticalCenter
            kind: "alert"
            tone: b.st.warning
            size: 13
        }
        Glyph {
            visible: b.info.blocked
            objectName: "badgeBlocked"
            anchors.verticalCenter: parent.verticalCenter
            kind: "blocked"
            tone: b.st.danger
            size: 13
        }
        Row {
            visible: b.info.waiting || b.info.notSent
            objectName: "badgeWaiting"
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Glyph {
                anchors.verticalCenter: parent.verticalCenter
                kind: "clock"
                tone: b.info.notSent ? b.st.warning : b.st.textMuted
                size: 12
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: b.info.notSent ? qsTr("Not sent") : qsTr("Waiting to send")
                color: b.info.notSent ? b.st.warning : b.st.textMuted
                font.pixelSize: 11
            }
        }
    }

    HoverHandler {
        id: hover
        cursorShape: b.info.canMove ? Qt.OpenHandCursor : Qt.ForbiddenCursor
    }
    ToolTip.visible: hover.hovered && !b.dragging
    ToolTip.delay: 500
    ToolTip.text: b.info.title + "\n" + b.info.dates + (b.info.percent >= 0 ? "  " + b.info.percent + "%" : "")
        + (b.info.locked && b.info.lockReason !== "" ? "\n" + b.info.lockReason : "")

    TapHandler {
        onTapped: {
            b.forceActiveFocus();
            b.ctrl.select(b.info.key);
        }
    }

    // The drag. The distance is measured from the press, in scene pixels, so the start of the drag (the threshold) counts too.
    DragHandler {
        id: drag
        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus
        readonly property real total: centroid.scenePosition.x - centroid.scenePressPosition.x
        onTotalChanged: {
            if (active && !b.cancelled) {
                b.dragDx = total;
                b.preview(total);
            }
        }
        onActiveChanged: {
            if (active) {
                b.dragging = true;
                b.cancelled = false;
                b.forceActiveFocus();
                b.dragDx = total;
                b.preview(total);
            } else {
                var dx = b.dragDx;
                var cancelled = b.cancelled;
                b.dragging = false;
                b.cancelled = false;
                b.dragDx = 0;
                if (cancelled) {
                    b.tl.dragCancel();
                } else {
                    b.tl.dragCommit(b.info.key, dx);
                }
            }
        }
    }
}
