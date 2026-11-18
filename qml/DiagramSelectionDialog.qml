import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

CatalogDialog {
    id: root
    property string scopeContainerId: ""
    property string scopeName: ""
    signal selectionRequested(string category, string type, bool includeNested,
                              string scopeContainerId)

    function openFor(containerId, name) {
        scopeContainerId = containerId
        scopeName = name
        open()
    }

    readonly property var entityTypes: [
        { value: "all", label: qsTr("All entities") },
        { value: "class", label: qsTr("Classes") },
        { value: "struct", label: qsTr("Structs") },
        { value: "enumeration", label: qsTr("Enumerations") },
        { value: "package", label: qsTr("Packages / namespaces") },
        { value: "folder", label: qsTr("Folders") },
        { value: "note", label: qsTr("Notes") }
    ]
    readonly property var relationTypes: [
        { value: "all", label: qsTr("All relationships") },
        { value: "dependency", label: qsTr("Dependency") },
        { value: "association", label: qsTr("Association") },
        { value: "aggregation", label: qsTr("Aggregation") },
        { value: "composition", label: qsTr("Composition") },
        { value: "generalization", label: qsTr("Generalization / inheritance") },
        { value: "realization", label: qsTr("Realization / implementation") },
        { value: "containment", label: qsTr("Containment") }
    ]

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(430, parent.width - 40)
    modal: true
    focus: true
    title: qsTr("Select all by type")
    standardButtons: Dialog.Ok | Dialog.Cancel
    onOpened: category.forceActiveFocus()
    onAccepted: selectionRequested(category.currentValue,
                                   category.currentValue === "all" ? "all" : type.currentValue,
                                   nested.checked, scopeContainerId)

    Shortcut {
        sequences: ["Return", "Enter"]
        context: Qt.WindowShortcut
        enabled: root.opened && !category.popup.visible && !type.popup.visible
        onActivated: root.accept()
    }

    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Scope: %1").arg(root.scopeName)
        }
        Label { text: qsTr("Select") }
        ComboBox {
            id: category
            objectName: "selectionCategory"
            Layout.fillWidth: true
            textRole: "label"
            valueRole: "value"
            model: [
                { value: "all", label: qsTr("Everything") },
                { value: "entities", label: qsTr("Entities") },
                { value: "relations", label: qsTr("Relationships") }
            ]
            onActivated: type.currentIndex = 0
        }
        Label {
            visible: type.visible
            text: qsTr("Type")
        }
        ComboBox {
            id: type
            objectName: "selectionType"
            Layout.fillWidth: true
            visible: category.currentValue !== "all"
            textRole: "label"
            valueRole: "value"
            model: category.currentValue === "relations" ? root.relationTypes : root.entityTypes
        }
        CheckBox {
            id: nested
            objectName: "selectionIncludeNested"
            text: qsTr("Include nested contents")
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Relationships are selected when both ends are in this scope. Diagram filters still apply.")
        }
    }
}
