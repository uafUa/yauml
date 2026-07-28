import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Yauml.Native

Item {
    id: root
    required property string diagramId
    readonly property alias diagramCanvas: canvas
    property bool relationshipToolboxGestureActive: false
    property string relationshipToolboxNodeId
    property string relationshipToolboxEdge
    property real relationshipToolboxSceneX: 0
    property real relationshipToolboxSceneY: 0

    readonly property var relationshipToolboxActions: [
        {
            type: "dependency",
            actionId: "createRelationship.dependency",
            label: qsTr("Dependency")
        },
        {
            type: "realization",
            actionId: "createRelationship.realization",
            label: qsTr("Implementation")
        },
        {
            type: "generalization",
            actionId: "createRelationship.generalization",
            label: qsTr("Inheritance")
        },
        {
            type: "association",
            actionId: "createRelationship.association",
            label: qsTr("Association")
        },
        {
            type: "aggregation",
            actionId: "createRelationship.aggregation",
            label: qsTr("Aggregation")
        },
        {
            type: "composition",
            actionId: "createRelationship.composition",
            label: qsTr("Composition")
        },
        {
            type: "containment",
            actionId: "createRelationship.containment",
            label: qsTr("Containment")
        }
    ]
    readonly property var arrangementToolboxActions: [
        {
            actionId: "arrange.alignLeft",
            action: alignLeftAction,
            fallback: qsTr("L")
        },
        {
            actionId: "arrange.alignHorizontalCenters",
            action: alignHorizontalCenterAction,
            fallback: qsTr("HC")
        },
        {
            actionId: "arrange.alignRight",
            action: alignRightAction,
            fallback: qsTr("R")
        },
        {
            actionId: "arrange.alignTop",
            action: alignTopAction,
            fallback: qsTr("T")
        },
        {
            actionId: "arrange.alignVerticalCenters",
            action: alignVerticalCenterAction,
            fallback: qsTr("VC")
        },
        {
            actionId: "arrange.alignBottom",
            action: alignBottomAction,
            fallback: qsTr("B")
        },
        {
            actionId: "arrange.matchWidth",
            action: matchWidthAction,
            fallback: qsTr("W")
        },
        {
            actionId: "arrange.matchHeight",
            action: matchHeightAction,
            fallback: qsTr("H")
        },
        {
            actionId: "arrange.matchSize",
            action: matchSizeAction,
            fallback: qsTr("WH")
        },
        {
            actionId: "arrange.distributeHorizontally",
            action: distributeHorizontallyAction,
            fallback: qsTr("DH")
        },
        {
            actionId: "arrange.distributeVertically",
            action: distributeVerticallyAction,
            fallback: qsTr("DV")
        },
        {
            actionId: "arrange.fitToContent",
            action: fitSelectionAction,
            fallback: qsTr("Fit")
        },
        {
            actionId: "style.assignNamed",
            kind: "style",
            fallback: qsTr("Style"),
            label: qsTr("Choose style for selection")
        }
    ]
    readonly property var connectorToolboxActions: [
        {
            actionId: "connector.routeStraight",
            kind: "routing",
            value: "straight",
            fallback: qsTr("—"),
            label: qsTr("Use straight routing")
        },
        {
            actionId: "connector.routeOrthogonal",
            kind: "routing",
            value: "orthogonal",
            fallback: qsTr("⌞"),
            label: qsTr("Use orthogonal routing")
        },
        {
            actionId: "connector.editName",
            kind: "annotation",
            field: "name",
            fallback: qsTr("N"),
            label: qsTr("Edit relationship name")
        },
        {
            actionId: "connector.editSourceRole",
            kind: "annotation",
            field: "sourceRole",
            fallback: qsTr("SR"),
            label: qsTr("Edit source role")
        },
        {
            actionId: "connector.editSourceMultiplicity",
            kind: "annotation",
            field: "sourceMultiplicity",
            fallback: qsTr("S#"),
            label: qsTr("Edit source cardinality")
        },
        {
            actionId: "connector.editTargetRole",
            kind: "annotation",
            field: "targetRole",
            fallback: qsTr("TR"),
            label: qsTr("Edit target role")
        },
        {
            actionId: "connector.editTargetMultiplicity",
            kind: "annotation",
            field: "targetMultiplicity",
            fallback: qsTr("T#"),
            label: qsTr("Edit target cardinality")
        },
        {
            actionId: "connector.editStereotypes",
            kind: "annotation",
            field: "stereotypes",
            fallback: qsTr("«»"),
            label: qsTr("Choose relationship stereotypes")
        },
        {
            actionId: "connector.resetAnnotationPositions",
            kind: "reset",
            fallback: qsTr("↺"),
            label: qsTr("Reset all annotation positions")
        }
    ]
    readonly property var presentationToolboxActions: [
        {
            actionId: "presentation.editName",
            kind: "editName",
            fallback: qsTr("Aa"),
            label: qsTr("Edit name")
        },
        {
            actionId: "presentation.attributesVisibility",
            kind: "attributesVisibility",
            fallback: qsTr("A"),
            label: qsTr("Cycle attributes visibility")
        },
        {
            actionId: "presentation.operationsVisibility",
            kind: "operationsVisibility",
            fallback: qsTr("O"),
            label: qsTr("Cycle operations visibility")
        },
        {
            actionId: "presentation.connectorSnapPoints",
            kind: "snapPoints",
            fallback: qsTr("Snap"),
            label: qsTr("Connector snap points")
        },
        {
            actionId: "arrange.fitToContent",
            kind: "fit",
            fallback: qsTr("Fit"),
            label: qsTr("Fit to content")
        },
        {
            actionId: "style.assignNamed",
            kind: "style",
            fallback: qsTr("Style"),
            label: qsTr("Choose style")
        },
        {
            actionId: "presentation.addIncomingRelatedTypes",
            kind: "incoming",
            fallback: qsTr("←+"),
            label: qsTr("Add types that depend on this")
        },
        {
            actionId: "presentation.addOutgoingRelatedTypes",
            kind: "outgoing",
            fallback: qsTr("+→"),
            label: qsTr("Add types this depends on")
        },
        {
            actionId: "presentation.wrapInNamespace",
            kind: "wrap",
            fallback: qsTr("Wrap"),
            label: qsTr("Wrap in parent namespace")
        }
    ]

    function configuredToolboxActions(toolboxId, descriptors) {
        // Reading the property here makes every toolbox react immediately when
        // Preferences is accepted, including diagram views in detached windows.
        const configuration = applicationSettings.contextToolboxConfiguration
        const entries = configuration[toolboxId] || []
        const descriptorById = {}
        for (let index = 0; index < descriptors.length; ++index)
            descriptorById[descriptors[index].actionId] = descriptors[index]

        const result = []
        for (let index = 0; index < entries.length; ++index) {
            const entry = entries[index]
            const descriptor = descriptorById[entry.actionId]
            if (entry.enabled && descriptor)
                result.push(descriptor)
        }
        return result
    }
    readonly property var activeRelationshipToolboxActions:
        configuredToolboxActions("relationship", relationshipToolboxActions)
    readonly property var activeArrangementToolboxActions:
        configuredToolboxActions("selection", arrangementToolboxActions)
    readonly property var activeConnectorToolboxActions:
        configuredToolboxActions("connector", connectorToolboxActions)
    readonly property var activePresentationToolboxActions:
        configuredToolboxActions("presentation", presentationToolboxActions)

    function presentationToolboxHasApplicableAction() {
        for (let index = 0;
             index < activePresentationToolboxActions.length; ++index) {
            const kind = activePresentationToolboxActions[index].kind
            if (canvas.presentationToolboxKind === "node")
                return true
            if (kind !== "incoming" && kind !== "outgoing" && kind !== "wrap"
                    && kind !== "snapPoints"
                    && kind !== "attributesVisibility"
                    && kind !== "operationsVisibility")
                return true
        }
        return false
    }

    function addElementsAt(elementIds, x, y) {
        canvas.addElementsAt(elementIds, x, y)
    }

    function addTreeItemsAt(elementIds, subjectsJson, x, y) {
        canvas.addTreeItemsAt(elementIds, subjectsJson, x, y)
    }

    function latchRelationshipToolboxCandidate() {
        relationshipToolboxNodeId = canvas.relationshipToolboxNodeId
        relationshipToolboxEdge = canvas.relationshipToolboxEdge
        relationshipToolboxSceneX = canvas.relationshipToolboxSceneAnchor.x
        relationshipToolboxSceneY = canvas.relationshipToolboxSceneAnchor.y
    }

    function dismissRelationshipToolbox() {
        relationshipToolbox.dismiss()
    }

    function openDiagramFilterDialog() {
        diagramFilterDialog.openForCanvas(canvas)
    }

    function openSelectedPortSnapPointsDialog() {
        horizontalSnapPoints.value = canvas.selectedHorizontalPortSnapPoints
        verticalSnapPoints.value = canvas.selectedVerticalPortSnapPoints
        portSnapPointsDialog.open()
    }

    function openPresentationTransferDialog(movePresentations) {
        presentationTransferDialog.movePresentations = movePresentations
        presentationTransferDialog.targets =
                projectController.diagramTransferTargets(root.diagramId)
        transferTarget.currentIndex =
                presentationTransferDialog.targets.length > 1 ? 1 : 0
        transferDiagramName.clear()
        presentationTransferDialog.open()
    }

    DiagramCanvas {
        id: canvas
        anchors.fill: parent
        project: projectController
        diagramId: root.diagramId
        defaultDistributionGap: applicationSettings.defaultDistributionGap
        snapToGridEnabled: applicationSettings.snapToGridEnabled
        alignmentGuidesEnabled: applicationSettings.alignmentGuidesEnabled
        gridSpacing: applicationSettings.gridSpacing
        diagramItemSizingMode: applicationSettings.diagramItemSizingMode
        defaultConnectorRouting: applicationSettings.defaultConnectorRouting
        relationshipGestureKeys: applicationSettings.relationshipGestureKeys

        onContextMenuRequested: function(target, menuX, menuY) {
            const menu = target === "element" ? elementMenu
                       : target === "connector" ? connectorMenu
                       : target === "container" ? containerMenu : canvasMenu
            menu.x = menuX
            menu.y = menuY
            menu.open()
        }

        onActiveFocusChanged: {
            if (activeFocus)
                workspaceController.activeDiagramId = root.diagramId
        }

        onEditRequested: function(objectId, field, index, text, x, y, width, height,
                                  fontPixelSize, fontBold) {
            editor.objectId = objectId
            editor.field = field
            editor.fieldIndex = index
            editor.originalText = text
            editor.text = text
            editor.x = Math.max(2, x)
            // QQuick TextInput's glyph baseline sits slightly below the atlas
            // text baseline. Compensate for detail rows; bold headers already
            // align correctly.
            editor.y = Math.max(2, y - (fontBold ? 0 : 2))
            editor.width = Math.max(100, width)
            editor.height = Math.max(28, height)
            editor.font.pixelSize = fontPixelSize
            editor.font.bold = fontBold
            editor.visible = true
            editor.forceActiveFocus()
            editor.selectAll()
        }

        onStereotypeEditRequested: function(objectId, objectKind,
                                            x, y, width, height) {
            diagramStereotypeDropdown.openAt(canvas, x, y + height,
                                             objectKind, objectId)
        }

    }

    Connections {
        target: uiTheme
        function onPaletteChanged() { canvas.refreshTheme() }
    }

    Connections {
        target: canvas

        function onRelationshipToolboxCandidateChanged() {
            if (root.relationshipToolboxGestureActive)
                return
            if (canvas.relationshipToolboxCandidate && !editor.visible)
                root.latchRelationshipToolboxCandidate()
        }

        function onViewportChanged() {
            root.dismissRelationshipToolbox()
            arrangementToolbox.dismiss()
            connectorToolbox.dismiss()
            presentationToolbox.dismiss()
        }

        function onCanvasSelectionChanged() {
            if (!root.relationshipToolboxGestureActive
                    && canvas.connectorInteractionPrompt.length === 0)
                root.dismissRelationshipToolbox()
            if (canvas.selectedNodeCount < 2)
                arrangementToolbox.dismiss()
            if (!canvas.connectorSelected)
                connectorToolbox.dismiss()
            if (canvas.selectedNodeCount !== 1
                    && !canvas.containerSelected)
                presentationToolbox.dismiss()
        }

        function onContextToolboxesDismissRequested() {
            root.dismissRelationshipToolbox()
            arrangementToolbox.dismiss()
            connectorToolbox.dismiss()
            presentationToolbox.dismiss()
        }
    }

    CatalogDialog {
        id: portSnapPointsDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(430, parent.width - 40)
        modal: true
        focus: true
        title: qsTr("Connector snap points")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: canvas.setSelectedPortSnapPoints(
                        horizontalSnapPoints.value,
                        verticalSnapPoints.value)

        contentItem: GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 10

            Label {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Snap points are evenly distributed along each side. Odd values preserve the point at the exact center.")
            }
            Label { text: qsTr("Top and bottom") }
            SpinBox {
                id: horizontalSnapPoints
                from: 1
                to: 31
                stepSize: 2
                editable: true
                validator: RegularExpressionValidator {
                    regularExpression: /^(?:[13579]|[12][13579]|31)$/
                }
            }
            Label { text: qsTr("Left and right") }
            SpinBox {
                id: verticalSnapPoints
                from: 1
                to: 31
                stepSize: 2
                editable: true
                validator: RegularExpressionValidator {
                    regularExpression: /^(?:[13579]|[12][13579]|31)$/
                }
            }
        }
    }

    CatalogDialog {
        id: presentationTransferDialog
        property bool movePresentations: false
        property var targets: []

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(430, parent.width - 40)
        modal: true
        focus: true
        title: movePresentations
               ? qsTr("Move presentations to diagram")
               : qsTr("Copy presentations to diagram")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            const targetDiagramId = canvas.transferSelectedPresentations(
                        transferTarget.currentValue,
                        movePresentations,
                        transferDiagramName.text)
            if (targetDiagramId.length > 0)
                workspaceController.activeDiagramId = targetDiagramId
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: presentationTransferDialog.movePresentations
                      ? qsTr("Move the selected presentations to:")
                      : qsTr("Copy the selected presentations to:")
            }
            ComboBox {
                id: transferTarget
                Layout.fillWidth: true
                model: presentationTransferDialog.targets
                textRole: "name"
                valueRole: "id"
            }
            Label {
                visible: transferTarget.currentValue === ""
                text: qsTr("New diagram name (optional)")
            }
            TextField {
                id: transferDiagramName
                visible: transferTarget.currentValue === ""
                Layout.fillWidth: true
                placeholderText: qsTr("Use the default name")
                Keys.onReturnPressed: presentationTransferDialog.accept()
            }
        }
    }

    ProjectStyleDialog {
        id: presentationStyleDialog
        onStyleChosen: function(styleId) {
            canvas.assignStyleToSelection(styleId)
        }
    }

    StyleAssignmentMenu {
        id: presentationStyleQuickMenu
        assignedStyleId: canvas.selectedStyleId
        onStyleChosen: function(styleId) {
            canvas.assignStyleToSelection(styleId)
        }
        onManageRequested: presentationStyleDialog.openFor(
                               canvas.selectedStyleId)
    }

    ProjectStereotypeDialog {
        id: diagramStereotypeDialog
    }

    DiagramFilterDialog {
        id: diagramFilterDialog
        objectName: "diagramFilterDialog"
    }

    StereotypeDropdown {
        id: diagramStereotypeDropdown
        showField: false
        onManageRequested: diagramStereotypeDialog.openManager()
    }

    Label {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        padding: 6
        text: Math.round(canvas.zoom * 100) + "%"
        color: uiTheme.zoomText
        background: Rectangle {
            radius: 4
            color: uiTheme.badgeBackground
            border.color: uiTheme.badgeBorder
            opacity: 0.96
        }
    }

    Button {
        id: filterIndicator
        objectName: "diagramFilterIndicator"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 10
        anchors.topMargin: 52
        z: 10
        highlighted: canvas.filterActive
        text: canvas.filterActive
              ? qsTr("Filtered: %1 of %2").arg(canvas.visibleNodeCount)
                    .arg(canvas.totalNodeCount)
              : qsTr("Filter…")
        Accessible.name: canvas.filterActive
                         ? qsTr("Diagram filter is active. %1 of %2 items are visible.")
                               .arg(canvas.visibleNodeCount)
                               .arg(canvas.totalNodeCount)
                         : qsTr("Filter diagram items")
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
        onClicked: root.openDiagramFilterDialog()
    }

    DiagramCanvasMenu {
        id: canvasMenu
        canvas: root.diagramCanvas
        onFilterRequested: root.openDiagramFilterDialog()
    }

    Menu {
        id: elementMenu
        title: canvas.selectedNodeCount > 1
               ? qsTr("Selected elements") : qsTr("Element")
        Menu {
            title: qsTr("Create relationship")
            enabled: canvas.selectedNodeCount === 2
            CatalogMenuItem {
                catalogId: "createRelationship.dependency"
                text: qsTr("Dependency")
                onTriggered: canvas.createRelationship("dependency")
            }
            CatalogMenuItem {
                catalogId: "createRelationship.realization"
                text: qsTr("Realization (implementation)")
                onTriggered: canvas.createRelationship("realization")
            }
            CatalogMenuItem {
                catalogId: "createRelationship.generalization"
                text: qsTr("Generalization (inheritance)")
                onTriggered: canvas.createRelationship("generalization")
            }
            MenuSeparator {}
            CatalogMenuItem {
                catalogId: "createRelationship.association"
                text: qsTr("Navigable association")
                onTriggered: canvas.createRelationship("association")
            }
            CatalogMenuItem {
                catalogId: "createRelationship.aggregation"
                text: qsTr("Aggregation")
                onTriggered: canvas.createRelationship("aggregation")
            }
            CatalogMenuItem {
                catalogId: "createRelationship.composition"
                text: qsTr("Composition")
                onTriggered: canvas.createRelationship("composition")
            }
            MenuSeparator {}
            CatalogMenuItem {
                catalogId: "createRelationship.containment"
                text: qsTr("Containment (nesting)")
                onTriggered: canvas.createRelationship("containment")
            }
        }
        MenuSeparator {}
        CatalogMenuItem {
            catalogId: "presentation.addIncomingRelatedTypes"
            text: qsTr("Add types that depend on this")
            enabled: canvas.selectedNodeCount === 1
                     && canvas.incomingRelatedTypeCount > 0
            onTriggered: canvas.addRelatedTypes("incoming")
        }
        CatalogMenuItem {
            catalogId: "presentation.addOutgoingRelatedTypes"
            text: qsTr("Add types this depends on")
            enabled: canvas.selectedNodeCount === 1
                     && canvas.outgoingRelatedTypeCount > 0
            onTriggered: canvas.addRelatedTypes("outgoing")
        }
        MenuSeparator {}
        Menu {
            title: qsTr("Attributes")
            enabled: canvas.selectedNodeCount > 0
            MenuItem {
                text: qsTr("Inherit diagram setting")
                checkable: true
                checked: canvas.selectedAttributesVisibility === "inherit"
                onTriggered: canvas.setSelectedCompartmentVisibility(
                                 "attributes", "inherit")
            }
            MenuItem {
                text: qsTr("Show")
                checkable: true
                checked: canvas.selectedAttributesVisibility === "show"
                onTriggered: canvas.setSelectedCompartmentVisibility(
                                 "attributes", "show")
            }
            MenuItem {
                text: qsTr("Hide")
                checkable: true
                checked: canvas.selectedAttributesVisibility === "hide"
                onTriggered: canvas.setSelectedCompartmentVisibility(
                                 "attributes", "hide")
            }
        }
        Menu {
            title: qsTr("Operations")
            enabled: canvas.selectedNodeCount > 0
            MenuItem {
                text: qsTr("Inherit diagram setting")
                checkable: true
                checked: canvas.selectedOperationsVisibility === "inherit"
                onTriggered: canvas.setSelectedCompartmentVisibility(
                                 "operations", "inherit")
            }
            MenuItem {
                text: qsTr("Show")
                checkable: true
                checked: canvas.selectedOperationsVisibility === "show"
                onTriggered: canvas.setSelectedCompartmentVisibility(
                                 "operations", "show")
            }
            MenuItem {
                text: qsTr("Hide")
                checkable: true
                checked: canvas.selectedOperationsVisibility === "hide"
                onTriggered: canvas.setSelectedCompartmentVisibility(
                                 "operations", "hide")
            }
        }
        MenuSeparator {}
        CatalogMenuItem {
            catalogId: "presentation.wrapInNamespace"
            visible: canvas.canWrapSelectionInPackage
            height: visible ? implicitHeight : 0
            text: qsTr("Wrap in parent namespace")
            onTriggered: canvas.wrapSelectionInPackage()
        }
        MenuSeparator {
            visible: canvas.canWrapSelectionInPackage
            height: visible ? implicitHeight : 0
        }
        Menu {
            title: qsTr("Arrange")
            enabled: canvas.selectedNodeCount >= 2
            Menu {
                title: qsTr("Align")
                MenuItem { action: alignLeftAction }
                MenuItem { action: alignHorizontalCenterAction }
                MenuItem { action: alignRightAction }
                MenuSeparator {}
                MenuItem { action: alignTopAction }
                MenuItem { action: alignVerticalCenterAction }
                MenuItem { action: alignBottomAction }
            }
            Menu {
                title: qsTr("Make same size")
                MenuItem { action: matchWidthAction }
                MenuItem { action: matchHeightAction }
                MenuItem { action: matchSizeAction }
            }
            MenuSeparator {}
            MenuItem { action: distributeHorizontallyAction }
            MenuItem { action: distributeVerticallyAction }
        }
        MenuSeparator {}
        MenuItem { action: fitSelectionAction }
        CatalogMenuItem {
            catalogId: "presentation.connectorSnapPoints"
            text: qsTr("Connector snap points…")
            enabled: canvas.selectedNodeCount === 1
            onTriggered: root.openSelectedPortSnapPointsDialog()
        }
        StyleAssignmentMenu {
            assignedStyleId: canvas.selectedStyleId
            onStyleChosen: function(styleId) {
                canvas.assignStyleToSelection(styleId)
            }
            onManageRequested: presentationStyleDialog.openFor(
                                   canvas.selectedStyleId)
        }
        MenuItem {
            text: qsTr("Stereotypes…")
            enabled: canvas.selectedNodeCount === 1
            onTriggered: {
                const menuX = elementMenu.x
                const menuY = elementMenu.y
                const objectId = projectController.selectedId
                Qt.callLater(function() {
                    diagramStereotypeDropdown.openAt(
                                root, menuX, menuY, "element", objectId)
                })
            }
        }
        MenuSeparator {}
        CatalogMenuItem {
            catalogId: "presentation.copyToDiagram"
            text: qsTr("Copy selected to diagram…")
            onTriggered: root.openPresentationTransferDialog(false)
        }
        CatalogMenuItem {
            catalogId: "presentation.moveToDiagram"
            text: qsTr("Move selected to diagram…")
            onTriggered: root.openPresentationTransferDialog(true)
        }
        MenuSeparator {}
        CatalogMenuItem {
            catalogId: "presentation.removeElement"
            text: canvas.selectedNodeCount > 1
                  ? qsTr("Remove presentations from diagram")
                  : qsTr("Remove presentation from diagram")
            onTriggered: canvas.removeSelectedPresentations()
        }
    }

    Menu {
        id: connectorMenu
        title: canvas.selectedConnectorCount > 1
               ? qsTr("Relationships")
               : qsTr("Relationship")
        CatalogMenuItem {
            catalogId: "connector.addBendPoint"
            text: qsTr("Add bend point here")
            enabled: canvas.selectedConnectorCount === 1
            onTriggered: canvas.addBendPointAtContextPosition()
        }
        CatalogMenuItem {
            catalogId: "connector.removeBendPoint"
            text: qsTr("Remove bend point")
            enabled: canvas.selectedConnectorCount === 1
                     && canvas.bendPointSelected
            onTriggered: canvas.removeSelectedBendPoint()
        }
        CatalogMenuItem {
            catalogId: "connector.clearBendPoints"
            text: qsTr("Clear bend points")
            enabled: canvas.selectedConnectorCount === 1
                     && canvas.selectedConnectorHasBendPoints
            onTriggered: canvas.clearSelectedConnectorBendPoints()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Reset this annotation position")
            enabled: canvas.selectedConnectorCount === 1
                     && canvas.contextAnnotationHasManualPosition
            onTriggered: canvas.resetContextAnnotationPosition()
        }
        MenuItem {
            text: qsTr("Reset all annotation positions")
            enabled: canvas.selectedConnectorCount === 1
                     && canvas.selectedConnectorHasManualAnnotationPositions
            onTriggered: canvas.resetSelectedConnectorAnnotationPositions()
        }
        MenuSeparator {}
        Menu {
            title: qsTr("Routing")
            CatalogMenuItem {
                catalogId: "connector.routeStraight"
                text: qsTr("Straight")
                checkable: true
                checked: canvas.selectedConnectorRouting === "straight"
                onTriggered: canvas.setSelectedConnectorRouting("straight")
            }
            CatalogMenuItem {
                catalogId: "connector.routeOrthogonal"
                text: qsTr("Orthogonal")
                checkable: true
                checked: canvas.selectedConnectorRouting === "orthogonal"
                onTriggered: canvas.setSelectedConnectorRouting("orthogonal")
            }
        }
        MenuItem {
            text: qsTr("Stereotypes…")
            enabled: canvas.selectedConnectorCount === 1
            onTriggered: {
                const menuX = connectorMenu.x
                const menuY = connectorMenu.y
                const objectId = projectController.selectedId
                Qt.callLater(function() {
                    diagramStereotypeDropdown.openAt(
                                root, menuX, menuY,
                                "relationship", objectId)
                })
            }
        }
        MenuSeparator {}
        CatalogMenuItem {
            catalogId: "connector.deleteRelationship"
            text: canvas.selectedConnectorCount > 1
                  ? qsTr("Delete relationships")
                  : qsTr("Delete relationship")
            onTriggered: canvas.deleteSelectedConnector()
        }
    }

    Menu {
        id: containerMenu
        title: qsTr("Folder frame")
        MenuItem { action: fitSelectionAction }
        StyleAssignmentMenu {
            assignedStyleId: canvas.selectedStyleId
            onStyleChosen: function(styleId) {
                canvas.assignStyleToSelection(styleId)
            }
            onManageRequested: presentationStyleDialog.openFor(
                                   canvas.selectedStyleId)
        }
        MenuSeparator {}
        CatalogMenuItem {
            catalogId: "presentation.copyToDiagram"
            text: qsTr("Copy selected to diagram…")
            onTriggered: root.openPresentationTransferDialog(false)
        }
        CatalogMenuItem {
            catalogId: "presentation.moveToDiagram"
            text: qsTr("Move selected to diagram…")
            onTriggered: root.openPresentationTransferDialog(true)
        }
        MenuSeparator {}
        CatalogMenuItem {
            catalogId: "presentation.removeFrame"
            text: qsTr("Remove frame from diagram")
            onTriggered: canvas.removeSelectedPresentations()
        }
    }

    CatalogAction {
        id: fitDiagramAction
        catalogId: "arrange.fitDiagram"
        text: qsTr("Fit diagram")
        shortcut: "Ctrl+0"
        enabled: root.visible
        onTriggered: canvas.fitToContent()
    }

    CatalogAction {
        id: fitSelectionAction
        catalogId: "arrange.fitToContent"
        text: qsTr("Fit to content")
        shortcut: "Ctrl+Shift+F"
        enabled: root.visible && !editor.visible
                 && (canvas.selectedNodeCount > 0 || canvas.containerSelected)
        onTriggered: canvas.fitSelectionToContent()
    }

    CatalogAction {
        id: alignLeftAction
        catalogId: "arrange.alignLeft"
        text: qsTr("Left")
        shortcut: "Ctrl+Shift+Left"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignLeft")
    }
    CatalogAction {
        id: alignHorizontalCenterAction
        catalogId: "arrange.alignHorizontalCenters"
        text: qsTr("Horizontal centers")
        shortcut: "Ctrl+Shift+H"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignHorizontalCenter")
    }
    CatalogAction {
        id: alignRightAction
        catalogId: "arrange.alignRight"
        text: qsTr("Right")
        shortcut: "Ctrl+Shift+Right"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignRight")
    }
    CatalogAction {
        id: alignTopAction
        catalogId: "arrange.alignTop"
        text: qsTr("Top")
        shortcut: "Ctrl+Shift+Up"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignTop")
    }
    CatalogAction {
        id: alignVerticalCenterAction
        catalogId: "arrange.alignVerticalCenters"
        text: qsTr("Vertical centers")
        shortcut: "Ctrl+Shift+V"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignVerticalCenter")
    }
    CatalogAction {
        id: alignBottomAction
        catalogId: "arrange.alignBottom"
        text: qsTr("Bottom")
        shortcut: "Ctrl+Shift+Down"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignBottom")
    }
    CatalogAction {
        id: matchWidthAction
        catalogId: "arrange.matchWidth"
        text: qsTr("Width")
        shortcut: "Ctrl+Alt+W"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("matchWidth")
    }
    CatalogAction {
        id: matchHeightAction
        catalogId: "arrange.matchHeight"
        text: qsTr("Height")
        shortcut: "Ctrl+Alt+H"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("matchHeight")
    }
    CatalogAction {
        id: matchSizeAction
        catalogId: "arrange.matchSize"
        text: qsTr("Width and height")
        shortcut: "Ctrl+Alt+E"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("matchSize")
    }
    CatalogAction {
        id: distributeHorizontallyAction
        catalogId: "arrange.distributeHorizontally"
        text: qsTr("Distribute horizontally")
        shortcut: "Ctrl+Alt+Shift+H"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 3
        onTriggered: canvas.arrangeSelection("distributeHorizontally")
    }
    CatalogAction {
        id: distributeVerticallyAction
        catalogId: "arrange.distributeVertically"
        text: qsTr("Distribute vertically")
        shortcut: "Ctrl+Alt+Shift+V"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 3
        onTriggered: canvas.arrangeSelection("distributeVertically")
    }

    Shortcut { sequences: ["Ctrl+Shift+P"]; context: Qt.WindowShortcut; enabled: root.visible && !editor.visible; onActivated: canvas.createElementAtViewportCenter("package") }
    Shortcut { sequences: ["Ctrl+Shift+C"]; context: Qt.WindowShortcut; enabled: root.visible && !editor.visible; onActivated: canvas.createElementAtViewportCenter("class") }
    Shortcut { sequences: ["Ctrl+Shift+S"]; context: Qt.WindowShortcut; enabled: root.visible && !editor.visible; onActivated: canvas.createElementAtViewportCenter("struct") }
    Shortcut { sequences: ["Ctrl+Shift+E"]; context: Qt.WindowShortcut; enabled: root.visible && !editor.visible; onActivated: canvas.createElementAtViewportCenter("enumeration") }
    Shortcut { sequences: ["Ctrl+Alt+D"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.selectedNodeCount === 2; onActivated: canvas.createRelationship("dependency") }
    Shortcut { sequences: ["Ctrl+Alt+I"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.selectedNodeCount === 2; onActivated: canvas.createRelationship("realization") }
    Shortcut { sequences: ["Ctrl+Alt+G"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.selectedNodeCount === 2; onActivated: canvas.createRelationship("generalization") }
    Shortcut { sequences: ["Ctrl+Alt+A"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.selectedNodeCount === 2; onActivated: canvas.createRelationship("association") }
    Shortcut { sequences: ["Ctrl+Alt+Shift+G"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.selectedNodeCount === 2; onActivated: canvas.createRelationship("aggregation") }
    Shortcut { sequences: ["Ctrl+Alt+C"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.selectedNodeCount === 2; onActivated: canvas.createRelationship("composition") }
    Shortcut { sequences: ["Ctrl+Alt+N"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.selectedNodeCount === 2; onActivated: canvas.createRelationship("containment") }
    Shortcut {
        sequences: ["Delete"]
        context: Qt.WindowShortcut
        enabled: root.visible && !editor.visible
                 && (canvas.selectedNodeCount > 0 || canvas.containerSelected
                     || canvas.connectorSelected)
        onActivated: canvas.bendPointSelected
                     ? canvas.removeSelectedBendPoint()
                     : canvas.connectorSelected
                     ? canvas.deleteSelectedConnector()
                     : canvas.removeSelectedPresentations()
    }

    ContextToolboxFrame {
        id: relationshipToolbox
        candidate: canvas.relationshipToolboxCandidate && !editor.visible
                   && root.activeRelationshipToolboxActions.length > 0
        candidateKey: canvas.relationshipToolboxNodeId + ":"
                      + canvas.relationshipToolboxEdge
        anchor: canvas.relationshipToolboxViewAnchor
        placement: canvas.relationshipToolboxEdge
        gestureActive: root.relationshipToolboxGestureActive

        Row {
            spacing: 2

            Repeater {
                model: root.activeRelationshipToolboxActions

                CatalogToolButton {
                    id: relationshipToolboxButton
                    required property var modelData
                    catalogId: modelData.actionId
                    readonly property string gestureKey:
                        applicationSettings.relationshipGestureKeys[
                            modelData.type] || ""

                    width: 32
                    height: 32
                    text: gestureKey
                    display: icon.source.toString().length > 0
                             ? AbstractButton.IconOnly
                             : AbstractButton.TextOnly
                    Accessible.name: modelData.label

                    ToolTip.visible: relationshipDragArea.containsMouse
                                     && !relationshipDragArea.pressed
                    ToolTip.text: qsTr("Drag to create %1").arg(
                                      modelData.label)

                    MouseArea {
                        id: relationshipDragArea
                        property bool relationshipStarted: false
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        preventStealing: true
                        cursorShape: Qt.CrossCursor

                        onPressed: function(mouse) {
                            relationshipStarted =
                                canvas.beginToolboxRelationship(
                                    modelData.type,
                                    root.relationshipToolboxNodeId,
                                    root.relationshipToolboxSceneX,
                                    root.relationshipToolboxSceneY)
                            if (!relationshipStarted)
                                return
                            root.relationshipToolboxGestureActive = true
                            const canvasPoint = mapToItem(
                                canvas, mouse.x, mouse.y)
                            canvas.updateToolboxRelationship(
                                canvasPoint.x, canvasPoint.y,
                                mouse.modifiers & Qt.AltModifier)
                        }

                        onPositionChanged: function(mouse) {
                            if (!relationshipStarted)
                                return
                            const canvasPoint = mapToItem(
                                canvas, mouse.x, mouse.y)
                            canvas.updateToolboxRelationship(
                                canvasPoint.x, canvasPoint.y,
                                mouse.modifiers & Qt.AltModifier)
                        }

                        onReleased: function(mouse) {
                            if (relationshipStarted) {
                                const canvasPoint = mapToItem(
                                    canvas, mouse.x, mouse.y)
                                canvas.finishToolboxRelationship(
                                    canvasPoint.x, canvasPoint.y,
                                    mouse.modifiers & Qt.AltModifier)
                            }
                            relationshipStarted = false
                            root.relationshipToolboxGestureActive = false
                            root.dismissRelationshipToolbox()
                            canvas.forceActiveFocus()
                        }

                        onCanceled: {
                            if (relationshipStarted)
                                canvas.cancelConnectorInteraction()
                            relationshipStarted = false
                            root.relationshipToolboxGestureActive = false
                            root.dismissRelationshipToolbox()
                            canvas.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }

    ContextToolboxFrame {
        id: arrangementToolbox
        candidate: canvas.arrangementToolboxCandidate
                   && canvas.selectedNodeCount >= 2
                   && !editor.visible
                   && root.activeArrangementToolboxActions.length > 0
        candidateKey: canvas.arrangementToolboxNodeId
        anchor: canvas.arrangementToolboxViewAnchor
        placement: "top"
        trackAnchorWhileShown: true

        Grid {
            columns: 7
            spacing: 2

            Repeater {
                model: root.activeArrangementToolboxActions

                CatalogToolButton {
                    required property var modelData
                    catalogId: modelData.actionId
                    width: 32
                    height: 32
                    text: modelData.fallback
                    enabled: modelData.kind === "style"
                             || modelData.action.enabled
                    display: icon.source.toString().length > 0
                             ? AbstractButton.IconOnly
                             : AbstractButton.TextOnly
                    Accessible.name: modelData.kind === "style"
                                     ? modelData.label
                                     : modelData.action.text
                    ToolTip.visible: hovered
                    ToolTip.text: Accessible.name
                    onClicked: {
                        if (modelData.kind === "style") {
                            const menuPoint = mapToItem(root, 0, height)
                            presentationStyleQuickMenu.x = menuPoint.x
                            presentationStyleQuickMenu.y = menuPoint.y
                            arrangementToolbox.dismiss()
                            presentationStyleQuickMenu.open()
                        } else {
                            modelData.action.trigger()
                        }
                        canvas.forceActiveFocus()
                    }
                }
            }
        }
    }

    ContextToolboxFrame {
        id: connectorToolbox
        objectName: "connectorToolbox"
        candidate: canvas.connectorToolboxCandidate
                   && canvas.connectorSelected
                   && !editor.visible
                   && root.activeConnectorToolboxActions.length > 0
        candidateKey: canvas.connectorToolboxConnectorId
        anchor: canvas.connectorToolboxViewAnchor
        placement: "top"
        // Follow the pointer during the show delay, then stay put while the
        // user crosses the hover bridge to the controls.
        trackAnchorWhileShown: false

        Row {
            spacing: 2

            Repeater {
                model: root.activeConnectorToolboxActions

                CatalogToolButton {
                    required property var modelData
                    catalogId: modelData.actionId
                    width: 32
                    height: 32
                    text: modelData.fallback
                    checkable: modelData.kind === "routing"
                    checked: modelData.kind === "routing"
                             && canvas.selectedConnectorRouting
                                === modelData.value
                    enabled: modelData.kind === "routing"
                             || (canvas.selectedConnectorCount === 1
                                 && (modelData.kind !== "reset"
                                     || canvas.selectedConnectorHasManualAnnotationPositions))
                    display: icon.source.toString().length > 0
                             ? AbstractButton.IconOnly
                             : AbstractButton.TextOnly
                    Accessible.name: modelData.label
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.label
                    onClicked: {
                        if (modelData.kind === "routing") {
                            canvas.setSelectedConnectorRouting(modelData.value)
                            canvas.forceActiveFocus()
                        } else if (modelData.kind === "annotation") {
                            canvas.editSelectedConnectorAnnotation(modelData.field)
                            connectorToolbox.dismiss()
                        } else {
                            canvas.resetSelectedConnectorAnnotationPositions()
                            canvas.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }

    ContextToolboxFrame {
        id: presentationToolbox
        objectName: "presentationToolbox"
        candidate: canvas.presentationToolboxCandidate
                   && !editor.visible
                   && root.presentationToolboxHasApplicableAction()
        candidateKey: canvas.presentationToolboxKind + ":"
                      + canvas.presentationToolboxPresentationId
        anchor: canvas.presentationToolboxViewAnchor
        placement: "top"
        trackAnchorWhileShown: true

        Row {
            spacing: 2

            Repeater {
                model: root.activePresentationToolboxActions

                Item {
                    required property var modelData
                    readonly property bool nodeOnly:
                        modelData.kind === "incoming"
                        || modelData.kind === "outgoing"
                        || modelData.kind === "wrap"
                        || modelData.kind === "snapPoints"
                        || modelData.kind === "attributesVisibility"
                        || modelData.kind === "operationsVisibility"
                    visible: !nodeOnly
                             || (canvas.presentationToolboxKind === "node"
                                 && (modelData.kind !== "wrap"
                                     || canvas.canWrapSelectionInPackage))
                    width: visible ? 38 : 0
                    height: 32

                    CatalogToolButton {
                        anchors.fill: parent
                        visible: parent.modelData.kind
                                 !== "attributesVisibility"
                                 && parent.modelData.kind
                                    !== "operationsVisibility"
                        catalogId: parent.modelData.actionId
                        text: parent.modelData.fallback
                        enabled: parent.modelData.kind === "incoming"
                                 ? canvas.incomingRelatedTypeCount > 0
                                 : parent.modelData.kind === "outgoing"
                                   ? canvas.outgoingRelatedTypeCount > 0 : true
                        display: icon.source.toString().length > 0
                                 ? AbstractButton.IconOnly
                                 : AbstractButton.TextOnly
                        Accessible.name: parent.modelData.label
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        onClicked: {
                            const kind = parent.modelData.kind
                            if (kind === "editName") {
                                canvas.editSelectedPresentationName()
                                presentationToolbox.dismiss()
                            } else if (kind === "fit") {
                                fitSelectionAction.trigger()
                                canvas.forceActiveFocus()
                            } else if (kind === "style") {
                                const menuPoint = mapToItem(root, 0, height)
                                presentationStyleQuickMenu.x = menuPoint.x
                                presentationStyleQuickMenu.y = menuPoint.y
                                presentationToolbox.dismiss()
                                presentationStyleQuickMenu.open()
                            } else if (kind === "incoming") {
                                canvas.addRelatedTypes("incoming")
                                canvas.forceActiveFocus()
                            } else if (kind === "outgoing") {
                                canvas.addRelatedTypes("outgoing")
                                canvas.forceActiveFocus()
                            } else if (kind === "snapPoints") {
                                presentationToolbox.dismiss()
                                root.openSelectedPortSnapPointsDialog()
                            } else {
                                canvas.wrapSelectionInPackage()
                                canvas.forceActiveFocus()
                            }
                        }
                    }

                    CompartmentVisibilityToolButton {
                        anchors.fill: parent
                        visible: parent.modelData.kind === "attributesVisibility"
                                 || parent.modelData.kind
                                    === "operationsVisibility"
                        compartment: parent.modelData.kind
                                     === "attributesVisibility"
                                     ? "attributes" : "operations"
                        visibilityState: compartment === "attributes"
                                         ? canvas.selectedAttributesVisibility
                                         : canvas.selectedOperationsVisibility
                        onVisibilityChangeRequested: function(state) {
                            canvas.setSelectedCompartmentVisibility(
                                        compartment, state)
                            canvas.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        visible: canvas.connectorInteractionPrompt.length > 0
        width: interactionMessage.implicitWidth + cancelInteraction.implicitWidth + 28
        height: 38
        radius: 5
        color: uiTheme.warningBackground
        border.color: uiTheme.warningBorder
        z: 10

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 4
            Label { id: interactionMessage; text: canvas.connectorInteractionPrompt }
            CatalogToolButton {
                id: cancelInteraction
                catalogId: "createRelationship.cancel"
                text: qsTr("Cancel")
                onClicked: canvas.cancelConnectorInteraction()
            }
        }
    }

    TextField {
        id: editor
        property string objectId
        property string field
        property int fieldIndex: -1
        property string originalText
        visible: false
        z: 20
        selectByMouse: true
        padding: 0
        leftPadding: 2
        rightPadding: 2
        verticalAlignment: TextInput.AlignVCenter
        background: Rectangle {
            color: uiTheme.editorBackground
            border.color: uiTheme.accent
            border.width: 1
        }

        function commit() {
            if (!visible)
                return
            const next = text
            visible = false
            if (next !== originalText)
                projectController.editText(objectId, field, fieldIndex, next)
            canvas.forceActiveFocus()
        }

        function cancel() {
            if (!visible)
                return
            text = originalText
            visible = false
            canvas.forceActiveFocus()
        }

        onAccepted: commit()
        onActiveFocusChanged: {
            if (visible && !activeFocus)
                commit()
        }
        Keys.priority: Keys.BeforeItem
        Keys.onEscapePressed: function(event) {
            cancel()
            event.accepted = true
        }
    }
}
