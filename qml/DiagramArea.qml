import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    readonly property string diagramMimeType: "application/x-uuml-diagram-id"
    required property string hostId
    property int modelRevision: 0
    property var diagramIds: {
        const ignored = workspaceController.revision
        return workspaceController.diagramIdsForHost(hostId)
    }
    property string currentDiagramId: diagramIds.length > 0 ? diagramIds[0] : ""
    readonly property bool containsActiveDiagram:
        diagramIds.indexOf(workspaceController.activeDiagramId) >= 0
    color: uiTheme.windowBackground
    border.color: uiTheme.accent
    border.width: containsActiveDiagram ? 2 : 0

    function diagramIdFromDrop(drop) {
        if (drop.source && drop.source.diagramId)
            return drop.source.diagramId
        if (drop.formats.indexOf(root.diagramMimeType) >= 0)
            return drop.getDataAsString(root.diagramMimeType)
        return ""
    }

    function acceptDiagramDrop(drop) {
        const diagramId = root.diagramIdFromDrop(drop)
        if (!diagramId)
            return
        const currentHost = workspaceController.hostForDiagram(diagramId)
        // A content-area drop moves the diagram between windows. Preserve its
        // position for a same-window drop and append it in a different host.
        const insertionIndex = currentHost === root.hostId
                ? root.diagramIds.indexOf(diagramId) : root.diagramIds.length
        workspaceController.moveDiagram(diagramId, root.hostId, insertionIndex)
        root.currentDiagramId = diagramId
        drop.acceptProposedAction()
    }

    onDiagramIdsChanged: {
        if (diagramIds.indexOf(currentDiagramId) < 0)
            currentDiagramId = diagramIds.length > 0 ? diagramIds[0] : ""
    }

    Connections {
        target: projectController
        function onStateChanged() { root.modelRevision++ }
    }

    Connections {
        target: workspaceController
        function onActiveDiagramIdChanged() {
            if (root.diagramIds.indexOf(workspaceController.activeDiagramId) >= 0)
                root.currentDiagramId = workspaceController.activeDiagramId
        }
    }

    DropArea {
        id: contentDropArea
        anchors.fill: parent
        // Native drags expose MIME formats to DropArea.keys. Using the same
        // value here and in Drag.mimeData keeps cross-window drops eligible.
        keys: [root.diagramMimeType]
        onDropped: function(drop) {
            root.acceptDiagramDrop(drop)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: tabStrip
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: uiTheme.tabStrip
            border.color: uiTheme.tabStripBorder

            Flickable {
                id: tabsFlickable
                anchors.fill: parent
                contentWidth: tabsRow.implicitWidth
                contentHeight: height
                clip: true

                Row {
                    id: tabsRow
                    height: parent.height

                    Repeater {
                        id: tabsRepeater
                        model: root.diagramIds

                        Rectangle {
                            id: tab
                            required property var modelData
                            property string diagramId: modelData
                            width: Math.max(150, tabLabel.implicitWidth + 48)
                            height: tabsRow.height
                            color: workspaceController.activeDiagramId === diagramId
                                   ? uiTheme.activeTab
                                   : root.currentDiagramId === diagramId ? uiTheme.surface : uiTheme.inactiveTab
                            border.color: uiTheme.controlBorder

                            Label {
                                id: tabLabel
                                anchors.left: parent.left
                                anchors.right: detachButton.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 12
                                text: {
                                    const ignored = root.modelRevision
                                    return projectController.diagramName(tab.diagramId)
                                }
                                elide: Text.ElideRight
                                font.bold: root.currentDiagramId === tab.diagramId
                                           || workspaceController.activeDiagramId === tab.diagramId
                            }

                            ToolButton {
                                id: detachButton
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 32
                                height: 32
                                text: "↗"
                                visible: root.hostId === workspaceController.mainHostId
                                         || root.diagramIds.length > 1
                                onClicked: {
                                    const point = tab.mapToGlobal(tab.width / 2, tab.height)
                                    workspaceController.detachDiagram(tab.diagramId,
                                                                      point.x, point.y)
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Detach diagram")
                            }

                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    root.currentDiagramId = tab.diagramId
                                    workspaceController.activeDiagramId = tab.diagramId
                                }
                            }

                            DragHandler {
                                id: dragHandler
                                target: null
                                grabPermissions: PointerHandler.CanTakeOverFromAnything
                                onActiveChanged: {
                                    if (active) {
                                        root.currentDiagramId = tab.diagramId
                                        workspaceController.activeDiagramId = tab.diagramId
                                        workspaceController.startDiagramDrag(tab.diagramId)
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: function(mouse) {
                                    root.currentDiagramId = tab.diagramId
                                    workspaceController.activeDiagramId = tab.diagramId
                                    tabMenu.x = mouse.x
                                    tabMenu.y = mouse.y
                                    tabMenu.open()
                                }
                            }

                            Menu {
                                id: tabMenu
                                MenuItem {
                                    text: qsTr("Detach to new window")
                                    enabled: root.hostId === workspaceController.mainHostId
                                             || root.diagramIds.length > 1
                                    onTriggered: {
                                        const point = tab.mapToGlobal(tab.width / 2,
                                                                      tab.height)
                                        workspaceController.detachDiagram(tab.diagramId,
                                                                          point.x,
                                                                          point.y)
                                    }
                                }
                                MenuItem {
                                    text: qsTr("Return to main window")
                                    visible: root.hostId !== workspaceController.mainHostId
                                    onTriggered: workspaceController.returnDiagramToMain(tab.diagramId)
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("Delete diagram")
                                    onTriggered: projectController.deleteDiagram(tab.diagramId)
                                }
                            }
                        }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: Math.max(0, root.diagramIds.indexOf(root.currentDiagramId))

            Repeater {
                model: root.diagramIds
                DiagramView {
                    required property var modelData
                    diagramId: modelData
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: root.diagramIds.length === 0
        text: qsTr("Drag a diagram tab here")
        color: uiTheme.emptyStateText
        font.pixelSize: 18
    }
}
