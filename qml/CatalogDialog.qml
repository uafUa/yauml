import QtQuick
import QtQuick.Controls

// Applies catalog icons to Qt-created standard dialog buttons. Specific
// dialogs can override a role's catalog ID while retaining generic defaults.
Dialog {
    id: root

    property string okCatalogId: "dialog.confirm"
    property string cancelCatalogId: "dialog.cancel"
    property string closeCatalogId: "dialog.close"
    property string applyCatalogId: "dialog.apply"
    property string saveCatalogId: "dialog.save"
    property string discardCatalogId: "dialog.discard"
    property string yesCatalogId: "dialog.yes"
    property string noCatalogId: "dialog.no"

    function applyCatalogIcon(role, catalogId) {
        if (!catalogId)
            return
        const button = standardButton(role)
        if (!button)
            return
        button.icon.source = iconRegistry.actionIcon(catalogId)
        button.icon.width = iconRegistry.defaultSize
        button.icon.height = iconRegistry.defaultSize
    }

    function refreshStandardButtonIcons() {
        applyCatalogIcon(Dialog.Ok, okCatalogId)
        applyCatalogIcon(Dialog.Cancel, cancelCatalogId)
        applyCatalogIcon(Dialog.Close, closeCatalogId)
        applyCatalogIcon(Dialog.Apply, applyCatalogId)
        applyCatalogIcon(Dialog.Save, saveCatalogId)
        applyCatalogIcon(Dialog.Discard, discardCatalogId)
        applyCatalogIcon(Dialog.Yes, yesCatalogId)
        applyCatalogIcon(Dialog.No, noCatalogId)
    }

    Connections {
        target: root
        function onOpened() {
            root.refreshStandardButtonIcons()
        }
    }
}
