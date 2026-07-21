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

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        width: controls.implicitWidth + 12
        height: controls.implicitHeight + 8
        radius: 5
        color: "#eaf0f5"
        border.color: "#c6d0da"
        opacity: 0.96

        RowLayout {
            id: controls
            anchors.centerIn: parent
            spacing: 2

            ToolButton {
                text: qsTr("Fit")
                onClicked: canvas.fitToContent()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Fit diagram to the available area")
            }
            ComboBox {
                id: relationshipType
                implicitWidth: 142
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: qsTr("Dependency"), value: "dependency" },
                    { text: qsTr("Generalization"), value: "generalization" },
                    { text: qsTr("Association"), value: "association" }
                ]
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Relationship type")
            }
            ToolButton {
                text: qsTr("Connect")
                enabled: canvas.selectedNodeCount === 2
                onClicked: canvas.createRelationship(relationshipType.currentValue)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Create from the first selected node to the second")
            }
            ToolButton {
                text: qsTr("Source…")
                enabled: canvas.connectorSelected
                checkable: true
                checked: canvas.reconnectPrompt.length > 0
                         && canvas.reconnectPrompt.indexOf("source") >= 0
                onClicked: canvas.reconnectSource()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Then click the new source node")
            }
            ToolButton {
                text: qsTr("Target…")
                enabled: canvas.connectorSelected
                checkable: true
                checked: canvas.reconnectPrompt.length > 0
                         && canvas.reconnectPrompt.indexOf("target") >= 0
                onClicked: canvas.reconnectTarget()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Then click the new target node")
            }
            ToolButton {
                text: qsTr("Remove")
                enabled: canvas.selectedNodeCount > 0
                onClicked: canvas.removeSelectedPresentations()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Remove selected presentations from this diagram; keep model elements")
            }
            Label {
                text: Math.round(canvas.zoom * 100) + "%"
                leftPadding: 6
                rightPadding: 6
                color: "#425466"
            }
        }
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
