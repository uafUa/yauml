#include "ui/diagram_arrangement.h"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

namespace uuml::ui {
namespace {

struct OperationEntry {
  const char *key;
  ArrangementOperation operation;
  const char *description;
};

constexpr std::array kOperations = {
    OperationEntry{"alignLeft", ArrangementOperation::AlignLeft, "Align left"},
    OperationEntry{"alignHorizontalCenter",
                   ArrangementOperation::AlignHorizontalCenter,
                   "Align horizontal centers"},
    OperationEntry{"alignRight", ArrangementOperation::AlignRight,
                   "Align right"},
    OperationEntry{"alignTop", ArrangementOperation::AlignTop, "Align top"},
    OperationEntry{"alignVerticalCenter",
                   ArrangementOperation::AlignVerticalCenter,
                   "Align vertical centers"},
    OperationEntry{"alignBottom", ArrangementOperation::AlignBottom,
                   "Align bottom"},
    OperationEntry{"matchWidth", ArrangementOperation::MatchWidth,
                   "Make same width"},
    OperationEntry{"matchHeight", ArrangementOperation::MatchHeight,
                   "Make same height"},
    OperationEntry{"matchSize", ArrangementOperation::MatchSize,
                   "Make same size"},
    OperationEntry{"distributeHorizontally",
                   ArrangementOperation::DistributeHorizontally,
                   "Distribute horizontally"},
    OperationEntry{"distributeVertically",
                   ArrangementOperation::DistributeVertically,
                   "Distribute vertically"},
};

QRectF selectionBounds(const QList<DiagramNodeGeometry> &nodes) {
  QRectF bounds = nodes.first().geometry;
  for (qsizetype index = 1; index < nodes.size(); ++index)
    bounds = bounds.united(nodes.at(index).geometry);
  return bounds;
}

template <typename Center, typename Start, typename End, typename Move>
void distribute(QList<DiagramNodeGeometry> &nodes, Center center, Start start,
                End end, Move move, qreal fallbackGap) {
  if (nodes.size() < 3)
    return;

  QList<qsizetype> order(nodes.size());
  std::iota(order.begin(), order.end(), qsizetype{0});
  std::stable_sort(order.begin(), order.end(),
                   [&](qsizetype left, qsizetype right) {
                     const qreal leftCenter = center(nodes.at(left).geometry);
                     const qreal rightCenter = center(nodes.at(right).geometry);
                     if (leftCenter != rightCenter)
                       return leftCenter < rightCenter;
                     return nodes.at(left).id < nodes.at(right).id;
                   });

  qreal gap = std::numeric_limits<qreal>::max();
  for (qsizetype position = 1; position < order.size(); ++position) {
    const qreal candidate = start(nodes.at(order.at(position)).geometry) -
                            end(nodes.at(order.at(position - 1)).geometry);
    if (candidate > 0.0)
      gap = std::min(gap, candidate);
  }
  if (gap == std::numeric_limits<qreal>::max())
    gap = std::max(0.0, fallbackGap);

  qreal cursor = end(nodes.at(order.first()).geometry) + gap;
  for (qsizetype position = 1; position < order.size(); ++position) {
    QRectF &geometry = nodes[order.at(position)].geometry;
    move(geometry, cursor);
    cursor = end(geometry) + gap;
  }
}

} // namespace

std::optional<ArrangementOperation>
arrangementOperationFromKey(const QString &key) {
  const auto found = std::find_if(kOperations.cbegin(), kOperations.cend(),
                                  [&](const OperationEntry &item) {
                                    return key == QLatin1String(item.key);
                                  });
  if (found == kOperations.cend())
    return std::nullopt;
  return found->operation;
}

QString arrangementDescription(ArrangementOperation operation) {
  const auto found = std::find_if(
      kOperations.cbegin(), kOperations.cend(),
      [&](const OperationEntry &item) { return item.operation == operation; });
  return found == kOperations.cend() ? QString()
                                     : QLatin1String(found->description);
}

QList<DiagramNodeGeometry>
arrangeDiagramNodes(const QList<DiagramNodeGeometry> &nodes,
                    ArrangementOperation operation,
                    qreal fallbackDistributionGap) {
  if (nodes.size() < 2)
    return nodes;

  QList<DiagramNodeGeometry> result = nodes;
  const QRectF bounds = selectionBounds(nodes);
  const QRectF &sizeReference = nodes.last().geometry;

  switch (operation) {
  case ArrangementOperation::AlignLeft:
    for (auto &node : result)
      node.geometry.moveLeft(bounds.left());
    break;
  case ArrangementOperation::AlignHorizontalCenter:
    for (auto &node : result)
      node.geometry.moveCenter(
          {bounds.center().x(), node.geometry.center().y()});
    break;
  case ArrangementOperation::AlignRight:
    for (auto &node : result)
      node.geometry.moveRight(bounds.right());
    break;
  case ArrangementOperation::AlignTop:
    for (auto &node : result)
      node.geometry.moveTop(bounds.top());
    break;
  case ArrangementOperation::AlignVerticalCenter:
    for (auto &node : result)
      node.geometry.moveCenter(
          {node.geometry.center().x(), bounds.center().y()});
    break;
  case ArrangementOperation::AlignBottom:
    for (auto &node : result)
      node.geometry.moveBottom(bounds.bottom());
    break;
  case ArrangementOperation::MatchWidth:
    for (auto &node : result)
      node.geometry.setWidth(sizeReference.width());
    break;
  case ArrangementOperation::MatchHeight:
    for (auto &node : result)
      node.geometry.setHeight(sizeReference.height());
    break;
  case ArrangementOperation::MatchSize:
    for (auto &node : result)
      node.geometry.setSize(sizeReference.size());
    break;
  case ArrangementOperation::DistributeHorizontally:
    distribute(
        result, [](const QRectF &rect) { return rect.center().x(); },
        [](const QRectF &rect) { return rect.left(); },
        [](const QRectF &rect) { return rect.right(); },
        [](QRectF &rect, qreal position) { rect.moveLeft(position); },
        fallbackDistributionGap);
    break;
  case ArrangementOperation::DistributeVertically:
    distribute(
        result, [](const QRectF &rect) { return rect.center().y(); },
        [](const QRectF &rect) { return rect.top(); },
        [](const QRectF &rect) { return rect.bottom(); },
        [](QRectF &rect, qreal position) { rect.moveTop(position); },
        fallbackDistributionGap);
    break;
  }
  return result;
}

} // namespace uuml::ui
