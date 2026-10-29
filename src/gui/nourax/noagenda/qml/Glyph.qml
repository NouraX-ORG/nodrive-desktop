// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// NoAgenda: small vector glyphs drawn from simple shapes (no emoji font is needed, which many Linux systems lack).

import QtQuick

Item {
    id: g

    property string kind: "event"
    property color tone: "#2563EB"
    property int size: 16

    width: size
    height: size

    // event: a small calendar page
    Rectangle {
        visible: g.kind === "event"
        anchors.fill: parent
        anchors.margins: 1
        radius: 3
        color: "transparent"
        border.color: g.tone
        border.width: 1.5
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: parent.height * 0.32
            radius: 2
            color: g.tone
        }
    }

    // to-do: a box
    Rectangle {
        visible: g.kind === "todo"
        anchors.fill: parent
        anchors.margins: 1.5
        radius: 3
        color: "transparent"
        border.color: g.tone
        border.width: 1.5
    }

    // project task: a bar
    Rectangle {
        visible: g.kind === "task"
        anchors.centerIn: parent
        width: parent.width
        height: parent.height * 0.5
        radius: 2
        color: g.tone
    }

    // milestone: a diamond
    Rectangle {
        visible: g.kind === "milestone"
        anchors.centerIn: parent
        width: parent.width * 0.68
        height: width
        rotation: 45
        color: g.tone
    }

    // board card: a card with a line
    Rectangle {
        visible: g.kind === "card"
        anchors.fill: parent
        anchors.margins: 1
        radius: 3
        color: "transparent"
        border.color: g.tone
        border.width: 1.5
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: parent.height * 0.3
            width: parent.width * 0.55
            height: 2
            radius: 1
            color: g.tone
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: parent.height * 0.58
            width: parent.width * 0.35
            height: 2
            radius: 1
            color: g.tone
        }
    }

    // agent: a robot head
    Item {
        visible: g.kind === "robot"
        anchors.fill: parent
        Rectangle {
            width: 2
            height: parent.height * 0.18
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            color: g.tone
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
            width: parent.width * 0.9
            height: parent.height * 0.7
            radius: 3
            color: g.tone
            Rectangle {
                x: parent.width * 0.22
                y: parent.height * 0.3
                width: parent.width * 0.18
                height: width
                radius: width / 2
                color: "#FFFFFF"
            }
            Rectangle {
                x: parent.width * 0.6
                y: parent.height * 0.3
                width: parent.width * 0.18
                height: width
                radius: width / 2
                color: "#FFFFFF"
            }
        }
    }

    // person (Self)
    Item {
        visible: g.kind === "person"
        anchors.fill: parent
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 1
            width: parent.width * 0.42
            height: width
            radius: width / 2
            color: g.tone
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: parent.width * 0.84
            height: parent.height * 0.42
            radius: 4
            color: g.tone
        }
    }

    // people (My team)
    Item {
        visible: g.kind === "people"
        anchors.fill: parent
        Rectangle {
            x: parent.width * 0.05
            y: 2
            width: parent.width * 0.36
            height: width
            radius: width / 2
            color: g.tone
        }
        Rectangle {
            x: parent.width * 0.52
            y: 2
            width: parent.width * 0.36
            height: width
            radius: width / 2
            color: g.tone
            opacity: 0.7
        }
        Rectangle {
            x: 0
            anchors.bottom: parent.bottom
            width: parent.width * 0.6
            height: parent.height * 0.4
            radius: 4
            color: g.tone
        }
        Rectangle {
            x: parent.width * 0.4
            anchors.bottom: parent.bottom
            width: parent.width * 0.6
            height: parent.height * 0.4
            radius: 4
            color: g.tone
            opacity: 0.7
        }
    }

    // late / problem: a circle with an exclamation mark
    Rectangle {
        visible: g.kind === "alert"
        anchors.fill: parent
        radius: width / 2
        color: g.tone
        Text {
            anchors.centerIn: parent
            text: "!"
            color: "#FFFFFF"
            font.bold: true
            font.pixelSize: g.size * 0.75
        }
    }

    // blocked: a circle with a bar
    Rectangle {
        visible: g.kind === "blocked"
        anchors.fill: parent
        radius: width / 2
        color: g.tone
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.55
            height: 2.5
            color: "#FFFFFF"
        }
    }

    // read only: a small lock
    Item {
        visible: g.kind === "lock"
        anchors.fill: parent
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            width: parent.width * 0.5
            height: parent.height * 0.5
            radius: width / 2
            color: "transparent"
            border.color: g.tone
            border.width: 1.5
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: parent.width * 0.8
            height: parent.height * 0.5
            radius: 2
            color: g.tone
        }
    }

    // board link
    Rectangle {
        visible: g.kind === "board"
        anchors.centerIn: parent
        width: parent.width * 0.8
        height: parent.height * 0.8
        radius: 2
        color: "transparent"
        border.color: g.tone
        border.width: 1.5
        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: parent.height * 0.5
            color: g.tone
        }
        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: parent.height * 0.5
            color: g.tone
        }
    }

    // repeats
    Rectangle {
        visible: g.kind === "repeat"
        anchors.fill: parent
        anchors.margins: 2
        radius: width / 2
        color: "transparent"
        border.color: g.tone
        border.width: 1.5
    }

    // waiting to send: a clock
    Item {
        visible: g.kind === "clock"
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: width / 2
            color: "transparent"
            border.color: g.tone
            border.width: 1.5
        }
        Rectangle {
            x: parent.width / 2 - 0.75
            y: parent.height * 0.22
            width: 1.5
            height: parent.height * 0.3
            color: g.tone
        }
        Rectangle {
            x: parent.width / 2 - 0.75
            y: parent.height / 2 - 0.75
            width: parent.width * 0.26
            height: 1.5
            color: g.tone
        }
    }

    // a plus
    Item {
        visible: g.kind === "plus"
        anchors.fill: parent
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.7
            height: 2
            color: g.tone
        }
        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: parent.height * 0.7
            color: g.tone
        }
    }

    // a clash: two overlapping boxes
    Item {
        visible: g.kind === "clash"
        anchors.fill: parent
        Rectangle {
            x: 1
            y: 1
            width: parent.width * 0.62
            height: parent.height * 0.62
            radius: 2
            color: "transparent"
            border.color: g.tone
            border.width: 1.5
        }
        Rectangle {
            x: parent.width * 0.32
            y: parent.height * 0.32
            width: parent.width * 0.62
            height: parent.height * 0.62
            radius: 2
            color: "transparent"
            border.color: g.tone
            border.width: 1.5
        }
    }
}
