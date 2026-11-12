import QtQuick
import QtQuick.Controls
import QtQml.Models

Menu {
    id: root
    required property string diagramType
    property bool forDestination: false
    readonly property bool activity: diagramType === "activity"
    readonly property bool structural: diagramType === "class"
    signal nodeTypeSelected(string type)

    title: activity ? qsTr("Add activity node") : qsTr("Add element")

    readonly property var classTypes: [
        { type: "class", label: qsTr("Class"), iconId: "createElement.class" },
        { type: "struct", label: qsTr("Struct"), iconId: "createElement.struct" },
        { type: "enumeration", label: qsTr("Enumeration"), iconId: "createElement.enumeration" },
        { type: "package", label: qsTr("Package"), iconId: "createElement.package", standalone: true }
    ]
    readonly property var activityTypes: [
        { type: "action", label: qsTr("Action") },
        { type: "initial", label: qsTr("Initial node"), standalone: true },
        { type: "final", label: qsTr("Activity final") },
        { type: "decision", label: qsTr("Decision") },
        { type: "merge", label: qsTr("Merge") },
        { type: "fork", label: qsTr("Fork") },
        { type: "join", label: qsTr("Join") }
    ]

    // Give the menu only applicable rows. Hidden zero-height items can still
    // participate in Qt Quick Controls' positioning and keyboard navigation.
    Instantiator {
        model: {
            const choices = root.activity ? root.activityTypes
                                          : root.structural ? root.classTypes : []
            return root.forDestination ? choices.filter(item => !item.standalone) : choices
        }
        delegate: MenuItem {
            required property var modelData
            text: modelData.label
            icon.source: modelData.iconId ? iconRegistry.actionIcon(modelData.iconId) : ""
            icon.width: modelData.iconId ? iconRegistry.defaultSize : 0
            icon.height: modelData.iconId ? iconRegistry.defaultSize : 0
            onTriggered: root.nodeTypeSelected(modelData.type)
        }
        onObjectAdded: function(index, object) { root.insertItem(index, object) }
        onObjectRemoved: function(index, object) { root.removeItem(object) }
    }
}
