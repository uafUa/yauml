#include "core/project_data.h"

#include "core/stereotype_catalog.h"

#include <QUuid>

namespace yauml {

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

QString toString(DiagramType type) {
  switch (type) {
  case DiagramType::Class:
    return QStringLiteral("class");
  case DiagramType::Sequence:
    return QStringLiteral("sequence");
  }
  return QStringLiteral("class");
}

DiagramType diagramTypeFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("class"))
    return DiagramType::Class;
  if (value == QStringLiteral("sequence"))
    return DiagramType::Sequence;
  if (ok)
    *ok = false;
  return DiagramType::Class;
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

QString toString(MemberVisibility visibility) {
  switch (visibility) {
  case MemberVisibility::Public:
    return QStringLiteral("public");
  case MemberVisibility::Protected:
    return QStringLiteral("protected");
  case MemberVisibility::Private:
    return QStringLiteral("private");
  case MemberVisibility::Package:
    return QStringLiteral("package");
  }
  return QStringLiteral("public");
}

MemberVisibility memberVisibilityFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("public"))
    return MemberVisibility::Public;
  if (value == QStringLiteral("protected"))
    return MemberVisibility::Protected;
  if (value == QStringLiteral("private"))
    return MemberVisibility::Private;
  if (value == QStringLiteral("package"))
    return MemberVisibility::Package;
  if (ok)
    *ok = false;
  return MemberVisibility::Public;
}

QString toString(OperationKind kind) {
  switch (kind) {
  case OperationKind::Method:
    return QStringLiteral("method");
  case OperationKind::Constructor:
    return QStringLiteral("constructor");
  case OperationKind::Destructor:
    return QStringLiteral("destructor");
  }
  return QStringLiteral("method");
}

OperationKind operationKindFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("method"))
    return OperationKind::Method;
  if (value == QStringLiteral("constructor"))
    return OperationKind::Constructor;
  if (value == QStringLiteral("destructor"))
    return OperationKind::Destructor;
  if (ok)
    *ok = false;
  return OperationKind::Method;
}

QString toString(OperationSignatureMode mode) {
  switch (mode) {
  case OperationSignatureMode::Full:
    return QStringLiteral("full");
  case OperationSignatureMode::NameAndReturnType:
    return QStringLiteral("name-and-return-type");
  case OperationSignatureMode::NameOnly:
    return QStringLiteral("name-only");
  }
  return QStringLiteral("full");
}

OperationSignatureMode operationSignatureModeFromString(const QString &value,
                                                        bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("full"))
    return OperationSignatureMode::Full;
  if (value == QStringLiteral("name-and-return-type"))
    return OperationSignatureMode::NameAndReturnType;
  if (value == QStringLiteral("name-only"))
    return OperationSignatureMode::NameOnly;
  if (ok)
    *ok = false;
  return OperationSignatureMode::Full;
}

QString toString(SequenceTargetOperationDisplayMode mode) {
  switch (mode) {
  case SequenceTargetOperationDisplayMode::Hidden:
    return QStringLiteral("hidden");
  case SequenceTargetOperationDisplayMode::NameOnly:
    return QStringLiteral("name-only");
  case SequenceTargetOperationDisplayMode::FullSignature:
    return QStringLiteral("full-signature");
  }
  return QStringLiteral("hidden");
}

SequenceTargetOperationDisplayMode
sequenceTargetOperationDisplayModeFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("hidden"))
    return SequenceTargetOperationDisplayMode::Hidden;
  if (value == QStringLiteral("name-only"))
    return SequenceTargetOperationDisplayMode::NameOnly;
  if (value == QStringLiteral("full-signature"))
    return SequenceTargetOperationDisplayMode::FullSignature;
  if (ok)
    *ok = false;
  return SequenceTargetOperationDisplayMode::Hidden;
}

QString toString(SequenceExecutionContextType type) {
  switch (type) {
  case SequenceExecutionContextType::Thread:
    return QStringLiteral("thread");
  case SequenceExecutionContextType::EventLoop:
    return QStringLiteral("event-loop");
  case SequenceExecutionContextType::Executor:
    return QStringLiteral("executor");
  case SequenceExecutionContextType::Task:
    return QStringLiteral("task");
  }
  return QStringLiteral("thread");
}

SequenceExecutionContextType
sequenceExecutionContextTypeFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("thread"))
    return SequenceExecutionContextType::Thread;
  if (value == QStringLiteral("event-loop"))
    return SequenceExecutionContextType::EventLoop;
  if (value == QStringLiteral("executor"))
    return SequenceExecutionContextType::Executor;
  if (value == QStringLiteral("task"))
    return SequenceExecutionContextType::Task;
  if (ok)
    *ok = false;
  return SequenceExecutionContextType::Thread;
}

QString toString(SequenceMessageType type) {
  switch (type) {
  case SequenceMessageType::Synchronous:
    return QStringLiteral("sync");
  case SequenceMessageType::Asynchronous:
    return QStringLiteral("async");
  case SequenceMessageType::Return:
    return QStringLiteral("return");
  case SequenceMessageType::Create:
    return QStringLiteral("create");
  case SequenceMessageType::Destroy:
    return QStringLiteral("destroy");
  }
  return QStringLiteral("sync");
}

SequenceMessageType sequenceMessageTypeFromString(const QString &value,
                                                  bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("sync"))
    return SequenceMessageType::Synchronous;
  if (value == QStringLiteral("async"))
    return SequenceMessageType::Asynchronous;
  if (value == QStringLiteral("return"))
    return SequenceMessageType::Return;
  if (value == QStringLiteral("create"))
    return SequenceMessageType::Create;
  if (value == QStringLiteral("destroy"))
    return SequenceMessageType::Destroy;
  if (ok)
    *ok = false;
  return SequenceMessageType::Synchronous;
}

QString toString(SequenceCombinedFragmentType type) {
  switch (type) {
  case SequenceCombinedFragmentType::Alternative:
    return QStringLiteral("alt");
  case SequenceCombinedFragmentType::Optional:
    return QStringLiteral("opt");
  case SequenceCombinedFragmentType::Loop:
    return QStringLiteral("loop");
  case SequenceCombinedFragmentType::Parallel:
    return QStringLiteral("par");
  case SequenceCombinedFragmentType::Critical:
    return QStringLiteral("critical");
  }
  return QStringLiteral("opt");
}

SequenceCombinedFragmentType
sequenceCombinedFragmentTypeFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("alt"))
    return SequenceCombinedFragmentType::Alternative;
  if (value == QStringLiteral("opt"))
    return SequenceCombinedFragmentType::Optional;
  if (value == QStringLiteral("loop"))
    return SequenceCombinedFragmentType::Loop;
  if (value == QStringLiteral("par"))
    return SequenceCombinedFragmentType::Parallel;
  if (value == QStringLiteral("critical"))
    return SequenceCombinedFragmentType::Critical;
  if (ok)
    *ok = false;
  return SequenceCombinedFragmentType::Optional;
}

bool sequenceCombinedFragmentHasSecondOperand(
    SequenceCombinedFragmentType type) {
  return type == SequenceCombinedFragmentType::Alternative ||
         type == SequenceCombinedFragmentType::Parallel;
}

QString toString(SequenceEvidenceConfidence confidence) {
  switch (confidence) {
  case SequenceEvidenceConfidence::Confirmed:
    return QStringLiteral("confirmed");
  case SequenceEvidenceConfidence::Inferred:
    return QStringLiteral("inferred");
  }
  return QStringLiteral("confirmed");
}

SequenceEvidenceConfidence
sequenceEvidenceConfidenceFromString(const QString &value, bool *ok) {
  if (ok)
    *ok = true;
  if (value == QStringLiteral("confirmed"))
    return SequenceEvidenceConfidence::Confirmed;
  if (value == QStringLiteral("inferred"))
    return SequenceEvidenceConfidence::Inferred;
  if (ok)
    *ok = false;
  return SequenceEvidenceConfidence::Confirmed;
}

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

DiagramDisplayOptions displayOptions(const Diagram &diagram) {
  return {diagram.showAttributes,
          diagram.showOperations,
          diagram.operationSignatureMode,
          diagram.showTypeIcons,
          diagram.sequenceTargetOperationDisplayMode,
          diagram.showSequenceMessageStereotypes};
}

void applyDisplayOptions(Diagram &diagram,
                         const DiagramDisplayOptions &options) {
  diagram.showAttributes = options.showAttributes;
  diagram.showOperations = options.showOperations;
  diagram.operationSignatureMode = options.operationSignatureMode;
  diagram.showTypeIcons = options.showTypeIcons;
  diagram.sequenceTargetOperationDisplayMode =
      options.sequenceTargetOperationDisplayMode;
  diagram.showSequenceMessageStereotypes =
      options.showSequenceMessageStereotypes;
}

ProjectData createStarterProject(const QString &name) {
  ProjectData project;
  project.id = newId();
  project.name = name;
  project.stereotypeDefinitions = stereotype_catalog::defaultDefinitions();

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

StereotypeDefinition *findStereotypeDefinition(ProjectData &project,
                                               const QString &id) {
  return findById(project.stereotypeDefinitions, id);
}

const StereotypeDefinition *findStereotypeDefinition(const ProjectData &project,
                                                     const QString &id) {
  return findById(project.stereotypeDefinitions, id);
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

NotePresentation *findNote(Diagram &diagram, const QString &id) {
  return findById(diagram.notes, id);
}

const NotePresentation *findNote(const Diagram &diagram, const QString &id) {
  return findById(diagram.notes, id);
}

NoteAttachment *findNoteAttachment(Diagram &diagram, const QString &id) {
  return findById(diagram.noteAttachments, id);
}

const NoteAttachment *findNoteAttachment(const Diagram &diagram,
                                         const QString &id) {
  return findById(diagram.noteAttachments, id);
}

SequenceParticipant *findSequenceParticipant(Diagram &diagram,
                                             const QString &id) {
  return findById(diagram.sequenceParticipants, id);
}

const SequenceParticipant *findSequenceParticipant(const Diagram &diagram,
                                                   const QString &id) {
  return findById(diagram.sequenceParticipants, id);
}

SequenceMessage *findSequenceMessage(Diagram &diagram, const QString &id) {
  return findById(diagram.sequenceMessages, id);
}

const SequenceMessage *findSequenceMessage(const Diagram &diagram,
                                           const QString &id) {
  return findById(diagram.sequenceMessages, id);
}

SequenceActivation *findSequenceActivation(Diagram &diagram,
                                           const QString &id) {
  return findById(diagram.sequenceActivations, id);
}

const SequenceActivation *findSequenceActivation(const Diagram &diagram,
                                                 const QString &id) {
  return findById(diagram.sequenceActivations, id);
}

SequenceCombinedFragment *findSequenceCombinedFragment(Diagram &diagram,
                                                       const QString &id) {
  return findById(diagram.sequenceCombinedFragments, id);
}

const SequenceCombinedFragment *
findSequenceCombinedFragment(const Diagram &diagram, const QString &id) {
  return findById(diagram.sequenceCombinedFragments, id);
}

SequenceExecutionContext *findSequenceExecutionContext(Diagram &diagram,
                                                       const QString &id) {
  return findById(diagram.sequenceExecutionContexts, id);
}

const SequenceExecutionContext *
findSequenceExecutionContext(const Diagram &diagram, const QString &id) {
  return findById(diagram.sequenceExecutionContexts, id);
}

} // namespace yauml
