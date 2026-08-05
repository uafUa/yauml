#pragma once

#include "core/project_data.h"

#include <QPointF>
#include <QRectF>
#include <QVector>

namespace yauml::ui {

// A route contains both the visible polyline and the positions of persisted
// user bend points within that polyline. Orthogonal routing may introduce
// additional automatic elbows, which deliberately have no editing handle.
struct ConnectorRoute {
  QVector<QPointF> points;
  QVector<int> bendPointRouteIndices;
};

ConnectorRoute
buildConnectorRoute(const QPointF &source, const QVector<QPointF> &bendPoints,
                    const QPointF &target, ConnectorRouting routing,
                    ConnectorSide sourceSide, ConnectorSide targetSide);

// Input for an explicit obstacle-routing operation. Endpoint bounds are kept
// separate from ordinary obstacles because the route must leave and enter
// those rectangles through the existing attachment points.
struct OrthogonalObstacleRoutingRequest {
  QPointF source;
  QPointF target;
  QRectF sourceBounds;
  QRectF targetBounds;
  ConnectorSide sourceSide = ConnectorSide::Automatic;
  ConnectorSide targetSide = ConnectorSide::Automatic;
  QVector<QRectF> obstacles;
  QVector<QVector<QPointF>> occupiedRoutes;
  // Minimum straight lead between an attachment and its first/last bend.
  // This is independent from obstacle clearance so diagrams can use generous
  // readable endpoint stubs without pushing every route far from every node.
  qreal endpointClearance = 12.0;
  qreal clearance = 12.0;
  qreal bendPenalty = 24.0;
  qreal crossingPenalty = 160.0;
  qreal sharedSegmentPenalty = 2.0;
};

// Returns the complete source-to-target polyline, or an empty vector when no
// route satisfying the requested clearance exists. The sparse rectilinear
// visibility graph is independent of view zoom and therefore deterministic
// for persisted diagram geometry.
QVector<QPointF> routeOrthogonallyAroundObstacles(
    const OrthogonalObstacleRoutingRequest &request);

// Separating structural route quality from its optional weighted cost keeps
// automatic routing visually stable on dense diagrams. Bend count and length
// can be compared before connector-crossing penalties, so avoiding an existing
// line never justifies a needlessly complicated detour.
struct OrthogonalRouteMetrics {
  bool valid = false;
  int bendCount = 0;
  qreal length = 0.0;
  qreal conflictCost = 0.0;
};

OrthogonalRouteMetrics
orthogonalRouteMetrics(const QVector<QPointF> &points,
                       const QVector<QVector<QPointF>> &occupiedRoutes,
                       qreal crossingPenalty, qreal sharedSegmentPenalty);

// Scores a complete orthogonal polyline with the same terms used by the
// visibility graph. Exposing the score lets endpoint optimization compare
// routes produced for different side pairs without duplicating cost rules.
qreal orthogonalRouteCost(const QVector<QPointF> &points,
                          const QVector<QVector<QPointF>> &occupiedRoutes,
                          qreal bendPenalty, qreal crossingPenalty,
                          qreal sharedSegmentPenalty);

} // namespace yauml::ui
