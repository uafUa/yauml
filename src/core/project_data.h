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
enum class DiagramType { Class, Sequence };
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
enum class SequenceExecutionContextType { Thread, EventLoop, Executor, Task };
enum class SequenceMessageType {
  Synchronous,
  Asynchronous,
  Return,
  Create,
  Destroy
};
// A combined fragment groups a contiguous part of an interaction. This set
// covers the common control-flow and concurrency constructs without imposing a
// guard-expression language on the project model.
enum class SequenceCombinedFragmentType {
  Alternative,
  Optional,
  Loop,
  Parallel,
  Critical
};
enum class SequenceEvidenceConfidence { Confirmed, Inferred };

QString toString(ElementType type);
ElementType elementTypeFromString(const QString &value, bool *ok = nullptr);
QString toString(DiagramType type);
DiagramType diagramTypeFromString(const QString &value, bool *ok = nullptr);
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
QString toString(SequenceExecutionContextType type);
SequenceExecutionContextType
sequenceExecutionContextTypeFromString(const QString &value,
                                       bool *ok = nullptr);
QString toString(SequenceMessageType type);
SequenceMessageType sequenceMessageTypeFromString(const QString &value,
                                                  bool *ok = nullptr);
QString toString(SequenceCombinedFragmentType type);
SequenceCombinedFragmentType
sequenceCombinedFragmentTypeFromString(const QString &value,
                                       bool *ok = nullptr);
bool sequenceCombinedFragmentHasSecondOperand(SequenceCombinedFragmentType type);
QString toString(SequenceEvidenceConfidence confidence);
SequenceEvidenceConfidence
sequenceEvidenceConfidenceFromString(const QString &value, bool *ok = nullptr);
QString newId();

// Browser organization is deliberately separate from UML/C++ ownership. An
// empty kind means "use semantic hierarchy"; otherwise the item is explicitly
// placed under the named browser container.
struct BrowserParent {
  QString kind;
  QString id;

  bool operator==(const BrowserParent &) const = default;
};

// Named, project-owned diagram styles share typography and borders while
// retaining per-classifier body fills. `fill` is the package/folder fallback;
// the type-specific fills preserve that distinction without requiring a
// separate style object for every UML classifier kind. Values are normalized
// serialized QColor strings (#RRGGBB or #AARRGGBB).
struct DiagramStyle {
  QString id;
  QString name;
  QString fill;
  QString classFill;
  QString structFill;
  QString enumerationFill;
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

// Notes are diagram-owned annotations rather than semantic model elements.
// They deliberately have no project-tree identity and cannot own other
// presentations. The text is stored as Markdown source so editing is lossless.
struct NotePresentation {
  QString id;
  QString text;
  QRectF geometry;
  QJsonObject extra;

  bool operator==(const NotePresentation &) const = default;
};

// A note attachment is also presentation-only. Keeping explicit perimeter
// anchors in the format leaves room for manual endpoint placement without ever
// turning the annotation into a UML relationship.
struct NoteAttachment {
  QString id;
  QString noteId;
  QString targetPresentationId;
  ConnectorAnchor noteAnchor;
  ConnectorAnchor targetAnchor;
  QJsonObject extra;

  bool operator==(const NoteAttachment &) const = default;
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

// Sequence-diagram records are diagram-local. Participants may refer to a
// semantic model element, but messages and execution contexts describe one
// behavioral view and therefore do not become structural model relationships.
struct SequenceExecutionContext {
  QString id;
  QString key;
  QString name;
  SequenceExecutionContextType type = SequenceExecutionContextType::Thread;
  QJsonObject extra;

  bool operator==(const SequenceExecutionContext &) const = default;
};

struct SequenceParticipant {
  QString id;
  QString key;
  QString name;
  QString elementId;
  QString defaultExecutionContextId;
  QRectF headerGeometry;
  QJsonObject extra;

  bool operator==(const SequenceParticipant &) const = default;
};

struct SequenceSourceEvidence {
  QString file;
  int line = 0;
  int column = 0;
  SequenceEvidenceConfidence confidence = SequenceEvidenceConfidence::Confirmed;
  QString note;
  QJsonObject extra;

  bool operator==(const SequenceSourceEvidence &) const = default;
};

struct SequenceMessage {
  QString id;
  QString key;
  QString sourceParticipantId;
  QString targetParticipantId;
  SequenceMessageType type = SequenceMessageType::Synchronous;
  QString text;
  qreal y = 0.0;
  SequenceSourceEvidence evidence;
  QJsonObject extra;
  // A message may name the operation invoked on its target lifeline. Keeping
  // both IDs makes the reference unambiguous even when operation IDs originate
  // from different C++ importers.
  QString targetOperationElementId;
  QString targetOperationId;

  bool operator==(const SequenceMessage &) const = default;
};

// An activation can either follow message anchors or occupy an explicitly
// positioned range on its lifeline. Message-bound activations survive message
// reordering; free-positioned activations make manual authoring possible before
// any messages exist. An empty endMessageId on a message-bound activation means
// that it continues to the end of the interaction.
struct SequenceActivation {
  QString id;
  QString key;
  QString participantId;
  QString executionContextId;
  QString startMessageId;
  QString endMessageId;
  std::optional<qreal> startY;
  std::optional<qreal> endY;
  QString label;
  QJsonObject extra;

  bool operator==(const SequenceActivation &) const = default;
};

// Bounds use message IDs rather than rows, which keeps the frame semantically
// attached when messages are repositioned. Alternative and parallel fragments
// have two operands: the first begins at startMessageId and the second at
// alternativeStartMessageId. The field retains its established serialized
// name so existing sequence projects remain compatible.
struct SequenceCombinedFragment {
  QString id;
  QString key;
  SequenceCombinedFragmentType type = SequenceCombinedFragmentType::Optional;
  QStringList participantIds;
  QString startMessageId;
  QString endMessageId;
  QString guard;
  QString alternativeStartMessageId;
  QString alternativeGuard;
  QJsonObject extra;

  bool operator==(const SequenceCombinedFragment &) const = default;
};

struct Diagram {
  QString id;
  QString name;
  DiagramType type = DiagramType::Class;
  bool showAttributes = true;
  bool showOperations = true;
  OperationSignatureMode operationSignatureMode = OperationSignatureMode::Full;
  DiagramFilter filter;
  QList<ContainerPresentation> containers;
  QList<NodePresentation> nodes;
  QList<ConnectorPresentation> connectors;
  QList<NotePresentation> notes;
  QList<NoteAttachment> noteAttachments;
  QList<SequenceExecutionContext> sequenceExecutionContexts;
  QList<SequenceParticipant> sequenceParticipants;
  QList<SequenceMessage> sequenceMessages;
  QList<SequenceActivation> sequenceActivations;
  QList<SequenceCombinedFragment> sequenceCombinedFragments;
  QJsonObject sequenceExtra;
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
NotePresentation *findNote(Diagram &diagram, const QString &id);
const NotePresentation *findNote(const Diagram &diagram, const QString &id);
NoteAttachment *findNoteAttachment(Diagram &diagram, const QString &id);
const NoteAttachment *findNoteAttachment(const Diagram &diagram,
                                         const QString &id);
SequenceParticipant *findSequenceParticipant(Diagram &diagram,
                                             const QString &id);
const SequenceParticipant *findSequenceParticipant(const Diagram &diagram,
                                                   const QString &id);
SequenceMessage *findSequenceMessage(Diagram &diagram, const QString &id);
const SequenceMessage *findSequenceMessage(const Diagram &diagram,
                                           const QString &id);
SequenceActivation *findSequenceActivation(Diagram &diagram, const QString &id);
const SequenceActivation *findSequenceActivation(const Diagram &diagram,
                                                 const QString &id);
SequenceCombinedFragment *findSequenceCombinedFragment(Diagram &diagram,
                                                        const QString &id);
const SequenceCombinedFragment *
findSequenceCombinedFragment(const Diagram &diagram, const QString &id);
SequenceExecutionContext *findSequenceExecutionContext(Diagram &diagram,
                                                       const QString &id);
const SequenceExecutionContext *
findSequenceExecutionContext(const Diagram &diagram, const QString &id);

} // namespace yauml

Q_DECLARE_METATYPE(yauml::ElementType)
Q_DECLARE_METATYPE(yauml::RelationshipType)
Q_DECLARE_METATYPE(yauml::ConnectorSide)
Q_DECLARE_METATYPE(yauml::ConnectorRouting)
Q_DECLARE_METATYPE(yauml::MemberVisibility)
Q_DECLARE_METATYPE(yauml::OperationKind)
Q_DECLARE_METATYPE(yauml::OperationSignatureMode)
