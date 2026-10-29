// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// Behind the Timeline: my busy time. Each day is tinted by the share of my working hours that my own busy events take
// (the more busy, the stronger); a day off is hatched. The line of today is drawn on top of the tint.

import QtQuick

Item {
    id: s

    required property var st
    required property var shading
    property real todayX: -1
    property real contentH: 0

    height: contentH

    Repeater {
        model: s.shading
        delegate: Item {
            id: day
            required property var modelData
            objectName: modelData.dayOff ? "dayOff" : "busyDay"
            x: modelData.x
            width: modelData.w
            height: s.contentH
            Rectangle {
                anchors.fill: parent
                visible: !day.modelData.dayOff && day.modelData.alpha > 0
                color: Qt.rgba(s.st.primary.r, s.st.primary.g, s.st.primary.b, day.modelData.alpha)
            }
            Rectangle {
                anchors.fill: parent
                visible: day.modelData.dayOff
                color: s.st.surfaceAlt
                opacity: 0.55
            }
            Canvas {
                visible: day.modelData.dayOff
                anchors.fill: parent
                renderTarget: Canvas.Image
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.clearRect(0, 0, width, height);
                    ctx.strokeStyle = "" + s.st.textMuted;
                    ctx.globalAlpha = 0.35;
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    for (var y = -width; y < height; y += 8) {
                        ctx.moveTo(0, y + width);
                        ctx.lineTo(width, y);
                    }
                    ctx.stroke();
                }
            }
        }
    }
    Rectangle {
        visible: s.todayX >= 0
        objectName: "todayLine"
        x: s.todayX
        width: 2
        height: s.contentH
        color: s.st.primary
        opacity: 0.8
    }
}
