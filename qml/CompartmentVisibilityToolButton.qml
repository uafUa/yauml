import QtQuick
import QtQuick.Controls

// Compact three-state compartment control used in the presentation toolbox.
//
// The visual vocabulary intentionally resembles a physical toggle:
// accent-filled means explicitly shown, released means explicitly hidden, and
// faded means inherited from the diagram.
ToolButton {
    id: root

    required property string compartment
    required property string visibilityState

    readonly property bool attributes: compartment === "attributes"
    readonly property string compartmentLabel:
        attributes ? qsTr("Attributes") : qsTr("Operations")
    readonly property string stateLabel:
        visibilityState === "show" ? qsTr("shown")
        : visibilityState === "hide" ? qsTr("hidden")
        : qsTr("inherited")
    readonly property string nextVisibilityState:
        visibilityState === "inherit" ? "show"
        : visibilityState === "show" ? "hide" : "inherit"

    signal visibilityChangeRequested(string state)

    text: attributes ? qsTr("A") : qsTr("O")
    opacity: visibilityState === "inherit" ? 0.45 : 1.0
    Accessible.name: qsTr("%1: %2. Click to change.")
                     .arg(compartmentLabel).arg(stateLabel)
    ToolTip.visible: hovered
    ToolTip.text: Accessible.name
    onClicked: visibilityChangeRequested(nextVisibilityState)

    background: Rectangle {
        radius: 3
        color: root.visibilityState === "show"
               ? uiTheme.accent
               : root.hovered ? uiTheme.hoverBackground : uiTheme.surface
        border.color: uiTheme.controlBorder
    }

    contentItem: Label {
        text: root.text
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: root.visibilityState === "show"
               ? root.palette.highlightedText : root.palette.buttonText
    }
}
