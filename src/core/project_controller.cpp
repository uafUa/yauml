#include "core/project_controller.h"

#include "core/project_command.h"
#include "core/project_commands.h"
#include "core/project_serializer.h"
#include "core/project_tree_model.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>

namespace uuml {

namespace {

constexpr qreal kDefaultNodeX = 50.0;
constexpr qreal kDefaultNodeY = 50.0;
constexpr qreal kDefaultNodeWidth = 220.0;
constexpr qreal kDefaultNodeHeight = 120.0;
constexpr qreal kNodeHorizontalSpacing = 250.0;
constexpr qreal kNodeVerticalSpacing = 160.0;
constexpr qreal kNodeClearance = 12.0;
constexpr int kPlacementColumns = 12;

QRectF firstAvailableNodeGeometry(const Diagram &diagram) {
  // Reusing the first open slot keeps newly placed nodes near the diagram's
  // origin and, importantly, restores holes left by removed presentations. A
  // node-count-based diagonal offset sends additions thousands of pixels away
  // on large diagrams, making a successful placement appear to have failed.
  const qsizetype candidateCount = diagram.nodes.size() + 1;
  for (qsizetype index = 0; index < candidateCount; ++index) {
    const int column = static_cast<int>(index % kPlacementColumns);
    const int row = static_cast<int>(index / kPlacementColumns);
    const QRectF candidate(kDefaultNodeX + column * kNodeHorizontalSpacing,
                           kDefaultNodeY + row * kNodeVerticalSpacing,
                           kDefaultNodeWidth, kDefaultNodeHeight);
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
  qreal contentBottom = kDefaultNodeY - kNodeVerticalSpacing;
  for (const auto &node : diagram.nodes)
    contentBottom = std::max(contentBottom, node.geometry.bottom());
  return {kDefaultNodeX, contentBottom + kNodeClearance, kDefaultNodeWidth,
          kDefaultNodeHeight};
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
  return true;
}

bool ProjectController::saveProject(const QUrl &url) {
  const QString path = normalizedLocalPath(url);
  if (path.isEmpty()) {
    m_diagnostics.addError(
        QStringLiteral("persistence"),
        QStringLiteral("Choose a project directory before saving"));
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

QString ProjectController::addElement(const QString &type,
                                      const QString &diagramId) {
  return addElementAt(type, diagramId, std::numeric_limits<qreal>::quiet_NaN(),
                      std::numeric_limits<qreal>::quiet_NaN());
}

QString ProjectController::addElementAt(const QString &type,
                                        const QString &diagramId, qreal x,
                                        qreal y) {
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

  std::optional<NodePresentation> presentation;
  if (const auto *diagram = findDiagram(m_data, diagramId)) {
    NodePresentation node;
    node.id = newId();
    node.elementId = element.id;
    node.geometry = std::isfinite(x) && std::isfinite(y)
                        ? QRectF(x, y, kDefaultNodeWidth, kDefaultNodeHeight)
                        : firstAvailableNodeGeometry(*diagram);
    presentation = std::move(node);
  }
  pushCommand(std::make_unique<CreateElementCommand>(
      this, m_data, element, diagramId, std::move(presentation)));
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

void ProjectController::addSelectedToDiagram(const QString &diagramId) {
  if (m_selectedKind != QStringLiteral("element"))
    return;
  const QString elementId = m_selectedId;
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return;
  if (std::any_of(diagram->nodes.cbegin(), diagram->nodes.cend(),
                  [&](const NodePresentation &node) {
                    return node.elementId == elementId;
                  })) {
    m_diagnostics.addWarning(
        QStringLiteral("command"),
        QStringLiteral("The element is already on this diagram"), elementId);
    return;
  }
  NodePresentation node;
  node.id = newId();
  node.elementId = elementId;
  node.geometry = firstAvailableNodeGeometry(*diagram);
  auto connectors = connectorsForNewPresentation(m_data, *diagram, elementId);
  pushCommand(std::make_unique<AddElementToDiagramCommand>(
      this, m_data, diagramId, std::move(node), std::move(connectors)));
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
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return;

  QList<NodeGeometryChange> changes;
  QHash<QString, int> changeIndex;
  for (const auto &value : geometries) {
    const QVariantMap map = value.toMap();
    const QString nodeId = map.value(QStringLiteral("id")).toString();
    const auto *node = findNode(*diagram, nodeId);
    if (!node)
      continue;
    const QRectF geometry(
        map.value(QStringLiteral("x")).toDouble(),
        map.value(QStringLiteral("y")).toDouble(),
        qMax(120.0, map.value(QStringLiteral("width")).toDouble()),
        qMax(60.0, map.value(QStringLiteral("height")).toDouble()));
    const auto existing = changeIndex.constFind(nodeId);
    if (existing == changeIndex.cend()) {
      changeIndex.insert(nodeId, static_cast<int>(changes.size()));
      changes.append({nodeId, node->geometry, geometry});
    } else {
      changes[*existing].after = geometry;
    }
  }
  changes.removeIf([](const NodeGeometryChange &change) {
    return change.before == change.after;
  });
  if (!changes.isEmpty())
    pushCommand(std::make_unique<UpdateNodeGeometriesCommand>(
        this, diagramId, std::move(changes)));
}

QString ProjectController::createRelationship(const QString &diagramId,
                                              const QString &sourceNodeId,
                                              const QString &targetNodeId,
                                              const QString &type) {
  const auto *diagram = findDiagram(m_data, diagramId);
  if (!diagram)
    return {};
  const auto *sourceNode = findNode(*diagram, sourceNodeId);
  const auto *targetNode = findNode(*diagram, targetNodeId);
  if (!sourceNode || !targetNode || sourceNode == targetNode) {
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
  case RelationshipType::Association:
    relationship.name = QStringLiteral("associated with");
    break;
  }
  relationship.sourceId = sourceNode->elementId;
  relationship.targetId = targetNode->elementId;
  ConnectorPresentation connector;
  connector.id = newId();
  connector.relationshipId = relationship.id;
  connector.sourceAnchor =
      edgeAnchorToward(sourceNode->geometry, targetNode->geometry.center());
  connector.targetAnchor =
      edgeAnchorToward(targetNode->geometry, sourceNode->geometry.center());
  pushCommand(std::make_unique<CreateRelationshipCommand>(
      this, m_data, diagramId, relationship, connector));
  selectObject(relationship.id, QStringLiteral("relationship"));
  return connector.id;
}

void ProjectController::reconnectRelationship(const QString &diagramId,
                                              const QString &connectorId,
                                              const QString &nodeId,
                                              bool reconnectSource) {
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
  ConnectorAnchor afterAnchor = beforeAnchor;
  afterAnchor.side = ConnectorSide::Automatic;
  afterAnchor.offset = 0.5;
  if (beforeElementId == node->elementId && beforeAnchor == afterAnchor)
    return;
  pushCommand(std::make_unique<ReconnectRelationshipCommand>(
      this, diagramId, connectorId, relationship->id, reconnectSource,
      beforeElementId, node->elementId, beforeAnchor, afterAnchor));
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
