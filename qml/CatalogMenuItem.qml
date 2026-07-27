import QtQuick.Controls

// Catalog-aware menu command. Dynamic entries may share a catalog ID, such as
// the per-project style choices that all use style.assignNamed.
MenuItem {
    required property string catalogId

    icon.source: iconRegistry.actionIcon(catalogId)
    icon.width: iconRegistry.defaultSize
    icon.height: iconRegistry.defaultSize
}
