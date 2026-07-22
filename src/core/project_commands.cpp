#include "core/project_commands.h"

#include "core/project_controller.h"

#include <algorithm>
#include <utility>

namespace uuml {
namespace {

template <typename T>
qsizetype indexOfId(const QList<T> &records, const QString &id) {
  for (qsizetype index = 0; index < records.size(); ++index)
    if (records.at(index).id == id)
      return index;
  return -1;
}

template <typename T>
void insertAtRecordedPosition(QList<T> &records, qsizetype index,
                              const T &value) {
  Q_ASSERT(index >= 0 && index <= records.size());
  Q_ASSERT(indexOfId(records, value.id) < 0);
  if (indexOfId(records, value.id) >= 0)
    return;
  records.insert(std::clamp(index, qsizetype{0}, records.size()), value);
}

template <typename T>
void removeRecordedValue(QList<T> &records, qsizetype expectedIndex,
                         const QString &id) {
  qsizetype index = expectedIndex;
  if (index < 0 || index >= records.size() || records.at(index).id != id)
    index = indexOfId(records, id);
  Q_ASSERT(index >= 0);
  if (index >= 0)
    records.removeAt(index);
}

BrowserParent *browserParentFor(ProjectData &project, const QString &kind,
                                const QString &id) {
  if (kind == QStringLiteral("element")) {
    if (auto *element = findElement(project, id))
      return &element->browserParent;
  } else if (kind == QStringLiteral("folder")) {
    if (auto *folder = findBrowserFolder(project, id))
      return &folder->parent;
  }
  return nullptr;
}

} // namespace

CreateElementCommand::CreateElementCommand(
    ProjectController *controller, const ProjectData &project,
    ModelElement element, QString diagramId,
    std::optional<NodePresentation> presentation)
    : ProjectCommand(controller,
                     QStringLiteral("Create %1").arg(toString(element.type))),
      m_element(std::move(element)), m_elementIndex(project.elements.size()),
      m_diagramId(std::move(diagramId)),
      m_presentation(std::move(presentation)) {
  if (m_presentation) {
    const auto *diagram = findDiagram(project, m_diagramId);
    Q_ASSERT(diagram);
    m_presentationIndex = diagram ? diagram->nodes.size() : -1;
  }
}

void CreateElementCommand::execute(ProjectData &project) {
  insertAtRecordedPosition(project.elements, m_elementIndex, m_element);
  if (m_presentation) {
    if (auto *diagram = findDiagram(project, m_diagramId))
      insertAtRecordedPosition(diagram->nodes, m_presentationIndex,
                               *m_presentation);
  }
}

void CreateElementCommand::revert(ProjectData &project) {
  if (m_presentation) {
    if (auto *diagram = findDiagram(project, m_diagramId))
      removeRecordedValue(diagram->nodes, m_presentationIndex,
                          m_presentation->id);
  }
  removeRecordedValue(project.elements, m_elementIndex, m_element.id);
}

ApplyCppImportCommand::ApplyCppImportCommand(
    ProjectController *controller, const ProjectData &project,
    QList<ModelElement> desiredElements,
    QList<Relationship> desiredRelationships)
    : ProjectCommand(controller, QStringLiteral("Import C++ changes")) {
  m_changes.reserve(desiredElements.size());
  qsizetype nextInsertionIndex = project.elements.size();
  for (auto &desired : desiredElements) {
    const qsizetype index = indexOfId(project.elements, desired.id);
    if (index >= 0) {
      if (project.elements.at(index) != desired)
        m_changes.append(
            {index, project.elements.at(index), std::move(desired)});
    } else {
      m_changes.append(
          {nextInsertionIndex++, std::nullopt, std::move(desired)});
    }
  }

  m_relationshipChanges.reserve(desiredRelationships.size());
  qsizetype nextRelationshipIndex = project.relationships.size();
  for (auto &desired : desiredRelationships) {
    const qsizetype index = indexOfId(project.relationships, desired.id);
    if (index >= 0) {
      if (project.relationships.at(index) != desired) {
        m_relationshipChanges.append(
            {index, project.relationships.at(index), std::move(desired)});
      }
    } else {
      m_relationshipChanges.append(
          {nextRelationshipIndex++, std::nullopt, std::move(desired)});
    }
  }
}

void ApplyCppImportCommand::execute(ProjectData &project) {
  for (const auto &change : m_changes) {
    if (change.before) {
      if (auto *element = findElement(project, change.after.id))
        *element = change.after;
    } else {
      insertAtRecordedPosition(project.elements, change.index, change.after);
    }
  }
  for (const auto &change : m_relationshipChanges) {
    if (change.before) {
      if (auto *relationship = findRelationship(project, change.after.id))
        *relationship = change.after;
    } else {
      insertAtRecordedPosition(project.relationships, change.index,
                               change.after);
    }
  }
}

void ApplyCppImportCommand::revert(ProjectData &project) {
  for (auto change = m_relationshipChanges.crbegin();
       change != m_relationshipChanges.crend(); ++change) {
    if (change->before) {
      if (auto *relationship = findRelationship(project, change->after.id))
        *relationship = *change->before;
    } else {
      removeRecordedValue(project.relationships, change->index,
                          change->after.id);
    }
  }
  for (auto change = m_changes.crbegin(); change != m_changes.crend();
       ++change) {
    if (change->before) {
      if (auto *element = findElement(project, change->after.id))
        *element = *change->before;
    } else {
      removeRecordedValue(project.elements, change->index, change->after.id);
    }
  }
}

CreateBrowserFolderCommand::CreateBrowserFolderCommand(
    ProjectController *controller, const ProjectData &project,
    BrowserFolder folder)
    : ProjectCommand(controller, QStringLiteral("Create browser folder")),
      m_folder(std::move(folder)), m_index(project.browserFolders.size()) {}

void CreateBrowserFolderCommand::execute(ProjectData &project) {
  insertAtRecordedPosition(project.browserFolders, m_index, m_folder);
}

void CreateBrowserFolderCommand::revert(ProjectData &project) {
  removeRecordedValue(project.browserFolders, m_index, m_folder.id);
}

RenameBrowserFolderCommand::RenameBrowserFolderCommand(
    ProjectController *controller, QString folderId, QString before,
    QString after)
    : ProjectCommand(controller, QStringLiteral("Rename browser folder")),
      m_folderId(std::move(folderId)), m_before(std::move(before)),
      m_after(std::move(after)) {}

void RenameBrowserFolderCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void RenameBrowserFolderCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void RenameBrowserFolderCommand::apply(ProjectData &project,
                                       const QString &name) {
  if (auto *folder = findBrowserFolder(project, m_folderId))
    folder->name = name;
}

MoveBrowserItemsCommand::MoveBrowserItemsCommand(ProjectController *controller,
                                                 const ProjectData &project,
                                                 const QStringList &elementIds,
                                                 const QStringList &folderIds,
                                                 BrowserParent target)
    : ProjectCommand(controller,
                     elementIds.size() + folderIds.size() == 1
                         ? QStringLiteral("Move project-tree item")
                         : QStringLiteral("Move project-tree items")) {
  QSet<QString> seenElements;
  for (const QString &elementId : elementIds) {
    if (seenElements.contains(elementId))
      continue;
    seenElements.insert(elementId);
    const ModelElement *element = findElement(project, elementId);
    if (element && element->browserParent != target)
      m_changes.append({QStringLiteral("element"), elementId,
                        element->browserParent, target});
  }
  QSet<QString> seenFolders;
  for (const QString &folderId : folderIds) {
    if (seenFolders.contains(folderId))
      continue;
    seenFolders.insert(folderId);
    const BrowserFolder *folder = findBrowserFolder(project, folderId);
    if (folder && folder->parent != target)
      m_changes.append(
          {QStringLiteral("folder"), folderId, folder->parent, target});
  }
}

void MoveBrowserItemsCommand::execute(ProjectData &project) {
  apply(project, true);
}

void MoveBrowserItemsCommand::revert(ProjectData &project) {
  apply(project, false);
}

void MoveBrowserItemsCommand::apply(ProjectData &project, bool forward) {
  for (const auto &change : m_changes) {
    if (BrowserParent *parent =
            browserParentFor(project, change.kind, change.id))
      *parent = forward ? change.after : change.before;
  }
}

DeleteBrowserFolderCommand::DeleteBrowserFolderCommand(
    ProjectController *controller, const ProjectData &project, QString folderId)
    : ProjectCommand(controller, QStringLiteral("Delete browser folder")) {
  m_index = indexOfId(project.browserFolders, folderId);
  if (m_index < 0)
    return;
  m_folder = project.browserFolders.at(m_index);
  for (const auto &element : project.elements) {
    if (element.browserParent.kind == QStringLiteral("folder") &&
        element.browserParent.id == folderId)
      m_changes.append({QStringLiteral("element"), element.id,
                        element.browserParent, m_folder.parent});
  }
  for (const auto &folder : project.browserFolders) {
    if (folder.parent.kind == QStringLiteral("folder") &&
        folder.parent.id == folderId)
      m_changes.append({QStringLiteral("folder"), folder.id, folder.parent,
                        m_folder.parent});
  }
}

void DeleteBrowserFolderCommand::execute(ProjectData &project) {
  applyParentChanges(project, true);
  if (m_index >= 0)
    removeRecordedValue(project.browserFolders, m_index, m_folder.id);
}

void DeleteBrowserFolderCommand::revert(ProjectData &project) {
  if (m_index < 0)
    return;
  insertAtRecordedPosition(project.browserFolders, m_index, m_folder);
  applyParentChanges(project, false);
}

void DeleteBrowserFolderCommand::applyParentChanges(ProjectData &project,
                                                    bool forward) {
  for (const auto &change : m_changes) {
    if (BrowserParent *parent =
            browserParentFor(project, change.kind, change.id))
      *parent = forward ? change.after : change.before;
  }
}

CreateDiagramCommand::CreateDiagramCommand(ProjectController *controller,
                                           const ProjectData &project,
                                           Diagram diagram)
    : ProjectCommand(controller, QStringLiteral("Create diagram")),
      m_diagram(std::move(diagram)), m_index(project.diagrams.size()) {}

void CreateDiagramCommand::execute(ProjectData &project) {
  insertAtRecordedPosition(project.diagrams, m_index, m_diagram);
}

void CreateDiagramCommand::revert(ProjectData &project) {
  removeRecordedValue(project.diagrams, m_index, m_diagram.id);
}

AddElementToDiagramCommand::AddElementToDiagramCommand(
    ProjectController *controller, const ProjectData &project,
    QString diagramId, NodePresentation presentation,
    QList<ConnectorPresentation> connectors)
    : ProjectCommand(controller, QStringLiteral("Add element to diagram")),
      m_diagramId(std::move(diagramId)),
      m_presentation(std::move(presentation)) {
  const auto *diagram = findDiagram(project, m_diagramId);
  Q_ASSERT(diagram);
  m_index = diagram ? diagram->nodes.size() : -1;
  if (diagram) {
    qsizetype connectorIndex = diagram->connectors.size();
    m_connectors.reserve(connectors.size());
    for (auto &connector : connectors)
      m_connectors.append({connectorIndex++, std::move(connector)});
  }
}

void AddElementToDiagramCommand::execute(ProjectData &project) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    insertAtRecordedPosition(diagram->nodes, m_index, m_presentation);
    for (const auto &connector : m_connectors)
      insertAtRecordedPosition(diagram->connectors, connector.index,
                               connector.value);
  }
}

void AddElementToDiagramCommand::revert(ProjectData &project) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    // Remove in reverse insertion order so every recorded index remains valid.
    for (auto connector = m_connectors.crbegin();
         connector != m_connectors.crend(); ++connector)
      removeRecordedValue(diagram->connectors, connector->index,
                          connector->value.id);
    removeRecordedValue(diagram->nodes, m_index, m_presentation.id);
  }
}

AddElementsToDiagramCommand::AddElementsToDiagramCommand(
    ProjectController *controller, const ProjectData &project,
    QString diagramId, QList<NodePresentation> presentations,
    QList<ConnectorPresentation> connectors)
    : ProjectCommand(controller,
                     presentations.size() == 1
                         ? QStringLiteral("Add element to diagram")
                         : QStringLiteral("Add %1 elements to diagram")
                               .arg(presentations.size())),
      m_diagramId(std::move(diagramId)) {
  const auto *diagram = findDiagram(project, m_diagramId);
  Q_ASSERT(diagram);
  if (!diagram)
    return;

  qsizetype nodeIndex = diagram->nodes.size();
  m_presentations.reserve(presentations.size());
  for (auto &presentation : presentations)
    m_presentations.append({nodeIndex++, std::move(presentation)});

  qsizetype connectorIndex = diagram->connectors.size();
  m_connectors.reserve(connectors.size());
  for (auto &connector : connectors)
    m_connectors.append({connectorIndex++, std::move(connector)});
}

void AddElementsToDiagramCommand::execute(ProjectData &project) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    for (const auto &presentation : m_presentations)
      insertAtRecordedPosition(diagram->nodes, presentation.index,
                               presentation.value);
    for (const auto &connector : m_connectors)
      insertAtRecordedPosition(diagram->connectors, connector.index,
                               connector.value);
  }
}

void AddElementsToDiagramCommand::revert(ProjectData &project) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    for (auto connector = m_connectors.crbegin();
         connector != m_connectors.crend(); ++connector)
      removeRecordedValue(diagram->connectors, connector->index,
                          connector->value.id);
    for (auto presentation = m_presentations.crbegin();
         presentation != m_presentations.crend(); ++presentation)
      removeRecordedValue(diagram->nodes, presentation->index,
                          presentation->value.id);
  }
}

RemovePresentationsCommand::RemovePresentationsCommand(
    ProjectController *controller, const ProjectData &project,
    QString diagramId, const QSet<QString> &nodeIds)
    : ProjectCommand(controller,
                     nodeIds.size() == 1
                         ? QStringLiteral("Remove presentation from diagram")
                         : QStringLiteral("Remove presentations from diagram")),
      m_diagramId(std::move(diagramId)) {
  const auto *diagram = findDiagram(project, m_diagramId);
  Q_ASSERT(diagram);
  if (!diagram)
    return;

  QSet<QString> removedElementIds;
  for (qsizetype index = 0; index < diagram->nodes.size(); ++index) {
    const auto &node = diagram->nodes.at(index);
    if (nodeIds.contains(node.id)) {
      m_nodes.append({index, node});
      removedElementIds.insert(node.elementId);
    }
  }
  for (qsizetype index = 0; index < diagram->connectors.size(); ++index) {
    const auto &connector = diagram->connectors.at(index);
    const auto *relationship =
        findRelationship(project, connector.relationshipId);
    if (relationship && (removedElementIds.contains(relationship->sourceId) ||
                         removedElementIds.contains(relationship->targetId)))
      m_connectors.append({index, connector});
  }
}

void RemovePresentationsCommand::execute(ProjectData &project) {
  auto *diagram = findDiagram(project, m_diagramId);
  if (!diagram)
    return;
  for (auto item = m_connectors.crbegin(); item != m_connectors.crend(); ++item)
    removeRecordedValue(diagram->connectors, item->index, item->value.id);
  for (auto item = m_nodes.crbegin(); item != m_nodes.crend(); ++item)
    removeRecordedValue(diagram->nodes, item->index, item->value.id);
}

void RemovePresentationsCommand::revert(ProjectData &project) {
  auto *diagram = findDiagram(project, m_diagramId);
  if (!diagram)
    return;
  for (const auto &item : m_nodes)
    insertAtRecordedPosition(diagram->nodes, item.index, item.value);
  for (const auto &item : m_connectors)
    insertAtRecordedPosition(diagram->connectors, item.index, item.value);
}

DeleteDiagramCommand::DeleteDiagramCommand(ProjectController *controller,
                                           const ProjectData &project,
                                           QString diagramId)
    : ProjectCommand(controller, QStringLiteral("Delete diagram")),
      m_index(indexOfId(project.diagrams, diagramId)) {
  Q_ASSERT(m_index >= 0);
  if (m_index >= 0)
    m_diagram = project.diagrams.at(m_index);
}

void DeleteDiagramCommand::execute(ProjectData &project) {
  removeRecordedValue(project.diagrams, m_index, m_diagram.id);
}

void DeleteDiagramCommand::revert(ProjectData &project) {
  insertAtRecordedPosition(project.diagrams, m_index, m_diagram);
}

DeleteRelationshipCommand::DeleteRelationshipCommand(
    ProjectController *controller, const ProjectData &project,
    QString relationshipId)
    : ProjectCommand(controller, QStringLiteral("Delete relationship")),
      m_relationshipIndex(indexOfId(project.relationships, relationshipId)) {
  Q_ASSERT(m_relationshipIndex >= 0);
  if (m_relationshipIndex < 0)
    return;
  m_relationship = project.relationships.at(m_relationshipIndex);
  for (const auto &diagram : project.diagrams) {
    DiagramConnectors removed{diagram.id, {}};
    for (qsizetype index = 0; index < diagram.connectors.size(); ++index) {
      const auto &connector = diagram.connectors.at(index);
      if (connector.relationshipId == relationshipId)
        removed.connectors.append({index, connector});
    }
    if (!removed.connectors.isEmpty())
      m_diagrams.append(std::move(removed));
  }
}

void DeleteRelationshipCommand::execute(ProjectData &project) {
  for (const auto &removed : m_diagrams) {
    if (auto *diagram = findDiagram(project, removed.diagramId)) {
      for (auto item = removed.connectors.crbegin();
           item != removed.connectors.crend(); ++item)
        removeRecordedValue(diagram->connectors, item->index, item->value.id);
    }
  }
  removeRecordedValue(project.relationships, m_relationshipIndex,
                      m_relationship.id);
}

void DeleteRelationshipCommand::revert(ProjectData &project) {
  insertAtRecordedPosition(project.relationships, m_relationshipIndex,
                           m_relationship);
  for (const auto &removed : m_diagrams) {
    if (auto *diagram = findDiagram(project, removed.diagramId)) {
      for (const auto &item : removed.connectors)
        insertAtRecordedPosition(diagram->connectors, item.index, item.value);
    }
  }
}

DeleteElementCommand::DeleteElementCommand(ProjectController *controller,
                                           const ProjectData &project,
                                           QString elementId)
    : ProjectCommand(controller, QStringLiteral("Delete element")),
      m_elementIndex(indexOfId(project.elements, elementId)) {
  Q_ASSERT(m_elementIndex >= 0);
  if (m_elementIndex < 0)
    return;
  m_element = project.elements.at(m_elementIndex);

  QSet<QString> relationshipIds;
  for (qsizetype index = 0; index < project.relationships.size(); ++index) {
    const auto &relationship = project.relationships.at(index);
    if (relationship.sourceId == elementId ||
        relationship.targetId == elementId) {
      m_relationships.append({index, relationship});
      relationshipIds.insert(relationship.id);
    }
  }

  for (const auto &diagram : project.diagrams) {
    DiagramRecords records{diagram.id, {}, {}};
    for (qsizetype index = 0; index < diagram.nodes.size(); ++index) {
      const auto &node = diagram.nodes.at(index);
      if (node.elementId == elementId)
        records.nodes.append({index, node});
    }
    for (qsizetype index = 0; index < diagram.connectors.size(); ++index) {
      const auto &connector = diagram.connectors.at(index);
      if (relationshipIds.contains(connector.relationshipId))
        records.connectors.append({index, connector});
    }
    if (!records.nodes.isEmpty() || !records.connectors.isEmpty())
      m_diagrams.append(std::move(records));
  }

  for (const auto &element : project.elements) {
    if (element.id != elementId &&
        element.browserParent.kind == QStringLiteral("element") &&
        element.browserParent.id == elementId)
      m_browserParentChanges.append(
          {QStringLiteral("element"), element.id, element.browserParent, {}});
  }
  for (const auto &folder : project.browserFolders) {
    if (folder.parent.kind == QStringLiteral("element") &&
        folder.parent.id == elementId)
      m_browserParentChanges.append({QStringLiteral("folder"),
                                     folder.id,
                                     folder.parent,
                                     {QStringLiteral("model"), {}}});
  }
}

void DeleteElementCommand::execute(ProjectData &project) {
  for (const auto &change : m_browserParentChanges)
    if (BrowserParent *parent =
            browserParentFor(project, change.kind, change.id))
      *parent = change.after;
  for (const auto &records : m_diagrams) {
    if (auto *diagram = findDiagram(project, records.diagramId)) {
      for (auto item = records.connectors.crbegin();
           item != records.connectors.crend(); ++item)
        removeRecordedValue(diagram->connectors, item->index, item->value.id);
      for (auto item = records.nodes.crbegin(); item != records.nodes.crend();
           ++item)
        removeRecordedValue(diagram->nodes, item->index, item->value.id);
    }
  }
  for (auto item = m_relationships.crbegin(); item != m_relationships.crend();
       ++item)
    removeRecordedValue(project.relationships, item->index, item->value.id);
  removeRecordedValue(project.elements, m_elementIndex, m_element.id);
}

void DeleteElementCommand::revert(ProjectData &project) {
  insertAtRecordedPosition(project.elements, m_elementIndex, m_element);
  for (const auto &change : m_browserParentChanges)
    if (BrowserParent *parent =
            browserParentFor(project, change.kind, change.id))
      *parent = change.before;
  for (const auto &item : m_relationships)
    insertAtRecordedPosition(project.relationships, item.index, item.value);
  for (const auto &records : m_diagrams) {
    if (auto *diagram = findDiagram(project, records.diagramId)) {
      for (const auto &item : records.nodes)
        insertAtRecordedPosition(diagram->nodes, item.index, item.value);
      for (const auto &item : records.connectors)
        insertAtRecordedPosition(diagram->connectors, item.index, item.value);
    }
  }
}

UpdateNodeGeometriesCommand::UpdateNodeGeometriesCommand(
    ProjectController *controller, QString diagramId,
    QList<NodeGeometryChange> changes, QString description)
    : ProjectCommand(controller, description),
      m_diagramId(std::move(diagramId)), m_changes(std::move(changes)) {}

void UpdateNodeGeometriesCommand::execute(ProjectData &project) {
  apply(project, true);
}

void UpdateNodeGeometriesCommand::revert(ProjectData &project) {
  apply(project, false);
}

void UpdateNodeGeometriesCommand::apply(ProjectData &project, bool forward) {
  auto *diagram = findDiagram(project, m_diagramId);
  if (!diagram)
    return;
  for (const auto &change : m_changes) {
    if (auto *node = findNode(*diagram, change.nodeId))
      node->geometry = forward ? change.after : change.before;
  }
}

CreateRelationshipCommand::CreateRelationshipCommand(
    ProjectController *controller, const ProjectData &project,
    QString diagramId, Relationship relationship,
    ConnectorPresentation connector)
    : ProjectCommand(
          controller,
          QStringLiteral("Create %1").arg(toString(relationship.type))),
      m_diagramId(std::move(diagramId)),
      m_relationship(std::move(relationship)),
      m_connector(std::move(connector)),
      m_relationshipIndex(project.relationships.size()) {
  const auto *diagram = findDiagram(project, m_diagramId);
  Q_ASSERT(diagram);
  m_connectorIndex = diagram ? diagram->connectors.size() : -1;
}

void CreateRelationshipCommand::execute(ProjectData &project) {
  insertAtRecordedPosition(project.relationships, m_relationshipIndex,
                           m_relationship);
  if (auto *diagram = findDiagram(project, m_diagramId))
    insertAtRecordedPosition(diagram->connectors, m_connectorIndex,
                             m_connector);
}

void CreateRelationshipCommand::revert(ProjectData &project) {
  if (auto *diagram = findDiagram(project, m_diagramId))
    removeRecordedValue(diagram->connectors, m_connectorIndex, m_connector.id);
  removeRecordedValue(project.relationships, m_relationshipIndex,
                      m_relationship.id);
}

ReconnectRelationshipCommand::ReconnectRelationshipCommand(
    ProjectController *controller, QString diagramId, QString connectorId,
    QString relationshipId, bool reconnectSource, QString beforeElementId,
    QString afterElementId, ConnectorAnchor beforeAnchor,
    ConnectorAnchor afterAnchor, QList<ConnectorBendPoint> beforeBendPoints,
    QList<ConnectorBendPoint> afterBendPoints)
    : ProjectCommand(controller, reconnectSource
                                     ? QStringLiteral("Reconnect source")
                                     : QStringLiteral("Reconnect target")),
      m_diagramId(std::move(diagramId)), m_connectorId(std::move(connectorId)),
      m_relationshipId(std::move(relationshipId)),
      m_reconnectSource(reconnectSource),
      m_beforeElementId(std::move(beforeElementId)),
      m_afterElementId(std::move(afterElementId)),
      m_beforeAnchor(std::move(beforeAnchor)),
      m_afterAnchor(std::move(afterAnchor)),
      m_beforeBendPoints(std::move(beforeBendPoints)),
      m_afterBendPoints(std::move(afterBendPoints)) {}

void ReconnectRelationshipCommand::execute(ProjectData &project) {
  apply(project, true);
}

void ReconnectRelationshipCommand::revert(ProjectData &project) {
  apply(project, false);
}

void ReconnectRelationshipCommand::apply(ProjectData &project, bool forward) {
  if (auto *relationship = findRelationship(project, m_relationshipId)) {
    QString &endpoint =
        m_reconnectSource ? relationship->sourceId : relationship->targetId;
    endpoint = forward ? m_afterElementId : m_beforeElementId;
  }
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    if (auto *connector = findConnector(*diagram, m_connectorId)) {
      ConnectorAnchor &anchor =
          m_reconnectSource ? connector->sourceAnchor : connector->targetAnchor;
      anchor = forward ? m_afterAnchor : m_beforeAnchor;
      connector->bendPoints = forward ? m_afterBendPoints : m_beforeBendPoints;
    }
  }
}

MoveConnectorAnchorCommand::MoveConnectorAnchorCommand(
    ProjectController *controller, QString diagramId, QString connectorId,
    bool source, ConnectorAnchor before, ConnectorAnchor after)
    : ProjectCommand(controller,
                     source ? QStringLiteral("Move connector source port")
                            : QStringLiteral("Move connector target port")),
      m_diagramId(std::move(diagramId)), m_connectorId(std::move(connectorId)),
      m_source(source), m_before(std::move(before)), m_after(std::move(after)) {
}

void MoveConnectorAnchorCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void MoveConnectorAnchorCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void MoveConnectorAnchorCommand::apply(ProjectData &project,
                                       const ConnectorAnchor &anchor) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    if (auto *connector = findConnector(*diagram, m_connectorId)) {
      ConnectorAnchor &target =
          m_source ? connector->sourceAnchor : connector->targetAnchor;
      target = anchor;
    }
  }
}

SetConnectorRoutingCommand::SetConnectorRoutingCommand(
    ProjectController *controller, QString diagramId, QString connectorId,
    ConnectorRouting before, ConnectorRouting after)
    : ProjectCommand(
          controller,
          QStringLiteral("Use %1 connector routing").arg(toString(after))),
      m_diagramId(std::move(diagramId)), m_connectorId(std::move(connectorId)),
      m_before(before), m_after(after) {}

void SetConnectorRoutingCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void SetConnectorRoutingCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void SetConnectorRoutingCommand::apply(ProjectData &project,
                                       ConnectorRouting routing) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    if (auto *connector = findConnector(*diagram, m_connectorId))
      connector->routing = routing;
  }
}

UpdateConnectorBendPointsCommand::UpdateConnectorBendPointsCommand(
    ProjectController *controller, QString diagramId, QString connectorId,
    QList<ConnectorBendPoint> before, QList<ConnectorBendPoint> after,
    const QString &description)
    : ProjectCommand(controller, description),
      m_diagramId(std::move(diagramId)), m_connectorId(std::move(connectorId)),
      m_before(std::move(before)), m_after(std::move(after)) {}

void UpdateConnectorBendPointsCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void UpdateConnectorBendPointsCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void UpdateConnectorBendPointsCommand::apply(
    ProjectData &project, const QList<ConnectorBendPoint> &bendPoints) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    if (auto *connector = findConnector(*diagram, m_connectorId))
      connector->bendPoints = bendPoints;
  }
}

EditElementTextCommand::EditElementTextCommand(ProjectController *controller,
                                               QString elementId,
                                               ElementTextProperty property,
                                               int index, QString before,
                                               QString after,
                                               const QString &description)
    : ProjectCommand(controller, description),
      m_elementId(std::move(elementId)), m_property(property), m_index(index),
      m_before(std::move(before)), m_after(std::move(after)) {}

void EditElementTextCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void EditElementTextCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void EditElementTextCommand::apply(ProjectData &project, const QString &value) {
  auto *element = findElement(project, m_elementId);
  if (!element)
    return;
  switch (m_property) {
  case ElementTextProperty::Name:
    element->name = value;
    break;
  case ElementTextProperty::Attribute:
    if (m_index >= 0 && m_index < element->attributes.size())
      element->attributes[m_index] = value;
    break;
  case ElementTextProperty::Operation:
    if (m_index >= 0 && m_index < element->operations.size())
      element->operations[m_index] = value;
    break;
  case ElementTextProperty::Literal:
    if (m_index >= 0 && m_index < element->enumLiterals.size())
      element->enumLiterals[m_index] = value;
    break;
  }
}

SetElementListCommand::SetElementListCommand(ProjectController *controller,
                                             QString elementId,
                                             ElementListProperty property,
                                             QStringList before,
                                             QStringList after,
                                             const QString &description)
    : ProjectCommand(controller, description),
      m_elementId(std::move(elementId)), m_property(property),
      m_before(std::move(before)), m_after(std::move(after)) {}

void SetElementListCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void SetElementListCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void SetElementListCommand::apply(ProjectData &project,
                                  const QStringList &value) {
  auto *element = findElement(project, m_elementId);
  if (!element)
    return;
  switch (m_property) {
  case ElementListProperty::Attributes:
    element->attributes = value;
    break;
  case ElementListProperty::Operations:
    element->operations = value;
    break;
  case ElementListProperty::Literals:
    element->enumLiterals = value;
    break;
  }
}

RenameRelationshipCommand::RenameRelationshipCommand(
    ProjectController *controller, QString relationshipId, QString before,
    QString after)
    : ProjectCommand(controller, QStringLiteral("Edit name")),
      m_relationshipId(std::move(relationshipId)), m_before(std::move(before)),
      m_after(std::move(after)) {}

void RenameRelationshipCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void RenameRelationshipCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void RenameRelationshipCommand::apply(ProjectData &project,
                                      const QString &value) {
  if (auto *relationship = findRelationship(project, m_relationshipId))
    relationship->name = value;
}

RenameDiagramCommand::RenameDiagramCommand(ProjectController *controller,
                                           QString diagramId, QString before,
                                           QString after)
    : ProjectCommand(controller, QStringLiteral("Edit name")),
      m_diagramId(std::move(diagramId)), m_before(std::move(before)),
      m_after(std::move(after)) {}

void RenameDiagramCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void RenameDiagramCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void RenameDiagramCommand::apply(ProjectData &project, const QString &value) {
  if (auto *diagram = findDiagram(project, m_diagramId))
    diagram->name = value;
}

} // namespace uuml
