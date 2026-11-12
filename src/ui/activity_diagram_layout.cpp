#include "ui/activity_diagram_layout.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QLineF>
#include <QPainterPath>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace yauml::ui {

QRectF activityNodeBounds(ActivityNodeType type, const QPointF &center) {
  QSizeF size;
  switch (type) {
  case ActivityNodeType::Action:
    size = {164.0, 52.0};
    break;
  case ActivityNodeType::Initial:
    size = {22.0, 22.0};
    break;
  case ActivityNodeType::Final:
    size = {30.0, 30.0};
    break;
  case ActivityNodeType::Decision:
  case ActivityNodeType::Merge:
    size = {40.0, 40.0};
    break;
  case ActivityNodeType::Fork:
  case ActivityNodeType::Join:
    size = {112.0, 8.0};
    break;
  }
  QRectF bounds(QPointF(), size);
  bounds.moveCenter(center);
  return bounds;
}

namespace {

ActivityNodeLayout layoutNode(const ActivityNode &node, const QPointF &center) {
  const QRectF bounds = activityNodeBounds(node.type, center);
  QPainterPath path;
  switch (node.type) {
  case ActivityNodeType::Action:
    path.addRoundedRect(bounds, 12.0, 12.0);
    break;
  case ActivityNodeType::Initial:
  case ActivityNodeType::Final:
    path.addEllipse(bounds);
    break;
  case ActivityNodeType::Decision:
  case ActivityNodeType::Merge:
    path.addPolygon({{center.x(), bounds.top()},
                     {bounds.right(), center.y()},
                     {center.x(), bounds.bottom()},
                     {bounds.left(), center.y()}});
    path.closeSubpath();
    break;
  case ActivityNodeType::Fork:
  case ActivityNodeType::Join:
    path.addRect(bounds);
    break;
  }
  // Flatten curves at a finer scale so start/final symbols remain smooth
  // when the view is zoomed in. Every outline is convex.
  const auto outline =
      QTransform::fromScale(0.25, 0.25)
          .map(path.toFillPolygon(QTransform::fromScale(4.0, 4.0)));
  return {node.id, node.type, node.name, bounds, outline};
}

QList<QPointF> flowPoints(const ActivityNodeLayout &source,
                          const ActivityNodeLayout &target, int lane) {
  const QPointF from = source.bounds.center();
  const QPointF to = target.bounds.center();
  const QPointF delta = to - from;
  const qreal distance = std::hypot(delta.x(), delta.y());
  if (source.id == target.id || source.bounds.intersects(target.bounds)) {
    const QRectF unionBounds = source.bounds.united(target.bounds);
    const qreal offset = 26.0 + 14.0 * lane;
    const qreal right = unionBounds.right() + offset;
    const qreal top = unionBounds.top() - offset;
    return {{source.bounds.right(), from.y()},
            {right, from.y()},
            {right, top},
            {to.x(), top},
            {to.x(), target.bounds.top()}};
  }
  if (lane == 0 || distance < 0.001)
    return {activityBoundaryPoint(source, to),
            activityBoundaryPoint(target, from)};

  // Offset additional flows between the same pair of nodes so reverse and
  // parallel edges remain independently visible and selectable.
  const qreal direction = source.id < target.id ? 1.0 : -1.0;
  const QPointF normal(-direction * delta.y() / distance,
                       direction * delta.x() / distance);
  const qreal offset = 22.0 * ((lane + 1) / 2) * (lane % 2 ? 1.0 : -1.0);
  const QPointF bend = (from + to) / 2.0 + normal * offset;
  return {activityBoundaryPoint(source, bend), bend,
          activityBoundaryPoint(target, bend)};
}

QRectF flowLabelBounds(const QList<QPointF> &points, const QString &guard) {
  QLineF longest;
  for (qsizetype i = 1; i < points.size(); ++i) {
    const QLineF segment(points.at(i - 1), points.at(i));
    if (segment.length() > longest.length())
      longest = segment;
  }
  QFont font = QGuiApplication::font();
  font.setPixelSize(13);
  const qreal width = guard.isEmpty()
                          ? 120.0
                          : std::clamp(QFontMetricsF(font).horizontalAdvance(
                                           QStringLiteral("[%1]").arg(guard)) +
                                           6.0,
                                       24.0, 160.0);
  QRectF label(0.0, 0.0, width, 22.0);
  const QPointF offset = std::abs(longest.dx()) >= std::abs(longest.dy())
                             ? QPointF(0.0, -15.0)
                             : QPointF(width / 2.0 + 6.0, 0.0);
  label.moveCenter(longest.center() + offset);
  return label;
}

qreal distanceToSegment(const QPointF &point, const QPointF &start,
                        const QPointF &end) {
  const QPointF delta = end - start;
  const qreal squaredLength = QPointF::dotProduct(delta, delta);
  const qreal t = squaredLength > 0.0
                      ? std::clamp(QPointF::dotProduct(point - start, delta) /
                                       squaredLength,
                                   0.0, 1.0)
                      : 0.0;
  return QLineF(point, start + delta * t).length();
}

} // namespace

QPointF activityBoundaryPoint(const ActivityNodeLayout &node,
                              const QPointF &toward) {
  const QPointF center = node.bounds.center();
  const QLineF ray(center, toward);
  for (qsizetype i = 1; i < node.outline.size(); ++i) {
    QPointF intersection;
    if (ray.intersects({node.outline.at(i - 1), node.outline.at(i)},
                       &intersection) == QLineF::BoundedIntersection)
      return intersection;
  }
  return toward;
}

ActivityDiagramLayout
buildActivityDiagramLayout(const ActivityDiagramData &activity,
                           const QHash<QString, QRectF> &previewGeometry) {
  ActivityDiagramLayout layout;
  QHash<QString, qsizetype> nodeRows;
  for (const auto &node : activity.nodes) {
    const auto preview = previewGeometry.constFind(node.id);
    const QPointF center =
        preview == previewGeometry.cend() ? node.center : preview->center();
    nodeRows.insert(node.id, layout.nodes.size());
    layout.nodes.append(layoutNode(node, center));
    layout.contentBounds |= layout.nodes.constLast().bounds;
  }
  QHash<QPair<QString, QString>, int> usedLanes;
  for (const auto &flow : activity.flows) {
    const auto source = nodeRows.constFind(flow.sourceId);
    const auto target = nodeRows.constFind(flow.targetId);
    if (source == nodeRows.cend() || target == nodeRows.cend())
      continue;
    const auto ids = std::minmax(flow.sourceId, flow.targetId);
    const int lane = usedLanes[{ids.first, ids.second}]++;
    auto points = flowPoints(layout.nodes.at(source.value()),
                             layout.nodes.at(target.value()), lane);
    for (const auto &point : points)
      layout.contentBounds |=
          QRectF(point - QPointF(1.0, 1.0), QSizeF(2.0, 2.0));
    const QRectF label = flowLabelBounds(points, flow.guard);
    if (!flow.guard.isEmpty())
      layout.contentBounds |= label;
    layout.flows.append({flow.id, flow.guard, std::move(points), label});
  }
  return layout;
}

const ActivityNodeLayout *hitActivityNode(const ActivityDiagramLayout &layout,
                                          const QPointF &point) {
  for (auto it = layout.nodes.crbegin(); it != layout.nodes.crend(); ++it)
    if (it->outline.containsPoint(point, Qt::OddEvenFill))
      return &*it;
  return nullptr;
}

const ActivityFlowLayout *hitActivityFlow(const ActivityDiagramLayout &layout,
                                          const QPointF &point,
                                          qreal tolerance) {
  for (auto it = layout.flows.crbegin(); it != layout.flows.crend(); ++it)
    if (!it->guard.isEmpty() && it->labelBounds.contains(point))
      return &*it;
  const ActivityFlowLayout *closest = nullptr;
  for (auto it = layout.flows.crbegin(); it != layout.flows.crend(); ++it) {
    for (qsizetype i = 1; i < it->points.size(); ++i) {
      const qreal distance =
          distanceToSegment(point, it->points.at(i - 1), it->points.at(i));
      if (distance < tolerance) {
        tolerance = distance;
        closest = &*it;
      }
    }
  }
  return closest;
}

} // namespace yauml::ui
