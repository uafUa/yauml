import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Project-owned stereotype definition editor. Assignment is intentionally
// handled by StereotypeDropdown so choosing stereotypes remains lightweight.
CatalogDialog {
    id: root

    property string editingId: ""
    property string draftName: ""
    property bool draftPackage: false
    property bool draftClass: true
    property bool draftStruct: true
    property bool draftEnumeration: false
    property bool draftRelationship: false
    readonly property bool draftHasApplicability:
        draftPackage || draftClass || draftStruct
        || draftEnumeration || draftRelationship

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(720, parent.width - 40)
    height: Math.min(560, parent.height - 40)
    modal: true
    focus: true
    title: qsTr("Project stereotype catalogue")
    standardButtons: Dialog.Close

    function openManager() {
        const catalogue = projectController.stereotypeCatalog
        if (catalogue.length > 0)
            editDefinition(catalogue[0].id)
        else
            beginNew()
        open()
    }

    function beginNew() {
        editingId = ""
        draftName = qsTr("New stereotype")
        draftPackage = false
        draftClass = true
        draftStruct = true
        draftEnumeration = false
        draftRelationship = false
        definitionName.forceActiveFocus()
        definitionName.selectAll()
    }

    function editDefinition(stereotypeId) {
        const definition = projectController.stereotypeDefinition(stereotypeId)
        if (!definition || !definition.id) {
            beginNew()
            return
        }
        editingId = definition.id
        draftName = definition.name
        const applicability = definition.applicableTo || []
        draftPackage = applicability.indexOf("package") >= 0
        draftClass = applicability.indexOf("class") >= 0
        draftStruct = applicability.indexOf("struct") >= 0
        draftEnumeration = applicability.indexOf("enumeration") >= 0
        draftRelationship = applicability.indexOf("relationship") >= 0
    }

    function saveDefinition() {
        const applicableTo = []
        if (draftPackage)
            applicableTo.push("package")
        if (draftClass)
            applicableTo.push("class")
        if (draftStruct)
            applicableTo.push("struct")
        if (draftEnumeration)
            applicableTo.push("enumeration")
        if (draftRelationship)
            applicableTo.push("relationship")
        const savedId = projectController.saveProjectStereotype(
                          editingId, draftName, applicableTo)
        if (savedId.length > 0)
            editDefinition(savedId)
    }

    contentItem: RowLayout {
        spacing: 12

        ColumnLayout {
            Layout.preferredWidth: 235
            Layout.fillHeight: true
            Label {
                text: qsTr("Project stereotypes")
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: uiTheme.mutedText
                text: qsTr("New projects start with conventional UML stereotypes. Every entry belongs to this project and can be changed.")
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: projectController.stereotypeCatalog
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: qsTr("«%1»").arg(modelData.name)
                    highlighted: root.editingId === modelData.id
                    onClicked: root.editDefinition(modelData.id)
                }
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("New stereotype")
                onClicked: root.beginNew()
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
            spacing: 8
            Label {
                text: root.editingId.length === 0
                      ? qsTr("Create stereotype")
                      : qsTr("Edit stereotype")
                font.bold: true
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                Label { text: qsTr("Name") }
                TextField {
                    id: definitionName
                    Layout.fillWidth: true
                    text: root.draftName
                    selectByMouse: true
                    onTextEdited: root.draftName = text
                    onAccepted: {
                        if (root.draftName.trim().length > 0
                                && root.draftHasApplicability)
                            root.saveDefinition()
                    }
                }
            }
            Label {
                text: qsTr("Applicable to")
                font.bold: true
            }
            GridLayout {
                columns: 2
                CheckBox {
                    text: qsTr("Packages")
                    checked: root.draftPackage
                    onToggled: root.draftPackage = checked
                }
                CheckBox {
                    text: qsTr("Classes")
                    checked: root.draftClass
                    onToggled: root.draftClass = checked
                }
                CheckBox {
                    text: qsTr("Structs")
                    checked: root.draftStruct
                    onToggled: root.draftStruct = checked
                }
                CheckBox {
                    text: qsTr("Enumerations")
                    checked: root.draftEnumeration
                    onToggled: root.draftEnumeration = checked
                }
                CheckBox {
                    text: qsTr("Relationships")
                    checked: root.draftRelationship
                    onToggled: root.draftRelationship = checked
                }
            }
            Label {
                visible: !root.draftHasApplicability
                color: uiTheme.warningBorder
                text: qsTr("Choose at least one applicable subject type.")
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("Delete…")
                    visible: root.editingId.length > 0
                    onClicked: deleteConfirmation.open()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Save")
                    highlighted: true
                    enabled: root.draftName.trim().length > 0
                             && root.draftHasApplicability
                    onClicked: root.saveDefinition()
                }
            }
        }
    }

    CatalogDialog {
        id: deleteConfirmation
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(450, parent.width - 40)
        height: 190
        modal: true
        focus: true
        title: qsTr("Delete project stereotype?")
        standardButtons: Dialog.Yes | Dialog.No
        contentItem: Label {
            wrapMode: Text.Wrap
            text: {
                const count = projectController.stereotypeAssignmentCount(
                                root.editingId)
                return count > 0
                        ? qsTr("«%1» is assigned to %2 item(s). Deleting it will clear those assignments.")
                              .arg(root.draftName).arg(count)
                        : qsTr("Delete «%1» from this project?")
                              .arg(root.draftName)
            }
        }
        onAccepted: {
            if (!projectController.deleteProjectStereotype(root.editingId))
                return
            const catalogue = projectController.stereotypeCatalog
            if (catalogue.length > 0)
                root.editDefinition(catalogue[0].id)
            else
                root.beginNew()
        }
    }
}
