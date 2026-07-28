#include "core/project_controller.h"

#include "core/connector_port_layout.h"
#include "core/cpp_import.h"
#include "core/diagram_filter.h"
#include "core/presentation_layout.h"
#include "core/project_command.h"
#include "core/project_commands.h"
#include "core/project_serializer.h"
#include "core/project_style.h"
#include "core/project_tree_model.h"
#include "core/stereotype_catalog.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>

namespace yauml {

namespace {

constexpr qreal kDefaultNodeX = 50.0;
constexpr qreal kDefaultNodeY = 50.0;
constexpr qreal kNodeClearance = 12.0;
constexpr int kPlacementColumns = 12;

QVariantMap diagramStyleMap(const DiagramStyle &style) {
  return {{QStringLiteral("id"), style.id},
          {QStringLiteral("name"), style.name},
          {QStringLiteral("fill"), style.fill},
          {QStringLiteral("headerFill"), style.headerFill},
          {QStringLiteral("border"), style.border},
          {QStringLiteral("primaryText"), style.primaryText},
          {QStringLiteral("secondaryText"), style.secondaryText},
          {QStringLiteral("divider"), style.divider}};
}

QVariantMap stereotypeDefinitionMap(const StereotypeDefinition &definition) {
  return {{QStringLiteral("id"), definition.id},
          {QStringLiteral("name"), definition.name},
          {QStringLiteral("applicableTo"), definition.applicableTo}};
}

QString normalizedStyleColor(const QVariant &value) {
  QColor color;
  if (value.canConvert<QColor>())
    color = value.value<QColor>();
  if (!color.isValid())
    color = QColor(value.toString().trimmed());
  if (!color.isValid())
    return {};
  const auto format = color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb;
  return color.name(format).toUpper();
}

QString browserSubjectKey(const QString &kind, const QString &id) {
  return (kind == QStringLiteral("element") ||
          kind == QStringLiteral("folder") ||
          kind == QStringLiteral("namespace")) &&
                 !id.isEmpty()
             ? kind + u':' + id
             : QString{};
}

QString browserParentSubjectKey(const BrowserParent &parent) {
  return browserSubjectKey(parent.kind, parent.id);
}

QString effectiveBrowserParentKey(const ProjectData &project,
                                  const QString &kind, const QString &id) {
  if (kind == QStringLiteral("namespace")) {
    const int separator = id.lastIndexOf(QStringLiteral("::"));
    if (separator < 0)
      return {};
    const QString parentPath = id.left(separator);
    const auto owner =
        std::find_if(project.elements.cbegin(), project.elements.cend(),
                     [&](const ModelElement &candidate) {
                       return candidate.name == parentPath;
                     });
    return owner != project.elements.cend()
               ? browserSubjectKey(QStringLiteral("element"), owner->id)
               : browserSubjectKey(QStringLiteral("namespace"), parentPath);
  }
  if (kind == QStringLiteral("folder")) {
    const BrowserFolder *folder = findBrowserFolder(project, id);
    return folder ? browserParentSubjectKey(folder->parent) : QString{};
  }
  const ModelElement *element = findElement(project, id);
  if (!element)
    return {};
  if (!element->browserParent.kind.isEmpty())
    return browserParentSubjectKey(element->browserParent);
  if (!element->enclosingTypeId.isEmpty())
    return browserSubjectKey(QStringLiteral("element"),
                             element->enclosingTypeId);

  const QStringList parts =
      element->name.split(QStringLiteral("::"), Qt::SkipEmptyParts);
  QString qualifiedPath;
  QString parent;
  for (int index = 0; index + 1 < parts.size(); ++index) {
    if (!qualifiedPath.isEmpty())
      qualifiedPath += QStringLiteral("::");
    qualifiedPath += parts.at(index);
    const auto owner = std::find_if(
        project.elements.cbegin(), project.elements.cend(),
        [&](const ModelElement &candidate) {
          return candidate.name == qualifiedPath && candidate.id != id;
        });
    if (owner != project.elements.cend())
      parent = browserSubjectKey(QStringLiteral("element"), owner->id);
  }
  return !parent.isEmpty()
             ? parent
             : browserSubjectKey(QStringLiteral("element"), element->packageId);
}

QStringList normalizedBrowserItemOrder(const ProjectData &project) {
  QStringList result;
  QSet<QString> seen;
  const auto appendIfValid = [&](const QString &key) {
    if (seen.contains(key))
      return;
    const int separator = key.indexOf(u':');
    if (separator <= 0)
      return;
    const QString kind = key.left(separator);
    const QString id = key.mid(separator + 1);
    const bool valid =
        (kind == QStringLiteral("element") && findElement(project, id)) ||
        (kind == QStringLiteral("folder") && findBrowserFolder(project, id));
    if (!valid)
      return;
    seen.insert(key);
    result.append(key);
  };
  for (const QString &key : project.browserItemOrder)
    appendIfValid(key);
  for (const auto &folder : project.browserFolders)
    appendIfValid(browserSubjectKey(QStringLiteral("folder"), folder.id));
  for (const auto &element : project.elements)
    appendIfValid(browserSubjectKey(QStringLiteral("element"), element.id));
  return result;
}

QStringList browserItemKeysFromJson(const ProjectData &project,
                                    const QString &itemsJson) {
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(itemsJson.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray())
    return {};

  QStringList keys;
  QSet<QString> seen;
  for (const QJsonValue &value : document.array()) {
    const QJsonObject object = value.toObject();
    const QString kind = object.value(QStringLiteral("kind")).toString();
    const QString id = object.value(QStringLiteral("id")).toString();
    const bool valid =
        (kind == QStringLiteral("element") && findElement(project, id)) ||
        (kind == QStringLiteral("folder") && findBrowserFolder(project, id));
    const QString key = browserSubjectKey(kind, id);
    if (valid && !key.isEmpty() && !seen.contains(key)) {
      seen.insert(key);
      keys.append(key);
    }
  }
  return keys;
}

QStringList missingRelatedTypeIds(const ProjectData &project,
                                  const Diagram &diagram,
                                  const QString &elementId, bool incoming) {
  QSet<QString> presentedElementIds;
  presentedElementIds.reserve(diagram.nodes.size());
  for (const auto &node : diagram.nodes)
    presentedElementIds.insert(node.elementId);

  QStringList result;
  QSet<QString> seen;
  for (const auto &relationship : project.relationships) {
    if ((incoming ? relationship.targetId : relationship.sourceId) !=
        elementId) {
      continue;
    }
    const QString relatedId =
        incoming ? relationship.sourceId : relationship.targetId;
    const auto *related = findElement(project, relatedId);
    if (!related || related->type == ElementType::Package ||
        relatedId == elementId || presentedElementIds.contains(relatedId) ||
        seen.contains(relatedId)) {
      continue;
    }
    seen.insert(relatedId);
    result.append(relatedId);
  }
  return result;
}

QSizeF nodePlacementSizeForDiagram(const ProjectData &project,
                                   const Diagram &diagram,
                                   const ModelElement &element,
                                   const QString &sizingMode) {
  if (sizingMode.trimmed().compare(QStringLiteral("fixed"),
                                   Qt::CaseInsensitive) == 0) {
    return {presentation_layout::kFixedNodeWidth,
            presentation_layout::kFixedNodeHeight};
  }
  return presentation_layout::nodeContentSize(
      project, element, diagram.showAttributes, diagram.showOperations);
}

QString packageIdForBrowserTarget(const ProjectData &project,
                                  const QString &targetKind,
                                  const QString &targetId) {
  if (targetKind == QStringLiteral("element")) {
    const ModelElement *target = findElement(project, targetId);
    if (!target)
      return {};
    return target->type == ElementType::Package ? target->id
                                                : target->packageId;
  }

  QSet<QString> visited;
  QString current = effectiveBrowserParentKey(project, targetKind, targetId);
  while (!current.isEmpty() && !visited.contains(current)) {
    visited.insert(current);
    const int separator = current.indexOf(u':');
    if (separator <= 0)
      break;
    const QString kind = current.left(separator);
    const QString id = current.mid(separator + 1);
    if (kind == QStringLiteral("element")) {
      const ModelElement *candidate = findElement(project, id);
      if (candidate && candidate->type == ElementType::Package)
        return candidate->id;
    }
    current = effectiveBrowserParentKey(project, kind, id);
  }
  return {};
}

bool canOwnNestedTypes(const ModelElement &element) {
  return element.type == ElementType::Class ||
         element.type == ElementType::Struct;
}

QString enclosingTypeIdForBrowserTarget(const ProjectData &project,
                                        const QString &targetKind,
                                        const QString &targetId) {
  if (targetKind == QStringLiteral("element")) {
    const auto *target = findElement(project, targetId);
    return target && canOwnNestedTypes(*target) ? target->id : QString{};
  }

  QSet<QString> visited;
  QString current = effectiveBrowserParentKey(project, targetKind, targetId);
  while (!current.isEmpty() && !visited.contains(current)) {
    visited.insert(current);
    const int separator = current.indexOf(u':');
    if (separator <= 0)
      break;
    const QString kind = current.left(separator);
    const QString id = current.mid(separator + 1);
    if (kind == QStringLiteral("element")) {
      const auto *candidate = findElement(project, id);
      if (candidate && canOwnNestedTypes(*candidate))
        return candidate->id;
    }
    current = effectiveBrowserParentKey(project, kind, id);
  }
  return {};
}

const ContainerPresentation *packageContainerFor(const Diagram &diagram,
                                                 const QString &packageId) {
  const auto found = std::find_if(
      diagram.containers.cbegin(), diagram.containers.cend(),
      [&](const ContainerPresentation &container) {
        return container.subjectKind == QStringLiteral("package") &&
               container.subjectId == packageId;
      });
  return found != diagram.containers.cend() ? &*found : nullptr;
}

const ContainerPresentation *
nearestPresentedPackageAncestor(const ProjectData &project,
                                const Diagram &diagram,
                                const QString &packageId) {
  QSet<QString> visited;
  const auto *package = findElement(project, packageId);
  QString current = package && package->type == ElementType::Package
                        ? package->packageId
                        : QString{};
  while (!current.isEmpty() && !visited.contains(current)) {
    visited.insert(current);
    if (const auto *container = packageContainerFor(diagram, current))
      return container;
    const auto *ancestor = findElement(project, current);
    current = ancestor && ancestor->type == ElementType::Package
                  ? ancestor->packageId
                  : QString{};
  }
  return nullptr;
}

QString ownerContainerIdForPresentation(const Diagram &diagram,
                                        const QString &presentationId) {
  for (const auto &container : diagram.containers)
    if (container.childPresentationIds.contains(presentationId))
      return container.id;
  return {};
}

QString containingPackageIdForPresentation(const Diagram &diagram,
                                           const QString &presentationId) {
  QSet<QString> visited;
  QString ownerId = ownerContainerIdForPresentation(diagram, presentationId);
  while (!ownerId.isEmpty() && !visited.contains(ownerId)) {
    visited.insert(ownerId);
    const auto *owner = findContainer(diagram, ownerId);
    if (!owner)
      break;
    if (owner->subjectKind == QStringLiteral("package"))
      return owner->subjectId;
    ownerId = ownerContainerIdForPresentation(diagram, ownerId);
  }
  return {};
}

QString packageIdForDropTarget(const Diagram &diagram,
                               const QString &targetContainerId) {
  QSet<QString> visited;
  QString currentId = targetContainerId;
  while (!currentId.isEmpty() && !visited.contains(currentId)) {
    visited.insert(currentId);
    const auto *container = findContainer(diagram, currentId);
    if (!container)
      break;
    if (container->subjectKind == QStringLiteral("package"))
      return container->subjectId;
    currentId = ownerContainerIdForPresentation(diagram, currentId);
  }
  return {};
}

bool packageIsAncestorOrSame(const ProjectData &project,
                             const QString &possibleAncestorId,
                             const QString &packageId) {
  QSet<QString> visited;
  QString currentId = packageId;
  while (!currentId.isEmpty() && !visited.contains(currentId)) {
    if (currentId == possibleAncestorId)
      return true;
    visited.insert(currentId);
    const auto *package = findElement(project, currentId);
    currentId = package && package->type == ElementType::Package
                    ? package->packageId
                    : QString{};
  }
  return false;
}

QString packageTargetLabel(const ProjectData &project,
                           const QString &packageId) {
  if (const auto *package = findElement(project, packageId))
    return QStringLiteral("package \"%1\"").arg(package->name);
  return QStringLiteral("the model root");
}

QString semanticReassignmentPrompt(const ProjectData &project,
                                   const QSet<QString> &elementIds,
                                   const QString &targetPackageId,
                                   const QString &targetEnclosingTypeId) {
  QStringList names;
  names.reserve(elementIds.size());
  for (const auto &element : project.elements)
    if (elementIds.contains(element.id))
      names.append(
          presentation_layout::fullyQualifiedElementName(project, element));
  if (names.isEmpty())
    return {};

  QString targetLabel = packageTargetLabel(project, targetPackageId);
  if (const auto *owner = findElement(project, targetEnclosingTypeId)) {
    targetLabel = QStringLiteral("type \"%1\"")
                      .arg(presentation_layout::fullyQualifiedElementName(
                          project, *owner));
  }
  if (names.size() == 1)
    return QStringLiteral("Move \"%1\" to %2?")
        .arg(names.constFirst(), targetLabel);

  constexpr int kVisibleNameLimit = 8;
  QStringList visibleNames;
  const qsizetype visibleCount =
      std::min(names.size(), qsizetype{kVisibleNameLimit});
  visibleNames.reserve(visibleCount + 1);
  for (int index = 0; index < visibleCount; ++index)
    visibleNames.append(QStringLiteral("• %1").arg(names.at(index)));
  if (names.size() > kVisibleNameLimit)
    visibleNames.append(
        QStringLiteral("• …and %1 more").arg(names.size() - kVisibleNameLimit));
  return QStringLiteral("Move these %1 elements to %2?\n\n%3")
      .arg(names.size())
      .arg(targetLabel, visibleNames.join(u'\n'));
}

QString packageReassignmentPrompt(const ProjectData &project,
                                  const QSet<QString> &elementIds,
                                  const QString &targetPackageId) {
  return semanticReassignmentPrompt(project, elementIds, targetPackageId, {});
}

bool browserContainerExists(const ProjectData &project,
                            const BrowserParent &parent) {
  if (parent.kind == QStringLiteral("model"))
    return parent.id.isEmpty();
  if (parent.kind == QStringLiteral("namespace"))
    return !parent.id.trimmed().isEmpty();
  if (parent.kind == QStringLiteral("element"))
    return findElement(project, parent.id);
  if (parent.kind == QStringLiteral("folder"))
    return findBrowserFolder(project, parent.id);
  return false;
}

QRectF firstAvailableNodeGeometry(const Diagram &diagram,
                                  const QSizeF &nodeSize) {
  // Reusing the first open slot keeps newly placed nodes near the diagram's
  // origin and, importantly, restores holes left by removed presentations. A
  // node-count-based diagonal offset sends additions thousands of pixels away
  // on large diagrams, making a successful placement appear to have failed.
  const qsizetype candidateCount = diagram.nodes.size() + 1;
  const qreal horizontalSpacing = nodeSize.width() + 24.0;
  const qreal verticalSpacing = nodeSize.height() + 24.0;
  for (qsizetype index = 0; index < candidateCount; ++index) {
    const int column = static_cast<int>(index % kPlacementColumns);
    const int row = static_cast<int>(index / kPlacementColumns);
    const QRectF candidate(kDefaultNodeX + column * horizontalSpacing,
                           kDefaultNodeY + row * verticalSpacing,
                           nodeSize.width(), nodeSize.height());
    const bool occupied =
        std::any_of(diagram.nodes.cbegin(), diagram.nodes.cend(),
                    [&](const NodePresentation &node) {
                      return node.geometry
                          .adjusted(-kNodeClearance, -kNodeClearance,
                                    kNodeClearance, kNodeClearance)
                          .intersects(candidate);
                    });
    if (!occupied)
      return candidate;
  }

  // Very large or irregular presentations can cover several logical slots.
  // Falling back below the current content guarantees a usable position while
  // keeping the search above bounded by the number of existing nodes.
  qreal contentBottom = kDefaultNodeY - verticalSpacing;
  for (const auto &node : diagram.nodes)
    contentBottom = std::max(contentBottom, node.geometry.bottom());
  return {kDefaultNodeX, contentBottom + kNodeClearance, nodeSize.width(),
          nodeSize.height()};
}

QList<ConnectorPresentation>
connectorsForNewPresentation(const ProjectData &project, const Diagram &diagram,
                             const QString &newElementId) {
  QSet<QString> presentedElementIds;
  presentedElementIds.reserve(diagram.nodes.size() + 1);
  for (const auto &node : diagram.nodes)
    presentedElementIds.insert(node.elementId);
  presentedElementIds.insert(newElementId);

  QSet<QString> presentedRelationshipIds;
  presentedRelationshipIds.reserve(diagram.connectors.size());
  for (const auto &connector : diagram.connectors)
    presentedRelationshipIds.insert(connector.relationshipId);

  QList<ConnectorPresentation> connectors;
  for (const auto &relationship : project.relationships) {
    const bool involvesNewElement = relationship.sourceId == newElementId ||
                                    relationship.targetId == newElementId;
    if (!involvesNewElement ||
        !presentedElementIds.contains(relationship.sourceId) ||
        !presentedElementIds.contains(relationship.targetId) ||
        presentedRelationshipIds.contains(relationship.id))
      continue;

    ConnectorPresentation connector;
    connector.id = newId();
    connector.relationshipId = relationship.id;
    connectors.append(std::move(connector));
  }
  return connectors;
}

ConnectorAnchor edgeAnchorToward(const QRectF &rect, const QPointF &target) {
  ConnectorAnchor anchor;
  const QPointF direction = target - rect.center();
  if (qFuzzyIsNull(direction.x()) && qFuzzyIsNull(direction.y())) {
    anchor.side = ConnectorSide::Right;
    anchor.offset = 0.5;
    return anchor;
  }

  const qreal horizontalScale =
      qFuzzyIsNull(direction.x())
          ? std::numeric_limits<qreal>::max()
          : (rect.width() / 2.0) / std::abs(direction.x());
  const qreal verticalScale =
      qFuzzyIsNull(direction.y())
          ? std::numeric_limits<qreal>::max()
          : (rect.height() / 2.0) / std::abs(direction.y());

  if (horizontalScale <= verticalScale) {
    anchor.side =
        direction.x() >= 0 ? ConnectorSide::Right : ConnectorSide::Left;
    const qreal edgeY = rect.center().y() + direction.y() * horizontalScale;
    anchor.offset = (edgeY - rect.top()) / rect.height();
  } else {
    anchor.side =
        direction.y() >= 0 ? ConnectorSide::Bottom : ConnectorSide::Top;
    const qreal edgeX = rect.center().x() + direction.x() * verticalScale;
    anchor.offset = (edgeX - rect.left()) / rect.width();
  }
  anchor.offset = std::clamp(anchor.offset, 0.0, 1.0);
  return anchor;
}

QPointF anchorPoint(const QRectF &rect, const ConnectorAnchor &anchor) {
  const qreal offset = std::clamp(anchor.offset, 0.0, 1.0);
  switch (anchor.side) {
  case ConnectorSide::Top:
    return {rect.left() + rect.width() * offset, rect.top()};
  case ConnectorSide::Right:
    return {rect.right(), rect.top() + rect.height() * offset};
  case ConnectorSide::Bottom:
    return {rect.left() + rect.width() * offset, rect.bottom()};
  case ConnectorSide::Left:
    return {rect.left(), rect.top() + rect.height() * offset};
  case ConnectorSide::Automatic:
    return rect.center();
  }
  return rect.center();
}

bool horizontalSide(ConnectorSide side) {
  return side == ConnectorSide::Left || side == ConnectorSide::Right;
}

QPointF outsidePoint(const QRectF &rect, const ConnectorAnchor &anchor,
                     qreal gap) {
  QPointF point = anchorPoint(rect, anchor);
  switch (anchor.side) {
  case ConnectorSide::Top:
    point.ry() -= gap;
    break;
  case ConnectorSide::Right:
    point.rx() += gap;
    break;
  case ConnectorSide::Bottom:
    point.ry() += gap;
    break;
  case ConnectorSide::Left:
    point.rx() -= gap;
    break;
  case ConnectorSide::Automatic:
    break;
  }
  return point;
}

QList<ConnectorBendPoint>
selfConnectorBendPoints(const QRectF &rect, const ConnectorAnchor &source,
                        const ConnectorAnchor &target) {
  constexpr qreal kLoopGap = 44.0;
  const QPointF sourceOutside = outsidePoint(rect, source, kLoopGap);
  const QPointF targetOutside = outsidePoint(rect, target, kLoopGap);
  QVector<QPointF> points{sourceOutside};
  bool appendTargetOutside = true;

  if (source.side == target.side) {
    if (QLineF(sourceOutside, targetOutside).length() < 1.0) {
      const qreal tangentDirection = source.offset <= 0.5 ? 1.0 : -1.0;
      const QPointF tangent = horizontalSide(source.side)
                                  ? QPointF(0, 40 * tangentDirection)
                                  : QPointF(40 * tangentDirection, 0);
      points.append(sourceOutside + tangent);
      points.append(anchorPoint(rect, target) + tangent);
      appendTargetOutside = false;
    }
  } else if (horizontalSide(source.side) != horizontalSide(target.side)) {
    points.append(horizontalSide(source.side)
                      ? QPointF(sourceOutside.x(), targetOutside.y())
                      : QPointF(targetOutside.x(), sourceOutside.y()));
  } else if (horizontalSide(source.side)) {
    const qreal top = rect.top() - kLoopGap;
    const qreal bottom = rect.bottom() + kLoopGap;
    const qreal routeY =
        std::abs(sourceOutside.y() - top) + std::abs(targetOutside.y() - top) <=
                std::abs(sourceOutside.y() - bottom) +
                    std::abs(targetOutside.y() - bottom)
            ? top
            : bottom;
    points.append({sourceOutside.x(), routeY});
    points.append({targetOutside.x(), routeY});
  } else {
    const qreal left = rect.left() - kLoopGap;
    const qreal right = rect.right() + kLoopGap;
    const qreal routeX = std::abs(sourceOutside.x() - left) +
                                     std::abs(targetOutside.x() - left) <=
                                 std::abs(sourceOutside.x() - right) +
                                     std::abs(targetOutside.x() - right)
                             ? left
                             : right;
    points.append({routeX, sourceOutside.y()});
    points.append({routeX, targetOutside.y()});
  }
  if (appendTargetOutside)
    points.append(targetOutside);

  QList<ConnectorBendPoint> bendPoints;
  bendPoints.reserve(points.size());
  for (const auto &point : points)
    bendPoints.append({point, {}});
  return bendPoints;
}

struct PendingConnectorEndpoint {
  qsizetype connectorIndex = -1;
  bool source = false;
  QString nodeId;
  ConnectorSide side = ConnectorSide::Automatic;
  qreal preferredOffset = 0.5;
};

QString endpointGroupKey(const QString &nodeId, ConnectorSide side) {
  return nodeId + u'|' + QString::number(static_cast<int>(side));
}

void attachNewConnectorsToSnapPoints(const ProjectData &project,
                                     Diagram &diagram,
                                     const QSet<QString> &newConnectorIds) {
  if (newConnectorIds.isEmpty())
    return;

  QHash<QString, NodePresentation *> nodeByElement;
  nodeByElement.reserve(diagram.nodes.size());
  for (auto &node : diagram.nodes)
    nodeByElement.insert(node.elementId, &node);

  QHash<QString, QList<PendingConnectorEndpoint>> pendingByGroup;
  QHash<QString, QList<qreal>> occupiedOffsetsByGroup;
  QHash<QString, NodePresentation *> nodeByGroup;
  QHash<QString, ConnectorSide> sideByGroup;

  const auto collectEndpoint = [&](qsizetype connectorIndex, bool source) {
    auto &connector = diagram.connectors[connectorIndex];
    const auto *relationship =
        findRelationship(project, connector.relationshipId);
    if (!relationship)
      return;
    auto *sourceNode = nodeByElement.value(relationship->sourceId, nullptr);
    auto *targetNode = nodeByElement.value(relationship->targetId, nullptr);
    if (!sourceNode || !targetNode)
      return;

    auto *node = source ? sourceNode : targetNode;
    const auto *otherNode = source ? targetNode : sourceNode;
    ConnectorAnchor anchor =
        source ? connector.sourceAnchor : connector.targetAnchor;
    if (anchor.side == ConnectorSide::Automatic) {
      QPointF target = otherNode->geometry.center();
      if (!connector.bendPoints.isEmpty()) {
        target = source ? connector.bendPoints.constFirst().position
                        : connector.bendPoints.constLast().position;
      }
      anchor = edgeAnchorToward(node->geometry, target);
    }

    const QString groupKey = endpointGroupKey(node->id, anchor.side);
    nodeByGroup.insert(groupKey, node);
    sideByGroup.insert(groupKey, anchor.side);
    if (newConnectorIds.contains(connector.id)) {
      pendingByGroup[groupKey].append(
          {connectorIndex, source, node->id, anchor.side, anchor.offset});
    } else {
      occupiedOffsetsByGroup[groupKey].append(anchor.offset);
    }
  };

  for (qsizetype index = 0; index < diagram.connectors.size(); ++index) {
    collectEndpoint(index, true);
    collectEndpoint(index, false);
  }

  for (auto group = pendingByGroup.begin(); group != pendingByGroup.end();
       ++group) {
    auto *node = nodeByGroup.value(group.key(), nullptr);
    const ConnectorSide side =
        sideByGroup.value(group.key(), ConnectorSide::Automatic);
    if (!node || side == ConnectorSide::Automatic)
      continue;

    const int endpointCount =
        occupiedOffsetsByGroup.value(group.key()).size() + group->size();
    const int currentCount =
        connector_ports::snapPointCountForSide(*node, side);
    const int pointCount = connector_ports::normalizedSnapPointCount(
        std::max(currentCount, endpointCount));
    if (side == ConnectorSide::Top || side == ConnectorSide::Bottom)
      node->horizontalPortSnapPoints = pointCount;
    else
      node->verticalPortSnapPoints = pointCount;

    const QVector<qreal> offsets = connector_ports::snapOffsets(pointCount);
    QVector<bool> occupied(offsets.size(), false);
    const auto nearestAvailableIndex = [&](qreal preferred) {
      int bestIndex = -1;
      qreal bestDistance = std::numeric_limits<qreal>::max();
      for (int index = 0; index < offsets.size(); ++index) {
        if (occupied.at(index))
          continue;
        const qreal distance = std::abs(offsets.at(index) - preferred);
        if (distance < bestDistance) {
          bestDistance = distance;
          bestIndex = index;
        }
      }
      return bestIndex;
    };

    // Existing endpoints remain user-authoritative. Reserve the closest new
    // marker for each one without moving its persisted free offset.
    for (const qreal existingOffset :
         occupiedOffsetsByGroup.value(group.key())) {
      const int index = nearestAvailableIndex(existingOffset);
      if (index >= 0)
        occupied[index] = true;
    }

    std::stable_sort(
        group->begin(), group->end(),
        [](const PendingConnectorEndpoint &left,
           const PendingConnectorEndpoint &right) {
          if (!qFuzzyCompare(left.preferredOffset, right.preferredOffset))
            return left.preferredOffset < right.preferredOffset;
          if (left.connectorIndex != right.connectorIndex)
            return left.connectorIndex < right.connectorIndex;
          return left.source && !right.source;
        });
    for (const auto &endpoint : std::as_const(*group)) {
      const int index = nearestAvailableIndex(endpoint.preferredOffset);
      if (index < 0)
        continue;
      occupied[index] = true;
      ConnectorAnchor anchor{endpoint.side, offsets.at(index)};
      auto &connector = diagram.connectors[endpoint.connectorIndex];
      if (endpoint.source)
        connector.sourceAnchor = anchor;
      else
        connector.targetAnchor = anchor;
    }
  }

  // A restored semantic self-relationship needs external geometry just like
  // one created interactively; otherwise its equal endpoints are invisible.
  for (auto &connector : diagram.connectors) {
    if (!newConnectorIds.contains(connector.id))
      continue;
    const auto *relationship =
        findRelationship(project, connector.relationshipId);
    if (!relationship || relationship->sourceId != relationship->targetId)
      continue;
    if (const auto *node =
            nodeByElement.value(relationship->sourceId, nullptr)) {
      connector.bendPoints = selfConnectorBendPoints(
          node->geometry, connector.sourceAnchor, connector.targetAnchor);
    }
  }
}

QList<NodePortSnapPointChange>
existingNodePortChanges(const Diagram &before, const Diagram &after,
                        const QSet<QString> &newNodeIds) {
  QList<NodePortSnapPointChange> changes;
  for (const auto &beforeNode : before.nodes) {
    if (newNodeIds.contains(beforeNode.id))
      continue;
    const auto *afterNode = findNode(after, beforeNode.id);
    if (!afterNode || (beforeNode.horizontalPortSnapPoints ==
                           afterNode->horizontalPortSnapPoints &&
                       beforeNode.verticalPortSnapPoints ==
                           afterNode->verticalPortSnapPoints)) {
      continue;
    }
    changes.append({beforeNode.id, beforeNode.horizontalPortSnapPoints,
                    beforeNode.verticalPortSnapPoints,
                    afterNode->horizontalPortSnapPoints,
                    afterNode->verticalPortSnapPoints});
  }
  return changes;
}

void copyAutoAttachedPresentations(
    const Diagram &prospective, QList<NodePresentation> &newNodes,
    QList<ConnectorPresentation> &newConnectors) {
  for (auto &node : newNodes)
    if (const auto *updated = findNode(prospective, node.id))
      node = *updated;
  for (auto &connector : newConnectors)
    if (const auto *updated = findConnector(prospective, connector.id))
      connector = *updated;
}

QHash<QString, QString> containerOwners(const Diagram &diagram) {
  QHash<QString, QString> owners;
  for (const auto &container : diagram.containers)
    for (const QString &childId : container.childPresentationIds)
      owners.insert(childId, container.id);
  return owners;
}

bool containerSubtreeContains(const Diagram &diagram,
                              const QString &containerId,
                              const QString &presentationId,
                              QSet<QString> &visited) {
  if (visited.contains(containerId))
    return false;
  visited.insert(containerId);
  const auto *container = findContainer(diagram, containerId);
  if (!container)
    return false;
  for (const QString &childId : container->childPresentationIds) {
    if (childId == presentationId)
      return true;
    if (findContainer(diagram, childId) &&
        containerSubtreeContains(diagram, childId, presentationId, visited))
      return true;
  }
  return false;
}

QList<ContainerChildrenChange>
membershipChangesForDrop(const Diagram &diagram,
                         const QStringList &orderedPresentationIds,
                         const QString &targetContainerId) {
  const auto owners = containerOwners(diagram);
  QSet<QString> movedIds;
  QStringList validIds;
  validIds.reserve(orderedPresentationIds.size());
  for (const QString &id : orderedPresentationIds) {
    if (movedIds.contains(id) ||
        (!findNode(diagram, id) && !findContainer(diagram, id)))
      continue;
    movedIds.insert(id);
    validIds.append(id);
  }

  // If callers ever provide a complete moving subtree, only its outermost
  // presentation changes owner. Descendant membership must remain intact.
  QStringList topLevelIds;
  for (const QString &id : validIds) {
    QString ownerId = owners.value(id);
    QSet<QString> visited;
    bool hasMovedAncestor = false;
    while (!ownerId.isEmpty() && !visited.contains(ownerId)) {
      if (movedIds.contains(ownerId)) {
        hasMovedAncestor = true;
        break;
      }
      visited.insert(ownerId);
      ownerId = owners.value(ownerId);
    }
    if (!hasMovedAncestor)
      topLevelIds.append(id);
  }

  const bool targetExists =
      targetContainerId.isEmpty() ||
      findContainer(diagram, targetContainerId) != nullptr;
  QHash<QString, QStringList> afterByContainer;
  const auto editableChildren =
      [&](const QString &containerId) -> QStringList & {
    auto found = afterByContainer.find(containerId);
    if (found == afterByContainer.end()) {
      const auto *container = findContainer(diagram, containerId);
      found = afterByContainer.insert(
          containerId,
          container ? container->childPresentationIds : QStringList{});
    }
    return found.value();
  };

  for (const QString &presentationId : topLevelIds) {
    const QString currentOwnerId = owners.value(presentationId);
    QString destinationId = targetContainerId;
    bool validDestination = targetExists;
    if (destinationId == presentationId || movedIds.contains(destinationId))
      validDestination = false;
    if (validDestination && findContainer(diagram, presentationId)) {
      QSet<QString> visited;
      if (containerSubtreeContains(diagram, presentationId, destinationId,
                                   visited))
        validDestination = false;
    }
    // Geometry can still be committed if an impossible programmatic target is
    // supplied, but membership must remain valid and cycle-free.
    if (!validDestination)
      destinationId = currentOwnerId;
    if (destinationId == currentOwnerId)
      continue;

    if (!currentOwnerId.isEmpty())
      editableChildren(currentOwnerId).removeAll(presentationId);
    if (!destinationId.isEmpty()) {
      QStringList &children = editableChildren(destinationId);
      if (!children.contains(presentationId))
        children.append(presentationId);
    }
  }

  QList<ContainerChildrenChange> changes;
  for (const auto &container : diagram.containers) {
    const auto after = afterByContainer.constFind(container.id);
    if (after != afterByContainer.cend() &&
        *after != container.childPresentationIds)
      changes.append(
          {container.id, container.childPresentationIds, after.value()});
  }
  return changes;
}

} // namespace

ProjectController::ProjectController(QObject *parent)
    : QObject(parent), m_data(createStarterProject()), m_diagnostics(this),
      m_treeModel(new ProjectTreeModel(this)),
      m_diagramModel(new DiagramListModel(this)) {
  connect(&m_undoStack, &QUndoStack::canUndoChanged, this,
          &ProjectController::undoStateChanged);
  connect(&m_undoStack, &QUndoStack::canRedoChanged, this,
          &ProjectController::undoStateChanged);
  connect(&m_undoStack, &QUndoStack::undoTextChanged, this,
          &ProjectController::undoStateChanged);
  connect(&m_undoStack, &QUndoStack::redoTextChanged, this,
          &ProjectController::undoStateChanged);
  connect(&m_undoStack, &QUndoStack::cleanChanged, this,
          &ProjectController::dirtyChanged);
  m_undoStack.setClean();
}

ProjectController::~ProjectController() = default;

ProjectTreeModel *ProjectController::treeModel() const { return m_treeModel; }
DiagramListModel *ProjectController::diagramModel() const {
  return m_diagramModel;
}
DiagnosticModel *ProjectController::diagnostics() { return &m_diagnostics; }
const ProjectData &ProjectController::data() const { return m_data; }
QString ProjectController::projectName() const { return m_data.name; }
QString ProjectController::projectPath() const { return m_projectPath; }
QString ProjectController::selectedId() const { return m_selectedId; }
QString ProjectController::selectedKind() const { return m_selectedKind; }

QString ProjectController::selectedType() const {
  if (m_selectedKind == QStringLiteral("element")) {
    if (const auto *element = findElement(m_data, m_selectedId))
      return toString(element->type);
  } else if (m_selectedKind == QStringLiteral("relationship")) {
    if (const auto *relationship = findRelationship(m_data, m_selectedId))
      return toString(relationship->type);
  } else if (m_selectedKind == QStringLiteral("diagram")) {
    return QStringLiteral("class diagram");
  }
  return {};
}

QString ProjectController::selectedName() const {
  if (m_selectedKind == QStringLiteral("element")) {
    if (const auto *element = findElement(m_data, m_selectedId))
      return element->name;
  } else if (m_selectedKind == QStringLiteral("relationship")) {
    if (const auto *relationship = findRelationship(m_data, m_selectedId))
      return relationship->name;
  } else if (m_selectedKind == QStringLiteral("diagram")) {
    if (const auto *diagram = findDiagram(m_data, m_selectedId))
      return diagram->name;
  }
  return {};
}

const ModelElement *ProjectController::selectedElement() const {
  return m_selectedKind == QStringLiteral("element")
             ? findElement(m_data, m_selectedId)
             : nullptr;
}

ModelElement *ProjectController::selectedElement(ProjectData &project) const {
  return m_selectedKind == QStringLiteral("element")
             ? findElement(project, m_selectedId)
             : nullptr;
}

QString ProjectController::selectedAttributes() const {
  const auto *element = selectedElement();
  return element ? element->attributes.join(u'\n') : QString();
}

QString ProjectController::selectedOperations() const {
  const auto *element = selectedElement();
  return element ? element->operations.join(u'\n') : QString();
}

QString ProjectController::selectedLiterals() const {
  const auto *element = selectedElement();
  return element ? element->enumLiterals.join(u'\n') : QString();
}

QString ProjectController::selectedSourceRole() const {
  const auto *relationship = m_selectedKind == QStringLiteral("relationship")
                                 ? findRelationship(m_data, m_selectedId)
                                 : nullptr;
  return relationship ? relationship->sourceEnd.role : QString();
}

QString ProjectController::selectedSourceMultiplicity() const {
  const auto *relationship = m_selectedKind == QStringLiteral("relationship")
                                 ? findRelationship(m_data, m_selectedId)
                                 : nullptr;
  return relationship ? relationship->sourceEnd.multiplicity : QString();
}

QString ProjectController::selectedTargetRole() const {
  const auto *relationship = m_selectedKind == QStringLiteral("relationship")
                                 ? findRelationship(m_data, m_selectedId)
                                 : nullptr;
  return relationship ? relationship->targetEnd.role : QString();
}

QString ProjectController::selectedTargetMultiplicity() const {
  const auto *relationship = m_selectedKind == QStringLiteral("relationship")
                                 ? findRelationship(m_data, m_selectedId)
                                 : nullptr;
  return relationship ? relationship->targetEnd.multiplicity : QString();
}

QString ProjectController::selectedStereotypes() const {
  if (m_selectedKind == QStringLiteral("element")) {
    if (const auto *element = findElement(m_data, m_selectedId))
      return stereotype_catalog::displayText(m_data, element->stereotypeIds);
  } else if (m_selectedKind == QStringLiteral("relationship")) {
    if (const auto *relationship = findRelationship(m_data, m_selectedId))
      return stereotype_catalog::displayText(m_data,
                                             relationship->stereotypeIds);
  }
  return {};
}

bool ProjectController::canUndo() const { return m_undoStack.canUndo(); }
bool ProjectController::canRedo() const { return m_undoStack.canRedo(); }
QString ProjectController::undoText() const { return m_undoStack.undoText(); }
QString ProjectController::redoText() const { return m_undoStack.redoText(); }
bool ProjectController::dirty() const { return !m_undoStack.isClean(); }
QStringList ProjectController::externallyChangedProjectFiles() const {
  return m_externallyChangedProjectFiles;
}
QVariantList ProjectController::diagramStyles() const {
  QVariantList styles;
  styles.reserve(m_data.diagramStyles.size());
  for (const auto &style : m_data.diagramStyles)
    styles.append(diagramStyleMap(style));
  return styles;
}

QVariantList ProjectController::stereotypeCatalog() const {
  QVariantList catalog;
  catalog.reserve(m_data.stereotypeDefinitions.size());
  for (const auto &definition : m_data.stereotypeDefinitions)
    catalog.append(stereotypeDefinitionMap(definition));
  return catalog;
}

void ProjectController::newProject(const QString &name) {
  m_diagnostics.clear();
  m_projectPath.clear();
  m_projectRevision = {};
  setExternallyChangedProjectFiles({}, false);
  m_selectedId.clear();
  m_selectedKind.clear();
  setDataDirect(createStarterProject(name.trimmed().isEmpty()
                                         ? QStringLiteral("New Project")
                                         : name.trimmed()));
  m_diagnostics.addInfo(QStringLiteral("project"),
                        QStringLiteral("Created a new project"));
}

QString ProjectController::normalizedLocalPath(const QUrl &url) const {
  if (url.isEmpty())
    return m_projectPath;
  if (url.isLocalFile())
    return QDir::cleanPath(url.toLocalFile());
  return QDir::cleanPath(url.toString());
}

bool ProjectController::openProject(const QUrl &url) {
  const QString path = normalizedLocalPath(url);
  const auto outcome = ProjectSerializer::load(path);
  logDiagnostics(outcome.diagnostics);
  if (!outcome.ok)
    return false;
  m_projectPath = ProjectSerializer::normalizeProjectPath(path);
  m_projectRevision = outcome.revision;
  setExternallyChangedProjectFiles({}, false);
  m_selectedId.clear();
  m_selectedKind.clear();
  setDataDirect(outcome.project);
  m_diagnostics.addInfo(QStringLiteral("persistence"),
                        QStringLiteral("Opened %1").arg(m_projectPath));
  emit projectOpened(m_projectPath);
  return true;
}

bool ProjectController::saveDestinationContainsProject(const QUrl &url) const {
  const QString path = normalizedLocalPath(url);
  if (path.isEmpty())
    return false;
  const QString destination = ProjectSerializer::normalizeProjectPath(path);
  if (!m_projectPath.isEmpty() &&
      QDir::cleanPath(destination)
              .compare(QDir::cleanPath(m_projectPath), Qt::CaseInsensitive) ==
          0)
    return false;
  return QFileInfo::exists(
      QDir(destination).filePath(QStringLiteral("manifest.json5")));
}

bool ProjectController::saveProject(const QUrl &url, bool overwriteExisting) {
  const QString path = normalizedLocalPath(url);
  if (path.isEmpty()) {
    m_diagnostics.addError(
        QStringLiteral("persistence"),
        QStringLiteral("Choose a project directory before saving"));
    return false;
  }
  if (!overwriteExisting && saveDestinationContainsProject(url)) {
    m_diagnostics.addWarning(
        QStringLiteral("persistence"),
        QStringLiteral("The selected folder already contains a yauml project; "
                       "confirm replacement before saving"));
    return false;
  }
  const QString destination = ProjectSerializer::normalizeProjectPath(path);
  const bool savingCurrentProject =
      !m_projectPath.isEmpty() &&
      QDir::cleanPath(destination)
              .compare(QDir::cleanPath(m_projectPath), Qt::CaseInsensitive) ==
          0;
  const auto outcome = ProjectSerializer::save(
      path, m_data,
      savingCurrentProject ? m_projectRevision : ProjectFileRevision{},
      overwriteExisting);
  logDiagnostics(outcome.diagnostics);
  if (outcome.externalChangesDetected) {
    setExternallyChangedProjectFiles(outcome.externallyChangedFiles, true);
    return false;
  }
  if (!outcome.ok)
    return false;
  m_projectPath = destination;
  m_projectRevision = outcome.revision;
  setExternallyChangedProjectFiles({}, false);
  m_undoStack.setClean();
  emit projectChanged();
  m_diagnostics.addInfo(
      QStringLiteral("persistence"),
      outcome.unchanged
          ? QStringLiteral("Project already matches the saved files")
          : QStringLiteral("Saved %1").arg(m_projectPath));
  return true;
}

bool ProjectController::overwriteExternallyChangedProject() {
  return saveProject({}, true);
}

bool ProjectController::reloadProjectFromDisk() {
  return !m_projectPath.isEmpty() &&
         openProject(QUrl::fromLocalFile(m_projectPath));
}

void ProjectController::undo() { m_undoStack.undo(); }
void ProjectController::redo() { m_undoStack.redo(); }

QVariantMap ProjectController::diagramStyle(const QString &styleId) const {
  const auto *style = findDiagramStyle(m_data, styleId);
  return style ? diagramStyleMap(*style) : QVariantMap{};
}

QString ProjectController::saveDiagramStyle(const QString &styleId,
                                            const QString &name,
                                            const QVariantMap &colors) {
  const QString trimmedName = name.trimmed();
  if (trimmedName.isEmpty()) {
    m_diagnostics.addError(QStringLiteral("style"),
                           QStringLiteral("A diagram style needs a name"));
    return {};
  }
  const auto duplicate = std::find_if(
      m_data.diagramStyles.cbegin(), m_data.diagramStyles.cend(),
      [&](const DiagramStyle &candidate) {
        return candidate.id != styleId &&
               candidate.name.compare(trimmedName, Qt::CaseInsensitive) == 0;
      });
  if (duplicate != m_data.diagramStyles.cend()) {
    m_diagnostics.addError(
        QStringLiteral("style"),
        QStringLiteral("A diagram style named \"%1\" already exists")
            .arg(trimmedName));
    return {};
  }

  DiagramStyle style;
  if (styleId.isEmpty()) {
    style.id = newId();
  } else {
    const auto *existing = findDiagramStyle(m_data, styleId);
    if (!existing)
      return {};
    style = *existing;
  }
  style.name = trimmedName;
  const auto assignColor = [&](QString &target, const QString &key) {
    target = normalizedStyleColor(colors.value(key));
    return !target.isEmpty();
  };
  if (!assignColor(style.fill, QStringLiteral("fill")) ||
      !assignColor(style.headerFill, QStringLiteral("headerFill")) ||
      !assignColor(style.border, QStringLiteral("border")) ||
      !assignColor(style.primaryText, QStringLiteral("primaryText")) ||
      !assignColor(style.secondaryText, QStringLiteral("secondaryText")) ||
      !assignColor(style.divider, QStringLiteral("divider"))) {
    m_diagnostics.addError(
        QStringLiteral("style"),
        QStringLiteral("Every diagram style color must be valid"));
    return {};
  }

  pushCommand(std::make_unique<SaveDiagramStyleCommand>(this, m_data, style));
  return style.id;
}

bool ProjectController::deleteDiagramStyle(const QString &styleId) {
  if (!findDiagramStyle(m_data, styleId))
    return false;
  pushCommand(
      std::make_unique<DeleteDiagramStyleCommand>(this, m_data, styleId));
  return true;
}

int ProjectController::diagramStyleAssignmentCount(
    const QString &styleId) const {
  int count = 0;
  for (const auto &element : m_data.elements)
    count += element.styleId == styleId;
  for (const auto &folder : m_data.browserFolders)
    count += folder.styleId == styleId;
  for (auto assignment = m_data.namespaceStyleIds.cbegin();
       assignment != m_data.namespaceStyleIds.cend(); ++assignment)
    count += assignment.value() == styleId;
  for (const auto &diagram : m_data.diagrams) {
    for (const auto &node : diagram.nodes)
      count += node.styleId == styleId;
    for (const auto &container : diagram.containers)
      count += container.styleId == styleId;
  }
  return count;
}

QString ProjectController::explicitStyleIdForBrowserSubject(
    const QString &kind, const QString &subjectId) const {
  return project_style::explicitStyleId(m_data, kind, subjectId);
}

void ProjectController::assignStyleToBrowserSubject(const QString &kind,
                                                    const QString &subjectId,
                                                    const QString &styleId) {
  if (!styleId.isEmpty() && !findDiagramStyle(m_data, styleId))
    return;
  QString normalizedKind = kind;
  if (normalizedKind == QStringLiteral("package"))
    normalizedKind = QStringLiteral("element");
  const QString before =
      project_style::explicitStyleId(m_data, normalizedKind, subjectId);
  const bool valid = (normalizedKind == QStringLiteral("element") &&
                      findElement(m_data, subjectId)) ||
                     (normalizedKind == QStringLiteral("folder") &&
                      findBrowserFolder(m_data, subjectId)) ||
                     (normalizedKind == QStringLiteral("namespace") &&
                      !subjectId.trimmed().isEmpty());
  if (!valid || before == styleId)
    return;
  pushCommand(std::make_unique<SetStyleAssignmentsCommand>(
      this,
      QList<StyleAssignmentChange>{
          {normalizedKind, {}, subjectId, before, styleId}},
      QStringLiteral("Assign project diagram style")));
}

QString ProjectController::explicitStyleIdForPresentation(
    const QString &diagramId, const QString &presentationId) const {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return {};
  if (const auto *node = findNode(*diagram, presentationId))
    return node->styleId;
  if (const auto *container = findContainer(*diagram, presentationId))
    return container->styleId;
  return {};
}

void ProjectController::assignStyleToPresentations(
    const QString &diagramId, const QStringList &presentationIds,
    const QString &styleId) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram || (!styleId.isEmpty() && !findDiagramStyle(m_data, styleId)))
    return;
  QList<StyleAssignmentChange> changes;
  QSet<QString> seen;
  for (const QString &presentationId : presentationIds) {
    if (seen.contains(presentationId))
      continue;
    seen.insert(presentationId);
    if (const auto *node = findNode(*diagram, presentationId)) {
      if (node->styleId != styleId)
        changes.append({QStringLiteral("node"), diagramId, node->id,
                        node->styleId, styleId});
    } else if (const auto *container =
                   findContainer(*diagram, presentationId)) {
      if (container->styleId != styleId)
        changes.append({QStringLiteral("container"), diagramId, container->id,
                        container->styleId, styleId});
    }
  }
  if (changes.isEmpty())
    return;
  const QString description =
      changes.size() == 1 ? QStringLiteral("Assign presentation style")
                          : QStringLiteral("Assign presentation styles");
  pushCommand(std::make_unique<SetStyleAssignmentsCommand>(
      this, std::move(changes), description));
}

QVariantMap
ProjectController::stereotypeDefinition(const QString &stereotypeId) const {
  const auto *definition = stereotype_catalog::find(m_data, stereotypeId);
  return definition ? stereotypeDefinitionMap(*definition) : QVariantMap{};
}

QString
ProjectController::saveProjectStereotype(const QString &stereotypeId,
                                         const QString &name,
                                         const QStringList &applicableTo) {
  const QString trimmedName = name.trimmed();
  if (trimmedName.isEmpty()) {
    m_diagnostics.addError(QStringLiteral("stereotype"),
                           QStringLiteral("A stereotype needs a name"));
    return {};
  }
  const auto duplicate = std::find_if(
      m_data.stereotypeDefinitions.cbegin(),
      m_data.stereotypeDefinitions.cend(),
      [&](const StereotypeDefinition &candidate) {
        return candidate.id != stereotypeId &&
               candidate.name.compare(trimmedName, Qt::CaseInsensitive) == 0;
      });
  if (duplicate != m_data.stereotypeDefinitions.cend()) {
    m_diagnostics.addError(
        QStringLiteral("stereotype"),
        QStringLiteral("A project stereotype named \"%1\" already exists")
            .arg(trimmedName));
    return {};
  }

  const QSet<QString> supported = {
      QStringLiteral("package"), QStringLiteral("class"),
      QStringLiteral("struct"), QStringLiteral("enumeration"),
      stereotype_catalog::kRelationshipApplicability};
  QStringList normalizedApplicability;
  for (const QString &value : applicableTo) {
    const QString normalized = value.trimmed();
    if (supported.contains(normalized) &&
        !normalizedApplicability.contains(normalized))
      normalizedApplicability.append(normalized);
  }
  if (normalizedApplicability.isEmpty()) {
    m_diagnostics.addError(
        QStringLiteral("stereotype"),
        QStringLiteral("Choose at least one applicable subject type"));
    return {};
  }

  StereotypeDefinition definition;
  if (stereotypeId.isEmpty()) {
    definition.id = newId();
  } else {
    const auto *existing = findStereotypeDefinition(m_data, stereotypeId);
    if (!existing)
      return {};
    definition = *existing;
    for (const auto &element : m_data.elements) {
      if (element.stereotypeIds.contains(stereotypeId) &&
          !normalizedApplicability.contains(
              stereotype_catalog::applicabilityFor(element.type))) {
        m_diagnostics.addError(
            QStringLiteral("stereotype"),
            QStringLiteral(
                "The edited applicability would invalidate an existing "
                "assignment to \"%1\"")
                .arg(element.name),
            element.id);
        return {};
      }
    }
    for (const auto &relationship : m_data.relationships) {
      if (relationship.stereotypeIds.contains(stereotypeId) &&
          !normalizedApplicability.contains(
              stereotype_catalog::kRelationshipApplicability)) {
        m_diagnostics.addError(
            QStringLiteral("stereotype"),
            QStringLiteral(
                "The edited applicability would invalidate an existing "
                "relationship assignment"),
            relationship.id);
        return {};
      }
    }
  }
  definition.name = trimmedName;
  definition.applicableTo = normalizedApplicability;
  pushCommand(std::make_unique<SaveStereotypeDefinitionCommand>(this, m_data,
                                                                definition));
  return definition.id;
}

bool ProjectController::deleteProjectStereotype(const QString &stereotypeId) {
  if (!findStereotypeDefinition(m_data, stereotypeId))
    return false;
  pushCommand(std::make_unique<DeleteStereotypeDefinitionCommand>(
      this, m_data, stereotypeId));
  return true;
}

int ProjectController::stereotypeAssignmentCount(
    const QString &stereotypeId) const {
  int count = 0;
  for (const auto &element : m_data.elements)
    count += element.stereotypeIds.contains(stereotypeId);
  for (const auto &relationship : m_data.relationships)
    count += relationship.stereotypeIds.contains(stereotypeId);
  return count;
}

QVariantList
ProjectController::applicableStereotypes(const QString &kind,
                                         const QString &subjectId) const {
  QString applicability;
  if (kind == QStringLiteral("element")) {
    const auto *element = findElement(m_data, subjectId);
    if (!element)
      return {};
    applicability = stereotype_catalog::applicabilityFor(element->type);
  } else if (kind == QStringLiteral("relationship")) {
    if (!findRelationship(m_data, subjectId))
      return {};
    applicability = stereotype_catalog::kRelationshipApplicability;
  } else {
    return {};
  }
  QVariantList result;
  for (const QVariant &entry : stereotypeCatalog()) {
    const QVariantMap definition = entry.toMap();
    if (definition.value(QStringLiteral("applicableTo"))
            .toStringList()
            .contains(applicability))
      result.append(definition);
  }
  return result;
}

QStringList
ProjectController::stereotypeIdsForObject(const QString &kind,
                                          const QString &subjectId) const {
  if (kind == QStringLiteral("element")) {
    if (const auto *element = findElement(m_data, subjectId))
      return element->stereotypeIds;
  } else if (kind == QStringLiteral("relationship")) {
    if (const auto *relationship = findRelationship(m_data, subjectId))
      return relationship->stereotypeIds;
  }
  return {};
}

void ProjectController::assignStereotypes(const QString &kind,
                                          const QString &subjectId,
                                          const QStringList &stereotypeIds) {
  QString applicability;
  const QStringList before = stereotypeIdsForObject(kind, subjectId);
  if (kind == QStringLiteral("element")) {
    const auto *element = findElement(m_data, subjectId);
    if (!element)
      return;
    applicability = stereotype_catalog::applicabilityFor(element->type);
  } else if (kind == QStringLiteral("relationship")) {
    if (!findRelationship(m_data, subjectId))
      return;
    applicability = stereotype_catalog::kRelationshipApplicability;
  } else {
    return;
  }

  QStringList normalized;
  for (const QString &id : stereotypeIds) {
    const auto *definition = stereotype_catalog::find(m_data, id);
    if (!definition ||
        !stereotype_catalog::appliesTo(*definition, applicability)) {
      m_diagnostics.addError(
          QStringLiteral("stereotype"),
          QStringLiteral("A selected stereotype does not apply to this item"),
          subjectId);
      return;
    }
    if (!normalized.contains(id))
      normalized.append(id);
  }
  if (before == normalized)
    return;
  pushCommand(std::make_unique<SetStereotypeAssignmentsCommand>(
      this, kind, subjectId, before, normalized));
}

int ProjectController::applyCppImportPlan(const CppImportPreview &preview) {
  QList<ModelElement> desiredElements;
  desiredElements.reserve(preview.elementApplicableCount());
  for (const auto &item : preview.items)
    if (item.isApplicable())
      desiredElements.append(item.appliedElement());
  QList<Relationship> desiredRelationships;
  desiredRelationships.reserve(preview.relationshipApplicableCount());
  for (const auto &item : preview.relationshipItems)
    if (item.isApplicable())
      desiredRelationships.append(item.appliedRelationship());
  const QStringList sourceRoots =
      !preview.sourceRoots.isEmpty()
          ? preview.sourceRoots
          : (preview.sourceRoot.isEmpty() ? QStringList{}
                                          : QStringList{preview.sourceRoot});
  const bool sourceRootsChanged =
      !sourceRoots.isEmpty() && sourceRoots != m_data.cppImport.sourceRoots;
  if (desiredElements.isEmpty() && desiredRelationships.isEmpty() &&
      !sourceRootsChanged)
    return 0;
  const int count = desiredElements.size() + desiredRelationships.size();
  pushCommand(std::make_unique<ApplyCppImportCommand>(
      this, m_data, std::move(desiredElements), std::move(desiredRelationships),
      sourceRoots));
  return count;
}

QString ProjectController::addElement(const QString &type,
                                      const QString &diagramId) {
  return addElementAtImpl(type, diagramId,
                          std::numeric_limits<qreal>::quiet_NaN(),
                          std::numeric_limits<qreal>::quiet_NaN(), false);
}

QString ProjectController::addElementAt(const QString &type,
                                        const QString &diagramId, qreal x,
                                        qreal y) {
  return addElementAtImpl(type, diagramId, x, y, false);
}

QString ProjectController::addElementCenteredAt(const QString &type,
                                                const QString &diagramId,
                                                qreal centerX, qreal centerY) {
  return addElementAtImpl(type, diagramId, centerX, centerY, true);
}

QString ProjectController::addElementAtImpl(const QString &type,
                                            const QString &diagramId, qreal x,
                                            qreal y,
                                            bool coordinatesAreCenter) {
  bool ok = false;
  const ElementType elementType = elementTypeFromString(type, &ok);
  if (!ok) {
    m_diagnostics.addError(
        QStringLiteral("command"),
        QStringLiteral("Unknown element type: %1").arg(type));
    return {};
  }
  ModelElement element;
  element.id = newId();
  element.type = elementType;
  int sameType = 0;
  for (const auto &candidate : m_data.elements)
    if (candidate.type == elementType)
      ++sameType;
  const QString label =
      elementType == ElementType::Enumeration
          ? QStringLiteral("Enumeration")
          : toString(elementType)
                .replace(0, 1, toString(elementType).left(1).toUpper());
  element.name = label + QString::number(sameType + 1);
  if (elementType == ElementType::Class || elementType == ElementType::Struct) {
    element.attributes = {QStringLiteral("+ attribute: Type")};
    element.operations = {QStringLiteral("+ operation(): void")};
  } else if (elementType == ElementType::Enumeration) {
    element.enumLiterals = {QStringLiteral("Value")};
  }

  std::optional<NodePresentation> nodePresentation;
  std::optional<ContainerPresentation> containerPresentation;
  if (const auto *diagram = findDiagram(m_data, diagramId)) {
    if (elementType == ElementType::Package) {
      ContainerPresentation container;
      container.id = newId();
      container.subjectKind = QStringLiteral("package");
      container.subjectId = element.id;
      container.geometry =
          QRectF(kDefaultNodeX + diagram->containers.size() * 36.0,
                 kDefaultNodeY + diagram->containers.size() * 36.0,
                 presentation_layout::kMinimumContainerWidth, 200.0);
      if (std::isfinite(x) && std::isfinite(y)) {
        container.geometry.moveTopLeft({x, y});
        if (coordinatesAreCenter)
          container.geometry.moveCenter({x, y});
      }
      containerPresentation = std::move(container);
    } else {
      NodePresentation node;
      node.id = newId();
      node.elementId = element.id;
      const QSizeF contentSize = presentation_layout::nodeContentSize(
          m_data, element, diagram->showAttributes, diagram->showOperations);
      if (std::isfinite(x) && std::isfinite(y)) {
        node.geometry = QRectF(QPointF(x, y), contentSize);
        if (coordinatesAreCenter)
          node.geometry.moveCenter({x, y});
      } else {
        node.geometry = firstAvailableNodeGeometry(*diagram, contentSize);
      }
      nodePresentation = std::move(node);
    }
  }
  pushCommand(std::make_unique<CreateElementCommand>(
      this, m_data, element, diagramId, std::move(nodePresentation),
      std::move(containerPresentation)));
  selectObject(element.id, QStringLiteral("element"));
  return element.id;
}

QString ProjectController::addDiagram() {
  Diagram diagram;
  diagram.id = newId();
  diagram.name =
      QStringLiteral("Class Diagram %1").arg(m_data.diagrams.size() + 1);
  pushCommand(std::make_unique<CreateDiagramCommand>(this, m_data, diagram));
  selectObject(diagram.id, QStringLiteral("diagram"));
  return diagram.id;
}

QString ProjectController::addBrowserFolder(const QString &parentKind,
                                            const QString &parentId,
                                            const QString &name) {
  const BrowserParent parent{parentKind, parentId};
  const QString trimmedName = name.trimmed();
  if (trimmedName.isEmpty() || !browserContainerExists(m_data, parent)) {
    m_diagnostics.addError(QStringLiteral("command"),
                           QStringLiteral("Cannot create browser folder at "
                                          "the selected location"));
    return {};
  }

  BrowserFolder folder;
  folder.id = newId();
  folder.name = trimmedName;
  folder.parent = parent;
  pushCommand(
      std::make_unique<CreateBrowserFolderCommand>(this, m_data, folder));
  return folder.id;
}

void ProjectController::renameBrowserFolder(const QString &folderId,
                                            const QString &name) {
  const BrowserFolder *folder = findBrowserFolder(m_data, folderId);
  const QString trimmedName = name.trimmed();
  if (!folder || trimmedName.isEmpty() || folder->name == trimmedName)
    return;
  pushCommand(std::make_unique<RenameBrowserFolderCommand>(
      this, folderId, folder->name, trimmedName));
}

void ProjectController::deleteBrowserFolder(const QString &folderId) {
  if (!findBrowserFolder(m_data, folderId))
    return;
  pushCommand(
      std::make_unique<DeleteBrowserFolderCommand>(this, m_data, folderId));
}

void ProjectController::deleteBrowserItems(const QString &itemsJson) {
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(itemsJson.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray())
    return;

  QStringList folderIds;
  QStringList elementIds;
  QStringList diagramIds;
  QSet<QString> seen;
  for (const QJsonValue &value : document.array()) {
    const QJsonObject object = value.toObject();
    const QString kind = object.value(QStringLiteral("kind")).toString();
    const QString id = object.value(QStringLiteral("id")).toString();
    const QString key = kind + u':' + id;
    if (id.isEmpty() || seen.contains(key))
      continue;
    seen.insert(key);
    if (kind == QStringLiteral("folder") && findBrowserFolder(m_data, id))
      folderIds.append(id);
    else if (kind == QStringLiteral("element") && findElement(m_data, id))
      elementIds.append(id);
    else if (kind == QStringLiteral("diagram") && findDiagram(m_data, id))
      diagramIds.append(id);
  }
  const int requestedCount =
      folderIds.size() + elementIds.size() + diagramIds.size();
  if (requestedCount == 0)
    return;

  m_undoStack.beginMacro(
      requestedCount == 1
          ? QStringLiteral("Delete project-tree item")
          : QStringLiteral("Delete %1 project-tree items").arg(requestedCount));
  for (const QString &folderId : folderIds)
    pushCommand(
        std::make_unique<DeleteBrowserFolderCommand>(this, m_data, folderId));
  for (const QString &elementId : elementIds)
    pushCommand(
        std::make_unique<DeleteElementCommand>(this, m_data, elementId));
  for (const QString &diagramId : diagramIds)
    deleteDiagram(diagramId);
  m_undoStack.endMacro();
  clearSelection();
}

bool ProjectController::canReorderBrowserItem(const QString &kind,
                                              const QString &id,
                                              int direction) const {
  if ((kind != QStringLiteral("element") && kind != QStringLiteral("folder")) ||
      direction == 0)
    return false;
  const QString key = browserSubjectKey(kind, id);
  const QString parent = effectiveBrowserParentKey(m_data, kind, id);
  QStringList siblings;
  for (const QString &candidate : normalizedBrowserItemOrder(m_data)) {
    const int separator = candidate.indexOf(u':');
    if (separator > 0 &&
        effectiveBrowserParentKey(m_data, candidate.left(separator),
                                  candidate.mid(separator + 1)) == parent)
      siblings.append(candidate);
  }
  const int position = siblings.indexOf(key);
  return position >= 0 &&
         (direction < 0 ? position > 0 : position + 1 < siblings.size());
}

bool ProjectController::reorderBrowserItem(const QString &kind,
                                           const QString &id, int direction) {
  if (!canReorderBrowserItem(kind, id, direction))
    return false;
  QStringList after = normalizedBrowserItemOrder(m_data);
  const QString key = browserSubjectKey(kind, id);
  const QString parent = effectiveBrowserParentKey(m_data, kind, id);
  QString adjacentKey;
  bool found = false;
  for (int index = 0; index < after.size(); ++index) {
    const QString &candidate = after.at(index);
    const int separator = candidate.indexOf(u':');
    if (separator <= 0 ||
        effectiveBrowserParentKey(m_data, candidate.left(separator),
                                  candidate.mid(separator + 1)) != parent)
      continue;
    if (candidate == key) {
      if (direction < 0)
        break;
      found = true;
      continue;
    }
    if (direction < 0)
      adjacentKey = candidate;
    else if (found) {
      adjacentKey = candidate;
      break;
    }
  }
  const int itemIndex = after.indexOf(key);
  const int adjacentIndex = after.indexOf(adjacentKey);
  if (itemIndex < 0 || adjacentIndex < 0)
    return false;
  std::swap(after[itemIndex], after[adjacentIndex]);
  pushCommand(std::make_unique<ReorderBrowserItemsCommand>(
      this, m_data.browserItemOrder, std::move(after)));
  return true;
}

bool ProjectController::canReorderBrowserItemsAround(
    const QString &itemsJson, const QString &targetKind,
    const QString &targetId) const {
  if (targetKind != QStringLiteral("element") &&
      targetKind != QStringLiteral("folder"))
    return false;
  const QString targetKey = browserSubjectKey(targetKind, targetId);
  const QStringList movedKeys = browserItemKeysFromJson(m_data, itemsJson);
  if (targetKey.isEmpty() || movedKeys.isEmpty() ||
      movedKeys.contains(targetKey))
    return false;

  const QString targetParent =
      effectiveBrowserParentKey(m_data, targetKind, targetId);
  return std::all_of(
      movedKeys.cbegin(), movedKeys.cend(), [&](const QString &key) {
        const int separator = key.indexOf(u':');
        return separator > 0 && effectiveBrowserParentKey(
                                    m_data, key.left(separator),
                                    key.mid(separator + 1)) == targetParent;
      });
}

bool ProjectController::reorderBrowserItemsAround(const QString &itemsJson,
                                                  const QString &targetKind,
                                                  const QString &targetId,
                                                  bool before) {
  if (!canReorderBrowserItemsAround(itemsJson, targetKind, targetId))
    return false;

  const QStringList normalized = normalizedBrowserItemOrder(m_data);
  const QStringList requestedKeys = browserItemKeysFromJson(m_data, itemsJson);
  const QSet<QString> requested(requestedKeys.cbegin(), requestedKeys.cend());
  QStringList movedKeys;
  QStringList after;
  movedKeys.reserve(requested.size());
  after.reserve(normalized.size());
  for (const QString &key : normalized) {
    if (requested.contains(key))
      movedKeys.append(key);
    else
      after.append(key);
  }

  const QString targetKey = browserSubjectKey(targetKind, targetId);
  int insertionIndex = after.indexOf(targetKey);
  if (insertionIndex < 0)
    return false;
  if (!before)
    ++insertionIndex;
  for (qsizetype index = 0; index < movedKeys.size(); ++index)
    after.insert(insertionIndex + index, movedKeys.at(index));
  if (after == normalized)
    return false;

  pushCommand(std::make_unique<ReorderBrowserItemsCommand>(
      this, m_data.browserItemOrder, std::move(after),
      static_cast<int>(movedKeys.size())));
  return true;
}

bool ProjectController::moveBrowserItems(const QString &itemsJson,
                                         const QString &targetKind,
                                         const QString &targetId) {
  return moveBrowserItemsImpl(itemsJson, targetKind, targetId, false);
}

bool ProjectController::moveBrowserItemsWithPackageReassignment(
    const QString &itemsJson, const QString &targetKind,
    const QString &targetId) {
  return moveBrowserItemsWithSemanticReassignment(itemsJson, targetKind,
                                                  targetId);
}

bool ProjectController::moveBrowserItemsWithSemanticReassignment(
    const QString &itemsJson, const QString &targetKind,
    const QString &targetId) {
  return moveBrowserItemsImpl(itemsJson, targetKind, targetId, true);
}

bool ProjectController::moveBrowserItemsImpl(const QString &itemsJson,
                                             const QString &targetKind,
                                             const QString &targetId,
                                             bool reassignPackage) {
  const BrowserParent target{targetKind, targetId};
  if (!browserContainerExists(m_data, target))
    return false;

  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(itemsJson.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray())
    return false;

  struct BrowserItem {
    QString kind;
    QString id;
  };
  QList<BrowserItem> requested;
  QSet<QString> requestedKeys;
  for (const QJsonValue &value : document.array()) {
    const QJsonObject object = value.toObject();
    const QString kind = object.value(QStringLiteral("kind")).toString();
    const QString id = object.value(QStringLiteral("id")).toString();
    const QString key = browserSubjectKey(kind, id);
    const bool exists = kind == QStringLiteral("element")
                            ? findElement(m_data, id) != nullptr
                        : kind == QStringLiteral("folder")
                            ? findBrowserFolder(m_data, id) != nullptr
                            : false;
    if (!key.isEmpty() && exists && !requestedKeys.contains(key)) {
      requested.append({kind, id});
      requestedKeys.insert(key);
    }
  }
  if (requested.isEmpty())
    return false;

  // If both a folder/owning type and one of its descendants are selected, move
  // only the ancestor. Its subtree remains intact instead of being flattened.
  QList<BrowserItem> topLevelItems;
  for (const auto &item : requested) {
    bool hasSelectedAncestor = false;
    QSet<QString> visited;
    QString parent = effectiveBrowserParentKey(m_data, item.kind, item.id);
    while (!parent.isEmpty() && !visited.contains(parent)) {
      if (requestedKeys.contains(parent)) {
        hasSelectedAncestor = true;
        break;
      }
      visited.insert(parent);
      const int separator = parent.indexOf(u':');
      parent = separator > 0
                   ? effectiveBrowserParentKey(m_data, parent.left(separator),
                                               parent.mid(separator + 1))
                   : QString{};
    }
    if (!hasSelectedAncestor)
      topLevelItems.append(item);
  }

  const QString targetKey = browserParentSubjectKey(target);
  QStringList elementIds;
  QStringList folderIds;
  for (const auto &item : topLevelItems) {
    const QString itemKey = browserSubjectKey(item.kind, item.id);
    QSet<QString> visited;
    QString ancestor = targetKey;
    while (!ancestor.isEmpty() && !visited.contains(ancestor)) {
      if (ancestor == itemKey) {
        m_diagnostics.addError(
            QStringLiteral("command"),
            QStringLiteral("A project-tree item cannot be moved into itself "
                           "or one of its descendants"),
            item.id);
        return false;
      }
      visited.insert(ancestor);
      const int separator = ancestor.indexOf(u':');
      ancestor =
          separator > 0
              ? effectiveBrowserParentKey(m_data, ancestor.left(separator),
                                          ancestor.mid(separator + 1))
              : QString{};
    }
    if (item.kind == QStringLiteral("element"))
      elementIds.append(item.id);
    else
      folderIds.append(item.id);
  }

  const QString targetPackageId =
      reassignPackage ? packageIdForBrowserTarget(m_data, targetKind, targetId)
                      : QString{};
  const QString targetEnclosingTypeId =
      reassignPackage
          ? enclosingTypeIdForBrowserTarget(m_data, targetKind, targetId)
          : QString{};

  const bool changesAnything =
      std::any_of(
          elementIds.cbegin(), elementIds.cend(),
          [&](const QString &id) {
            const ModelElement *element = findElement(m_data, id);
            return element &&
                   (element->browserParent != target ||
                    (reassignPackage &&
                     (element->packageId != targetPackageId ||
                      (element->type != ElementType::Package &&
                       element->enclosingTypeId != targetEnclosingTypeId))));
          }) ||
      std::any_of(folderIds.cbegin(), folderIds.cend(), [&](const QString &id) {
        const BrowserFolder *folder = findBrowserFolder(m_data, id);
        return folder && folder->parent != target;
      });
  if (!changesAnything)
    return false;
  pushCommand(std::make_unique<MoveBrowserItemsCommand>(
      this, m_data, elementIds, folderIds, target,
      reassignPackage ? std::optional<QString>(targetPackageId) : std::nullopt,
      reassignPackage ? std::optional<QString>(targetEnclosingTypeId)
                      : std::nullopt));
  return true;
}

QString ProjectController::browserMovePackageChangeSummary(
    const QString &itemsJson, const QString &targetKind,
    const QString &targetId) const {
  return browserMoveSemanticChangeSummary(itemsJson, targetKind, targetId);
}

QString ProjectController::browserMoveSemanticChangeSummary(
    const QString &itemsJson, const QString &targetKind,
    const QString &targetId) const {
  const QString targetPackageId =
      packageIdForBrowserTarget(m_data, targetKind, targetId);
  const QString targetEnclosingTypeId =
      enclosingTypeIdForBrowserTarget(m_data, targetKind, targetId);

  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(itemsJson.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray())
    return {};

  QSet<QString> changedElementIds;
  for (const QJsonValue &value : document.array()) {
    const QJsonObject object = value.toObject();
    if (object.value(QStringLiteral("kind")).toString() !=
        QStringLiteral("element"))
      continue;
    const auto *element =
        findElement(m_data, object.value(QStringLiteral("id")).toString());
    const QString desiredOwner =
        element && element->type != ElementType::Package ? targetEnclosingTypeId
                                                         : QString{};
    if (element && (element->packageId != targetPackageId ||
                    element->enclosingTypeId != desiredOwner))
      changedElementIds.insert(element->id);
  }
  return semanticReassignmentPrompt(m_data, changedElementIds, targetPackageId,
                                    targetEnclosingTypeId);
}

void ProjectController::addSelectedToDiagram(const QString &diagramId,
                                             const QString &sizingMode) {
  if (m_selectedKind != QStringLiteral("element"))
    return;
  const ModelElement *element = findElement(m_data, m_selectedId);
  if (element && element->type == ElementType::Package) {
    const QModelIndex packageIndex =
        m_treeModel->indexForObject(m_selectedId, QStringLiteral("element"));
    const QStringList containedElements =
        m_treeModel->elementIdsForIndexes({packageIndex});
    QJsonObject subject;
    subject.insert(QStringLiteral("kind"), QStringLiteral("element"));
    subject.insert(QStringLiteral("id"), m_selectedId);
    addTreeItemsToDiagram(
        diagramId, containedElements,
        QString::fromUtf8(
            QJsonDocument(QJsonArray{subject}).toJson(QJsonDocument::Compact)),
        std::numeric_limits<qreal>::quiet_NaN(),
        std::numeric_limits<qreal>::quiet_NaN(), sizingMode);
    return;
  }
  addElementsToDiagram(diagramId, {m_selectedId},
                       std::numeric_limits<qreal>::quiet_NaN(),
                       std::numeric_limits<qreal>::quiet_NaN(), sizingMode);
}

int ProjectController::addElementsToDiagram(const QString &diagramId,
                                            const QStringList &elementIds,
                                            qreal x, qreal y,
                                            const QString &sizingMode) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram || elementIds.isEmpty())
    return 0;

  QSet<QString> existingElementIds;
  existingElementIds.reserve(diagram->nodes.size());
  for (const auto &node : diagram->nodes)
    existingElementIds.insert(node.elementId);

  QStringList acceptedIds;
  QSet<QString> seenIds;
  int duplicateCount = 0;
  for (const auto &elementId : elementIds) {
    if (seenIds.contains(elementId) || !findElement(m_data, elementId))
      continue;
    seenIds.insert(elementId);
    if (existingElementIds.contains(elementId)) {
      ++duplicateCount;
      continue;
    }
    acceptedIds.append(elementId);
  }
  if (acceptedIds.isEmpty()) {
    m_diagnostics.addWarning(
        QStringLiteral("command"),
        duplicateCount == 1
            ? QStringLiteral("The selected element is already on this diagram")
            : QStringLiteral(
                  "The selected elements are already on this diagram"));
    return 0;
  }

  QList<NodePresentation> presentations;
  presentations.reserve(acceptedIds.size());
  Diagram prospective = *diagram;
  const bool hasDropPosition = std::isfinite(x) && std::isfinite(y);
  constexpr int kBulkPlacementColumns = 5;
  QHash<QString, QSizeF> contentSizeByElement;
  qreal placementWidth = 0.0;
  qreal placementHeight = 0.0;
  for (const QString &elementId : acceptedIds) {
    const QSizeF size = nodePlacementSizeForDiagram(
        m_data, *diagram, *findElement(m_data, elementId), sizingMode);
    contentSizeByElement.insert(elementId, size);
    placementWidth = std::max(placementWidth, size.width() + 24.0);
    placementHeight = std::max(placementHeight, size.height() + 24.0);
  }
  for (int index = 0; index < acceptedIds.size(); ++index) {
    NodePresentation node;
    node.id = newId();
    node.elementId = acceptedIds.at(index);
    const QSizeF contentSize = contentSizeByElement.value(node.elementId);
    if (hasDropPosition) {
      const int column = index % kBulkPlacementColumns;
      const int row = index / kBulkPlacementColumns;
      node.geometry = QRectF(
          QPointF(x + column * placementWidth, y + row * placementHeight),
          contentSize);
    } else {
      node.geometry = firstAvailableNodeGeometry(prospective, contentSize);
    }
    prospective.nodes.append(node);
    presentations.append(std::move(node));
  }

  QList<ConnectorPresentation> connectors;
  QSet<QString> newConnectorIds;
  for (const auto &elementId : acceptedIds) {
    auto additions =
        connectorsForNewPresentation(m_data, prospective, elementId);
    for (const auto &connector : additions)
      newConnectorIds.insert(connector.id);
    prospective.connectors.append(additions);
    connectors.append(std::move(additions));
  }
  attachNewConnectorsToSnapPoints(m_data, prospective, newConnectorIds);
  copyAutoAttachedPresentations(prospective, presentations, connectors);
  QSet<QString> newNodeIds;
  for (const auto &presentation : presentations)
    newNodeIds.insert(presentation.id);
  const QList<NodePortSnapPointChange> portChanges =
      existingNodePortChanges(*diagram, prospective, newNodeIds);
  pushCommand(std::make_unique<AddElementsToDiagramCommand>(
      this, m_data, diagramId, std::move(presentations), std::move(connectors),
      portChanges));
  if (duplicateCount > 0) {
    m_diagnostics.addInfo(
        QStringLiteral("command"),
        QStringLiteral("Skipped %1 element(s) already present on the diagram")
            .arg(duplicateCount));
  }
  return acceptedIds.size();
}

int ProjectController::relatedElementCountForDiagram(
    const QString &diagramId, const QString &nodeId,
    const QString &direction) const {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *node = diagram ? findNode(*diagram, nodeId) : nullptr;
  if (!node)
    return 0;
  const bool incoming = direction == QStringLiteral("incoming");
  if (!incoming && direction != QStringLiteral("outgoing"))
    return 0;
  return missingRelatedTypeIds(m_data, *diagram, node->elementId, incoming)
      .size();
}

int ProjectController::addRelatedElementsToDiagram(const QString &diagramId,
                                                   const QString &nodeId,
                                                   const QString &direction,
                                                   const QString &sizingMode) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *node = diagram ? findNode(*diagram, nodeId) : nullptr;
  if (!node)
    return 0;
  const bool incoming = direction == QStringLiteral("incoming");
  if (!incoming && direction != QStringLiteral("outgoing")) {
    m_diagnostics.addError(
        QStringLiteral("command"),
        QStringLiteral("Unknown relationship direction: %1").arg(direction));
    return 0;
  }

  const QStringList relatedIds =
      missingRelatedTypeIds(m_data, *diagram, node->elementId, incoming);
  if (relatedIds.isEmpty()) {
    m_diagnostics.addInfo(
        QStringLiteral("command"),
        incoming
            ? QStringLiteral(
                  "All types that depend on this type are already shown")
            : QStringLiteral(
                  "All types on which this type depends are already shown"));
    return 0;
  }

  constexpr int kBulkPlacementColumns = 5;
  qreal x = node->geometry.right() + 80.0;
  if (incoming) {
    qreal widest = 0.0;
    for (const QString &relatedId : relatedIds) {
      if (const auto *element = findElement(m_data, relatedId)) {
        widest = std::max(widest, nodePlacementSizeForDiagram(
                                      m_data, *diagram, *element, sizingMode)
                                          .width() +
                                      24.0);
      }
    }
    const int columns =
        std::min(static_cast<int>(relatedIds.size()), kBulkPlacementColumns);
    x = node->geometry.left() - columns * widest - 80.0;
  }
  return addElementsToDiagram(diagramId, relatedIds, x, node->geometry.top(),
                              sizingMode);
}

int ProjectController::addTreeItemsToDiagram(const QString &diagramId,
                                             const QStringList &elementIds,
                                             const QString &subjectsJson,
                                             qreal x, qreal y,
                                             const QString &sizingMode) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return 0;

  struct ContainerSubject {
    QString kind;
    QString id;
    QString browserKind;

    QString key() const { return kind + u':' + id; }
  };

  const auto containerSubjectForBrowserItem =
      [&](const QString &kind,
          const QString &id) -> std::optional<ContainerSubject> {
    if (kind == QStringLiteral("folder") && findBrowserFolder(m_data, id))
      return ContainerSubject{QStringLiteral("folder"), id, kind};
    const ModelElement *element =
        kind == QStringLiteral("element") ? findElement(m_data, id) : nullptr;
    if (element && element->type == ElementType::Package)
      return ContainerSubject{QStringLiteral("package"), id, kind};
    return std::nullopt;
  };

  QJsonParseError parseError;
  const QJsonDocument subjects =
      QJsonDocument::fromJson(subjectsJson.toUtf8(), &parseError);
  QSet<QString> rootBrowserKeys;
  if (parseError.error == QJsonParseError::NoError && subjects.isArray()) {
    for (const QJsonValue &value : subjects.array()) {
      const QJsonObject object = value.toObject();
      const QString kind = object.value(QStringLiteral("kind")).toString();
      const QString id = object.value(QStringLiteral("id")).toString();
      if (containerSubjectForBrowserItem(kind, id))
        rootBrowserKeys.insert(browserSubjectKey(kind, id));
    }
  }
  if (rootBrowserKeys.isEmpty())
    return addElementsToDiagram(diagramId, elementIds, x, y, sizingMode);

  const auto parentOfKey = [&](const QString &key) {
    const int separator = key.indexOf(u':');
    return separator > 0
               ? effectiveBrowserParentKey(m_data, key.left(separator),
                                           key.mid(separator + 1))
               : QString{};
  };
  const auto belongsToSelectedContainer = [&](const QString &kind,
                                              const QString &id) {
    QSet<QString> visited;
    QString current = browserSubjectKey(kind, id);
    while (!current.isEmpty() && !visited.contains(current)) {
      visited.insert(current);
      if (rootBrowserKeys.contains(current))
        return true;
      current = parentOfKey(current);
    }
    return false;
  };

  QList<ContainerSubject> includedSubjects;
  for (const auto &folder : m_data.browserFolders)
    if (belongsToSelectedContainer(QStringLiteral("folder"), folder.id))
      includedSubjects.append(
          {QStringLiteral("folder"), folder.id, QStringLiteral("folder")});
  for (const auto &element : m_data.elements)
    if (element.type == ElementType::Package &&
        belongsToSelectedContainer(QStringLiteral("element"), element.id))
      includedSubjects.append(
          {QStringLiteral("package"), element.id, QStringLiteral("element")});

  QSet<QString> includedSubjectKeys;
  for (const auto &subject : includedSubjects)
    includedSubjectKeys.insert(subject.key());

  const auto subjectKeyForBrowserKey = [&](const QString &browserKey) {
    const int separator = browserKey.indexOf(u':');
    if (separator <= 0)
      return QString{};
    const auto subject = containerSubjectForBrowserItem(
        browserKey.left(separator), browserKey.mid(separator + 1));
    return subject ? subject->key() : QString{};
  };
  const auto containerDepth = [&](const ContainerSubject &subject) {
    int depth = 0;
    QSet<QString> visited;
    QString current =
        effectiveBrowserParentKey(m_data, subject.browserKind, subject.id);
    while (!current.isEmpty() && !visited.contains(current)) {
      visited.insert(current);
      if (includedSubjectKeys.contains(subjectKeyForBrowserKey(current)))
        ++depth;
      current = parentOfKey(current);
    }
    return depth;
  };
  std::stable_sort(
      includedSubjects.begin(), includedSubjects.end(),
      [&](const ContainerSubject &left, const ContainerSubject &right) {
        return containerDepth(left) < containerDepth(right);
      });

  QSet<QString> requestedElements(elementIds.cbegin(), elementIds.cend());
  QStringList acceptedElementIds;
  for (const auto &element : m_data.elements)
    if (requestedElements.contains(element.id) &&
        element.type != ElementType::Package)
      acceptedElementIds.append(element.id);

  const auto includedContainerAncestors = [&](const QString &kind,
                                              const QString &id) {
    QStringList result;
    QSet<QString> visited;
    QString current = effectiveBrowserParentKey(m_data, kind, id);
    while (!current.isEmpty() && !visited.contains(current)) {
      visited.insert(current);
      const QString subjectKey = subjectKeyForBrowserKey(current);
      if (includedSubjectKeys.contains(subjectKey))
        result.append(subjectKey);
      current = parentOfKey(current);
    }
    return result;
  };
  QHash<QString, QStringList> containerAncestorsByElement;
  for (const QString &elementId : acceptedElementIds)
    containerAncestorsByElement.insert(
        elementId,
        includedContainerAncestors(QStringLiteral("element"), elementId));

  Diagram prospective = *diagram;
  QHash<QString, QString> nodeIdByElement;
  QHash<QString, QRectF> nodeGeometryByElement;
  for (const auto &node : diagram->nodes) {
    nodeIdByElement.insert(node.elementId, node.id);
    nodeGeometryByElement.insert(node.elementId, node.geometry);
  }

  QList<NodePresentation> newNodes;
  const bool hasDropPosition = std::isfinite(x) && std::isfinite(y);
  constexpr int kBulkPlacementColumns = 5;
  QHash<QString, QSizeF> contentSizeByElement;
  qreal placementWidth = 0.0;
  qreal placementHeight = 0.0;
  for (const QString &elementId : acceptedElementIds) {
    const auto *element = findElement(m_data, elementId);
    if (!element)
      continue;
    const QSizeF size =
        nodePlacementSizeForDiagram(m_data, *diagram, *element, sizingMode);
    contentSizeByElement.insert(elementId, size);
    placementWidth = std::max(placementWidth, size.width() + 24.0);
    placementHeight = std::max(placementHeight, size.height() + 24.0);
  }
  int placementIndex = 0;
  for (const QString &elementId : acceptedElementIds) {
    if (nodeIdByElement.contains(elementId))
      continue;
    NodePresentation node;
    node.id = newId();
    node.elementId = elementId;
    const QSizeF contentSize = contentSizeByElement.value(elementId);
    if (hasDropPosition) {
      const int column = placementIndex % kBulkPlacementColumns;
      const int row = placementIndex / kBulkPlacementColumns;
      node.geometry = QRectF(QPointF(x + 30.0 + column * placementWidth,
                                     y + 50.0 + row * placementHeight),
                             contentSize);
    } else {
      node.geometry = firstAvailableNodeGeometry(prospective, contentSize);
    }
    ++placementIndex;
    nodeIdByElement.insert(elementId, node.id);
    nodeGeometryByElement.insert(elementId, node.geometry);
    prospective.nodes.append(node);
    newNodes.append(std::move(node));
  }

  QHash<QString, QString> containerIdBySubject;
  for (const auto &container : diagram->containers)
    containerIdBySubject.insert(
        container.subjectKind + u':' + container.subjectId, container.id);

  QList<ContainerPresentation> newContainers;
  QHash<QString, qsizetype> newContainerIndexById;
  int emptyContainerIndex = 0;
  for (const auto &subject : includedSubjects) {
    if (containerIdBySubject.contains(subject.key()))
      continue;
    ContainerPresentation container;
    container.id = newId();
    container.subjectKind = subject.kind;
    container.subjectId = subject.id;

    QRectF contentBounds;
    bool hasContent = false;
    for (const QString &elementId : acceptedElementIds) {
      if (!containerAncestorsByElement.value(elementId).contains(
              subject.key()) ||
          !nodeGeometryByElement.contains(elementId))
        continue;
      const QRectF geometry = nodeGeometryByElement.value(elementId);
      contentBounds = hasContent ? contentBounds.united(geometry) : geometry;
      hasContent = true;
    }
    if (hasContent) {
      container.geometry = contentBounds.adjusted(
          -presentation_layout::kContainerHorizontalPadding,
          -presentation_layout::kContainerTopPadding,
          presentation_layout::kContainerHorizontalPadding,
          presentation_layout::kContainerBottomPadding);
      container.geometry.setWidth(
          qMax(presentation_layout::kMinimumContainerWidth,
               container.geometry.width()));
      container.geometry.setHeight(
          qMax(presentation_layout::kMinimumContainerHeight,
               container.geometry.height()));
    } else {
      const qreal baseX = hasDropPosition ? x : kDefaultNodeX;
      const qreal baseY = hasDropPosition ? y : kDefaultNodeY;
      container.geometry =
          QRectF(baseX + emptyContainerIndex * 36.0,
                 baseY + emptyContainerIndex * 36.0,
                 presentation_layout::kMinimumContainerWidth, 200.0);
      ++emptyContainerIndex;
    }
    containerIdBySubject.insert(subject.key(), container.id);
    newContainerIndexById.insert(container.id, newContainers.size());
    newContainers.append(std::move(container));
  }

  // A nested frame can be wider than the leaf nodes that established its
  // initial bounds (notably because frames have a minimum size). Expand each
  // newly created ancestor around all descendant frames after every frame has
  // concrete geometry.
  for (const auto &subject : includedSubjects) {
    const QString containerId = containerIdBySubject.value(subject.key());
    const auto parentIndex = newContainerIndexById.constFind(containerId);
    if (parentIndex == newContainerIndexById.cend())
      continue;
    QRectF expanded = newContainers[*parentIndex].geometry;
    for (const auto &candidate : includedSubjects) {
      if (candidate.key() == subject.key() ||
          !includedContainerAncestors(candidate.browserKind, candidate.id)
               .contains(subject.key()))
        continue;
      const QString candidateId = containerIdBySubject.value(candidate.key());
      QRectF candidateGeometry;
      const auto newCandidate = newContainerIndexById.constFind(candidateId);
      if (newCandidate != newContainerIndexById.cend())
        candidateGeometry = newContainers[*newCandidate].geometry;
      else if (const auto *existing = findContainer(*diagram, candidateId))
        candidateGeometry = existing->geometry;
      if (!candidateGeometry.isNull())
        expanded = expanded.united(
            candidateGeometry.adjusted(-20.0, -40.0, 20.0, 20.0));
    }
    newContainers[*parentIndex].geometry = expanded;
  }

  QHash<QString, QString> desiredOwnerByChild;
  QStringList desiredChildOrder;
  const auto nearestPresentedContainerSubject = [&](const QString &kind,
                                                    const QString &id) {
    QSet<QString> visited;
    QString current = effectiveBrowserParentKey(m_data, kind, id);
    while (!current.isEmpty() && !visited.contains(current)) {
      visited.insert(current);
      const QString subjectKey = subjectKeyForBrowserKey(current);
      if (containerIdBySubject.contains(subjectKey))
        return subjectKey;
      current = parentOfKey(current);
    }
    return QString{};
  };
  for (const auto &subject : includedSubjects) {
    const QString ownerSubject =
        nearestPresentedContainerSubject(subject.browserKind, subject.id);
    if (!ownerSubject.isEmpty()) {
      const QString childId = containerIdBySubject.value(subject.key());
      desiredOwnerByChild.insert(childId,
                                 containerIdBySubject.value(ownerSubject));
      desiredChildOrder.append(childId);
    }
  }
  for (const QString &elementId : acceptedElementIds) {
    const QString ownerSubject =
        nearestPresentedContainerSubject(QStringLiteral("element"), elementId);
    if (!ownerSubject.isEmpty()) {
      const QString childId = nodeIdByElement.value(elementId);
      desiredOwnerByChild.insert(childId,
                                 containerIdBySubject.value(ownerSubject));
      desiredChildOrder.append(childId);
    }
  }

  QSet<QString> affectedChildIds;
  for (auto owner = desiredOwnerByChild.cbegin();
       owner != desiredOwnerByChild.cend(); ++owner)
    affectedChildIds.insert(owner.key());

  QHash<QString, QStringList> existingChildrenAfter;
  for (const auto &container : diagram->containers) {
    QStringList children = container.childPresentationIds;
    children.removeIf([&](const QString &childId) {
      return affectedChildIds.contains(childId);
    });
    existingChildrenAfter.insert(container.id, std::move(children));
  }
  for (const QString &childId : desiredChildOrder) {
    const QString containerId = desiredOwnerByChild.value(childId);
    const auto existing = existingChildrenAfter.find(containerId);
    if (existing != existingChildrenAfter.end()) {
      if (!existing->contains(childId))
        existing->append(childId);
      continue;
    }
    const auto newContainerIndex = newContainerIndexById.constFind(containerId);
    if (newContainerIndex != newContainerIndexById.cend()) {
      auto &newContainer = newContainers[*newContainerIndex];
      if (!newContainer.childPresentationIds.contains(childId))
        newContainer.childPresentationIds.append(childId);
    }
  }

  QList<ContainerChildrenChange> membershipChanges;
  for (const auto &container : diagram->containers) {
    const QStringList after = existingChildrenAfter.value(container.id);
    if (after != container.childPresentationIds)
      membershipChanges.append(
          {container.id, container.childPresentationIds, after});
  }

  QList<ConnectorPresentation> connectors;
  QSet<QString> newConnectorIds;
  for (const auto &node : newNodes) {
    auto additions =
        connectorsForNewPresentation(m_data, prospective, node.elementId);
    for (const auto &connector : additions)
      newConnectorIds.insert(connector.id);
    prospective.connectors.append(additions);
    connectors.append(std::move(additions));
  }
  attachNewConnectorsToSnapPoints(m_data, prospective, newConnectorIds);
  copyAutoAttachedPresentations(prospective, newNodes, connectors);
  QSet<QString> newNodeIds;
  for (const auto &node : newNodes)
    newNodeIds.insert(node.id);
  const QList<NodePortSnapPointChange> portChanges =
      existingNodePortChanges(*diagram, prospective, newNodeIds);

  if (newContainers.isEmpty() && newNodes.isEmpty() &&
      membershipChanges.isEmpty()) {
    m_diagnostics.addInfo(
        QStringLiteral("command"),
        QStringLiteral("The selected containers and their contents are already "
                       "on this diagram"));
    return 0;
  }
  const int changes = qMax(1, newContainers.size() + newNodes.size());
  pushCommand(std::make_unique<AddContainerPresentationsCommand>(
      this, m_data, diagramId, std::move(newContainers), std::move(newNodes),
      std::move(connectors), std::move(membershipChanges), portChanges));
  return changes;
}

int ProjectController::addEmptyPackageToDiagram(const QString &diagramId,
                                                const QString &packageId) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *package = findElement(m_data, packageId);
  if (!diagram || !package || package->type != ElementType::Package)
    return 0;
  if (packageContainerFor(*diagram, packageId)) {
    m_diagnostics.addInfo(
        QStringLiteral("command"),
        QStringLiteral("That namespace is already presented on this diagram"));
    return 0;
  }

  ContainerPresentation frame;
  frame.id = newId();
  frame.subjectKind = QStringLiteral("package");
  frame.subjectId = packageId;
  const qreal width =
      qMax(presentation_layout::kMinimumContainerWidth,
           presentation_layout::containerTitleWidth(m_data, frame));

  QList<ContainerChildrenChange> membershipChanges;
  QList<PresentationGeometryChange> geometryChanges;
  if (const auto *ancestor =
          nearestPresentedPackageAncestor(m_data, *diagram, packageId)) {
    qreal nextTop =
        ancestor->geometry.top() + presentation_layout::kContainerTopPadding;
    for (const QString &childId : ancestor->childPresentationIds) {
      if (const auto *childNode = findNode(*diagram, childId))
        nextTop = qMax(nextTop, childNode->geometry.bottom() + kNodeClearance);
      else if (const auto *childFrame = findContainer(*diagram, childId))
        nextTop = qMax(nextTop, childFrame->geometry.bottom() + kNodeClearance);
    }
    frame.geometry =
        QRectF(ancestor->geometry.left() +
                   presentation_layout::kContainerHorizontalPadding,
               nextTop, width, 200.0);

    QStringList children = ancestor->childPresentationIds;
    children.append(frame.id);
    membershipChanges.append(
        {ancestor->id, ancestor->childPresentationIds, children});

    const QRectF expanded = ancestor->geometry.united(frame.geometry.adjusted(
        -presentation_layout::kContainerHorizontalPadding,
        -presentation_layout::kContainerTopPadding,
        presentation_layout::kContainerHorizontalPadding,
        presentation_layout::kContainerBottomPadding));
    if (expanded != ancestor->geometry)
      geometryChanges.append({ancestor->id, ancestor->geometry, expanded});
  } else {
    const qreal offset = diagram->containers.size() * 36.0;
    frame.geometry =
        QRectF(kDefaultNodeX + offset, kDefaultNodeY + offset, width, 200.0);
  }

  pushCommand(std::make_unique<AddContainerPresentationsCommand>(
      this, m_data, diagramId, QList<ContainerPresentation>{frame},
      QList<NodePresentation>{}, QList<ConnectorPresentation>{},
      std::move(membershipChanges), QList<NodePortSnapPointChange>{},
      std::move(geometryChanges),
      QStringLiteral("Add empty namespace to diagram")));
  return 1;
}

bool ProjectController::canWrapPresentationInPackage(
    const QString &diagramId, const QString &presentationId) const {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *node = diagram ? findNode(*diagram, presentationId) : nullptr;
  const auto *element = node ? findElement(m_data, node->elementId) : nullptr;
  const auto *package =
      element ? findElement(m_data, element->packageId) : nullptr;
  if (!package || package->type != ElementType::Package)
    return false;

  return containingPackageIdForPresentation(*diagram, presentationId) !=
         element->packageId;
}

bool ProjectController::wrapPresentationInPackage(
    const QString &diagramId, const QString &presentationId) {
  if (!canWrapPresentationInPackage(diagramId, presentationId))
    return false;

  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *node = findNode(*diagram, presentationId);
  const auto *element = findElement(m_data, node->elementId);
  const auto *existingFrame = packageContainerFor(*diagram, element->packageId);
  if (existingFrame) {
    QRectF expanded = existingFrame->geometry.united(node->geometry.adjusted(
        -presentation_layout::kContainerHorizontalPadding,
        -presentation_layout::kContainerTopPadding,
        presentation_layout::kContainerHorizontalPadding,
        presentation_layout::kContainerBottomPadding));
    expanded.setWidth(
        qMax(expanded.width(),
             presentation_layout::containerTitleWidth(m_data, *existingFrame)));

    QVariantList geometries;
    const auto appendGeometry = [&](const QString &id, const QRectF &geometry) {
      QVariantMap value;
      value.insert(QStringLiteral("id"), id);
      value.insert(QStringLiteral("x"), geometry.x());
      value.insert(QStringLiteral("y"), geometry.y());
      value.insert(QStringLiteral("width"), geometry.width());
      value.insert(QStringLiteral("height"), geometry.height());
      geometries.append(value);
    };
    appendGeometry(existingFrame->id, expanded);
    appendGeometry(node->id, node->geometry);
    movePresentationsToContainer(
        diagramId, geometries, {node->id}, existingFrame->id,
        QStringLiteral("Wrap presentation in parent namespace"));
    return true;
  }

  ContainerPresentation frame;
  frame.id = newId();
  frame.subjectKind = QStringLiteral("package");
  frame.subjectId = element->packageId;
  frame.childPresentationIds = {node->id};
  frame.geometry =
      node->geometry.adjusted(-presentation_layout::kContainerHorizontalPadding,
                              -presentation_layout::kContainerTopPadding,
                              presentation_layout::kContainerHorizontalPadding,
                              presentation_layout::kContainerBottomPadding);
  frame.geometry.setWidth(std::max(
      {presentation_layout::kMinimumContainerWidth, frame.geometry.width(),
       presentation_layout::containerTitleWidth(m_data, frame)}));
  frame.geometry.setHeight(qMax(presentation_layout::kMinimumContainerHeight,
                                frame.geometry.height()));

  QList<ContainerChildrenChange> membershipChanges;
  QList<PresentationGeometryChange> geometryChanges;
  const QString ownerId =
      ownerContainerIdForPresentation(*diagram, presentationId);
  if (const auto *owner = findContainer(*diagram, ownerId)) {
    QStringList children = owner->childPresentationIds;
    const qsizetype nodeIndex = children.indexOf(node->id);
    children.removeAll(node->id);
    children.insert(nodeIndex >= 0 ? nodeIndex : children.size(), frame.id);
    membershipChanges.append(
        {owner->id, owner->childPresentationIds, children});

    const QRectF expanded = owner->geometry.united(frame.geometry.adjusted(
        -presentation_layout::kContainerHorizontalPadding,
        -presentation_layout::kContainerTopPadding,
        presentation_layout::kContainerHorizontalPadding,
        presentation_layout::kContainerBottomPadding));
    if (expanded != owner->geometry)
      geometryChanges.append({owner->id, owner->geometry, expanded});
  }

  pushCommand(std::make_unique<AddContainerPresentationsCommand>(
      this, m_data, diagramId, QList<ContainerPresentation>{frame},
      QList<NodePresentation>{}, QList<ConnectorPresentation>{},
      std::move(membershipChanges), QList<NodePortSnapPointChange>{},
      std::move(geometryChanges),
      QStringLiteral("Wrap presentation in parent namespace")));
  return true;
}

void ProjectController::removePresentations(const QString &diagramId,
                                            const QStringList &nodeIds) {
  if (nodeIds.isEmpty())
    return;
  const QSet<QString> ids(nodeIds.cbegin(), nodeIds.cend());
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram || std::none_of(diagram->nodes.cbegin(), diagram->nodes.cend(),
                               [&](const NodePresentation &node) {
                                 return ids.contains(node.id);
                               }))
    return;
  pushCommand(std::make_unique<RemovePresentationsCommand>(this, m_data,
                                                           diagramId, ids));
  clearSelection();
}

void ProjectController::removeContainerPresentation(
    const QString &diagramId, const QString &containerId) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram || !findContainer(*diagram, containerId))
    return;
  pushCommand(std::make_unique<RemoveContainerPresentationCommand>(
      this, m_data, diagramId, containerId));
  clearSelection();
}

void ProjectController::removeDiagramPresentations(
    const QString &diagramId, const QStringList &nodeIds,
    const QStringList &containerIds) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return;

  const QSet<QString> requestedNodeIds(nodeIds.cbegin(), nodeIds.cend());
  QSet<QString> validNodeIds;
  for (const auto &node : diagram->nodes)
    if (requestedNodeIds.contains(node.id))
      validNodeIds.insert(node.id);

  const QSet<QString> requestedContainerIds(containerIds.cbegin(),
                                            containerIds.cend());
  QStringList validContainerIds;
  for (const auto &container : diagram->containers)
    if (requestedContainerIds.contains(container.id))
      validContainerIds.append(container.id);

  const qsizetype selectionSize =
      validNodeIds.size() + validContainerIds.size();
  if (selectionSize == 0)
    return;
  if (validContainerIds.isEmpty()) {
    removePresentations(diagramId, validNodeIds.values());
    return;
  }
  if (validNodeIds.isEmpty() && validContainerIds.size() == 1) {
    removeContainerPresentation(diagramId, validContainerIds.constFirst());
    return;
  }

  // One user selection is one undo step. Container commands are built after
  // node removal so each command records the membership state it actually
  // receives from the preceding command.
  m_undoStack.beginMacro(QStringLiteral("Remove presentations from diagram"));
  if (!validNodeIds.isEmpty())
    pushCommand(std::make_unique<RemovePresentationsCommand>(
        this, m_data, diagramId, validNodeIds));
  for (const QString &containerId : validContainerIds) {
    const auto *currentDiagram = findDiagram(m_data, diagramId);
    if (currentDiagram && findContainer(*currentDiagram, containerId))
      pushCommand(std::make_unique<RemoveContainerPresentationCommand>(
          this, m_data, diagramId, containerId));
  }
  m_undoStack.endMacro();
  clearSelection();
}

void ProjectController::deleteSelected() {
  if (m_selectedId.isEmpty())
    return;
  const QString id = m_selectedId;
  const QString kind = m_selectedKind;
  if (kind == QStringLiteral("diagram"))
    deleteDiagram(id);
  else if (kind == QStringLiteral("relationship"))
    deleteRelationship(id);
  else if (kind == QStringLiteral("element"))
    deleteElement(id);
}

void ProjectController::deleteDiagram(const QString &diagramId) {
  if (!findDiagram(m_data, diagramId))
    return;
  if (m_data.diagrams.size() <= 1) {
    m_diagnostics.addError(
        QStringLiteral("command"),
        QStringLiteral("A project must keep at least one diagram"), diagramId);
    return;
  }
  pushCommand(std::make_unique<DeleteDiagramCommand>(this, m_data, diagramId));
  if (m_selectedKind == QStringLiteral("diagram") && m_selectedId == diagramId)
    clearSelection();
}

void ProjectController::deleteRelationship(const QString &relationshipId) {
  if (!findRelationship(m_data, relationshipId))
    return;
  pushCommand(std::make_unique<DeleteRelationshipCommand>(this, m_data,
                                                          relationshipId));
  if (m_selectedKind == QStringLiteral("relationship") &&
      m_selectedId == relationshipId)
    clearSelection();
}

void ProjectController::deleteRelationships(
    const QStringList &relationshipIds) {
  QSet<QString> requested(relationshipIds.cbegin(), relationshipIds.cend());
  QStringList validIds;
  validIds.reserve(requested.size());
  for (const auto &relationship : m_data.relationships)
    if (requested.contains(relationship.id))
      validIds.append(relationship.id);
  if (validIds.isEmpty())
    return;
  if (validIds.size() == 1) {
    deleteRelationship(validIds.constFirst());
    return;
  }

  m_undoStack.beginMacro(QStringLiteral("Delete relationships"));
  for (const QString &relationshipId : validIds)
    pushCommand(std::make_unique<DeleteRelationshipCommand>(this, m_data,
                                                            relationshipId));
  m_undoStack.endMacro();
  clearSelection();
}

void ProjectController::deleteElement(const QString &elementId) {
  if (!findElement(m_data, elementId))
    return;
  pushCommand(std::make_unique<DeleteElementCommand>(this, m_data, elementId));
  if (m_selectedKind == QStringLiteral("element") && m_selectedId == elementId)
    clearSelection();
}

void ProjectController::selectObject(const QString &id, const QString &kind) {
  if (m_selectedId == id && m_selectedKind == kind)
    return;
  m_selectedId = id;
  m_selectedKind = kind;
  emit selectionChanged();
}

void ProjectController::clearSelection() { selectObject({}, {}); }

QString ProjectController::diagramName(const QString &diagramId) const {
  const auto *diagram = findDiagram(m_data, diagramId);
  return diagram ? diagram->name : QString();
}

void ProjectController::renameDiagram(const QString &diagramId,
                                      const QString &name) {
  editText(diagramId, QStringLiteral("name"), -1, name);
}

QVariantMap ProjectController::diagramFilter(const QString &diagramId) const {
  const auto *diagram = findDiagram(m_data, diagramId);
  return diagram ? diagram_filter::toVariantMap(diagram->filter)
                 : QVariantMap{};
}

bool ProjectController::setDiagramFilter(const QString &diagramId,
                                         const QVariantMap &filterValues) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return false;
  DiagramFilter after = diagram_filter::fromVariantMap(filterValues);
  // Retain extension fields that this editor does not understand.
  after.extra = diagram->filter.extra;
  if (diagram->filter == after)
    return true;
  pushCommand(std::make_unique<SetDiagramFilterCommand>(
      this, diagramId, diagram->filter, std::move(after)));
  return true;
}

void ProjectController::updateNodeGeometry(const QString &diagramId,
                                           const QString &nodeId, qreal x,
                                           qreal y, qreal width, qreal height) {
  QVariantMap geometry;
  geometry.insert(QStringLiteral("id"), nodeId);
  geometry.insert(QStringLiteral("x"), x);
  geometry.insert(QStringLiteral("y"), y);
  geometry.insert(QStringLiteral("width"), width);
  geometry.insert(QStringLiteral("height"), height);
  updateNodeGeometries(diagramId, {geometry});
}

void ProjectController::updateNodeGeometries(const QString &diagramId,
                                             const QVariantList &geometries) {
  updatePresentationGeometries(
      diagramId, geometries, QStringLiteral("Move or resize diagram elements"));
}

void ProjectController::setNodePortSnapPoints(const QString &diagramId,
                                              const QString &nodeId,
                                              int horizontalPointCount,
                                              int verticalPointCount) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *node = diagram ? findNode(*diagram, nodeId) : nullptr;
  if (!node)
    return;

  const int horizontal =
      connector_ports::normalizedSnapPointCount(horizontalPointCount);
  const int vertical =
      connector_ports::normalizedSnapPointCount(verticalPointCount);
  if (node->horizontalPortSnapPoints == horizontal &&
      node->verticalPortSnapPoints == vertical)
    return;

  pushCommand(std::make_unique<SetNodePortSnapPointsCommand>(
      this, diagramId, nodeId, node->horizontalPortSnapPoints,
      node->verticalPortSnapPoints, horizontal, vertical));
}

void ProjectController::setDiagramCompartmentVisible(const QString &diagramId,
                                                     const QString &compartment,
                                                     bool visible) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return;
  const bool attributes = compartment == QStringLiteral("attributes");
  if (!attributes && compartment != QStringLiteral("operations"))
    return;
  const bool before =
      attributes ? diagram->showAttributes : diagram->showOperations;
  if (before == visible)
    return;
  pushCommand(std::make_unique<SetDiagramCompartmentVisibilityCommand>(
      this, diagramId, attributes, before, visible));
}

void ProjectController::setNodeCompartmentVisibility(
    const QString &diagramId, const QString &nodeId, const QString &compartment,
    const QString &visibility) {
  setNodesCompartmentVisibility(diagramId, {nodeId}, compartment, visibility);
}

void ProjectController::setNodesCompartmentVisibility(
    const QString &diagramId, const QStringList &nodeIds,
    const QString &compartment, const QString &visibility) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram || nodeIds.isEmpty())
    return;
  const bool attributes = compartment == QStringLiteral("attributes");
  if (!attributes && compartment != QStringLiteral("operations"))
    return;

  std::optional<bool> after;
  if (visibility == QStringLiteral("show"))
    after = true;
  else if (visibility == QStringLiteral("hide"))
    after = false;
  else if (visibility != QStringLiteral("inherit"))
    return;

  const QSet<QString> requestedNodeIds(nodeIds.cbegin(), nodeIds.cend());
  QList<NodeCompartmentVisibilityChange> changes;
  changes.reserve(requestedNodeIds.size());
  for (const auto &node : diagram->nodes) {
    if (!requestedNodeIds.contains(node.id))
      continue;
    const std::optional<bool> before =
        attributes ? node.showAttributes : node.showOperations;
    if (before != after)
      changes.append({node.id, before, after});
  }
  if (changes.isEmpty())
    return;
  pushCommand(std::make_unique<SetNodeCompartmentVisibilityCommand>(
      this, attributes, diagramId, std::move(changes)));
}

void ProjectController::updatePresentationGeometries(
    const QString &diagramId, const QVariantList &geometries,
    const QString &description) {
  commitPresentationChanges(diagramId, geometries, {}, std::nullopt,
                            description);
}

bool ProjectController::canMovePresentationsToContainer(
    const QString &diagramId, const QStringList &movedPresentationIds,
    const QString &targetContainerId) const {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return false;
  if (targetContainerId.isEmpty())
    return true;
  if (!findContainer(*diagram, targetContainerId))
    return false;

  const QString targetPackageId =
      packageIdForDropTarget(*diagram, targetContainerId);
  if (targetPackageId.isEmpty())
    return true;

  QSet<QString> visited;
  const auto presentationCanUseTarget =
      [&](const auto &self, const QString &presentationId) -> bool {
    if (visited.contains(presentationId))
      return true;
    visited.insert(presentationId);

    if (const auto *node = findNode(*diagram, presentationId)) {
      const auto *element = findElement(m_data, node->elementId);
      return element && packageIsAncestorOrSame(m_data, targetPackageId,
                                                element->packageId);
    }

    const auto *container = findContainer(*diagram, presentationId);
    if (!container)
      return false;
    if (container->subjectKind == QStringLiteral("package")) {
      const auto *package = findElement(m_data, container->subjectId);
      if (!package || package->type != ElementType::Package ||
          !packageIsAncestorOrSame(m_data, targetPackageId, package->packageId))
        return false;
    }
    for (const QString &childId : container->childPresentationIds)
      if (!self(self, childId))
        return false;
    return true;
  };

  return std::all_of(movedPresentationIds.cbegin(), movedPresentationIds.cend(),
                     [&](const QString &presentationId) {
                       return presentationCanUseTarget(presentationCanUseTarget,
                                                       presentationId);
                     });
}

void ProjectController::movePresentationsToContainer(
    const QString &diagramId, const QVariantList &geometries,
    const QStringList &movedPresentationIds, const QString &targetContainerId,
    const QString &description) {
  if (!canMovePresentationsToContainer(diagramId, movedPresentationIds,
                                       targetContainerId))
    return;
  commitPresentationChanges(diagramId, geometries, movedPresentationIds,
                            targetContainerId, description);
}

void ProjectController::commitPresentationChanges(
    const QString &diagramId, const QVariantList &geometries,
    const QStringList &movedPresentationIds,
    const std::optional<QString> &targetContainerId,
    const QString &description) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return;

  QList<PresentationGeometryChange> changes;
  QHash<QString, int> changeIndex;
  for (const auto &value : geometries) {
    const QVariantMap map = value.toMap();
    const QString presentationId = map.value(QStringLiteral("id")).toString();
    const auto *node = findNode(*diagram, presentationId);
    const auto *container = findContainer(*diagram, presentationId);
    if (!node && !container)
      continue;
    const qreal minimumWidth = container
                                   ? presentation_layout::kMinimumContainerWidth
                                   : presentation_layout::kMinimumNodeWidth;
    const qreal minimumHeight =
        container ? presentation_layout::kMinimumContainerHeight
                  : presentation_layout::kMinimumNodeHeight;
    const QRectF geometry(
        map.value(QStringLiteral("x")).toDouble(),
        map.value(QStringLiteral("y")).toDouble(),
        qMax(minimumWidth, map.value(QStringLiteral("width")).toDouble()),
        qMax(minimumHeight, map.value(QStringLiteral("height")).toDouble()));
    const auto existing = changeIndex.constFind(presentationId);
    if (existing == changeIndex.cend()) {
      changeIndex.insert(presentationId, static_cast<int>(changes.size()));
      changes.append({presentationId,
                      node ? node->geometry : container->geometry, geometry});
    } else {
      changes[*existing].after = geometry;
    }
  }
  changes.removeIf([](const PresentationGeometryChange &change) {
    return change.before == change.after;
  });
  const QList<ContainerChildrenChange> membershipChanges =
      targetContainerId
          ? membershipChangesForDrop(*diagram, movedPresentationIds,
                                     *targetContainerId)
          : QList<ContainerChildrenChange>{};

  if (!changes.isEmpty() || !membershipChanges.isEmpty())
    pushCommand(std::make_unique<UpdatePresentationGeometriesCommand>(
        this, diagramId, std::move(changes), membershipChanges,
        QList<ElementPackageChange>{}, description));
}

QString ProjectController::createRelationship(const QString &diagramId,
                                              const QString &sourceNodeId,
                                              const QString &targetNodeId,
                                              const QString &type) {
  return createRelationshipImpl(diagramId, sourceNodeId, targetNodeId, type,
                                ConnectorRouting::Straight);
}

QString ProjectController::createRelationshipWithRouting(
    const QString &diagramId, const QString &sourceNodeId,
    const QString &targetNodeId, const QString &type, const QString &routing) {
  bool routingOk = false;
  const ConnectorRouting parsedRouting =
      connectorRoutingFromString(routing, &routingOk);
  if (!routingOk) {
    m_diagnostics.addError(
        QStringLiteral("command"),
        QStringLiteral("Unknown connector routing mode: %1").arg(routing));
    return {};
  }
  return createRelationshipImpl(diagramId, sourceNodeId, targetNodeId, type,
                                parsedRouting);
}

QString ProjectController::createRelationshipImpl(const QString &diagramId,
                                                  const QString &sourceNodeId,
                                                  const QString &targetNodeId,
                                                  const QString &type,
                                                  ConnectorRouting routing) {
  return createRelationshipImpl(diagramId, sourceNodeId, targetNodeId, type,
                                routing, std::nullopt, std::nullopt);
}

QString ProjectController::createRelationshipAtAnchors(
    const QString &diagramId, const QString &sourceNodeId,
    const QString &targetNodeId, const QString &type, const QString &routing,
    ConnectorAnchor sourceAnchor, ConnectorAnchor targetAnchor) {
  bool routingOk = false;
  const ConnectorRouting parsedRouting =
      connectorRoutingFromString(routing, &routingOk);
  if (!routingOk) {
    m_diagnostics.addError(
        QStringLiteral("command"),
        QStringLiteral("Unknown connector routing mode: %1").arg(routing));
    return {};
  }
  return createRelationshipImpl(diagramId, sourceNodeId, targetNodeId, type,
                                parsedRouting, std::move(sourceAnchor),
                                std::move(targetAnchor));
}

QString ProjectController::createRelationshipImpl(
    const QString &diagramId, const QString &sourceNodeId,
    const QString &targetNodeId, const QString &type, ConnectorRouting routing,
    std::optional<ConnectorAnchor> sourceAnchor,
    std::optional<ConnectorAnchor> targetAnchor) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return {};
  const auto *sourceNode = findNode(*diagram, sourceNodeId);
  const auto *targetNode = findNode(*diagram, targetNodeId);
  const bool anchoredGesture =
      sourceAnchor.has_value() && targetAnchor.has_value();
  if (!sourceNode || !targetNode ||
      (sourceNode == targetNode && !anchoredGesture)) {
    m_diagnostics.addError(
        QStringLiteral("command"),
        QStringLiteral("Select two different diagram elements to connect"));
    return {};
  }
  bool typeOk = false;
  const RelationshipType relationshipType =
      relationshipTypeFromString(type, &typeOk);
  if (!typeOk) {
    m_diagnostics.addError(
        QStringLiteral("command"),
        QStringLiteral("Unknown relationship type: %1").arg(type));
    return {};
  }

  Relationship relationship;
  relationship.id = newId();
  relationship.type = relationshipType;
  relationship.sourceId = sourceNode->elementId;
  relationship.targetId = targetNode->elementId;
  ConnectorPresentation connector;
  connector.id = newId();
  connector.relationshipId = relationship.id;
  connector.routing = routing;
  connector.sourceAnchor = sourceAnchor.value_or(
      edgeAnchorToward(sourceNode->geometry, targetNode->geometry.center()));
  connector.targetAnchor = targetAnchor.value_or(
      edgeAnchorToward(targetNode->geometry, sourceNode->geometry.center()));
  if (sourceNode == targetNode)
    connector.bendPoints = selfConnectorBendPoints(
        sourceNode->geometry, connector.sourceAnchor, connector.targetAnchor);
  pushCommand(std::make_unique<CreateRelationshipCommand>(
      this, m_data, diagramId, relationship, connector));
  selectObject(relationship.id, QStringLiteral("relationship"));
  return connector.id;
}

void ProjectController::reconnectRelationship(const QString &diagramId,
                                              const QString &connectorId,
                                              const QString &nodeId,
                                              bool reconnectSource) {
  reconnectRelationshipAtAnchor(diagramId, connectorId, nodeId, reconnectSource,
                                {ConnectorSide::Automatic, 0.5});
}

void ProjectController::reconnectRelationshipAtAnchor(
    const QString &diagramId, const QString &connectorId, const QString &nodeId,
    bool reconnectSource, ConnectorAnchor afterAnchor) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return;
  const auto *connector = findConnector(*diagram, connectorId);
  const auto *node = findNode(*diagram, nodeId);
  if (!connector || !node)
    return;
  const auto *relationship =
      findRelationship(m_data, connector->relationshipId);
  if (!relationship)
    return;
  const QString beforeElementId =
      reconnectSource ? relationship->sourceId : relationship->targetId;
  const ConnectorAnchor beforeAnchor =
      reconnectSource ? connector->sourceAnchor : connector->targetAnchor;
  afterAnchor.offset = std::clamp(afterAnchor.offset, 0.0, 1.0);

  QList<ConnectorBendPoint> afterBendPoints = connector->bendPoints;
  const QString afterSourceId =
      reconnectSource ? node->elementId : relationship->sourceId;
  const QString afterTargetId =
      reconnectSource ? relationship->targetId : node->elementId;
  if (afterSourceId == afterTargetId && afterBendPoints.isEmpty()) {
    const ConnectorAnchor sourceAnchor =
        reconnectSource ? afterAnchor : connector->sourceAnchor;
    const ConnectorAnchor targetAnchor =
        reconnectSource ? connector->targetAnchor : afterAnchor;
    afterBendPoints =
        selfConnectorBendPoints(node->geometry, sourceAnchor, targetAnchor);
  }

  if (beforeElementId == node->elementId && beforeAnchor == afterAnchor &&
      connector->bendPoints == afterBendPoints)
    return;

  // A port move on the same element remains the smaller, more specific
  // command. Reattachment changes both the semantic relationship endpoint and
  // its diagram presentation in one undoable operation.
  if (beforeElementId == node->elementId &&
      connector->bendPoints == afterBendPoints) {
    pushCommand(std::make_unique<MoveConnectorAnchorCommand>(
        this, diagramId, connectorId, reconnectSource, beforeAnchor,
        afterAnchor));
    return;
  }
  pushCommand(std::make_unique<ReconnectRelationshipCommand>(
      this, diagramId, connectorId, relationship->id, reconnectSource,
      beforeElementId, node->elementId, beforeAnchor, afterAnchor,
      connector->bendPoints, afterBendPoints));
}

void ProjectController::updateConnectorAnchor(const QString &diagramId,
                                              const QString &connectorId,
                                              bool source, const QString &side,
                                              qreal offset) {
  bool sideOk = false;
  const ConnectorSide parsedSide = connectorSideFromString(side, &sideOk);
  if (!sideOk) {
    m_diagnostics.addError(
        QStringLiteral("command"),
        QStringLiteral("Cannot move a connector port to unknown side '%1'")
            .arg(side),
        connectorId);
    return;
  }

  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return;
  const auto *connector = findConnector(*diagram, connectorId);
  if (!connector)
    return;
  const ConnectorAnchor before =
      source ? connector->sourceAnchor : connector->targetAnchor;
  ConnectorAnchor after = before;
  after.side = parsedSide;
  after.offset = std::clamp(offset, 0.0, 1.0);
  if (before == after)
    return;
  pushCommand(std::make_unique<MoveConnectorAnchorCommand>(
      this, diagramId, connectorId, source, before, after));
}

void ProjectController::setConnectorRouting(const QString &diagramId,
                                            const QString &connectorId,
                                            const QString &routing) {
  setConnectorsRouting(diagramId, {connectorId}, routing);
}

void ProjectController::setConnectorsRouting(const QString &diagramId,
                                             const QStringList &connectorIds,
                                             const QString &routing) {
  bool routingOk = false;
  const ConnectorRouting parsedRouting =
      connectorRoutingFromString(routing, &routingOk);
  if (!routingOk) {
    m_diagnostics.addError(
        QStringLiteral("command"),
        QStringLiteral("Unknown connector routing mode: %1").arg(routing),
        connectorIds.join(QStringLiteral(", ")));
    return;
  }

  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return;

  const QSet<QString> requested(connectorIds.cbegin(), connectorIds.cend());
  QList<ConnectorRoutingChange> changes;
  changes.reserve(requested.size());
  for (const auto &connector : diagram->connectors) {
    if (requested.contains(connector.id) && connector.routing != parsedRouting)
      changes.append({connector.id, connector.routing});
  }
  if (changes.isEmpty())
    return;
  pushCommand(std::make_unique<SetConnectorsRoutingCommand>(
      this, diagramId, std::move(changes), parsedRouting,
      requested.size() > 1));
}

void ProjectController::insertConnectorBendPoint(const QString &diagramId,
                                                 const QString &connectorId,
                                                 int index, qreal x, qreal y) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *connector =
      diagram ? findConnector(*diagram, connectorId) : nullptr;
  if (!connector || index < 0 || index > connector->bendPoints.size() ||
      !std::isfinite(x) || !std::isfinite(y))
    return;
  auto bendPoints = connector->bendPoints;
  bendPoints.insert(index, {{x, y}, {}});
  updateConnectorBendPoints(diagramId, connectorId, std::move(bendPoints),
                            QStringLiteral("Add connector bend point"));
}

void ProjectController::moveConnectorBendPoint(const QString &diagramId,
                                               const QString &connectorId,
                                               int index, qreal x, qreal y) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *connector =
      diagram ? findConnector(*diagram, connectorId) : nullptr;
  if (!connector || index < 0 || index >= connector->bendPoints.size() ||
      !std::isfinite(x) || !std::isfinite(y))
    return;
  auto bendPoints = connector->bendPoints;
  bendPoints[index].position = {x, y};
  updateConnectorBendPoints(diagramId, connectorId, std::move(bendPoints),
                            QStringLiteral("Move connector bend point"));
}

void ProjectController::removeConnectorBendPoint(const QString &diagramId,
                                                 const QString &connectorId,
                                                 int index) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *connector =
      diagram ? findConnector(*diagram, connectorId) : nullptr;
  if (!connector || index < 0 || index >= connector->bendPoints.size())
    return;
  auto bendPoints = connector->bendPoints;
  bendPoints.removeAt(index);
  updateConnectorBendPoints(diagramId, connectorId, std::move(bendPoints),
                            QStringLiteral("Remove connector bend point"));
}

void ProjectController::clearConnectorBendPoints(const QString &diagramId,
                                                 const QString &connectorId) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *connector =
      diagram ? findConnector(*diagram, connectorId) : nullptr;
  if (!connector || connector->bendPoints.isEmpty())
    return;
  updateConnectorBendPoints(diagramId, connectorId, {},
                            QStringLiteral("Clear connector bend points"));
}

void ProjectController::setConnectorAnnotationPlacement(
    const QString &diagramId, const QString &connectorId,
    const QString &annotationKey, qreal routePosition, qreal tangentOffset,
    qreal normalOffset) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *connector =
      diagram ? findConnector(*diagram, connectorId) : nullptr;
  if (!connector || annotationKey.trimmed().isEmpty() ||
      !std::isfinite(routePosition) || routePosition < 0.0 ||
      routePosition > 1.0 || !std::isfinite(tangentOffset) ||
      !std::isfinite(normalOffset))
    return;
  ConnectorAnnotationPlacement placement;
  const auto existing =
      connector->annotationPlacements.constFind(annotationKey);
  if (existing != connector->annotationPlacements.cend())
    placement = *existing;
  placement.routePosition = routePosition;
  placement.tangentOffset = tangentOffset;
  placement.normalOffset = normalOffset;
  auto after = connector->annotationPlacements;
  after.insert(annotationKey, placement);
  if (after == connector->annotationPlacements)
    return;
  pushCommand(std::make_unique<UpdateConnectorAnnotationPlacementsCommand>(
      this, diagramId, connectorId, connector->annotationPlacements,
      std::move(after), QStringLiteral("Move connector annotation")));
}

void ProjectController::resetConnectorAnnotationPlacement(
    const QString &diagramId, const QString &connectorId,
    const QString &annotationKey) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *connector =
      diagram ? findConnector(*diagram, connectorId) : nullptr;
  if (!connector || !connector->annotationPlacements.contains(annotationKey))
    return;
  auto after = connector->annotationPlacements;
  after.remove(annotationKey);
  pushCommand(std::make_unique<UpdateConnectorAnnotationPlacementsCommand>(
      this, diagramId, connectorId, connector->annotationPlacements,
      std::move(after), QStringLiteral("Reset connector annotation position")));
}

void ProjectController::resetConnectorAnnotationPlacements(
    const QString &diagramId, const QString &connectorId) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *connector =
      diagram ? findConnector(*diagram, connectorId) : nullptr;
  if (!connector || connector->annotationPlacements.isEmpty())
    return;
  pushCommand(std::make_unique<UpdateConnectorAnnotationPlacementsCommand>(
      this, diagramId, connectorId, connector->annotationPlacements,
      QHash<QString, ConnectorAnnotationPlacement>{},
      QStringLiteral("Reset connector annotation positions")));
}

void ProjectController::updateConnectorBendPoints(
    const QString &diagramId, const QString &connectorId,
    QList<ConnectorBendPoint> bendPoints, const QString &description) {
  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *connector =
      diagram ? findConnector(*diagram, connectorId) : nullptr;
  if (!connector || connector->bendPoints == bendPoints)
    return;
  pushCommand(std::make_unique<UpdateConnectorBendPointsCommand>(
      this, diagramId, connectorId, connector->bendPoints,
      std::move(bendPoints), description));
}

void ProjectController::editText(const QString &objectId, const QString &field,
                                 int index, const QString &value) {
  const QString trimmed = value.trimmed();
  if (const auto *relationship = findRelationship(m_data, objectId)) {
    RelationshipTextProperty property;
    QString before;
    QString description;
    bool optional = false;
    if (field == QStringLiteral("name")) {
      property = RelationshipTextProperty::Name;
      before = relationship->name;
      description = QStringLiteral("Edit relationship name");
      optional = true;
    } else if (field == QStringLiteral("sourceRole")) {
      property = RelationshipTextProperty::SourceRole;
      before = relationship->sourceEnd.role;
      description = QStringLiteral("Edit source role");
      optional = true;
    } else if (field == QStringLiteral("sourceMultiplicity")) {
      property = RelationshipTextProperty::SourceMultiplicity;
      before = relationship->sourceEnd.multiplicity;
      description = QStringLiteral("Edit source multiplicity");
      optional = true;
    } else if (field == QStringLiteral("targetRole")) {
      property = RelationshipTextProperty::TargetRole;
      before = relationship->targetEnd.role;
      description = QStringLiteral("Edit target role");
      optional = true;
    } else if (field == QStringLiteral("targetMultiplicity")) {
      property = RelationshipTextProperty::TargetMultiplicity;
      before = relationship->targetEnd.multiplicity;
      description = QStringLiteral("Edit target multiplicity");
      optional = true;
    } else {
      return;
    }
    if (!optional && trimmed.isEmpty()) {
      m_diagnostics.addError(QStringLiteral("validation"),
                             QStringLiteral("Text values cannot be empty"),
                             objectId);
      emit selectionChanged();
      return;
    }
    if (before != trimmed)
      pushCommand(std::make_unique<EditRelationshipTextCommand>(
          this, objectId, property, before, trimmed, description));
    return;
  }
  if (trimmed.isEmpty()) {
    m_diagnostics.addError(QStringLiteral("validation"),
                           QStringLiteral("Text values cannot be empty"),
                           objectId);
    emit selectionChanged();
    return;
  }
  const QString description = QStringLiteral("Edit %1").arg(field);
  if (const auto *element = findElement(m_data, objectId)) {
    ElementTextProperty property;
    QString before;
    if (field == QStringLiteral("name")) {
      property = ElementTextProperty::Name;
      before = element->name;
    } else if (field == QStringLiteral("attribute") && index >= 0 &&
               index < element->attributes.size()) {
      property = ElementTextProperty::Attribute;
      before = element->attributes.at(index);
    } else if (field == QStringLiteral("operation") && index >= 0 &&
               index < element->operations.size()) {
      property = ElementTextProperty::Operation;
      before = element->operations.at(index);
    } else if (field == QStringLiteral("literal") && index >= 0 &&
               index < element->enumLiterals.size()) {
      property = ElementTextProperty::Literal;
      before = element->enumLiterals.at(index);
    } else {
      return;
    }
    if (before != trimmed)
      pushCommand(std::make_unique<EditElementTextCommand>(
          this, objectId, property, index, before, trimmed, description));
    return;
  }
  if (const auto *folder = findBrowserFolder(m_data, objectId)) {
    if (field == QStringLiteral("name") && folder->name != trimmed)
      pushCommand(std::make_unique<RenameBrowserFolderCommand>(
          this, objectId, folder->name, trimmed));
    return;
  }
  if (const auto *diagram = findDiagram(m_data, objectId)) {
    if (field == QStringLiteral("name") && diagram->name != trimmed)
      pushCommand(std::make_unique<RenameDiagramCommand>(
          this, objectId, diagram->name, trimmed));
  }
}

void ProjectController::setSelectedName(const QString &name) {
  if (!m_selectedId.isEmpty())
    editText(m_selectedId, QStringLiteral("name"), -1, name);
}

static QStringList lines(const QString &value) {
  QStringList result;
  for (const auto &line : value.split(u'\n')) {
    const QString trimmed = line.trimmed();
    if (!trimmed.isEmpty())
      result.append(trimmed);
  }
  return result;
}

void ProjectController::setSelectedAttributes(const QString &value) {
  const auto *element = findElement(m_data, m_selectedId);
  const QStringList after = lines(value);
  if (element && element->attributes != after)
    pushCommand(std::make_unique<SetElementListCommand>(
        this, element->id, ElementListProperty::Attributes, element->attributes,
        after, QStringLiteral("Edit attributes")));
}

void ProjectController::setSelectedOperations(const QString &value) {
  const auto *element = findElement(m_data, m_selectedId);
  const QStringList after = lines(value);
  if (element && element->operations != after)
    pushCommand(std::make_unique<SetElementListCommand>(
        this, element->id, ElementListProperty::Operations, element->operations,
        after, QStringLiteral("Edit operations")));
}

void ProjectController::setSelectedLiterals(const QString &value) {
  const auto *element = findElement(m_data, m_selectedId);
  const QStringList after = lines(value);
  if (element && element->enumLiterals != after)
    pushCommand(std::make_unique<SetElementListCommand>(
        this, element->id, ElementListProperty::Literals, element->enumLiterals,
        after, QStringLiteral("Edit enumeration literals")));
}

void ProjectController::setSelectedSourceRole(const QString &value) {
  if (m_selectedKind == QStringLiteral("relationship"))
    editText(m_selectedId, QStringLiteral("sourceRole"), -1, value);
}

void ProjectController::setSelectedSourceMultiplicity(const QString &value) {
  if (m_selectedKind == QStringLiteral("relationship"))
    editText(m_selectedId, QStringLiteral("sourceMultiplicity"), -1, value);
}

void ProjectController::setSelectedTargetRole(const QString &value) {
  if (m_selectedKind == QStringLiteral("relationship"))
    editText(m_selectedId, QStringLiteral("targetRole"), -1, value);
}

void ProjectController::setSelectedTargetMultiplicity(const QString &value) {
  if (m_selectedKind == QStringLiteral("relationship"))
    editText(m_selectedId, QStringLiteral("targetMultiplicity"), -1, value);
}

void ProjectController::pushCommand(std::unique_ptr<ProjectCommand> command) {
  Q_ASSERT(command);
  m_undoStack.push(command.release());
}

void ProjectController::applyCommand(ProjectCommand &command, bool execute) {
  // Commands are fully prepared and validated before they reach QUndoStack.
  // Their model operations are therefore non-failing, and observers see only
  // the completed action through this single notification boundary.
  if (execute)
    command.execute(m_data);
  else
    command.revert(m_data);
  bool selectionExists = m_selectedId.isEmpty();
  if (m_selectedKind == QStringLiteral("element"))
    selectionExists = findElement(m_data, m_selectedId);
  else if (m_selectedKind == QStringLiteral("relationship"))
    selectionExists = findRelationship(m_data, m_selectedId);
  else if (m_selectedKind == QStringLiteral("diagram"))
    selectionExists = findDiagram(m_data, m_selectedId);
  if (!selectionExists) {
    m_selectedId.clear();
    m_selectedKind.clear();
  }
  emit stateChanged();
  emit diagramsChanged();
  emit projectChanged();
  emit selectionChanged();
}

void ProjectController::setDataDirect(const ProjectData &state) {
  m_undoStack.clear();
  m_data = state;
  m_undoStack.setClean();
  emit stateChanged();
  emit diagramsChanged();
  emit projectChanged();
  emit selectionChanged();
  emit undoStateChanged();
  emit dirtyChanged();
}

void ProjectController::logDiagnostics(const QList<Diagnostic> &items) {
  for (const auto &item : items)
    m_diagnostics.add(item);
}

void ProjectController::setExternallyChangedProjectFiles(
    const QStringList &files, bool reportConflict) {
  if (m_externallyChangedProjectFiles != files) {
    m_externallyChangedProjectFiles = files;
    emit externallyChangedProjectFilesChanged();
  }
  if (reportConflict)
    emit externalProjectChangeDetected();
}

} // namespace yauml
