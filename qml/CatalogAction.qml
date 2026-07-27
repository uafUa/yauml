import QtQuick.Controls

// An Action whose icon is resolved from the project-wide icon catalog.
// Keeping this lookup here makes newly assigned SVGs effective without
// touching each menu or toolbar that consumes the action.
Action {
    required property string catalogId

    icon.source: iconRegistry.actionIcon(catalogId)
    icon.width: iconRegistry.defaultSize
    icon.height: iconRegistry.defaultSize
}
