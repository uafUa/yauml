import QtQuick.Controls

// Catalog-aware compact command used by the main and diagram toolbars.
ToolButton {
    required property string catalogId

    icon.source: iconRegistry.actionIcon(catalogId)
    icon.width: iconRegistry.defaultSize
    icon.height: iconRegistry.defaultSize
}
