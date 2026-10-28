// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// One item of the list: kind glyph, title, time, source name in the source's colour, the agent, badges.

import QtQuick

Rectangle {
    id: r

    required property var st
    property var row: ({})
    property bool selected: false
    signal activated

    height: Math.max(52, content.implicitHeight + 16)
    radius: 8
    color: selected ? st.selected : (area.containsMouse ? st.hover : st.surface)
    border.width: selected ? 1 : 0
    border.color: st.primary
    opacity: row.info ? 0.85 : 1

    Accessible.role: Accessible.ListItem
    Accessible.name: row.title + ", " + row.timeText + ", " + row.sourceName + ", " + row.agentText

    Rectangle {
        id: stripe
        width: 4
        radius: 2
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 6
        color: r.row.sourceColor ? r.row.sourceColor : r.st.primary
        opacity: r.row.dim ? 0.45 : 1
    }

    Item {
        id: content
        anchors.left: stripe.right
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        implicitHeight: Math.max(titleRow.implicitHeight + detailRow.implicitHeight + 4, 28)

        Row {
            id: titleRow
            anchors.left: parent.left
            anchors.right: timeLabel.left
            anchors.rightMargin: 8
            anchors.top: parent.top
            spacing: 8

            Glyph {
                id: glyph
                anchors.verticalCenter: parent.verticalCenter
                kind: r.row.glyph ? r.row.glyph : "event"
                tone: r.row.sourceColor ? r.row.sourceColor : r.st.primary
                size: 16
                opacity: r.row.dim ? 0.6 : 1
            }
            Text {
                id: title
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(40, titleRow.width - glyph.width - badgeRow.width - 24)
                text: r.row.title
                elide: Text.ElideRight
                color: r.st.text
                font.pixelSize: 14
                font.bold: false
                font.italic: r.row.tentative === true
                opacity: (r.row.transparent === true || r.row.dim === true) ? 0.7 : 1
            }
            Row {
                id: badgeRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Glyph {
                    visible: (r.row.badges || []).indexOf("late") >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "alert"
                    tone: r.st.warning
                    size: 14
                }
                Glyph {
                    visible: (r.row.badges || []).indexOf("blocked") >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "blocked"
                    tone: r.st.danger
                    size: 14
                }
                Glyph {
                    visible: (r.row.badges || []).indexOf("flag") >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "alert"
                    tone: r.st.warning
                    size: 14
                }
                Glyph {
                    visible: (r.row.badges || []).indexOf("board") >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "board"
                    tone: r.st.textMuted
                    size: 14
                }
                Glyph {
                    visible: (r.row.badges || []).indexOf("repeating") >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "repeat"
                    tone: r.st.textMuted
                    size: 12
                }
                Glyph {
                    visible: (r.row.badges || []).indexOf("lock") >= 0
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "lock"
                    tone: r.st.textMuted
                    size: 12
                }
            }
        }

        Text {
            id: timeLabel
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 1
            text: r.row.timeText ? r.row.timeText : ""
            color: r.st.textMuted
            font.pixelSize: 12
            opacity: r.row.dim ? 0.7 : 1
        }

        Row {
            id: detailRow
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.right: parent.right
            anchors.top: titleRow.bottom
            anchors.topMargin: 4
            spacing: 10

            Text {
                visible: text !== ""
                text: r.row.sourceName ? r.row.sourceName : ""
                color: r.row.sourceColor ? r.row.sourceColor : r.st.primary
                font.pixelSize: 12
                elide: Text.ElideRight
                width: Math.min(implicitWidth, detailRow.width * 0.4)
            }
            Text {
                visible: r.row.workKind === "team" && r.row.personName !== ""
                text: r.row.personName ? r.row.personName : ""
                color: r.st.text
                font.pixelSize: 12
            }
            AgentLabel {
                visible: !r.row.info
                st: r.st
                text: r.row.agentText ? r.row.agentText : ""
                agentKind: r.row.agentKind ? r.row.agentKind : "self"
            }
            Text {
                visible: text !== ""
                text: r.row.workKind === "team" ? (r.row.problemText ? r.row.problemText : "") : (r.row.lateText ? r.row.lateText : "")
                color: r.st.warning
                font.pixelSize: 12
                elide: Text.ElideRight
                width: Math.min(implicitWidth, detailRow.width * 0.45)
            }
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: r.activated()
    }
}
