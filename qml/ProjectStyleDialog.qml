import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Dialog {
    id: root

    signal styleChosen(string styleId)

    property string assignedStyleId: ""
    property string editingStyleId: ""
    property string draftName: ""
    property string draftFill: ""
    property string draftHeaderFill: ""
    property string draftBorder: ""
    property string draftPrimaryText: ""
    property string draftSecondaryText: ""
    property string draftDivider: ""
    property string colorPickerRole: ""

    readonly property bool draftValid:
        draftName.trim().length > 0
        && uiTheme.normalizeColor(draftFill).length > 0
        && uiTheme.normalizeColor(draftHeaderFill).length > 0
        && uiTheme.normalizeColor(draftBorder).length > 0
        && uiTheme.normalizeColor(draftPrimaryText).length > 0
        && uiTheme.normalizeColor(draftSecondaryText).length > 0
        && uiTheme.normalizeColor(draftDivider).length > 0

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(790, parent.width - 40)
    height: Math.min(570, parent.height - 40)
    modal: true
    focus: true
    title: qsTr("Project diagram styles")
    standardButtons: Dialog.Close

    function defaultColor(role) {
        if (role === "fill")
            return uiTheme.colorText(uiTheme.classFill)
        if (role === "headerFill")
            return uiTheme.colorText(uiTheme.panelHeader)
        if (role === "border")
            return uiTheme.colorText(uiTheme.nodeBorder)
        if (role === "primaryText")
            return uiTheme.colorText(uiTheme.nodeTitleText)
        if (role === "secondaryText")
            return uiTheme.colorText(uiTheme.bodyText)
        return uiTheme.colorText(uiTheme.compartmentDivider)
    }

    function draftColor(role) {
        if (role === "fill")
            return draftFill
        if (role === "headerFill")
            return draftHeaderFill
        if (role === "border")
            return draftBorder
        if (role === "primaryText")
            return draftPrimaryText
        if (role === "secondaryText")
            return draftSecondaryText
        return draftDivider
    }

    function setDraftColor(role, value) {
        if (role === "fill")
            draftFill = value
        else if (role === "headerFill")
            draftHeaderFill = value
        else if (role === "border")
            draftBorder = value
        else if (role === "primaryText")
            draftPrimaryText = value
        else if (role === "secondaryText")
            draftSecondaryText = value
        else
            draftDivider = value
    }

    function beginNewStyle() {
        editingStyleId = ""
        draftName = qsTr("New style")
        draftFill = defaultColor("fill")
        draftHeaderFill = defaultColor("headerFill")
        draftBorder = defaultColor("border")
        draftPrimaryText = defaultColor("primaryText")
        draftSecondaryText = defaultColor("secondaryText")
        draftDivider = defaultColor("divider")
        styleNameEditor.forceActiveFocus()
        styleNameEditor.selectAll()
    }

    function editStyle(styleId) {
        const style = projectController.diagramStyle(styleId)
        if (!style || !style.id) {
            beginNewStyle()
            return
        }
        editingStyleId = style.id
        draftName = style.name
        draftFill = style.fill
        draftHeaderFill = style.headerFill
        draftBorder = style.border
        draftPrimaryText = style.primaryText
        draftSecondaryText = style.secondaryText
        draftDivider = style.divider
    }

    function openFor(styleId) {
        assignedStyleId = styleId
        if (styleId.length > 0) {
            editStyle(styleId)
        } else if (projectController.diagramStyles.length > 0) {
            editStyle(projectController.diagramStyles[0].id)
        } else {
            beginNewStyle()
        }
        open()
    }

    function saveDraft(assignAfterSave) {
        const savedId = projectController.saveDiagramStyle(
                            editingStyleId, draftName,
                            {
                                "fill": draftFill,
                                "headerFill": draftHeaderFill,
                                "border": draftBorder,
                                "primaryText": draftPrimaryText,
                                "secondaryText": draftSecondaryText,
                                "divider": draftDivider
                            })
        if (savedId.length === 0)
            return
        editingStyleId = savedId
        assignedStyleId = savedId
        if (assignAfterSave) {
            styleChosen(savedId)
            close()
        } else {
            editStyle(savedId)
        }
    }

    contentItem: RowLayout {
        spacing: 12

        ColumnLayout {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: qsTr("Styles in this project")
                font.bold: true
            }
            ListView {
                id: stylesList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: projectController.diagramStyles
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.name
                    highlighted: root.editingStyleId === modelData.id
                    onClicked: root.editStyle(modelData.id)
                }
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("New style")
                onClicked: root.beginNewStyle()
            }
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: uiTheme.controlBorder
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: root.editingStyleId.length > 0
                      ? qsTr("Edit style") : qsTr("Create style")
                font.bold: true
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Label { text: qsTr("Name") }
                TextField {
                    id: styleNameEditor
                    Layout.fillWidth: true
                    text: root.draftName
                    selectByMouse: true
                    onTextEdited: root.draftName = text
                }

                Repeater {
                    model: [
                        { "key": "fill", "label": qsTr("Body fill") },
                        { "key": "headerFill", "label": qsTr("Header fill") },
                        { "key": "border", "label": qsTr("Border") },
                        { "key": "primaryText", "label": qsTr("Primary text") },
                        { "key": "secondaryText", "label": qsTr("Secondary text") },
                        { "key": "divider", "label": qsTr("Divider") }
                    ]

                    delegate: RowLayout {
                        required property var modelData
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: 12

                        Label {
                            Layout.preferredWidth: 120
                            text: parent.modelData.label
                        }
                        Button {
                            Layout.preferredWidth: 54
                            Layout.preferredHeight: 28
                            Accessible.name: qsTr("Choose %1 color").arg(
                                                 parent.modelData.label)
                            onClicked: {
                                root.colorPickerRole = parent.modelData.key
                                styleColorPicker.selectedColor =
                                        root.draftColor(parent.modelData.key)
                                styleColorPicker.open()
                            }
                            background: Rectangle {
                                color: root.draftColor(parent.parent.modelData.key)
                                border.color: uiTheme.controlBorder
                                radius: 3
                            }
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: root.draftColor(parent.modelData.key)
                            selectByMouse: true
                            validator: RegularExpressionValidator {
                                regularExpression: /^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$/
                            }
                            onEditingFinished: {
                                const normalized = uiTheme.normalizeColor(text)
                                if (acceptableInput && normalized.length > 0)
                                    root.setDraftColor(parent.modelData.key,
                                                       normalized)
                                else
                                    text = root.draftColor(parent.modelData.key)
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: !root.draftValid
                color: uiTheme.warningBorder
                wrapMode: Text.Wrap
                text: qsTr("Enter a name and a valid color for every role.")
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("Delete…")
                    enabled: root.editingStyleId.length > 0
                    onClicked: deleteStyleConfirmation.open()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Assign selected")
                    enabled: root.editingStyleId.length > 0
                    onClicked: {
                        root.styleChosen(root.editingStyleId)
                        root.close()
                    }
                }
                Button {
                    text: qsTr("Save")
                    enabled: root.draftValid
                    onClicked: root.saveDraft(false)
                }
                Button {
                    text: qsTr("Save and assign")
                    enabled: root.draftValid
                    highlighted: true
                    onClicked: root.saveDraft(true)
                }
            }
        }
    }

    ColorDialog {
        id: styleColorPicker
        title: qsTr("Choose diagram style color")
        onAccepted: root.setDraftColor(
                        root.colorPickerRole,
                        uiTheme.colorText(selectedColor))
    }

    Dialog {
        id: deleteStyleConfirmation
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(430, parent.width - 40)
        height: 190
        modal: true
        focus: true
        title: qsTr("Delete project style?")
        standardButtons: Dialog.Yes | Dialog.No

        contentItem: Label {
            wrapMode: Text.Wrap
            text: {
                const count = projectController.diagramStyleAssignmentCount(
                                root.editingStyleId)
                return count > 0
                        ? qsTr("“%1” is assigned in %2 place(s). Deleting it will clear those assignments; affected elements will inherit their style.")
                              .arg(root.draftName).arg(count)
                        : qsTr("Delete “%1” from this project?")
                              .arg(root.draftName)
            }
        }

        onAccepted: {
            if (projectController.deleteDiagramStyle(root.editingStyleId)) {
                if (root.assignedStyleId === root.editingStyleId)
                    root.assignedStyleId = ""
                if (projectController.diagramStyles.length > 0)
                    root.editStyle(projectController.diagramStyles[0].id)
                else
                    root.beginNewStyle()
            }
        }
    }
}
