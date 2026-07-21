import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    x: workspaceController.mainWindowX
    y: workspaceController.mainWindowY
    width: workspaceController.mainWindowWidth
    height: workspaceController.mainWindowHeight
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: (projectController.dirty ? "*" : "") + projectController.projectName + " — u uml"

    property bool leftPanelVisible: workspaceController.projectTreeVisible
    property bool rightPanelVisible: workspaceController.propertiesVisible
    property bool geometryReady: false
    property bool closeAuthorized: false
    property bool quitScheduled: false
    property string pendingDocumentAction: ""

    Component.onCompleted: geometryReady = true
    onXChanged: if (geometryReady) workspaceController.updateMainWindowGeometry(x, y, width, height)
    onYChanged: if (geometryReady) workspaceController.updateMainWindowGeometry(x, y, width, height)
    onWidthChanged: if (geometryReady) workspaceController.updateMainWindowGeometry(x, y, width, height)
    onHeightChanged: if (geometryReady) workspaceController.updateMainWindowGeometry(x, y, width, height)
    onLeftPanelVisibleChanged: workspaceController.projectTreeVisible = leftPanelVisible
    onRightPanelVisibleChanged: workspaceController.propertiesVisible = rightPanelVisible

    function finishClose() {
        closeAuthorized = true
        root.close()
    }

    function performDocumentAction(action) {
        pendingDocumentAction = ""
        if (action === "close") {
            finishClose()
        } else if (action === "new") {
            projectController.newProject()
        } else if (action === "open") {
            openDialog.open()
        }
    }

    function requestDocumentAction(action) {
        if (projectController.dirty) {
            pendingDocumentAction = action
            unsavedChangesDialog.open()
        } else {
            performDocumentAction(action)
        }
    }

    function saveAndContinue() {
        if (projectController.projectPath.length > 0) {
            if (projectController.saveProject())
                performDocumentAction(pendingDocumentAction)
        } else {
            saveDialog.open()
        }
    }

    onClosing: function(close) {
        if (projectController.dirty && !closeAuthorized) {
            close.accepted = false
            pendingDocumentAction = "close"
            unsavedChangesDialog.open()
            return
        }
        close.accepted = true
        if (!quitScheduled) {
            quitScheduled = true
            // Do not remove detached-window delegates from inside this close
            // callback. Let the application event loop stop first so QML and
            // render-thread resources are destroyed in their normal order.
            Qt.callLater(Qt.quit)
        }
    }
    onActiveChanged: {
        if (active && mainDiagramArea.currentDiagramId.length > 0)
            workspaceController.activeDiagramId = mainDiagramArea.currentDiagramId
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            Action { text: qsTr("&New"); shortcut: StandardKey.New; onTriggered: root.requestDocumentAction("new") }
            Action { text: qsTr("&Open…"); shortcut: StandardKey.Open; onTriggered: root.requestDocumentAction("open") }
            Action {
                text: qsTr("&Save")
                shortcut: StandardKey.Save
                onTriggered: projectController.projectPath.length > 0
                             ? projectController.saveProject() : saveDialog.open()
            }
            MenuSeparator {}
            Action { text: qsTr("E&xit"); shortcut: StandardKey.Quit; onTriggered: root.close() }
        }
        Menu {
            title: qsTr("&Edit")
            Action { text: qsTr("&Undo"); shortcut: StandardKey.Undo; enabled: projectController.canUndo; onTriggered: projectController.undo() }
            Action { text: qsTr("&Redo"); shortcut: StandardKey.Redo; enabled: projectController.canRedo; onTriggered: projectController.redo() }
            MenuSeparator {}
            Action {
                text: qsTr("Delete selected project object")
                onTriggered: projectController.deleteSelected()
            }
        }
        Menu {
            title: qsTr("&View")
            Action { text: qsTr("Project tree"); checkable: true; checked: root.leftPanelVisible; onTriggered: root.leftPanelVisible = checked }
            Action { text: qsTr("Properties"); checkable: true; checked: root.rightPanelVisible; onTriggered: root.rightPanelVisible = checked }
            Action { text: qsTr("Log"); onTriggered: logPopup.open() }
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            ToolButton { text: qsTr("New"); onClicked: root.requestDocumentAction("new") }
            ToolButton { text: qsTr("Open"); onClicked: root.requestDocumentAction("open") }
            ToolButton {
                text: qsTr("Save")
                onClicked: projectController.projectPath.length > 0
                           ? projectController.saveProject() : saveDialog.open()
            }
            ToolSeparator {}
            ToolButton { text: qsTr("Undo"); enabled: projectController.canUndo; onClicked: projectController.undo() }
            ToolButton { text: qsTr("Redo"); enabled: projectController.canRedo; onClicked: projectController.redo() }
            ToolSeparator {}
            ToolButton {
                text: qsTr("+ Diagram")
                onClicked: {
                    const id = projectController.addDiagram()
                    workspaceController.activeDiagramId = id
                }
            }
            Item { Layout.fillWidth: true }
            ToolButton { text: root.leftPanelVisible ? "◀" : "▶"; onClicked: root.leftPanelVisible = !root.leftPanelVisible }
            ToolButton { text: root.rightPanelVisible ? "▶" : "◀"; onClicked: root.rightPanelVisible = !root.rightPanelVisible }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Pane {
            id: leftPanel
            visible: root.leftPanelVisible
            SplitView.preferredWidth: workspaceController.projectTreeWidth
            SplitView.minimumWidth: 170
            padding: 0
            onWidthChanged: {
                if (visible && width >= SplitView.minimumWidth)
                    workspaceController.updatePanelWidths(width, rightPanel.width)
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    color: "#e8edf2"
                    RowLayout {
                        anchors.fill: parent
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            leftPadding: 10
                            text: qsTr("Project")
                            font.bold: true
                        }
                        ToolButton {
                            text: qsTr("Delete")
                            enabled: projectController.selectedKind === "diagram"
                                     || projectController.selectedKind === "element"
                            onClicked: projectController.deleteSelected()
                            ToolTip.visible: hovered
                            ToolTip.text: projectController.selectedKind === "diagram"
                                          ? qsTr("Delete selected diagram")
                                          : qsTr("Delete selected model element")
                        }
                    }
                }
                TreeView {
                    id: projectTree
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: projectController.treeModel
                    clip: true

                    Component.onCompleted: expandRecursively()

                    Connections {
                        target: projectController.treeModel
                        function onModelReset() {
                            // Model edits replace rows, but should not destroy
                            // the user's navigation context.
                            Qt.callLater(function() { projectTree.expandRecursively() })
                        }
                    }
                    Connections {
                        target: projectController
                        function onSelectionChanged() {
                            Qt.callLater(function() {
                                projectTree.expandRecursively()
                                const itemIndex = projectController.treeModel.indexForObject(
                                                    projectController.selectedId,
                                                    projectController.selectedKind)
                                const row = projectTree.rowAtIndex(itemIndex)
                                if (row >= 0)
                                    projectTree.positionViewAtRow(row, TableView.Contain)
                            })
                        }
                    }

                    delegate: TreeViewDelegate {
                        id: treeDelegate
                        required property string objectId
                        required property string kind
                        required property string objectType
                        highlighted: kind !== "root" && (
                                         kind === "diagram"
                                         ? objectId === workspaceController.activeDiagramId
                                         : objectId === projectController.selectedId
                                           && kind === projectController.selectedKind)
                        background: Rectangle {
                            color: treeDelegate.highlighted ? "#1769d2"
                                 : treeDelegate.hovered ? "#edf3f8" : "transparent"
                        }
                        onClicked: {
                            if (kind !== "root")
                                projectController.selectObject(objectId, kind)
                        }
                        onDoubleClicked: {
                            if (kind === "element") {
                                projectController.selectObject(objectId, kind)
                                projectController.addSelectedToDiagram(workspaceController.activeDiagramId)
                            } else if (kind === "diagram") {
                                workspaceController.activeDiagramId = objectId
                            }
                        }
                    }
                }
            }
        }

        DiagramArea {
            id: mainDiagramArea
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            SplitView.minimumWidth: 400
            hostId: workspaceController.mainHostId
        }

        Pane {
            id: rightPanel
            visible: root.rightPanelVisible
            SplitView.preferredWidth: workspaceController.propertiesWidth
            SplitView.minimumWidth: 220
            padding: 0
            onWidthChanged: {
                if (visible && width >= SplitView.minimumWidth)
                    workspaceController.updatePanelWidths(leftPanel.width, width)
            }

            ScrollView {
                anchors.fill: parent
                clip: true
                ColumnLayout {
                    width: Math.max(rightPanel.width, 260)
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        padding: 10
                        text: qsTr("Selected element properties")
                        font.bold: true
                        background: Rectangle { color: "#e8edf2" }
                    }
                    Label {
                        Layout.leftMargin: 10
                        text: projectController.selectedKind.length > 0
                              ? projectController.selectedType : qsTr("Nothing selected")
                        color: "#64717d"
                    }
                    Label { Layout.leftMargin: 10; text: qsTr("Name"); visible: projectController.selectedKind.length > 0 }
                    TextField {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        visible: projectController.selectedKind.length > 0
                        text: projectController.selectedName
                        onEditingFinished: {
                            if (text !== projectController.selectedName)
                                projectController.selectedName = text
                        }
                    }
                    Label { Layout.leftMargin: 10; text: qsTr("Attributes — one per line"); visible: projectController.selectedKind === "element" && (projectController.selectedType === "class" || projectController.selectedType === "struct") }
                    TextArea {
                        id: attributesEditor
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        visible: projectController.selectedKind === "element" && (projectController.selectedType === "class" || projectController.selectedType === "struct")
                        text: projectController.selectedAttributes
                        wrapMode: TextEdit.NoWrap
                        background: Rectangle { color: "white"; border.color: "#b9c4ce"; radius: 3 }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Layout.rightMargin: 10
                        visible: attributesEditor.visible
                        Button {
                            text: qsTr("Apply")
                            enabled: attributesEditor.text !== projectController.selectedAttributes
                            onClicked: projectController.selectedAttributes = attributesEditor.text
                        }
                        Button {
                            text: qsTr("Revert")
                            enabled: attributesEditor.text !== projectController.selectedAttributes
                            onClicked: attributesEditor.text = projectController.selectedAttributes
                        }
                    }
                    Label { Layout.leftMargin: 10; text: qsTr("Operations — one per line"); visible: projectController.selectedKind === "element" && (projectController.selectedType === "class" || projectController.selectedType === "struct") }
                    TextArea {
                        id: operationsEditor
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        visible: projectController.selectedKind === "element" && (projectController.selectedType === "class" || projectController.selectedType === "struct")
                        text: projectController.selectedOperations
                        wrapMode: TextEdit.NoWrap
                        background: Rectangle { color: "white"; border.color: "#b9c4ce"; radius: 3 }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Layout.rightMargin: 10
                        visible: operationsEditor.visible
                        Button {
                            text: qsTr("Apply")
                            enabled: operationsEditor.text !== projectController.selectedOperations
                            onClicked: projectController.selectedOperations = operationsEditor.text
                        }
                        Button {
                            text: qsTr("Revert")
                            enabled: operationsEditor.text !== projectController.selectedOperations
                            onClicked: operationsEditor.text = projectController.selectedOperations
                        }
                    }
                    Label { Layout.leftMargin: 10; text: qsTr("Enumeration literals — one per line"); visible: projectController.selectedKind === "element" && projectController.selectedType === "enumeration" }
                    TextArea {
                        id: literalsEditor
                        Layout.fillWidth: true
                        Layout.preferredHeight: 160
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        visible: projectController.selectedKind === "element" && projectController.selectedType === "enumeration"
                        text: projectController.selectedLiterals
                        wrapMode: TextEdit.NoWrap
                        background: Rectangle { color: "white"; border.color: "#b9c4ce"; radius: 3 }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Layout.rightMargin: 10
                        visible: literalsEditor.visible
                        Button {
                            text: qsTr("Apply")
                            enabled: literalsEditor.text !== projectController.selectedLiterals
                            onClicked: projectController.selectedLiterals = literalsEditor.text
                        }
                        Button {
                            text: qsTr("Revert")
                            enabled: literalsEditor.text !== projectController.selectedLiterals
                            onClicked: literalsEditor.text = projectController.selectedLiterals
                        }
                    }
                    Connections {
                        target: projectController
                        function onSelectionChanged() {
                            // TextInput user edits and Revert assignments can
                            // replace declarative bindings. Refresh all draft
                            // editors explicitly when the model or selection
                            // changes so stale text never crosses selections.
                            if (!attributesEditor.activeFocus)
                                attributesEditor.text = projectController.selectedAttributes
                            if (!operationsEditor.activeFocus)
                                operationsEditor.text = projectController.selectedOperations
                            if (!literalsEditor.activeFocus)
                                literalsEditor.text = projectController.selectedLiterals
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.minimumHeight: 20 }
                }
            }
        }
    }

    Dialog {
        id: unsavedChangesDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 40)
        modal: true
        focus: true
        title: qsTr("Unsaved changes")
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: pendingDocumentAction === "close"
                  ? qsTr("Save changes to %1 before closing?").arg(projectController.projectName)
                  : qsTr("Save changes to %1 before continuing?").arg(projectController.projectName)
        }

        onAccepted: root.saveAndContinue()
        onDiscarded: root.performDocumentAction(root.pendingDocumentAction)
        onRejected: root.pendingDocumentAction = ""
    }

    Popup {
        id: logPopup
        parent: Overlay.overlay
        x: 16
        y: parent.height - height - 16
        width: parent.width - 32
        height: Math.min(300, parent.height * 0.38)
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape
        padding: 0

        background: Rectangle {
            color: "#ffffff"
            border.color: "#8f9ba6"
            radius: 5
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                Layout.leftMargin: 10
                Label { text: qsTr("Log"); font.bold: true }
                Item { Layout.fillWidth: true }
                ToolButton { text: qsTr("Clear"); onClicked: projectController.diagnostics.clear() }
                ToolButton { text: "×"; onClicked: logPopup.close() }
            }
            ListView {
                id: logList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.bottomMargin: 6
                clip: true
                model: projectController.diagnostics
                onCountChanged: Qt.callLater(function() { logList.positionViewAtEnd() })
                delegate: Rectangle {
                    required property int index
                    required property string severity
                    required property string category
                    required property string message
                    required property date timestamp
                    width: ListView.view.width
                    height: Math.max(34, logText.implicitHeight + 12)
                    color: severity === "error" ? "#fff0f0"
                         : severity === "warning" ? "#fff8e5"
                         : index % 2 ? "#f7f9fb" : "#ffffff"
                    Label {
                        id: logText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 8
                        text: timestamp.toLocaleTimeString(Qt.locale(), "HH:mm:ss")
                              + "  [" + severity.toUpperCase() + "/" + category + "]  " + message
                        wrapMode: Text.Wrap
                        color: "#263238"
                    }
                }
            }
        }
    }

    Connections {
        target: projectController.diagnostics
        function onErrorAdded() { logPopup.open() }
    }

    FolderDialog {
        id: openDialog
        title: qsTr("Open u uml project directory")
        onAccepted: projectController.openProject(selectedFolder)
    }

    FolderDialog {
        id: saveDialog
        title: qsTr("Choose u uml project directory")
        onAccepted: {
            const saved = projectController.saveProject(selectedFolder)
            if (saved && root.pendingDocumentAction.length > 0)
                root.performDocumentAction(root.pendingDocumentAction)
        }
        onRejected: root.pendingDocumentAction = ""
    }

    Instantiator {
        // A real item model updates hosts row-by-row. Existing windows are not
        // recreated when tabs change hosts, so their geometry stays intact.
        model: workspaceController.detachedHostModel
        delegate: DiagramWindow {
            required property string windowHostId
            hostId: windowHostId
        }
    }
}
