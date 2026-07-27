import QtQuick
import QtQuick.Controls

Menu {
    id: root

    // This is the explicit assignment at the selected scope. An empty value
    // means the presentation or browser subject inherits from its parent.
    property string assignedStyleId: ""
    signal styleChosen(string styleId)
    signal manageRequested()

    title: qsTr("Style")

    CatalogMenuItem {
        catalogId: "style.inherit"
        text: qsTr("Inherit")
        checkable: true
        checked: root.assignedStyleId.length === 0
        onTriggered: root.styleChosen("")
    }
    MenuSeparator {}

    Instantiator {
        model: projectController.diagramStyles

        delegate: CatalogMenuItem {
            required property var modelData
            catalogId: "style.assignNamed"
            text: modelData.name
            checkable: true
            checked: root.assignedStyleId === modelData.id
            onTriggered: root.styleChosen(modelData.id)
        }

        // Keep the generated style entries between Inherit and the final
        // management action. Menu does not adopt Instantiator objects itself.
        onObjectAdded: function(index, object) {
            root.insertItem(index + 2, object)
        }
        onObjectRemoved: function(index, object) {
            root.removeItem(object)
        }
    }

    MenuSeparator {}
    CatalogMenuItem {
        catalogId: "style.manage"
        text: qsTr("Manage styles…")
        onTriggered: root.manageRequested()
    }
}
