#pragma once

#include "core/project_command.h"
#include "core/project_data.h"

#include <QSet>
#include <optional>

namespace uuml {

struct ContainerChildrenChange {
  QString containerId;
  QStringList before;
  QStringList after;
};

struct PresentationGeometryChange {
  QString presentationId;
  QRectF before;
  QRectF after;
};

class CreateElementCommand final : public ProjectCommand {
public:
  CreateElementCommand(ProjectController *controller,
                       const ProjectData &project, ModelElement element,
                       QString diagramId,
                       std::optional<NodePresentation> nodePresentation,
                       std::optional<ContainerPresentation>
                           containerPresentation = std::nullopt);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  ModelElement m_element;
  qsizetype m_elementIndex;
  QString m_diagramId;
  std::optional<NodePresentation> m_nodePresentation;
  std::optional<ContainerPresentation> m_containerPresentation;
  qsizetype m_presentationIndex = -1;
};

class ApplyCppImportCommand final : public ProjectCommand {
public:
  ApplyCppImportCommand(ProjectController *controller,
                        const ProjectData &project,
                        QList<ModelElement> desiredElements,
                        QList<Relationship> desiredRelationships,
                        QStringList sourceRoots);

private:
  struct ElementChange {
    qsizetype index = -1;
    std::optional<ModelElement> before;
    ModelElement after;
  };
  struct RelationshipChange {
    qsizetype index = -1;
    std::optional<Relationship> before;
    Relationship after;
  };

  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  QList<ElementChange> m_changes;
  QList<RelationshipChange> m_relationshipChanges;
  QStringList m_sourceRootsBefore;
  QStringList m_sourceRootsAfter;
  bool m_sourceRootsChanged = false;
};

class CreateBrowserFolderCommand final : public ProjectCommand {
public:
  CreateBrowserFolderCommand(ProjectController *controller,
                             const ProjectData &project, BrowserFolder folder);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  BrowserFolder m_folder;
  qsizetype m_index;
};

class RenameBrowserFolderCommand final : public ProjectCommand {
public:
  RenameBrowserFolderCommand(ProjectController *controller, QString folderId,
                             QString before, QString after);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, const QString &name);

  QString m_folderId;
  QString m_before;
  QString m_after;
};

class MoveBrowserItemsCommand final : public ProjectCommand {
public:
  MoveBrowserItemsCommand(
      ProjectController *controller, const ProjectData &project,
      const QStringList &elementIds, const QStringList &folderIds,
      BrowserParent target,
      std::optional<QString> targetPackageId = std::nullopt,
      std::optional<QString> targetEnclosingTypeId = std::nullopt);

private:
  struct ParentChange {
    QString kind;
    QString id;
    BrowserParent before;
    BrowserParent after;
  };
  struct PackageChange {
    QString elementId;
    QString before;
    QString after;
  };
  struct EnclosingTypeChange {
    QString elementId;
    QString before;
    QString after;
  };

  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, bool forward);

  QList<ParentChange> m_changes;
  QList<PackageChange> m_packageChanges;
  QList<EnclosingTypeChange> m_enclosingTypeChanges;
};

class ReorderBrowserItemsCommand final : public ProjectCommand {
public:
  ReorderBrowserItemsCommand(ProjectController *controller, QStringList before,
                             QStringList after, int itemCount = 1);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  QStringList m_before;
  QStringList m_after;
};

class DeleteBrowserFolderCommand final : public ProjectCommand {
public:
  DeleteBrowserFolderCommand(ProjectController *controller,
                             const ProjectData &project, QString folderId);

private:
  struct ParentChange {
    QString kind;
    QString id;
    BrowserParent before;
    BrowserParent after;
  };
  struct DiagramContainer {
    QString diagramId;
    qsizetype index;
    ContainerPresentation value;
    std::optional<ContainerChildrenChange> ownerChange;
  };

  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void applyParentChanges(ProjectData &project, bool forward);

  BrowserFolder m_folder;
  qsizetype m_index = -1;
  qsizetype m_browserOrderIndex = -1;
  QList<ParentChange> m_changes;
  QList<DiagramContainer> m_diagramContainers;
};

class CreateDiagramCommand final : public ProjectCommand {
public:
  CreateDiagramCommand(ProjectController *controller,
                       const ProjectData &project, Diagram diagram);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  Diagram m_diagram;
  qsizetype m_index;
};

// Adding a presentation can materialize several semantic relationships at
// once. Existing endpoint nodes may therefore need denser snap-point sets in
// the same undoable command.
struct NodePortSnapPointChange {
  QString nodeId;
  int beforeHorizontal = 1;
  int beforeVertical = 1;
  int afterHorizontal = 1;
  int afterVertical = 1;
};

class AddElementToDiagramCommand final : public ProjectCommand {
public:
  AddElementToDiagramCommand(ProjectController *controller,
                             const ProjectData &project, QString diagramId,
                             NodePresentation presentation,
                             QList<ConnectorPresentation> connectors);

private:
  struct PositionedConnector {
    qsizetype index;
    ConnectorPresentation value;
  };

  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  QString m_diagramId;
  NodePresentation m_presentation;
  qsizetype m_index;
  QList<PositionedConnector> m_connectors;
};

// A tree drop is one user action even when it places hundreds of elements.
// Store only the inserted presentations and connector positions so undo stays
// proportional to the change instead of copying the complete project model.
class AddElementsToDiagramCommand final : public ProjectCommand {
public:
  AddElementsToDiagramCommand(ProjectController *controller,
                              const ProjectData &project, QString diagramId,
                              QList<NodePresentation> presentations,
                              QList<ConnectorPresentation> connectors,
                              QList<NodePortSnapPointChange> portChanges = {});

private:
  struct PositionedNode {
    qsizetype index;
    NodePresentation value;
  };
  struct PositionedConnector {
    qsizetype index;
    ConnectorPresentation value;
  };

  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  QString m_diagramId;
  QList<PositionedNode> m_presentations;
  QList<PositionedConnector> m_connectors;
  QList<NodePortSnapPointChange> m_portChanges;
};

// Adds a cohesive presentation slice—frames, leaves, connectors, membership,
// and any required enclosing-frame geometry—as one compact undo transaction.
// It is shared by tree drops and diagram-side wrapping actions.
class AddContainerPresentationsCommand final : public ProjectCommand {
public:
  AddContainerPresentationsCommand(
      ProjectController *controller, const ProjectData &project,
      QString diagramId, QList<ContainerPresentation> containers,
      QList<NodePresentation> nodes, QList<ConnectorPresentation> connectors,
      QList<ContainerChildrenChange> membershipChanges,
      QList<NodePortSnapPointChange> portChanges = {},
      QList<PresentationGeometryChange> geometryChanges = {},
      QString description =
          QStringLiteral("Add project-tree items to diagram"));

private:
  struct PositionedContainer {
    qsizetype index;
    ContainerPresentation value;
  };
  struct PositionedNode {
    qsizetype index;
    NodePresentation value;
  };
  struct PositionedConnector {
    qsizetype index;
    ConnectorPresentation value;
  };

  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  QString m_diagramId;
  QList<PositionedContainer> m_containers;
  QList<PositionedNode> m_nodes;
  QList<PositionedConnector> m_connectors;
  QList<ContainerChildrenChange> m_membershipChanges;
  QList<NodePortSnapPointChange> m_portChanges;
  QList<PresentationGeometryChange> m_geometryChanges;
};

class RemovePresentationsCommand final : public ProjectCommand {
public:
  RemovePresentationsCommand(ProjectController *controller,
                             const ProjectData &project, QString diagramId,
                             const QSet<QString> &nodeIds);

private:
  struct PositionedNode {
    qsizetype index;
    NodePresentation value;
  };
  struct PositionedConnector {
    qsizetype index;
    ConnectorPresentation value;
  };

  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  QString m_diagramId;
  QList<PositionedNode> m_nodes;
  QList<PositionedConnector> m_connectors;
  QList<ContainerChildrenChange> m_membershipChanges;
};

class RemoveContainerPresentationCommand final : public ProjectCommand {
public:
  RemoveContainerPresentationCommand(ProjectController *controller,
                                     const ProjectData &project,
                                     QString diagramId, QString containerId);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  QString m_diagramId;
  ContainerPresentation m_container;
  qsizetype m_index = -1;
  std::optional<ContainerChildrenChange> m_ownerChange;
};

class DeleteDiagramCommand final : public ProjectCommand {
public:
  DeleteDiagramCommand(ProjectController *controller,
                       const ProjectData &project, QString diagramId);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  Diagram m_diagram;
  qsizetype m_index;
};

class DeleteRelationshipCommand final : public ProjectCommand {
public:
  DeleteRelationshipCommand(ProjectController *controller,
                            const ProjectData &project, QString relationshipId);

private:
  struct PositionedConnector {
    qsizetype index;
    ConnectorPresentation value;
  };
  struct DiagramConnectors {
    QString diagramId;
    QList<PositionedConnector> connectors;
  };

  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  Relationship m_relationship;
  qsizetype m_relationshipIndex;
  QList<DiagramConnectors> m_diagrams;
};

class DeleteElementCommand final : public ProjectCommand {
public:
  DeleteElementCommand(ProjectController *controller,
                       const ProjectData &project, QString elementId);

private:
  struct PositionedRelationship {
    qsizetype index;
    Relationship value;
  };
  struct PositionedNode {
    qsizetype index;
    NodePresentation value;
  };
  struct PositionedContainer {
    qsizetype index;
    ContainerPresentation value;
  };
  struct PositionedConnector {
    qsizetype index;
    ConnectorPresentation value;
  };
  struct DiagramRecords {
    QString diagramId;
    QList<PositionedNode> nodes;
    QList<PositionedConnector> connectors;
    QList<ContainerChildrenChange> membershipChanges;
    std::optional<PositionedContainer> container;
    std::optional<ContainerChildrenChange> containerOwnerChange;
  };
  struct BrowserParentChange {
    QString kind;
    QString id;
    BrowserParent before;
    BrowserParent after;
  };
  struct PackageChange {
    QString elementId;
    QString before;
    QString after;
  };
  struct EnclosingTypeChange {
    QString elementId;
    QString before;
    QString after;
  };

  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  ModelElement m_element;
  qsizetype m_elementIndex;
  qsizetype m_browserOrderIndex = -1;
  QList<PositionedRelationship> m_relationships;
  QList<DiagramRecords> m_diagrams;
  QList<BrowserParentChange> m_browserParentChanges;
  QList<PackageChange> m_packageChanges;
  QList<EnclosingTypeChange> m_enclosingTypeChanges;
};

struct ElementPackageChange {
  QString elementId;
  QString before;
  QString after;
};

class UpdatePresentationGeometriesCommand final : public ProjectCommand {
public:
  UpdatePresentationGeometriesCommand(
      ProjectController *controller, QString diagramId,
      QList<PresentationGeometryChange> changes,
      QList<ContainerChildrenChange> membershipChanges,
      QList<ElementPackageChange> packageChanges, QString description);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, bool forward);

  QString m_diagramId;
  QList<PresentationGeometryChange> m_changes;
  QList<ContainerChildrenChange> m_membershipChanges;
  QList<ElementPackageChange> m_packageChanges;
};

class SetNodePortSnapPointsCommand final : public ProjectCommand {
public:
  SetNodePortSnapPointsCommand(ProjectController *controller, QString diagramId,
                               QString nodeId, int beforeHorizontal,
                               int beforeVertical, int afterHorizontal,
                               int afterVertical);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, int horizontal, int vertical);

  QString m_diagramId;
  QString m_nodeId;
  int m_beforeHorizontal;
  int m_beforeVertical;
  int m_afterHorizontal;
  int m_afterVertical;
};

class SetDiagramCompartmentVisibilityCommand final : public ProjectCommand {
public:
  SetDiagramCompartmentVisibilityCommand(ProjectController *controller,
                                         QString diagramId,
                                         bool attributesCompartment,
                                         bool before, bool after);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, bool value);

  QString m_diagramId;
  bool m_attributesCompartment;
  bool m_before;
  bool m_after;
};

struct NodeCompartmentVisibilityChange {
  QString nodeId;
  std::optional<bool> before;
  std::optional<bool> after;
};

class SetNodeCompartmentVisibilityCommand final : public ProjectCommand {
public:
  SetNodeCompartmentVisibilityCommand(ProjectController *controller,
                                      bool attributesCompartment,
                                      QString diagramId,
                                      QList<NodeCompartmentVisibilityChange>
                                          changes);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, bool forward);

  QString m_diagramId;
  bool m_attributesCompartment;
  QList<NodeCompartmentVisibilityChange> m_changes;
};

class CreateRelationshipCommand final : public ProjectCommand {
public:
  CreateRelationshipCommand(ProjectController *controller,
                            const ProjectData &project, QString diagramId,
                            Relationship relationship,
                            ConnectorPresentation connector);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  QString m_diagramId;
  Relationship m_relationship;
  ConnectorPresentation m_connector;
  qsizetype m_relationshipIndex;
  qsizetype m_connectorIndex;
};

class ReconnectRelationshipCommand final : public ProjectCommand {
public:
  ReconnectRelationshipCommand(ProjectController *controller, QString diagramId,
                               QString connectorId, QString relationshipId,
                               bool reconnectSource, QString beforeElementId,
                               QString afterElementId,
                               ConnectorAnchor beforeAnchor,
                               ConnectorAnchor afterAnchor,
                               QList<ConnectorBendPoint> beforeBendPoints,
                               QList<ConnectorBendPoint> afterBendPoints);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, bool forward);

  QString m_diagramId;
  QString m_connectorId;
  QString m_relationshipId;
  bool m_reconnectSource;
  QString m_beforeElementId;
  QString m_afterElementId;
  ConnectorAnchor m_beforeAnchor;
  ConnectorAnchor m_afterAnchor;
  QList<ConnectorBendPoint> m_beforeBendPoints;
  QList<ConnectorBendPoint> m_afterBendPoints;
};

class MoveConnectorAnchorCommand final : public ProjectCommand {
public:
  MoveConnectorAnchorCommand(ProjectController *controller, QString diagramId,
                             QString connectorId, bool source,
                             ConnectorAnchor before, ConnectorAnchor after);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, const ConnectorAnchor &anchor);

  QString m_diagramId;
  QString m_connectorId;
  bool m_source;
  ConnectorAnchor m_before;
  ConnectorAnchor m_after;
};

class SetConnectorRoutingCommand final : public ProjectCommand {
public:
  SetConnectorRoutingCommand(ProjectController *controller, QString diagramId,
                             QString connectorId, ConnectorRouting before,
                             ConnectorRouting after);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, ConnectorRouting routing);

  QString m_diagramId;
  QString m_connectorId;
  ConnectorRouting m_before;
  ConnectorRouting m_after;
};

class UpdateConnectorBendPointsCommand final : public ProjectCommand {
public:
  UpdateConnectorBendPointsCommand(ProjectController *controller,
                                   QString diagramId, QString connectorId,
                                   QList<ConnectorBendPoint> before,
                                   QList<ConnectorBendPoint> after,
                                   const QString &description);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, const QList<ConnectorBendPoint> &bendPoints);

  QString m_diagramId;
  QString m_connectorId;
  QList<ConnectorBendPoint> m_before;
  QList<ConnectorBendPoint> m_after;
};

class UpdateConnectorAnnotationPlacementsCommand final : public ProjectCommand {
public:
  UpdateConnectorAnnotationPlacementsCommand(
      ProjectController *controller, QString diagramId, QString connectorId,
      QHash<QString, ConnectorAnnotationPlacement> before,
      QHash<QString, ConnectorAnnotationPlacement> after,
      const QString &description);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project,
             const QHash<QString, ConnectorAnnotationPlacement> &placements);

  QString m_diagramId;
  QString m_connectorId;
  QHash<QString, ConnectorAnnotationPlacement> m_before;
  QHash<QString, ConnectorAnnotationPlacement> m_after;
};

enum class ElementTextProperty { Name, Attribute, Operation, Literal };

class EditElementTextCommand final : public ProjectCommand {
public:
  EditElementTextCommand(ProjectController *controller, QString elementId,
                         ElementTextProperty property, int index,
                         QString before, QString after,
                         const QString &description);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, const QString &value);

  QString m_elementId;
  ElementTextProperty m_property;
  int m_index;
  QString m_before;
  QString m_after;
};

enum class ElementListProperty { Attributes, Operations, Literals };

class SetElementListCommand final : public ProjectCommand {
public:
  SetElementListCommand(ProjectController *controller, QString elementId,
                        ElementListProperty property, QStringList before,
                        QStringList after, const QString &description);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, const QStringList &value);

  QString m_elementId;
  ElementListProperty m_property;
  QStringList m_before;
  QStringList m_after;
};

enum class RelationshipTextProperty {
  Name,
  SourceRole,
  SourceMultiplicity,
  TargetRole,
  TargetMultiplicity
};

class EditRelationshipTextCommand final : public ProjectCommand {
public:
  EditRelationshipTextCommand(ProjectController *controller,
                              QString relationshipId,
                              RelationshipTextProperty property, QString before,
                              QString after, const QString &description);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, const QString &value);

  QString m_relationshipId;
  RelationshipTextProperty m_property;
  QString m_before;
  QString m_after;
};

class RenameDiagramCommand final : public ProjectCommand {
public:
  RenameDiagramCommand(ProjectController *controller, QString diagramId,
                       QString before, QString after);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, const QString &value);

  QString m_diagramId;
  QString m_before;
  QString m_after;
};

struct StyleAssignmentChange {
  QString kind;
  QString diagramId;
  QString subjectId;
  QString before;
  QString after;
};

class SaveDiagramStyleCommand final : public ProjectCommand {
public:
  SaveDiagramStyleCommand(ProjectController *controller,
                          const ProjectData &project, DiagramStyle after);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  qsizetype m_index = -1;
  std::optional<DiagramStyle> m_before;
  DiagramStyle m_after;
};

class SetStyleAssignmentsCommand final : public ProjectCommand {
public:
  SetStyleAssignmentsCommand(ProjectController *controller,
                             QList<StyleAssignmentChange> changes,
                             const QString &description);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, bool forward);

  QList<StyleAssignmentChange> m_changes;
};

class DeleteDiagramStyleCommand final : public ProjectCommand {
public:
  DeleteDiagramStyleCommand(ProjectController *controller,
                            const ProjectData &project, QString styleId);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  qsizetype m_index = -1;
  DiagramStyle m_style;
  QList<StyleAssignmentChange> m_assignments;
};

class SetStereotypeAssignmentsCommand final : public ProjectCommand {
public:
  SetStereotypeAssignmentsCommand(ProjectController *controller, QString kind,
                                  QString subjectId, QStringList before,
                                  QStringList after);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, const QStringList &stereotypeIds);

  QString m_kind;
  QString m_subjectId;
  QStringList m_before;
  QStringList m_after;
};

class SaveStereotypeDefinitionCommand final : public ProjectCommand {
public:
  SaveStereotypeDefinitionCommand(ProjectController *controller,
                                  const ProjectData &project,
                                  StereotypeDefinition after);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  qsizetype m_index = -1;
  std::optional<StereotypeDefinition> m_before;
  StereotypeDefinition m_after;
};

struct StereotypeAssignmentSnapshot {
  QString kind;
  QString subjectId;
  QStringList stereotypeIds;
};

class DeleteStereotypeDefinitionCommand final : public ProjectCommand {
public:
  DeleteStereotypeDefinitionCommand(ProjectController *controller,
                                    const ProjectData &project,
                                    QString stereotypeId);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void restoreAssignments(ProjectData &project) const;

  qsizetype m_index = -1;
  StereotypeDefinition m_definition;
  QList<StereotypeAssignmentSnapshot> m_assignments;
};

} // namespace uuml
