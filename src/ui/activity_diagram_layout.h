#pragma once

#include "core/activity_diagram.h"

#include <QHash>
#include <QPolygonF>
#include <QRectF>

namespace yauml::ui {

struct ActivityNodeLayout {
  QString id;
  ActivityNodeType type;
  QString name;
  QRectF bounds;
  QPolygonF outline;
};

struct ActivityFlowLayout {
  QString id;
  QString guard;
  QList<QPointF> points;
  QRectF labelBounds;
};

struct ActivityDiagramLayout {
  QList<ActivityNodeLayout> nodes;
  QList<ActivityFlowLayout> flows;
  QRectF contentBounds;
};

ActivityDiagramLayout
buildActivityDiagramLayout(const ActivityDiagramData &activity,
                           const QHash<QString, QRectF> &previewGeometry = {});
QRectF activityNodeBounds(ActivityNodeType type, const QPointF &center);
const ActivityNodeLayout *hitActivityNode(const ActivityDiagramLayout &layout,
                                          const QPointF &point);
const ActivityFlowLayout *hitActivityFlow(const ActivityDiagramLayout &layout,
                                          const QPointF &point,
                                          qreal tolerance);
QPointF activityBoundaryPoint(const ActivityNodeLayout &node,
                              const QPointF &toward);

} // namespace yauml::ui
