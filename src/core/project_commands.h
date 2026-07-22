#pragma once

#include "core/project_command.h"
#include "core/project_data.h"

#include <QSet>
#include <optional>

namespace uuml {

class CreateElementCommand final : public ProjectCommand {
public:
  CreateElementCommand(ProjectController *controller,
                       const ProjectData &project, ModelElement element,
                       QString diagramId,
                       std::optional<NodePresentation> presentation);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  ModelElement m_element;
  qsizetype m_elementIndex;
  QString m_diagramId;
  std::optional<NodePresentation> m_presentation;
  qsizetype m_presentationIndex = -1;
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
  struct PositionedConnector {
    qsizetype index;
    ConnectorPresentation value;
  };
  struct DiagramRecords {
    QString diagramId;
    QList<PositionedNode> nodes;
    QList<PositionedConnector> connectors;
  };

  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;

  ModelElement m_element;
  qsizetype m_elementIndex;
  QList<PositionedRelationship> m_relationships;
  QList<DiagramRecords> m_diagrams;
};

struct NodeGeometryChange {
  QString nodeId;
  QRectF before;
  QRectF after;
};

class UpdateNodeGeometriesCommand final : public ProjectCommand {
public:
  UpdateNodeGeometriesCommand(ProjectController *controller, QString diagramId,
                              QList<NodeGeometryChange> changes,
                              QString description);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, bool forward);

  QString m_diagramId;
  QList<NodeGeometryChange> m_changes;
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

class RenameRelationshipCommand final : public ProjectCommand {
public:
  RenameRelationshipCommand(ProjectController *controller,
                            QString relationshipId, QString before,
                            QString after);

private:
  void execute(ProjectData &project) override;
  void revert(ProjectData &project) override;
  void apply(ProjectData &project, const QString &value);

  QString m_relationshipId;
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

} // namespace uuml
