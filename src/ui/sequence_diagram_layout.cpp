#include "ui/sequence_diagram_layout.h"

#include "core/presentation_layout.h"

#include <QHash>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace yauml::ui {
namespace {

constexpr qreal kInteractionTail = 90.0;
constexpr qreal kMinimumLifelineLength = 160.0;
constexpr qreal kSelfMessageWidth = 64.0;
constexpr qreal kSelfMessageHeight = 32.0;
constexpr qreal kMessageLabelHeight = 24.0;
constexpr qreal kActivationWidth = 14.0;
constexpr qreal kMinimumActivationHeight = 24.0;
constexpr qreal kSiblingActivationGap = 4.0;
constexpr qreal kFragmentHorizontalMargin = 16.0;
constexpr qreal kFragmentTopPadding = 20.0;
constexpr qreal kFragmentBottomPadding = 18.0;
constexpr qreal kParticipantPrimaryHeight = 28.0;
constexpr qreal kParticipantSecondaryHeight = 18.0;

qreal distanceToSegment(const QPointF &point, const QPointF &start,
                        const QPointF &end) {
  const QPointF delta = end - start;
  const qreal lengthSquared = QPointF::dotProduct(delta, delta);
  if (lengthSquared <= 0.001)
    return QLineF(point, start).length();
  const qreal t = std::clamp(
      QPointF::dotProduct(point - start, delta) / lengthSquared, 0.0, 1.0);
  return QLineF(point, start + delta * t).length();
}

QString contextLabel(const Diagram &diagram, const QString &contextId) {
  const auto *context = findSequenceExecutionContext(diagram, contextId);
  if (!context)
    return {};
  return QStringLiteral("«%1» %2").arg(toString(context->type), context->name);
}

void includeRect(QRectF &bounds, bool &hasBounds, const QRectF &rect) {
  if (!rect.isValid() || rect.isEmpty())
    return;
  bounds = hasBounds ? bounds.united(rect) : rect;
  hasBounds = true;
}

QRectF pointsBounds(const QVector<QPointF> &points) {
  if (points.isEmpty())
    return {};
  qreal left = points.first().x();
  qreal right = left;
  qreal top = points.first().y();
  qreal bottom = top;
  for (const QPointF &point : points) {
    left = std::min(left, point.x());
    right = std::max(right, point.x());
    top = std::min(top, point.y());
    bottom = std::max(bottom, point.y());
  }
  return QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
}

// A message belongs to the innermost activation that is active at its row.
// Activations are laid out progressively to the right, so the rightmost active
// bar is the deepest one for a participant. This keeps message endpoints on a
// bar's edge rather than hidden below its fill and border.
const SequenceActivationLayout *
activeActivationAt(const SequenceDiagramLayout &layout,
                   const QString &participantId, qreal y) {
  const SequenceActivationLayout *result = nullptr;
  for (const SequenceActivationLayout &activation : layout.activations) {
    if (activation.participantId != participantId ||
        activation.rect.top() - 0.01 > y || activation.rect.bottom() + 0.01 < y)
      continue;
    if (!result || activation.rect.right() > result->rect.right())
      result = &activation;
  }
  return result;
}

const SequenceActivationLayout *
activationLayoutById(const SequenceDiagramLayout &layout,
                     const QString &activationId) {
  if (activationId.isEmpty())
    return nullptr;
  const auto found =
      std::find_if(layout.activations.cbegin(), layout.activations.cend(),
                   [&activationId](const SequenceActivationLayout &activation) {
                     return activation.id == activationId;
                   });
  return found == layout.activations.cend() ? nullptr : &*found;
}

const SequenceActivationLayout *
activationClosedByMessage(const Diagram &diagram,
                          const SequenceDiagramLayout &layout,
                          const SequenceMessage &message) {
  const SequenceActivationLayout *result = nullptr;
  for (const SequenceActivation &activation : diagram.sequenceActivations) {
    if (activation.participantId != message.sourceParticipantId ||
        activation.endMessageId != message.id)
      continue;
    const auto visual =
        std::find_if(layout.activations.cbegin(), layout.activations.cend(),
                     [&activation](const SequenceActivationLayout &candidate) {
                       return candidate.id == activation.id;
                     });
    // One return may implicitly close nested activations together with their
    // selected parent. The outermost (leftmost) closing bar owns the arrow.
    if (visual != layout.activations.cend() &&
        (!result || visual->rect.left() < result->rect.left()))
      result = &*visual;
  }
  return result;
}

qreal messageEndpointX(const SequenceDiagramLayout &layout,
                       const QString &participantId, qreal y, qreal remoteX,
                       qreal lifelineX) {
  const auto *activation = activeActivationAt(layout, participantId, y);
  if (!activation)
    return lifelineX;
  return remoteX < activation->rect.center().x() ? activation->rect.left()
                                                 : activation->rect.right();
}

struct ParticipantTitle {
  QString primary;
  QString qualifier;
  QString editableName;
  QString fullTypeName;
};

ParticipantTitle participantTitle(const ProjectData &project,
                                  const SequenceParticipant &participant) {
  const ModelElement *element =
      participant.elementId.isEmpty()
          ? nullptr
          : findElement(project, participant.elementId);
  if (!element) {
    const QString name = participant.name.trimmed();
    return {name, {}, name, {}};
  }

  const QString fullTypeName =
      presentation_layout::fullyQualifiedElementName(project, *element);
  const QString shortTypeName = fullTypeName.section(QStringLiteral("::"), -1);
  const qsizetype separator = fullTypeName.lastIndexOf(QStringLiteral("::"));
  const QString qualifier =
      separator >= 0 ? fullTypeName.left(separator) : QString{};
  QString instanceName = participant.name.trimmed();
  // Older projects copied the type name into participant.name. Treat both the
  // short and fully qualified forms as an absent instance alias.
  if (instanceName == fullTypeName || instanceName == shortTypeName ||
      instanceName == element->name.trimmed()) {
    instanceName.clear();
  }
  const QString primary =
      instanceName.isEmpty()
          ? shortTypeName
          : QStringLiteral("%1 : %2").arg(instanceName, shortTypeName);
  return {primary, qualifier,
          instanceName.isEmpty() ? shortTypeName : instanceName, fullTypeName};
}

} // namespace

SequenceDiagramLayout buildSequenceDiagramLayout(const ProjectData &project,
                                                 const Diagram &diagram) {
  SequenceDiagramLayout layout;
  if (diagram.type != DiagramType::Sequence)
    return layout;

  qreal lastMessageY = 0.0;
  QHash<QString, qreal> messageYById;
  QHash<QString, qreal> creationYByParticipant;
  QHash<QString, qreal> destructionYByParticipant;
  for (const SequenceMessage &message : diagram.sequenceMessages) {
    lastMessageY = std::max(lastMessageY, message.y);
    messageYById.insert(message.id, message.y);
    if (message.type == SequenceMessageType::Create) {
      const auto existing =
          creationYByParticipant.constFind(message.targetParticipantId);
      if (existing == creationYByParticipant.cend() || message.y < *existing)
        creationYByParticipant.insert(message.targetParticipantId, message.y);
    }
    if (message.type == SequenceMessageType::Destroy)
      destructionYByParticipant.insert(message.targetParticipantId, message.y);
  }
  for (const SequenceActivation &activation : diagram.sequenceActivations)
    if (activation.endY)
      lastMessageY = std::max(lastMessageY, *activation.endY);

  QHash<QString, qreal> lifelineXByParticipant;
  QHash<QString, qreal> lifelineEndByParticipant;
  bool hasBounds = false;
  for (const SequenceParticipant &participant : diagram.sequenceParticipants) {
    const ParticipantTitle title = participantTitle(project, participant);
    QRectF header = participant.headerGeometry.normalized();
    const int secondaryLines =
        (!title.qualifier.isEmpty() ? 1 : 0) +
        (!participant.defaultExecutionContextId.isEmpty() ? 1 : 0);
    header.setHeight(std::max(
        header.height(), kParticipantPrimaryHeight +
                             secondaryLines * kParticipantSecondaryHeight));
    const auto creation = creationYByParticipant.constFind(participant.id);
    if (creation != creationYByParticipant.cend()) {
      // UML creation is rendered by placing the new instance header at the
      // message row. The message then meets the header's side and the lifeline
      // begins below it, making the instance lifetime explicit at a glance.
      header.moveCenter({header.center().x(), *creation});
    }
    const qreal destructionY =
        destructionYByParticipant.value(participant.id, -1.0);
    const qreal defaultEnd = std::max(lastMessageY + kInteractionTail,
                                      header.bottom() + kMinimumLifelineLength);
    const qreal lifelineEnd = destructionY >= 0.0 ? destructionY : defaultEnd;
    const qreal lifelineStart = header.bottom();
    layout.participants.append(
        {participant.id, title.primary, title.qualifier, title.editableName,
         title.fullTypeName, participant.elementId,
         contextLabel(diagram, participant.defaultExecutionContextId), header,
         header.center().x(), lifelineStart, lifelineEnd, destructionY});
    lifelineXByParticipant.insert(participant.id, header.center().x());
    lifelineEndByParticipant.insert(participant.id, lifelineEnd);
    includeRect(layout.contentBounds, hasBounds, header);
    includeRect(layout.contentBounds, hasBounds,
                QRectF(header.center().x() - 1.0, lifelineStart, 2.0,
                       std::max(1.0, lifelineEnd - lifelineStart)));
  }

  QHash<QString, int> legacyActivationDepthByParticipant;
  QHash<QString, int> activationDepthById;
  for (const SequenceActivation &activation : diagram.sequenceActivations) {
    if (!lifelineXByParticipant.contains(activation.participantId))
      continue;
    const bool messageBound = !activation.startMessageId.isEmpty();
    if (messageBound && !messageYById.contains(activation.startMessageId))
      continue;
    if (!messageBound && (!activation.startY || !activation.endY))
      continue;
    const qreal startY = messageBound
                             ? messageYById.value(activation.startMessageId)
                             : *activation.startY;
    const qreal requestedEnd =
        messageBound
            ? (activation.endMessageId.isEmpty()
                   ? lifelineEndByParticipant.value(activation.participantId,
                                                    startY)
                   : messageYById.value(activation.endMessageId, startY))
            : *activation.endY;
    const qreal endY =
        std::max(startY + kMinimumActivationHeight, requestedEnd);
    int depth = legacyActivationDepthByParticipant[activation.participantId]++;
    if (messageBound) {
      const auto *startMessage =
          findSequenceMessage(diagram, activation.startMessageId);
      const auto *parent =
          startMessage && !startMessage->sourceActivationId.isEmpty()
              ? findSequenceActivation(diagram,
                                       startMessage->sourceActivationId)
              : nullptr;
      const auto parentDepth = parent
                                   ? activationDepthById.constFind(parent->id)
                                   : activationDepthById.cend();
      if (parent && parent->participantId == activation.participantId &&
          parentDepth != activationDepthById.cend()) {
        // Two self-calls explicitly launched from the same outer bar are
        // siblings. They therefore share a visual depth even if their spans
        // overlap, rather than drifting progressively farther right.
        depth = *parentDepth + 1;
      }
    }
    activationDepthById.insert(activation.id, depth);
    const qreal x = lifelineXByParticipant.value(activation.participantId) -
                    kActivationWidth / 2.0 + depth * 4.0;
    const QString activationContext =
        contextLabel(diagram, activation.executionContextId);
    SequenceActivationLayout visual{
        activation.id, activation.participantId, activation.label,
        activationContext, QRectF(x, startY, kActivationWidth, endY - startY)};
    includeRect(layout.contentBounds, hasBounds, visual.rect);
    layout.activations.append(std::move(visual));
  }

  // Consecutive sibling activations share the same horizontal level. Their
  // semantic boundary is the message that closes one and starts the next, but
  // two rectangles touching at that row look like one uninterrupted bar.
  // Shorten only the preceding bar to preserve the new call's attachment row
  // while leaving a small, zoom-independent visual break.
  for (SequenceActivationLayout &earlier : layout.activations) {
    for (const SequenceActivationLayout &later :
         std::as_const(layout.activations)) {
      if (earlier.id == later.id ||
          earlier.participantId != later.participantId ||
          std::abs(earlier.rect.left() - later.rect.left()) > 0.01 ||
          std::abs(earlier.rect.bottom() - later.rect.top()) > 0.01) {
        continue;
      }
      const qreal separatedBottom = later.rect.top() - kSiblingActivationGap;
      if (separatedBottom > earlier.rect.top())
        earlier.rect.setBottom(separatedBottom);
    }
  }

  // Combined fragments are computed before messages so the renderer can paint
  // their frames behind lifelines, bars, and arrows. Scope follows the
  // explicitly selected lifelines rather than incidental message endpoints.
  for (const SequenceCombinedFragment &fragment :
       diagram.sequenceCombinedFragments) {
    if (!messageYById.contains(fragment.startMessageId) ||
        !messageYById.contains(fragment.endMessageId))
      continue;
    qreal left = std::numeric_limits<qreal>::infinity();
    qreal right = -std::numeric_limits<qreal>::infinity();
    for (const QString &participantId : fragment.participantIds) {
      const auto participant = std::find_if(
          layout.participants.cbegin(), layout.participants.cend(),
          [&participantId](const SequenceParticipantLayout &candidate) {
            return candidate.id == participantId;
          });
      if (participant == layout.participants.cend())
        continue;
      left = std::min(left, participant->headerRect.left());
      right = std::max(right, participant->headerRect.right());
    }
    if (!std::isfinite(left) || !std::isfinite(right))
      continue;
    const qreal top =
        messageYById.value(fragment.startMessageId) - kFragmentTopPadding;
    const qreal bottom =
        messageYById.value(fragment.endMessageId) + kFragmentBottomPadding;
    SequenceCombinedFragmentLayout visual;
    visual.id = fragment.id;
    visual.type = fragment.type;
    visual.rect = QRectF(left - kFragmentHorizontalMargin, top,
                         right - left + kFragmentHorizontalMargin * 2.0,
                         std::max(1.0, bottom - top));
    for (qsizetype operandIndex = 0; operandIndex < fragment.operands.size();
         ++operandIndex) {
      const auto &operand = fragment.operands.at(operandIndex);
      SequenceCombinedFragmentOperandLayout operandLayout;
      operandLayout.guard = operand.guard;
      if (operandIndex > 0 && messageYById.contains(operand.startMessageId))
        operandLayout.dividerY =
            messageYById.value(operand.startMessageId) - 12.0;
      visual.operands.append(std::move(operandLayout));
    }
    includeRect(layout.contentBounds, hasBounds, visual.rect);
    layout.fragments.append(std::move(visual));
  }

  for (const SequenceMessage &message : diagram.sequenceMessages) {
    if (!lifelineXByParticipant.contains(message.sourceParticipantId) ||
        !lifelineXByParticipant.contains(message.targetParticipantId))
      continue;
    const qreal sourceLifelineX =
        lifelineXByParticipant.value(message.sourceParticipantId);
    const qreal targetLifelineX =
        lifelineXByParticipant.value(message.targetParticipantId);
    SequenceMessageLayout visual;
    visual.id = message.id;
    visual.text = message.text;
    visual.type = message.type;
    visual.confidence = message.evidence.confidence;
    const auto *closingActivation =
        message.type == SequenceMessageType::Return
            ? activationClosedByMessage(diagram, layout, message)
            : nullptr;
    const auto *explicitSourceActivation =
        activationLayoutById(layout, message.sourceActivationId);
    const auto *sourceActivation =
        closingActivation ? closingActivation : explicitSourceActivation;
    if (message.sourceParticipantId == message.targetParticipantId) {
      const qreal startX =
          sourceActivation
              ? sourceActivation->rect.right()
              : messageEndpointX(layout, message.sourceParticipantId, message.y,
                                 sourceLifelineX + kSelfMessageWidth,
                                 sourceLifelineX);
      const qreal endX = messageEndpointX(
          layout, message.targetParticipantId, message.y + kSelfMessageHeight,
          sourceLifelineX + kSelfMessageWidth, sourceLifelineX);
      visual.points = {
          {startX, message.y},
          {startX + kSelfMessageWidth, message.y},
          {startX + kSelfMessageWidth, message.y + kSelfMessageHeight},
          {endX, message.y + kSelfMessageHeight}};
      visual.labelRect = QRectF(startX + 8.0, message.y - kMessageLabelHeight,
                                kSelfMessageWidth + 120.0, kMessageLabelHeight);
    } else {
      qreal sourceX =
          sourceActivation
              ? (targetLifelineX < sourceActivation->rect.center().x()
                     ? sourceActivation->rect.left()
                     : sourceActivation->rect.right())
              : messageEndpointX(layout, message.sourceParticipantId, message.y,
                                 targetLifelineX, sourceLifelineX);
      qreal targetX = messageEndpointX(layout, message.targetParticipantId,
                                       message.y, sourceX, targetLifelineX);
      if (message.type == SequenceMessageType::Create) {
        const auto targetParticipant = std::find_if(
            layout.participants.cbegin(), layout.participants.cend(),
            [&message](const SequenceParticipantLayout &candidate) {
              return candidate.id == message.targetParticipantId;
            });
        if (targetParticipant != layout.participants.cend()) {
          targetX = sourceX < targetParticipant->headerRect.center().x()
                        ? targetParticipant->headerRect.left()
                        : targetParticipant->headerRect.right();
        }
      }
      // Ordinary messages use the innermost activation at their row, and the
      // target edge can flip which side of that bar faces the message. A return
      // is different: it must remain attached to the explicitly closed
      // activation, even when deeper activations are still open at that row.
      if (!sourceActivation)
        sourceX = messageEndpointX(layout, message.sourceParticipantId,
                                   message.y, targetX, sourceLifelineX);
      visual.points = {{sourceX, message.y}, {targetX, message.y}};
      const qreal left = std::min(sourceX, targetX);
      const qreal width = std::max(80.0, std::abs(targetX - sourceX) - 16.0);
      visual.labelRect = QRectF(left + 8.0, message.y - kMessageLabelHeight,
                                width, kMessageLabelHeight);
    }
    includeRect(layout.contentBounds, hasBounds,
                pointsBounds(visual.points).adjusted(-8.0, -8.0, 8.0, 8.0));
    includeRect(layout.contentBounds, hasBounds, visual.labelRect);
    layout.messages.append(std::move(visual));
  }

  if (hasBounds)
    layout.contentBounds.adjust(-24.0, -24.0, 24.0, 24.0);
  return layout;
}

const SequenceParticipantLayout *
hitSequenceParticipant(const SequenceDiagramLayout &layout,
                       const QPointF &scenePoint) {
  for (auto participant = layout.participants.crbegin();
       participant != layout.participants.crend(); ++participant)
    if (participant->headerRect.contains(scenePoint))
      return &*participant;
  return nullptr;
}

const SequenceParticipantLayout *
hitSequenceLifeline(const SequenceDiagramLayout &layout,
                    const QPointF &scenePoint, qreal tolerance) {
  for (auto participant = layout.participants.crbegin();
       participant != layout.participants.crend(); ++participant) {
    if (scenePoint.y() < participant->lifelineStartY - tolerance ||
        scenePoint.y() > participant->lifelineEndY + tolerance)
      continue;
    if (std::abs(scenePoint.x() - participant->lifelineX) <= tolerance)
      return &*participant;
  }
  return nullptr;
}

const SequenceMessageLayout *
hitSequenceMessage(const SequenceDiagramLayout &layout,
                   const QPointF &scenePoint, qreal tolerance) {
  for (auto message = layout.messages.crbegin();
       message != layout.messages.crend(); ++message) {
    if (message->labelRect
            .adjusted(-tolerance, -tolerance, tolerance, tolerance)
            .contains(scenePoint))
      return &*message;
    for (qsizetype index = 1; index < message->points.size(); ++index)
      if (distanceToSegment(scenePoint, message->points.at(index - 1),
                            message->points.at(index)) <= tolerance)
        return &*message;
  }
  return nullptr;
}

const SequenceActivationLayout *
hitSequenceActivation(const SequenceDiagramLayout &layout,
                      const QPointF &scenePoint, qreal tolerance) {
  // Prefer the actual painted rectangle before applying a generous hit slop.
  // Otherwise the expanded area of a nested bar steals the narrow visible
  // strip of its parent and makes that outer activation impossible to choose.
  for (auto activation = layout.activations.crbegin();
       activation != layout.activations.crend(); ++activation)
    if (activation->rect.contains(scenePoint))
      return &*activation;
  for (auto activation = layout.activations.crbegin();
       activation != layout.activations.crend(); ++activation)
    if (activation->rect.adjusted(-tolerance, -tolerance, tolerance, tolerance)
            .contains(scenePoint))
      return &*activation;
  return nullptr;
}

const SequenceCombinedFragmentLayout *
hitSequenceCombinedFragment(const SequenceDiagramLayout &layout,
                            const QPointF &scenePoint, qreal tolerance) {
  for (auto fragment = layout.fragments.crbegin();
       fragment != layout.fragments.crend(); ++fragment) {
    const QRectF outer =
        fragment->rect.adjusted(-tolerance, -tolerance, tolerance, tolerance);
    bool nearHorizontal =
        std::abs(scenePoint.y() - fragment->rect.top()) <= tolerance ||
        std::abs(scenePoint.y() - fragment->rect.bottom()) <= tolerance;
    for (const auto &operand : fragment->operands)
      nearHorizontal =
          nearHorizontal ||
          (operand.dividerY >= 0.0 &&
           std::abs(scenePoint.y() - operand.dividerY) <= tolerance);
    const bool nearVertical =
        std::abs(scenePoint.x() - fragment->rect.left()) <= tolerance ||
        std::abs(scenePoint.x() - fragment->rect.right()) <= tolerance;
    if (outer.contains(scenePoint) && (nearHorizontal || nearVertical))
      return &*fragment;
  }
  return nullptr;
}

} // namespace yauml::ui
