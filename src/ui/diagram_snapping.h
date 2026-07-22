#pragma once

#include "ui/diagram_arrangement.h"

#include <QLineF>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>

namespace uuml::ui {

// Options are expressed in scene units. DiagramCanvas converts the visual
// tolerance from view pixels so snapping feels consistent at every zoom level.
struct DiagramSnapOptions {
  bool snapToGrid = true;
  bool snapToAlignment = true;
  qreal gridSpacing = 20.0;
  qreal tolerance = 8.0;
};

struct DiagramSnapResult {
  QPointF delta;
  QVector<QLineF> guides;
};

struct DiagramResizeSnapResult {
  QRectF geometry;
  QVector<QLineF> guides;
};

// Adjusts one shared movement delta for the complete moving set, preserving
// relative geometry. The primary node supplies the top-left point used for
// grid snapping. Element alignment considers left/center/right and
// top/center/bottom features of every moving and stationary rectangle.
DiagramSnapResult snapDiagramMove(const QList<DiagramNodeGeometry> &moving,
                                  const QList<DiagramNodeGeometry> &stationary,
                                  const QString &primaryNodeId,
                                  const QPointF &requestedDelta,
                                  const DiagramSnapOptions &options);

// Snaps a lower-right resize while keeping the requested rectangle's top-left
// corner fixed. Only the actively dragged right and bottom edges participate;
// otherwise an already-aligned fixed edge could mask a useful resize target.
DiagramResizeSnapResult
snapDiagramBottomRightResize(const QRectF &requestedGeometry,
                             const QList<DiagramNodeGeometry> &stationary,
                             const QSizeF &minimumSize,
                             const DiagramSnapOptions &options);

} // namespace uuml::ui
