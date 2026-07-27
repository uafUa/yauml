import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A lightweight multi-select field for semantic stereotype assignments.
// Checkbox edits stay local while the dropdown is open and are submitted
// together, so one user interaction produces one undoable model command.
Button {
    id: root

    property string targetKind: ""
    property string targetId: ""
    property string summaryText: qsTr("None")
    property string activeTargetKind: ""
    property string activeTargetId: ""
    property bool showField: true
    signal manageRequested()

    visible: showField
    enabled: targetId.length > 0
             && (targetKind === "element"
                 || targetKind === "relationship")
    text: summaryText + "  ▾"
    ToolTip.visible: hovered
    ToolTip.text: qsTr("Choose stereotypes")
    Accessible.name: qsTr("Stereotypes: %1").arg(summaryText)
    contentItem: Label {
        text: root.text
        font: root.font
        color: root.enabled ? root.palette.buttonText
                            : root.palette.placeholderText
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    function displayText(kind, objectId) {
        if (objectId.length === 0)
            return qsTr("None")
        const ids = projectController.stereotypeIdsForObject(kind, objectId)
        const names = []
        for (let index = 0; index < ids.length; ++index) {
            const definition = projectController.stereotypeDefinition(ids[index])
            if (definition && definition.name)
                names.push(definition.name)
        }
        return names.length > 0 ? "«" + names.join(", ") + "»" : qsTr("None")
    }

    function refreshSummary() {
        summaryText = displayText(targetKind, targetId)
    }

    function populate(kind, objectId) {
        assignmentModel.clear()
        const assigned = projectController.stereotypeIdsForObject(kind, objectId)
        const available = projectController.applicableStereotypes(kind, objectId)
        for (let index = 0; index < available.length; ++index) {
            const definition = available[index]
            assignmentModel.append({
                "stereotypeId": definition.id,
                "stereotypeName": definition.name,
                "assigned": assigned.indexOf(definition.id) >= 0
            })
        }
        assignmentPopup.dirty = false
        assignmentPopup.cancelled = false
        assignmentPopup.manageAfterClose = false
    }

    function selectedIds() {
        const result = []
        for (let index = 0; index < assignmentModel.count; ++index) {
            const entry = assignmentModel.get(index)
            if (entry.assigned)
                result.push(entry.stereotypeId)
        }
        return result
    }

    function commitAssignments() {
        if (!assignmentPopup.dirty || assignmentPopup.cancelled)
            return
        projectController.assignStereotypes(activeTargetKind,
                                            activeTargetId,
                                            selectedIds())
    }

    function positionAndOpen(anchorItem, anchorX, anchorY, kind, objectId) {
        if (!anchorItem || objectId.length === 0)
            return
        activeTargetKind = kind
        activeTargetId = objectId
        populate(kind, objectId)

        const point = anchorItem.mapToItem(assignmentPopup.parent,
                                           anchorX, anchorY)
        assignmentPopup.requestedX = point.x
        assignmentPopup.requestedY = point.y
        assignmentPopup.reposition()
        assignmentPopup.open()
        // Delegate creation can change contentHeight after opening.
        Qt.callLater(function() { assignmentPopup.reposition() })
    }

    function openBelow() {
        positionAndOpen(root, 0, root.height, targetKind, targetId)
    }

    function openAt(anchorItem, anchorX, anchorY, kind, objectId) {
        positionAndOpen(anchorItem, anchorX, anchorY, kind, objectId)
    }

    function cancelDropdown() {
        assignmentPopup.cancelled = true
        assignmentPopup.close()
    }

    onClicked: openBelow()
    onTargetKindChanged: refreshSummary()
    onTargetIdChanged: refreshSummary()
    Component.onCompleted: refreshSummary()

    Connections {
        target: projectController
        function onStateChanged() { root.refreshSummary() }
    }

    ListModel { id: assignmentModel }

    Popup {
        id: assignmentPopup

        property bool dirty: false
        property bool cancelled: false
        property bool manageAfterClose: false
        property real requestedX: 0
        property real requestedY: 0

        parent: Overlay.overlay
        width: Math.min(340, parent.width - 16)
        height: Math.min(390, Math.max(150, assignmentList.contentHeight + 104))
        modal: false
        focus: true
        closePolicy: Popup.CloseOnPressOutside
        padding: 0

        background: Rectangle {
            color: uiTheme.surface
            border.color: uiTheme.overlayBorder
            radius: 4
        }

        function reposition() {
            const margin = 8
            x = Math.max(margin,
                         Math.min(requestedX,
                                  parent.width - width - margin))
            y = Math.max(margin,
                         Math.min(requestedY,
                                  parent.height - height - margin))
        }

        onClosed: {
            root.commitAssignments()
            root.refreshSummary()
            if (manageAfterClose)
                root.manageRequested()
        }
        onOpened: popupContent.forceActiveFocus()

        contentItem: ColumnLayout {
            id: popupContent
            focus: true
            spacing: 0

            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape) {
                    assignmentPopup.cancelled = true
                    assignmentPopup.close()
                    event.accepted = true
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 10
                Layout.bottomMargin: 6
                text: qsTr("Stereotypes")
                font.bold: true
            }

            ListView {
                id: assignmentList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: assignmentModel
                ScrollBar.vertical: ScrollBar {}

                delegate: CheckDelegate {
                    required property int index
                    required property string stereotypeName
                    required property bool assigned

                    width: ListView.view.width
                    text: qsTr("«%1»").arg(stereotypeName)
                    checked: assigned
                    onToggled: {
                        assignmentModel.setProperty(index, "assigned", checked)
                        assignmentPopup.dirty = true
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: assignmentModel.count === 0
                    color: uiTheme.mutedText
                    text: qsTr("No applicable stereotypes")
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: uiTheme.controlBorder
            }

            Button {
                Layout.fillWidth: true
                Layout.margins: 6
                flat: true
                text: qsTr("Manage catalogue…")
                onClicked: {
                    assignmentPopup.manageAfterClose = true
                    assignmentPopup.close()
                }
            }
        }
    }
}
