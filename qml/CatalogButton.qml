import QtQuick.Controls

// Catalog-aware push button for property panels and project dialogs.
Button {
    required property string catalogId

    icon.source: iconRegistry.actionIcon(catalogId)
    icon.width: iconRegistry.defaultSize
    icon.height: iconRegistry.defaultSize
}
