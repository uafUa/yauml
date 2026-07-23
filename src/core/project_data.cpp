#include "core/project_data.h"

#include <QUuid>

namespace uuml {

QString toString(ElementType type) {
  switch (type) {
  case ElementType::Package:
    return QStringLiteral("package");
  case ElementType::Class:
    return QStringLiteral("class");
  case ElementType::Struct:
    return QStringLiteral("struct");
  case ElementType::Enumeration:
    return QStringLiteral("enumeration");
  }
  return QStringLiteral("class");
}

ElementType elementTypeFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("package"))
    return ElementType::Package;
  if (value == QStringLiteral("class"))
    return ElementType::Class;
  if (value == QStringLiteral("struct"))
    return ElementType::Struct;
  if (value == QStringLiteral("enumeration"))
    return ElementType::Enumeration;
  if (ok)
    *ok = false;
  return ElementType::Class;
}

QString toString(RelationshipType type) {
  switch (type) {
  case RelationshipType::Dependency:
    return QStringLiteral("dependency");
  case RelationshipType::Generalization:
    return QStringLiteral("generalization");
  case RelationshipType::Realization:
    return QStringLiteral("realization");
  case RelationshipType::Association:
    return QStringLiteral("association");
  case RelationshipType::Aggregation:
    return QStringLiteral("aggregation");
  case RelationshipType::Composition:
    return QStringLiteral("composition");
  case RelationshipType::Containment:
    return QStringLiteral("containment");
  }
  return QStringLiteral("dependency");
}

RelationshipType relationshipTypeFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("dependency"))
    return RelationshipType::Dependency;
  if (value == QStringLiteral("generalization"))
    return RelationshipType::Generalization;
  if (value == QStringLiteral("realization"))
    return RelationshipType::Realization;
  if (value == QStringLiteral("association"))
    return RelationshipType::Association;
  if (value == QStringLiteral("aggregation"))
    return RelationshipType::Aggregation;
  if (value == QStringLiteral("composition"))
    return RelationshipType::Composition;
  if (value == QStringLiteral("containment"))
    return RelationshipType::Containment;
  if (ok)
    *ok = false;
  return RelationshipType::Dependency;
}

QString toString(ConnectorSide side) {
  switch (side) {
  case ConnectorSide::Automatic:
    return QStringLiteral("automatic");
  case ConnectorSide::Top:
    return QStringLiteral("top");
  case ConnectorSide::Right:
    return QStringLiteral("right");
  case ConnectorSide::Bottom:
    return QStringLiteral("bottom");
  case ConnectorSide::Left:
    return QStringLiteral("left");
  }
  return QStringLiteral("automatic");
}

ConnectorSide connectorSideFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("automatic"))
    return ConnectorSide::Automatic;
  if (value == QStringLiteral("top"))
    return ConnectorSide::Top;
  if (value == QStringLiteral("right"))
    return ConnectorSide::Right;
  if (value == QStringLiteral("bottom"))
    return ConnectorSide::Bottom;
  if (value == QStringLiteral("left"))
    return ConnectorSide::Left;
  if (ok)
    *ok = false;
  return ConnectorSide::Automatic;
}

QString toString(ConnectorRouting routing) {
  switch (routing) {
  case ConnectorRouting::Straight:
    return QStringLiteral("straight");
  case ConnectorRouting::Orthogonal:
    return QStringLiteral("orthogonal");
  }
  return QStringLiteral("straight");
}

ConnectorRouting connectorRoutingFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("straight"))
    return ConnectorRouting::Straight;
  if (value == QStringLiteral("orthogonal"))
    return ConnectorRouting::Orthogonal;
  if (ok)
    *ok = false;
  return ConnectorRouting::Straight;
}

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

ProjectData createStarterProject(const QString &name) {
  ProjectData project;
  project.id = newId();
  project.name = name;

  Diagram diagram;
  diagram.id = newId();
  diagram.name = QStringLiteral("Class Diagram 1");
  project.diagrams.append(diagram);
  return project;
}

template <typename T> static T *findById(QList<T> &items, const QString &id) {
  for (auto &item : items) {
    if (item.id == id)
      return &item;
  }
  return nullptr;
}

template <typename T>
static const T *findById(const QList<T> &items, const QString &id) {
  for (const auto &item : items) {
    if (item.id == id)
      return &item;
  }
  return nullptr;
}

ModelElement *findElement(ProjectData &project, const QString &id) {
  return findById(project.elements, id);
}

const ModelElement *findElement(const ProjectData &project, const QString &id) {
  return findById(project.elements, id);
}

DiagramStyle *findDiagramStyle(ProjectData &project, const QString &id) {
  return findById(project.diagramStyles, id);
}

const DiagramStyle *findDiagramStyle(const ProjectData &project,
                                     const QString &id) {
  return findById(project.diagramStyles, id);
}

BrowserFolder *findBrowserFolder(ProjectData &project, const QString &id) {
  return findById(project.browserFolders, id);
}

const BrowserFolder *findBrowserFolder(const ProjectData &project,
                                       const QString &id) {
  return findById(project.browserFolders, id);
}

Relationship *findRelationship(ProjectData &project, const QString &id) {
  return findById(project.relationships, id);
}

const Relationship *findRelationship(const ProjectData &project,
                                     const QString &id) {
  return findById(project.relationships, id);
}

Diagram *findDiagram(ProjectData &project, const QString &id) {
  return findById(project.diagrams, id);
}

const Diagram *findDiagram(const ProjectData &project, const QString &id) {
  return findById(project.diagrams, id);
}

NodePresentation *findNode(Diagram &diagram, const QString &id) {
  return findById(diagram.nodes, id);
}

const NodePresentation *findNode(const Diagram &diagram, const QString &id) {
  return findById(diagram.nodes, id);
}

ContainerPresentation *findContainer(Diagram &diagram, const QString &id) {
  return findById(diagram.containers, id);
}

const ContainerPresentation *findContainer(const Diagram &diagram,
                                           const QString &id) {
  return findById(diagram.containers, id);
}

ConnectorPresentation *findConnector(Diagram &diagram, const QString &id) {
  return findById(diagram.connectors, id);
}

const ConnectorPresentation *findConnector(const Diagram &diagram,
                                           const QString &id) {
  return findById(diagram.connectors, id);
}

} // namespace uuml
