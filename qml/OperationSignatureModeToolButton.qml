import QtQuick
import QtQuick.Controls

// Compact presentation override for operation detail. The inherited state is
// intentionally faded, matching the compartment visibility controls.
ToolButton {
    id: root

    required property string mode

    readonly property string stateLabel:
        mode === "full" ? qsTr("full signature")
        : mode === "name-and-return-type" ? qsTr("name and return type")
        : mode === "name-only" ? qsTr("name only")
        : mode === "mixed" ? qsTr("mixed selection")
        : qsTr("inherited from diagram")

    signal modeChangeRequested()

    text: mode === "full" ? qsTr("F")
          : mode === "name-and-return-type" ? qsTr("R")
          : mode === "name-only" ? qsTr("N") : qsTr("Sig")
    opacity: mode === "inherit" ? 0.45 : 1.0
    Accessible.name: qsTr("Operation signatures: %1. Click to change.")
                     .arg(stateLabel)
    ToolTip.visible: hovered
    ToolTip.text: Accessible.name
    onClicked: modeChangeRequested()

    background: Rectangle {
        radius: 3
        color: root.hovered ? uiTheme.hoverBackground : uiTheme.surface
        border.color: uiTheme.controlBorder
    }

    contentItem: Label {
        text: root.text
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: root.palette.buttonText
    }
}
