#include "ui/connector_routing.h"

#include <QLineF>
#include <QMap>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace yauml::ui {
namespace {

enum class Axis { Horizontal, Vertical };

constexpr qreal kGeometryEpsilon = 0.000001;

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
  return qAbs(left.x() - right.x()) <= kGeometryEpsilon &&
         qAbs(left.y() - right.y()) <= kGeometryEpsilon;
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

bool pointStrictlyInside(const QPointF &point, const QRectF &rect) {
  return point.x() > rect.left() + kGeometryEpsilon &&
         point.x() < rect.right() - kGeometryEpsilon &&
         point.y() > rect.top() + kGeometryEpsilon &&
         point.y() < rect.bottom() - kGeometryEpsilon;
}

bool pointStrictlyInsideAny(const QPointF &point,
                            const QVector<QRectF> &obstacles) {
  return std::any_of(obstacles.cbegin(), obstacles.cend(),
                     [&](const QRectF &obstacle) {
                       return pointStrictlyInside(point, obstacle);
                     });
}

bool segmentCrossesObstacleInterior(const QPointF &first, const QPointF &second,
                                    const QRectF &obstacle) {
  if (qAbs(first.y() - second.y()) <= kGeometryEpsilon) {
    const qreal y = first.y();
    if (y <= obstacle.top() + kGeometryEpsilon ||
        y >= obstacle.bottom() - kGeometryEpsilon)
      return false;
    const qreal left = std::min(first.x(), second.x());
    const qreal right = std::max(first.x(), second.x());
    return right > obstacle.left() + kGeometryEpsilon &&
           left < obstacle.right() - kGeometryEpsilon;
  }
  if (qAbs(first.x() - second.x()) <= kGeometryEpsilon) {
    const qreal x = first.x();
    if (x <= obstacle.left() + kGeometryEpsilon ||
        x >= obstacle.right() - kGeometryEpsilon)
      return false;
    const qreal top = std::min(first.y(), second.y());
    const qreal bottom = std::max(first.y(), second.y());
    return bottom > obstacle.top() + kGeometryEpsilon &&
           top < obstacle.bottom() - kGeometryEpsilon;
  }
  return true;
}

bool segmentIsClear(const QPointF &first, const QPointF &second,
                    const QVector<QRectF> &obstacles) {
  return std::none_of(
      obstacles.cbegin(), obstacles.cend(), [&](const QRectF &obstacle) {
        return segmentCrossesObstacleInterior(first, second, obstacle);
      });
}

bool pointIsOnPerimeter(const QRectF &bounds, const QPointF &point) {
  if (!bounds.isValid())
    return false;
  const bool withinX = point.x() >= bounds.left() - kGeometryEpsilon &&
                       point.x() <= bounds.right() + kGeometryEpsilon;
  const bool withinY = point.y() >= bounds.top() - kGeometryEpsilon &&
                       point.y() <= bounds.bottom() + kGeometryEpsilon;
  return (withinX && (qAbs(point.y() - bounds.top()) <= kGeometryEpsilon ||
                      qAbs(point.y() - bounds.bottom()) <= kGeometryEpsilon)) ||
         (withinY && (qAbs(point.x() - bounds.left()) <= kGeometryEpsilon ||
                      qAbs(point.x() - bounds.right()) <= kGeometryEpsilon));
}

ConnectorSide resolvedSide(ConnectorSide requested, const QRectF &bounds,
                           const QPointF &point) {
  if (requested != ConnectorSide::Automatic)
    return requested;

  const std::array<std::pair<qreal, ConnectorSide>, 4> candidates{{
      {qAbs(point.y() - bounds.top()), ConnectorSide::Top},
      {qAbs(point.x() - bounds.right()), ConnectorSide::Right},
      {qAbs(point.y() - bounds.bottom()), ConnectorSide::Bottom},
      {qAbs(point.x() - bounds.left()), ConnectorSide::Left},
  }};
  return std::min_element(candidates.cbegin(), candidates.cend(),
                          [](const auto &left, const auto &right) {
                            return left.first < right.first;
                          })
      ->second;
}

QPointF outwardNormal(ConnectorSide side) {
  switch (side) {
  case ConnectorSide::Top:
    return {0.0, -1.0};
  case ConnectorSide::Right:
    return {1.0, 0.0};
  case ConnectorSide::Bottom:
    return {0.0, 1.0};
  case ConnectorSide::Left:
    return {-1.0, 0.0};
  case ConnectorSide::Automatic:
    break;
  }
  return {};
}

QRectF expanded(const QRectF &rect, qreal clearance) {
  return rect.normalized().adjusted(-clearance, -clearance, clearance,
                                    clearance);
}

void appendObstacleIfDistinct(QVector<QRectF> &obstacles,
                              const QRectF &candidate) {
  if (!candidate.isValid() || candidate.isEmpty())
    return;
  const bool duplicate = std::any_of(
      obstacles.cbegin(), obstacles.cend(), [&](const QRectF &item) {
        return qAbs(item.left() - candidate.left()) <= kGeometryEpsilon &&
               qAbs(item.top() - candidate.top()) <= kGeometryEpsilon &&
               qAbs(item.right() - candidate.right()) <= kGeometryEpsilon &&
               qAbs(item.bottom() - candidate.bottom()) <= kGeometryEpsilon;
      });
  if (!duplicate)
    obstacles.append(candidate);
}

int appendCandidate(QVector<QPointF> &candidates, const QPointF &candidate,
                    const QVector<QRectF> &obstacles) {
  if (!std::isfinite(candidate.x()) || !std::isfinite(candidate.y()) ||
      pointStrictlyInsideAny(candidate, obstacles))
    return -1;
  for (qsizetype index = 0; index < candidates.size(); ++index) {
    if (samePoint(candidates.at(index), candidate))
      return static_cast<int>(index);
  }
  candidates.append(candidate);
  return candidates.size() - 1;
}

enum class RayDirection { Left, Right, Up, Down };

std::optional<QPointF> nearestRayProjection(const QPointF &origin,
                                            RayDirection direction,
                                            const QVector<QRectF> &obstacles) {
  qreal nearestDistance = std::numeric_limits<qreal>::infinity();
  std::optional<QPointF> result;
  for (const QRectF &obstacle : obstacles) {
    if (direction == RayDirection::Left || direction == RayDirection::Right) {
      if (origin.y() <= obstacle.top() + kGeometryEpsilon ||
          origin.y() >= obstacle.bottom() - kGeometryEpsilon)
        continue;
      const qreal boundary =
          direction == RayDirection::Left ? obstacle.right() : obstacle.left();
      const qreal distance = direction == RayDirection::Left
                                 ? origin.x() - boundary
                                 : boundary - origin.x();
      if (distance > kGeometryEpsilon && distance < nearestDistance) {
        nearestDistance = distance;
        result = QPointF(boundary, origin.y());
      }
      continue;
    }

    if (origin.x() <= obstacle.left() + kGeometryEpsilon ||
        origin.x() >= obstacle.right() - kGeometryEpsilon)
      continue;
    const qreal boundary =
        direction == RayDirection::Up ? obstacle.bottom() : obstacle.top();
    const qreal distance = direction == RayDirection::Up
                               ? origin.y() - boundary
                               : boundary - origin.y();
    if (distance > kGeometryEpsilon && distance < nearestDistance) {
      nearestDistance = distance;
      result = QPointF(origin.x(), boundary);
    }
  }
  return result;
}

qint64 coordinateKey(qreal coordinate) {
  // Diagram data is persisted with sub-pixel precision. A micro-unit key is
  // fine enough to retain that precision while grouping numerically equal
  // results produced by different rectangle calculations.
  return std::llround(coordinate * 1000000.0);
}

struct VisibilityEdge {
  int target = -1;
  qreal length = 0.0;
  qreal conflictCost = 0.0;
  Axis axis = Axis::Horizontal;
};

bool pointIsSegmentEndpoint(const QPointF &point, const QPointF &first,
                            const QPointF &second) {
  return samePoint(point, first) || samePoint(point, second);
}

qreal collinearOverlapLength(const QPointF &first, const QPointF &second,
                             const QPointF &occupiedFirst,
                             const QPointF &occupiedSecond) {
  const bool horizontal =
      qAbs(first.y() - second.y()) <= kGeometryEpsilon &&
      qAbs(occupiedFirst.y() - occupiedSecond.y()) <= kGeometryEpsilon &&
      qAbs(first.y() - occupiedFirst.y()) <= kGeometryEpsilon;
  if (horizontal) {
    const qreal overlapStart =
        std::max(std::min(first.x(), second.x()),
                 std::min(occupiedFirst.x(), occupiedSecond.x()));
    const qreal overlapEnd =
        std::min(std::max(first.x(), second.x()),
                 std::max(occupiedFirst.x(), occupiedSecond.x()));
    return qMax(0.0, overlapEnd - overlapStart);
  }

  const bool vertical =
      qAbs(first.x() - second.x()) <= kGeometryEpsilon &&
      qAbs(occupiedFirst.x() - occupiedSecond.x()) <= kGeometryEpsilon &&
      qAbs(first.x() - occupiedFirst.x()) <= kGeometryEpsilon;
  if (!vertical)
    return 0.0;
  const qreal overlapStart =
      std::max(std::min(first.y(), second.y()),
               std::min(occupiedFirst.y(), occupiedSecond.y()));
  const qreal overlapEnd =
      std::min(std::max(first.y(), second.y()),
               std::max(occupiedFirst.y(), occupiedSecond.y()));
  return qMax(0.0, overlapEnd - overlapStart);
}

qreal segmentConflictCost(const QPointF &first, const QPointF &second,
                          const QVector<QVector<QPointF>> &occupiedRoutes,
                          qreal crossingPenalty, qreal sharedSegmentPenalty) {
  qreal cost = 0.0;
  const QLineF candidate(first, second);
  for (const auto &route : occupiedRoutes) {
    for (qsizetype index = 1; index < route.size(); ++index) {
      const QPointF occupiedFirst = route.at(index - 1);
      const QPointF occupiedSecond = route.at(index);
      const qreal overlap =
          collinearOverlapLength(first, second, occupiedFirst, occupiedSecond);
      if (overlap > kGeometryEpsilon) {
        // A fixed cost prevents short shared fragments from being treated as
        // free, while the length term strongly discourages long stacked lanes.
        cost += crossingPenalty + overlap * sharedSegmentPenalty;
        continue;
      }

      QPointF intersection;
      const auto intersectionType = candidate.intersects(
          QLineF(occupiedFirst, occupiedSecond), &intersection);
      if (intersectionType != QLineF::BoundedIntersection)
        continue;
      // Fan-out from a deliberately shared snap point is valid. Intersections
      // elsewhere—including at an intermediate route vertex—remain costly.
      if (pointIsSegmentEndpoint(intersection, first, second) &&
          pointIsSegmentEndpoint(intersection, occupiedFirst, occupiedSecond))
        continue;
      cost += crossingPenalty;
    }
  }
  return cost;
}

void appendVisibilityEdges(QVector<QVector<VisibilityEdge>> &graph,
                           const QVector<QPointF> &points,
                           const QVector<int> &orderedIndices, Axis axis,
                           const QVector<QRectF> &obstacles,
                           const QVector<QVector<QPointF>> &occupiedRoutes,
                           qreal crossingPenalty, qreal sharedSegmentPenalty) {
  for (qsizetype index = 1; index < orderedIndices.size(); ++index) {
    const int firstIndex = orderedIndices.at(index - 1);
    const int secondIndex = orderedIndices.at(index);
    const QPointF &first = points.at(firstIndex);
    const QPointF &second = points.at(secondIndex);
    if (!segmentIsClear(first, second, obstacles))
      continue;
    const qreal length = axis == Axis::Horizontal
                             ? qAbs(second.x() - first.x())
                             : qAbs(second.y() - first.y());
    if (length <= kGeometryEpsilon)
      continue;
    const qreal conflictCost = segmentConflictCost(
        first, second, occupiedRoutes, crossingPenalty, sharedSegmentPenalty);
    graph[firstIndex].append({secondIndex, length, conflictCost, axis});
    graph[secondIndex].append({firstIndex, length, conflictCost, axis});
  }
}

QVector<QVector<VisibilityEdge>>
buildVisibilityGraph(const QVector<QPointF> &points,
                     const QVector<QRectF> &obstacles,
                     const QVector<QVector<QPointF>> &occupiedRoutes,
                     qreal crossingPenalty, qreal sharedSegmentPenalty) {
  QVector<QVector<VisibilityEdge>> graph(points.size());
  QMap<qint64, QVector<int>> rows;
  QMap<qint64, QVector<int>> columns;
  for (qsizetype index = 0; index < points.size(); ++index) {
    rows[coordinateKey(points.at(index).y())].append(index);
    columns[coordinateKey(points.at(index).x())].append(index);
  }

  for (auto row = rows.begin(); row != rows.end(); ++row) {
    auto &indices = row.value();
    std::sort(indices.begin(), indices.end(), [&](int left, int right) {
      if (points.at(left).x() != points.at(right).x())
        return points.at(left).x() < points.at(right).x();
      return left < right;
    });
    appendVisibilityEdges(graph, points, indices, Axis::Horizontal, obstacles,
                          occupiedRoutes, crossingPenalty,
                          sharedSegmentPenalty);
  }
  for (auto column = columns.begin(); column != columns.end(); ++column) {
    auto &indices = column.value();
    std::sort(indices.begin(), indices.end(), [&](int left, int right) {
      if (points.at(left).y() != points.at(right).y())
        return points.at(left).y() < points.at(right).y();
      return left < right;
    });
    appendVisibilityEdges(graph, points, indices, Axis::Vertical, obstacles,
                          occupiedRoutes, crossingPenalty,
                          sharedSegmentPenalty);
  }
  return graph;
}

struct PathCost {
  int bendCount = std::numeric_limits<int>::max();
  qreal length = std::numeric_limits<qreal>::infinity();
  qreal conflictCost = std::numeric_limits<qreal>::infinity();
};

bool pathCostLess(const PathCost &left, const PathCost &right) {
  if (left.bendCount != right.bendCount)
    return left.bendCount < right.bendCount;
  if (left.length != right.length)
    return left.length < right.length;
  return left.conflictCost < right.conflictCost;
}

bool samePathCost(const PathCost &left, const PathCost &right) {
  return left.bendCount == right.bendCount && left.length == right.length &&
         left.conflictCost == right.conflictCost;
}

QVector<int> leastCostPath(const QVector<QVector<VisibilityEdge>> &graph,
                           int source, int target, Axis sourceAxis,
                           Axis targetAxis) {
  constexpr int kDirectionCount = 3;
  constexpr int kHorizontalDirection = 1;
  constexpr int kVerticalDirection = 2;
  const int stateCount = graph.size() * kDirectionCount;
  QVector<PathCost> distance(stateCount);
  QVector<int> previous(stateCount, -1);
  struct QueueEntry {
    PathCost cost;
    int state = -1;
  };
  struct QueueEntryGreater {
    bool operator()(const QueueEntry &left, const QueueEntry &right) const {
      if (pathCostLess(right.cost, left.cost))
        return true;
      if (pathCostLess(left.cost, right.cost))
        return false;
      return left.state > right.state;
    }
  };
  std::priority_queue<QueueEntry, std::vector<QueueEntry>, QueueEntryGreater>
      queue;

  const auto directionForAxis = [&](Axis axis) {
    return axis == Axis::Horizontal ? kHorizontalDirection : kVerticalDirection;
  };
  const int sourceDirection = directionForAxis(sourceAxis);
  const int targetDirection = directionForAxis(targetAxis);
  const int sourceState = source * kDirectionCount + sourceDirection;
  distance[sourceState] = {0, 0.0, 0.0};
  queue.push({distance.at(sourceState), sourceState});

  while (!queue.empty()) {
    const QueueEntry current = queue.top();
    queue.pop();
    const int state = current.state;
    if (!samePathCost(current.cost, distance.at(state)))
      continue;
    const int vertex = state / kDirectionCount;
    const int incomingDirection = state % kDirectionCount;

    for (const VisibilityEdge &edge : graph.at(vertex)) {
      const int outgoingDirection = edge.axis == Axis::Horizontal
                                        ? kHorizontalDirection
                                        : kVerticalDirection;
      const int nextState = edge.target * kDirectionCount + outgoingDirection;
      PathCost candidate = current.cost;
      if (incomingDirection != outgoingDirection)
        ++candidate.bendCount;
      candidate.length += edge.length;
      candidate.conflictCost += edge.conflictCost;
      if (!pathCostLess(candidate, distance.at(nextState)))
        continue;
      distance[nextState] = candidate;
      previous[nextState] = state;
      queue.push({candidate, nextState});
    }
  }

  int targetState = -1;
  PathCost targetCost;
  for (const int incomingDirection :
       {kHorizontalDirection, kVerticalDirection}) {
    const int candidateState = target * kDirectionCount + incomingDirection;
    PathCost candidate = distance.at(candidateState);
    if (candidate.bendCount == std::numeric_limits<int>::max())
      continue;
    // The escape-to-presentation segment has the target side's axis. Counting
    // this turn prevents the route from selecting a cheap-looking path which
    // immediately hooks around beside its endpoint.
    if (incomingDirection != targetDirection)
      ++candidate.bendCount;
    if (pathCostLess(candidate, targetCost)) {
      targetCost = candidate;
      targetState = candidateState;
    }
  }
  if (targetState < 0)
    return {};
  QVector<int> reversed;
  for (int state = targetState; state >= 0; state = previous.at(state)) {
    reversed.append(state / kDirectionCount);
    if (state == sourceState)
      break;
    if (previous.at(state) < 0)
      return {};
  }
  std::reverse(reversed.begin(), reversed.end());
  return reversed;
}

QVector<QPointF> simplifiedOrthogonalPolyline(const QVector<QPointF> &input) {
  QVector<QPointF> result;
  result.reserve(input.size());
  for (const QPointF &point : input) {
    appendDistinct(result, point);
    while (result.size() >= 3) {
      const QPointF &first = result.at(result.size() - 3);
      const QPointF &middle = result.at(result.size() - 2);
      const QPointF &last = result.at(result.size() - 1);
      const bool horizontal =
          qAbs(first.y() - middle.y()) <= kGeometryEpsilon &&
          qAbs(middle.y() - last.y()) <= kGeometryEpsilon;
      const bool vertical = qAbs(first.x() - middle.x()) <= kGeometryEpsilon &&
                            qAbs(middle.x() - last.x()) <= kGeometryEpsilon;
      if (!horizontal && !vertical)
        break;
      result.remove(result.size() - 2);
    }
  }
  return result;
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

OrthogonalRouteMetrics
orthogonalRouteMetrics(const QVector<QPointF> &points,
                       const QVector<QVector<QPointF>> &occupiedRoutes,
                       qreal crossingPenalty, qreal sharedSegmentPenalty) {
  OrthogonalRouteMetrics metrics;
  if (points.size() < 2 || !std::isfinite(crossingPenalty) ||
      crossingPenalty < 0.0 || !std::isfinite(sharedSegmentPenalty) ||
      sharedSegmentPenalty < 0.0)
    return metrics;

  std::optional<Axis> previousAxis;
  for (qsizetype index = 1; index < points.size(); ++index) {
    const QPointF first = points.at(index - 1);
    const QPointF second = points.at(index);
    if (samePoint(first, second))
      continue;
    Axis axis;
    if (qAbs(first.y() - second.y()) <= kGeometryEpsilon)
      axis = Axis::Horizontal;
    else if (qAbs(first.x() - second.x()) <= kGeometryEpsilon)
      axis = Axis::Vertical;
    else
      return metrics;

    if (previousAxis && *previousAxis != axis)
      ++metrics.bendCount;
    previousAxis = axis;
    metrics.length += QLineF(first, second).length();
    metrics.conflictCost += segmentConflictCost(
        first, second, occupiedRoutes, crossingPenalty, sharedSegmentPenalty);
  }
  metrics.valid = previousAxis.has_value();
  return metrics;
}

qreal orthogonalRouteCost(const QVector<QPointF> &points,
                          const QVector<QVector<QPointF>> &occupiedRoutes,
                          qreal bendPenalty, qreal crossingPenalty,
                          qreal sharedSegmentPenalty) {
  if (!std::isfinite(bendPenalty) || bendPenalty < 0.0)
    return std::numeric_limits<qreal>::infinity();
  const OrthogonalRouteMetrics metrics = orthogonalRouteMetrics(
      points, occupiedRoutes, crossingPenalty, sharedSegmentPenalty);
  if (!metrics.valid)
    return std::numeric_limits<qreal>::infinity();
  return metrics.length + metrics.bendCount * bendPenalty +
         metrics.conflictCost;
}

QVector<QPointF> routeOrthogonallyAroundObstacles(
    const OrthogonalObstacleRoutingRequest &request) {
  if (!request.sourceBounds.isValid() || !request.targetBounds.isValid() ||
      !pointIsOnPerimeter(request.sourceBounds, request.source) ||
      !pointIsOnPerimeter(request.targetBounds, request.target) ||
      !std::isfinite(request.endpointClearance) ||
      request.endpointClearance < 0.0 || !std::isfinite(request.clearance) ||
      request.clearance < 0.0 || !std::isfinite(request.bendPenalty) ||
      request.bendPenalty < 0.0 || !std::isfinite(request.crossingPenalty) ||
      request.crossingPenalty < 0.0 ||
      !std::isfinite(request.sharedSegmentPenalty) ||
      request.sharedSegmentPenalty < 0.0)
    return {};

  const ConnectorSide sourceSide =
      resolvedSide(request.sourceSide, request.sourceBounds, request.source);
  const ConnectorSide targetSide =
      resolvedSide(request.targetSide, request.targetBounds, request.target);
  const QPointF sourceEscape =
      request.source + outwardNormal(sourceSide) * request.endpointClearance;
  const QPointF targetEscape =
      request.target + outwardNormal(targetSide) * request.endpointClearance;

  QVector<QRectF> externalObstacles;
  externalObstacles.reserve(request.obstacles.size());
  for (const QRectF &obstacle : request.obstacles)
    appendObstacleIfDistinct(externalObstacles,
                             expanded(obstacle, request.clearance));

  // Expanded endpoint bounds force the visibility path to respect the chosen
  // outgoing sides. The short endpoint-to-escape legs are added afterward and
  // are intentionally the only segments allowed inside those two bounds.
  QVector<QRectF> routingObstacles = externalObstacles;
  appendObstacleIfDistinct(
      routingObstacles,
      expanded(request.sourceBounds, request.endpointClearance));
  appendObstacleIfDistinct(
      routingObstacles,
      expanded(request.targetBounds, request.endpointClearance));

  if (pointStrictlyInsideAny(sourceEscape, routingObstacles) ||
      pointStrictlyInsideAny(targetEscape, routingObstacles))
    return {};

  QVector<QPointF> candidates;
  candidates.reserve(routingObstacles.size() * 8 + 6);
  const int sourceIndex =
      appendCandidate(candidates, sourceEscape, routingObstacles);
  const int targetIndex =
      appendCandidate(candidates, targetEscape, routingObstacles);
  if (sourceIndex < 0 || targetIndex < 0)
    return {};

  appendCandidate(candidates, {sourceEscape.x(), targetEscape.y()},
                  routingObstacles);
  appendCandidate(candidates, {targetEscape.x(), sourceEscape.y()},
                  routingObstacles);
  for (const QRectF &obstacle : routingObstacles) {
    appendCandidate(candidates, obstacle.topLeft(), routingObstacles);
    appendCandidate(candidates, obstacle.topRight(), routingObstacles);
    appendCandidate(candidates, obstacle.bottomRight(), routingObstacles);
    appendCandidate(candidates, obstacle.bottomLeft(), routingObstacles);
  }

  // Orthogonal visibility graphs need Steiner points where rays from obstacle
  // vertices meet another obstacle boundary. One projection pass is enough:
  // every projection shares its other coordinate with the originating vertex
  // and its boundary coordinate with that obstacle's two corners.
  const QVector<QPointF> projectionOrigins = candidates;
  constexpr std::array<RayDirection, 4> directions{
      RayDirection::Left, RayDirection::Right, RayDirection::Up,
      RayDirection::Down};
  for (const QPointF &origin : projectionOrigins) {
    for (RayDirection direction : directions) {
      if (const auto projection =
              nearestRayProjection(origin, direction, routingObstacles))
        appendCandidate(candidates, *projection, routingObstacles);
    }
  }

  const auto graph = buildVisibilityGraph(
      candidates, routingObstacles, request.occupiedRoutes,
      request.crossingPenalty, request.sharedSegmentPenalty);
  const QVector<int> path =
      leastCostPath(graph, sourceIndex, targetIndex,
                    axisForSide(sourceSide, request.source, sourceEscape),
                    axisForSide(targetSide, targetEscape, request.target));
  if (path.isEmpty())
    return {};

  QVector<QPointF> points;
  points.reserve(path.size() + 2);
  appendDistinct(points, request.source);
  for (int candidateIndex : path)
    appendDistinct(points, candidates.at(candidateIndex));
  appendDistinct(points, request.target);
  points = simplifiedOrthogonalPolyline(points);

  // Endpoint legs may cross an unrelated rectangle when presentations are
  // packed more tightly than the requested clearance. Fail cleanly rather
  // than persisting a route which claims to avoid obstacles but does not.
  for (qsizetype index = 1; index < points.size(); ++index) {
    if (!segmentIsClear(points.at(index - 1), points.at(index),
                        externalObstacles))
      return {};
  }
  return points;
}

} // namespace yauml::ui
