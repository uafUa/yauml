#include "ui/connector_routing.h"

#include <QtMath>

namespace yauml::ui {
namespace {

enum class Axis { Horizontal, Vertical };

Axis opposite(Axis axis) {
  return axis == Axis::Horizontal ? Axis::Vertical : Axis::Horizontal;
}

Axis axisForSide(ConnectorSide side, const QPointF &from, const QPointF &to) {
  switch (side) {
  case ConnectorSide::Left:
  case ConnectorSide::Right:
    return Axis::Horizontal;
  case ConnectorSide::Top:
  case ConnectorSide::Bottom:
    return Axis::Vertical;
  case ConnectorSide::Automatic:
    return qAbs(to.x() - from.x()) >= qAbs(to.y() - from.y()) ? Axis::Horizontal
                                                              : Axis::Vertical;
  }
  return Axis::Horizontal;
}

bool samePoint(const QPointF &left, const QPointF &right) {
  return qFuzzyCompare(left.x(), right.x()) &&
         qFuzzyCompare(left.y(), right.y());
}

void appendDistinct(QVector<QPointF> &points, const QPointF &point) {
  if (points.isEmpty() || !samePoint(points.constLast(), point))
    points.append(point);
}

void appendOrthogonalLeg(QVector<QPointF> &points, const QPointF &target,
                         Axis outgoingAxis, Axis incomingAxis) {
  const QPointF source = points.constLast();
  if (samePoint(source, target))
    return;

  // An already axis-aligned leg is the minimal valid route. The visual tangent
  // is unambiguous even when the requested axes differ at a zero-length elbow.
  if (qFuzzyCompare(source.x(), target.x()) ||
      qFuzzyCompare(source.y(), target.y())) {
    appendDistinct(points, target);
    return;
  }

  if (outgoingAxis == Axis::Horizontal && incomingAxis == Axis::Vertical) {
    appendDistinct(points, {target.x(), source.y()});
  } else if (outgoingAxis == Axis::Vertical &&
             incomingAxis == Axis::Horizontal) {
    appendDistinct(points, {source.x(), target.y()});
  } else if (outgoingAxis == Axis::Horizontal) {
    const qreal middleX = (source.x() + target.x()) / 2.0;
    appendDistinct(points, {middleX, source.y()});
    appendDistinct(points, {middleX, target.y()});
  } else {
    const qreal middleY = (source.y() + target.y()) / 2.0;
    appendDistinct(points, {source.x(), middleY});
    appendDistinct(points, {target.x(), middleY});
  }
  appendDistinct(points, target);
}

} // namespace

ConnectorRoute
buildConnectorRoute(const QPointF &source, const QVector<QPointF> &bendPoints,
                    const QPointF &target, ConnectorRouting routing,
                    ConnectorSide sourceSide, ConnectorSide targetSide) {
  ConnectorRoute route;
  route.points.reserve(bendPoints.size() * 3 + 4);
  route.bendPointRouteIndices.reserve(bendPoints.size());
  route.points.append(source);

  if (routing == ConnectorRouting::Straight) {
    for (const auto &bendPoint : bendPoints) {
      appendDistinct(route.points, bendPoint);
      route.bendPointRouteIndices.append(route.points.size() - 1);
    }
    appendDistinct(route.points, target);
    return route;
  }

  const QPointF firstTarget =
      bendPoints.isEmpty() ? target : bendPoints.constFirst();
  const Axis sourceAxis = axisForSide(sourceSide, source, firstTarget);
  Axis outgoingAxis = sourceAxis;
  for (const auto &bendPoint : bendPoints) {
    // A persisted point remains an explicit editing handle. Automatic elbows
    // are inserted around it so arbitrary user movement cannot create a
    // diagonal segment in an orthogonal connector.
    const Axis incomingAxis = opposite(outgoingAxis);
    appendOrthogonalLeg(route.points, bendPoint, outgoingAxis, incomingAxis);
    route.bendPointRouteIndices.append(route.points.size() - 1);
    outgoingAxis = opposite(incomingAxis);
  }

  const QPointF finalSource = route.points.constLast();
  const Axis targetAxis = axisForSide(targetSide, target, finalSource);
  appendOrthogonalLeg(route.points, target, outgoingAxis, targetAxis);
  return route;
}

} // namespace yauml::ui
