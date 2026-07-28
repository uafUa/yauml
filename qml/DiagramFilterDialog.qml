import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Edits the presentation-only filter persisted with one diagram. The filter
// never removes model elements or their diagram presentations; it only controls
// what the canvas renders and makes interactive.
CatalogDialog {
    id: root

    property var diagramCanvas: null
    property bool excludeClasses: false
    property bool excludeStructs: false
    property bool excludeEnumerations: false
    property string namePattern: ""
    property int nameMode: 0
    property string memberPattern: ""
    property int memberMode: 0

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(680, parent.width - 40)
    height: Math.min(650, parent.height - 40)
    modal: true
    focus: true
    title: qsTr("Filter diagram items")
    standardButtons: Dialog.Ok | Dialog.Cancel

    ListModel {
        id: stereotypeModel
    }

    function contains(values, value) {
        return values && values.indexOf(value) >= 0
    }

    function loadStereotypes(included, excluded) {
        stereotypeModel.clear()
        const catalogue = projectController.stereotypeCatalog
        for (let index = 0; index < catalogue.length; ++index) {
            const definition = catalogue[index]
            let mode = 0
            if (contains(included, definition.id))
                mode = 1
            else if (contains(excluded, definition.id))
                mode = 2
            stereotypeModel.append({
                stereotypeId: definition.id,
                stereotypeName: definition.name,
                filterMode: mode
            })
        }
    }

    function openForCanvas(canvas) {
        diagramCanvas = canvas
        const filter = canvas.diagramFilter()
        const excludedTypes = filter.excludedElementTypes || []
        excludeClasses = contains(excludedTypes, "class")
        excludeStructs = contains(excludedTypes, "struct")
        excludeEnumerations = contains(excludedTypes, "enumeration")
        namePattern = filter.namePattern || ""
        nameMode = filter.excludeNameMatches ? 1 : 0
        memberPattern = filter.memberPattern || ""
        memberMode = filter.excludeMemberMatches ? 1 : 0

        const included = filter.includedStereotypeIds || []
        const excluded = filter.excludedStereotypeIds || []
        loadStereotypes(included, excluded)
        open()
    }

    Component.onCompleted: loadStereotypes([], [])

    function clearCriteria() {
        excludeClasses = false
        excludeStructs = false
        excludeEnumerations = false
        namePattern = ""
        nameMode = 0
        memberPattern = ""
        memberMode = 0
        for (let index = 0; index < stereotypeModel.count; ++index)
            stereotypeModel.setProperty(index, "filterMode", 0)
    }

    function buildFilter() {
        const excludedTypes = []
        if (excludeClasses)
            excludedTypes.push("class")
        if (excludeStructs)
            excludedTypes.push("struct")
        if (excludeEnumerations)
            excludedTypes.push("enumeration")

        const includedStereotypes = []
        const excludedStereotypes = []
        for (let index = 0; index < stereotypeModel.count; ++index) {
            const entry = stereotypeModel.get(index)
            if (entry.filterMode === 1)
                includedStereotypes.push(entry.stereotypeId)
            else if (entry.filterMode === 2)
                excludedStereotypes.push(entry.stereotypeId)
        }
        return {
            excludedElementTypes: excludedTypes,
            includedStereotypeIds: includedStereotypes,
            excludedStereotypeIds: excludedStereotypes,
            namePattern: namePattern.trim(),
            excludeNameMatches: nameMode === 1,
            memberPattern: memberPattern.trim(),
            excludeMemberMatches: memberMode === 1
        }
    }

    onAccepted: {
        if (diagramCanvas)
            diagramCanvas.setDiagramFilter(buildFilter())
    }

    contentItem: ColumnLayout {
        spacing: 10

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: uiTheme.mutedText
            text: qsTr("Filtering only changes this diagram's view. Hidden items remain in the project and on the diagram.")
        }

        ScrollView {
            id: filterScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: filterScroll.availableWidth
                spacing: 12

                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Element types to hide")

                    GridLayout {
                        anchors.fill: parent
                        columns: 2
                        CheckBox {
                            text: qsTr("Classes")
                            checked: root.excludeClasses
                            onToggled: root.excludeClasses = checked
                        }
                        CheckBox {
                            text: qsTr("Structs")
                            checked: root.excludeStructs
                            onToggled: root.excludeStructs = checked
                        }
                        CheckBox {
                            text: qsTr("Enumerations")
                            checked: root.excludeEnumerations
                            onToggled: root.excludeEnumerations = checked
                        }
                        Label {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: uiTheme.mutedText
                            text: qsTr("Namespace and folder frames remain visible to preserve diagram context.")
                        }
                    }
                }

                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Name wildcard")

                    RowLayout {
                        anchors.fill: parent
                        ComboBox {
                            model: [qsTr("Show matching"),
                                    qsTr("Hide matching")]
                            currentIndex: root.nameMode
                            onActivated: root.nameMode = currentIndex
                        }
                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("For example: I*Service")
                            text: root.namePattern
                            selectByMouse: true
                            onTextEdited: root.namePattern = text
                        }
                    }
                }

                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Operation or field wildcard")

                    RowLayout {
                        anchors.fill: parent
                        ComboBox {
                            model: [qsTr("Show matching"),
                                    qsTr("Hide matching")]
                            currentIndex: root.memberMode
                            onActivated: root.memberMode = currentIndex
                        }
                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("For example: serialize*")
                            text: root.memberPattern
                            selectByMouse: true
                            onTextEdited: root.memberPattern = text
                        }
                    }
                }

                GroupBox {
                    Layout.fillWidth: true
                    title: qsTr("Stereotypes")

                    ColumnLayout {
                        anchors.fill: parent
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: uiTheme.mutedText
                            text: qsTr("Included stereotypes use “any” matching. Exclusions take precedence.")
                        }
                        Repeater {
                            model: stereotypeModel

                            RowLayout {
                                required property int index
                                required property string stereotypeId
                                required property string stereotypeName
                                required property int filterMode
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("«%1»").arg(parent.stereotypeName)
                                }
                                ComboBox {
                                    model: [qsTr("Ignore"), qsTr("Include"),
                                            qsTr("Exclude")]
                                    currentIndex: parent.filterMode
                                    onActivated: stereotypeModel.setProperty(
                                                     parent.index, "filterMode",
                                                     currentIndex)
                                }
                            }
                        }
                        Label {
                            visible: stereotypeModel.count === 0
                            color: uiTheme.mutedText
                            text: qsTr("This project has no stereotypes.")
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                text: qsTr("Clear all")
                onClicked: root.clearCriteria()
            }
            Item { Layout.fillWidth: true }
            Label {
                color: uiTheme.mutedText
                text: qsTr("* and ? wildcards are supported")
            }
        }
    }
}
