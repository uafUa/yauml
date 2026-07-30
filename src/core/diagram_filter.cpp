#include "core/diagram_filter.h"

#include "core/model_operation.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace yauml::diagram_filter {
namespace {

QStringList uniqueTrimmedValues(const QVariant &value) {
  QStringList result;
  QSet<QString> seen;
  for (const QVariant &entry : value.toList()) {
    const QString normalized = entry.toString().trimmed();
    if (normalized.isEmpty() || seen.contains(normalized))
      continue;
    result.append(normalized);
    seen.insert(normalized);
  }
  return result;
}

QRegularExpression wildcardExpression(const QString &pattern) {
  return QRegularExpression(
      QRegularExpression::wildcardToRegularExpression(pattern.trimmed()),
      QRegularExpression::CaseInsensitiveOption);
}

QString unqualifiedName(const QString &name) {
  return name.section(QStringLiteral("::"), -1);
}

QString memberName(const QString &declaration) {
  // UML member text commonly begins with an optional visibility marker,
  // followed by the field or operation identifier. Matching this token lets a
  // simple pattern such as `update*` work without requiring `*update*` merely
  // because a visibility symbol precedes it.
  static const QRegularExpression expression(
      QStringLiteral(R"(^\s*[+\-#~]?\s*([A-Za-z_]\w*))"));
  return expression.match(declaration).captured(1);
}

bool matchesWildcard(const QRegularExpression &expression,
                     const QStringList &values) {
  return std::any_of(values.cbegin(), values.cend(), [&](const QString &value) {
    return expression.match(value).hasMatch();
  });
}

QSet<QString> knownStereotypeIds(const ProjectData &project) {
  QSet<QString> result;
  result.reserve(project.stereotypeDefinitions.size());
  for (const auto &definition : project.stereotypeDefinitions)
    result.insert(definition.id);
  return result;
}

} // namespace

bool isActive(const DiagramFilter &filter) {
  return !filter.excludedElementTypes.isEmpty() ||
         !filter.includedStereotypeIds.isEmpty() ||
         !filter.excludedStereotypeIds.isEmpty() ||
         !filter.namePattern.trimmed().isEmpty() ||
         !filter.memberPattern.trimmed().isEmpty();
}

bool matchesElement(const ProjectData &project, const ModelElement &element,
                    const DiagramFilter &filter) {
  if (!isActive(filter))
    return true;

  if (filter.excludedElementTypes.contains(toString(element.type),
                                           Qt::CaseInsensitive))
    return false;

  const QSet<QString> knownIds = knownStereotypeIds(project);
  const auto effectiveIds = [&](const QStringList &configuredIds) {
    QStringList ids;
    for (const QString &id : configuredIds)
      if (knownIds.contains(id))
        ids.append(id);
    return ids;
  };
  const QStringList includedIds = effectiveIds(filter.includedStereotypeIds);
  const QStringList excludedIds = effectiveIds(filter.excludedStereotypeIds);
  if (!includedIds.isEmpty() &&
      std::none_of(includedIds.cbegin(), includedIds.cend(),
                   [&](const QString &id) {
                     return element.stereotypeIds.contains(id);
                   }))
    return false;
  if (std::any_of(excludedIds.cbegin(), excludedIds.cend(),
                  [&](const QString &id) {
                    return element.stereotypeIds.contains(id);
                  }))
    return false;

  if (!filter.namePattern.trimmed().isEmpty()) {
    const QRegularExpression expression =
        wildcardExpression(filter.namePattern);
    const bool match = matchesWildcard(
        expression, {element.name, unqualifiedName(element.name)});
    if (match == filter.excludeNameMatches)
      return false;
  }

  if (!filter.memberPattern.trimmed().isEmpty()) {
    const QRegularExpression expression =
        wildcardExpression(filter.memberPattern);
    QStringList memberValues;
    memberValues.reserve(element.attributes.size() * 2 +
                         element.operations.size() * 2);
    for (const QString &attribute : element.attributes)
      memberValues.append({attribute, memberName(attribute)});
    for (const auto &operation : element.operations) {
      const QString signature = modelOperationSignature(operation);
      memberValues.append({signature, operation.name});
    }
    const bool match = matchesWildcard(expression, memberValues);
    if (match == filter.excludeMemberMatches)
      return false;
  }
  return true;
}

QVariantMap toVariantMap(const DiagramFilter &filter) {
  return {
      {QStringLiteral("excludedElementTypes"), filter.excludedElementTypes},
      {QStringLiteral("includedStereotypeIds"), filter.includedStereotypeIds},
      {QStringLiteral("excludedStereotypeIds"), filter.excludedStereotypeIds},
      {QStringLiteral("namePattern"), filter.namePattern},
      {QStringLiteral("excludeNameMatches"), filter.excludeNameMatches},
      {QStringLiteral("memberPattern"), filter.memberPattern},
      {QStringLiteral("excludeMemberMatches"), filter.excludeMemberMatches},
  };
}

DiagramFilter fromVariantMap(const QVariantMap &values) {
  DiagramFilter filter;
  filter.excludedElementTypes =
      uniqueTrimmedValues(values.value(QStringLiteral("excludedElementTypes")));
  filter.includedStereotypeIds = uniqueTrimmedValues(
      values.value(QStringLiteral("includedStereotypeIds")));
  filter.excludedStereotypeIds = uniqueTrimmedValues(
      values.value(QStringLiteral("excludedStereotypeIds")));
  filter.namePattern =
      values.value(QStringLiteral("namePattern")).toString().trimmed();
  filter.excludeNameMatches =
      values.value(QStringLiteral("excludeNameMatches")).toBool();
  filter.memberPattern =
      values.value(QStringLiteral("memberPattern")).toString().trimmed();
  filter.excludeMemberMatches =
      values.value(QStringLiteral("excludeMemberMatches")).toBool();
  return filter;
}

} // namespace yauml::diagram_filter
