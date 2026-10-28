// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later

// The agent of a row: the robot marker and the agent's name, or the person marker and "Self".

import QtQuick

Row {
    id: a

    required property var st
    property string text: ""
    property string agentKind: "self" // self | agent | unknown | retired | paused | other
    property int pixelSize: 12

    spacing: 4

    Glyph {
        anchors.verticalCenter: parent.verticalCenter
        kind: a.agentKind === "self" ? "person" : "robot"
        tone: (a.agentKind === "unknown" || a.agentKind === "retired") ? a.st.warning : (a.agentKind === "paused" ? a.st.textMuted : a.st.primary)
        size: a.pixelSize + 2
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: a.text
        color: (a.agentKind === "unknown" || a.agentKind === "retired") ? a.st.warning : (a.agentKind === "agent" ? a.st.text : a.st.textMuted)
        opacity: a.agentKind === "paused" ? 0.6 : 1
        font.pixelSize: a.pixelSize
    }
}
