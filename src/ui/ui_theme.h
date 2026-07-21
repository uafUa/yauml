#pragma once

#include <QColor>
#include <QObject>

namespace uuml::ui {

// Semantic palette shared by QML controls and native scene-graph rendering.
// Names describe intent so a future light/dark theme can change values without
// requiring callers to know which roles currently happen to share a color.
struct UiPalette {
  QColor accent;
  QColor surface;
  QColor windowBackground;
  QColor panelHeader;
  QColor hoverBackground;
  QColor controlBorder;
  QColor overlayBorder;

  QColor bodyText;
  QColor nodeTitleText;
  QColor mutedText;
  QColor emptyStateText;
  QColor zoomText;

  QColor tabStrip;
  QColor tabStripBorder;
  QColor activeTab;
  QColor inactiveTab;

  QColor badgeBackground;
  QColor badgeBorder;
  QColor warningBackground;
  QColor warningBorder;
  QColor editorBackground;
  QColor errorRow;
  QColor warningRow;
  QColor alternateRow;

  QColor canvasGrid;
  QColor selectionOverlay;
  QColor connector;
  QColor nodeBorder;
  QColor compartmentLine;
  QColor compartmentDivider;
  QColor activeHandleFill;
  QColor packageFill;
  QColor classFill;
  QColor structFill;
  QColor enumerationFill;

  QColor dragGhostBorder;
  QColor dragGhostFill;
  QColor dragGhostText;
};

const UiPalette &uiPalette();

// QObject facade for QML. Native C++ code reads the same UiPalette directly,
// keeping every literal and every semantic role in one implementation.
class UiTheme final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QColor accent READ accent CONSTANT)
  Q_PROPERTY(QColor surface READ surface CONSTANT)
  Q_PROPERTY(QColor windowBackground READ windowBackground CONSTANT)
  Q_PROPERTY(QColor panelHeader READ panelHeader CONSTANT)
  Q_PROPERTY(QColor hoverBackground READ hoverBackground CONSTANT)
  Q_PROPERTY(QColor controlBorder READ controlBorder CONSTANT)
  Q_PROPERTY(QColor overlayBorder READ overlayBorder CONSTANT)
  Q_PROPERTY(QColor bodyText READ bodyText CONSTANT)
  Q_PROPERTY(QColor nodeTitleText READ nodeTitleText CONSTANT)
  Q_PROPERTY(QColor mutedText READ mutedText CONSTANT)
  Q_PROPERTY(QColor emptyStateText READ emptyStateText CONSTANT)
  Q_PROPERTY(QColor zoomText READ zoomText CONSTANT)
  Q_PROPERTY(QColor tabStrip READ tabStrip CONSTANT)
  Q_PROPERTY(QColor tabStripBorder READ tabStripBorder CONSTANT)
  Q_PROPERTY(QColor activeTab READ activeTab CONSTANT)
  Q_PROPERTY(QColor inactiveTab READ inactiveTab CONSTANT)
  Q_PROPERTY(QColor badgeBackground READ badgeBackground CONSTANT)
  Q_PROPERTY(QColor badgeBorder READ badgeBorder CONSTANT)
  Q_PROPERTY(QColor warningBackground READ warningBackground CONSTANT)
  Q_PROPERTY(QColor warningBorder READ warningBorder CONSTANT)
  Q_PROPERTY(QColor editorBackground READ editorBackground CONSTANT)
  Q_PROPERTY(QColor errorRow READ errorRow CONSTANT)
  Q_PROPERTY(QColor warningRow READ warningRow CONSTANT)
  Q_PROPERTY(QColor alternateRow READ alternateRow CONSTANT)
  Q_PROPERTY(QColor canvasGrid READ canvasGrid CONSTANT)
  Q_PROPERTY(QColor selectionOverlay READ selectionOverlay CONSTANT)
  Q_PROPERTY(QColor connector READ connector CONSTANT)
  Q_PROPERTY(QColor nodeBorder READ nodeBorder CONSTANT)
  Q_PROPERTY(QColor compartmentLine READ compartmentLine CONSTANT)
  Q_PROPERTY(QColor compartmentDivider READ compartmentDivider CONSTANT)
  Q_PROPERTY(QColor activeHandleFill READ activeHandleFill CONSTANT)
  Q_PROPERTY(QColor packageFill READ packageFill CONSTANT)
  Q_PROPERTY(QColor classFill READ classFill CONSTANT)
  Q_PROPERTY(QColor structFill READ structFill CONSTANT)
  Q_PROPERTY(QColor enumerationFill READ enumerationFill CONSTANT)
  Q_PROPERTY(QColor dragGhostBorder READ dragGhostBorder CONSTANT)
  Q_PROPERTY(QColor dragGhostFill READ dragGhostFill CONSTANT)
  Q_PROPERTY(QColor dragGhostText READ dragGhostText CONSTANT)

public:
  explicit UiTheme(QObject *parent = nullptr);

  QColor accent() const;
  QColor surface() const;
  QColor windowBackground() const;
  QColor panelHeader() const;
  QColor hoverBackground() const;
  QColor controlBorder() const;
  QColor overlayBorder() const;
  QColor bodyText() const;
  QColor nodeTitleText() const;
  QColor mutedText() const;
  QColor emptyStateText() const;
  QColor zoomText() const;
  QColor tabStrip() const;
  QColor tabStripBorder() const;
  QColor activeTab() const;
  QColor inactiveTab() const;
  QColor badgeBackground() const;
  QColor badgeBorder() const;
  QColor warningBackground() const;
  QColor warningBorder() const;
  QColor editorBackground() const;
  QColor errorRow() const;
  QColor warningRow() const;
  QColor alternateRow() const;
  QColor canvasGrid() const;
  QColor selectionOverlay() const;
  QColor connector() const;
  QColor nodeBorder() const;
  QColor compartmentLine() const;
  QColor compartmentDivider() const;
  QColor activeHandleFill() const;
  QColor packageFill() const;
  QColor classFill() const;
  QColor structFill() const;
  QColor enumerationFill() const;
  QColor dragGhostBorder() const;
  QColor dragGhostFill() const;
  QColor dragGhostText() const;
};

} // namespace uuml::ui
