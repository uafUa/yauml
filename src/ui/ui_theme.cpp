#include "ui/ui_theme.h"

namespace uuml::ui {

const UiPalette &uiPalette() {
  // This is the only source-code location containing product color literals.
  // Keeping construction local also avoids static initialization order issues.
  static const UiPalette palette = [] {
    UiPalette value;
    value.accent = QColor(QStringLiteral("#1769d2"));
    value.surface = QColor(QStringLiteral("#ffffff"));
    value.windowBackground = QColor(QStringLiteral("#f4f6f8"));
    value.panelHeader = QColor(QStringLiteral("#e8edf2"));
    value.hoverBackground = QColor(QStringLiteral("#edf3f8"));
    value.controlBorder = QColor(QStringLiteral("#b9c4ce"));
    value.overlayBorder = QColor(QStringLiteral("#8f9ba6"));

    value.bodyText = QColor(QStringLiteral("#263238"));
    value.nodeTitleText = QColor(QStringLiteral("#18212a"));
    value.mutedText = QColor(QStringLiteral("#64717d"));
    value.emptyStateText = QColor(QStringLiteral("#687684"));
    value.zoomText = QColor(QStringLiteral("#425466"));

    value.tabStrip = QColor(QStringLiteral("#e7ecf1"));
    value.tabStripBorder = QColor(QStringLiteral("#c5ced7"));
    value.activeTab = QColor(QStringLiteral("#d9eaff"));
    value.inactiveTab = QColor(QStringLiteral("#dfe5eb"));

    value.badgeBackground = QColor(QStringLiteral("#eaf0f5"));
    value.badgeBorder = QColor(QStringLiteral("#c6d0da"));
    value.warningBackground = QColor(QStringLiteral("#fff4ce"));
    value.warningBorder = QColor(QStringLiteral("#c89b25"));
    value.editorBackground = QColor(QStringLiteral("#ffffe8"));
    value.errorRow = QColor(QStringLiteral("#fff0f0"));
    value.warningRow = QColor(QStringLiteral("#fff8e5"));
    value.alternateRow = QColor(QStringLiteral("#f7f9fb"));

    value.canvasGrid = QColor(QStringLiteral("#dce2e8"));
    value.selectionOverlay = QColor(23, 105, 210, 28);
    value.connector = QColor(QStringLiteral("#52606d"));
    value.nodeBorder = QColor(QStringLiteral("#3f4b56"));
    value.compartmentLine = QColor(QStringLiteral("#65727e"));
    value.compartmentDivider = QColor(QStringLiteral("#c5ccd3"));
    value.activeHandleFill = QColor(QStringLiteral("#9dceff"));
    value.packageFill = QColor(QStringLiteral("#fff1c2"));
    value.classFill = QColor(QStringLiteral("#f8fbff"));
    value.structFill = QColor(QStringLiteral("#eefaf1"));
    value.enumerationFill = QColor(QStringLiteral("#f7efff"));

    value.dragGhostBorder = QColor(QStringLiteral("#8aa9cc"));
    value.dragGhostFill = QColor(QStringLiteral("#e2efff"));
    value.dragGhostText = QColor(QStringLiteral("#203548"));
    return value;
  }();
  return palette;
}

UiTheme::UiTheme(QObject *parent) : QObject(parent) {}

#define UUML_THEME_GETTER(name)                                                \
  QColor UiTheme::name() const { return uiPalette().name; }

UUML_THEME_GETTER(accent)
UUML_THEME_GETTER(surface)
UUML_THEME_GETTER(windowBackground)
UUML_THEME_GETTER(panelHeader)
UUML_THEME_GETTER(hoverBackground)
UUML_THEME_GETTER(controlBorder)
UUML_THEME_GETTER(overlayBorder)
UUML_THEME_GETTER(bodyText)
UUML_THEME_GETTER(nodeTitleText)
UUML_THEME_GETTER(mutedText)
UUML_THEME_GETTER(emptyStateText)
UUML_THEME_GETTER(zoomText)
UUML_THEME_GETTER(tabStrip)
UUML_THEME_GETTER(tabStripBorder)
UUML_THEME_GETTER(activeTab)
UUML_THEME_GETTER(inactiveTab)
UUML_THEME_GETTER(badgeBackground)
UUML_THEME_GETTER(badgeBorder)
UUML_THEME_GETTER(warningBackground)
UUML_THEME_GETTER(warningBorder)
UUML_THEME_GETTER(editorBackground)
UUML_THEME_GETTER(errorRow)
UUML_THEME_GETTER(warningRow)
UUML_THEME_GETTER(alternateRow)
UUML_THEME_GETTER(canvasGrid)
UUML_THEME_GETTER(selectionOverlay)
UUML_THEME_GETTER(connector)
UUML_THEME_GETTER(nodeBorder)
UUML_THEME_GETTER(compartmentLine)
UUML_THEME_GETTER(compartmentDivider)
UUML_THEME_GETTER(activeHandleFill)
UUML_THEME_GETTER(packageFill)
UUML_THEME_GETTER(classFill)
UUML_THEME_GETTER(structFill)
UUML_THEME_GETTER(enumerationFill)
UUML_THEME_GETTER(dragGhostBorder)
UUML_THEME_GETTER(dragGhostFill)
UUML_THEME_GETTER(dragGhostText)

#undef UUML_THEME_GETTER

} // namespace uuml::ui
