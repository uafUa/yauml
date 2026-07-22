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
}

void DeleteElementCommand::execute(ProjectData &project) {
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
    ConnectorAnchor afterAnchor)
    : ProjectCommand(controller, reconnectSource
                                     ? QStringLiteral("Reconnect source")
                                     : QStringLiteral("Reconnect target")),
      m_diagramId(std::move(diagramId)), m_connectorId(std::move(connectorId)),
      m_relationshipId(std::move(relationshipId)),
      m_reconnectSource(reconnectSource),
      m_beforeElementId(std::move(beforeElementId)),
      m_afterElementId(std::move(afterElementId)),
      m_beforeAnchor(std::move(beforeAnchor)),
      m_afterAnchor(std::move(afterAnchor)) {}

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
