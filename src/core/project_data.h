#pragma once

#include <QHash>
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
  Composition,
  Containment
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

// Browser organization is deliberately separate from UML/C++ ownership. An
// empty kind means "use semantic hierarchy"; otherwise the item is explicitly
// placed under the named browser container.
struct BrowserParent {
  QString kind;
  QString id;

  bool operator==(const BrowserParent &) const = default;
};

// Named, project-owned diagram styles use generic roles that apply to both
// classifier nodes and package/folder frames. Values are normalized serialized
// QColor strings (#RRGGBB or #AARRGGBB).
struct DiagramStyle {
  QString id;
  QString name;
  QString fill;
  QString headerFill;
  QString border;
  QString primaryText;
  QString secondaryText;
  QString divider;
  QJsonObject extra;

  bool operator==(const DiagramStyle &) const = default;
};

struct ModelElement {
  QString id;
  ElementType type = ElementType::Class;
  QString name;
  QString packageId;
  // Stable semantic ownership for a type declared inside another class or
  // struct. packageId continues to identify the enclosing UML package.
  QString enclosingTypeId;
  QStringList attributes;
  QStringList operations;
  QStringList enumLiterals;
  BrowserParent browserParent;
  QString styleId;
  QJsonObject extra;

  bool operator==(const ModelElement &) const = default;
};

struct BrowserFolder {
  QString id;
  QString name;
  BrowserParent parent;
  QString styleId;
  QJsonObject extra;

  bool operator==(const BrowserFolder &) const = default;
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
  // Counts apply to side pairs: top/bottom share the horizontal value and
  // left/right share the vertical value. Odd values keep a center snap point.
  int horizontalPortSnapPoints = 1;
  int verticalPortSnapPoints = 1;
  QString styleId;
  QJsonObject extra;

  bool operator==(const NodePresentation &) const = default;
};

// Diagram containers are presentation-only views of a project-browser
// subject. Membership is explicit so moving a frame is deterministic and does
// not depend on incidental rectangle overlap.
struct ContainerPresentation {
  QString id;
  QString subjectKind;
  QString subjectId;
  QRectF geometry;
  QStringList childPresentationIds;
  QString styleId;
  QJsonObject extra;

  bool operator==(const ContainerPresentation &) const = default;
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
  QList<ContainerPresentation> containers;
  QList<NodePresentation> nodes;
  QList<ConnectorPresentation> connectors;
  QJsonObject extra;

  bool operator==(const Diagram &) const = default;
};

// Project-scoped source synchronization settings. Keeping this in the
// manifest (rather than application preferences) lets each model remember its
// own source tree and leaves room for future per-project sync policy.
struct CppImportConfiguration {
  QString sourceRoot;
  QJsonObject extra;

  bool operator==(const CppImportConfiguration &) const = default;
};

struct ProjectData {
  int schemaVersion = 1;
  QString id;
  QString name;
  CppImportConfiguration cppImport;
  QList<DiagramStyle> diagramStyles;
  QList<ModelElement> elements;
  QList<BrowserFolder> browserFolders;
  // Legacy/synthetic namespace tree nodes do not have a stable ModelElement.
  // Their qualified path is therefore the durable assignment key.
  QHash<QString, QString> namespaceStyleIds;
  // Stable cross-type ordering for explicit project-browser items. Entries
  // use "element:<id>" and "folder:<id>"; missing entries retain their
  // natural creation order for backward-compatible project files.
  QStringList browserItemOrder;
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
DiagramStyle *findDiagramStyle(ProjectData &project, const QString &id);
const DiagramStyle *findDiagramStyle(const ProjectData &project,
                                     const QString &id);
BrowserFolder *findBrowserFolder(ProjectData &project, const QString &id);
const BrowserFolder *findBrowserFolder(const ProjectData &project,
                                       const QString &id);
Relationship *findRelationship(ProjectData &project, const QString &id);
const Relationship *findRelationship(const ProjectData &project,
                                     const QString &id);
Diagram *findDiagram(ProjectData &project, const QString &id);
const Diagram *findDiagram(const ProjectData &project, const QString &id);
NodePresentation *findNode(Diagram &diagram, const QString &id);
const NodePresentation *findNode(const Diagram &diagram, const QString &id);
ContainerPresentation *findContainer(Diagram &diagram, const QString &id);
const ContainerPresentation *findContainer(const Diagram &diagram,
                                           const QString &id);
ConnectorPresentation *findConnector(Diagram &diagram, const QString &id);
const ConnectorPresentation *findConnector(const Diagram &diagram,
                                           const QString &id);

} // namespace uuml

Q_DECLARE_METATYPE(uuml::ElementType)
Q_DECLARE_METATYPE(uuml::RelationshipType)
Q_DECLARE_METATYPE(uuml::ConnectorSide)
Q_DECLARE_METATYPE(uuml::ConnectorRouting)
