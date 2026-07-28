import QtQuick
import QtQuick.Controls

Window {
    id: root
    required property string hostId
    property bool geometryReady: false
    width: workspaceController.hostWidth(hostId)
    height: workspaceController.hostHeight(hostId)
    minimumWidth: 480
    minimumHeight: 320
    x: workspaceController.hostX(hostId)
    y: workspaceController.hostY(hostId)
    visible: true
    title: qsTr("yauml — Diagram Area")
    color: uiTheme.windowBackground

    DiagramArea {
        id: diagramArea
        anchors.fill: parent
        hostId: root.hostId
    }

    Shortcut {
        sequences: [StandardKey.Undo]
        context: Qt.WindowShortcut
        enabled: projectController.canUndo
        onActivated: projectController.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        context: Qt.WindowShortcut
        enabled: projectController.canRedo
        onActivated: projectController.redo()
    }

    onActiveChanged: {
        if (active && diagramArea.currentDiagramId.length > 0)
            workspaceController.activeDiagramId = diagramArea.currentDiagramId
    }

    function persistGeometry() {
        if (geometryReady)
            workspaceController.updateHostGeometry(hostId, x, y, width, height)
    }

    Component.onCompleted: {
        geometryReady = true
        persistGeometry()
    }
    onXChanged: persistGeometry()
    onYChanged: persistGeometry()
    onWidthChanged: persistGeometry()
    onHeightChanged: persistGeometry()

    Connections {
        target: workspaceController
        function onHostRelocationRequested(relocatedHostId, nextX, nextY) {
            if (relocatedHostId !== root.hostId)
                return
            root.x = nextX
            root.y = nextY
        }
    }

    onClosing: function(close) {
        workspaceController.closeHost(root.hostId)
        close.accepted = true
    }
}
