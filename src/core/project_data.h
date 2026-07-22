#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

namespace uuml {

enum class ElementType { Package, Class, Struct, Enumeration };
enum class RelationshipType {
  Dependency,
  Generalization,
  Realization,
  Association,
  Aggregation,
  Composition
};
enum class ConnectorSide { Automatic, Top, Right, Bottom, Left };
enum class ConnectorRouting { Straight, Orthogonal };

QString toString(ElementType type);
ElementType elementTypeFromString(const QString &value, bool *ok = nullptr);
QString toString(RelationshipType type);
RelationshipType relationshipTypeFromString(const QString &value,
                                            bool *ok = nullptr);
QString toString(ConnectorSide side);
ConnectorSide connectorSideFromString(const QString &value, bool *ok = nullptr);
QString toString(ConnectorRouting routing);
ConnectorRouting connectorRoutingFromString(const QString &value,
                                            bool *ok = nullptr);
QString newId();

struct ModelElement {
  QString id;
  ElementType type = ElementType::Class;
  QString name;
  QString packageId;
  QStringList attributes;
  QStringList operations;
  QStringList enumLiterals;
  QJsonObject extra;

  bool operator==(const ModelElement &) const = default;
};

struct Relationship {
  QString id;
  RelationshipType type = RelationshipType::Dependency;
  QString name;
  QString sourceId;
  QString targetId;
  QJsonObject extra;

  bool operator==(const Relationship &) const = default;
};

struct NodePresentation {
  QString id;
  QString elementId;
  QRectF geometry;
  QJsonObject extra;

  bool operator==(const NodePresentation &) const = default;
};

struct ConnectorAnchor {
  ConnectorSide side = ConnectorSide::Automatic;
  qreal offset = 0.5;
  QJsonObject extra;

  bool operator==(const ConnectorAnchor &) const = default;
};

struct ConnectorBendPoint {
  QPointF position;
  QJsonObject extra;

  bool operator==(const ConnectorBendPoint &) const = default;
};

struct ConnectorPresentation {
  QString id;
  QString relationshipId;
  ConnectorRouting routing = ConnectorRouting::Straight;
  ConnectorAnchor sourceAnchor;
  ConnectorAnchor targetAnchor;
  QList<ConnectorBendPoint> bendPoints;
  QJsonObject extra;

  bool operator==(const ConnectorPresentation &) const = default;
};

struct Diagram {
  QString id;
  QString name;
  QList<NodePresentation> nodes;
  QList<ConnectorPresentation> connectors;
  QJsonObject extra;

  bool operator==(const Diagram &) const = default;
};

struct ProjectData {
  int schemaVersion = 1;
  QString id;
  QString name;
  QList<ModelElement> elements;
  QList<Relationship> relationships;
  QList<Diagram> diagrams;
  QJsonObject manifestExtra;
  QJsonObject modelExtra;
  QJsonObject diagramsExtra;
  bool loadedFromCommentedJson5 = false;

  bool operator==(const ProjectData &) const = default;
};

ProjectData
createStarterProject(const QString &name = QStringLiteral("New Project"));

ModelElement *findElement(ProjectData &project, const QString &id);
const ModelElement *findElement(const ProjectData &project, const QString &id);
Relationship *findRelationship(ProjectData &project, const QString &id);
const Relationship *findRelationship(const ProjectData &project,
                                     const QString &id);
Diagram *findDiagram(ProjectData &project, const QString &id);
const Diagram *findDiagram(const ProjectData &project, const QString &id);
NodePresentation *findNode(Diagram &diagram, const QString &id);
const NodePresentation *findNode(const Diagram &diagram, const QString &id);
ConnectorPresentation *findConnector(Diagram &diagram, const QString &id);
const ConnectorPresentation *findConnector(const Diagram &diagram,
                                           const QString &id);

} // namespace uuml

Q_DECLARE_METATYPE(uuml::ElementType)
Q_DECLARE_METATYPE(uuml::RelationshipType)
Q_DECLARE_METATYPE(uuml::ConnectorSide)
Q_DECLARE_METATYPE(uuml::ConnectorRouting)
