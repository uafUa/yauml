#pragma once

#include <QList>
#include <QRectF>
#include <QString>
#include <optional>

namespace yauml::ui {

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

struct DiagramLayoutEdge {
  QString sourceId;
  QString targetId;

  bool operator==(const DiagramLayoutEdge &) const = default;
};

enum class AutomaticLayoutDirection { LeftToRight, TopToBottom };

struct AutomaticLayoutOptions {
  AutomaticLayoutDirection direction = AutomaticLayoutDirection::LeftToRight;
  qreal layerGap = 100.0;
  qreal itemGap = 40.0;
  qreal componentGap = 80.0;
};

std::optional<ArrangementOperation>
arrangementOperationFromKey(const QString &key);
QString arrangementDescription(ArrangementOperation operation);

// Alignment and size matching use the last node in selection order as the
// reference, allowing the user to choose the key object by selecting or
// clicking it last. Distribution is spatial: it keeps the first positioned
// node fixed and uses the smallest positive adjacent edge-to-edge gap; the
// supplied fallback is used when all selected nodes overlap or touch.
QList<DiagramNodeGeometry>
arrangeDiagramNodes(const QList<DiagramNodeGeometry> &nodes,
                    ArrangementOperation operation,
                    qreal fallbackDistributionGap);

// Produces a deterministic layered layout without changing presentation sizes.
// Strongly connected nodes share a layer, so cycles cannot make ranking
// unstable. Disconnected components are laid out independently and packed on
// shelves whose top-left remains at the input bounds' top-left.
QList<DiagramNodeGeometry>
automaticallyLayoutDiagramNodes(const QList<DiagramNodeGeometry> &nodes,
                                const QList<DiagramLayoutEdge> &edges,
                                const AutomaticLayoutOptions &options = {});

} // namespace yauml::ui
