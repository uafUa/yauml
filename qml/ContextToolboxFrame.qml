import QtQuick
import QtQuick.Controls

// Shared hover-safe frame for contextual diagram commands.
//
// The canvas owns target hit testing; this component owns the deliberately
// forgiving pointer journey from that target to a floating toolbox. Keeping
// those responsibilities separate lets relationship, selection, connector,
// and element toolboxes share identical show/dismiss behavior.
Item {
    id: root

    required property bool candidate
    required property point anchor
    property string candidateKey: ""
    property string placement: "top"
    property bool gestureActive: false
    property bool trackAnchorWhileShown: false
    property int showDelay: 300
    property int hideDelay: 450

    readonly property bool hovered: frameHover.hovered || bridgeHover.hovered
    property bool shown: false
    default property alias contentData: toolboxContent.data

    property point displayAnchor: Qt.point(0, 0)
    property string latchedCandidateKey: ""
    property string latchedPlacement: ""
    // Candidate hit testing may briefly clear while the pointer crosses from
    // the diagram into the hover bridge. Keep using the last valid placement
    // until the toolbox is actually dismissed; using the live empty value
    // would recenter the visible frame directly under the pointer.
    readonly property string effectivePlacement:
        latchedPlacement.length > 0 ? latchedPlacement : placement

    anchors.fill: parent
    z: 30

    function latchCandidate() {
        const targetChanged =
            latchedCandidateKey !== candidateKey
            || latchedPlacement !== placement
        if (targetChanged) {
            shown = false
            latchedCandidateKey = candidateKey
            latchedPlacement = placement
        }
        if (!shown || trackAnchorWhileShown)
            displayAnchor = anchor
        hideTimer.stop()
        if (!shown)
            showTimer.restart()
    }

    function scheduleHide() {
        if (!gestureActive && !frameHover.hovered && !bridgeHover.hovered)
            hideTimer.restart()
    }

    function dismiss() {
        showTimer.stop()
        hideTimer.stop()
        shown = false
    }

    onCandidateChanged: {
        if (candidate)
            latchCandidate()
        else {
            showTimer.stop()
            scheduleHide()
        }
    }
    onCandidateKeyChanged: {
        if (candidate)
            latchCandidate()
    }
    onPlacementChanged: {
        if (candidate)
            latchCandidate()
    }
    onAnchorChanged: {
        if ((candidate || shown) && (!shown || trackAnchorWhileShown))
            displayAnchor = anchor
    }
    onGestureActiveChanged: {
        if (gestureActive) {
            hideTimer.stop()
            shown = true
        } else if (!candidate) {
            scheduleHide()
        }
    }
    Component.onCompleted: {
        if (candidate)
            latchCandidate()
    }

    Timer {
        id: showTimer
        interval: root.showDelay
        onTriggered: {
            if (root.candidate && !root.gestureActive)
                root.shown = true
        }
    }

    Timer {
        id: hideTimer
        interval: root.hideDelay
        onTriggered: root.shown = false
    }

    // This non-clickable bridge prevents the toolbox disappearing while the
    // pointer crosses the visual gap from the diagram target to the frame.
    Item {
        id: hoverBridge
        visible: root.shown && !root.gestureActive
        x: root.effectivePlacement === "left"
           ? toolboxFrame.x + toolboxFrame.width
           : root.effectivePlacement === "right"
             ? root.displayAnchor.x + 2
             : Math.min(root.displayAnchor.x, toolboxFrame.x) - 4
        y: root.effectivePlacement === "top"
           ? toolboxFrame.y + toolboxFrame.height
           : root.effectivePlacement === "bottom"
             ? root.displayAnchor.y + 2
             : Math.min(root.displayAnchor.y, toolboxFrame.y) - 4
        width: root.effectivePlacement === "left"
               ? Math.max(1, root.displayAnchor.x - x - 2)
               : root.effectivePlacement === "right"
                 ? Math.max(1, toolboxFrame.x - x)
                 : Math.max(root.displayAnchor.x,
                            toolboxFrame.x + toolboxFrame.width) - x + 4
        height: root.effectivePlacement === "top"
                ? Math.max(1, root.displayAnchor.y - y - 2)
                : root.effectivePlacement === "bottom"
                  ? Math.max(1, toolboxFrame.y - y)
                  : Math.max(root.displayAnchor.y,
                             toolboxFrame.y + toolboxFrame.height) - y + 4

        HoverHandler {
            id: bridgeHover
            onHoveredChanged: {
                if (hovered)
                    hideTimer.stop()
                else
                    root.scheduleHide()
            }
        }
    }

    Rectangle {
        id: toolboxFrame

        readonly property real gap: 10
        readonly property real proposedX:
            root.effectivePlacement === "left"
            ? root.displayAnchor.x - width - gap
            : root.effectivePlacement === "right"
              ? root.displayAnchor.x + gap
              : root.displayAnchor.x - width / 2
        readonly property real proposedY:
            root.effectivePlacement === "top"
            ? root.displayAnchor.y - height - gap
            : root.effectivePlacement === "bottom"
              ? root.displayAnchor.y + gap
              : root.displayAnchor.y - height / 2

        x: Math.max(6, Math.min(root.width - width - 6, proposedX))
        y: Math.max(6, Math.min(root.height - height - 6, proposedY))
        width: toolboxContent.implicitWidth + 10
        height: toolboxContent.implicitHeight + 8
        radius: 6
        color: uiTheme.surface
        border.color: uiTheme.overlayBorder
        border.width: 1
        opacity: root.gestureActive ? 0.45 : root.shown ? 1 : 0
        visible: opacity > 0
        enabled: root.shown || root.gestureActive

        Behavior on opacity {
            NumberAnimation { duration: 140 }
        }

        HoverHandler {
            id: frameHover
            onHoveredChanged: {
                if (hovered)
                    hideTimer.stop()
                else
                    root.scheduleHide()
            }
        }

        Column {
            id: toolboxContent
            anchors.centerIn: parent
            spacing: 2
        }
    }
}
