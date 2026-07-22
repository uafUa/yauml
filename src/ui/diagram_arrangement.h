#pragma once

#include <QList>
#include <QRectF>
#include <QString>
#include <optional>

namespace uuml::ui {

// Pure geometry operations used by the interactive canvas and its tests.
// Keeping these rules independent of QML and ProjectController makes their
// reference-object and distribution behavior explicit and deterministic.
enum class ArrangementOperation {
  AlignLeft,
  AlignHorizontalCenter,
  AlignRight,
  AlignTop,
  AlignVerticalCenter,
  AlignBottom,
  MatchWidth,
  MatchHeight,
  MatchSize,
  DistributeHorizontally,
  DistributeVertically
};

struct DiagramNodeGeometry {
  QString id;
  QRectF geometry;

  bool operator==(const DiagramNodeGeometry &) const = default;
};

std::optional<ArrangementOperation>
arrangementOperationFromKey(const QString &key);
QString arrangementDescription(ArrangementOperation operation);

// Alignment uses the selected set's outer bounds. Size matching uses the last
// node in selection order as the reference. Distribution keeps the first
// spatial node fixed and uses the smallest positive adjacent edge-to-edge gap;
// the supplied fallback is used when all selected nodes overlap or touch.
QList<DiagramNodeGeometry>
arrangeDiagramNodes(const QList<DiagramNodeGeometry> &nodes,
                    ArrangementOperation operation,
                    qreal fallbackDistributionGap);

} // namespace uuml::ui
