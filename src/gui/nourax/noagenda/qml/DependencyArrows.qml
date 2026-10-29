// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The dependency arrows of the Timeline, read only: from the predecessor to the successor, and a short stub for a
// predecessor that is not on the screen. Drawn on a Canvas (part of QtQuick); the tooltips come from small hover areas.

import QtQuick
import QtQuick.Controls

Item {
    id: a

    required property var st
    required property var arrows
    property real contentW: 0
    property real contentH: 0

    width: contentW
    height: contentH

    onArrowsChanged: canvas.requestPaint()
    Connections {
        target: a.st
        function onChanged() { canvas.requestPaint(); }
    }

    Canvas {
        id: canvas
        objectName: "arrowCanvas"
        anchors.fill: parent
        renderTarget: Canvas.Image
        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            ctx.lineWidth = 1.5;
            ctx.lineJoin = "round";
            var color = a.st.dark ? "#CBD5E1" : "#475569";
            ctx.strokeStyle = color;
            ctx.fillStyle = color;
            for (var i = 0; i < a.arrows.length; ++i) {
                var p = a.arrows[i].points;
                if (!p || p.length < 4) {
                    continue;
                }
                ctx.beginPath();
                ctx.moveTo(p[0], p[1]);
                for (var k = 2; k < p.length; k += 2) {
                    ctx.lineTo(p[k], p[k + 1]);
                }
                ctx.stroke();
                // The arrow head at the last point, pointing along the last line.
                var n = p.length;
                var ex = p[n - 2], ey = p[n - 1], sx = p[n - 4], sy = p[n - 3];
                var dx = ex - sx, dy = ey - sy;
                var len = Math.sqrt(dx * dx + dy * dy);
                if (len > 0) {
                    dx /= len;
                    dy /= len;
                    ctx.beginPath();
                    ctx.moveTo(ex, ey);
                    ctx.lineTo(ex - dx * 6 - dy * 3.5, ey - dy * 6 + dx * 3.5);
                    ctx.lineTo(ex - dx * 6 + dy * 3.5, ey - dy * 6 - dx * 3.5);
                    ctx.closePath();
                    ctx.fill();
                }
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
