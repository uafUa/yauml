import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQml.Models

ApplicationWindow {
    id: root
    x: workspaceController.mainWindowX
    y: workspaceController.mainWindowY
    width: workspaceController.mainWindowWidth
    height: workspaceController.mainWindowHeight
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: (projectController.dirty ? "*" : "") + projectController.projectName + " — yauml"

    property bool leftPanelVisible: workspaceController.projectTreeVisible
    property bool rightPanelVisible: workspaceController.propertiesVisible
    property bool geometryReady: false
    property bool closeAuthorized: false
    property bool quitScheduled: false
    property string pendingDocumentAction: ""
    property url pendingRecentProjectUrl: ""
    property url pendingSaveUrl: ""
    property string updateResultMessage: ""
    property bool updateResultFailed: false
    property bool applyingProjectTreeColumnWidths: false

    Component.onCompleted: {
        geometryReady = true
        Qt.callLater(applyProjectTreeColumnWidths)
    }
    onXChanged: if (geometryReady) workspaceController.updateMainWindowGeometry(x, y, width, height)
    onYChanged: if (geometryReady) workspaceController.updateMainWindowGeometry(x, y, width, height)
    onWidthChanged: if (geometryReady) workspaceController.updateMainWindowGeometry(x, y, width, height)
    onHeightChanged: if (geometryReady) workspaceController.updateMainWindowGeometry(x, y, width, height)
    onLeftPanelVisibleChanged: {
        workspaceController.projectTreeVisible = leftPanelVisible
        if (!leftPanelVisible && projectTreeFindPopup.opened)
            projectTreeFindPopup.close()
    }
    onRightPanelVisibleChanged: workspaceController.propertiesVisible = rightPanelVisible

    function finishClose() {
        closeAuthorized = true
        root.close()
    }

    function performDocumentAction(action) {
        const recentProjectUrl = pendingRecentProjectUrl
        pendingDocumentAction = ""
        pendingRecentProjectUrl = ""
        if (action === "close") {
            finishClose()
        } else if (action === "new") {
            projectController.newProject()
        } else if (action === "open") {
            openDialog.open()
        } else if (action === "openRecent") {
            projectController.openProject(recentProjectUrl)
        } else if (action === "update") {
            if (updateController.scheduleUpdaterOnExit()) {
                finishClose()
            } else {
                updateResultFailed = true
                updateResultMessage =
                        qsTr("The maintenance tool is not available. Reinstall yauml using the latest installer to enable in-place updates.")
                updateCheckResultDialog.open()
            }
        }
    }

    function requestDocumentAction(action) {
        if (action !== "openRecent")
            pendingRecentProjectUrl = ""
        if (projectController.dirty) {
            pendingDocumentAction = action
            unsavedChangesDialog.open()
        } else {
            performDocumentAction(action)
        }
    }

    function requestRecentProject(projectUrl) {
        pendingRecentProjectUrl = projectUrl
        requestDocumentAction("openRecent")
    }

    function requestUpdateInstall() {
        if (updateController.maintenanceToolAvailable)
            requestDocumentAction("update")
        else
            updateController.openReleasePage()
    }

    function cancelPendingDocumentAction() {
        pendingDocumentAction = ""
        pendingRecentProjectUrl = ""
    }

    function setProjectTreeColumnVisible(columnKey, visible) {
        rememberProjectTreeColumnWidths()
        const columns = applicationSettings.projectTreeColumns.slice()
        const index = columns.indexOf(columnKey)
        if (visible && index < 0)
            columns.push(columnKey)
        else if (!visible && index >= 0)
            columns.splice(index, 1)
        applicationSettings.projectTreeColumns = columns
        Qt.callLater(applyProjectTreeColumnWidths)
    }

    function applyProjectTreeColumnWidths() {
        if (projectTree.columns <= 0)
            return
        applyingProjectTreeColumnWidths = true
        projectTree.clearColumnWidths()
        const columns = applicationSettings.projectTreeColumns
        const widths = applicationSettings.projectTreeColumnWidths
        for (let column = 0; column < columns.length; ++column)
            projectTree.setColumnWidth(column, widths[columns[column]])
        projectTree.forceLayout()
        Qt.callLater(function() {
            applyingProjectTreeColumnWidths = false
        })
    }

    function rememberProjectTreeColumnWidths() {
        if (applyingProjectTreeColumnWidths || projectTree.columns <= 0)
            return
        const columns = applicationSettings.projectTreeColumns
        const widths = Object.assign({},
                                     applicationSettings.projectTreeColumnWidths)
        for (let column = 0; column < columns.length; ++column) {
            const width = projectTree.explicitColumnWidth(column)
            if (width > 0)
                widths[columns[column]] = Math.round(width)
        }
        applicationSettings.projectTreeColumnWidths = widths
    }

    Timer {
        id: projectTreeColumnWidthSaveTimer
        interval: 200
        repeat: false
        onTriggered: root.rememberProjectTreeColumnWidths()
    }

    function saveAndContinue() {
        if (projectController.projectPath.length > 0) {
            if (projectController.saveProject())
                performDocumentAction(pendingDocumentAction)
        } else {
            saveDialog.open()
        }
    }

    function saveToSelectedFolder(folderUrl, overwriteExisting) {
        const saved = projectController.saveProject(folderUrl, overwriteExisting)
        pendingSaveUrl = ""
        if (saved && root.pendingDocumentAction.length > 0)
            root.performDocumentAction(root.pendingDocumentAction)
    }

    function overwriteExternallyChangedProject() {
        const saved = projectController.overwriteExternallyChangedProject()
        externalProjectChangesDialog.close()
        if (saved && root.pendingDocumentAction.length > 0)
            root.performDocumentAction(root.pendingDocumentAction)
    }

    function reloadExternallyChangedProject() {
        projectController.reloadProjectFromDisk()
        externalProjectChangesDialog.close()
        root.cancelPendingDocumentAction()
    }

    function saveExternallyChangedProjectAs() {
        externalProjectChangesDialog.close()
        pendingSaveUrl = ""
        Qt.callLater(saveDialog.open)
    }

    function createFolderAt(parentKind, parentId) {
        folderNameDialog.mode = "create"
        folderNameDialog.folderId = ""
        folderNameDialog.parentKind = parentKind
        folderNameDialog.parentId = parentId
        folderNameDialog.open()
    }

    function renameFolder(folderId, currentName) {
        folderNameDialog.mode = "rename"
        folderNameDialog.folderId = folderId
        folderNameDialog.currentName = currentName
        folderNameDialog.open()
    }

    onClosing: function(close) {
        rememberProjectTreeColumnWidths()
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
            CatalogAction {
                catalogId: "project.newProject"
                text: qsTr("&New")
                shortcut: StandardKey.New
                onTriggered: root.requestDocumentAction("new")
            }
            CatalogAction {
                catalogId: "project.openProject"
                text: qsTr("&Open…")
                shortcut: StandardKey.Open
                onTriggered: root.requestDocumentAction("open")
            }
            Menu {
                id: recentProjectsMenu
                title: qsTr("Open &Recent…")

                Instantiator {
                    model: applicationSettings.recentProjects
                    delegate: CatalogMenuItem {
                        required property int index
                        required property var modelData
                        catalogId: "project.openRecent"
                        text: qsTr("%1. %2 — %3")
                              .arg(index + 1)
                              .arg(modelData.name)
                              .arg(modelData.displayPath)
                        onTriggered: root.requestRecentProject(modelData.url)
                    }
                    onObjectAdded: function(index, object) {
                        recentProjectsMenu.insertItem(index, object)
                    }
                    onObjectRemoved: function(index, object) {
                        recentProjectsMenu.removeItem(object)
                    }
                }
                MenuItem {
                    visible: applicationSettings.recentProjects.length === 0
                    height: visible ? implicitHeight : 0
                    text: qsTr("No recent projects")
                    enabled: false
                }
                MenuSeparator {
                    visible: applicationSettings.recentProjects.length > 0
                    height: visible ? implicitHeight : 0
                }
                CatalogMenuItem {
                    catalogId: "project.clearRecent"
                    visible: applicationSettings.recentProjects.length > 0
                    height: visible ? implicitHeight : 0
                    text: qsTr("Clear Recent Projects")
                    onTriggered: applicationSettings.clearRecentProjects()
                }
            }
            CatalogAction {
                catalogId: "project.saveProject"
                text: qsTr("&Save")
                shortcut: StandardKey.Save
                onTriggered: projectController.projectPath.length > 0
                             ? projectController.saveProject() : saveDialog.open()
            }
            MenuSeparator {}
            CatalogAction {
                catalogId: "project.synchronizeCpp"
                text: qsTr("Synchronize C++")
                enabled: cppImportController.canSynchronize
                onTriggered: {
                    cppImportDialog.open()
                    cppImportController.synchronize()
                }
            }
            CatalogAction {
                catalogId: "project.configureCppSource"
                text: cppImportController.configuredSourceRoots.length > 0
                      ? qsTr("Change C++ sources…")
                      : qsTr("Import C++…")
                enabled: !cppImportController.busy
                onTriggered: sourceFolderPicker.open(
                                 root,
                                 cppImportController.configuredSourceRoots)
            }
            MenuSeparator {}
            CatalogAction {
                catalogId: "project.exit"
                text: qsTr("E&xit")
                shortcut: StandardKey.Quit
                onTriggered: root.close()
            }
        }
        Menu {
            title: qsTr("&Edit")
            CatalogAction {
                catalogId: "edit.undo"
                text: qsTr("&Undo")
                shortcut: StandardKey.Undo
                enabled: projectController.canUndo
                onTriggered: projectController.undo()
            }
            CatalogAction {
                catalogId: "edit.redo"
                text: qsTr("&Redo")
                shortcut: StandardKey.Redo
                enabled: projectController.canRedo
                onTriggered: projectController.redo()
            }
            MenuSeparator {}
            CatalogAction {
                catalogId: "source.open"
                text: qsTr("Open in &VS Code")
                enabled: sourceEditorController.canOpenObject(
                             projectController.selectedKind,
                             projectController.selectedId)
                onTriggered: sourceEditorController.openObject(
                                 projectController.selectedKind,
                                 projectController.selectedId)
            }
            MenuSeparator {}
            CatalogAction {
                catalogId: "style.manage"
                text: qsTr("Project diagram &styles…")
                onTriggered: browserStyleDialog.openManager()
            }
            MenuItem {
                text: qsTr("Project &stereotypes…")
                onTriggered: projectStereotypeDialog.openManager()
            }
            CatalogAction {
                catalogId: "edit.preferences"
                text: qsTr("&Preferences…")
                shortcut: "Ctrl+,"
                onTriggered: preferencesDialog.open()
            }
        }
        Menu {
            title: qsTr("&View")
            CatalogAction {
                catalogId: "workspace.toggleProjectTree"
                text: qsTr("Project tree")
                checkable: true
                checked: root.leftPanelVisible
                onTriggered: root.leftPanelVisible = checked
            }
            CatalogAction {
                catalogId: "workspace.toggleProperties"
                text: qsTr("Properties")
                checkable: true
                checked: root.rightPanelVisible
                onTriggered: root.rightPanelVisible = checked
            }
            CatalogAction {
                catalogId: "workspace.showLog"
                text: qsTr("Log")
                onTriggered: logPopup.open()
            }
        }
        Menu {
            title: qsTr("&Help")
            CatalogAction {
                catalogId: "help.checkUpdates"
                text: updateController.checking
                      ? qsTr("Checking for &Updates…")
                      : qsTr("Check for &Updates…")
                enabled: !updateController.checking
                onTriggered: updateController.checkForUpdates(true)
            }
        }
    }

    Shortcut {
        sequence: "F12"
        context: Qt.ApplicationShortcut
        enabled: sourceEditorController.canOpenObject(
                     projectController.selectedKind,
                     projectController.selectedId)
        onActivated: sourceEditorController.openObject(
                         projectController.selectedKind,
                         projectController.selectedId)
    }

    Shortcut {
        sequences: [StandardKey.Find]
        context: Qt.ApplicationShortcut
        onActivated: projectTreeFindPopup.openForFind()
    }

    Shortcut {
        sequences: [StandardKey.FindNext]
        context: Qt.ApplicationShortcut
        enabled: projectTreeFindPopup.opened
        onActivated: projectTreeFindPopup.advance(1)
    }

    Shortcut {
        sequences: [StandardKey.FindPrevious]
        context: Qt.ApplicationShortcut
        enabled: projectTreeFindPopup.opened
        onActivated: projectTreeFindPopup.advance(-1)
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            CatalogToolButton {
                catalogId: "project.newProject"
                text: qsTr("New")
                onClicked: root.requestDocumentAction("new")
            }
            CatalogToolButton {
                catalogId: "project.openProject"
                text: qsTr("Open")
                onClicked: root.requestDocumentAction("open")
            }
            CatalogToolButton {
                catalogId: "project.saveProject"
                text: qsTr("Save")
                onClicked: projectController.projectPath.length > 0
                           ? projectController.saveProject() : saveDialog.open()
            }
            ToolSeparator {}
            CatalogToolButton {
                catalogId: "edit.undo"
                text: qsTr("Undo")
                enabled: projectController.canUndo
                onClicked: projectController.undo()
            }
            CatalogToolButton {
                catalogId: "edit.redo"
                text: qsTr("Redo")
                enabled: projectController.canRedo
                onClicked: projectController.redo()
            }
            ToolSeparator {}
            CatalogToolButton {
                catalogId: "workspace.addDiagram"
                text: qsTr("+ Diagram")
                onClicked: {
                    const id = projectController.addDiagram()
                    workspaceController.activeDiagramId = id
                }
            }
            Item { Layout.fillWidth: true }
            CatalogToolButton {
                catalogId: "workspace.toggleProjectTree"
                text: root.leftPanelVisible ? "◀" : "▶"
                onClicked: root.leftPanelVisible = !root.leftPanelVisible
            }
            CatalogToolButton {
                catalogId: "workspace.toggleProperties"
                text: root.rightPanelVisible ? "▶" : "◀"
                onClicked: root.rightPanelVisible = !root.rightPanelVisible
            }
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
                    color: uiTheme.panelHeader
                    RowLayout {
                        anchors.fill: parent
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            leftPadding: 10
                            text: qsTr("Project")
                            font.bold: true
                        }
                        CatalogToolButton {
                            catalogId: "browser.createFolder"
                            text: qsTr("+ Folder")
                            onClicked: root.createFolderAt("model", "")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Create a project-tree folder")
                        }
                    }
                }
                RowLayout {
                    id: projectTreeFilterRow
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 4
                    Layout.topMargin: 6
                    Layout.bottomMargin: 6
                    spacing: 2

                    TextField {
                        id: projectTreeFilter
                        objectName: "projectTreeFilter"
                        Layout.fillWidth: true
                        placeholderText: qsTr("Filter tree (* and ? wildcards)")
                        text: projectController.treeModel.filterPattern
                        Accessible.name: qsTr("Filter project tree")
                        onTextChanged:
                            projectController.treeModel.filterPattern = text
                        Keys.onEscapePressed: clear()
                    }
                    ToolButton {
                        objectName: "clearProjectTreeFilter"
                        text: "×"
                        visible: projectTreeFilter.text.length > 0
                        Accessible.name: qsTr("Clear project-tree filter")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Clear filter")
                        onClicked: {
                            projectTreeFilter.clear()
                            projectTreeFilter.forceActiveFocus()
                        }
                    }
                    CatalogToolButton {
                        id: projectTreeColumnsButton
                        objectName: "projectTreeColumnsButton"
                        catalogId: "browser.chooseColumns"
                        text: "▥"
                        Accessible.name: qsTr("Configure project tree")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Project-tree display")
                        onClicked: projectTreeColumnsMenu.popup(
                                       projectTreeColumnsButton, 0, height)

                        Menu {
                            id: projectTreeColumnsMenu
                            MenuItem {
                                text: qsTr("Name")
                                checkable: true
                                checked: true
                                enabled: false
                            }
                            MenuItem {
                                text: qsTr("Relative source path")
                                checkable: true
                                checked: applicationSettings.projectTreeColumns
                                         .indexOf("sourceDirectory") >= 0
                                onToggled: root.setProjectTreeColumnVisible(
                                               "sourceDirectory", checked)
                            }
                            MenuItem {
                                text: qsTr("File name")
                                checkable: true
                                checked: applicationSettings.projectTreeColumns
                                         .indexOf("sourceFile") >= 0
                                onToggled: root.setProjectTreeColumnVisible(
                                               "sourceFile", checked)
                            }
                            MenuItem {
                                text: qsTr("Stereotypes")
                                checkable: true
                                checked: applicationSettings.projectTreeColumns
                                         .indexOf("stereotypes") >= 0
                                onToggled: root.setProjectTreeColumnVisible(
                                               "stereotypes", checked)
                            }
                            MenuItem {
                                text: qsTr("Type")
                                checkable: true
                                checked: applicationSettings.projectTreeColumns
                                         .indexOf("type") >= 0
                                onToggled: root.setProjectTreeColumnVisible(
                                               "type", checked)
                            }
                            MenuItem {
                                text: qsTr("Qualified name")
                                checkable: true
                                checked: applicationSettings.projectTreeColumns
                                         .indexOf("qualifiedName") >= 0
                                onToggled: root.setProjectTreeColumnVisible(
                                               "qualifiedName", checked)
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: qsTr("Show relationships")
                                checkable: true
                                checked: applicationSettings
                                         .projectTreeRelationshipsVisible
                                onToggled: applicationSettings
                                           .projectTreeRelationshipsVisible =
                                               checked
                            }
                        }
                    }
                }
                Binding {
                    target: projectController.treeModel
                    property: "columns"
                    value: applicationSettings.projectTreeColumns
                }
                Binding {
                    target: projectController.treeModel
                    property: "relationshipsVisible"
                    value: applicationSettings.projectTreeRelationshipsVisible
                }
                HorizontalHeaderView {
                    id: projectTreeHeader
                    objectName: "projectTreeHeader"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    syncView: projectTree
                    resizableColumns: true
                    clip: true

                    delegate: HorizontalHeaderViewDelegate {
                        id: projectTreeHeaderDelegate

                        // Keep the platform header styling and only overlay a
                        // quiet boundary. Explicit parenting prevents Control
                        // from moving the line into its content item.
                        Rectangle {
                            parent: projectTreeHeaderDelegate
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            width: 1
                            color: uiTheme.overlayBorder
                            opacity: 0.32
                            z: 2
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: function(mouse) {
                            projectTreeColumnsMenu.popup(
                                        projectTreeHeader, mouse.x, mouse.y)
                        }
                    }
                }
                TreeView {
                    id: projectTree
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: projectController.treeModel
                    onColumnsChanged:
                        Qt.callLater(root.applyProjectTreeColumnWidths)
                    onLayoutChanged: {
                        if (!root.applyingProjectTreeColumnWidths)
                            projectTreeColumnWidthSaveTimer.restart()
                    }
                    clip: true
                    // Apply selection explicitly in selectFromPointer(). The
                    // native delegate can otherwise collapse an extended
                    // selection before Ctrl/Shift handling on Windows.
                    pointerNavigationEnabled: false
                    selectionBehavior: TableView.SelectRows
                    selectionMode: TableView.ExtendedSelection
                    readonly property string browserItemsMimeType:
                        "application/x-yauml-browser-items"
                    property bool selectionOriginatesFromTree: false
                    function revealProjectSelection(selectRow) {
                        const itemIndex =
                                projectController.treeModel.indexForObject(
                                    projectController.selectedId,
                                    projectController.selectedKind)
                        if (!itemIndex.valid)
                            return
                        projectTree.expandToIndex(itemIndex)
                        // Expansion updates TreeView's flattened row mapping.
                        // Select and scroll only after that mapping is current.
                        Qt.callLater(function() {
                            if (selectRow
                                    && (projectController.selectedKind === "element"
                                        || projectController.selectedKind
                                           === "relationship")) {
                                projectTreeSelection.select(
                                            itemIndex,
                                            ItemSelectionModel.ClearAndSelect
                                            | ItemSelectionModel.Rows)
                                projectTreeSelection.setCurrentIndex(
                                            itemIndex,
                                            ItemSelectionModel.NoUpdate)
                            }
                            const row = projectTree.rowAtIndex(itemIndex)
                            if (row >= 0)
                                projectTree.positionViewAtRow(
                                            row, TableView.Contain)
                        })
                    }
                    function toggleBranch(row) {
                        if (row < 0)
                            return
                        if (projectTree.isExpanded(row))
                            projectTree.collapse(row)
                        else
                            projectTree.expand(row)
                    }
                    function toggleBranchForObject(objectId, kind) {
                        // Delegate rows are flattened view coordinates and may
                        // change as neighboring branches open or close. Resolve
                        // the current row from the stable model identity at the
                        // moment of the click.
                        const itemIndex =
                                projectController.treeModel.indexForObject(
                                    objectId, kind)
                        if (!itemIndex.valid)
                            return
                        toggleBranch(projectTree.rowAtIndex(itemIndex))
                    }
                    function selectedBrowserItemsJson() {
                        return projectController.treeModel.browserItemsJsonForIndexes(
                                    projectTreeSelection.selectedIndexes)
                    }
                    function deleteSelectedBrowserItems() {
                        const itemsJson = selectedBrowserItemsJson()
                        if (JSON.parse(itemsJson).length > 0)
                            projectController.deleteBrowserItems(itemsJson)
                    }
                    function moveBrowserItemsFromDrop(drop, targetKind,
                                                      targetId) {
                        if (drop.formats.indexOf(browserItemsMimeType) < 0)
                            return false
                        const itemsJson = drop.getDataAsString(
                                            browserItemsMimeType)
                        const semanticChange =
                                projectController.browserMoveSemanticChangeSummary(
                                    itemsJson, targetKind, targetId)
                        if (semanticChange.length > 0
                                && applicationSettings.packageReassignmentPolicy
                                   === "disallow")
                            return false
                        if (semanticChange.length > 0
                                && applicationSettings.packageReassignmentPolicy
                                   === "ask") {
                            packageMoveConfirmation.itemsJson = itemsJson
                            packageMoveConfirmation.targetKind = targetKind
                            packageMoveConfirmation.targetId = targetId
                            packageMoveConfirmation.message = semanticChange
                            packageMoveConfirmation.open()
                            drop.acceptProposedAction()
                            return true
                        }
                        const changed = semanticChange.length > 0
                                ? projectController.moveBrowserItemsWithSemanticReassignment(
                                      itemsJson, targetKind, targetId)
                                : projectController.moveBrowserItems(
                                      itemsJson, targetKind, targetId)
                        if (changed)
                            drop.acceptProposedAction()
                        return changed
                    }
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Delete) {
                            deleteSelectedBrowserItems()
                            event.accepted = true
                        }
                    }
                    selectionModel: ItemSelectionModel {
                        id: projectTreeSelection
                        model: projectController.treeModel
                    }

                    Component.onCompleted: expandRecursively()

                    // Observe without blocking TreeView's own scrolling. The
                    // shared guard drains any inertial tail after the pointer
                    // crosses into a diagram instead of treating it as zoom.
                    WheelHandler {
                        target: null
                        blocking: false
                        onWheel: workspaceController.noteProjectTreeWheelInput()
                    }

                    Connections {
                        target: projectController.treeModel
                        function onModelReset() {
                            // Model edits replace rows, but should not destroy
                            // the user's navigation context.
                            Qt.callLater(function() {
                                projectTree.expandRecursively()
                                projectTree.revealProjectSelection(true)
                                if (projectTreeFindPopup.opened)
                                    projectTreeFindPopup.refreshMatches(true)
                            })
                        }
                    }
                    Connections {
                        target: projectController
                        function revealControllerSelection() {
                            const originatedFromTree = projectTree.selectionOriginatesFromTree
                            Qt.callLater(function() {
                                projectTree.revealProjectSelection(
                                            !originatedFromTree)
                            })
                        }
                        function onSelectionChanged() {
                            revealControllerSelection()
                        }
                        function onSelectionReasserted() {
                            revealControllerSelection()
                        }
                    }

                    delegate: TreeViewDelegate {
                        id: treeDelegate
                        required property string objectId
                        required property string kind
                        required property string objectType
                        required property bool nested
                        required property string name
                        property bool browserDropActive: false
                        property int browserInsertionEdge: 0
                        readonly property url treeIconSource:
                            column === 0 ? iconRegistry.projectTreeIcon(
                                kind, objectType, objectId, nested, expanded)
                                         : ""
                        highlighted: kind !== "root" && (
                                         kind === "diagram"
                                         ? objectId === workspaceController.activeDiagramId
                                         : selected)
                        // Fusion's standard TreeViewDelegate content item only
                        // paints the model text, even though the delegate
                        // exposes an icon property. Keep the standard tree
                        // indentation and disclosure indicator, but paint the
                        // configured project icon explicitly beside the label.
                        contentItem: RowLayout {
                            spacing: treeIcon.visible ? 4 : 0
                            visible: !treeDelegate.editing

                            Image {
                                id: treeIcon
                                visible: treeDelegate.column === 0
                                         && treeDelegate.treeIconSource.toString() !== ""
                                source: treeDelegate.treeIconSource
                                fillMode: Image.PreserveAspectFit
                                sourceSize.width: iconRegistry.defaultSize
                                sourceSize.height: iconRegistry.defaultSize
                                Layout.preferredWidth: visible
                                                       ? iconRegistry.defaultSize : 0
                                Layout.preferredHeight: visible
                                                        ? iconRegistry.defaultSize : 0
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Label {
                                text: treeDelegate.model.display
                                font: treeDelegate.font
                                color: treeDelegate.highlighted
                                       ? treeDelegate.palette.highlightedText
                                       : treeDelegate.palette.buttonText
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }
                        background: Rectangle {
                            color: treeDelegate.highlighted ? uiTheme.accent
                                 : treeDelegate.browserDropActive ? uiTheme.hoverBackground
                                 : treeDelegate.hovered ? uiTheme.hoverBackground : "transparent"
                        }
                        onClicked: {
                            if (kind !== "root") {
                                // TreeView rows are flattened view coordinates.
                                // Resolve the semantic model index by stable ID
                                // so nested rows cannot select a sibling/child.
                                const itemIndex =
                                        projectController.treeModel.indexForObject(
                                            treeDelegate.objectId,
                                            treeDelegate.kind)
                                projectController.treeModel.selectFromPointer(
                                            projectTreeSelection, itemIndex)
                                if (kind === "element"
                                        || kind === "diagram"
                                        || kind === "relationship")
                                    projectController.selectObject(objectId, kind)
                                else
                                    projectController.clearSelection()
                            }
                        }
                        onDoubleClicked: {
                            if (kind === "element"
                                    || kind === "folder"
                                    || kind === "namespace") {
                                if (kind === "element")
                                    projectController.selectObject(objectId, kind)
                                projectController.addBrowserItemToDiagram(
                                            workspaceController.activeDiagramId,
                                            kind, objectId,
                                            applicationSettings.diagramItemSizingMode)
                            } else if (kind === "diagram") {
                                workspaceController.activeDiagramId = objectId
                            } else if (treeDelegate.isTreeNode
                                       && treeDelegate.hasChildren) {
                                projectTree.toggleBranchForObject(
                                            treeDelegate.objectId,
                                            treeDelegate.kind)
                            }
                        }

                        onPressed: projectTree.selectionOriginatesFromTree = true
                        onReleased: Qt.callLater(function() {
                            projectTree.selectionOriginatesFromTree = false
                        })
                        onCanceled: projectTree.selectionOriginatesFromTree = false

                        // pointerNavigationEnabled is intentionally disabled so
                        // Ctrl/Shift row selection remains under our control.
                        // Restore the standard disclosure-arrow behavior with a
                        // dedicated hit target that does not change selection.
                        // Keep it as a top-level delegate child: parenting it
                        // inside the style's indicator leaves later row-wide
                        // drag/drop layers above it and makes the arrow inert.
                        MouseArea {
                            id: disclosureArea
                            // Control's default property reparents visual
                            // children into contentItem.  The indicator's x is
                            // expressed in delegate coordinates, so leaving
                            // this implicit shifts the hit target away from the
                            // painted arrow.  Keep both in the same coordinate
                            // system explicitly.
                            parent: treeDelegate
                            x: treeDelegate.indicator
                               ? treeDelegate.indicator.x : 0
                            y: 0
                            width: treeDelegate.indicator
                                   ? Math.max(20,
                                              treeDelegate.indicator.width) : 20
                            height: treeDelegate.height
                            z: 20
                            enabled: treeDelegate.isTreeNode
                                     && treeDelegate.hasChildren
                            acceptedButtons: Qt.LeftButton
                            preventStealing: true
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.Button
                            Accessible.name: (treeDelegate.expanded
                                              ? qsTr("Collapse %1")
                                              : qsTr("Expand %1"))
                                             .arg(treeDelegate.name)
                            onClicked: projectTree.toggleBranchForObject(
                                           treeDelegate.objectId,
                                           treeDelegate.kind)
                        }

                        DragHandler {
                            target: null
                            // Every visible column represents the same browser
                            // row. Starting a drag from metadata must therefore
                            // behave like starting it from the name cell.
                            enabled: treeDelegate.kind === "element"
                                     || treeDelegate.kind === "namespace"
                                     || treeDelegate.kind === "folder"
                            // Starting QDrag transfers the native mouse grab
                            // away from this handler. Include the approval and
                            // cancellation flags so TreeView's flick gesture
                            // cannot win and pan the whole browser instead.
                            grabPermissions:
                                PointerHandler.CanTakeOverFromAnything
                                | PointerHandler.ApprovesTakeOverByAnything
                                | PointerHandler.ApprovesCancellation
                            onActiveChanged: {
                                if (!active)
                                    return
                                const itemIndex =
                                        projectController.treeModel.indexForObject(
                                            treeDelegate.objectId,
                                            treeDelegate.kind)
                                if (!projectTreeSelection.isSelected(itemIndex)) {
                                    projectTreeSelection.select(
                                                itemIndex,
                                                ItemSelectionModel.ClearAndSelect
                                                | ItemSelectionModel.Rows)
                                }
                                projectController.treeModel.startTreeDrag(
                                            projectTreeSelection.selectedIndexes)
                            }
                        }

                        DropArea {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.topMargin: 7
                            anchors.bottomMargin: 7
                            // A row remains one semantic drop target even when
                            // optional metadata columns are visible. Restricting
                            // this area to column zero made a drop silently fail
                            // whenever the pointer happened to be over Source,
                            // Stereotypes, or another metadata cell.
                            enabled: treeDelegate.kind === "namespace"
                                     || treeDelegate.kind === "element"
                                     || treeDelegate.kind === "folder"
                                     || (treeDelegate.kind === "root"
                                         && treeDelegate.objectId === "model")
                            keys: [projectTree.browserItemsMimeType]
                            onEntered: treeDelegate.browserDropActive = true
                            onExited: treeDelegate.browserDropActive = false
                            onDropped: function(drop) {
                                treeDelegate.browserDropActive = false
                                const targetKind = treeDelegate.kind === "root"
                                                 ? "model" : treeDelegate.kind
                                const targetId = targetKind === "model"
                                               ? "" : treeDelegate.objectId
                                projectTree.moveBrowserItemsFromDrop(
                                            drop, targetKind, targetId)
                            }
                        }

                        Repeater {
                            model: [-1, 1]
                            delegate: DropArea {
                                required property int modelData
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: modelData < 0
                                             ? parent.top : undefined
                                anchors.bottom: modelData > 0
                                                ? parent.bottom : undefined
                                height: 7
                                z: 4
                                enabled: treeDelegate.column === 0
                                         && (treeDelegate.kind === "element"
                                             || treeDelegate.kind === "folder")
                                keys: [projectTree.browserItemsMimeType]
                                function canReorder(drag) {
                                    if (drag.formats.indexOf(
                                                projectTree.browserItemsMimeType) < 0)
                                        return false
                                    return projectController.canReorderBrowserItemsAround(
                                                drag.getDataAsString(
                                                    projectTree.browserItemsMimeType),
                                                treeDelegate.kind,
                                                treeDelegate.objectId)
                                }
                                onEntered: function(drag) {
                                    const reorder = canReorder(drag)
                                    treeDelegate.browserInsertionEdge =
                                            reorder ? modelData : 0
                                    treeDelegate.browserDropActive = !reorder
                                }
                                onExited: {
                                    treeDelegate.browserInsertionEdge = 0
                                    treeDelegate.browserDropActive = false
                                }
                                onDropped: function(drop) {
                                    const reorder = canReorder(drop)
                                    treeDelegate.browserInsertionEdge = 0
                                    treeDelegate.browserDropActive = false
                                    if (reorder) {
                                        const changed =
                                                projectController.reorderBrowserItemsAround(
                                                    drop.getDataAsString(
                                                        projectTree.browserItemsMimeType),
                                                    treeDelegate.kind,
                                                    treeDelegate.objectId,
                                                    modelData < 0)
                                        if (changed)
                                            drop.acceptProposedAction()
                                        return
                                    }
                                    projectTree.moveBrowserItemsFromDrop(
                                                drop, treeDelegate.kind,
                                                treeDelegate.objectId)
                                }
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: treeDelegate.indentation
                            anchors.top: treeDelegate.browserInsertionEdge < 0
                                         ? parent.top : undefined
                            anchors.bottom: treeDelegate.browserInsertionEdge > 0
                                            ? parent.bottom : undefined
                            height: 2
                            z: 5
                            visible: treeDelegate.browserInsertionEdge !== 0
                            color: uiTheme.accent
                        }

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: function(mouse) {
                                if (treeDelegate.kind === "root"
                                        && treeDelegate.objectId !== "model")
                                    return
                                const itemIndex =
                                        projectController.treeModel.indexForObject(
                                            treeDelegate.objectId,
                                            treeDelegate.kind)
                                if ((treeDelegate.kind === "element"
                                     || treeDelegate.kind === "folder"
                                     || treeDelegate.kind === "relationship")
                                        && !projectTreeSelection.isSelected(
                                            itemIndex)) {
                                    projectTreeSelection.select(
                                                itemIndex,
                                                ItemSelectionModel.ClearAndSelect
                                                | ItemSelectionModel.Rows)
                                    projectTreeSelection.setCurrentIndex(
                                                itemIndex,
                                                ItemSelectionModel.NoUpdate)
                                } else if (treeDelegate.kind === "diagram") {
                                    projectTreeSelection.select(
                                                itemIndex,
                                                ItemSelectionModel.ClearAndSelect
                                                | ItemSelectionModel.Rows)
                                } else if (treeDelegate.kind === "root") {
                                    projectTreeSelection.clearSelection()
                                }
                                treeContextMenu.targetId = treeDelegate.objectId
                                treeContextMenu.targetKind = treeDelegate.kind
                                treeContextMenu.targetType = treeDelegate.objectType
                                treeContextMenu.targetName = treeDelegate.name
                                treeContextMenu.targetStyleId =
                                        projectController.explicitStyleIdForBrowserSubject(
                                            treeDelegate.kind,
                                            treeDelegate.objectId)
                                treeContextMenu.selectedItemsJson =
                                        projectTree.selectedBrowserItemsJson()
                                treeContextMenu.selectedItemCount =
                                        JSON.parse(
                                            treeContextMenu.selectedItemsJson).length
                                const point = treeDelegate.mapToItem(
                                                root.contentItem,
                                                mouse.x, mouse.y)
                                treeContextMenu.x = point.x
                                treeContextMenu.y = point.y
                                treeContextMenu.open()
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
                        text: qsTr("Selected item properties")
                        font.bold: true
                        background: Rectangle { color: uiTheme.panelHeader }
                    }
                    Label {
                        Layout.leftMargin: 10
                        text: projectController.selectedKind.length > 0
                              ? projectController.selectedType : qsTr("Nothing selected")
                        color: uiTheme.mutedText
                    }
                    CatalogButton {
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        catalogId: "source.open"
                        text: qsTr("Open in VS Code")
                        visible: projectController.selectedKind === "element"
                                 || projectController.selectedKind
                                    === "relationship"
                        enabled: sourceEditorController.canOpenObject(
                                     projectController.selectedKind,
                                     projectController.selectedId)
                        onClicked: sourceEditorController.openObject(
                                       projectController.selectedKind,
                                       projectController.selectedId)
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
                    Label {
                        Layout.leftMargin: 10
                        text: qsTr("Stereotypes")
                        visible: projectController.selectedKind === "element"
                                 || projectController.selectedKind
                                    === "relationship"
                    }
                    StereotypeDropdown {
                        id: propertyStereotypeDropdown
                        objectName: "propertyStereotypeDropdown"
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        visible: projectController.selectedKind === "element"
                                 || projectController.selectedKind
                                    === "relationship"
                        targetKind: projectController.selectedKind
                        targetId: projectController.selectedId
                        onManageRequested:
                            projectStereotypeDialog.openManager()
                    }
                    Label {
                        Layout.leftMargin: 10
                        text: qsTr("Source end")
                        font.bold: true
                        visible: projectController.selectedKind === "relationship"
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6
                        visible: projectController.selectedKind === "relationship"

                        Label { text: qsTr("Role") }
                        TextField {
                            id: sourceRoleEditor
                            Layout.fillWidth: true
                            placeholderText: qsTr("Optional")
                            text: projectController.selectedSourceRole
                            onEditingFinished: {
                                if (text !== projectController.selectedSourceRole)
                                    projectController.selectedSourceRole = text
                            }
                        }
                        Label { text: qsTr("Multiplicity") }
                        TextField {
                            id: sourceMultiplicityEditor
                            Layout.fillWidth: true
                            placeholderText: qsTr("For example: 0..*")
                            text: projectController.selectedSourceMultiplicity
                            onEditingFinished: {
                                if (text !== projectController.selectedSourceMultiplicity)
                                    projectController.selectedSourceMultiplicity = text
                            }
                        }
                    }
                    Label {
                        Layout.leftMargin: 10
                        text: qsTr("Target end")
                        font.bold: true
                        visible: projectController.selectedKind === "relationship"
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6
                        visible: projectController.selectedKind === "relationship"

                        Label { text: qsTr("Role") }
                        TextField {
                            id: targetRoleEditor
                            Layout.fillWidth: true
                            placeholderText: qsTr("Optional")
                            text: projectController.selectedTargetRole
                            onEditingFinished: {
                                if (text !== projectController.selectedTargetRole)
                                    projectController.selectedTargetRole = text
                            }
                        }
                        Label { text: qsTr("Multiplicity") }
                        TextField {
                            id: targetMultiplicityEditor
                            Layout.fillWidth: true
                            placeholderText: qsTr("For example: 1")
                            text: projectController.selectedTargetMultiplicity
                            onEditingFinished: {
                                if (text !== projectController.selectedTargetMultiplicity)
                                    projectController.selectedTargetMultiplicity = text
                            }
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
                        background: Rectangle { color: uiTheme.surface; border.color: uiTheme.controlBorder; radius: 3 }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Layout.rightMargin: 10
                        visible: attributesEditor.visible
                        CatalogButton {
                            catalogId: "edit.applyProperty"
                            text: qsTr("Apply")
                            enabled: attributesEditor.text !== projectController.selectedAttributes
                            onClicked: projectController.selectedAttributes = attributesEditor.text
                        }
                        CatalogButton {
                            catalogId: "edit.revertProperty"
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
                        background: Rectangle { color: uiTheme.surface; border.color: uiTheme.controlBorder; radius: 3 }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Layout.rightMargin: 10
                        visible: operationsEditor.visible
                        CatalogButton {
                            catalogId: "edit.applyProperty"
                            text: qsTr("Apply")
                            enabled: operationsEditor.text !== projectController.selectedOperations
                            onClicked: projectController.selectedOperations = operationsEditor.text
                        }
                        CatalogButton {
                            catalogId: "edit.revertProperty"
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
                        background: Rectangle { color: uiTheme.surface; border.color: uiTheme.controlBorder; radius: 3 }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Layout.rightMargin: 10
                        visible: literalsEditor.visible
                        CatalogButton {
                            catalogId: "edit.applyProperty"
                            text: qsTr("Apply")
                            enabled: literalsEditor.text !== projectController.selectedLiterals
                            onClicked: projectController.selectedLiterals = literalsEditor.text
                        }
                        CatalogButton {
                            catalogId: "edit.revertProperty"
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
                            if (!sourceRoleEditor.activeFocus)
                                sourceRoleEditor.text = projectController.selectedSourceRole
                            if (!sourceMultiplicityEditor.activeFocus)
                                sourceMultiplicityEditor.text = projectController.selectedSourceMultiplicity
                            if (!targetRoleEditor.activeFocus)
                                targetRoleEditor.text = projectController.selectedTargetRole
                            if (!targetMultiplicityEditor.activeFocus)
                                targetMultiplicityEditor.text = projectController.selectedTargetMultiplicity
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.minimumHeight: 20 }
                }
            }
        }
    }

    Popup {
        id: projectTreeFindPopup
        objectName: "projectTreeFindPopup"
        parent: root.contentItem
        modal: false
        focus: true
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property var matches: []
        property int currentMatchIndex: -1
        readonly property string matchStatus: {
            if (projectTreeFindInput.text.trim().length === 0)
                return qsTr("Type to find")
            if (matches.length === 0)
                return qsTr("No matches")
            return qsTr("%1 of %2").arg(currentMatchIndex + 1)
                                     .arg(matches.length)
        }

        function openForFind() {
            root.leftPanelVisible = true
            Qt.callLater(function() {
                const point = projectTreeFilterRow.mapToItem(
                                root.contentItem, 0,
                                projectTreeFilterRow.height)
                projectTreeFindPopup.x = point.x
                projectTreeFindPopup.y = point.y
                projectTreeFindPopup.width = Math.max(
                            340, projectTreeFilterRow.width)
                if (!projectTreeFindPopup.opened)
                    projectTreeFindPopup.open()
                projectTreeFindPopup.refreshMatches(true)
                projectTreeFindInput.forceActiveFocus()
                projectTreeFindInput.selectAll()
            })
        }

        function refreshMatches(preserveCurrent) {
            let previousId = ""
            let previousKind = ""
            if (preserveCurrent && currentMatchIndex >= 0
                    && currentMatchIndex < matches.length) {
                previousId = matches[currentMatchIndex].objectId
                previousKind = matches[currentMatchIndex].kind
            }

            const updatedMatches = projectController.treeModel.findMatches(
                                     projectTreeFindInput.text)
            matches = updatedMatches
            currentMatchIndex = -1
            if (matches.length === 0)
                return
            if (previousId.length > 0) {
                for (let index = 0; index < matches.length; ++index) {
                    if (matches[index].objectId === previousId
                            && matches[index].kind === previousKind) {
                        currentMatchIndex = index
                        break
                    }
                }
            }
            if (currentMatchIndex < 0)
                currentMatchIndex = 0
            selectCurrentMatch()
        }

        function advance(delta) {
            if (matches.length === 0) {
                refreshMatches(false)
                if (matches.length === 0)
                    return
            }
            currentMatchIndex = (currentMatchIndex + delta + matches.length)
                              % matches.length
            selectCurrentMatch()
        }

        function selectCurrentMatch() {
            if (currentMatchIndex < 0 || currentMatchIndex >= matches.length)
                return
            const match = matches[currentMatchIndex]
            const expectedId = match.objectId
            const expectedKind = match.kind
            const itemIndex = projectController.treeModel.indexForObject(
                                expectedId, expectedKind)
            if (!itemIndex.valid)
                return
            projectTree.expandToIndex(itemIndex)
            Qt.callLater(function() {
                if (projectTreeFindPopup.currentMatchIndex < 0
                        || projectTreeFindPopup.currentMatchIndex
                           >= projectTreeFindPopup.matches.length)
                    return
                const current = projectTreeFindPopup.matches[
                                projectTreeFindPopup.currentMatchIndex]
                if (current.objectId !== expectedId
                        || current.kind !== expectedKind)
                    return
                const currentIndex = projectController.treeModel.indexForObject(
                                     expectedId, expectedKind)
                if (!currentIndex.valid)
                    return
                projectTreeSelection.select(
                            currentIndex,
                            ItemSelectionModel.ClearAndSelect
                            | ItemSelectionModel.Rows)
                projectTreeSelection.setCurrentIndex(
                            currentIndex, ItemSelectionModel.NoUpdate)
                projectTree.selectionOriginatesFromTree = true
                if (expectedKind === "diagram")
                    workspaceController.activeDiagramId = expectedId
                if (expectedKind === "element"
                        || expectedKind === "diagram"
                        || expectedKind === "relationship")
                    projectController.selectObject(expectedId, expectedKind)
                else
                    projectController.clearSelection()
                Qt.callLater(function() {
                    projectTree.selectionOriginatesFromTree = false
                })
                const row = projectTree.rowAtIndex(currentIndex)
                if (row >= 0)
                    projectTree.positionViewAtRow(row, TableView.Contain)
            })
        }

        background: Rectangle {
            color: uiTheme.surface
            border.color: uiTheme.overlayBorder
            radius: 3
        }

        contentItem: RowLayout {
            spacing: 4
            TextField {
                id: projectTreeFindInput
                objectName: "projectTreeFindInput"
                Layout.fillWidth: true
                placeholderText: qsTr("Find (* and ? wildcards)")
                Accessible.name: qsTr("Find in project tree")
                onTextEdited: projectTreeFindPopup.refreshMatches(true)
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter) {
                        projectTreeFindPopup.advance(
                                    event.modifiers & Qt.ShiftModifier ? -1 : 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        projectTreeFindPopup.close()
                        event.accepted = true
                    }
                }
            }
            Label {
                text: projectTreeFindPopup.matchStatus
                color: uiTheme.mutedText
                Layout.minimumWidth: 70
                horizontalAlignment: Text.AlignHCenter
            }
            ToolButton {
                text: "↑"
                enabled: projectTreeFindPopup.matches.length > 0
                Accessible.name: qsTr("Previous match")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Previous match (Shift+F3)")
                onClicked: projectTreeFindPopup.advance(-1)
            }
            ToolButton {
                text: "↓"
                enabled: projectTreeFindPopup.matches.length > 0
                Accessible.name: qsTr("Next match")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Next match (F3)")
                onClicked: projectTreeFindPopup.advance(1)
            }
            ToolButton {
                text: "×"
                Accessible.name: qsTr("Close find")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Close find (Esc)")
                onClicked: projectTreeFindPopup.close()
            }
        }
    }

    Menu {
        id: treeContextMenu
        property string targetId: ""
        property string targetKind: ""
        property string targetType: ""
        property string targetName: ""
        property string targetStyleId: ""
        property string selectedItemsJson: "[]"
        property int selectedItemCount: 0

        CatalogMenuItem {
            catalogId: "source.open"
            visible: treeContextMenu.selectedItemCount === 1
                     && (treeContextMenu.targetKind === "element"
                         || treeContextMenu.targetKind === "relationship")
            height: visible ? implicitHeight : 0
            text: qsTr("Open in VS Code")
            enabled: sourceEditorController.canOpenObject(
                         treeContextMenu.targetKind,
                         treeContextMenu.targetId)
            onTriggered: sourceEditorController.openObject(
                             treeContextMenu.targetKind,
                             treeContextMenu.targetId)
        }
        MenuSeparator {
            visible: treeContextMenu.selectedItemCount === 1
                     && (treeContextMenu.targetKind === "element"
                         || treeContextMenu.targetKind === "relationship")
            height: visible ? implicitHeight : 0
        }
        CatalogMenuItem {
            catalogId: "browser.createFolder"
            visible: treeContextMenu.targetKind === "folder"
                     || treeContextMenu.targetKind === "element"
                     || (treeContextMenu.targetKind === "root"
                         && treeContextMenu.targetId === "model")
            height: visible ? implicitHeight : 0
            text: qsTr("New folder here…")
            onTriggered: root.createFolderAt(
                             treeContextMenu.targetKind === "root"
                             ? "model" : treeContextMenu.targetKind,
                             treeContextMenu.targetKind === "root"
                             ? "" : treeContextMenu.targetId)
        }
        MenuSeparator {
            visible: treeContextMenu.targetKind === "folder"
                     || treeContextMenu.targetKind === "element"
                     || treeContextMenu.targetKind === "diagram"
            height: visible ? implicitHeight : 0
        }
        CatalogMenuItem {
            catalogId: "browser.addEmptyNamespace"
            visible: treeContextMenu.targetKind === "element"
                     && treeContextMenu.targetType === "package"
            height: visible ? implicitHeight : 0
            text: qsTr("Add empty namespace to active diagram")
            enabled: workspaceController.activeDiagramId.length > 0
            onTriggered: projectController.addEmptyPackageToDiagram(
                             workspaceController.activeDiagramId,
                             treeContextMenu.targetId)
        }
        CatalogMenuItem {
            catalogId: "browser.renameFolder"
            visible: treeContextMenu.targetKind === "folder"
            height: visible ? implicitHeight : 0
            text: qsTr("Rename folder…")
            onTriggered: root.renameFolder(treeContextMenu.targetId,
                                           treeContextMenu.targetName)
        }
        StyleAssignmentMenu {
            visible: treeContextMenu.targetKind === "namespace"
                     || treeContextMenu.targetKind === "folder"
                     || treeContextMenu.targetKind === "element"
            assignedStyleId: treeContextMenu.targetStyleId
            onStyleChosen: function(styleId) {
                projectController.assignStyleToBrowserSubject(
                            treeContextMenu.targetKind,
                            treeContextMenu.targetId,
                            styleId)
            }
            onManageRequested: browserStyleDialog.openFor(
                                   treeContextMenu.targetStyleId)
        }
        MenuItem {
            visible: treeContextMenu.targetKind === "element"
            height: visible ? implicitHeight : 0
            text: qsTr("Stereotypes…")
            onTriggered: {
                const menuX = treeContextMenu.x
                const menuY = treeContextMenu.y
                const objectId = treeContextMenu.targetId
                // Let the context menu finish closing before opening the
                // checkable dropdown in the same popup stack.
                Qt.callLater(function() {
                    treeStereotypeDropdown.openAt(
                                root.contentItem, menuX, menuY,
                                "element", objectId)
                })
            }
        }
        CatalogMenuItem {
            catalogId: "browser.deleteSelection"
            visible: treeContextMenu.selectedItemCount > 0
            height: visible ? implicitHeight : 0
            text: treeContextMenu.selectedItemCount === 1
                  ? (treeContextMenu.targetKind === "relationship"
                     ? qsTr("Delete relationship from model")
                     : qsTr("Delete"))
                  : qsTr("Delete selected (%1)").arg(
                        treeContextMenu.selectedItemCount)
            onTriggered: projectController.deleteBrowserItems(
                             treeContextMenu.selectedItemsJson)
        }
        MenuSeparator {
            visible: treeContextMenu.selectedItemCount === 1
                     && (treeContextMenu.targetKind === "folder"
                         || treeContextMenu.targetKind === "element")
            height: visible ? implicitHeight : 0
        }
        CatalogMenuItem {
            catalogId: "browser.moveUp"
            visible: treeContextMenu.selectedItemCount === 1
                     && (treeContextMenu.targetKind === "folder"
                         || treeContextMenu.targetKind === "element")
            height: visible ? implicitHeight : 0
            text: qsTr("Move up")
            enabled: projectController.canReorderBrowserItem(
                         treeContextMenu.targetKind,
                         treeContextMenu.targetId, -1)
            onTriggered: projectController.reorderBrowserItem(
                             treeContextMenu.targetKind,
                             treeContextMenu.targetId, -1)
        }
        CatalogMenuItem {
            catalogId: "browser.moveDown"
            visible: treeContextMenu.selectedItemCount === 1
                     && (treeContextMenu.targetKind === "folder"
                         || treeContextMenu.targetKind === "element")
            height: visible ? implicitHeight : 0
            text: qsTr("Move down")
            enabled: projectController.canReorderBrowserItem(
                         treeContextMenu.targetKind,
                         treeContextMenu.targetId, 1)
            onTriggered: projectController.reorderBrowserItem(
                             treeContextMenu.targetKind,
                             treeContextMenu.targetId, 1)
        }
    }

    ProjectStyleDialog {
        id: browserStyleDialog
        objectName: "browserStyleDialog"
        onStyleChosen: function(styleId) {
            projectController.assignStyleToBrowserSubject(
                        treeContextMenu.targetKind,
                        treeContextMenu.targetId,
                        styleId)
        }
    }

    ProjectStereotypeDialog {
        id: projectStereotypeDialog
        objectName: "projectStereotypeDialog"
    }

    StereotypeDropdown {
        id: treeStereotypeDropdown
        objectName: "treeStereotypeDropdown"
        showField: false
        onManageRequested: projectStereotypeDialog.openManager()
    }

    CatalogDialog {
        id: folderNameDialog
        objectName: "folderNameDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        title: mode === "rename" ? qsTr("Rename folder")
                                 : qsTr("New project-tree folder")
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string mode: "create"
        property string folderId: ""
        property string parentKind: "model"
        property string parentId: ""
        property string currentName: ""

        onOpened: {
            folderNameInput.text = mode === "rename" ? currentName
                                                      : qsTr("New Folder")
            folderNameInput.forceActiveFocus()
            folderNameInput.selectAll()
            const okButton = standardButton(Dialog.Ok)
            if (okButton)
                okButton.enabled = folderNameInput.text.trim().length > 0
        }
        onAccepted: {
            if (mode === "rename")
                projectController.renameBrowserFolder(folderId,
                                                      folderNameInput.text)
            else
                projectController.addBrowserFolder(parentKind, parentId,
                                                   folderNameInput.text)
        }

        contentItem: TextField {
            id: folderNameInput
            implicitWidth: 320
            selectByMouse: true
            onAccepted: {
                if (text.trim().length > 0)
                    folderNameDialog.accept()
            }
            onTextChanged: {
                const okButton = folderNameDialog.standardButton(Dialog.Ok)
                if (okButton)
                    okButton.enabled = text.trim().length > 0
            }
        }
    }

    CatalogDialog {
        id: packageMoveConfirmation
        objectName: "packageMoveConfirmation"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 40)
        modal: true
        focus: true
        title: qsTr("Change semantic containment?")
        standardButtons: Dialog.Yes | Dialog.No
        property string itemsJson: ""
        property string targetKind: ""
        property string targetId: ""
        property string message: ""

        onAccepted: projectController.moveBrowserItemsWithSemanticReassignment(
                        itemsJson, targetKind, targetId)

        contentItem: Label {
            text: packageMoveConfirmation.message
            wrapMode: Text.Wrap
            padding: 16
        }
    }

    Connections {
        target: updateController

        function onCheckFinished(userInitiated, updateAvailable, failed,
                                 message) {
            if (updateAvailable) {
                updateAvailableDialog.open()
            } else if (userInitiated) {
                root.updateResultFailed = failed
                root.updateResultMessage = message
                updateCheckResultDialog.open()
            }
        }
    }

    CatalogDialog {
        id: updateAvailableDialog
        objectName: "updateAvailableDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(500, parent.width - 40)
        modal: true
        focus: true
        title: qsTr("Update available")
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            const updateButton = standardButton(Dialog.Ok)
            if (updateButton)
                updateButton.text = updateController.maintenanceToolAvailable
                                  ? qsTr("Update now")
                                  : qsTr("Open download page")
            const laterButton = standardButton(Dialog.Cancel)
            if (laterButton)
                laterButton.text = qsTr("Later")
        }
        onAccepted: root.requestUpdateInstall()

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("yauml %1 is available. You are currently using %2.")
                      .arg(updateController.availableVersion)
                      .arg(updateController.currentVersion)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: uiTheme.mutedText
                text: updateController.maintenanceToolAvailable
                      ? qsTr("Your project will follow the normal save prompt before yauml closes and starts the maintenance tool.")
                      : qsTr("This portable copy has no maintenance tool. The release page will open so you can download the latest installer.")
            }
            Label {
                Layout.fillWidth: true
                textFormat: Text.RichText
                text: "<a href=\"" + updateController.releaseNotesUrl
                      + "\">" + qsTr("View release notes") + "</a>"
                onLinkActivated: function(link) {
                    Qt.openUrlExternally(link)
                }
            }
        }
    }

    CatalogDialog {
        id: updateCheckResultDialog
        objectName: "updateCheckResultDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 40)
        modal: true
        focus: true
        title: root.updateResultFailed
               ? qsTr("Update check failed")
               : qsTr("No updates available")
        standardButtons: Dialog.Ok

        contentItem: Label {
            padding: 16
            wrapMode: Text.Wrap
            text: root.updateResultMessage
        }
    }

    CatalogDialog {
        id: preferencesDialog
        objectName: "preferencesDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(720, parent.width - 40)
        height: Math.min(680, parent.height - 40)
        modal: true
        focus: true
        title: qsTr("Preferences")
        standardButtons: Dialog.Ok | Dialog.Cancel
        property bool connectorGestureKeysValid: {
            const values = [dependencyGestureKey.text, realizationGestureKey.text,
                            generalizationGestureKey.text, associationGestureKey.text,
                            aggregationGestureKey.text, compositionGestureKey.text,
                            containmentGestureKey.text]
            const assigned = {}
            for (let index = 0; index < values.length; ++index) {
                const key = values[index].trim().toUpperCase()
                if (!/^[A-Z0-9]$/.test(key) || assigned[key])
                    return false
                assigned[key] = true
            }
            return true
        }
        property bool cppInterfacePatternValid:
            applicationSettings.isValidCppInterfacePattern(cppInterfacePattern.text)
        property int cppMemberTypeRulesRevision: 0
        property bool cppMemberTypeRulesValid: {
            // Referencing the revision makes this binding react to edits in
            // ListModel roles, not only to rows being added or removed.
            const revision = cppMemberTypeRulesRevision
            const seen = {}
            for (let index = 0; index < cppMemberTypeRulesModel.count; ++index) {
                const rule = cppMemberTypeRulesModel.get(index)
                let typeName = rule.typeName.replace(/\s+/g, "")
                while (typeName.startsWith("::"))
                    typeName = typeName.substring(2)
                const templateStart = typeName.indexOf("<")
                if (templateStart >= 0)
                    typeName = typeName.substring(0, templateStart)
                if (typeName.length === 0 || seen[typeName])
                    return false
                seen[typeName] = true
            }
            return true
        }
        readonly property var contextToolboxKinds: [
            { id: "relationship", label: qsTr("Relationship creation") },
            { id: "selection", label: qsTr("Multiple selection") },
            { id: "connector", label: qsTr("Selected connector") },
            { id: "presentation", label: qsTr("Selected element or container") }
        ]
        readonly property var contextToolboxActionLabels: ({
            "createRelationship.dependency": qsTr("Create dependency"),
            "createRelationship.realization": qsTr("Create realization / implementation"),
            "createRelationship.generalization": qsTr("Create generalization / inheritance"),
            "createRelationship.association": qsTr("Create navigable association"),
            "createRelationship.aggregation": qsTr("Create aggregation"),
            "createRelationship.composition": qsTr("Create composition"),
            "createRelationship.containment": qsTr("Create containment / nesting"),
            "arrange.autoLayoutSelection": qsTr("Auto arrange selection"),
            "arrange.alignLeft": qsTr("Align left edges"),
            "arrange.alignHorizontalCenters": qsTr("Align horizontal centers"),
            "arrange.alignRight": qsTr("Align right edges"),
            "arrange.alignTop": qsTr("Align top edges"),
            "arrange.alignVerticalCenters": qsTr("Align vertical centers"),
            "arrange.alignBottom": qsTr("Align bottom edges"),
            "arrange.matchWidth": qsTr("Make same width"),
            "arrange.matchHeight": qsTr("Make same height"),
            "arrange.matchSize": qsTr("Make same size"),
            "arrange.distributeHorizontally": qsTr("Distribute horizontally"),
            "arrange.distributeVertically": qsTr("Distribute vertically"),
            "arrange.fitToContent": qsTr("Fit to content"),
            "style.assignNamed": qsTr("Choose named style"),
            "connector.routeStraight": qsTr("Use straight routing"),
            "connector.routeOrthogonal": qsTr("Use orthogonal routing"),
            "connector.optimizeEndsAndRoute": qsTr("Optimize connectors"),
            "connector.editName": qsTr("Edit relationship name"),
            "connector.editSourceRole": qsTr("Edit source role"),
            "connector.editSourceMultiplicity": qsTr("Edit source cardinality"),
            "connector.editTargetRole": qsTr("Edit target role"),
            "connector.editTargetMultiplicity": qsTr("Edit target cardinality"),
            "connector.editStereotypes": qsTr("Choose relationship stereotypes"),
            "connector.resetAnnotationPositions": qsTr("Reset annotation positions"),
            "presentation.editName": qsTr("Edit name"),
            "source.open": qsTr("Open in VS Code"),
            "presentation.optimizeInternalConnectorEndsAndRoute": qsTr("Optimize connectors"),
            "presentation.attributesVisibility": qsTr("Cycle attributes visibility"),
            "presentation.operationsVisibility": qsTr("Cycle operations visibility"),
            "presentation.operationSignatureMode": qsTr("Cycle operation signature detail"),
            "presentation.connectorSnapPoints": qsTr("Connector snap points"),
            "presentation.addIncomingRelatedTypes": qsTr("Add types that depend on this"),
            "presentation.addOutgoingRelatedTypes": qsTr("Add types this depends on"),
            "presentation.wrapInNamespace": qsTr("Wrap in parent namespace")
        })

        function contextToolboxModel(index) {
            if (index === 0)
                return relationshipToolboxPreferencesModel
            if (index === 1)
                return selectionToolboxPreferencesModel
            if (index === 2)
                return connectorToolboxPreferencesModel
            return presentationToolboxPreferencesModel
        }

        function populateContextToolboxModel(model, entries) {
            model.clear()
            for (let index = 0; index < entries.length; ++index) {
                const entry = entries[index]
                model.append({
                    actionId: entry.actionId,
                    actionLabel: contextToolboxActionLabels[entry.actionId]
                                 || entry.actionId,
                    actionEnabled: entry.enabled
                })
            }
        }

        function loadContextToolboxConfiguration(configuration) {
            for (let index = 0; index < contextToolboxKinds.length; ++index) {
                const toolboxId = contextToolboxKinds[index].id
                populateContextToolboxModel(
                            contextToolboxModel(index),
                            configuration[toolboxId] || [])
            }
        }

        function loadCppMemberTypeRules(rules) {
            cppMemberTypeRulesModel.clear()
            for (let index = 0; index < rules.length; ++index) {
                const rule = rules[index]
                cppMemberTypeRulesModel.append({
                    typeName: rule.typeName,
                    relationshipType: rule.relationshipType,
                    multiplicity: rule.multiplicity,
                    targetArgument: rule.targetArgument
                })
            }
            ++cppMemberTypeRulesRevision
        }

        function stagedContextToolboxConfiguration() {
            const configuration = {}
            for (let groupIndex = 0;
                 groupIndex < contextToolboxKinds.length; ++groupIndex) {
                const entries = []
                const model = contextToolboxModel(groupIndex)
                for (let index = 0; index < model.count; ++index) {
                    const entry = model.get(index)
                    entries.push({
                        actionId: entry.actionId,
                        enabled: entry.actionEnabled
                    })
                }
                configuration[contextToolboxKinds[groupIndex].id] = entries
            }
            return configuration
        }

        function resetCurrentContextToolbox() {
            const defaults = applicationSettings.contextToolboxDefaults()
            const toolboxId =
                    contextToolboxKinds[contextToolboxKind.currentIndex].id
            populateContextToolboxModel(
                        contextToolboxModel(contextToolboxKind.currentIndex),
                        defaults[toolboxId] || [])
        }

        function updateOkButton() {
            const button = standardButton(Dialog.Ok)
            if (button)
                button.enabled = connectorGestureKeysValid
                                 && cppInterfacePatternValid
                                 && cppMemberTypeRulesValid
        }

        onConnectorGestureKeysValidChanged: updateOkButton()
        onCppInterfacePatternValidChanged: updateOkButton()
        onCppMemberTypeRulesValidChanged: updateOkButton()

        onOpened: {
            distributionGap.value = applicationSettings.defaultDistributionGap
            snapToGrid.checked = applicationSettings.snapToGridEnabled
            alignmentGuides.checked = applicationSettings.alignmentGuidesEnabled
            gridSpacing.value = applicationSettings.gridSpacing
            diagramItemSizing.currentIndex =
                    applicationSettings.diagramItemSizingMode === "fixed"
                    ? 0 : 1
            packageReassignment.currentIndex =
                    applicationSettings.packageReassignmentPolicy === "disallow"
                    ? 0
                    : applicationSettings.packageReassignmentPolicy === "allow"
                      ? 2 : 1
            automaticUpdateChecks.checked =
                    applicationSettings.automaticUpdateChecksEnabled
            sourceEditorCommand.text =
                    applicationSettings.sourceEditorCommand
            sourceEditorDoubleClick.checked =
                    applicationSettings.sourceEditorDoubleClickEnabled
            defaultConnectorRouting.currentIndex =
                    applicationSettings.defaultConnectorRouting === "orthogonal" ? 1 : 0
            const gestureKeys = applicationSettings.relationshipGestureKeys
            dependencyGestureKey.text = gestureKeys.dependency
            realizationGestureKey.text = gestureKeys.realization
            generalizationGestureKey.text = gestureKeys.generalization
            associationGestureKey.text = gestureKeys.association
            aggregationGestureKey.text = gestureKeys.aggregation
            compositionGestureKey.text = gestureKeys.composition
            containmentGestureKey.text = gestureKeys.containment
            cppInterfacePattern.text = applicationSettings.cppInterfacePattern
            loadCppMemberTypeRules(applicationSettings.cppMemberTypeRules)
            loadContextToolboxConfiguration(
                        applicationSettings.contextToolboxConfiguration)
            Qt.callLater(updateOkButton)
            colorPreferencesModel.clear()
            const roles = uiTheme.colorRoles
            for (let index = 0; index < roles.length; ++index) {
                const role = roles[index]
                colorPreferencesModel.append({
                    roleKey: role.key,
                    displayName: role.label,
                    groupName: role.group,
                    themeColor: uiTheme.color(role.key)
                })
            }
        }
        onAccepted: {
            applicationSettings.setRelationshipGestureKeys({
                dependency: dependencyGestureKey.text,
                realization: realizationGestureKey.text,
                generalization: generalizationGestureKey.text,
                association: associationGestureKey.text,
                aggregation: aggregationGestureKey.text,
                composition: compositionGestureKey.text,
                containment: containmentGestureKey.text
            })
            applicationSettings.defaultDistributionGap = distributionGap.value
            applicationSettings.snapToGridEnabled = snapToGrid.checked
            applicationSettings.alignmentGuidesEnabled = alignmentGuides.checked
            applicationSettings.gridSpacing = gridSpacing.value
            applicationSettings.diagramItemSizingMode =
                    diagramItemSizing.currentIndex === 0 ? "fixed" : "content"
            applicationSettings.packageReassignmentPolicy =
                    packageReassignment.currentIndex === 0
                    ? "disallow"
                    : packageReassignment.currentIndex === 2 ? "allow" : "ask"
            applicationSettings.automaticUpdateChecksEnabled =
                    automaticUpdateChecks.checked
            applicationSettings.sourceEditorCommand =
                    sourceEditorCommand.text
            applicationSettings.sourceEditorDoubleClickEnabled =
                    sourceEditorDoubleClick.checked
            applicationSettings.defaultConnectorRouting =
                    defaultConnectorRouting.currentIndex === 1
                    ? "orthogonal" : "straight"
            applicationSettings.setCppInterfacePattern(cppInterfacePattern.text)
            const memberRules = []
            for (let index = 0; index < cppMemberTypeRulesModel.count; ++index) {
                const rule = cppMemberTypeRulesModel.get(index)
                if (rule.typeName.trim().length === 0)
                    continue
                memberRules.push({
                    typeName: rule.typeName,
                    relationshipType: rule.relationshipType,
                    multiplicity: rule.multiplicity,
                    targetArgument: rule.targetArgument
                })
            }
            applicationSettings.setCppMemberTypeRules(memberRules)
            applicationSettings.setContextToolboxConfiguration(
                        stagedContextToolboxConfiguration())
            const colors = {}
            for (let index = 0; index < colorPreferencesModel.count; ++index) {
                const entry = colorPreferencesModel.get(index)
                colors[entry.roleKey] = entry.themeColor
            }
            uiTheme.setColors(colors)
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            TabBar {
                id: preferencesTabs
                objectName: "preferencesTabs"
                Layout.fillWidth: true
                TabButton { text: qsTr("General") }
                TabButton { text: qsTr("Connectors") }
                TabButton { text: qsTr("Toolboxes") }
                TabButton { text: qsTr("C++ Relationships") }
                TabButton { text: qsTr("Colors") }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: preferencesTabs.currentIndex

                ScrollView {
                    id: generalPreferencesScroll
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: generalPreferencesScroll.availableWidth
                        spacing: 12

                        Label {
                            text: qsTr("Diagram arrangement")
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("When distributed elements have no positive gap, this spacing is used between their edges.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Default distribution gap") }
                            Item { Layout.fillWidth: true }
                            SpinBox {
                                id: distributionGap
                                from: 1
                                to: 1000
                                editable: true
                            }
                            Label { text: qsTr("px") }
                        }
                        Label {
                            text: qsTr("Diagram snapping")
                            font.bold: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Diagram item sizing") }
                            Item { Layout.fillWidth: true }
                            ComboBox {
                                id: diagramItemSizing
                                model: [qsTr("Fixed size (220 × 120)"),
                                        qsTr("Fit to content")]
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: uiTheme.mutedText
                            text: qsTr("Controls the initial size of items added from the project tree. “Fit to content” remains available from the diagram context menu.")
                        }
                        CheckBox {
                            id: snapToGrid
                            text: qsTr("Snap element positions to the grid")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            enabled: snapToGrid.checked
                            Label { text: qsTr("Grid spacing") }
                            Item { Layout.fillWidth: true }
                            SpinBox {
                                id: gridSpacing
                                from: 5
                                to: 200
                                editable: true
                            }
                            Label { text: qsTr("px") }
                        }
                        CheckBox {
                            id: alignmentGuides
                            text: qsTr("Snap to element edges and centers and show guides")
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: uiTheme.mutedText
                            text: qsTr("Hold Alt while dragging to temporarily disable snapping.")
                        }
                        Label {
                            text: qsTr("Model organization")
                            font.bold: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("Project-tree containment changes by drag and drop")
                            }
                            Item { Layout.fillWidth: true }
                            ComboBox {
                                id: packageReassignment
                                model: [qsTr("Disallow"), qsTr("Ask"), qsTr("Allow")]
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: uiTheme.mutedText
                            text: qsTr("Controls whether dragging in the project tree changes a type's UML package or enclosing type. Diagram dragging is always presentation-only.")
                        }
                        Label {
                            text: qsTr("Software updates")
                            font.bold: true
                        }
                        CheckBox {
                            id: automaticUpdateChecks
                            text: qsTr("Automatically check for stable updates")
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: uiTheme.mutedText
                            text: qsTr("Installed builds check at most once per day. Updates are never installed without confirmation.")
                        }
                        Label {
                            text: qsTr("Source editor")
                            font.bold: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("VS Code command") }
                            TextField {
                                id: sourceEditorCommand
                                Layout.fillWidth: true
                                placeholderText: "code"
                                Accessible.name: qsTr("VS Code command")
                            }
                        }
                        CheckBox {
                            id: sourceEditorDoubleClick
                            text: qsTr("Open source when a type or operation is double-clicked on a diagram")
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: uiTheme.mutedText
                            text: qsTr("F12 and “Open in VS Code” are always available for imported items. Enter “code” when VS Code is on PATH, or specify Code.exe/code.cmd and optional arguments.")
                        }
                        Item { Layout.preferredHeight: 8 }
                    }
                }

                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 12

                        Label {
                            text: qsTr("Connector creation")
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Choose the route used when a new relationship is created. Existing relationships keep their own route setting.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Default connector shape") }
                            Item { Layout.fillWidth: true }
                            ComboBox {
                                id: defaultConnectorRouting
                                model: [qsTr("Straight"), qsTr("Orthogonal")]
                            }
                        }
                        Label {
                            text: qsTr("Edge-drag relationship keys")
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Hold the pointer button on an element edge, press the relationship key, then drag to a target element. Each assignment must be unique.")
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 6

                            Label { text: qsTr("Dependency") }
                            TextField {
                                id: dependencyGestureKey
                                Layout.preferredWidth: 64
                                maximumLength: 1
                                selectByMouse: true
                                validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9]$/ }
                                onTextEdited: text = text.toUpperCase()
                            }
                            Label { text: qsTr("Realization / implementation") }
                            TextField {
                                id: realizationGestureKey
                                Layout.preferredWidth: 64
                                maximumLength: 1
                                selectByMouse: true
                                validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9]$/ }
                                onTextEdited: text = text.toUpperCase()
                            }
                            Label { text: qsTr("Generalization / inheritance") }
                            TextField {
                                id: generalizationGestureKey
                                Layout.preferredWidth: 64
                                maximumLength: 1
                                selectByMouse: true
                                validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9]$/ }
                                onTextEdited: text = text.toUpperCase()
                            }
                            Label { text: qsTr("Navigable association") }
                            TextField {
                                id: associationGestureKey
                                Layout.preferredWidth: 64
                                maximumLength: 1
                                selectByMouse: true
                                validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9]$/ }
                                onTextEdited: text = text.toUpperCase()
                            }
                            Label { text: qsTr("Aggregation") }
                            TextField {
                                id: aggregationGestureKey
                                Layout.preferredWidth: 64
                                maximumLength: 1
                                selectByMouse: true
                                validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9]$/ }
                                onTextEdited: text = text.toUpperCase()
                            }
                            Label { text: qsTr("Composition") }
                            TextField {
                                id: compositionGestureKey
                                Layout.preferredWidth: 64
                                maximumLength: 1
                                selectByMouse: true
                                validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9]$/ }
                                onTextEdited: text = text.toUpperCase()
                            }
                            Label { text: qsTr("Containment / nesting") }
                            TextField {
                                id: containmentGestureKey
                                Layout.preferredWidth: 64
                                maximumLength: 1
                                selectByMouse: true
                                validator: RegularExpressionValidator { regularExpression: /^[A-Za-z0-9]$/ }
                                onTextEdited: text = text.toUpperCase()
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !preferencesDialog.connectorGestureKeysValid
                            color: uiTheme.warningBorder
                            text: qsTr("Assign one unique letter or digit to every relationship type.")
                        }
                        Item { Layout.fillHeight: true }
                    }
                }

                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Choose which non-destructive commands appear in each contextual diagram toolbox and arrange them in the order you use most often.")
                        }
                        ComboBox {
                            id: contextToolboxKind
                            Layout.fillWidth: true
                            textRole: "label"
                            model: preferencesDialog.contextToolboxKinds
                        }
                        ListView {
                            id: contextToolboxPreferencesList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 2
                            model: preferencesDialog.contextToolboxModel(
                                       contextToolboxKind.currentIndex)
                            ScrollBar.vertical: ScrollBar {}

                            delegate: Rectangle {
                                required property int index
                                required property string actionId
                                required property string actionLabel
                                required property bool actionEnabled
                                width: contextToolboxPreferencesList.width
                                height: 42
                                color: index % 2 === 0
                                       ? uiTheme.panelBackground
                                       : uiTheme.panelHeader

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 4
                                    spacing: 6

                                    CheckBox {
                                        Layout.fillWidth: true
                                        text: actionLabel
                                        checked: actionEnabled
                                        onToggled:
                                            contextToolboxPreferencesList.model
                                            .setProperty(index,
                                                         "actionEnabled",
                                                         checked)
                                    }
                                    ToolButton {
                                        text: "↑"
                                        enabled: index > 0
                                        Accessible.name: qsTr("Move action up")
                                        ToolTip.visible: hovered
                                        ToolTip.text: Accessible.name
                                        onClicked:
                                            contextToolboxPreferencesList.model
                                            .move(index, index - 1, 1)
                                    }
                                    ToolButton {
                                        text: "↓"
                                        enabled:
                                            index + 1
                                            < contextToolboxPreferencesList
                                              .model.count
                                        Accessible.name: qsTr("Move action down")
                                        ToolTip.visible: hovered
                                        ToolTip.text: Accessible.name
                                        onClicked:
                                            contextToolboxPreferencesList.model
                                            .move(index, index + 1, 1)
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                color: uiTheme.mutedText
                                text: qsTr("Commands that do not apply to the current target remain hidden or disabled. Toolbox priority and hover behavior are not changed here.")
                            }
                            Button {
                                text: qsTr("Reset this toolbox")
                                onClicked:
                                    preferencesDialog
                                    .resetCurrentContextToolbox()
                            }
                        }
                    }
                }

                ScrollView {
                    id: cppRelationshipPreferencesScroll
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: cppRelationshipPreferencesScroll.availableWidth
                        spacing: 10

                        Label {
                            text: qsTr("Interface recognition")
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Base class names matching this regular expression are imported as realization / implementation relationships. Matching uses the unqualified name, such as IService rather than app::IService.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Interface pattern") }
                            TextField {
                                id: cppInterfacePattern
                                Layout.fillWidth: true
                                selectByMouse: true
                                placeholderText: "^I[A-Z].*$"
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !preferencesDialog.cppInterfacePatternValid
                            color: uiTheme.warningBorder
                            text: qsTr("Enter a valid, non-empty regular expression.")
                        }
                        Label {
                            text: qsTr("Member type mappings")
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: uiTheme.mutedText
                            text: qsTr("Rules map a C++ wrapper or container to an imported UML relationship and source-end multiplicity. The target argument is one-based: use 2 for the mapped value in map<Key, Value>. A multiplicity such as {2} reads the corresponding non-type template argument, as in array<T, N>.")
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !preferencesDialog.cppMemberTypeRulesValid
                            color: uiTheme.warningBorder
                            text: qsTr("Every type rule needs a unique, non-empty C++ type name.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("C++ type / template")
                                font.bold: true
                            }
                            Label {
                                Layout.preferredWidth: 112
                                text: qsTr("Relationship")
                                font.bold: true
                            }
                            Label {
                                Layout.preferredWidth: 86
                                text: qsTr("Multiplicity")
                                font.bold: true
                            }
                            Label {
                                Layout.preferredWidth: 72
                                text: qsTr("Type arg.")
                                font.bold: true
                            }
                            Item { Layout.preferredWidth: 34 }
                        }
                        Repeater {
                            model: cppMemberTypeRulesModel
                            delegate: RowLayout {
                                id: memberTypeRuleRow
                                required property int index
                                required property string typeName
                                required property string relationshipType
                                required property string multiplicity
                                required property int targetArgument
                                Layout.fillWidth: true
                                spacing: 8

                                TextField {
                                    Layout.fillWidth: true
                                    text: memberTypeRuleRow.typeName
                                    selectByMouse: true
                                    placeholderText: "std::vector"
                                    onEditingFinished:
                                    {
                                        cppMemberTypeRulesModel.setProperty(
                                            memberTypeRuleRow.index,
                                            "typeName", text.trim())
                                        ++preferencesDialog.cppMemberTypeRulesRevision
                                    }
                                }
                                ComboBox {
                                    Layout.preferredWidth: 112
                                    model: [qsTr("Composition"),
                                            qsTr("Aggregation")]
                                    currentIndex:
                                        memberTypeRuleRow.relationshipType
                                        === "aggregation" ? 1 : 0
                                    onActivated:
                                    {
                                        cppMemberTypeRulesModel.setProperty(
                                            memberTypeRuleRow.index,
                                            "relationshipType",
                                            currentIndex === 1
                                            ? "aggregation"
                                            : "composition")
                                        ++preferencesDialog.cppMemberTypeRulesRevision
                                    }
                                }
                                TextField {
                                    Layout.preferredWidth: 86
                                    text: memberTypeRuleRow.multiplicity
                                    selectByMouse: true
                                    placeholderText: "0..*"
                                    onEditingFinished:
                                    {
                                        cppMemberTypeRulesModel.setProperty(
                                            memberTypeRuleRow.index,
                                            "multiplicity", text.trim())
                                        ++preferencesDialog.cppMemberTypeRulesRevision
                                    }
                                }
                                SpinBox {
                                    Layout.preferredWidth: 72
                                    from: 1
                                    to: 16
                                    editable: true
                                    value: memberTypeRuleRow.targetArgument
                                    onValueModified:
                                    {
                                        cppMemberTypeRulesModel.setProperty(
                                            memberTypeRuleRow.index,
                                            "targetArgument", value)
                                        ++preferencesDialog.cppMemberTypeRulesRevision
                                    }
                                }
                                ToolButton {
                                    Layout.preferredWidth: 34
                                    text: "×"
                                    Accessible.name: qsTr("Remove rule")
                                    onClicked: {
                                        cppMemberTypeRulesModel.remove(
                                            memberTypeRuleRow.index)
                                        ++preferencesDialog.cppMemberTypeRulesRevision
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                text: qsTr("Restore default mappings")
                                onClicked:
                                    preferencesDialog.loadCppMemberTypeRules(
                                        applicationSettings
                                        .cppMemberTypeRuleDefaults())
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: qsTr("Add type rule")
                                onClicked: {
                                    cppMemberTypeRulesModel.append({
                                        typeName: "",
                                        relationshipType: "composition",
                                        multiplicity: "1",
                                        targetArgument: 1
                                    })
                                    ++preferencesDialog.cppMemberTypeRulesRevision
                                }
                            }
                        }
                        Item { Layout.preferredHeight: 8 }
                    }
                }

                Item {
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Edit semantic color roles using the swatch or hexadecimal value. Eight-digit values use #AARRGGBB order.")
                        }
                        ListView {
                            id: colorPreferencesList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: colorPreferencesModel
                            spacing: 2
                            section.property: "groupName"
                            section.criteria: ViewSection.FullString
                            ScrollBar.vertical: ScrollBar {}

                            section.delegate: Rectangle {
                                required property string section
                                width: colorPreferencesList.width
                                height: 32
                                color: uiTheme.panelHeader

                                Label {
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 8
                                    text: parent.section
                                    font.bold: true
                                }
                            }

                            delegate: Item {
                                required property int index
                                required property string roleKey
                                required property string displayName
                                required property var themeColor
                                width: colorPreferencesList.width
                                height: 40
                                onThemeColorChanged: colorValueEditor.text =
                                                         uiTheme.colorText(themeColor)

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 10

                                    Label {
                                        Layout.fillWidth: true
                                        text: displayName
                                        elide: Text.ElideRight
                                    }
                                    CatalogButton {
                                        catalogId: "preferences.chooseThemeColor"
                                        Layout.preferredWidth: 54
                                        Layout.preferredHeight: 28
                                        Accessible.name: qsTr("Choose %1 color").arg(displayName)
                                        onClicked: {
                                            colorPicker.modelIndex = index
                                            colorPicker.selectedColor = themeColor
                                            colorPicker.open()
                                        }
                                        background: Rectangle {
                                            color: themeColor
                                            border.color: uiTheme.controlBorder
                                            radius: 3
                                        }
                                    }
                                    TextField {
                                        id: colorValueEditor
                                        Layout.preferredWidth: 116
                                        text: uiTheme.colorText(themeColor)
                                        selectByMouse: true
                                        validator: RegularExpressionValidator {
                                            regularExpression: /^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$/
                                        }
                                        onEditingFinished: {
                                            const normalized = uiTheme.normalizeColor(text)
                                            if (acceptableInput && normalized.length > 0)
                                                colorPreferencesModel.setProperty(index, "themeColor", normalized)
                                            else
                                                text = uiTheme.colorText(themeColor)
                                        }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            CatalogButton {
                                catalogId: "preferences.resetThemeColors"
                                text: qsTr("Reset colors")
                                onClicked: {
                                    for (let index = 0; index < colorPreferencesModel.count; ++index) {
                                        const role = colorPreferencesModel.get(index).roleKey
                                        colorPreferencesModel.setProperty(
                                                    index, "themeColor", uiTheme.defaultColor(role))
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ListModel { id: colorPreferencesModel }
        ListModel { id: cppMemberTypeRulesModel }
        ListModel { id: relationshipToolboxPreferencesModel }
        ListModel { id: selectionToolboxPreferencesModel }
        ListModel { id: connectorToolboxPreferencesModel }
        ListModel { id: presentationToolboxPreferencesModel }
    }

    ColorDialog {
        id: colorPicker
        property int modelIndex: -1
        title: qsTr("Choose theme color")
        options: ColorDialog.ShowAlphaChannel
        onAccepted: {
            if (modelIndex >= 0)
                colorPreferencesModel.setProperty(modelIndex, "themeColor", selectedColor)
            modelIndex = -1
        }
        onRejected: modelIndex = -1
    }

    CatalogDialog {
        id: unsavedChangesDialog
        saveCatalogId: "project.saveProject"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 40)
        modal: true
        focus: true
        title: qsTr("Unsaved changes")
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

        contentItem: Label {
            wrapMode: Text.Wrap
            padding: 16
            text: pendingDocumentAction === "close"
                  ? qsTr("Save changes to %1 before closing?").arg(projectController.projectName)
                  : qsTr("Save changes to %1 before continuing?").arg(projectController.projectName)
        }

        onAccepted: root.saveAndContinue()
        onDiscarded: root.performDocumentAction(root.pendingDocumentAction)
        onRejected: root.cancelPendingDocumentAction()
    }

    CatalogDialog {
        id: replaceProjectDialog
        yesCatalogId: "project.replaceProject"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(500, parent.width - 40)
        // Fusion's Dialog style derives implicitHeight from a wrapped content
        // label whose laid-out height, in turn, depends on the popup geometry.
        // This compact confirmation has bounded content, so an explicit
        // implicit height breaks that startup-time cycle deterministically.
        implicitHeight: 180
        modal: true
        focus: true
        title: qsTr("Replace existing project?")
        standardButtons: Dialog.Yes | Dialog.Cancel

        contentItem: Label {
            wrapMode: Text.Wrap
            padding: 16
            text: qsTr("The selected folder already contains a yauml project. "
                       + "Saving here will replace its manifest, model, and "
                       + "diagram files. Continue?")
        }

        onOpened: {
            const replaceButton = standardButton(Dialog.Yes)
            if (replaceButton)
                replaceButton.text = qsTr("Replace project")
        }
        onAccepted: root.saveToSelectedFolder(root.pendingSaveUrl, true)
        onRejected: {
            root.pendingSaveUrl = ""
            root.cancelPendingDocumentAction()
        }
    }

    CatalogDialog {
        id: externalProjectChangesDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 40)
        implicitHeight: 310
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        title: qsTr("Project files changed outside yauml")

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Saving was stopped to avoid overwriting files "
                           + "changed by another application, Git operation, "
                           + "or yauml instance.")
            }

            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                wrapMode: Text.Wrap
                font.family: "monospace"
                text: projectController.externallyChangedProjectFiles.length > 0
                      ? "• " + projectController.externallyChangedProjectFiles.join("\n• ")
                      : qsTr("The changed file list is unavailable.")
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Reload discards the unsaved in-memory changes. "
                           + "Overwrite replaces the external changes.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                CatalogButton {
                    catalogId: "project.saveProject"
                    text: qsTr("Save As…")
                    onClicked: root.saveExternallyChangedProjectAs()
                }
                CatalogButton {
                    catalogId: "project.openProject"
                    text: qsTr("Reload")
                    onClicked: root.reloadExternallyChangedProject()
                }
                Item { Layout.fillWidth: true }
                CatalogButton {
                    catalogId: "dialog.cancel"
                    text: qsTr("Cancel")
                    onClicked: {
                        externalProjectChangesDialog.close()
                        root.cancelPendingDocumentAction()
                    }
                }
                CatalogButton {
                    catalogId: "project.replaceProject"
                    text: qsTr("Overwrite")
                    onClicked: root.overwriteExternallyChangedProject()
                }
            }
        }

        Shortcut {
            sequences: [StandardKey.Cancel]
            onActivated: {
                externalProjectChangesDialog.close()
                root.cancelPendingDocumentAction()
            }
        }
    }

    CatalogDialog {
        id: cppImportDialog
        applyCatalogId: "project.applyCppSynchronization"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(1120, parent.width - 40)
        height: Math.min(700, parent.height - 40)
        modal: true
        focus: true
        // A synchronization decision must be explicit. Qt dialogs otherwise
        // inherit CloseOnPressOutside and appear to stop being modal when the
        // user clicks the main window.
        closePolicy: Popup.CloseOnEscape
        title: qsTr("C++ synchronization preview")
        standardButtons: Dialog.Apply | Dialog.Close
        property string statusFilter: "all"
        property string textFilter: ""
        property string sortColumn: "action"
        property bool sortAscending: true
        readonly property var filteredPreviewItems: filterAndSortPreviewItems()
        readonly property int filteredResolvableConflictCount:
            filteredItemCount("conflict", true)
        readonly property int filteredCleanupCount:
            filteredItemCount("missing-source", false)
            + filteredItemCount("out-of-scope", false)

        function statusLabel(action) {
            switch (action) {
            case "create": return qsTr("CREATE")
            case "update": return qsTr("UPDATE")
            case "conflict": return qsTr("CONFLICT")
            case "unchanged": return qsTr("UNCHANGED")
            case "user-modified": return qsTr("USER MODIFIED")
            case "missing-source": return qsTr("NOT FOUND IN SCAN")
            case "out-of-scope": return qsTr("OUT OF SCOPE")
            }
            return action.toUpperCase()
        }

        function resolutionValue(item) {
            if (item.action === "conflict")
                return item.resolvable ? item.resolution : "manual-repair"
            if (item.action === "out-of-scope")
                return item.outOfScopeResolution
            if (item.action === "missing-source")
                return item.missingSourceResolution
            return ""
        }

        function resolutionLabel(item) {
            const resolution = resolutionValue(item)
            switch (resolution) {
            case "unresolved": return qsTr("Unresolved")
            case "keep-model": return qsTr("Keep model")
            case "use-source": return qsTr("Use C++ source")
            case "keep": return qsTr("Keep for now")
            case "remove": return qsTr("Remove from model")
            case "keep-manual": return qsTr("Keep as manual")
            case "manual-repair": return qsTr("Manual repair required")
            }
            return qsTr("—")
        }

        function itemMatchesFilter(item) {
            if (statusFilter === "needs-decision") {
                const unresolvedConflict =
                        item.action === "conflict"
                        && (!item.resolvable
                            || item.resolution === "unresolved")
                const unresolvedRemoval =
                        item.action === "out-of-scope"
                        && item.outOfScopeResolution === "unresolved"
                if (!unresolvedConflict && !unresolvedRemoval)
                    return false
            } else if (statusFilter !== "all"
                       && item.action !== statusFilter) {
                return false
            }

            const needle = textFilter.trim().toLocaleLowerCase()
            if (needle.length === 0)
                return true
            const searchable = [
                item.action, item.name, item.type, item.file, item.message,
                item.impact, item.resolutionDetail, item.classification,
                resolutionLabel(item)
            ].join(" ").toLocaleLowerCase()
            return searchable.indexOf(needle) >= 0
        }

        function itemSortValue(item) {
            switch (sortColumn) {
            case "name": return item.name
            case "type": return item.type
            case "file": return item.file + ":" + item.line
            case "resolution": return resolutionLabel(item)
            }
            return statusLabel(item.action)
        }

        function filterAndSortPreviewItems() {
            const result = []
            const source = cppImportController.previewItems
            for (let index = 0; index < source.length; ++index) {
                if (itemMatchesFilter(source[index]))
                    result.push(source[index])
            }
            result.sort(function(left, right) {
                const leftValue =
                        String(itemSortValue(left)).toLocaleLowerCase()
                const rightValue =
                        String(itemSortValue(right)).toLocaleLowerCase()
                const comparison = leftValue.localeCompare(rightValue)
                if (comparison !== 0)
                    return sortAscending ? comparison : -comparison
                return String(left.name).localeCompare(String(right.name))
            })
            return result
        }

        function filteredItemCount(action, resolvableOnly) {
            let count = 0
            const items = filteredPreviewItems
            for (let index = 0; index < items.length; ++index) {
                if (items[index].action === action
                        && (!resolvableOnly || items[index].resolvable))
                    ++count
            }
            return count
        }

        function setSortColumn(column) {
            if (sortColumn === column)
                sortAscending = !sortAscending
            else {
                sortColumn = column
                sortAscending = true
            }
        }

        function sortHeaderText(label, column) {
            if (sortColumn !== column)
                return label
            return label + (sortAscending ? "  ↑" : "  ↓")
        }

        function preservePreviewScroll(change) {
            const savedContentY = cppImportPreviewList.contentY
            change()
            // Replacing a QVariantList resets ListView's viewport. Restore it
            // after bindings have produced the refreshed, filtered model.
            Qt.callLater(function() {
                const minimum = cppImportPreviewList.originY
                const maximum = Math.max(
                                  minimum,
                                  minimum
                                  + cppImportPreviewList.contentHeight
                                  - cppImportPreviewList.height)
                cppImportPreviewList.contentY = Math.max(
                            minimum, Math.min(maximum, savedContentY))
            })
        }

        function setConflictResolution(conflictKey, resolution) {
            preservePreviewScroll(function() {
                cppImportController.setConflictResolution(
                            conflictKey, resolution)
            })
        }

        function resolveAllConflicts(resolution) {
            preservePreviewScroll(function() {
                cppImportController.resolveAllConflicts(resolution)
            })
        }

        function setOutOfScopeResolution(outOfScopeKey, resolution) {
            preservePreviewScroll(function() {
                cppImportController.setOutOfScopeResolution(
                            outOfScopeKey, resolution)
            })
        }

        function resolveAllOutOfScope(resolution) {
            preservePreviewScroll(function() {
                cppImportController.resolveAllOutOfScope(resolution)
            })
        }

        function setMissingSourceResolution(missingSourceKey, resolution) {
            preservePreviewScroll(function() {
                cppImportController.setMissingSourceResolution(
                            missingSourceKey, resolution)
            })
        }

        function resolveVisibleConflicts(resolution) {
            preservePreviewScroll(function() {
                // Each controller call rebuilds previewItems, so iterate a
                // snapshot rather than the live filtered list.
                const visibleItems = filteredPreviewItems.slice()
                for (let index = 0; index < visibleItems.length; ++index) {
                    const item = visibleItems[index]
                    if (item.action === "conflict" && item.resolvable)
                        cppImportController.setConflictResolution(
                                    item.conflictKey, resolution)
                }
            })
        }

        function resolveVisibleCleanup(resolution) {
            preservePreviewScroll(function() {
                const visibleItems = filteredPreviewItems.slice()
                for (let index = 0; index < visibleItems.length; ++index) {
                    const item = visibleItems[index]
                    if (item.action === "missing-source") {
                        cppImportController.setMissingSourceResolution(
                                    item.missingSourceKey,
                                    resolution === "reset"
                                    ? "keep" : resolution)
                    } else if (item.action === "out-of-scope") {
                        cppImportController.setOutOfScopeResolution(
                                    item.outOfScopeKey,
                                    resolution === "reset"
                                    ? "unresolved" : resolution)
                    }
                }
            })
        }

        onOpened: {
            const applyButton = standardButton(Dialog.Apply)
            if (applyButton)
                applyButton.text = qsTr("Apply synchronization")
        }
        onApplied: cppImportController.applyPreview()

        Connections {
            target: cppImportController
            function refreshApplyButton() {
                const applyButton = cppImportDialog.standardButton(Dialog.Apply)
                if (applyButton)
                    applyButton.enabled = cppImportController.canApply
            }
            function onPreviewChanged() { refreshApplyButton() }
            function onBusyChanged() { refreshApplyButton() }
        }

        contentItem: ColumnLayout {
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                BusyIndicator {
                    running: cppImportController.busy
                    visible: running
                }
                Label {
                    Layout.fillWidth: true
                    text: cppImportController.busy
                          ? cppImportController.progressText
                          : cppImportController.summary
                    wrapMode: Text.Wrap
                }
            }
            ProgressBar {
                Layout.fillWidth: true
                visible: cppImportController.busy
                         && cppImportController.progressMaximum > 0
                from: 0
                to: Math.max(1, cppImportController.progressMaximum)
                value: cppImportController.progressValue
            }
            Label {
                Layout.fillWidth: true
                visible: cppImportController.busy
                         && cppImportController.progressDetail.length > 0
                text: cppImportController.progressDetail
                color: uiTheme.mutedText
                elide: Text.ElideMiddle
            }
            Label {
                Layout.fillWidth: true
                visible: cppImportController.previewSourceRoots.length > 0
                text: cppImportController.previewSourceRoots.length === 1
                      ? qsTr("Source: %1").arg(
                            cppImportController.previewSourceRoot)
                      : qsTr("Sources (%1):\n%2")
                            .arg(cppImportController.previewSourceRoots.length)
                            .arg(cppImportController.previewSourceRootsText)
                color: uiTheme.mutedText
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideMiddle
            }
            Label {
                Layout.fillWidth: true
                visible: cppImportController.compilationDatabasePath.length > 0
                text: qsTr("Compilation database: %1")
                      .arg(cppImportController.compilationDatabasePath)
                color: uiTheme.mutedText
                elide: Text.ElideMiddle
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !cppImportController.busy
                         && cppImportController.conflictCount > 0
                Label {
                    Layout.fillWidth: true
                    text: qsTr("%1 conflict(s), %2 still unresolved")
                          .arg(cppImportController.conflictCount)
                          .arg(cppImportController.unresolvedConflictCount)
                    color: cppImportController.unresolvedConflictCount > 0
                           ? uiTheme.warningBorder : uiTheme.mutedText
                }
                Button {
                    text: qsTr("Keep model for all")
                    enabled: cppImportController.resolvableConflictCount > 0
                    onClicked: cppImportDialog.resolveAllConflicts(
                                   "keep-model")
                }
                Button {
                    text: qsTr("Use source for all")
                    enabled: cppImportController.resolvableConflictCount > 0
                    onClicked: cppImportDialog.resolveAllConflicts(
                                   "use-source")
                }
                Button {
                    text: qsTr("Clear choices")
                    enabled: cppImportController.unresolvedConflictCount
                             < cppImportController.conflictCount
                    onClicked: cppImportDialog.resolveAllConflicts(
                                   "unresolved")
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !cppImportController.busy
                         && cppImportController.outOfScopeCount > 0
                Label {
                    Layout.fillWidth: true
                    text: qsTr("%1 item(s) belong to excluded source folders; "
                               + "%2 still need a decision")
                          .arg(cppImportController.outOfScopeCount)
                          .arg(cppImportController.unresolvedOutOfScopeCount)
                    color: cppImportController.unresolvedOutOfScopeCount > 0
                           ? uiTheme.warningBorder : uiTheme.mutedText
                    wrapMode: Text.Wrap
                }
                Button {
                    text: qsTr("Remove all")
                    onClicked: cppImportDialog.resolveAllOutOfScope(
                                   "remove")
                }
                Button {
                    text: qsTr("Keep all as manual")
                    onClicked: cppImportDialog.resolveAllOutOfScope(
                                   "keep-manual")
                }
                Button {
                    text: qsTr("Clear choices")
                    enabled: cppImportController.unresolvedOutOfScopeCount
                             < cppImportController.outOfScopeCount
                    onClicked: cppImportDialog.resolveAllOutOfScope(
                                   "unresolved")
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !cppImportController.busy
                         && cppImportController.missingSourceCount > 0
                Label {
                    Layout.fillWidth: true
                    text: qsTr(
                              "%1 previously imported item(s) were not matched "
                              + "in this scan. They are retained by default; "
                              + "filter this table before applying a bulk action.")
                          .arg(cppImportController.missingSourceCount)
                    color: uiTheme.warningBorder
                    wrapMode: Text.Wrap
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !cppImportController.busy
                         && cppImportController.previewItems.length > 0
                spacing: 8

                TextField {
                    id: cppImportItemFilter
                    Layout.fillWidth: true
                    placeholderText: qsTr(
                                         "Filter by name, type, source, or details")
                    text: cppImportDialog.textFilter
                    Accessible.name: qsTr("Filter synchronization items")
                    onTextChanged: cppImportDialog.textFilter = text
                    Keys.onEscapePressed: clear()
                }
                ComboBox {
                    id: cppImportStatusFilter
                    Layout.preferredWidth: 190
                    model: [
                        qsTr("All statuses"),
                        qsTr("Needs decision"),
                        qsTr("CREATE"),
                        qsTr("UPDATE"),
                        qsTr("CONFLICT"),
                        qsTr("UNCHANGED"),
                        qsTr("USER MODIFIED"),
                        qsTr("NOT FOUND IN SCAN"),
                        qsTr("OUT OF SCOPE")
                    ]
                    Accessible.name: qsTr("Synchronization status filter")
                    onActivated: {
                        const filters = [
                            "all", "needs-decision", "create", "update",
                            "conflict", "unchanged", "user-modified",
                            "missing-source", "out-of-scope"
                        ]
                        cppImportDialog.statusFilter = filters[currentIndex]
                    }
                }
                ToolButton {
                    id: cppImportSetFilteredButton
                    text: qsTr("Set filtered…")
                    enabled: cppImportDialog.filteredResolvableConflictCount > 0
                             || cppImportDialog.filteredCleanupCount > 0
                    Accessible.name: qsTr(
                                         "Set resolution for filtered items")
                    onClicked: cppImportFilteredResolutionMenu.popup(
                                   cppImportSetFilteredButton, 0, height)

                    Menu {
                        id: cppImportFilteredResolutionMenu
                        MenuItem {
                            text: qsTr("Keep model for filtered conflicts")
                            enabled:
                                cppImportDialog.filteredResolvableConflictCount
                                > 0
                            onTriggered:
                                cppImportDialog.resolveVisibleConflicts(
                                    "keep-model")
                        }
                        MenuItem {
                            text: qsTr(
                                      "Use C++ source for filtered conflicts")
                            enabled:
                                cppImportDialog.filteredResolvableConflictCount
                                > 0
                            onTriggered:
                                cppImportDialog.resolveVisibleConflicts(
                                    "use-source")
                        }
                        MenuItem {
                            text: qsTr(
                                      "Clear filtered conflict choices")
                            enabled:
                                cppImportDialog.filteredResolvableConflictCount
                                > 0
                            onTriggered:
                                cppImportDialog.resolveVisibleConflicts(
                                    "unresolved")
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr(
                                      "Remove filtered source items")
                            enabled: cppImportDialog.filteredCleanupCount > 0
                            onTriggered:
                                cppImportDialog.resolveVisibleCleanup("remove")
                        }
                        MenuItem {
                            text: qsTr(
                                      "Keep filtered source items as manual")
                            enabled: cppImportDialog.filteredCleanupCount > 0
                            onTriggered:
                                cppImportDialog.resolveVisibleCleanup(
                                    "keep-manual")
                        }
                        MenuItem {
                            text: qsTr(
                                      "Reset filtered source decisions")
                            enabled: cppImportDialog.filteredCleanupCount > 0
                            onTriggered:
                                cppImportDialog.resolveVisibleCleanup("reset")
                        }
                    }
                }
                Label {
                    text: qsTr("%1 of %2 items")
                          .arg(cppImportDialog.filteredPreviewItems.length)
                          .arg(cppImportController.previewItems.length)
                    color: uiTheme.mutedText
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: uiTheme.surface
                border.color: uiTheme.controlBorder
                radius: 3

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        color: uiTheme.alternateRow

                        RowLayout {
                            anchors.fill: parent
                            spacing: 0

                            ToolButton {
                                Layout.preferredWidth: 128
                                Layout.fillHeight: true
                                text: cppImportDialog.sortHeaderText(
                                          qsTr("Status"), "action")
                                font.bold: true
                                onClicked: cppImportDialog.setSortColumn(
                                               "action")
                            }
                            ToolButton {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: cppImportDialog.sortHeaderText(
                                          qsTr("Item"), "name")
                                font.bold: true
                                onClicked: cppImportDialog.setSortColumn("name")
                            }
                            ToolButton {
                                Layout.preferredWidth: 110
                                Layout.fillHeight: true
                                text: cppImportDialog.sortHeaderText(
                                          qsTr("Type"), "type")
                                font.bold: true
                                onClicked: cppImportDialog.setSortColumn("type")
                            }
                            ToolButton {
                                Layout.preferredWidth: 220
                                Layout.fillHeight: true
                                text: cppImportDialog.sortHeaderText(
                                          qsTr("Source"), "file")
                                font.bold: true
                                onClicked: cppImportDialog.setSortColumn("file")
                            }
                            ToolButton {
                                Layout.preferredWidth: 190
                                Layout.fillHeight: true
                                text: cppImportDialog.sortHeaderText(
                                          qsTr("Resolution"), "resolution")
                                font.bold: true
                                onClicked: cppImportDialog.setSortColumn(
                                               "resolution")
                            }
                        }
                    }

                    ListView {
                        id: cppImportPreviewList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: cppImportDialog.filteredPreviewItems
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            width: ListView.view.width
                            height: Math.max(
                                        58, importItemRow.implicitHeight + 14)
                            color: modelData.action === "conflict"
                                   || modelData.action === "out-of-scope"
                                   ? uiTheme.warningRow
                                 : index % 2
                                   ? uiTheme.alternateRow : uiTheme.surface

                            RowLayout {
                                id: importItemRow
                                anchors.fill: parent
                                anchors.margins: 7
                                spacing: 8

                                Label {
                                    Layout.preferredWidth: 114
                                    Layout.alignment: Qt.AlignTop
                                    text: cppImportDialog.statusLabel(
                                              modelData.action)
                                    font.bold: true
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    text: modelData.name + "\n"
                                          + modelData.message
                                          + (modelData.impact
                                             ? "\n" + modelData.impact : "")
                                          + (modelData.resolutionDetail
                                             ? "\n"
                                               + modelData.resolutionDetail : "")
                                          + (modelData.classification
                                             ? "\n"
                                               + modelData.classification : "")
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    Layout.preferredWidth: 96
                                    Layout.alignment: Qt.AlignTop
                                    text: modelData.type
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    Layout.preferredWidth: 206
                                    Layout.alignment: Qt.AlignTop
                                    text: modelData.file.length > 0
                                          ? modelData.file + ":"
                                            + modelData.line : qsTr("—")
                                    wrapMode: Text.WrapAnywhere
                                    color: uiTheme.mutedText
                                }
                                Item {
                                    Layout.preferredWidth: 176
                                    Layout.fillHeight: true

                                    ComboBox {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        visible: modelData.action === "conflict"
                                                 && modelData.resolvable
                                        model: [qsTr("Unresolved"),
                                                qsTr("Keep model"),
                                                qsTr("Use C++ source")]
                                        currentIndex:
                                            modelData.resolution === "keep-model"
                                            ? 1
                                            : modelData.resolution
                                              === "use-source" ? 2 : 0
                                        Accessible.name: qsTr(
                                            "Conflict resolution for %1")
                                            .arg(modelData.name)
                                        onActivated: {
                                            const resolutions = [
                                                "unresolved", "keep-model",
                                                "use-source"
                                            ]
                                            cppImportDialog
                                                .setConflictResolution(
                                                    modelData.conflictKey,
                                                    resolutions[currentIndex])
                                        }
                                    }
                                    ComboBox {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        visible: modelData.action
                                                 === "out-of-scope"
                                        model: [qsTr("Choose action"),
                                                qsTr("Remove from model"),
                                                qsTr("Keep as manual")]
                                        currentIndex:
                                            modelData.outOfScopeResolution
                                            === "remove" ? 1
                                            : modelData.outOfScopeResolution
                                              === "keep-manual" ? 2 : 0
                                        Accessible.name: qsTr(
                                            "Excluded source action for %1")
                                            .arg(modelData.name)
                                        onActivated: {
                                            const resolutions = [
                                                "unresolved", "remove",
                                                "keep-manual"
                                            ]
                                            cppImportDialog
                                                .setOutOfScopeResolution(
                                                    modelData.outOfScopeKey,
                                                    resolutions[currentIndex])
                                        }
                                    }
                                    ComboBox {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        visible: modelData.action
                                                 === "missing-source"
                                        model: [qsTr("Keep for now"),
                                                qsTr("Remove from model"),
                                                qsTr("Keep as manual")]
                                        currentIndex:
                                            modelData.missingSourceResolution
                                            === "remove" ? 1
                                            : modelData
                                              .missingSourceResolution
                                              === "keep-manual" ? 2 : 0
                                        Accessible.name: qsTr(
                                            "Not-found source action for %1")
                                            .arg(modelData.name)
                                        onActivated: {
                                            const resolutions = [
                                                "keep", "remove",
                                                "keep-manual"
                                            ]
                                            cppImportDialog
                                                .setMissingSourceResolution(
                                                    modelData.missingSourceKey,
                                                    resolutions[currentIndex])
                                        }
                                    }
                                    Label {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        visible: modelData.action === "conflict"
                                                 && !modelData.resolvable
                                        text: qsTr("Manual repair required")
                                        color: uiTheme.warningBorder
                                        wrapMode: Text.Wrap
                                    }
                                    Label {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        visible: modelData.action !== "conflict"
                                                 && modelData.action
                                                 !== "out-of-scope"
                                                 && modelData.action
                                                 !== "missing-source"
                                        text: qsTr("—")
                                        color: uiTheme.mutedText
                                    }
                                }
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: !cppImportController.busy
                                     && cppImportController.previewItems.length
                                     === 0
                            text: qsTr(
                                      "No C++ model declarations were discovered")
                            color: uiTheme.mutedText
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: !cppImportController.busy
                                     && cppImportController.previewItems.length
                                     > 0
                                     && cppImportDialog.filteredPreviewItems
                                     .length === 0
                            text: qsTr(
                                      "No synchronization items match the filter")
                            color: uiTheme.mutedText
                        }
                    }
                }
            }
        }
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
            color: uiTheme.surface
            border.color: uiTheme.overlayBorder
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
                CatalogToolButton {
                    catalogId: "workspace.clearLog"
                    text: qsTr("Clear")
                    onClicked: projectController.diagnostics.clear()
                }
                CatalogToolButton {
                    catalogId: "workspace.closeLog"
                    text: "×"
                    onClicked: logPopup.close()
                }
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
                    color: severity === "error" ? uiTheme.errorRow
                         : severity === "warning" ? uiTheme.warningRow
                         : index % 2 ? uiTheme.alternateRow : uiTheme.surface
                    Label {
                        id: logText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 8
                        text: timestamp.toLocaleTimeString(Qt.locale(), "HH:mm:ss")
                              + "  [" + severity.toUpperCase() + "/" + category + "]  " + message
                        wrapMode: Text.Wrap
                        color: uiTheme.bodyText
                    }
                }
            }
        }
    }

    Connections {
        target: projectController.diagnostics
        function onAttentionAdded() { logPopup.open() }
    }

    Connections {
        target: projectController
        function onExternalProjectChangeDetected() {
            externalProjectChangesDialog.open()
        }
    }

    Connections {
        target: cppImportController
        function onAttentionRequired() { logPopup.open() }
    }

    Connections {
        target: sourceFolderPicker
        function onFoldersSelected(folders) {
            cppImportDialog.open()
            cppImportController.previewSources(folders)
        }
        function onFallbackRequested() {
            cppImportFolderDialog.open()
        }
    }

    FolderDialog {
        id: openDialog
        title: qsTr("Open yauml project directory")
        onAccepted: projectController.openProject(selectedFolder)
    }

    FolderDialog {
        id: saveDialog
        title: qsTr("Choose yauml project directory")
        onAccepted: {
            root.pendingSaveUrl = selectedFolder
            if (projectController.saveDestinationContainsProject(selectedFolder))
                replaceProjectDialog.open()
            else
                root.saveToSelectedFolder(selectedFolder, false)
        }
        onRejected: {
            root.pendingSaveUrl = ""
            root.cancelPendingDocumentAction()
        }
    }

    FolderDialog {
        id: cppImportFolderDialog
        title: qsTr("Choose C++ source directory")
        onAccepted: {
            cppImportDialog.open()
            cppImportController.previewSources([selectedFolder])
        }
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
