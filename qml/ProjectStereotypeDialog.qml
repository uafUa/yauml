import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

CatalogDialog {
    id: root

    property string targetKind: ""
    property string targetId: ""
    property string editingId: ""
    property string draftName: ""
    property bool draftPackage: false
    property bool draftClass: true
    property bool draftStruct: true
    property bool draftEnumeration: false
    property bool draftRelationship: false
    readonly property bool assignmentEnabled:
        targetId.length > 0
        && (targetKind === "element" || targetKind === "relationship")
    readonly property bool editingCommon:
        editingId.length > 0
        && projectController.stereotypeDefinition(editingId).common === true
    readonly property bool draftHasApplicability:
        draftPackage || draftClass || draftStruct
        || draftEnumeration || draftRelationship

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(820, parent.width - 40)
    height: Math.min(610, parent.height - 40)
    modal: true
    focus: true
    title: qsTr("UML stereotypes")
    standardButtons: Dialog.Close

    function populateAssignments() {
        assignmentModel.clear()
        if (!assignmentEnabled)
            return
        const assigned = projectController.stereotypeIdsForObject(
                           targetKind, targetId)
        const available = projectController.applicableStereotypes(
                            targetKind, targetId)
        for (let index = 0; index < available.length; ++index) {
            const definition = available[index]
            assignmentModel.append({
                "stereotypeId": definition.id,
                "stereotypeName": definition.name,
                "common": definition.common,
                "assigned": assigned.indexOf(definition.id) >= 0
            })
        }
    }

    function openFor(kind, objectId) {
        targetKind = kind
        targetId = objectId
        populateAssignments()
        tabs.currentIndex = 0
        open()
    }

    function openManager() {
        targetKind = ""
        targetId = ""
        assignmentModel.clear()
        tabs.currentIndex = 1
        if (projectController.stereotypeCatalog.length > 0)
            editDefinition(projectController.stereotypeCatalog[0].id)
        else
            beginNew()
        open()
    }

    function applyAssignments() {
        const ids = []
        for (let index = 0; index < assignmentModel.count; ++index) {
            const item = assignmentModel.get(index)
            if (item.assigned)
                ids.push(item.stereotypeId)
        }
        projectController.assignStereotypes(targetKind, targetId, ids)
        close()
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
        if (savedId.length > 0) {
            editDefinition(savedId)
            // A definition created from an assignment workflow should be
            // available as soon as the user returns to the Assignments tab.
            populateAssignments()
        }
    }

    ListModel { id: assignmentModel }

    contentItem: ColumnLayout {
        spacing: 8

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton {
                text: qsTr("Assignments")
                enabled: root.assignmentEnabled
            }
            TabButton { text: qsTr("Project catalog") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("Choose zero or more stereotypes. Common UML stereotypes are read-only; project stereotypes are managed on the second tab.")
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: assignmentModel
                    ScrollBar.vertical: ScrollBar {}
                    delegate: CheckDelegate {
                        required property int index
                        required property string stereotypeName
                        required property bool common
                        required property bool assigned
                        width: ListView.view.width
                        text: common
                              ? qsTr("«%1» — common UML").arg(stereotypeName)
                              : qsTr("«%1» — project").arg(stereotypeName)
                        checked: assigned
                        onToggled: assignmentModel.setProperty(
                                       index, "assigned", checked)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button {
                        text: qsTr("Apply")
                        highlighted: true
                        onClicked: root.applyAssignments()
                    }
                }
            }

            RowLayout {
                spacing: 12

                ColumnLayout {
                    Layout.preferredWidth: 235
                    Layout.fillHeight: true
                    Label {
                        text: qsTr("Common and project stereotypes")
                        font.bold: true
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
                            text: modelData.common
                                  ? qsTr("«%1»  (common)").arg(modelData.name)
                                  : qsTr("«%1»").arg(modelData.name)
                            highlighted: root.editingId === modelData.id
                            onClicked: root.editDefinition(modelData.id)
                        }
                    }
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("New project stereotype")
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
                              ? qsTr("Create project stereotype")
                              : root.editingCommon
                                ? qsTr("Common UML stereotype")
                                : qsTr("Edit project stereotype")
                        font.bold: true
                    }
                    Label {
                        visible: root.editingCommon
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: uiTheme.mutedText
                        text: qsTr("Common UML definitions are supplied by the application and cannot be edited or deleted.")
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        Label { text: qsTr("Name") }
                        TextField {
                            id: definitionName
                            Layout.fillWidth: true
                            enabled: !root.editingCommon
                            text: root.draftName
                            selectByMouse: true
                            onTextEdited: root.draftName = text
                            onAccepted: {
                                if (!root.editingCommon
                                        && root.draftName.trim().length > 0
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
                        enabled: !root.editingCommon
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
                        visible: !root.editingCommon
                                 && !root.draftHasApplicability
                        color: uiTheme.warningBorder
                        text: qsTr("Choose at least one applicable subject type.")
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            text: qsTr("Delete…")
                            visible: root.editingId.length > 0
                                     && !root.editingCommon
                            onClicked: deleteConfirmation.open()
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: qsTr("Save")
                            highlighted: true
                            visible: !root.editingCommon
                            enabled: root.draftName.trim().length > 0
                                     && root.draftHasApplicability
                            onClicked: root.saveDefinition()
                        }
                    }
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
            if (projectController.deleteProjectStereotype(root.editingId)) {
                if (projectController.stereotypeCatalog.length > 0)
                    root.editDefinition(
                        projectController.stereotypeCatalog[0].id)
                else
                    root.beginNew()
                root.populateAssignments()
            }
        }
    }
}
