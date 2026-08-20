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
    // Only the visible DiagramView owns keyboard accelerators. Tab context
    // menus reuse this component but must not register duplicate shortcuts.
    property bool shortcutsEnabled: false
    readonly property bool canvasAvailable:
        canvas !== null && canvas !== undefined
    readonly property bool structuralDiagram:
        canvasAvailable && canvas.diagramType === "class"
    readonly property bool sequenceDiagram:
        canvasAvailable && canvas.diagramType === "sequence"

    signal filterRequested()
    signal displayOptionsRequested()
    signal exportPngRequested()
    signal exportPdfRequested()
    signal exportSvgRequested()
    signal exportDrawioRequested()
    signal automaticLayoutRequested()
    signal connectorOptimizationRequested(string scope)
    signal sequenceMessageRequested()
    signal sequenceCombinedFragmentRequested()
    signal sequenceExecutionContextsRequested()

    function createElement(type) {
        if (!canvasAvailable)
            return
        if (createAtViewportCenter)
            canvas.createElementAtViewportCenter(type)
        else
            canvas.createElementAtContextPosition(type)
    }

    function createNote() {
        if (!canvasAvailable)
            return
        if (createAtViewportCenter)
            canvas.createNoteAtViewportCenter()
        else
            canvas.createNoteAtContextPosition()
    }

    function createSequenceParticipant() {
        if (!canvasAvailable)
            return
        if (createAtViewportCenter)
            canvas.addSequenceParticipantAtViewportCenter()
        else
            canvas.addSequenceParticipantAtContextPosition()
    }

    title: qsTr("Diagram")
    enabled: canvasAvailable

    CatalogMenuItem {
        catalogId: "diagram.displayOptions"
        text: qsTr("Display options…")
        onTriggered: root.displayOptionsRequested()
    }
    MenuItem {
        text: root.canvasAvailable && root.canvas.filterActive
              ? qsTr("Edit active filter…") : qsTr("Filter items…")
        visible: root.structuralDiagram
        height: visible ? implicitHeight : 0
        onTriggered: root.filterRequested()
    }
    MenuItem {
        text: qsTr("Clear filter")
        visible: root.structuralDiagram && root.canvas.filterActive
        height: visible ? implicitHeight : 0
        onTriggered: root.canvas.clearDiagramFilter()
    }
    MenuSeparator {
        visible: root.structuralDiagram
        height: visible ? implicitHeight : 0
    }
    Menu {
        title: qsTr("New element")
        visible: root.structuralDiagram
        height: visible ? implicitHeight : 0
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
    MenuItem {
        text: qsTr("New note")
        visible: root.structuralDiagram
        height: visible ? implicitHeight : 0
        onTriggered: root.createNote()
    }
    CatalogMenuItem {
        catalogId: "sequence.addParticipant"
        text: root.shortcutsEnabled
              ? qsTr("New participant (Ctrl+Shift+P)")
              : qsTr("New participant")
        visible: root.sequenceDiagram
        height: visible ? implicitHeight : 0
        onTriggered: root.createSequenceParticipant()
    }
    MenuItem {
        text: qsTr("Execution contexts…")
        visible: root.sequenceDiagram
        height: visible ? implicitHeight : 0
        onTriggered: root.sequenceExecutionContextsRequested()
    }
    CatalogMenuItem {
        catalogId: "sequence.createMessage"
        text: root.shortcutsEnabled
              ? qsTr("New message… (Ctrl+Shift+M)")
              : qsTr("New message…")
        visible: root.sequenceDiagram
        height: visible ? implicitHeight : 0
        enabled: root.canvas.selectedSequenceParticipantCount === 2
        onTriggered: root.sequenceMessageRequested()
    }
    CatalogMenuItem {
        catalogId: "sequence.addFragment"
        text: root.shortcutsEnabled
              ? qsTr("New combined fragment… (Ctrl+Shift+G)")
              : qsTr("New combined fragment…")
        visible: root.sequenceDiagram
        height: visible ? implicitHeight : 0
        enabled: root.canvas.selectedSequenceParticipantCount > 0
        onTriggered: root.sequenceCombinedFragmentRequested()
    }
    MenuSeparator {}
    CatalogMenuItem {
        catalogId: "diagram.exportPng"
        text: qsTr("Export diagram as PNG…")
        onTriggered: root.exportPngRequested()
    }
    CatalogMenuItem {
        catalogId: "diagram.exportPdf"
        text: qsTr("Export diagram as PDF…")
        onTriggered: root.exportPdfRequested()
    }
    CatalogMenuItem {
        catalogId: "diagram.exportSvg"
        text: qsTr("Export diagram as SVG…")
        onTriggered: root.exportSvgRequested()
    }
    CatalogMenuItem {
        catalogId: "diagram.exportDrawio"
        text: qsTr("Export diagram as Draw.io…")
        onTriggered: root.exportDrawioRequested()
    }
    MenuSeparator {}
    CatalogMenuItem {
        catalogId: "arrange.fitDiagram"
        text: qsTr("Fit diagram")
        onTriggered: root.canvas.fitToContent()
    }
    CatalogMenuItem {
        catalogId: "arrange.autoLayoutDiagram"
        text: qsTr("Auto arrange diagram…")
        visible: root.structuralDiagram
        height: visible ? implicitHeight : 0
        onTriggered: root.automaticLayoutRequested()
    }
    CatalogMenuItem {
        catalogId: "diagram.optimizeVisibleConnectorEndsAndRoute"
        text: qsTr("Optimize connectors…")
        visible: root.structuralDiagram
        height: visible ? implicitHeight : 0
        onTriggered: root.connectorOptimizationRequested("visible")
    }
}
