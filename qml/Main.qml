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
    title: (projectController.dirty ? "*" : "") + projectController.projectName + " — u uml"

    property bool leftPanelVisible: workspaceController.projectTreeVisible
    property bool rightPanelVisible: workspaceController.propertiesVisible
    property bool geometryReady: false
    property bool closeAuthorized: false
    property bool quitScheduled: false
    property string pendingDocumentAction: ""
    property url pendingRecentProjectUrl: ""
    property url pendingSaveUrl: ""

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

    function cancelPendingDocumentAction() {
        pendingDocumentAction = ""
        pendingRecentProjectUrl = ""
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
                text: cppImportController.configuredSourceRoot.length > 0
                      ? qsTr("Change C++ source…")
                      : qsTr("Import C++…")
                enabled: !cppImportController.busy
                onTriggered: cppImportFolderDialog.open()
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
                TreeView {
                    id: projectTree
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: projectController.treeModel
                    clip: true
                    // Apply selection explicitly in selectFromPointer(). The
                    // native delegate can otherwise collapse an extended
                    // selection before Ctrl/Shift handling on Windows.
                    pointerNavigationEnabled: false
                    selectionBehavior: TableView.SelectRows
                    selectionMode: TableView.ExtendedSelection
                    readonly property string browserItemsMimeType:
                        "application/x-uuml-browser-items"
                    property bool selectionOriginatesFromTree: false
                    function toggleBranch(row) {
                        if (row >= 0)
                            toggleExpanded(row)
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
                            const originatedFromTree = projectTree.selectionOriginatesFromTree
                            Qt.callLater(function() {
                                const itemIndex = projectController.treeModel.indexForObject(
                                                    projectController.selectedId,
                                                    projectController.selectedKind)
                                if (itemIndex.valid)
                                    projectTree.expandToIndex(itemIndex)
                                if (!originatedFromTree
                                        && projectController.selectedKind === "element"
                                        && itemIndex.valid) {
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
                                    projectTree.positionViewAtRow(row, TableView.Contain)
                            })
                        }
                    }

                    delegate: TreeViewDelegate {
                        id: treeDelegate
                        required property string objectId
                        required property string kind
                        required property string objectType
                        required property bool nested
                        property bool browserDropActive: false
                        property int browserInsertionEdge: 0
                        highlighted: kind !== "root" && (
                                         kind === "diagram"
                                         ? objectId === workspaceController.activeDiagramId
                                         : selected)
                        icon.source: iconRegistry.projectTreeIcon(
                                         kind, objectType, objectId,
                                         nested, expanded)
                        icon.width: iconRegistry.defaultSize
                        icon.height: iconRegistry.defaultSize
                        background: Rectangle {
                            color: treeDelegate.highlighted ? uiTheme.accent
                                 : treeDelegate.browserDropActive ? uiTheme.hoverBackground
                                 : treeDelegate.hovered ? uiTheme.hoverBackground : "transparent"
                        }
                        onClicked: {
                            if (kind !== "root") {
                                const itemIndex = projectTree.index(treeDelegate.row,
                                                                    treeDelegate.column)
                                projectController.treeModel.selectFromPointer(
                                            projectTreeSelection, itemIndex)
                                if (kind === "element" || kind === "diagram")
                                    projectController.selectObject(objectId, kind)
                                else
                                    projectController.clearSelection()
                            }
                        }
                        onDoubleClicked: {
                            if (kind === "element") {
                                projectController.selectObject(objectId, kind)
                                projectController.addSelectedToDiagram(
                                            workspaceController.activeDiagramId,
                                            applicationSettings.diagramItemSizingMode)
                            } else if (kind === "diagram") {
                                workspaceController.activeDiagramId = objectId
                            } else if (treeDelegate.isTreeNode
                                       && treeDelegate.hasChildren) {
                                projectTree.toggleBranch(treeDelegate.row)
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
                        MouseArea {
                            parent: treeDelegate.indicator
                            anchors.fill: parent
                            enabled: treeDelegate.isTreeNode
                                     && treeDelegate.hasChildren
                            acceptedButtons: Qt.LeftButton
                            preventStealing: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: projectTree.toggleBranch(treeDelegate.row)
                        }

                        DragHandler {
                            target: null
                            enabled: treeDelegate.kind === "element"
                                     || treeDelegate.kind === "namespace"
                                     || treeDelegate.kind === "folder"
                            grabPermissions: PointerHandler.CanTakeOverFromAnything
                            onActiveChanged: {
                                if (!active)
                                    return
                                const itemIndex = projectTree.index(treeDelegate.row,
                                                                    treeDelegate.column)
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
                                enabled: treeDelegate.kind === "element"
                                         || treeDelegate.kind === "folder"
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
                                const itemIndex = projectTree.index(
                                                    treeDelegate.row,
                                                    treeDelegate.column)
                                if ((treeDelegate.kind === "element"
                                     || treeDelegate.kind === "folder")
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
                                treeContextMenu.targetName = treeDelegate.text
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
                  ? qsTr("Delete")
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

        function updateOkButton() {
            const button = standardButton(Dialog.Ok)
            if (button)
                button.enabled = connectorGestureKeysValid
                                 && cppInterfacePatternValid
        }

        onConnectorGestureKeysValidChanged: updateOkButton()
        onCppInterfacePatternValidChanged: updateOkButton()

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
            cppOwningPointerTypes.text =
                    applicationSettings.cppOwningPointerTypes.join("\n")
            cppSharedPointerTypes.text =
                    applicationSettings.cppSharedPointerTypes.join("\n")
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
            applicationSettings.defaultConnectorRouting =
                    defaultConnectorRouting.currentIndex === 1
                    ? "orthogonal" : "straight"
            applicationSettings.setCppInterfacePattern(cppInterfacePattern.text)
            applicationSettings.setCppPointerTypes(
                        cppOwningPointerTypes.text.split(/\r?\n/),
                        cppSharedPointerTypes.text.split(/\r?\n/))
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
                            text: qsTr("C++ import")
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
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: uiTheme.mutedText
                            text: qsTr("Member ownership determines the imported UML relationship: values and owning pointers become compositions; shared pointers and raw pointer/reference members become aggregations. Enter one qualified pointer-template name per line.")
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 8

                            Label {
                                text: qsTr("Owning pointer types")
                                Layout.alignment: Qt.AlignTop
                            }
                            TextArea {
                                id: cppOwningPointerTypes
                                Layout.fillWidth: true
                                Layout.preferredHeight: 76
                                selectByMouse: true
                                wrapMode: TextEdit.NoWrap
                                placeholderText: "std::unique_ptr"
                            }
                            Label {
                                text: qsTr("Shared pointer types")
                                Layout.alignment: Qt.AlignTop
                            }
                            TextArea {
                                id: cppSharedPointerTypes
                                Layout.fillWidth: true
                                Layout.preferredHeight: 76
                                selectByMouse: true
                                wrapMode: TextEdit.NoWrap
                                placeholderText: "std::shared_ptr"
                            }
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
            text: qsTr("The selected folder already contains a u uml project. "
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
        id: cppImportDialog
        applyCatalogId: "project.applyCppSynchronization"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(780, parent.width - 40)
        height: Math.min(620, parent.height - 40)
        modal: true
        focus: true
        title: qsTr("C++ synchronization preview")
        standardButtons: Dialog.Apply | Dialog.Close

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
                    text: cppImportController.summary
                    wrapMode: Text.Wrap
                }
            }
            Label {
                Layout.fillWidth: true
                visible: cppImportController.previewSourceRoot.length > 0
                text: qsTr("Source: %1")
                      .arg(cppImportController.previewSourceRoot)
                color: uiTheme.mutedText
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
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: uiTheme.surface
                border.color: uiTheme.controlBorder
                radius: 3

                ListView {
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: cppImportController.previewItems
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        width: ListView.view.width
                        height: Math.max(50, importItemText.implicitHeight + 14)
                        color: modelData.action === "conflict" ? uiTheme.warningRow
                             : index % 2 ? uiTheme.alternateRow : uiTheme.surface

                        Label {
                            id: importItemText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 7
                            text: modelData.action.toUpperCase() + "  "
                                  + modelData.name + " (" + modelData.type + ")\n"
                                  + modelData.message + (modelData.file.length > 0
                                    ? " — " + modelData.file + ":" + modelData.line : "")
                                  + (modelData.classification
                                     ? "\n" + modelData.classification : "")
                            wrapMode: Text.Wrap
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: !cppImportController.busy
                                 && cppImportController.previewItems.length === 0
                        text: qsTr("No C++ model declarations were discovered")
                        color: uiTheme.mutedText
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
        function onErrorAdded() { logPopup.open() }
    }

    Connections {
        target: cppImportController
        function onAttentionRequired() { logPopup.open() }
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
            cppImportController.preview(selectedFolder)
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
