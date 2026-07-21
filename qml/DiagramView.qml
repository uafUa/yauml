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

    Label {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        padding: 6
        text: Math.round(canvas.zoom * 100) + "%"
        color: "#425466"
        background: Rectangle {
            radius: 4
            color: "#eaf0f5"
            border.color: "#c6d0da"
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
            MenuItem { text: qsTr("Generalization"); onTriggered: canvas.createRelationship("generalization") }
            MenuItem { text: qsTr("Association"); onTriggered: canvas.createRelationship("association") }
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
        MenuItem { text: qsTr("Reconnect source…"); onTriggered: canvas.reconnectSource() }
        MenuItem { text: qsTr("Reconnect target…"); onTriggered: canvas.reconnectTarget() }
        MenuSeparator {}
        MenuItem { text: qsTr("Delete relationship"); onTriggered: canvas.deleteSelectedConnector() }
    }

    Shortcut { sequences: ["Ctrl+0"]; context: Qt.WindowShortcut; enabled: root.visible; onActivated: canvas.fitToContent() }
    Shortcut { sequences: ["Ctrl+Shift+P"]; context: Qt.WindowShortcut; enabled: root.visible && !editor.visible; onActivated: canvas.createElementAtViewportCenter("package") }
    Shortcut { sequences: ["Ctrl+Shift+C"]; context: Qt.WindowShortcut; enabled: root.visible && !editor.visible; onActivated: canvas.createElementAtViewportCenter("class") }
    Shortcut { sequences: ["Ctrl+Shift+S"]; context: Qt.WindowShortcut; enabled: root.visible && !editor.visible; onActivated: canvas.createElementAtViewportCenter("struct") }
    Shortcut { sequences: ["Ctrl+Shift+E"]; context: Qt.WindowShortcut; enabled: root.visible && !editor.visible; onActivated: canvas.createElementAtViewportCenter("enumeration") }
    Shortcut { sequences: ["Ctrl+Alt+D"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.selectedNodeCount === 2; onActivated: canvas.createRelationship("dependency") }
    Shortcut { sequences: ["Ctrl+Alt+G"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.selectedNodeCount === 2; onActivated: canvas.createRelationship("generalization") }
    Shortcut { sequences: ["Ctrl+Alt+A"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.selectedNodeCount === 2; onActivated: canvas.createRelationship("association") }
    Shortcut { sequences: ["Ctrl+Alt+S"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.connectorSelected; onActivated: canvas.reconnectSource() }
    Shortcut { sequences: ["Ctrl+Alt+T"]; context: Qt.WindowShortcut; enabled: root.visible && canvas.connectorSelected; onActivated: canvas.reconnectTarget() }
    Shortcut {
        sequences: ["Delete"]
        context: Qt.WindowShortcut
        enabled: root.visible && !editor.visible
                 && (canvas.selectedNodeCount > 0 || canvas.connectorSelected)
        onActivated: canvas.connectorSelected
                     ? canvas.deleteSelectedConnector()
                     : canvas.removeSelectedPresentations()
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        visible: canvas.reconnectPrompt.length > 0
        width: reconnectMessage.implicitWidth + cancelReconnect.implicitWidth + 28
        height: 38
        radius: 5
        color: "#fff4ce"
        border.color: "#c89b25"
        z: 10

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 4
            Label { id: reconnectMessage; text: canvas.reconnectPrompt }
            ToolButton {
                id: cancelReconnect
                text: qsTr("Cancel")
                onClicked: canvas.cancelReconnect()
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
            color: "#ffffffe8"
            border.color: "#1769d2"
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
