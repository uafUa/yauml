import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Uuml.Native

Item {
    id: root
    required property string diagramId

    DiagramCanvas {
        id: canvas
        anchors.fill: parent
        project: projectController
        diagramId: root.diagramId
        defaultDistributionGap: applicationSettings.defaultDistributionGap
        snapToGridEnabled: applicationSettings.snapToGridEnabled
        alignmentGuidesEnabled: applicationSettings.alignmentGuidesEnabled
        gridSpacing: applicationSettings.gridSpacing
        defaultConnectorRouting: applicationSettings.defaultConnectorRouting
        relationshipGestureKeys: applicationSettings.relationshipGestureKeys

        onContextMenuRequested: function(target, menuX, menuY) {
            const menu = target === "element" ? elementMenu
                       : target === "connector" ? connectorMenu : canvasMenu
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
            MenuItem { text: qsTr("Package"); onTriggered: canvas.createElementAtContextPosition("package") }
            MenuItem { text: qsTr("Class"); onTriggered: canvas.createElementAtContextPosition("class") }
            MenuItem { text: qsTr("Struct"); onTriggered: canvas.createElementAtContextPosition("struct") }
            MenuItem { text: qsTr("Enumeration"); onTriggered: canvas.createElementAtContextPosition("enumeration") }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Fit diagram"); onTriggered: canvas.fitToContent() }
    }

    Menu {
        id: elementMenu
        title: canvas.selectedNodeCount > 1
               ? qsTr("Selected elements") : qsTr("Element")
        Menu {
            title: qsTr("Create relationship")
            enabled: canvas.selectedNodeCount === 2
            MenuItem { text: qsTr("Dependency"); onTriggered: canvas.createRelationship("dependency") }
            MenuItem { text: qsTr("Realization (implementation)"); onTriggered: canvas.createRelationship("realization") }
            MenuItem { text: qsTr("Generalization (inheritance)"); onTriggered: canvas.createRelationship("generalization") }
            MenuSeparator {}
            MenuItem { text: qsTr("Navigable association"); onTriggered: canvas.createRelationship("association") }
            MenuItem { text: qsTr("Aggregation"); onTriggered: canvas.createRelationship("aggregation") }
            MenuItem { text: qsTr("Composition"); onTriggered: canvas.createRelationship("composition") }
        }
        MenuSeparator {}
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
        MenuItem {
            text: canvas.selectedNodeCount > 1
                  ? qsTr("Remove presentations from diagram")
                  : qsTr("Remove presentation from diagram")
            onTriggered: canvas.removeSelectedPresentations()
        }
    }

    Menu {
        id: connectorMenu
        title: qsTr("Relationship")
        MenuItem { text: qsTr("Add bend point here"); onTriggered: canvas.addBendPointAtContextPosition() }
        MenuItem {
            text: qsTr("Remove bend point")
            enabled: canvas.bendPointSelected
            onTriggered: canvas.removeSelectedBendPoint()
        }
        MenuItem {
            text: qsTr("Clear bend points")
            enabled: canvas.selectedConnectorHasBendPoints
            onTriggered: canvas.clearSelectedConnectorBendPoints()
        }
        MenuSeparator {}
        Menu {
            title: qsTr("Routing")
            MenuItem {
                text: qsTr("Straight")
                checkable: true
                checked: canvas.selectedConnectorRouting === "straight"
                onTriggered: canvas.setSelectedConnectorRouting("straight")
            }
            MenuItem {
                text: qsTr("Orthogonal")
                checkable: true
                checked: canvas.selectedConnectorRouting === "orthogonal"
                onTriggered: canvas.setSelectedConnectorRouting("orthogonal")
            }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Delete relationship"); onTriggered: canvas.deleteSelectedConnector() }
    }

    Action {
        id: alignLeftAction
        text: qsTr("Left")
        shortcut: "Ctrl+Shift+Left"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignLeft")
    }
    Action {
        id: alignHorizontalCenterAction
        text: qsTr("Horizontal centers")
        shortcut: "Ctrl+Shift+H"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignHorizontalCenter")
    }
    Action {
        id: alignRightAction
        text: qsTr("Right")
        shortcut: "Ctrl+Shift+Right"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignRight")
    }
    Action {
        id: alignTopAction
        text: qsTr("Top")
        shortcut: "Ctrl+Shift+Up"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignTop")
    }
    Action {
        id: alignVerticalCenterAction
        text: qsTr("Vertical centers")
        shortcut: "Ctrl+Shift+V"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignVerticalCenter")
    }
    Action {
        id: alignBottomAction
        text: qsTr("Bottom")
        shortcut: "Ctrl+Shift+Down"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("alignBottom")
    }
    Action {
        id: matchWidthAction
        text: qsTr("Width")
        shortcut: "Ctrl+Alt+W"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("matchWidth")
    }
    Action {
        id: matchHeightAction
        text: qsTr("Height")
        shortcut: "Ctrl+Alt+H"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("matchHeight")
    }
    Action {
        id: matchSizeAction
        text: qsTr("Width and height")
        shortcut: "Ctrl+Alt+E"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 2
        onTriggered: canvas.arrangeSelection("matchSize")
    }
    Action {
        id: distributeHorizontallyAction
        text: qsTr("Distribute horizontally")
        shortcut: "Ctrl+Alt+Shift+H"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 3
        onTriggered: canvas.arrangeSelection("distributeHorizontally")
    }
    Action {
        id: distributeVerticallyAction
        text: qsTr("Distribute vertically")
        shortcut: "Ctrl+Alt+Shift+V"
        enabled: root.visible && !editor.visible && canvas.selectedNodeCount >= 3
        onTriggered: canvas.arrangeSelection("distributeVertically")
    }

    Shortcut { sequences: ["Ctrl+0"]; context: Qt.WindowShortcut; enabled: root.visible; onActivated: canvas.fitToContent() }
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
    Shortcut {
        sequences: ["Delete"]
        context: Qt.WindowShortcut
        enabled: root.visible && !editor.visible
                 && (canvas.selectedNodeCount > 0 || canvas.connectorSelected)
        onActivated: canvas.bendPointSelected
                     ? canvas.removeSelectedBendPoint()
                     : canvas.connectorSelected
                     ? canvas.deleteSelectedConnector()
                     : canvas.removeSelectedPresentations()
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
            ToolButton {
                id: cancelInteraction
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
