#include "core/sequence_diagram_import.h"

#include "core/json5.h"
#include "core/presentation_layout.h"
#include "core/project_serializer.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace yauml {
namespace {

constexpr qreal kFirstParticipantX = 120.0;
constexpr qreal kParticipantSpacing = 280.0;
constexpr qreal kParticipantHeaderY = 60.0;
constexpr qreal kParticipantHeaderWidth = 200.0;
constexpr qreal kParticipantHeaderHeight = 60.0;
constexpr qreal kFirstMessageY = 180.0;
constexpr qreal kMessageSpacing = 64.0;

Diagnostic importError(const QString &message, const QString &subject = {}) {
  return {DiagnosticSeverity::Error, QStringLiteral("sequence-import"), message,
          subject};
}

bool hasErrors(const QList<Diagnostic> &diagnostics) {
  return std::any_of(diagnostics.cbegin(), diagnostics.cend(),
                     [](const Diagnostic &diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::Error;
                     });
}

QJsonObject withoutKeys(QJsonObject object,
                        std::initializer_list<QString> keys) {
  for (const QString &key : keys)
    object.remove(key);
  return object;
}

QString requiredString(const QJsonObject &object, const QString &field,
                       const QString &subject, QList<Diagnostic> &diagnostics) {
  const QJsonValue value = object.value(field);
  if (!value.isString() || value.toString().trimmed().isEmpty()) {
    diagnostics.append(importError(
        QStringLiteral("%1 must contain non-empty text").arg(field), subject));
    return {};
  }
  return value.toString().trimmed();
}

QString optionalString(const QJsonObject &object, const QString &field,
                       const QString &subject, QList<Diagnostic> &diagnostics) {
  const QJsonValue value = object.value(field);
  if (value.isUndefined() || value.isNull())
    return {};
  if (!value.isString()) {
    diagnostics.append(importError(
        QStringLiteral("%1 must be text when present").arg(field), subject));
    return {};
  }
  return value.toString().trimmed();
}

qreal optionalNumber(const QJsonObject &object, const QString &field,
                     qreal fallback, const QString &subject,
                     QList<Diagnostic> &diagnostics) {
  const QJsonValue value = object.value(field);
  if (value.isUndefined())
    return fallback;
  const qreal number = value.toDouble(std::numeric_limits<qreal>::quiet_NaN());
  if (!value.isDouble() || !std::isfinite(number)) {
    diagnostics.append(importError(
        QStringLiteral("%1 must be a finite number").arg(field), subject));
    return fallback;
  }
  return number;
}

int optionalNonNegativeInteger(const QJsonObject &object, const QString &field,
                               const QString &subject,
                               QList<Diagnostic> &diagnostics) {
  const QJsonValue value = object.value(field);
  if (value.isUndefined())
    return 0;
  const double number = value.toDouble(-1.0);
  if (!value.isDouble() || !std::isfinite(number) || number < 0.0 ||
      number != std::floor(number) ||
      number > std::numeric_limits<int>::max()) {
    diagnostics.append(importError(
        QStringLiteral("%1 must be a non-negative integer").arg(field),
        subject));
    return 0;
  }
  return static_cast<int>(number);
}

QJsonArray arrayField(const QJsonObject &object, const QString &field,
                      const QString &subject, QList<Diagnostic> &diagnostics,
                      bool required = false) {
  const QJsonValue value = object.value(field);
  if (value.isUndefined()) {
    if (required)
      diagnostics.append(importError(
          QStringLiteral("%1 must be an array").arg(field), subject));
    return {};
  }
  if (!value.isArray()) {
    diagnostics.append(
        importError(QStringLiteral("%1 must be an array").arg(field), subject));
    return {};
  }
  return value.toArray();
}

const ModelElement *resolveElement(const ProjectData &project,
                                   const QString &reference,
                                   QList<Diagnostic> &diagnostics,
                                   const QString &participantKey) {
  QList<const ModelElement *> matches;
  for (const auto &element : project.elements) {
    if (element.id == reference || element.name == reference ||
        presentation_layout::fullyQualifiedElementName(project, element) ==
            reference)
      matches.append(&element);
  }
  QSet<QString> uniqueIds;
  QList<const ModelElement *> uniqueMatches;
  for (const ModelElement *match : matches) {
    if (!uniqueIds.contains(match->id)) {
      uniqueIds.insert(match->id);
      uniqueMatches.append(match);
    }
  }
  if (uniqueMatches.size() == 1)
    return uniqueMatches.first();
  if (uniqueMatches.isEmpty()) {
    diagnostics.append(importError(
        QStringLiteral("Participant '%1' references unknown model element '%2'")
            .arg(participantKey, reference),
        participantKey));
    return nullptr;
  }

  QStringList candidates;
  for (const ModelElement *match : uniqueMatches) {
    candidates.append(QStringLiteral("%1 [%2]").arg(
        presentation_layout::fullyQualifiedElementName(project, *match),
        match->id));
  }
  diagnostics.append(importError(
      QStringLiteral("Participant '%1' element reference '%2' is ambiguous: %3")
          .arg(participantKey, reference,
               candidates.join(QStringLiteral(", "))),
      participantKey));
  return nullptr;
}

template <typename T>
bool rememberUniqueKey(const QString &key, const QString &kind,
                       QHash<QString, T> &byKey,
                       QList<Diagnostic> &diagnostics) {
  if (byKey.contains(key)) {
    diagnostics.append(importError(
        QStringLiteral("Duplicate %1 key '%2'").arg(kind, key), key));
    return false;
  }
  return true;
}

} // namespace

SequenceDiagramImportOutcome
SequenceDiagramImportService::parse(const QByteArray &contents,
                                    const ProjectData &project) {
  SequenceDiagramImportOutcome outcome;
  const Json5Result parsed = Json5::parse(contents);
  if (!parsed) {
    outcome.diagnostics.append(importError(
        QStringLiteral("Cannot parse sequence JSON5: %1").arg(parsed.error)));
    return outcome;
  }
  if (!parsed.document.isObject()) {
    outcome.diagnostics.append(
        importError(QStringLiteral("Sequence import root must be an object")));
    return outcome;
  }

  const QJsonObject root = parsed.document.object();
  if (root.value(QStringLiteral("format")).toString() !=
      QStringLiteral("yauml-sequence"))
    outcome.diagnostics.append(
        importError(QStringLiteral("format must be 'yauml-sequence'")));
  const QJsonValue version = root.value(QStringLiteral("version"));
  if (!version.isDouble() || version.toInt(-1) != 1 ||
      version.toDouble() != 1.0)
    outcome.diagnostics.append(importError(
        QStringLiteral("Only sequence import version 1 is supported")));
  if (!root.value(QStringLiteral("diagram")).isObject()) {
    outcome.diagnostics.append(
        importError(QStringLiteral("diagram must be an object")));
    return outcome;
  }

  const QJsonObject sourceDiagram =
      root.value(QStringLiteral("diagram")).toObject();
  Diagram diagram;
  diagram.id = newId();
  diagram.type = DiagramType::Sequence;
  diagram.name = requiredString(sourceDiagram, QStringLiteral("name"),
                                QStringLiteral("diagram"), outcome.diagnostics);
  const QString importKey =
      optionalString(sourceDiagram, QStringLiteral("key"),
                     QStringLiteral("diagram"), outcome.diagnostics);
  if (!importKey.isEmpty()) {
    diagram.extra.insert(QStringLiteral("sequenceImportKey"), importKey);
    for (const auto &existing : project.diagrams) {
      if (existing.extra.value(QStringLiteral("sequenceImportKey"))
              .toString() == importKey)
        outcome.diagnostics.append(importError(
            QStringLiteral("A diagram imported with key '%1' already exists; "
                           "replacement is intentionally not implicit")
                .arg(importKey),
            importKey));
    }
  }

  QHash<QString, QString> contextIdByKey;
  const QJsonArray contexts =
      arrayField(sourceDiagram, QStringLiteral("contexts"),
                 QStringLiteral("diagram"), outcome.diagnostics);
  for (int index = 0; index < contexts.size(); ++index) {
    if (!contexts.at(index).isObject()) {
      outcome.diagnostics.append(importError(
          QStringLiteral("contexts[%1] must be an object").arg(index)));
      continue;
    }
    const QJsonObject item = contexts.at(index).toObject();
    SequenceExecutionContext context;
    context.id = newId();
    context.key =
        requiredString(item, QStringLiteral("key"), QStringLiteral("context"),
                       outcome.diagnostics);
    context.name = requiredString(item, QStringLiteral("name"), context.key,
                                  outcome.diagnostics);
    bool typeOk = false;
    context.type = sequenceExecutionContextTypeFromString(
        requiredString(item, QStringLiteral("kind"), context.key,
                       outcome.diagnostics),
        &typeOk);
    if (!typeOk)
      outcome.diagnostics.append(importError(
          QStringLiteral("Context '%1' has an unknown kind").arg(context.key),
          context.key));
    context.extra =
        withoutKeys(item, {QStringLiteral("key"), QStringLiteral("name"),
                           QStringLiteral("kind")});
    if (!context.key.isEmpty() &&
        rememberUniqueKey(context.key, QStringLiteral("context"),
                          contextIdByKey, outcome.diagnostics)) {
      contextIdByKey.insert(context.key, context.id);
    }
    diagram.sequenceExecutionContexts.append(std::move(context));
  }

  QHash<QString, QString> participantIdByKey;
  const QJsonArray participants =
      arrayField(sourceDiagram, QStringLiteral("participants"),
                 QStringLiteral("diagram"), outcome.diagnostics, true);
  for (int index = 0; index < participants.size(); ++index) {
    if (!participants.at(index).isObject()) {
      outcome.diagnostics.append(importError(
          QStringLiteral("participants[%1] must be an object").arg(index)));
      continue;
    }
    const QJsonObject item = participants.at(index).toObject();
    SequenceParticipant participant;
    participant.id = newId();
    participant.key =
        requiredString(item, QStringLiteral("key"),
                       QStringLiteral("participant"), outcome.diagnostics);
    const QString elementReference = optionalString(
        item, QStringLiteral("element"), participant.key, outcome.diagnostics);
    const ModelElement *element = nullptr;
    if (!elementReference.isEmpty())
      element = resolveElement(project, elementReference, outcome.diagnostics,
                               participant.key);
    participant.elementId = element ? element->id : QString{};
    participant.name = optionalString(item, QStringLiteral("name"),
                                      participant.key, outcome.diagnostics);
    if (participant.name.isEmpty() && element)
      participant.name = element->name.section(QStringLiteral("::"), -1);
    if (participant.name.isEmpty())
      outcome.diagnostics.append(importError(
          QStringLiteral("Participant '%1' needs name or resolvable element")
              .arg(participant.key),
          participant.key));
    const QString contextKey = optionalString(
        item, QStringLiteral("context"), participant.key, outcome.diagnostics);
    if (!contextKey.isEmpty()) {
      if (!contextIdByKey.contains(contextKey))
        outcome.diagnostics.append(importError(
            QStringLiteral("Participant '%1' references unknown context '%2'")
                .arg(participant.key, contextKey),
            participant.key));
      participant.defaultExecutionContextId = contextIdByKey.value(contextKey);
    }
    const qreal x =
        optionalNumber(item, QStringLiteral("x"),
                       kFirstParticipantX + index * kParticipantSpacing,
                       participant.key, outcome.diagnostics);
    const qreal width =
        optionalNumber(item, QStringLiteral("width"), kParticipantHeaderWidth,
                       participant.key, outcome.diagnostics);
    participant.headerGeometry =
        QRectF(x, kParticipantHeaderY, width, kParticipantHeaderHeight);
    participant.extra =
        withoutKeys(item, {QStringLiteral("key"), QStringLiteral("name"),
                           QStringLiteral("element"), QStringLiteral("context"),
                           QStringLiteral("x"), QStringLiteral("width")});
    if (!participant.key.isEmpty() &&
        rememberUniqueKey(participant.key, QStringLiteral("participant"),
                          participantIdByKey, outcome.diagnostics)) {
      participantIdByKey.insert(participant.key, participant.id);
    }
    diagram.sequenceParticipants.append(std::move(participant));
  }

  QHash<QString, QString> messageIdByKey;
  QHash<QString, int> messageOrderByKey;
  const QJsonArray messages =
      arrayField(sourceDiagram, QStringLiteral("messages"),
                 QStringLiteral("diagram"), outcome.diagnostics, true);
  qreal previousY = kFirstMessageY - kMessageSpacing;
  for (int index = 0; index < messages.size(); ++index) {
    if (!messages.at(index).isObject()) {
      outcome.diagnostics.append(importError(
          QStringLiteral("messages[%1] must be an object").arg(index)));
      continue;
    }
    const QJsonObject item = messages.at(index).toObject();
    SequenceMessage message;
    message.id = newId();
    message.key =
        optionalString(item, QStringLiteral("key"), QStringLiteral("message"),
                       outcome.diagnostics);
    if (message.key.isEmpty())
      message.key = QStringLiteral("message-%1").arg(index + 1);
    const QString sourceKey = requiredString(item, QStringLiteral("from"),
                                             message.key, outcome.diagnostics);
    const QString targetKey = requiredString(item, QStringLiteral("to"),
                                             message.key, outcome.diagnostics);
    if (!participantIdByKey.contains(sourceKey))
      outcome.diagnostics.append(importError(
          QStringLiteral("Message '%1' references unknown participant '%2'")
              .arg(message.key, sourceKey),
          message.key));
    if (!participantIdByKey.contains(targetKey))
      outcome.diagnostics.append(importError(
          QStringLiteral("Message '%1' references unknown participant '%2'")
              .arg(message.key, targetKey),
          message.key));
    message.sourceParticipantId = participantIdByKey.value(sourceKey);
    message.targetParticipantId = participantIdByKey.value(targetKey);
    bool typeOk = false;
    message.type = sequenceMessageTypeFromString(
        requiredString(item, QStringLiteral("kind"), message.key,
                       outcome.diagnostics),
        &typeOk);
    if (!typeOk)
      outcome.diagnostics.append(importError(
          QStringLiteral("Message '%1' has an unknown kind").arg(message.key),
          message.key));
    message.text = optionalString(item, QStringLiteral("text"), message.key,
                                  outcome.diagnostics);
    message.y = optionalNumber(item, QStringLiteral("y"),
                               kFirstMessageY + index * kMessageSpacing,
                               message.key, outcome.diagnostics);
    if (message.y <= previousY)
      outcome.diagnostics.append(importError(
          QStringLiteral("Message '%1' must be below the preceding message")
              .arg(message.key),
          message.key));
    previousY = message.y;

    const QJsonValue sourceValue = item.value(QStringLiteral("source"));
    if (!sourceValue.isUndefined() && !sourceValue.isObject()) {
      outcome.diagnostics.append(
          importError(QStringLiteral("Message '%1' source must be an object")
                          .arg(message.key),
                      message.key));
    } else {
      const QJsonObject source = sourceValue.toObject();
      message.evidence.file = optionalString(source, QStringLiteral("file"),
                                             message.key, outcome.diagnostics);
      message.evidence.line = optionalNonNegativeInteger(
          source, QStringLiteral("line"), message.key, outcome.diagnostics);
      message.evidence.column = optionalNonNegativeInteger(
          source, QStringLiteral("column"), message.key, outcome.diagnostics);
      const QString confidence =
          optionalString(source, QStringLiteral("confidence"), message.key,
                         outcome.diagnostics);
      if (!confidence.isEmpty()) {
        bool confidenceOk = false;
        message.evidence.confidence =
            sequenceEvidenceConfidenceFromString(confidence, &confidenceOk);
        if (!confidenceOk)
          outcome.diagnostics.append(
              importError(QStringLiteral("Message '%1' source confidence must "
                                         "be confirmed or inferred")
                              .arg(message.key),
                          message.key));
      }
      message.evidence.note = optionalString(source, QStringLiteral("note"),
                                             message.key, outcome.diagnostics);
      message.evidence.extra = withoutKeys(
          source, {QStringLiteral("file"), QStringLiteral("line"),
                   QStringLiteral("column"), QStringLiteral("confidence"),
                   QStringLiteral("note")});
    }
    message.extra =
        withoutKeys(item, {QStringLiteral("key"), QStringLiteral("from"),
                           QStringLiteral("to"), QStringLiteral("kind"),
                           QStringLiteral("text"), QStringLiteral("y"),
                           QStringLiteral("source")});
    if (rememberUniqueKey(message.key, QStringLiteral("message"),
                          messageIdByKey, outcome.diagnostics)) {
      messageIdByKey.insert(message.key, message.id);
      messageOrderByKey.insert(message.key, index);
    }
    diagram.sequenceMessages.append(std::move(message));
  }

  QHash<QString, QString> activationIdByKey;
  const QJsonArray activations =
      arrayField(sourceDiagram, QStringLiteral("activations"),
                 QStringLiteral("diagram"), outcome.diagnostics);
  for (int index = 0; index < activations.size(); ++index) {
    if (!activations.at(index).isObject()) {
      outcome.diagnostics.append(importError(
          QStringLiteral("activations[%1] must be an object").arg(index)));
      continue;
    }
    const QJsonObject item = activations.at(index).toObject();
    SequenceActivation activation;
    activation.id = newId();
    activation.key =
        optionalString(item, QStringLiteral("key"),
                       QStringLiteral("activation"), outcome.diagnostics);
    if (activation.key.isEmpty())
      activation.key = QStringLiteral("activation-%1").arg(index + 1);
    const QString participantKey =
        requiredString(item, QStringLiteral("participant"), activation.key,
                       outcome.diagnostics);
    const QString contextKey = optionalString(
        item, QStringLiteral("context"), activation.key, outcome.diagnostics);
    const QString startKey = requiredString(
        item, QStringLiteral("start"), activation.key, outcome.diagnostics);
    const QString endKey = optionalString(item, QStringLiteral("end"),
                                          activation.key, outcome.diagnostics);
    if (!participantIdByKey.contains(participantKey))
      outcome.diagnostics.append(importError(
          QStringLiteral("Activation '%1' references unknown participant '%2'")
              .arg(activation.key, participantKey),
          activation.key));
    if (!contextKey.isEmpty() && !contextIdByKey.contains(contextKey))
      outcome.diagnostics.append(importError(
          QStringLiteral("Activation '%1' references unknown context '%2'")
              .arg(activation.key, contextKey),
          activation.key));
    if (!messageIdByKey.contains(startKey))
      outcome.diagnostics.append(importError(
          QStringLiteral(
              "Activation '%1' references unknown start message '%2'")
              .arg(activation.key, startKey),
          activation.key));
    if (!endKey.isEmpty() && !messageIdByKey.contains(endKey))
      outcome.diagnostics.append(importError(
          QStringLiteral("Activation '%1' references unknown end message '%2'")
              .arg(activation.key, endKey),
          activation.key));
    if (!endKey.isEmpty() && messageOrderByKey.contains(startKey) &&
        messageOrderByKey.contains(endKey) &&
        messageOrderByKey.value(endKey) < messageOrderByKey.value(startKey))
      outcome.diagnostics.append(
          importError(QStringLiteral("Activation '%1' ends before it starts")
                          .arg(activation.key),
                      activation.key));
    activation.participantId = participantIdByKey.value(participantKey);
    activation.executionContextId = contextIdByKey.value(contextKey);
    activation.startMessageId = messageIdByKey.value(startKey);
    activation.endMessageId = messageIdByKey.value(endKey);
    activation.label = optionalString(item, QStringLiteral("label"),
                                      activation.key, outcome.diagnostics);
    activation.extra =
        withoutKeys(item, {QStringLiteral("key"), QStringLiteral("participant"),
                           QStringLiteral("context"), QStringLiteral("start"),
                           QStringLiteral("end"), QStringLiteral("label")});
    if (rememberUniqueKey(activation.key, QStringLiteral("activation"),
                          activationIdByKey, outcome.diagnostics))
      activationIdByKey.insert(activation.key, activation.id);
    diagram.sequenceActivations.append(std::move(activation));
  }

  const QJsonArray fragments =
      arrayField(sourceDiagram, QStringLiteral("fragments"),
                 QStringLiteral("diagram"), outcome.diagnostics);
  QHash<QString, QString> fragmentIdByKey;
  for (int index = 0; index < fragments.size(); ++index) {
    if (!fragments.at(index).isObject()) {
      outcome.diagnostics.append(importError(
          QStringLiteral("fragments[%1] must be an object").arg(index)));
      continue;
    }
    const QJsonObject item = fragments.at(index).toObject();
    SequenceCombinedFragment fragment;
    fragment.id = newId();
    fragment.key =
        optionalString(item, QStringLiteral("key"), QStringLiteral("fragment"),
                       outcome.diagnostics);
    if (fragment.key.isEmpty())
      fragment.key = QStringLiteral("fragment-%1").arg(index + 1);
    bool typeOk = false;
    fragment.type = sequenceCombinedFragmentTypeFromString(
        requiredString(item, QStringLiteral("kind"), fragment.key,
                       outcome.diagnostics),
        &typeOk);
    if (!typeOk)
      outcome.diagnostics.append(importError(
          QStringLiteral("Fragment '%1' has an unknown kind").arg(fragment.key),
          fragment.key));
    const QJsonArray participantKeys =
        arrayField(item, QStringLiteral("participants"), fragment.key,
                   outcome.diagnostics, true);
    QSet<QString> usedParticipants;
    for (const QJsonValue &value : participantKeys) {
      const QString participantKey = value.toString();
      if (!participantIdByKey.contains(participantKey))
        outcome.diagnostics.append(importError(
            QStringLiteral("Fragment '%1' references unknown participant '%2'")
                .arg(fragment.key, participantKey),
            fragment.key));
      if (usedParticipants.contains(participantKey))
        outcome.diagnostics.append(
            importError(QStringLiteral("Fragment '%1' repeats participant '%2'")
                            .arg(fragment.key, participantKey),
                        fragment.key));
      usedParticipants.insert(participantKey);
      fragment.participantIds.append(participantIdByKey.value(participantKey));
    }
    const QString startKey = requiredString(item, QStringLiteral("start"),
                                            fragment.key, outcome.diagnostics);
    const QString endKey = requiredString(item, QStringLiteral("end"),
                                          fragment.key, outcome.diagnostics);
    if (!messageIdByKey.contains(startKey) || !messageIdByKey.contains(endKey))
      outcome.diagnostics.append(importError(
          QStringLiteral("Fragment '%1' references an unknown boundary message")
              .arg(fragment.key),
          fragment.key));
    else if (messageOrderByKey.value(endKey) <
             messageOrderByKey.value(startKey))
      outcome.diagnostics.append(
          importError(QStringLiteral("Fragment '%1' ends before it starts")
                          .arg(fragment.key),
                      fragment.key));
    fragment.startMessageId = messageIdByKey.value(startKey);
    fragment.endMessageId = messageIdByKey.value(endKey);
    const bool multipleOperands =
        sequenceCombinedFragmentHasSecondOperand(fragment.type);
    if (item.contains(QStringLiteral("operands"))) {
      const QJsonArray operands =
          arrayField(item, QStringLiteral("operands"), fragment.key,
                     outcome.diagnostics, true);
      int previousOrder = -1;
      for (int operandIndex = 0; operandIndex < operands.size();
           ++operandIndex) {
        if (!operands.at(operandIndex).isObject()) {
          outcome.diagnostics.append(importError(
              QStringLiteral("Fragment '%1' operand %2 must be an object")
                  .arg(fragment.key)
                  .arg(operandIndex + 1),
              fragment.key));
          continue;
        }
        const QJsonObject operandObject = operands.at(operandIndex).toObject();
        const QString operandStartKey =
            requiredString(operandObject, QStringLiteral("start"), fragment.key,
                           outcome.diagnostics);
        SequenceCombinedFragmentOperand operand;
        operand.startMessageId = messageIdByKey.value(operandStartKey);
        operand.guard = optionalString(operandObject, QStringLiteral("guard"),
                                       fragment.key, outcome.diagnostics);
        operand.extra = withoutKeys(
            operandObject, {QStringLiteral("start"), QStringLiteral("guard")});
        if (!messageIdByKey.contains(operandStartKey)) {
          outcome.diagnostics.append(importError(
              QStringLiteral("Fragment '%1' operand %2 references unknown "
                             "message '%3'")
                  .arg(fragment.key)
                  .arg(operandIndex + 1)
                  .arg(operandStartKey),
              fragment.key));
        } else {
          const int order = messageOrderByKey.value(operandStartKey);
          if (order < messageOrderByKey.value(startKey) ||
              order > messageOrderByKey.value(endKey) || order <= previousOrder)
            outcome.diagnostics.append(importError(
                QStringLiteral("Fragment '%1' operands must begin in strict "
                               "message order inside its span")
                    .arg(fragment.key),
                fragment.key));
          previousOrder = order;
        }
        fragment.operands.append(std::move(operand));
      }
    } else {
      // Version-one interchange files used fixed first/second fields. Keep
      // accepting them so generated proposals remain forward compatible.
      fragment.operands.append(
          {fragment.startMessageId,
           optionalString(item, QStringLiteral("guard"), fragment.key,
                          outcome.diagnostics),
           {}});
      if (multipleOperands) {
        const QString alternativeStartKey =
            requiredString(item, QStringLiteral("alternativeStart"),
                           fragment.key, outcome.diagnostics);
        fragment.operands.append(
            {messageIdByKey.value(alternativeStartKey),
             optionalString(item, QStringLiteral("alternativeGuard"),
                            fragment.key, outcome.diagnostics),
             {}});
      }
    }
    const qsizetype minimumOperandCount = multipleOperands ? 2 : 1;
    if (fragment.operands.size() < minimumOperandCount ||
        (!multipleOperands && fragment.operands.size() != 1))
      outcome.diagnostics.append(importError(
          QStringLiteral("%1 fragment '%2' needs %3")
              .arg(toString(fragment.type), fragment.key,
                   multipleOperands ? QStringLiteral("at least two operands")
                                    : QStringLiteral("exactly one operand")),
          fragment.key));
    if (!fragment.operands.isEmpty() &&
        fragment.operands.constFirst().startMessageId !=
            fragment.startMessageId)
      outcome.diagnostics.append(importError(
          QStringLiteral("Fragment '%1' first operand must begin at its start")
              .arg(fragment.key),
          fragment.key));
    fragment.extra = withoutKeys(
        item, {QStringLiteral("key"), QStringLiteral("kind"),
               QStringLiteral("participants"), QStringLiteral("start"),
               QStringLiteral("end"), QStringLiteral("guard"),
               QStringLiteral("alternativeStart"),
               QStringLiteral("alternativeGuard"), QStringLiteral("operands")});
    if (rememberUniqueKey(fragment.key, QStringLiteral("fragment"),
                          fragmentIdByKey, outcome.diagnostics))
      fragmentIdByKey.insert(fragment.key, fragment.id);
    diagram.sequenceCombinedFragments.append(std::move(fragment));
  }

  diagram.sequenceExtra =
      withoutKeys(sourceDiagram,
                  {QStringLiteral("key"), QStringLiteral("name"),
                   QStringLiteral("contexts"), QStringLiteral("participants"),
                   QStringLiteral("messages"), QStringLiteral("activations"),
                   QStringLiteral("fragments")});
  outcome.diagram = std::move(diagram);

  ProjectData candidate = project;
  candidate.diagrams.append(outcome.diagram);
  outcome.diagnostics.append(ProjectSerializer::validate(candidate));
  outcome.ok = !hasErrors(outcome.diagnostics);
  return outcome;
}

SequenceDiagramImportOutcome
SequenceDiagramImportService::load(const QString &filePath,
                                   const ProjectData &project) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    SequenceDiagramImportOutcome outcome;
    outcome.diagnostics.append(
        importError(QStringLiteral("Cannot read %1: %2")
                        .arg(filePath, file.errorString())));
    return outcome;
  }
  return parse(file.readAll(), project);
}

bool SequenceDiagramImportService::append(
    ProjectData &project, const SequenceDiagramImportOutcome &outcome,
    QList<Diagnostic> *diagnostics) {
  if (!outcome.ok) {
    if (diagnostics)
      diagnostics->append(importError(QStringLiteral(
          "Sequence import was not applied because validation failed")));
    return false;
  }
  project.diagrams.append(outcome.diagram);
  return true;
}

} // namespace yauml
