#include "core/project_commands.h"

#include "core/diagram_filter.h"
#include "core/project_controller.h"

#include <algorithm>
#include <utility>

namespace yauml {
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

QString *styleAssignmentFor(ProjectData &project, const QString &kind,
                            const QString &diagramId,
                            const QString &subjectId) {
  if (kind == QStringLiteral("element")) {
    if (auto *element = findElement(project, subjectId))
      return &element->styleId;
  } else if (kind == QStringLiteral("folder")) {
    if (auto *folder = findBrowserFolder(project, subjectId))
      return &folder->styleId;
  } else if (kind == QStringLiteral("node")) {
    if (auto *diagram = findDiagram(project, diagramId))
      if (auto *node = findNode(*diagram, subjectId))
        return &node->styleId;
  } else if (kind == QStringLiteral("container")) {
    if (auto *diagram = findDiagram(project, diagramId))
      if (auto *container = findContainer(*diagram, subjectId))
        return &container->styleId;
  }
  return nullptr;
}

void applyStyleAssignment(ProjectData &project,
                          const StyleAssignmentChange &change,
                          const QString &styleId) {
  if (change.kind == QStringLiteral("namespace")) {
    if (styleId.isEmpty())
      project.namespaceStyleIds.remove(change.subjectId);
    else
      project.namespaceStyleIds.insert(change.subjectId, styleId);
    return;
  }
  if (QString *assignment = styleAssignmentFor(
          project, change.kind, change.diagramId, change.subjectId))
    *assignment = styleId;
}

QString browserItemKey(const QString &kind, const QString &id) {
  return kind + u':' + id;
}

std::optional<ContainerChildrenChange>
promoteContainerInOwner(const Diagram &diagram,
                        const ContainerPresentation &removed) {
  for (const auto &candidate : diagram.containers) {
    const qsizetype position =
        candidate.childPresentationIds.indexOf(removed.id);
    if (position < 0)
      continue;
    QStringList after = candidate.childPresentationIds;
    after.removeAt(position);
    for (qsizetype index = 0; index < removed.childPresentationIds.size();
         ++index)
      after.insert(position + index, removed.childPresentationIds.at(index));
    return ContainerChildrenChange{candidate.id, candidate.childPresentationIds,
                                   std::move(after)};
  }
  return std::nullopt;
}

QList<ContainerChildrenChange>
membershipChangesForRemoval(const Diagram &diagram,
                            const QSet<QString> &removedIds) {
  QList<ContainerChildrenChange> changes;
  for (const auto &container : diagram.containers) {
    QStringList after = container.childPresentationIds;
    after.removeIf(
        [&](const QString &childId) { return removedIds.contains(childId); });
    if (after != container.childPresentationIds)
      changes.append(
          {container.id, container.childPresentationIds, std::move(after)});
  }
  return changes;
}

void applyMembershipChanges(Diagram &diagram,
                            const QList<ContainerChildrenChange> &changes,
                            bool forward) {
  for (const auto &change : changes) {
    if (auto *container = findContainer(diagram, change.containerId))
      container->childPresentationIds = forward ? change.after : change.before;
  }
}

void applyPortSnapPointChanges(Diagram &diagram,
                               const QList<NodePortSnapPointChange> &changes,
                               bool forward) {
  for (const auto &change : changes) {
    if (auto *node = findNode(diagram, change.nodeId)) {
      node->horizontalPortSnapPoints =
          forward ? change.afterHorizontal : change.beforeHorizontal;
      node->verticalPortSnapPoints =
          forward ? change.afterVertical : change.beforeVertical;
    }
    for (const auto &anchorChange : change.anchorChanges) {
      if (auto *connector = findConnector(diagram, anchorChange.connectorId)) {
        ConnectorAnchor &anchor = anchorChange.source ? connector->sourceAnchor
                                                      : connector->targetAnchor;
        anchor.offset = forward ? anchorChange.after : anchorChange.before;
      }
    }
  }
}

} // namespace

CreateElementCommand::CreateElementCommand(
    ProjectController *controller, const ProjectData &project,
    ModelElement element, QString diagramId,
    std::optional<NodePresentation> nodePresentation,
    std::optional<ContainerPresentation> containerPresentation)
    : ProjectCommand(controller,
                     QStringLiteral("Create %1").arg(toString(element.type))),
      m_element(std::move(element)), m_elementIndex(project.elements.size()),
      m_diagramId(std::move(diagramId)),
      m_nodePresentation(std::move(nodePresentation)),
      m_containerPresentation(std::move(containerPresentation)) {
  if (m_nodePresentation || m_containerPresentation) {
    const auto *diagram = findDiagram(project, m_diagramId);
    Q_ASSERT(diagram);
    m_presentationIndex = !diagram             ? -1
                          : m_nodePresentation ? diagram->nodes.size()
                                               : diagram->containers.size();
  }
}

void CreateElementCommand::execute(ProjectData &project) {
  insertAtRecordedPosition(project.elements, m_elementIndex, m_element);
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    if (m_nodePresentation)
      insertAtRecordedPosition(diagram->nodes, m_presentationIndex,
                               *m_nodePresentation);
    else if (m_containerPresentation)
      insertAtRecordedPosition(diagram->containers, m_presentationIndex,
                               *m_containerPresentation);
  }
}

void CreateElementCommand::revert(ProjectData &project) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    if (m_nodePresentation)
      removeRecordedValue(diagram->nodes, m_presentationIndex,
                          m_nodePresentation->id);
    else if (m_containerPresentation)
      removeRecordedValue(diagram->containers, m_presentationIndex,
                          m_containerPresentation->id);
  }
  removeRecordedValue(project.elements, m_elementIndex, m_element.id);
}

ApplyCppImportCommand::ApplyCppImportCommand(
    ProjectController *controller, const ProjectData &project,
    QList<ModelElement> desiredElements,
    QList<Relationship> desiredRelationships, QStringList sourceRoots)
    : ProjectCommand(controller,
                     desiredElements.isEmpty() && desiredRelationships.isEmpty()
                         ? QStringLiteral("Configure C++ synchronization")
                         : QStringLiteral("Import C++ changes")),
      m_sourceRootsBefore(project.cppImport.sourceRoots),
      m_sourceRootsAfter(sourceRoots.isEmpty() ? project.cppImport.sourceRoots
                                               : std::move(sourceRoots)),
      m_sourceRootsChanged(m_sourceRootsBefore != m_sourceRootsAfter) {
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
  if (m_sourceRootsChanged)
    project.cppImport.sourceRoots = m_sourceRootsAfter;
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
  if (m_sourceRootsChanged)
    project.cppImport.sourceRoots = m_sourceRootsBefore;
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

MoveBrowserItemsCommand::MoveBrowserItemsCommand(
    ProjectController *controller, const ProjectData &project,
    const QStringList &elementIds, const QStringList &folderIds,
    BrowserParent target, std::optional<QString> targetPackageId,
    std::optional<QString> targetEnclosingTypeId)
    : ProjectCommand(controller,
                     elementIds.size() + folderIds.size() == 1
                         ? QStringLiteral("Move project-tree item")
                         : QStringLiteral("Move project-tree items")) {
  QSet<QString> seenElements;
  QSet<QString> semanticSubtreeIds;
  for (const QString &elementId : elementIds) {
    if (seenElements.contains(elementId))
      continue;
    seenElements.insert(elementId);
    const ModelElement *element = findElement(project, elementId);
    if (element && element->browserParent != target)
      m_changes.append({QStringLiteral("element"), elementId,
                        element->browserParent, target});
    if (element && targetPackageId && element->packageId != *targetPackageId)
      m_packageChanges.append(
          {elementId, element->packageId, *targetPackageId});
    if (element && targetEnclosingTypeId) {
      const QString desiredOwner = element->type == ElementType::Package
                                       ? QString{}
                                       : *targetEnclosingTypeId;
      if (element->enclosingTypeId != desiredOwner)
        m_enclosingTypeChanges.append(
            {elementId, element->enclosingTypeId, desiredOwner});
    }
    if (element)
      semanticSubtreeIds.insert(elementId);
  }

  // A nested subtree follows its moved root to the target package. Direct
  // descendants retain their owner IDs, so only the roots change owner.
  bool foundDescendant = true;
  while (foundDescendant) {
    foundDescendant = false;
    for (const auto &element : project.elements) {
      if (!semanticSubtreeIds.contains(element.id) &&
          semanticSubtreeIds.contains(element.enclosingTypeId)) {
        semanticSubtreeIds.insert(element.id);
        foundDescendant = true;
      }
    }
  }
  if (targetPackageId) {
    for (const auto &element : project.elements) {
      if (semanticSubtreeIds.contains(element.id) &&
          !seenElements.contains(element.id) &&
          element.packageId != *targetPackageId)
        m_packageChanges.append(
            {element.id, element.packageId, *targetPackageId});
    }
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
  for (const auto &change : m_packageChanges)
    if (auto *element = findElement(project, change.elementId))
      element->packageId = forward ? change.after : change.before;
  for (const auto &change : m_enclosingTypeChanges)
    if (auto *element = findElement(project, change.elementId))
      element->enclosingTypeId = forward ? change.after : change.before;
}

ReorderBrowserItemsCommand::ReorderBrowserItemsCommand(
    ProjectController *controller, QStringList before, QStringList after,
    int itemCount)
    : ProjectCommand(
          controller,
          itemCount == 1
              ? QStringLiteral("Reorder project-tree item")
              : QStringLiteral("Reorder %1 project-tree items").arg(itemCount)),
      m_before(std::move(before)), m_after(std::move(after)) {}

void ReorderBrowserItemsCommand::execute(ProjectData &project) {
  project.browserItemOrder = m_after;
}

void ReorderBrowserItemsCommand::revert(ProjectData &project) {
  project.browserItemOrder = m_before;
}

DeleteBrowserFolderCommand::DeleteBrowserFolderCommand(
    ProjectController *controller, const ProjectData &project, QString folderId)
    : ProjectCommand(controller, QStringLiteral("Delete browser folder")) {
  m_index = indexOfId(project.browserFolders, folderId);
  if (m_index < 0)
    return;
  m_folder = project.browserFolders.at(m_index);
  m_browserOrderIndex = project.browserItemOrder.indexOf(
      browserItemKey(QStringLiteral("folder"), folderId));
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
  for (const auto &diagram : project.diagrams) {
    for (qsizetype index = 0; index < diagram.containers.size(); ++index) {
      const auto &container = diagram.containers.at(index);
      if (container.subjectKind == QStringLiteral("folder") &&
          container.subjectId == folderId) {
        m_diagramContainers.append(
            {diagram.id, index, container,
             promoteContainerInOwner(diagram, container)});
        break;
      }
    }
  }
}

void DeleteBrowserFolderCommand::execute(ProjectData &project) {
  applyParentChanges(project, true);
  for (const auto &record : m_diagramContainers) {
    if (auto *diagram = findDiagram(project, record.diagramId)) {
      if (record.ownerChange)
        applyMembershipChanges(*diagram, {*record.ownerChange}, true);
      removeRecordedValue(diagram->containers, record.index, record.value.id);
    }
  }
  if (m_index >= 0)
    removeRecordedValue(project.browserFolders, m_index, m_folder.id);
  project.browserItemOrder.removeAll(
      browserItemKey(QStringLiteral("folder"), m_folder.id));
}

void DeleteBrowserFolderCommand::revert(ProjectData &project) {
  if (m_index < 0)
    return;
  insertAtRecordedPosition(project.browserFolders, m_index, m_folder);
  if (m_browserOrderIndex >= 0)
    project.browserItemOrder.insert(
        std::clamp(m_browserOrderIndex, qsizetype{0},
                   project.browserItemOrder.size()),
        browserItemKey(QStringLiteral("folder"), m_folder.id));
  for (const auto &record : m_diagramContainers) {
    if (auto *diagram = findDiagram(project, record.diagramId)) {
      insertAtRecordedPosition(diagram->containers, record.index, record.value);
      if (record.ownerChange)
        applyMembershipChanges(*diagram, {*record.ownerChange}, false);
    }
  }
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
    QList<ConnectorPresentation> connectors,
    QList<NodePortSnapPointChange> portChanges)
    : ProjectCommand(controller,
                     presentations.size() == 1
                         ? QStringLiteral("Add element to diagram")
                         : QStringLiteral("Add %1 elements to diagram")
                               .arg(presentations.size())),
      m_diagramId(std::move(diagramId)), m_portChanges(std::move(portChanges)) {
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
    applyPortSnapPointChanges(*diagram, m_portChanges, true);
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
    applyPortSnapPointChanges(*diagram, m_portChanges, false);
    for (auto presentation = m_presentations.crbegin();
         presentation != m_presentations.crend(); ++presentation)
      removeRecordedValue(diagram->nodes, presentation->index,
                          presentation->value.id);
  }
}

AddContainerPresentationsCommand::AddContainerPresentationsCommand(
    ProjectController *controller, const ProjectData &project,
    QString diagramId, QList<ContainerPresentation> containers,
    QList<NodePresentation> nodes, QList<ConnectorPresentation> connectors,
    QList<ContainerChildrenChange> membershipChanges,
    QList<NodePortSnapPointChange> portChanges,
    QList<PresentationGeometryChange> geometryChanges, QString description)
    : ProjectCommand(controller, description),
      m_diagramId(std::move(diagramId)),
      m_membershipChanges(std::move(membershipChanges)),
      m_portChanges(std::move(portChanges)),
      m_geometryChanges(std::move(geometryChanges)) {
  const auto *diagram = findDiagram(project, m_diagramId);
  Q_ASSERT(diagram);
  if (!diagram)
    return;

  qsizetype containerIndex = diagram->containers.size();
  m_containers.reserve(containers.size());
  for (auto &container : containers)
    m_containers.append({containerIndex++, std::move(container)});

  qsizetype nodeIndex = diagram->nodes.size();
  m_nodes.reserve(nodes.size());
  for (auto &node : nodes)
    m_nodes.append({nodeIndex++, std::move(node)});

  qsizetype connectorIndex = diagram->connectors.size();
  m_connectors.reserve(connectors.size());
  for (auto &connector : connectors)
    m_connectors.append({connectorIndex++, std::move(connector)});
}

void AddContainerPresentationsCommand::execute(ProjectData &project) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    for (const auto &container : m_containers)
      insertAtRecordedPosition(diagram->containers, container.index,
                               container.value);
    for (const auto &node : m_nodes)
      insertAtRecordedPosition(diagram->nodes, node.index, node.value);
    applyPortSnapPointChanges(*diagram, m_portChanges, true);
    for (const auto &connector : m_connectors)
      insertAtRecordedPosition(diagram->connectors, connector.index,
                               connector.value);
    applyMembershipChanges(*diagram, m_membershipChanges, true);
    for (const auto &change : m_geometryChanges) {
      if (auto *node = findNode(*diagram, change.presentationId))
        node->geometry = change.after;
      else if (auto *container = findContainer(*diagram, change.presentationId))
        container->geometry = change.after;
    }
  }
}

void AddContainerPresentationsCommand::revert(ProjectData &project) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    for (const auto &change : m_geometryChanges) {
      if (auto *node = findNode(*diagram, change.presentationId))
        node->geometry = change.before;
      else if (auto *container = findContainer(*diagram, change.presentationId))
        container->geometry = change.before;
    }
    applyMembershipChanges(*diagram, m_membershipChanges, false);
    for (auto connector = m_connectors.crbegin();
         connector != m_connectors.crend(); ++connector)
      removeRecordedValue(diagram->connectors, connector->index,
                          connector->value.id);
    applyPortSnapPointChanges(*diagram, m_portChanges, false);
    for (auto node = m_nodes.crbegin(); node != m_nodes.crend(); ++node)
      removeRecordedValue(diagram->nodes, node->index, node->value.id);
    for (auto container = m_containers.crbegin();
         container != m_containers.crend(); ++container)
      removeRecordedValue(diagram->containers, container->index,
                          container->value.id);
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
  m_membershipChanges = membershipChangesForRemoval(*diagram, nodeIds);
}

void RemovePresentationsCommand::execute(ProjectData &project) {
  auto *diagram = findDiagram(project, m_diagramId);
  if (!diagram)
    return;
  for (auto item = m_connectors.crbegin(); item != m_connectors.crend(); ++item)
    removeRecordedValue(diagram->connectors, item->index, item->value.id);
  for (auto item = m_nodes.crbegin(); item != m_nodes.crend(); ++item)
    removeRecordedValue(diagram->nodes, item->index, item->value.id);
  applyMembershipChanges(*diagram, m_membershipChanges, true);
}

void RemovePresentationsCommand::revert(ProjectData &project) {
  auto *diagram = findDiagram(project, m_diagramId);
  if (!diagram)
    return;
  for (const auto &item : m_nodes)
    insertAtRecordedPosition(diagram->nodes, item.index, item.value);
  for (const auto &item : m_connectors)
    insertAtRecordedPosition(diagram->connectors, item.index, item.value);
  applyMembershipChanges(*diagram, m_membershipChanges, false);
}

RemoveContainerPresentationCommand::RemoveContainerPresentationCommand(
    ProjectController *controller, const ProjectData &project,
    QString diagramId, QString containerId)
    : ProjectCommand(controller,
                     QStringLiteral("Remove folder presentation from diagram")),
      m_diagramId(std::move(diagramId)) {
  const auto *diagram = findDiagram(project, m_diagramId);
  if (!diagram)
    return;
  m_index = indexOfId(diagram->containers, containerId);
  if (m_index < 0)
    return;
  m_container = diagram->containers.at(m_index);
  m_ownerChange = promoteContainerInOwner(*diagram, m_container);
}

void RemoveContainerPresentationCommand::execute(ProjectData &project) {
  auto *diagram = findDiagram(project, m_diagramId);
  if (!diagram || m_index < 0)
    return;
  if (m_ownerChange)
    applyMembershipChanges(*diagram, {*m_ownerChange}, true);
  removeRecordedValue(diagram->containers, m_index, m_container.id);
}

void RemoveContainerPresentationCommand::revert(ProjectData &project) {
  auto *diagram = findDiagram(project, m_diagramId);
  if (!diagram || m_index < 0)
    return;
  insertAtRecordedPosition(diagram->containers, m_index, m_container);
  if (m_ownerChange)
    applyMembershipChanges(*diagram, {*m_ownerChange}, false);
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
  m_browserOrderIndex = project.browserItemOrder.indexOf(
      browserItemKey(QStringLiteral("element"), elementId));

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
    DiagramRecords records{diagram.id, {}, {}, {}, std::nullopt, std::nullopt};
    QSet<QString> removedNodeIds;
    for (qsizetype index = 0; index < diagram.nodes.size(); ++index) {
      const auto &node = diagram.nodes.at(index);
      if (node.elementId == elementId) {
        records.nodes.append({index, node});
        removedNodeIds.insert(node.id);
      }
    }
    for (qsizetype index = 0; index < diagram.connectors.size(); ++index) {
      const auto &connector = diagram.connectors.at(index);
      if (relationshipIds.contains(connector.relationshipId))
        records.connectors.append({index, connector});
    }
    records.membershipChanges =
        membershipChangesForRemoval(diagram, removedNodeIds);
    for (qsizetype index = 0; index < diagram.containers.size(); ++index) {
      const auto &container = diagram.containers.at(index);
      if (container.subjectKind == QStringLiteral("package") &&
          container.subjectId == elementId) {
        records.container = PositionedContainer{index, container};
        records.containerOwnerChange =
            promoteContainerInOwner(diagram, container);
        break;
      }
    }
    if (!records.nodes.isEmpty() || !records.connectors.isEmpty() ||
        !records.membershipChanges.isEmpty() || records.container)
      m_diagrams.append(std::move(records));
  }

  for (const auto &element : project.elements) {
    if (element.id != elementId &&
        element.browserParent.kind == QStringLiteral("element") &&
        element.browserParent.id == elementId)
      m_browserParentChanges.append(
          {QStringLiteral("element"), element.id, element.browserParent, {}});
    if (element.id != elementId && element.packageId == elementId)
      m_packageChanges.append(
          {element.id, element.packageId, m_element.packageId});
    if (element.id != elementId && element.enclosingTypeId == elementId)
      m_enclosingTypeChanges.append(
          {element.id, element.enclosingTypeId, m_element.enclosingTypeId});
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
  for (const auto &change : m_packageChanges)
    if (auto *element = findElement(project, change.elementId))
      element->packageId = change.after;
  for (const auto &change : m_enclosingTypeChanges)
    if (auto *element = findElement(project, change.elementId))
      element->enclosingTypeId = change.after;
  for (const auto &records : m_diagrams) {
    if (auto *diagram = findDiagram(project, records.diagramId)) {
      for (auto item = records.connectors.crbegin();
           item != records.connectors.crend(); ++item)
        removeRecordedValue(diagram->connectors, item->index, item->value.id);
      for (auto item = records.nodes.crbegin(); item != records.nodes.crend();
           ++item)
        removeRecordedValue(diagram->nodes, item->index, item->value.id);
      applyMembershipChanges(*diagram, records.membershipChanges, true);
      if (records.containerOwnerChange)
        applyMembershipChanges(*diagram, {*records.containerOwnerChange}, true);
      if (records.container)
        removeRecordedValue(diagram->containers, records.container->index,
                            records.container->value.id);
    }
  }
  for (auto item = m_relationships.crbegin(); item != m_relationships.crend();
       ++item)
    removeRecordedValue(project.relationships, item->index, item->value.id);
  removeRecordedValue(project.elements, m_elementIndex, m_element.id);
  project.browserItemOrder.removeAll(
      browserItemKey(QStringLiteral("element"), m_element.id));
}

void DeleteElementCommand::revert(ProjectData &project) {
  insertAtRecordedPosition(project.elements, m_elementIndex, m_element);
  for (const auto &change : m_packageChanges)
    if (auto *element = findElement(project, change.elementId))
      element->packageId = change.before;
  for (const auto &change : m_enclosingTypeChanges)
    if (auto *element = findElement(project, change.elementId))
      element->enclosingTypeId = change.before;
  if (m_browserOrderIndex >= 0)
    project.browserItemOrder.insert(
        std::clamp(m_browserOrderIndex, qsizetype{0},
                   project.browserItemOrder.size()),
        browserItemKey(QStringLiteral("element"), m_element.id));
  for (const auto &change : m_browserParentChanges)
    if (BrowserParent *parent =
            browserParentFor(project, change.kind, change.id))
      *parent = change.before;
  for (const auto &item : m_relationships)
    insertAtRecordedPosition(project.relationships, item.index, item.value);
  for (const auto &records : m_diagrams) {
    if (auto *diagram = findDiagram(project, records.diagramId)) {
      if (records.container)
        insertAtRecordedPosition(diagram->containers, records.container->index,
                                 records.container->value);
      if (records.containerOwnerChange)
        applyMembershipChanges(*diagram, {*records.containerOwnerChange},
                               false);
      for (const auto &item : records.nodes)
        insertAtRecordedPosition(diagram->nodes, item.index, item.value);
      applyMembershipChanges(*diagram, records.membershipChanges, false);
      for (const auto &item : records.connectors)
        insertAtRecordedPosition(diagram->connectors, item.index, item.value);
    }
  }
}

UpdatePresentationGeometriesCommand::UpdatePresentationGeometriesCommand(
    ProjectController *controller, QString diagramId,
    QList<PresentationGeometryChange> changes,
    QList<ContainerChildrenChange> membershipChanges,
    QList<ElementPackageChange> packageChanges, QString description)
    : ProjectCommand(controller, description),
      m_diagramId(std::move(diagramId)), m_changes(std::move(changes)),
      m_membershipChanges(std::move(membershipChanges)),
      m_packageChanges(std::move(packageChanges)) {}

void UpdatePresentationGeometriesCommand::execute(ProjectData &project) {
  apply(project, true);
}

void UpdatePresentationGeometriesCommand::revert(ProjectData &project) {
  apply(project, false);
}

void UpdatePresentationGeometriesCommand::apply(ProjectData &project,
                                                bool forward) {
  auto *diagram = findDiagram(project, m_diagramId);
  if (!diagram)
    return;
  for (const auto &change : m_changes) {
    if (auto *node = findNode(*diagram, change.presentationId))
      node->geometry = forward ? change.after : change.before;
    else if (auto *container = findContainer(*diagram, change.presentationId))
      container->geometry = forward ? change.after : change.before;
  }
  applyMembershipChanges(*diagram, m_membershipChanges, forward);
  for (const auto &change : m_packageChanges)
    if (auto *element = findElement(project, change.elementId))
      element->packageId = forward ? change.after : change.before;
}

SetNodePortSnapPointsCommand::SetNodePortSnapPointsCommand(
    ProjectController *controller, QString diagramId,
    NodePortSnapPointChange change)
    : ProjectCommand(controller,
                     QStringLiteral("Change connector snap points")),
      m_diagramId(std::move(diagramId)), m_change(std::move(change)) {}

void SetNodePortSnapPointsCommand::execute(ProjectData &project) {
  apply(project, true);
}

void SetNodePortSnapPointsCommand::revert(ProjectData &project) {
  apply(project, false);
}

void SetNodePortSnapPointsCommand::apply(ProjectData &project, bool forward) {
  if (auto *diagram = findDiagram(project, m_diagramId))
    applyPortSnapPointChanges(*diagram, {m_change}, forward);
}

SetDiagramCompartmentVisibilityCommand::SetDiagramCompartmentVisibilityCommand(
    ProjectController *controller, QString diagramId,
    bool attributesCompartment, bool before, bool after)
    : ProjectCommand(
          controller,
          QStringLiteral("%1 %2").arg(
              after ? QStringLiteral("Show") : QStringLiteral("Hide"),
              attributesCompartment ? QStringLiteral("attributes")
                                    : QStringLiteral("operations"))),
      m_diagramId(std::move(diagramId)),
      m_attributesCompartment(attributesCompartment), m_before(before),
      m_after(after) {}

void SetDiagramCompartmentVisibilityCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void SetDiagramCompartmentVisibilityCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void SetDiagramCompartmentVisibilityCommand::apply(ProjectData &project,
                                                   bool value) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    if (m_attributesCompartment)
      diagram->showAttributes = value;
    else
      diagram->showOperations = value;
  }
}

SetDiagramFilterCommand::SetDiagramFilterCommand(ProjectController *controller,
                                                 QString diagramId,
                                                 DiagramFilter before,
                                                 DiagramFilter after)
    : ProjectCommand(controller, diagram_filter::isActive(after)
                                     ? QStringLiteral("Filter diagram")
                                     : QStringLiteral("Clear diagram filter")),
      m_diagramId(std::move(diagramId)), m_before(std::move(before)),
      m_after(std::move(after)) {}

void SetDiagramFilterCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void SetDiagramFilterCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void SetDiagramFilterCommand::apply(ProjectData &project,
                                    const DiagramFilter &filter) {
  if (auto *diagram = findDiagram(project, m_diagramId))
    diagram->filter = filter;
}

SetNodeCompartmentVisibilityCommand::SetNodeCompartmentVisibilityCommand(
    ProjectController *controller, bool attributesCompartment,
    QString diagramId, QList<NodeCompartmentVisibilityChange> changes)
    : ProjectCommand(controller, QStringLiteral("Set selected %1 visibility")
                                     .arg(attributesCompartment
                                              ? QStringLiteral("attributes")
                                              : QStringLiteral("operations"))),
      m_diagramId(std::move(diagramId)),
      m_attributesCompartment(attributesCompartment),
      m_changes(std::move(changes)) {}

void SetNodeCompartmentVisibilityCommand::execute(ProjectData &project) {
  apply(project, true);
}

void SetNodeCompartmentVisibilityCommand::revert(ProjectData &project) {
  apply(project, false);
}

void SetNodeCompartmentVisibilityCommand::apply(ProjectData &project,
                                                bool forward) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    for (const auto &change : m_changes) {
      auto *node = findNode(*diagram, change.nodeId);
      if (!node)
        continue;
      const std::optional<bool> value = forward ? change.after : change.before;
      if (m_attributesCompartment)
        node->showAttributes = value;
      else
        node->showOperations = value;
    }
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

SetConnectorsRoutingCommand::SetConnectorsRoutingCommand(
    ProjectController *controller, QString diagramId,
    QList<ConnectorRoutingChange> changes, ConnectorRouting after,
    bool selectionWide)
    : ProjectCommand(controller,
                     selectionWide
                         ? QStringLiteral("Use %1 routing for connectors")
                               .arg(toString(after))
                         : QStringLiteral("Use %1 connector routing")
                               .arg(toString(after))),
      m_diagramId(std::move(diagramId)), m_changes(std::move(changes)),
      m_after(after) {}

void SetConnectorsRoutingCommand::execute(ProjectData &project) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    for (const auto &change : m_changes)
      if (auto *connector = findConnector(*diagram, change.connectorId))
        connector->routing = m_after;
  }
}

void SetConnectorsRoutingCommand::revert(ProjectData &project) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    for (const auto &change : m_changes)
      if (auto *connector = findConnector(*diagram, change.connectorId))
        connector->routing = change.before;
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

UpdateConnectorAnnotationPlacementsCommand::
    UpdateConnectorAnnotationPlacementsCommand(
        ProjectController *controller, QString diagramId, QString connectorId,
        QHash<QString, ConnectorAnnotationPlacement> before,
        QHash<QString, ConnectorAnnotationPlacement> after,
        const QString &description)
    : ProjectCommand(controller, description),
      m_diagramId(std::move(diagramId)), m_connectorId(std::move(connectorId)),
      m_before(std::move(before)), m_after(std::move(after)) {}

void UpdateConnectorAnnotationPlacementsCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void UpdateConnectorAnnotationPlacementsCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void UpdateConnectorAnnotationPlacementsCommand::apply(
    ProjectData &project,
    const QHash<QString, ConnectorAnnotationPlacement> &placements) {
  if (auto *diagram = findDiagram(project, m_diagramId)) {
    if (auto *connector = findConnector(*diagram, m_connectorId))
      connector->annotationPlacements = placements;
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

EditRelationshipTextCommand::EditRelationshipTextCommand(
    ProjectController *controller, QString relationshipId,
    RelationshipTextProperty property, QString before, QString after,
    const QString &description)
    : ProjectCommand(controller, description),
      m_relationshipId(std::move(relationshipId)), m_property(property),
      m_before(std::move(before)), m_after(std::move(after)) {}

void EditRelationshipTextCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void EditRelationshipTextCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void EditRelationshipTextCommand::apply(ProjectData &project,
                                        const QString &value) {
  auto *relationship = findRelationship(project, m_relationshipId);
  if (!relationship)
    return;
  switch (m_property) {
  case RelationshipTextProperty::Name:
    relationship->name = value;
    break;
  case RelationshipTextProperty::SourceRole:
    relationship->sourceEnd.role = value;
    break;
  case RelationshipTextProperty::SourceMultiplicity:
    relationship->sourceEnd.multiplicity = value;
    break;
  case RelationshipTextProperty::TargetRole:
    relationship->targetEnd.role = value;
    break;
  case RelationshipTextProperty::TargetMultiplicity:
    relationship->targetEnd.multiplicity = value;
    break;
  }
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

SaveDiagramStyleCommand::SaveDiagramStyleCommand(ProjectController *controller,
                                                 const ProjectData &project,
                                                 DiagramStyle after)
    : ProjectCommand(controller, findDiagramStyle(project, after.id)
                                     ? QStringLiteral("Edit diagram style")
                                     : QStringLiteral("Create diagram style")),
      m_index(indexOfId(project.diagramStyles, after.id)),
      m_after(std::move(after)) {
  if (m_index >= 0)
    m_before = project.diagramStyles.at(m_index);
  else
    m_index = project.diagramStyles.size();
}

void SaveDiagramStyleCommand::execute(ProjectData &project) {
  if (m_before) {
    if (auto *style = findDiagramStyle(project, m_after.id))
      *style = m_after;
  } else {
    insertAtRecordedPosition(project.diagramStyles, m_index, m_after);
  }
}

void SaveDiagramStyleCommand::revert(ProjectData &project) {
  if (m_before) {
    if (auto *style = findDiagramStyle(project, m_before->id))
      *style = *m_before;
  } else {
    removeRecordedValue(project.diagramStyles, m_index, m_after.id);
  }
}

SetStyleAssignmentsCommand::SetStyleAssignmentsCommand(
    ProjectController *controller, QList<StyleAssignmentChange> changes,
    const QString &description)
    : ProjectCommand(controller, description), m_changes(std::move(changes)) {}

void SetStyleAssignmentsCommand::execute(ProjectData &project) {
  apply(project, true);
}

void SetStyleAssignmentsCommand::revert(ProjectData &project) {
  apply(project, false);
}

void SetStyleAssignmentsCommand::apply(ProjectData &project, bool forward) {
  for (const auto &change : m_changes)
    applyStyleAssignment(project, change,
                         forward ? change.after : change.before);
}

DeleteDiagramStyleCommand::DeleteDiagramStyleCommand(
    ProjectController *controller, const ProjectData &project, QString styleId)
    : ProjectCommand(controller, QStringLiteral("Delete diagram style")),
      m_index(indexOfId(project.diagramStyles, styleId)) {
  if (m_index < 0)
    return;
  m_style = project.diagramStyles.at(m_index);
  const auto record = [&](const QString &kind, const QString &diagramId,
                          const QString &subjectId,
                          const QString &assignedStyleId) {
    if (assignedStyleId == styleId)
      m_assignments.append({kind, diagramId, subjectId, styleId, QString{}});
  };
  for (const auto &element : project.elements)
    record(QStringLiteral("element"), {}, element.id, element.styleId);
  for (const auto &folder : project.browserFolders)
    record(QStringLiteral("folder"), {}, folder.id, folder.styleId);
  for (auto assignment = project.namespaceStyleIds.cbegin();
       assignment != project.namespaceStyleIds.cend(); ++assignment)
    record(QStringLiteral("namespace"), {}, assignment.key(),
           assignment.value());
  for (const auto &diagram : project.diagrams) {
    for (const auto &node : diagram.nodes)
      record(QStringLiteral("node"), diagram.id, node.id, node.styleId);
    for (const auto &container : diagram.containers)
      record(QStringLiteral("container"), diagram.id, container.id,
             container.styleId);
  }
}

void DeleteDiagramStyleCommand::execute(ProjectData &project) {
  if (m_index < 0)
    return;
  for (const auto &assignment : m_assignments)
    applyStyleAssignment(project, assignment, {});
  removeRecordedValue(project.diagramStyles, m_index, m_style.id);
}

void DeleteDiagramStyleCommand::revert(ProjectData &project) {
  if (m_index < 0)
    return;
  insertAtRecordedPosition(project.diagramStyles, m_index, m_style);
  for (const auto &assignment : m_assignments)
    applyStyleAssignment(project, assignment, assignment.before);
}

SetStereotypeAssignmentsCommand::SetStereotypeAssignmentsCommand(
    ProjectController *controller, QString kind, QString subjectId,
    QStringList before, QStringList after)
    : ProjectCommand(controller, QStringLiteral("Assign stereotypes")),
      m_kind(std::move(kind)), m_subjectId(std::move(subjectId)),
      m_before(std::move(before)), m_after(std::move(after)) {}

void SetStereotypeAssignmentsCommand::execute(ProjectData &project) {
  apply(project, m_after);
}

void SetStereotypeAssignmentsCommand::revert(ProjectData &project) {
  apply(project, m_before);
}

void SetStereotypeAssignmentsCommand::apply(ProjectData &project,
                                            const QStringList &stereotypeIds) {
  if (m_kind == QStringLiteral("element")) {
    if (auto *element = findElement(project, m_subjectId))
      element->stereotypeIds = stereotypeIds;
  } else if (m_kind == QStringLiteral("relationship")) {
    if (auto *relationship = findRelationship(project, m_subjectId))
      relationship->stereotypeIds = stereotypeIds;
  }
}

SaveStereotypeDefinitionCommand::SaveStereotypeDefinitionCommand(
    ProjectController *controller, const ProjectData &project,
    StereotypeDefinition after)
    : ProjectCommand(controller,
                     findStereotypeDefinition(project, after.id)
                         ? QStringLiteral("Edit project stereotype")
                         : QStringLiteral("Create project stereotype")),
      m_index(indexOfId(project.stereotypeDefinitions, after.id)),
      m_after(std::move(after)) {
  if (m_index >= 0)
    m_before = project.stereotypeDefinitions.at(m_index);
  else
    m_index = project.stereotypeDefinitions.size();
}

void SaveStereotypeDefinitionCommand::execute(ProjectData &project) {
  if (m_before) {
    if (auto *definition = findStereotypeDefinition(project, m_after.id))
      *definition = m_after;
  } else {
    insertAtRecordedPosition(project.stereotypeDefinitions, m_index, m_after);
  }
}

void SaveStereotypeDefinitionCommand::revert(ProjectData &project) {
  if (m_before) {
    if (auto *definition = findStereotypeDefinition(project, m_before->id))
      *definition = *m_before;
  } else {
    removeRecordedValue(project.stereotypeDefinitions, m_index, m_after.id);
  }
}

DeleteStereotypeDefinitionCommand::DeleteStereotypeDefinitionCommand(
    ProjectController *controller, const ProjectData &project,
    QString stereotypeId)
    : ProjectCommand(controller, QStringLiteral("Delete project stereotype")),
      m_index(indexOfId(project.stereotypeDefinitions, stereotypeId)) {
  if (m_index < 0)
    return;
  m_definition = project.stereotypeDefinitions.at(m_index);
  for (const auto &element : project.elements) {
    if (element.stereotypeIds.contains(stereotypeId))
      m_assignments.append(
          {QStringLiteral("element"), element.id, element.stereotypeIds});
  }
  for (const auto &relationship : project.relationships) {
    if (relationship.stereotypeIds.contains(stereotypeId))
      m_assignments.append({QStringLiteral("relationship"), relationship.id,
                            relationship.stereotypeIds});
  }
}

void DeleteStereotypeDefinitionCommand::execute(ProjectData &project) {
  if (m_index < 0)
    return;
  for (const auto &assignment : m_assignments) {
    QStringList after = assignment.stereotypeIds;
    after.removeAll(m_definition.id);
    if (assignment.kind == QStringLiteral("element")) {
      if (auto *element = findElement(project, assignment.subjectId))
        element->stereotypeIds = after;
    } else if (auto *relationship =
                   findRelationship(project, assignment.subjectId)) {
      relationship->stereotypeIds = after;
    }
  }
  removeRecordedValue(project.stereotypeDefinitions, m_index, m_definition.id);
}

void DeleteStereotypeDefinitionCommand::revert(ProjectData &project) {
  if (m_index < 0)
    return;
  insertAtRecordedPosition(project.stereotypeDefinitions, m_index,
                           m_definition);
  restoreAssignments(project);
}

void DeleteStereotypeDefinitionCommand::restoreAssignments(
    ProjectData &project) const {
  for (const auto &assignment : m_assignments) {
    if (assignment.kind == QStringLiteral("element")) {
      if (auto *element = findElement(project, assignment.subjectId))
        element->stereotypeIds = assignment.stereotypeIds;
    } else if (auto *relationship =
                   findRelationship(project, assignment.subjectId)) {
      relationship->stereotypeIds = assignment.stereotypeIds;
    }
  }
}

} // namespace yauml
