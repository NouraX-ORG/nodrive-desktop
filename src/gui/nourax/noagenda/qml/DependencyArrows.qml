// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The dependency arrows of the Timeline, read only: from the predecessor to the successor, and a short stub for a
// predecessor that is not on the screen. Every line of an arrow runs along the x or the y axis, so each one is a thin
// rectangle (QtQuick only, drawn with the scene graph: no Canvas, so nothing is painted late or on another thread).
// The arrow head is two short lines at the tip. The tooltips come from small hover areas.

import QtQuick
import QtQuick.Controls

Item {
    id: a

    required property var st
    required property var arrows
    property real contentW: 0
    property real contentH: 0
    readonly property color lineColor: st.dark ? "#CBD5E1" : "#475569"
    readonly property real thickness: 1.5

    width: contentW
    height: contentH

    // [{x, y, w, h}] of every line, and [{x, y, right}] of every head (right = it points to the right).
    function segmentsOf(list) {
        var out = [];
        for (var i = 0; i < list.length; ++i) {
            var p = list[i].points;
            for (var k = 2; k + 1 < p.length; k += 2) {
                var x0 = p[k - 2], y0 = p[k - 1], x1 = p[k], y1 = p[k + 1];
                out.push({ "x": Math.min(x0, x1) - (y0 === y1 ? 0 : thickness / 2), "y": Math.min(y0, y1) - (y0 === y1 ? thickness / 2 : 0),
                    "w": Math.abs(x1 - x0) + (y0 === y1 ? 0 : thickness), "h": Math.abs(y1 - y0) + (y0 === y1 ? thickness : 0) });
            }
        }
        return out;
    }
    function headsOf(list) {
        var out = [];
        for (var i = 0; i < list.length; ++i) {
            var p = list[i].points;
            var n = p.length;
            if (n >= 4) {
                out.push({ "x": p[n - 2], "y": p[n - 1], "right": p[n - 2] >= p[n - 4] });
            }
        }
        return out;
    }
    readonly property var segments: segmentsOf(arrows)
    readonly property var heads: headsOf(arrows)

    Repeater {
        model: a.segments
        delegate: Rectangle {
            required property var modelData
            objectName: "arrowSegment"
            x: modelData.x
            y: modelData.y
            width: modelData.w
            height: modelData.h
            color: a.lineColor
        }
    }
    Repeater {
        model: a.heads
        delegate: Item {
            id: head
            required property var modelData
            objectName: "arrowHead"
            x: modelData.x
            y: modelData.y
            Rectangle {
                width: 7
                height: a.thickness
                x: head.modelData.right ? -7 : 0
                y: -a.thickness / 2
                color: a.lineColor
                rotation: 35
                transformOrigin: head.modelData.right ? Item.Right : Item.Left
            }
            Rectangle {
                width: 7
                height: a.thickness
                x: head.modelData.right ? -7 : 0
                y: -a.thickness / 2
                color: a.lineColor
                rotation: -35
                transformOrigin: head.modelData.right ? Item.Right : Item.Left
            }
        }
    }

    Repeater {
        model: a.arrows
        delegate: Item {
            id: hit
            required property var modelData
            objectName: "arrowHit"
            x: modelData.hx
            y: modelData.hy
            width: modelData.hw
            height: modelData.hh
            HoverHandler {
                id: hover
            }
            ToolTip.visible: hover.hovered
            ToolTip.delay: 400
            ToolTip.text: hit.modelData.tooltip
        }
    }
}
