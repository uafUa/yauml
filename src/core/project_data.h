#pragma once

#include "core/project_schema_version.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <optional>

namespace yauml {

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
enum class MemberVisibility { Public, Protected, Private, Package };
enum class OperationKind { Method, Constructor, Destructor };
enum class OperationSignatureMode { Full, NameAndReturnType, NameOnly };

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
QString toString(MemberVisibility visibility);
MemberVisibility memberVisibilityFromString(const QString &value,
                                            bool *ok = nullptr);
QString toString(OperationKind kind);
OperationKind operationKindFromString(const QString &value, bool *ok = nullptr);
QString toString(OperationSignatureMode mode);
OperationSignatureMode operationSignatureModeFromString(const QString &value,
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

// Every stereotype definition belongs to the project. New projects begin with
// conventional UML entries, but users can edit or delete those defaults.
// Assignments use stable IDs so renaming does not rewrite every subject.
struct StereotypeDefinition {
  QString id;
  QString name;
  QStringList applicableTo;
  QJsonObject extra;

  bool operator==(const StereotypeDefinition &) const = default;
};

struct OperationParameter {
  QString name;
  QString type;
  QString direction;
  QString defaultValue;
  QJsonObject extra;

  bool operator==(const OperationParameter &) const = default;
};

// Operations are semantic model members rather than preformatted diagram
// lines. Stable IDs and source locations let synchronization and editor
// navigation address one operation without replacing the classifier's whole
// operation list. customSignature is used only as a lossless fallback for
// legacy or hand-authored syntax which the structured parser cannot reproduce.
struct ModelOperation {
  QString id;
  QString name;
  MemberVisibility visibility = MemberVisibility::Public;
  OperationKind kind = OperationKind::Method;
  QList<OperationParameter> parameters;
  QString returnType;
  QStringList modifiers;
  QString sourceFile;
  int sourceLine = 0;
  int sourceColumn = 0;
  QJsonObject sourceExtra;
  QString customSignature;
  QJsonObject extra;

  bool operator==(const ModelOperation &) const = default;
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
  QList<ModelOperation> operations;
  QStringList enumLiterals;
  BrowserParent browserParent;
  QString styleId;
  QStringList stereotypeIds;
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

// Role and multiplicity belong to the semantic relationship end, so every
// diagram presenting the same relationship shows the same UML meaning.
// Presentation-specific annotation positions are intentionally stored on the
// connector presentation instead.
struct RelationshipEnd {
  QString role;
  QString multiplicity;
  QJsonObject extra;

  bool operator==(const RelationshipEnd &) const = default;
};

struct Relationship {
  QString id;
  RelationshipType type = RelationshipType::Dependency;
  QString name;
  QString sourceId;
  QString targetId;
  RelationshipEnd sourceEnd;
  RelationshipEnd targetEnd;
  QStringList stereotypeIds;
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
  // An unset value inherits the diagram-level compartment setting. Keeping
  // inheritance explicit avoids copying defaults into every presentation and
  // lets a later diagram change continue to affect non-overridden nodes.
  std::optional<bool> showAttributes;
  std::optional<bool> showOperations;
  // An unset signature mode follows the diagram default independently of
  // whether the operations compartment itself is shown.
  std::optional<OperationSignatureMode> operationSignatureMode;
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

// Manual connector annotations remain attached to a route as its geometry
// changes. routePosition is normalized from source (0) to target (1); the two
// offsets are measured in scene units along the local tangent and normal.
struct ConnectorAnnotationPlacement {
  qreal routePosition = 0.5;
  qreal tangentOffset = 0.0;
  qreal normalOffset = 0.0;
  QJsonObject extra;

  bool operator==(const ConnectorAnnotationPlacement &) const = default;
};

struct ConnectorPresentation {
  QString id;
  QString relationshipId;
  ConnectorRouting routing = ConnectorRouting::Straight;
  ConnectorAnchor sourceAnchor;
  ConnectorAnchor targetAnchor;
  QList<ConnectorBendPoint> bendPoints;
  QHash<QString, ConnectorAnnotationPlacement> annotationPlacements;
  QJsonObject extra;

  bool operator==(const ConnectorPresentation &) const = default;
};

// A diagram filter changes only what is currently presented. It never removes
// presentations or semantic elements, so clearing the filter restores the
// exact diagram and connector layout.
struct DiagramFilter {
  QStringList excludedElementTypes;
  QStringList includedStereotypeIds;
  QStringList excludedStereotypeIds;
  QString namePattern;
  bool excludeNameMatches = false;
  QString memberPattern;
  bool excludeMemberMatches = false;
  QJsonObject extra;

  bool operator==(const DiagramFilter &) const = default;
};

struct Diagram {
  QString id;
  QString name;
  bool showAttributes = true;
  bool showOperations = true;
  OperationSignatureMode operationSignatureMode = OperationSignatureMode::Full;
  DiagramFilter filter;
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
  QStringList sourceRoots;
  QJsonObject extra;

  bool operator==(const CppImportConfiguration &) const = default;
};

struct ProjectData {
  int schemaVersion = kCurrentProjectSchemaVersion;
  QString id;
  QString name;
  CppImportConfiguration cppImport;
  QList<DiagramStyle> diagramStyles;
  QList<StereotypeDefinition> stereotypeDefinitions;
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
StereotypeDefinition *findStereotypeDefinition(ProjectData &project,
                                               const QString &id);
const StereotypeDefinition *findStereotypeDefinition(const ProjectData &project,
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

} // namespace yauml

Q_DECLARE_METATYPE(yauml::ElementType)
Q_DECLARE_METATYPE(yauml::RelationshipType)
Q_DECLARE_METATYPE(yauml::ConnectorSide)
Q_DECLARE_METATYPE(yauml::ConnectorRouting)
Q_DECLARE_METATYPE(yauml::MemberVisibility)
Q_DECLARE_METATYPE(yauml::OperationKind)
Q_DECLARE_METATYPE(yauml::OperationSignatureMode)
