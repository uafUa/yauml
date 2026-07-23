#include "core/project_controller.h"

#include "core/connector_port_layout.h"
#include "core/cpp_import.h"
#include "core/presentation_layout.h"
#include "core/project_command.h"
#include "core/project_commands.h"
#include "core/project_serializer.h"
#include "core/project_tree_model.h"

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

namespace uuml {

namespace {

constexpr qreal kDefaultNodeX = 50.0;
constexpr qreal kDefaultNodeY = 50.0;
constexpr qreal kNodeClearance = 12.0;
constexpr int kPlacementColumns = 12;

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

QString packageIdForDiagramContainer(const Diagram &diagram,
                                     const QString &containerId) {
  QHash<QString, QString> ownerByChild;
  for (const auto &container : diagram.containers)
    for (const QString &childId : container.childPresentationIds)
      ownerByChild.insert(childId, container.id);

  QSet<QString> visited;
  QString current = containerId;
  while (!current.isEmpty() && !visited.contains(current)) {
    visited.insert(current);
    const auto *container = findContainer(diagram, current);
    if (!container)
      break;
    if (container->subjectKind == QStringLiteral("package"))
      return container->subjectId;
    current = ownerByChild.value(current);
  }
  return {};
}

QString ownerContainerIdForPresentation(const Diagram &diagram,
                                        const QString &presentationId) {
  for (const auto &container : diagram.containers)
    if (container.childPresentationIds.contains(presentationId))
      return container.id;
  return {};
}

QString packageTargetLabel(const ProjectData &project,
                           const QString &packageId) {
  if (const auto *package = findElement(project, packageId))
    return QStringLiteral("package \"%1\"").arg(package->name);
  return QStringLiteral("the model root");
}

QString packageReassignmentPrompt(const ProjectData &project,
                                  const QSet<QString> &elementIds,
                                  const QString &targetPackageId) {
  QStringList names;
  names.reserve(elementIds.size());
  for (const auto &element : project.elements)
    if (elementIds.contains(element.id))
      names.append(element.name);
  if (names.isEmpty())
    return {};

  const QString targetLabel = packageTargetLabel(project, targetPackageId);
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

QString elementIdForPresentation(const Diagram &diagram,
                                 const QString &presentationId) {
  if (const auto *node = findNode(diagram, presentationId))
    return node->elementId;
  const auto *container = findContainer(diagram, presentationId);
  return container && container->subjectKind == QStringLiteral("package")
             ? container->subjectId
             : QString{};
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

bool ProjectController::canUndo() const { return m_undoStack.canUndo(); }
bool ProjectController::canRedo() const { return m_undoStack.canRedo(); }
QString ProjectController::undoText() const { return m_undoStack.undoText(); }
QString ProjectController::redoText() const { return m_undoStack.redoText(); }
bool ProjectController::dirty() const { return !m_undoStack.isClean(); }

void ProjectController::newProject(const QString &name) {
  m_diagnostics.clear();
  m_projectPath.clear();
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
        QStringLiteral("The selected folder already contains a u uml project; "
                       "confirm replacement before saving"));
    return false;
  }
  const auto outcome = ProjectSerializer::save(path, m_data);
  logDiagnostics(outcome.diagnostics);
  if (!outcome.ok)
    return false;
  m_projectPath = ProjectSerializer::normalizeProjectPath(path);
  m_undoStack.setClean();
  emit projectChanged();
  m_diagnostics.addInfo(
      QStringLiteral("persistence"),
      outcome.unchanged
          ? QStringLiteral("Project already matches the saved files")
          : QStringLiteral("Saved %1").arg(m_projectPath));
  return true;
}

void ProjectController::undo() { m_undoStack.undo(); }
void ProjectController::redo() { m_undoStack.redo(); }

int ProjectController::applyCppImportPlan(const CppImportPreview &preview) {
  QList<ModelElement> desiredElements;
  desiredElements.reserve(preview.elementApplicableCount());
  for (const auto &item : preview.items)
    if (item.isApplicable())
      desiredElements.append(item.desiredElement);
  QList<Relationship> desiredRelationships;
  desiredRelationships.reserve(preview.relationshipApplicableCount());
  for (const auto &item : preview.relationshipItems)
    if (item.isApplicable())
      desiredRelationships.append(item.desiredRelationship);
  const bool sourceRootChanged =
      !preview.sourceRoot.isEmpty() &&
      preview.sourceRoot != m_data.cppImport.sourceRoot;
  if (desiredElements.isEmpty() && desiredRelationships.isEmpty() &&
      !sourceRootChanged)
    return 0;
  const int count = desiredElements.size() + desiredRelationships.size();
  pushCommand(std::make_unique<ApplyCppImportCommand>(
      this, m_data, std::move(desiredElements), std::move(desiredRelationships),
      preview.sourceRoot));
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
      const QSizeF contentSize = presentation_layout::nodeContentSize(element);
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

  const bool changesAnything =
      std::any_of(elementIds.cbegin(), elementIds.cend(),
                  [&](const QString &id) {
                    const ModelElement *element = findElement(m_data, id);
                    return element && (element->browserParent != target ||
                                       (reassignPackage &&
                                        element->packageId != targetPackageId));
                  }) ||
      std::any_of(folderIds.cbegin(), folderIds.cend(), [&](const QString &id) {
        const BrowserFolder *folder = findBrowserFolder(m_data, id);
        return folder && folder->parent != target;
      });
  if (!changesAnything)
    return false;
  pushCommand(std::make_unique<MoveBrowserItemsCommand>(
      this, m_data, elementIds, folderIds, target,
      reassignPackage ? std::optional<QString>(targetPackageId)
                      : std::nullopt));
  return true;
}

QString ProjectController::browserMovePackageChangeSummary(
    const QString &itemsJson, const QString &targetKind,
    const QString &targetId) const {
  const QString targetPackageId =
      packageIdForBrowserTarget(m_data, targetKind, targetId);

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
    if (element && element->packageId != targetPackageId)
      changedElementIds.insert(element->id);
  }
  return packageReassignmentPrompt(m_data, changedElementIds, targetPackageId);
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
    const QSizeF size = presentation_layout::nodePlacementSize(
        *findElement(m_data, elementId), sizingMode);
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
  for (const auto &elementId : acceptedIds) {
    auto additions =
        connectorsForNewPresentation(m_data, prospective, elementId);
    prospective.connectors.append(additions);
    connectors.append(std::move(additions));
  }
  pushCommand(std::make_unique<AddElementsToDiagramCommand>(
      this, m_data, diagramId, std::move(presentations),
      std::move(connectors)));
  if (duplicateCount > 0) {
    m_diagnostics.addInfo(
        QStringLiteral("command"),
        QStringLiteral("Skipped %1 element(s) already present on the diagram")
            .arg(duplicateCount));
  }
  return acceptedIds.size();
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
        presentation_layout::nodePlacementSize(*element, sizingMode);
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
  for (const auto &node : newNodes) {
    auto additions =
        connectorsForNewPresentation(m_data, prospective, node.elementId);
    prospective.connectors.append(additions);
    connectors.append(std::move(additions));
  }

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
      std::move(connectors), std::move(membershipChanges)));
  return changes;
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

void ProjectController::updatePresentationGeometries(
    const QString &diagramId, const QVariantList &geometries,
    const QString &description) {
  commitPresentationChanges(diagramId, geometries, {}, std::nullopt,
                            description);
}

void ProjectController::movePresentationsToContainer(
    const QString &diagramId, const QVariantList &geometries,
    const QStringList &movedPresentationIds, const QString &targetContainerId,
    const QString &description, bool reassignPackage) {
  commitPresentationChanges(diagramId, geometries, movedPresentationIds,
                            targetContainerId, description, reassignPackage);
}

QString ProjectController::presentationMovePackageChangeSummary(
    const QString &diagramId, const QStringList &movedPresentationIds,
    const QString &targetContainerId) const {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return {};

  const QString targetPackageId =
      packageIdForDiagramContainer(*diagram, targetContainerId);

  QSet<QString> changedElementIds;
  for (const QString &presentationId : movedPresentationIds) {
    if (ownerContainerIdForPresentation(*diagram, presentationId) ==
        targetContainerId)
      continue;
    const QString elementId =
        elementIdForPresentation(*diagram, presentationId);
    const auto *element = findElement(m_data, elementId);
    if (element && element->packageId != targetPackageId)
      changedElementIds.insert(elementId);
  }
  return packageReassignmentPrompt(m_data, changedElementIds, targetPackageId);
}

void ProjectController::commitPresentationChanges(
    const QString &diagramId, const QVariantList &geometries,
    const QStringList &movedPresentationIds,
    const std::optional<QString> &targetContainerId, const QString &description,
    bool reassignPackage) {
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

  QList<ElementPackageChange> packageChanges;
  if (reassignPackage && targetContainerId) {
    const QString targetPackageId =
        packageIdForDiagramContainer(*diagram, *targetContainerId);
    QSet<QString> seenElementIds;
    for (const QString &presentationId : movedPresentationIds) {
      if (ownerContainerIdForPresentation(*diagram, presentationId) ==
          *targetContainerId)
        continue;
      const QString elementId =
          elementIdForPresentation(*diagram, presentationId);
      const auto *element = findElement(m_data, elementId);
      if (!element || seenElementIds.contains(elementId) ||
          element->packageId == targetPackageId)
        continue;
      seenElementIds.insert(elementId);
      packageChanges.append({elementId, element->packageId, targetPackageId});
    }
  }
  if (!changes.isEmpty() || !membershipChanges.isEmpty() ||
      !packageChanges.isEmpty())
    pushCommand(std::make_unique<UpdatePresentationGeometriesCommand>(
        this, diagramId, std::move(changes), membershipChanges,
        std::move(packageChanges), description));
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
  switch (relationshipType) {
  case RelationshipType::Dependency:
    relationship.name = QStringLiteral("uses");
    break;
  case RelationshipType::Generalization:
    relationship.name = QStringLiteral("inherits");
    break;
  case RelationshipType::Realization:
    relationship.name = QStringLiteral("implements");
    break;
  case RelationshipType::Association:
    relationship.name = QStringLiteral("associated with");
    break;
  case RelationshipType::Aggregation:
    relationship.name = QStringLiteral("aggregates");
    break;
  case RelationshipType::Composition:
    relationship.name = QStringLiteral("composes");
    break;
  }
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
  bool routingOk = false;
  const ConnectorRouting parsedRouting =
      connectorRoutingFromString(routing, &routingOk);
  if (!routingOk) {
    m_diagnostics.addError(
        QStringLiteral("command"),
        QStringLiteral("Unknown connector routing mode: %1").arg(routing),
        connectorId);
    return;
  }

  const auto *diagram = findDiagram(m_data, diagramId);
  const auto *connector =
      diagram ? findConnector(*diagram, connectorId) : nullptr;
  if (!connector || connector->routing == parsedRouting)
    return;
  pushCommand(std::make_unique<SetConnectorRoutingCommand>(
      this, diagramId, connectorId, connector->routing, parsedRouting));
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
  if (const auto *relationship = findRelationship(m_data, objectId)) {
    if (field == QStringLiteral("name") && relationship->name != trimmed)
      pushCommand(std::make_unique<RenameRelationshipCommand>(
          this, objectId, relationship->name, trimmed));
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

} // namespace uuml
