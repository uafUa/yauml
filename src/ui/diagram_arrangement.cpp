#include "ui/diagram_arrangement.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>

namespace yauml::ui {
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

struct ComponentLayout {
  QList<DiagramNodeGeometry> nodes;
  QSizeF size;
  qsizetype firstInputIndex = -1;
};

QVector<QVector<qsizetype>> sortedUniqueAdjacency(
    qsizetype nodeCount, const QList<DiagramLayoutEdge> &edges,
    const QHash<QString, qsizetype> &indexById, bool undirected) {
  QVector<QVector<qsizetype>> adjacency(nodeCount);
  for (const auto &edge : edges) {
    const auto source = indexById.constFind(edge.sourceId);
    const auto target = indexById.constFind(edge.targetId);
    if (source == indexById.cend() || target == indexById.cend() ||
        source.value() == target.value()) {
      continue;
    }
    adjacency[source.value()].append(target.value());
    if (undirected)
      adjacency[target.value()].append(source.value());
  }
  for (auto &neighbors : adjacency) {
    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()),
                    neighbors.end());
  }
  return adjacency;
}

QList<QVector<qsizetype>>
weakComponents(const QVector<QVector<qsizetype>> &undirectedAdjacency) {
  QList<QVector<qsizetype>> components;
  QVector<bool> visited(undirectedAdjacency.size(), false);
  for (qsizetype start = 0; start < undirectedAdjacency.size(); ++start) {
    if (visited.at(start))
      continue;
    QVector<qsizetype> component;
    std::queue<qsizetype> pending;
    visited[start] = true;
    pending.push(start);
    while (!pending.empty()) {
      const qsizetype node = pending.front();
      pending.pop();
      component.append(node);
      for (qsizetype neighbor : undirectedAdjacency.at(node)) {
        if (visited.at(neighbor))
          continue;
        visited[neighbor] = true;
        pending.push(neighbor);
      }
    }
    std::sort(component.begin(), component.end());
    components.append(std::move(component));
  }
  return components;
}

QVector<int> stronglyConnectedComponents(
    const QVector<qsizetype> &componentNodes,
    const QVector<QVector<qsizetype>> &directedAdjacency, int *componentCount) {
  const qsizetype nodeCount = directedAdjacency.size();
  QSet<qsizetype> included(componentNodes.cbegin(), componentNodes.cend());
  QVector<int> discovery(nodeCount, -1);
  QVector<int> lowLink(nodeCount, -1);
  QVector<int> result(nodeCount, -1);
  QVector<qsizetype> stack;
  QVector<bool> onStack(nodeCount, false);
  int nextDiscovery = 0;
  int nextComponent = 0;

  const std::function<void(qsizetype)> visit = [&](qsizetype node) {
    discovery[node] = nextDiscovery;
    lowLink[node] = nextDiscovery;
    ++nextDiscovery;
    stack.append(node);
    onStack[node] = true;

    for (qsizetype neighbor : directedAdjacency.at(node)) {
      if (!included.contains(neighbor))
        continue;
      if (discovery.at(neighbor) < 0) {
        visit(neighbor);
        lowLink[node] = std::min(lowLink.at(node), lowLink.at(neighbor));
      } else if (onStack.at(neighbor)) {
        lowLink[node] = std::min(lowLink.at(node), discovery.at(neighbor));
      }
    }

    if (lowLink.at(node) != discovery.at(node))
      return;
    while (!stack.isEmpty()) {
      const qsizetype member = stack.takeLast();
      onStack[member] = false;
      result[member] = nextComponent;
      if (member == node)
        break;
    }
    ++nextComponent;
  };

  for (qsizetype node : componentNodes)
    if (discovery.at(node) < 0)
      visit(node);
  *componentCount = nextComponent;
  return result;
}

QVector<int>
componentRanks(const QVector<qsizetype> &nodes,
               const QVector<QVector<qsizetype>> &directedAdjacency,
               const QVector<int> &strongComponentByNode,
               int strongComponentCount) {
  QVector<QVector<int>> successors(strongComponentCount);
  QVector<int> inDegree(strongComponentCount, 0);
  QVector<qsizetype> firstNode(strongComponentCount,
                               std::numeric_limits<qsizetype>::max());
  for (qsizetype node : nodes)
    firstNode[strongComponentByNode.at(node)] =
        std::min(firstNode.at(strongComponentByNode.at(node)), node);
  for (qsizetype source : nodes) {
    const int sourceComponent = strongComponentByNode.at(source);
    for (qsizetype target : directedAdjacency.at(source)) {
      const int targetComponent = strongComponentByNode.at(target);
      if (targetComponent < 0 || sourceComponent == targetComponent)
        continue;
      successors[sourceComponent].append(targetComponent);
    }
  }
  for (auto &targets : successors) {
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    for (int target : targets)
      ++inDegree[target];
  }

  QVector<int> rank(strongComponentCount, 0);
  QVector<int> ready;
  for (int component = 0; component < strongComponentCount; ++component)
    if (inDegree.at(component) == 0)
      ready.append(component);
  const auto sortReady = [&] {
    std::sort(ready.begin(), ready.end(), [&](int left, int right) {
      return firstNode.at(left) < firstNode.at(right);
    });
  };
  sortReady();
  while (!ready.isEmpty()) {
    const int source = ready.takeFirst();
    for (int target : successors.at(source)) {
      rank[target] = std::max(rank.at(target), rank.at(source) + 1);
      if (--inDegree[target] == 0) {
        ready.append(target);
        sortReady();
      }
    }
  }
  return rank;
}

ComponentLayout
layoutComponent(const QVector<qsizetype> &componentNodes,
                const QList<DiagramNodeGeometry> &input,
                const QVector<QVector<qsizetype>> &directedAdjacency,
                const AutomaticLayoutOptions &options) {
  int strongComponentCount = 0;
  const QVector<int> strongComponentByNode = stronglyConnectedComponents(
      componentNodes, directedAdjacency, &strongComponentCount);
  const QVector<int> strongComponentRank =
      componentRanks(componentNodes, directedAdjacency, strongComponentByNode,
                     strongComponentCount);

  int maximumRank = 0;
  QHash<int, QVector<qsizetype>> layers;
  for (qsizetype node : componentNodes) {
    const int rank = strongComponentRank.at(strongComponentByNode.at(node));
    maximumRank = std::max(maximumRank, rank);
    layers[rank].append(node);
  }
  const bool horizontal =
      options.direction == AutomaticLayoutDirection::LeftToRight;
  const auto secondaryCenter = [&](qsizetype node) {
    const QPointF center = input.at(node).geometry.center();
    return horizontal ? center.y() : center.x();
  };
  for (auto layer = layers.begin(); layer != layers.end(); ++layer) {
    std::stable_sort(layer->begin(), layer->end(),
                     [&](qsizetype left, qsizetype right) {
                       const qreal leftCenter = secondaryCenter(left);
                       const qreal rightCenter = secondaryCenter(right);
                       if (!qFuzzyCompare(leftCenter, rightCenter))
                         return leftCenter < rightCenter;
                       return input.at(left).id < input.at(right).id;
                     });
  }

  QVector<qreal> layerPrimary(maximumRank + 1, 0.0);
  QVector<qreal> layerSecondarySize(maximumRank + 1, 0.0);
  qreal primaryCursor = 0.0;
  qreal maximumSecondarySize = 0.0;
  for (int rank = 0; rank <= maximumRank; ++rank) {
    qreal maximumPrimary = 0.0;
    qreal secondarySize = 0.0;
    const auto &layer = layers.value(rank);
    for (qsizetype position = 0; position < layer.size(); ++position) {
      const QSizeF size = input.at(layer.at(position)).geometry.size();
      maximumPrimary =
          std::max(maximumPrimary, horizontal ? size.width() : size.height());
      secondarySize += horizontal ? size.height() : size.width();
      if (position + 1 < layer.size())
        secondarySize += options.itemGap;
    }
    layerPrimary[rank] = primaryCursor;
    layerSecondarySize[rank] = secondarySize;
    maximumSecondarySize = std::max(maximumSecondarySize, secondarySize);
    primaryCursor += maximumPrimary;
    if (rank < maximumRank)
      primaryCursor += options.layerGap;
  }

  ComponentLayout result;
  result.firstInputIndex = componentNodes.first();
  for (int rank = 0; rank <= maximumRank; ++rank) {
    qreal secondaryCursor =
        (maximumSecondarySize - layerSecondarySize.at(rank)) / 2.0;
    for (qsizetype node : layers.value(rank)) {
      DiagramNodeGeometry placed = input.at(node);
      const QPointF topLeft =
          horizontal ? QPointF(layerPrimary.at(rank), secondaryCursor)
                     : QPointF(secondaryCursor, layerPrimary.at(rank));
      placed.geometry.moveTopLeft(topLeft);
      result.nodes.append(std::move(placed));
      secondaryCursor += (horizontal ? input.at(node).geometry.height()
                                     : input.at(node).geometry.width()) +
                         options.itemGap;
    }
  }
  result.size = horizontal ? QSizeF(primaryCursor, maximumSecondarySize)
                           : QSizeF(maximumSecondarySize, primaryCursor);
  return result;
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
  const QRectF &reference = nodes.last().geometry;

  switch (operation) {
  case ArrangementOperation::AlignLeft:
    for (auto &node : result)
      node.geometry.moveLeft(reference.left());
    break;
  case ArrangementOperation::AlignHorizontalCenter:
    for (auto &node : result)
      node.geometry.moveCenter(
          {reference.center().x(), node.geometry.center().y()});
    break;
  case ArrangementOperation::AlignRight:
    for (auto &node : result)
      node.geometry.moveRight(reference.right());
    break;
  case ArrangementOperation::AlignTop:
    for (auto &node : result)
      node.geometry.moveTop(reference.top());
    break;
  case ArrangementOperation::AlignVerticalCenter:
    for (auto &node : result)
      node.geometry.moveCenter(
          {node.geometry.center().x(), reference.center().y()});
    break;
  case ArrangementOperation::AlignBottom:
    for (auto &node : result)
      node.geometry.moveBottom(reference.bottom());
    break;
  case ArrangementOperation::MatchWidth:
    for (auto &node : result)
      node.geometry.setWidth(reference.width());
    break;
  case ArrangementOperation::MatchHeight:
    for (auto &node : result)
      node.geometry.setHeight(reference.height());
    break;
  case ArrangementOperation::MatchSize:
    for (auto &node : result)
      node.geometry.setSize(reference.size());
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

QList<DiagramNodeGeometry>
automaticallyLayoutDiagramNodes(const QList<DiagramNodeGeometry> &nodes,
                                const QList<DiagramLayoutEdge> &edges,
                                const AutomaticLayoutOptions &options) {
  if (nodes.size() < 2)
    return nodes;

  QHash<QString, qsizetype> indexById;
  for (qsizetype index = 0; index < nodes.size(); ++index)
    if (!nodes.at(index).id.isEmpty() &&
        !indexById.contains(nodes.at(index).id))
      indexById.insert(nodes.at(index).id, index);
  const auto directed =
      sortedUniqueAdjacency(nodes.size(), edges, indexById, false);
  const auto undirected =
      sortedUniqueAdjacency(nodes.size(), edges, indexById, true);

  QList<ComponentLayout> components;
  for (const auto &component : weakComponents(undirected))
    components.append(layoutComponent(component, nodes, directed, options));
  std::sort(components.begin(), components.end(),
            [](const ComponentLayout &left, const ComponentLayout &right) {
              return left.firstInputIndex < right.firstInputIndex;
            });

  QRectF inputBounds;
  qreal totalArea = 0.0;
  qreal widestComponent = 0.0;
  for (const auto &node : nodes)
    inputBounds = inputBounds.isValid() ? inputBounds.united(node.geometry)
                                        : node.geometry;
  for (const auto &component : components) {
    totalArea += (component.size.width() + options.componentGap) *
                 (component.size.height() + options.componentGap);
    widestComponent = std::max(widestComponent, component.size.width());
  }
  const qreal targetShelfWidth =
      std::max(widestComponent, std::sqrt(totalArea) * 1.4);
  qreal cursorX = 0.0;
  qreal cursorY = 0.0;
  qreal rowHeight = 0.0;
  QHash<QString, QRectF> placedById;
  for (const auto &component : components) {
    if (cursorX > 0.0 && cursorX + component.size.width() > targetShelfWidth) {
      cursorX = 0.0;
      cursorY += rowHeight + options.componentGap;
      rowHeight = 0.0;
    }
    const QPointF componentOffset =
        inputBounds.topLeft() + QPointF(cursorX, cursorY);
    for (const auto &node : component.nodes) {
      QRectF geometry = node.geometry;
      geometry.translate(componentOffset);
      placedById.insert(node.id, geometry);
    }
    cursorX += component.size.width() + options.componentGap;
    rowHeight = std::max(rowHeight, component.size.height());
  }

  QList<DiagramNodeGeometry> result = nodes;
  for (auto &node : result)
    if (placedById.contains(node.id))
      node.geometry = placedById.value(node.id);
  return result;
}

} // namespace yauml::ui
