#pragma once

#include "core/project_data.h"

#include <QPointF>
#include <QVector>

namespace uuml::ui {

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

} // namespace uuml::ui
