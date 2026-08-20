import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Edits presentation defaults owned by one diagram. Applying the dialog calls
// one command so all changed fields undo and redo together.
CatalogDialog {
    id: root

    property string diagramId: ""
    property string diagramType: "class"
    property bool showAttributes: true
    property bool showOperations: true
    property bool showTypeIcons: false
    property string operationSignatureMode: "full"
    property string targetOperationDisplayMode: "hidden"
    property bool showMessageStereotypes: true

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(480, parent.width - 40)
    modal: true
    focus: true
    title: qsTr("Diagram display options")
    standardButtons: Dialog.Ok | Dialog.Cancel

    function comboIndex(values, value) {
        const index = values.indexOf(value)
        return index >= 0 ? index : 0
    }

    function openForDiagram(requestedDiagramId) {
        const values = projectController.diagramDisplayOptions(
                         requestedDiagramId,
                         applicationSettings.diagramTypeIconsVisible)
        if (!values || !values.diagramType)
            return
        diagramId = requestedDiagramId
        diagramType = values.diagramType
        showAttributes = values.showAttributes
        showOperations = values.showOperations
        showTypeIcons = values.showTypeIcons
        operationSignatureMode = values.operationSignatureMode
        targetOperationDisplayMode = values.targetOperationDisplayMode
        showMessageStereotypes = values.showMessageStereotypes
        open()
    }

    onAccepted: projectController.setDiagramDisplayOptions(diagramId, {
        showAttributes: showAttributes,
        showOperations: showOperations,
        showTypeIcons: showTypeIcons,
        operationSignatureMode: operationSignatureMode,
        targetOperationDisplayMode: targetOperationDisplayMode,
        showMessageStereotypes: showMessageStereotypes
    })

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: uiTheme.mutedText
            text: qsTr("These presentation choices are stored with this diagram and do not change the model.")
        }

        GroupBox {
            Layout.fillWidth: true
            visible: root.diagramType === "class"
            title: qsTr("Class and type presentations")

            ColumnLayout {
                anchors.fill: parent
                CheckBox {
                    text: qsTr("Show attributes")
                    checked: root.showAttributes
                    onToggled: root.showAttributes = checked
                }
                CheckBox {
                    text: qsTr("Show operations")
                    checked: root.showOperations
                    onToggled: root.showOperations = checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: root.showOperations
                    Label { text: qsTr("Operation signatures") }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: operationSignatureModeCombo
                        Layout.preferredWidth: 210
                        readonly property var values: [
                            "full", "name-and-return-type", "name-only"
                        ]
                        model: [qsTr("Full signature"),
                                qsTr("Name + return type"),
                                qsTr("Name only")]
                        currentIndex: root.comboIndex(values,
                                                      root.operationSignatureMode)
                        onActivated: root.operationSignatureMode =
                                     values[currentIndex]
                    }
                }
                CheckBox {
                    text: qsTr("Show type icons in presentation titles")
                    checked: root.showTypeIcons
                    onToggled: root.showTypeIcons = checked
                }
            }
        }

        GroupBox {
            Layout.fillWidth: true
            visible: root.diagramType === "sequence"
            title: qsTr("Messages")

            ColumnLayout {
                anchors.fill: parent
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Target operation") }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: targetOperationModeCombo
                        Layout.preferredWidth: 210
                        readonly property var values: [
                            "hidden", "name-only", "full-signature"
                        ]
                        model: [qsTr("Do not show"),
                                qsTr("Show only name"),
                                qsTr("Show full signature")]
                        currentIndex: root.comboIndex(
                                          values,
                                          root.targetOperationDisplayMode)
                        onActivated: root.targetOperationDisplayMode =
                                     values[currentIndex]
                    }
                }
                CheckBox {
                    text: qsTr("Show message stereotypes")
                    checked: root.showMessageStereotypes
                    onToggled: root.showMessageStereotypes = checked
                }
            }
        }
    }
}
