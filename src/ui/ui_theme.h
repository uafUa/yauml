#pragma once

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

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
  QColor alignmentGuide;
  QColor selectionOverlay;
  QColor connector;
  QColor containerFill;
  QColor containerHeaderFill;
  QColor containerBorder;
  QColor containerTitleText;
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

// Returns an immutable snapshot. Theme changes originate on the GUI thread,
// while scene-graph rendering may read the palette on the render thread.
UiPalette uiPalette();

// QObject facade for QML. Native C++ code reads the same UiPalette directly,
// keeping every literal and every semantic role in one implementation.
class UiTheme final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QColor accent READ accent NOTIFY paletteChanged)
  Q_PROPERTY(QColor surface READ surface NOTIFY paletteChanged)
  Q_PROPERTY(
      QColor windowBackground READ windowBackground NOTIFY paletteChanged)
  Q_PROPERTY(QColor panelHeader READ panelHeader NOTIFY paletteChanged)
  Q_PROPERTY(QColor hoverBackground READ hoverBackground NOTIFY paletteChanged)
  Q_PROPERTY(QColor controlBorder READ controlBorder NOTIFY paletteChanged)
  Q_PROPERTY(QColor overlayBorder READ overlayBorder NOTIFY paletteChanged)
  Q_PROPERTY(QColor bodyText READ bodyText NOTIFY paletteChanged)
  Q_PROPERTY(QColor nodeTitleText READ nodeTitleText NOTIFY paletteChanged)
  Q_PROPERTY(QColor mutedText READ mutedText NOTIFY paletteChanged)
  Q_PROPERTY(QColor emptyStateText READ emptyStateText NOTIFY paletteChanged)
  Q_PROPERTY(QColor zoomText READ zoomText NOTIFY paletteChanged)
  Q_PROPERTY(QColor tabStrip READ tabStrip NOTIFY paletteChanged)
  Q_PROPERTY(QColor tabStripBorder READ tabStripBorder NOTIFY paletteChanged)
  Q_PROPERTY(QColor activeTab READ activeTab NOTIFY paletteChanged)
  Q_PROPERTY(QColor inactiveTab READ inactiveTab NOTIFY paletteChanged)
  Q_PROPERTY(QColor badgeBackground READ badgeBackground NOTIFY paletteChanged)
  Q_PROPERTY(QColor badgeBorder READ badgeBorder NOTIFY paletteChanged)
  Q_PROPERTY(
      QColor warningBackground READ warningBackground NOTIFY paletteChanged)
  Q_PROPERTY(QColor warningBorder READ warningBorder NOTIFY paletteChanged)
  Q_PROPERTY(
      QColor editorBackground READ editorBackground NOTIFY paletteChanged)
  Q_PROPERTY(QColor errorRow READ errorRow NOTIFY paletteChanged)
  Q_PROPERTY(QColor warningRow READ warningRow NOTIFY paletteChanged)
  Q_PROPERTY(QColor alternateRow READ alternateRow NOTIFY paletteChanged)
  Q_PROPERTY(QColor canvasGrid READ canvasGrid NOTIFY paletteChanged)
  Q_PROPERTY(QColor alignmentGuide READ alignmentGuide NOTIFY paletteChanged)
  Q_PROPERTY(
      QColor selectionOverlay READ selectionOverlay NOTIFY paletteChanged)
  Q_PROPERTY(QColor connector READ connector NOTIFY paletteChanged)
  Q_PROPERTY(QColor containerFill READ containerFill NOTIFY paletteChanged)
  Q_PROPERTY(
      QColor containerHeaderFill READ containerHeaderFill NOTIFY paletteChanged)
  Q_PROPERTY(QColor containerBorder READ containerBorder NOTIFY paletteChanged)
  Q_PROPERTY(
      QColor containerTitleText READ containerTitleText NOTIFY paletteChanged)
  Q_PROPERTY(QColor nodeBorder READ nodeBorder NOTIFY paletteChanged)
  Q_PROPERTY(QColor compartmentLine READ compartmentLine NOTIFY paletteChanged)
  Q_PROPERTY(
      QColor compartmentDivider READ compartmentDivider NOTIFY paletteChanged)
  Q_PROPERTY(
      QColor activeHandleFill READ activeHandleFill NOTIFY paletteChanged)
  Q_PROPERTY(QColor packageFill READ packageFill NOTIFY paletteChanged)
  Q_PROPERTY(QColor classFill READ classFill NOTIFY paletteChanged)
  Q_PROPERTY(QColor structFill READ structFill NOTIFY paletteChanged)
  Q_PROPERTY(QColor enumerationFill READ enumerationFill NOTIFY paletteChanged)
  Q_PROPERTY(QColor dragGhostBorder READ dragGhostBorder NOTIFY paletteChanged)
  Q_PROPERTY(QColor dragGhostFill READ dragGhostFill NOTIFY paletteChanged)
  Q_PROPERTY(QColor dragGhostText READ dragGhostText NOTIFY paletteChanged)
  Q_PROPERTY(QVariantList colorRoles READ colorRoles CONSTANT)

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
  QColor alignmentGuide() const;
  QColor selectionOverlay() const;
  QColor connector() const;
  QColor containerFill() const;
  QColor containerHeaderFill() const;
  QColor containerBorder() const;
  QColor containerTitleText() const;
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

  QVariantList colorRoles() const;
  Q_INVOKABLE QColor color(const QString &role) const;
  Q_INVOKABLE QColor defaultColor(const QString &role) const;
  Q_INVOKABLE QString colorText(const QColor &color) const;
  Q_INVOKABLE QString normalizeColor(const QString &text) const;
  Q_INVOKABLE void setColor(const QString &role, const QColor &color);
  Q_INVOKABLE void setColors(const QVariantMap &colors);
  Q_INVOKABLE void resetDefaultColors();

signals:
  void paletteChanged();

private:
  void load();
  void persist(const QStringList &roles) const;
};

} // namespace uuml::ui
