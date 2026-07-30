import QtQuick
import QtQuick.Controls

// Diagram-level commands shared by the canvas background and diagram tabs.
//
// A background invocation creates elements at the pointer. A tab has no
// meaningful scene position, so it uses the center of the visible viewport.
Menu {
    id: root

    required property var canvas
    property bool createAtViewportCenter: false
    readonly property bool canvasAvailable:
        canvas !== null && canvas !== undefined

    signal filterRequested()
    signal exportPngRequested()

    function createElement(type) {
        if (!canvasAvailable)
            return
        if (createAtViewportCenter)
            canvas.createElementAtViewportCenter(type)
        else
            canvas.createElementAtContextPosition(type)
    }

    title: qsTr("Diagram")
    enabled: canvasAvailable

    MenuItem {
        text: root.canvasAvailable && root.canvas.filterActive
              ? qsTr("Edit active filter…") : qsTr("Filter items…")
        onTriggered: root.filterRequested()
    }
    MenuItem {
        text: qsTr("Clear filter")
        visible: root.canvasAvailable && root.canvas.filterActive
        height: visible ? implicitHeight : 0
        onTriggered: root.canvas.clearDiagramFilter()
    }
    MenuSeparator {}
    Menu {
        title: qsTr("New element")
        CatalogMenuItem {
            catalogId: "createElement.package"
            text: qsTr("Package")
            onTriggered: root.createElement("package")
        }
        CatalogMenuItem {
            catalogId: "createElement.class"
            text: qsTr("Class")
            onTriggered: root.createElement("class")
        }
        CatalogMenuItem {
            catalogId: "createElement.struct"
            text: qsTr("Struct")
            onTriggered: root.createElement("struct")
        }
        CatalogMenuItem {
            catalogId: "createElement.enumeration"
            text: qsTr("Enumeration")
            onTriggered: root.createElement("enumeration")
        }
    }
    MenuSeparator {}
    Menu {
        title: qsTr("Compartment visibility")
        MenuItem {
            text: qsTr("Show attributes")
            checkable: true
            checked: root.canvasAvailable
                     && root.canvas.diagramAttributesVisible
            onTriggered: root.canvas.setDiagramCompartmentVisible(
                             "attributes",
                             !root.canvas.diagramAttributesVisible)
        }
        MenuItem {
            text: qsTr("Show operations")
            checkable: true
            checked: root.canvasAvailable
                     && root.canvas.diagramOperationsVisible
            onTriggered: root.canvas.setDiagramCompartmentVisible(
                             "operations",
                             !root.canvas.diagramOperationsVisible)
        }
        MenuSeparator {}
        Menu {
            title: qsTr("Operation signatures")
            MenuItem {
                text: qsTr("Full signature")
                checkable: true
                checked: root.canvasAvailable
                         && root.canvas.diagramOperationSignatureMode === "full"
                onTriggered: root.canvas.setDiagramOperationSignatureMode(
                                 "full")
            }
            MenuItem {
                text: qsTr("Name + return type")
                checkable: true
                checked: root.canvasAvailable
                         && root.canvas.diagramOperationSignatureMode
                            === "name-and-return-type"
                onTriggered: root.canvas.setDiagramOperationSignatureMode(
                                 "name-and-return-type")
            }
            MenuItem {
                text: qsTr("Name only")
                checkable: true
                checked: root.canvasAvailable
                         && root.canvas.diagramOperationSignatureMode
                            === "name-only"
                onTriggered: root.canvas.setDiagramOperationSignatureMode(
                                 "name-only")
            }
        }
    }
    MenuSeparator {}
    CatalogMenuItem {
        catalogId: "diagram.exportPng"
        text: qsTr("Export diagram as PNG…")
        onTriggered: root.exportPngRequested()
    }
    MenuSeparator {}
    CatalogMenuItem {
        catalogId: "arrange.fitDiagram"
        text: qsTr("Fit diagram")
        onTriggered: root.canvas.fitToContent()
    }
}
