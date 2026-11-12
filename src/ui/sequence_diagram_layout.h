#pragma once

#include "core/project_data.h"

#include <QFont>
#include <QRectF>
#include <QVector>

namespace yauml::ui {

// Scene-graph rendering and pointer interaction consume the same immutable
// layout. Keeping this calculation independent of QQuickItem makes sequence
// geometry deterministic and directly testable.
struct SequenceParticipantLayout {
  QString id;
  // The primary label is compact ("instance : Type" or just "Type"). The
  // namespace is kept separately so the renderer can use a smaller second
  // line instead of widening the participant header to fit an FQDN.
  QString name;
  QString qualifier;
  QString editableName;
  QString fullTypeName;
  QString elementId;
  QString executionContextLabel;
  QRectF headerRect;
  qreal lifelineX = 0.0;
  qreal lifelineStartY = 0.0;
  qreal lifelineEndY = 0.0;
  qreal destructionY = -1.0;
};

struct SequenceMessageLayout {
  QString id;
  QString text;
  SequenceMessageType type = SequenceMessageType::Synchronous;
  SequenceEvidenceConfidence confidence = SequenceEvidenceConfidence::Confirmed;
  QVector<QPointF> points;
  QRectF labelRect;
};

struct SequenceMessageLabel {
  QString text;
  QFont font;
};

struct SequenceActivationLayout {
  QString id;
  QString participantId;
  QString label;
  QString executionContextLabel;
  QRectF rect;
};

struct SequenceCombinedFragmentOperandLayout {
  QString guard;
  // The first operand starts at the frame top and therefore has no divider.
  qreal dividerY = -1.0;
};

struct SequenceCombinedFragmentLayout {
  QString id;
  SequenceCombinedFragmentType type = SequenceCombinedFragmentType::Optional;
  QRectF rect;
  QRectF headingRect;
  QVector<SequenceCombinedFragmentOperandLayout> operands;
};

struct SequenceDiagramLayout {
  QVector<SequenceParticipantLayout> participants;
  QVector<SequenceMessageLayout> messages;
  QVector<SequenceActivationLayout> activations;
  QVector<SequenceCombinedFragmentLayout> fragments;
  QRectF contentBounds;
};

SequenceDiagramLayout buildSequenceDiagramLayout(const ProjectData &project,
                                                 const Diagram &diagram);
SequenceMessageLabel sequenceMessageLabelForDisplay(
    const SequenceMessageLayout &message, QFont font);
const SequenceParticipantLayout *
hitSequenceParticipant(const SequenceDiagramLayout &layout,
                       const QPointF &scenePoint);
const SequenceParticipantLayout *
hitSequenceLifeline(const SequenceDiagramLayout &layout,
                    const QPointF &scenePoint, qreal tolerance);
const SequenceMessageLayout *
hitSequenceMessage(const SequenceDiagramLayout &layout,
                   const QPointF &scenePoint, qreal tolerance,
                   bool labelsVisible = true);
const SequenceActivationLayout *
hitSequenceActivation(const SequenceDiagramLayout &layout,
                      const QPointF &scenePoint, qreal tolerance);
const SequenceCombinedFragmentLayout *
hitSequenceCombinedFragment(const SequenceDiagramLayout &layout,
                            const QPointF &scenePoint, qreal tolerance);

} // namespace yauml::ui
