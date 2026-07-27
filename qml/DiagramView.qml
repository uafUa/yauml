import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Uuml.Native

Item {
    id: root
    required property string diagramId
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
        }

        function onCanvasSelectionChanged() {
            if (!root.relationshipToolboxGestureActive
                    && canvas.connectorInteractionPrompt.length === 0)
                root.dismissRelationshipToolbox()
            if (canvas.selectedNodeCount < 2)
                arrangementToolbox.dismiss()
        }

        function onContextToolboxesDismissRequested() {
            root.dismissRelationshipToolbox()
            arrangementToolbox.dismiss()
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

    ProjectStyleDialog {
        id: presentationStyleDialog
        onStyleChosen: function(styleId) {
            canvas.assignStyleToSelection(styleId)
        }
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

    Menu {
        id: canvasMenu
        title: qsTr("Diagram")
        Menu {
            title: qsTr("New element")
            CatalogMenuItem {
                catalogId: "createElement.package"
                text: qsTr("Package")
                onTriggered: canvas.createElementAtContextPosition("package")
            }
            CatalogMenuItem {
                catalogId: "createElement.class"
                text: qsTr("Class")
                onTriggered: canvas.createElementAtContextPosition("class")
            }
            CatalogMenuItem {
                catalogId: "createElement.struct"
                text: qsTr("Struct")
                onTriggered: canvas.createElementAtContextPosition("struct")
            }
            CatalogMenuItem {
                catalogId: "createElement.enumeration"
                text: qsTr("Enumeration")
                onTriggered: canvas.createElementAtContextPosition("enumeration")
            }
        }
        MenuSeparator {}
        MenuItem { action: fitDiagramAction }
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
            onTriggered: {
                horizontalSnapPoints.value =
                    canvas.selectedHorizontalPortSnapPoints
                verticalSnapPoints.value =
                    canvas.selectedVerticalPortSnapPoints
                portSnapPointsDialog.open()
            }
        }
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
            catalogId: "presentation.removeElement"
            text: canvas.selectedNodeCount > 1
                  ? qsTr("Remove presentations from diagram")
                  : qsTr("Remove presentation from diagram")
            onTriggered: canvas.removeSelectedPresentations()
        }
    }

    Menu {
        id: connectorMenu
        title: qsTr("Relationship")
        CatalogMenuItem {
            catalogId: "connector.addBendPoint"
            text: qsTr("Add bend point here")
            onTriggered: canvas.addBendPointAtContextPosition()
        }
        CatalogMenuItem {
            catalogId: "connector.removeBendPoint"
            text: qsTr("Remove bend point")
            enabled: canvas.bendPointSelected
            onTriggered: canvas.removeSelectedBendPoint()
        }
        CatalogMenuItem {
            catalogId: "connector.clearBendPoints"
            text: qsTr("Clear bend points")
            enabled: canvas.selectedConnectorHasBendPoints
            onTriggered: canvas.clearSelectedConnectorBendPoints()
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
        MenuSeparator {}
        CatalogMenuItem {
            catalogId: "connector.deleteRelationship"
            text: qsTr("Delete relationship")
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
        candidateKey: canvas.relationshipToolboxNodeId + ":"
                      + canvas.relationshipToolboxEdge
        anchor: canvas.relationshipToolboxViewAnchor
        placement: canvas.relationshipToolboxEdge
        gestureActive: root.relationshipToolboxGestureActive

        Row {
            spacing: 2

            Repeater {
                model: root.relationshipToolboxActions

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
        candidateKey: canvas.arrangementToolboxNodeId
        anchor: canvas.arrangementToolboxViewAnchor
        placement: "top"
        trackAnchorWhileShown: true

        Grid {
            columns: 6
            spacing: 2

            Repeater {
                model: [
                    { action: alignLeftAction, fallback: qsTr("L") },
                    { action: alignHorizontalCenterAction,
                      fallback: qsTr("HC") },
                    { action: alignRightAction, fallback: qsTr("R") },
                    { action: alignTopAction, fallback: qsTr("T") },
                    { action: alignVerticalCenterAction,
                      fallback: qsTr("VC") },
                    { action: alignBottomAction, fallback: qsTr("B") },
                    { action: matchWidthAction, fallback: qsTr("W") },
                    { action: matchHeightAction, fallback: qsTr("H") },
                    { action: matchSizeAction, fallback: qsTr("WH") },
                    { action: distributeHorizontallyAction,
                      fallback: qsTr("DH") },
                    { action: distributeVerticallyAction,
                      fallback: qsTr("DV") }
                ]

                CatalogToolButton {
                    required property var modelData
                    catalogId: modelData.action.catalogId
                    width: 32
                    height: 32
                    text: modelData.fallback
                    enabled: modelData.action.enabled
                    display: icon.source.toString().length > 0
                             ? AbstractButton.IconOnly
                             : AbstractButton.TextOnly
                    Accessible.name: modelData.action.text
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.action.text
                    onClicked: {
                        modelData.action.trigger()
                        canvas.forceActiveFocus()
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
