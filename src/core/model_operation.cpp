#include "core/model_operation.h"

#include <algorithm>

namespace yauml {
namespace {

QString visibilitySymbol(MemberVisibility visibility) {
  switch (visibility) {
  case MemberVisibility::Public:
    return QStringLiteral("+");
  case MemberVisibility::Protected:
    return QStringLiteral("#");
  case MemberVisibility::Private:
    return QStringLiteral("-");
  case MemberVisibility::Package:
    return QStringLiteral("~");
  }
  return QStringLiteral("+");
}

MemberVisibility visibilityFromSymbol(QChar symbol, bool *recognized) {
  *recognized = true;
  switch (symbol.unicode()) {
  case '+':
    return MemberVisibility::Public;
  case '#':
    return MemberVisibility::Protected;
  case '-':
    return MemberVisibility::Private;
  case '~':
    return MemberVisibility::Package;
  default:
    *recognized = false;
    return MemberVisibility::Public;
  }
}

QStringList splitTopLevel(const QString &text, QChar delimiter) {
  QStringList parts;
  int angleDepth = 0;
  int parenthesisDepth = 0;
  int bracketDepth = 0;
  int braceDepth = 0;
  int start = 0;
  for (int index = 0; index < text.size(); ++index) {
    const QChar character = text.at(index);
    switch (character.unicode()) {
    case '<':
      ++angleDepth;
      break;
    case '>':
      angleDepth = std::max(0, angleDepth - 1);
      break;
    case '(':
      ++parenthesisDepth;
      break;
    case ')':
      parenthesisDepth = std::max(0, parenthesisDepth - 1);
      break;
    case '[':
      ++bracketDepth;
      break;
    case ']':
      bracketDepth = std::max(0, bracketDepth - 1);
      break;
    case '{':
      ++braceDepth;
      break;
    case '}':
      braceDepth = std::max(0, braceDepth - 1);
      break;
    default:
      break;
    }
    if (character == delimiter && angleDepth == 0 && parenthesisDepth == 0 &&
        bracketDepth == 0 && braceDepth == 0) {
      parts.append(text.mid(start, index - start).trimmed());
      start = index + 1;
    }
  }
  parts.append(text.mid(start).trimmed());
  return parts;
}

int topLevelDelimiter(const QString &text, QChar delimiter) {
  int angleDepth = 0;
  int parenthesisDepth = 0;
  int bracketDepth = 0;
  int braceDepth = 0;
  for (int index = 0; index < text.size(); ++index) {
    const QChar character = text.at(index);
    if (character == delimiter && angleDepth == 0 && parenthesisDepth == 0 &&
        bracketDepth == 0 && braceDepth == 0)
      return index;
    switch (character.unicode()) {
    case '<':
      ++angleDepth;
      break;
    case '>':
      angleDepth = std::max(0, angleDepth - 1);
      break;
    case '(':
      ++parenthesisDepth;
      break;
    case ')':
      parenthesisDepth = std::max(0, parenthesisDepth - 1);
      break;
    case '[':
      ++bracketDepth;
      break;
    case ']':
      bracketDepth = std::max(0, bracketDepth - 1);
      break;
    case '{':
      ++braceDepth;
      break;
    case '}':
      braceDepth = std::max(0, braceDepth - 1);
      break;
    default:
      break;
    }
  }
  return -1;
}

QPair<int, int> operationParameterRange(const QString &text) {
  QList<QPair<int, int>> topLevelPairs;
  QList<int> openingPositions;
  int angleDepth = 0;
  int bracketDepth = 0;
  int braceDepth = 0;
  for (int index = 0; index < text.size(); ++index) {
    const QChar character = text.at(index);
    if (character == u'<' && openingPositions.isEmpty())
      ++angleDepth;
    else if (character == u'>' && openingPositions.isEmpty())
      angleDepth = std::max(0, angleDepth - 1);
    else if (character == u'[')
      ++bracketDepth;
    else if (character == u']')
      bracketDepth = std::max(0, bracketDepth - 1);
    else if (character == u'{')
      ++braceDepth;
    else if (character == u'}')
      braceDepth = std::max(0, braceDepth - 1);
    else if (character == u'(' && angleDepth == 0 && bracketDepth == 0 &&
             braceDepth == 0)
      openingPositions.append(index);
    else if (character == u')' && !openingPositions.isEmpty()) {
      const int opening = openingPositions.takeLast();
      if (openingPositions.isEmpty())
        topLevelPairs.append({opening, index});
    }
  }
  return topLevelPairs.isEmpty() ? QPair<int, int>{-1, -1}
                                 : topLevelPairs.constLast();
}

OperationParameter parseParameter(const QString &source) {
  OperationParameter parameter;
  QString text = source.trimmed();
  const int defaultDelimiter = topLevelDelimiter(text, u'=');
  if (defaultDelimiter >= 0) {
    parameter.defaultValue = text.mid(defaultDelimiter + 1).trimmed();
    text = text.left(defaultDelimiter).trimmed();
  }

  int nameTypeDelimiter = -1;
  int angleDepth = 0;
  int parenthesisDepth = 0;
  int bracketDepth = 0;
  for (int index = 0; index < text.size(); ++index) {
    const QChar character = text.at(index);
    if (character == u'<')
      ++angleDepth;
    else if (character == u'>')
      angleDepth = std::max(0, angleDepth - 1);
    else if (character == u'(')
      ++parenthesisDepth;
    else if (character == u')')
      parenthesisDepth = std::max(0, parenthesisDepth - 1);
    else if (character == u'[')
      ++bracketDepth;
    else if (character == u']')
      bracketDepth = std::max(0, bracketDepth - 1);
    else if (character == u':' && angleDepth == 0 && parenthesisDepth == 0 &&
             bracketDepth == 0 && (index == 0 || text.at(index - 1) != u':') &&
             (index + 1 >= text.size() || text.at(index + 1) != u':')) {
      nameTypeDelimiter = index;
      break;
    }
  }
  if (nameTypeDelimiter < 0) {
    parameter.type = text;
    return parameter;
  }

  QString name = text.left(nameTypeDelimiter).trimmed();
  parameter.type = text.mid(nameTypeDelimiter + 1).trimmed();
  for (const QString &direction :
       {QStringLiteral("inout"), QStringLiteral("out"), QStringLiteral("in")}) {
    if (name.startsWith(direction + u' ', Qt::CaseInsensitive)) {
      parameter.direction = direction;
      name = name.mid(direction.size() + 1).trimmed();
      break;
    }
  }
  parameter.name = name;
  return parameter;
}

QString parameterSignature(const OperationParameter &parameter) {
  QString text;
  if (!parameter.direction.isEmpty())
    text += parameter.direction + u' ';
  if (!parameter.name.isEmpty())
    text += parameter.name + QStringLiteral(": ");
  text += parameter.type;
  if (!parameter.defaultValue.isEmpty())
    text += QStringLiteral(" = ") + parameter.defaultValue;
  return text.trimmed();
}

void extractTrailingModifiers(QString &suffix, QStringList &modifiers) {
  const auto takeSuffix = [&](const QString &token, const QString &modifier) {
    if (!suffix.endsWith(token, Qt::CaseInsensitive))
      return false;
    const int start = suffix.size() - token.size();
    if (start > 0 && !suffix.at(start - 1).isSpace())
      return false;
    suffix = suffix.left(start).trimmed();
    modifiers.prepend(modifier);
    return true;
  };

  bool changed = true;
  while (changed) {
    changed = false;
    if (suffix.endsWith(QStringLiteral("{static}"), Qt::CaseInsensitive)) {
      suffix.chop(8);
      suffix = suffix.trimmed();
      modifiers.prepend(QStringLiteral("static"));
      changed = true;
    } else if (suffix.endsWith(QStringLiteral("= 0"))) {
      suffix.chop(3);
      suffix = suffix.trimmed();
      modifiers.prepend(QStringLiteral("abstract"));
      changed = true;
    } else {
      for (const QString &token :
           {QStringLiteral("final"), QStringLiteral("override"),
            QStringLiteral("noexcept"), QStringLiteral("const")}) {
        if (takeSuffix(token, token)) {
          changed = true;
          break;
        }
      }
    }
  }
}

QString modifierSignature(const QString &modifier) {
  if (modifier.compare(QStringLiteral("static"), Qt::CaseInsensitive) == 0)
    return QStringLiteral("{static}");
  if (modifier.compare(QStringLiteral("abstract"), Qt::CaseInsensitive) == 0)
    return QStringLiteral("= 0");
  return modifier;
}

QJsonObject parameterToJson(const OperationParameter &parameter) {
  QJsonObject object = parameter.extra;
  if (!parameter.name.isEmpty())
    object.insert(QStringLiteral("name"), parameter.name);
  else
    object.remove(QStringLiteral("name"));
  object.insert(QStringLiteral("type"), parameter.type);
  if (!parameter.direction.isEmpty())
    object.insert(QStringLiteral("direction"), parameter.direction);
  else
    object.remove(QStringLiteral("direction"));
  if (!parameter.defaultValue.isEmpty())
    object.insert(QStringLiteral("defaultValue"), parameter.defaultValue);
  else
    object.remove(QStringLiteral("defaultValue"));
  return object;
}

QJsonObject withoutKeys(QJsonObject object,
                        std::initializer_list<QString> keys) {
  for (const QString &key : keys)
    object.remove(key);
  return object;
}

} // namespace

ModelOperation modelOperationFromSignature(const QString &signature,
                                           const QString &stableId) {
  ModelOperation operation;
  operation.id = stableId.isEmpty() ? newId() : stableId;
  const QString original = signature.trimmed();
  QString text = original;

  if (!text.isEmpty()) {
    bool recognized = false;
    operation.visibility = visibilityFromSymbol(text.front(), &recognized);
    if (recognized)
      text = text.mid(1).trimmed();
  }

  const auto [opening, closing] = operationParameterRange(text);
  if (opening < 0 || closing < opening) {
    operation.name = text.trimmed();
    operation.customSignature = original;
    return operation;
  }

  operation.name = text.left(opening).trimmed();
  const QString parameters = text.mid(opening + 1, closing - opening - 1);
  for (const QString &parameter : splitTopLevel(parameters, u',')) {
    if (!parameter.isEmpty())
      operation.parameters.append(parseParameter(parameter));
  }

  QString suffix = text.mid(closing + 1).trimmed();
  extractTrailingModifiers(suffix, operation.modifiers);
  if (suffix.startsWith(u':'))
    operation.returnType = suffix.mid(1).trimmed();
  else if (!suffix.isEmpty())
    operation.customSignature = original;

  const QString canonical =
      modelOperationSignature(operation, OperationSignatureMode::Full);
  if (operation.customSignature.isEmpty() && canonical != original)
    operation.customSignature = original;
  return operation;
}

ModelOperation modelOperationWithEditedSignature(const ModelOperation &existing,
                                                 const QString &signature) {
  ModelOperation edited = modelOperationFromSignature(signature, existing.id);
  // The compact text syntax does not encode constructor/destructor kind.
  // Editing a source-imported signature must not silently turn it into an
  // ordinary method.
  edited.kind = existing.kind;
  edited.sourceFile = existing.sourceFile;
  edited.sourceLine = existing.sourceLine;
  edited.sourceColumn = existing.sourceColumn;
  edited.sourceExtra = existing.sourceExtra;
  edited.extra = existing.extra;
  return edited;
}

QString modelOperationSignature(const ModelOperation &operation,
                                OperationSignatureMode mode) {
  if (mode == OperationSignatureMode::Full &&
      !operation.customSignature.isEmpty())
    return operation.customSignature;

  QString signature =
      visibilitySymbol(operation.visibility) + u' ' + operation.name;
  if (mode == OperationSignatureMode::NameOnly)
    return signature + QStringLiteral("()");

  if (mode == OperationSignatureMode::NameAndReturnType) {
    signature += QStringLiteral("()");
    if (!operation.returnType.isEmpty())
      signature += QStringLiteral(": ") + operation.returnType;
    return signature;
  }

  QStringList parameters;
  parameters.reserve(operation.parameters.size());
  for (const auto &parameter : operation.parameters)
    parameters.append(parameterSignature(parameter));
  signature += u'(';
  signature += parameters.join(QStringLiteral(", "));
  signature += u')';
  if (!operation.returnType.isEmpty())
    signature += QStringLiteral(": ") + operation.returnType;
  for (const QString &modifier : operation.modifiers)
    signature += u' ' + modifierSignature(modifier);
  return signature;
}

QStringList modelOperationSignatures(const QList<ModelOperation> &operations,
                                     OperationSignatureMode mode) {
  QStringList result;
  result.reserve(operations.size());
  for (const auto &operation : operations)
    result.append(modelOperationSignature(operation, mode));
  return result;
}

QString modelOperationsText(const QList<ModelOperation> &operations) {
  return modelOperationSignatures(operations).join(u'\n');
}

QList<ModelOperation>
modelOperationsFromText(const QString &text,
                        const QList<ModelOperation> &existing) {
  const QStringList lines = text.split(u'\n', Qt::SkipEmptyParts);
  QStringList normalizedLines;
  QList<ModelOperation> parsed;
  for (const QString &sourceLine : lines) {
    const QString line = sourceLine.trimmed();
    if (!line.isEmpty()) {
      normalizedLines.append(line);
      parsed.append(modelOperationFromSignature(line));
    }
  }

  QList<int> matches(parsed.size(), -1);
  QList<bool> claimed(existing.size(), false);
  const auto claimUnique = [&](qsizetype parsedIndex, const auto &predicate) {
    int match = -1;
    for (qsizetype existingIndex = 0; existingIndex < existing.size();
         ++existingIndex) {
      if (claimed.at(existingIndex) || !predicate(existing.at(existingIndex)))
        continue;
      if (match >= 0)
        return;
      match = static_cast<int>(existingIndex);
    }
    if (match >= 0) {
      matches[parsedIndex] = match;
      claimed[match] = true;
    }
  };

  // Exact semantic matches preserve IDs across arbitrary reorder/delete edits.
  for (qsizetype parsedIndex = 0; parsedIndex < parsed.size(); ++parsedIndex) {
    claimUnique(parsedIndex, [&](const ModelOperation &candidate) {
      return modelOperationSemanticallyEqual(parsed.at(parsedIndex), candidate);
    });
  }
  // A uniquely named operation is most likely an edited signature.
  for (qsizetype parsedIndex = 0; parsedIndex < parsed.size(); ++parsedIndex) {
    if (matches.at(parsedIndex) >= 0)
      continue;
    claimUnique(parsedIndex, [&](const ModelOperation &candidate) {
      return parsed.at(parsedIndex).name == candidate.name;
    });
  }
  // A same-sized replacement may include a rename. Position is the least
  // reliable evidence and is therefore used only after semantic/name matching.
  if (parsed.size() == existing.size()) {
    for (qsizetype parsedIndex = 0; parsedIndex < parsed.size();
         ++parsedIndex) {
      if (matches.at(parsedIndex) >= 0 || claimed.at(parsedIndex))
        continue;
      matches[parsedIndex] = static_cast<int>(parsedIndex);
      claimed[parsedIndex] = true;
    }
    qsizetype nextExisting = 0;
    for (qsizetype parsedIndex = 0; parsedIndex < parsed.size();
         ++parsedIndex) {
      if (matches.at(parsedIndex) >= 0)
        continue;
      while (nextExisting < existing.size() && claimed.at(nextExisting))
        ++nextExisting;
      if (nextExisting >= existing.size())
        break;
      matches[parsedIndex] = static_cast<int>(nextExisting);
      claimed[nextExisting] = true;
    }
  }

  QList<ModelOperation> result;
  result.reserve(parsed.size());
  for (qsizetype index = 0; index < parsed.size(); ++index) {
    const int existingIndex = matches.at(index);
    result.append(existingIndex >= 0 ? modelOperationWithEditedSignature(
                                           existing.at(existingIndex),
                                           normalizedLines.at(index))
                                     : parsed.at(index));
  }
  return result;
}

bool modelOperationSemanticallyEqual(const ModelOperation &left,
                                     const ModelOperation &right) {
  return left.name == right.name && left.visibility == right.visibility &&
         left.kind == right.kind && left.parameters == right.parameters &&
         left.returnType == right.returnType &&
         left.modifiers == right.modifiers &&
         left.customSignature == right.customSignature;
}

bool modelOperationsSemanticallyEqual(const QList<ModelOperation> &left,
                                      const QList<ModelOperation> &right) {
  if (left.size() != right.size())
    return false;
  for (qsizetype index = 0; index < left.size(); ++index) {
    if (!modelOperationSemanticallyEqual(left.at(index), right.at(index)))
      return false;
  }
  return true;
}

QJsonObject modelOperationToJson(const ModelOperation &operation) {
  QJsonObject object = operation.extra;
  object.insert(QStringLiteral("id"), operation.id);
  object.insert(QStringLiteral("name"), operation.name);
  object.insert(QStringLiteral("visibility"), toString(operation.visibility));
  if (operation.kind != OperationKind::Method)
    object.insert(QStringLiteral("kind"), toString(operation.kind));
  else
    object.remove(QStringLiteral("kind"));

  QJsonArray parameters;
  for (const auto &parameter : operation.parameters)
    parameters.append(parameterToJson(parameter));
  object.insert(QStringLiteral("parameters"), parameters);
  if (!operation.returnType.isEmpty())
    object.insert(QStringLiteral("returnType"), operation.returnType);
  else
    object.remove(QStringLiteral("returnType"));
  if (!operation.modifiers.isEmpty())
    object.insert(QStringLiteral("modifiers"),
                  QJsonArray::fromStringList(operation.modifiers));
  else
    object.remove(QStringLiteral("modifiers"));
  if (!operation.sourceFile.isEmpty() || operation.sourceLine > 0 ||
      operation.sourceColumn > 0 || !operation.sourceExtra.isEmpty()) {
    QJsonObject source = operation.sourceExtra;
    source.insert(QStringLiteral("file"), operation.sourceFile);
    source.insert(QStringLiteral("line"), operation.sourceLine);
    source.insert(QStringLiteral("column"), operation.sourceColumn);
    object.insert(QStringLiteral("source"), source);
  } else {
    object.remove(QStringLiteral("source"));
  }
  if (!operation.customSignature.isEmpty())
    object.insert(QStringLiteral("customSignature"), operation.customSignature);
  else
    object.remove(QStringLiteral("customSignature"));
  return object;
}

QJsonArray modelOperationsToJson(const QList<ModelOperation> &operations) {
  QJsonArray array;
  for (const auto &operation : operations)
    array.append(modelOperationToJson(operation));
  return array;
}

std::optional<ModelOperation> modelOperationFromJson(const QJsonValue &value,
                                                     QString *errorMessage) {
  if (!value.isObject()) {
    if (errorMessage)
      *errorMessage = QStringLiteral("An operation must be an object");
    return std::nullopt;
  }
  const QJsonObject object = value.toObject();
  ModelOperation operation;
  operation.id = object.value(QStringLiteral("id")).toString();
  operation.name = object.value(QStringLiteral("name")).toString();
  bool visibilityOk = false;
  operation.visibility = memberVisibilityFromString(
      object.value(QStringLiteral("visibility")).toString(), &visibilityOk);
  if (!visibilityOk) {
    if (errorMessage)
      *errorMessage = QStringLiteral("Operation visibility is invalid");
    return std::nullopt;
  }
  const QJsonValue kindValue = object.value(QStringLiteral("kind"));
  if (!kindValue.isUndefined()) {
    bool kindOk = false;
    operation.kind = operationKindFromString(kindValue.toString(), &kindOk);
    if (!kindOk) {
      if (errorMessage)
        *errorMessage = QStringLiteral("Operation kind is invalid");
      return std::nullopt;
    }
  }
  const QJsonValue parametersValue = object.value(QStringLiteral("parameters"));
  if (!parametersValue.isArray()) {
    if (errorMessage)
      *errorMessage = QStringLiteral("Operation parameters must be an array");
    return std::nullopt;
  }
  for (const QJsonValue &parameterValue : parametersValue.toArray()) {
    if (!parameterValue.isObject()) {
      if (errorMessage)
        *errorMessage =
            QStringLiteral("An operation parameter must be an object");
      return std::nullopt;
    }
    const QJsonObject parameterObject = parameterValue.toObject();
    OperationParameter parameter;
    parameter.name = parameterObject.value(QStringLiteral("name")).toString();
    parameter.type = parameterObject.value(QStringLiteral("type")).toString();
    parameter.direction =
        parameterObject.value(QStringLiteral("direction")).toString();
    parameter.defaultValue =
        parameterObject.value(QStringLiteral("defaultValue")).toString();
    parameter.extra = withoutKeys(
        parameterObject,
        {QStringLiteral("name"), QStringLiteral("type"),
         QStringLiteral("direction"), QStringLiteral("defaultValue")});
    operation.parameters.append(std::move(parameter));
  }
  operation.returnType = object.value(QStringLiteral("returnType")).toString();
  const QJsonValue modifiersValue = object.value(QStringLiteral("modifiers"));
  if (!modifiersValue.isUndefined() && !modifiersValue.isArray()) {
    if (errorMessage)
      *errorMessage = QStringLiteral("Operation modifiers must be an array");
    return std::nullopt;
  }
  for (const QJsonValue &modifier : modifiersValue.toArray()) {
    if (!modifier.isString()) {
      if (errorMessage)
        *errorMessage =
            QStringLiteral("An operation modifier must be a string");
      return std::nullopt;
    }
    operation.modifiers.append(modifier.toString());
  }
  const QJsonValue sourceValue = object.value(QStringLiteral("source"));
  if (!sourceValue.isUndefined() && !sourceValue.isObject()) {
    if (errorMessage)
      *errorMessage = QStringLiteral("Operation source must be an object");
    return std::nullopt;
  }
  const QJsonObject source = sourceValue.toObject();
  operation.sourceFile = source.value(QStringLiteral("file")).toString();
  operation.sourceLine = source.value(QStringLiteral("line")).toInt();
  operation.sourceColumn = source.value(QStringLiteral("column")).toInt();
  operation.sourceExtra =
      withoutKeys(source, {QStringLiteral("file"), QStringLiteral("line"),
                           QStringLiteral("column")});
  operation.customSignature =
      object.value(QStringLiteral("customSignature")).toString();
  operation.extra = withoutKeys(
      object, {QStringLiteral("id"), QStringLiteral("name"),
               QStringLiteral("visibility"), QStringLiteral("kind"),
               QStringLiteral("parameters"), QStringLiteral("returnType"),
               QStringLiteral("modifiers"), QStringLiteral("source"),
               QStringLiteral("customSignature")});
  return operation;
}

std::optional<QList<ModelOperation>>
modelOperationsFromJson(const QJsonValue &value, QString *errorMessage) {
  if (!value.isArray()) {
    if (errorMessage)
      *errorMessage = QStringLiteral("Element operations must be an array");
    return std::nullopt;
  }
  QList<ModelOperation> result;
  for (const QJsonValue &operationValue : value.toArray()) {
    auto operation = modelOperationFromJson(operationValue, errorMessage);
    if (!operation)
      return std::nullopt;
    result.append(std::move(*operation));
  }
  return result;
}

} // namespace yauml
