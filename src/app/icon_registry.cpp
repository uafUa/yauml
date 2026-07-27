#include "app/icon_registry.h"

#include "core/json5.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <algorithm>

namespace uuml::ui {
namespace {

constexpr auto kCatalogResource = ":/icons/action-icons.json5";

QString normalizedResourcePrefix(const QString &value) {
  QString prefix = value.trimmed();
  if (prefix.isEmpty())
    return QStringLiteral("/icons");
  prefix.replace(u'\\', u'/');
  if (!prefix.startsWith(u'/'))
    prefix.prepend(u'/');
  while (prefix.endsWith(u'/'))
    prefix.chop(1);
  return prefix;
}

} // namespace

IconRegistry::IconRegistry(QObject *parent) : QObject(parent) { load(); }

int IconRegistry::defaultSize() const { return m_defaultSize; }
bool IconRegistry::isValid() const { return m_errors.isEmpty(); }
QStringList IconRegistry::errors() const { return m_errors; }

QUrl IconRegistry::actionIcon(const QString &actionId) const {
  return m_actionIcons.value(actionId);
}

QUrl IconRegistry::projectTreeIcon(const QString &kind,
                                   const QString &objectType,
                                   const QString &objectId, bool nested,
                                   bool expanded) const {
  const TreeIcon *best = nullptr;
  for (const auto &candidate : m_treeIcons) {
    if (!candidate.matches(kind, objectType, objectId, nested))
      continue;
    const QUrl url = expanded && !candidate.expandedUrl.isEmpty()
                         ? candidate.expandedUrl
                         : candidate.collapsedUrl;
    if (url.isEmpty())
      continue;
    if (!best || candidate.specificity() > best->specificity())
      best = &candidate;
  }
  if (!best)
    return {};
  return expanded && !best->expandedUrl.isEmpty() ? best->expandedUrl
                                                  : best->collapsedUrl;
}

int IconRegistry::TreeIcon::specificity() const {
  return !kind.isEmpty() + !objectType.isEmpty() + !objectId.isEmpty() +
         matchesNested;
}

bool IconRegistry::TreeIcon::matches(const QString &candidateKind,
                                     const QString &candidateObjectType,
                                     const QString &candidateObjectId,
                                     bool candidateNested) const {
  return (kind.isEmpty() || kind == candidateKind) &&
         (objectType.isEmpty() || objectType == candidateObjectType) &&
         (objectId.isEmpty() || objectId == candidateObjectId) &&
         (!matchesNested || nested == candidateNested);
}

QUrl IconRegistry::resolveSvg(const QString &relativePath,
                              const QString &entryId) {
  QString path = relativePath.trimmed();
  if (path.isEmpty())
    return {};
  path.replace(u'\\', u'/');
  const QString clean = QDir::cleanPath(path);
  if (QDir::isAbsolutePath(path) || clean == QStringLiteral("..") ||
      clean.startsWith(QStringLiteral("../")) ||
      !clean.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
    m_errors.append(
        QStringLiteral("%1 has an invalid SVG path: %2").arg(entryId, path));
    return {};
  }

  const QString resourcePath = u':' + m_resourcePrefix + u'/' + clean;
  if (!QFileInfo::exists(resourcePath)) {
    m_errors.append(
        QStringLiteral("%1 references a missing SVG: %2").arg(entryId, clean));
    return {};
  }
  return QUrl(QStringLiteral("qrc:") + m_resourcePrefix + u'/' + clean);
}

void IconRegistry::load() {
  QFile catalog(QString::fromLatin1(kCatalogResource));
  if (!catalog.open(QIODevice::ReadOnly)) {
    m_errors.append(QStringLiteral("Cannot read the icon catalog: %1")
                        .arg(catalog.errorString()));
    return;
  }
  const auto parsed = Json5::parse(catalog.readAll());
  if (!parsed) {
    m_errors.append(
        QStringLiteral("Cannot parse the icon catalog: %1").arg(parsed.error));
    return;
  }

  const QJsonObject root = parsed.document.object();
  m_defaultSize =
      std::clamp(root.value(QStringLiteral("defaultSize")).toInt(20), 8, 64);
  m_resourcePrefix = normalizedResourcePrefix(
      root.value(QStringLiteral("resourcePrefix")).toString());

  const QJsonObject actions = root.value(QStringLiteral("actions")).toObject();
  for (auto category = actions.begin(); category != actions.end(); ++category) {
    const QJsonObject entries = category.value().toObject();
    for (auto action = entries.begin(); action != entries.end(); ++action) {
      const QString id = category.key() + u'.' + action.key();
      const QUrl url = resolveSvg(
          action.value().toObject().value(QStringLiteral("svg")).toString(),
          id);
      if (!url.isEmpty())
        m_actionIcons.insert(id, url);
    }
  }

  const QJsonObject nodes =
      root.value(QStringLiteral("projectTreeNodes")).toObject();
  m_treeIcons.reserve(nodes.size());
  for (auto node = nodes.begin(); node != nodes.end(); ++node) {
    const QJsonObject entry = node.value().toObject();
    const QJsonObject match = entry.value(QStringLiteral("match")).toObject();
    TreeIcon icon;
    icon.kind = match.value(QStringLiteral("kind")).toString();
    icon.objectType = match.value(QStringLiteral("objectType")).toString();
    icon.objectId = match.value(QStringLiteral("objectId")).toString();
    icon.matchesNested = match.contains(QStringLiteral("nested"));
    icon.nested = match.value(QStringLiteral("nested")).toBool();
    const QString id = QStringLiteral("projectTreeNodes.") + node.key();
    icon.collapsedUrl =
        resolveSvg(entry.value(QStringLiteral("svg")).toString(), id);
    icon.expandedUrl =
        resolveSvg(entry.value(QStringLiteral("expandedSvg")).toString(),
                   id + QStringLiteral(".expanded"));
    m_treeIcons.append(std::move(icon));
  }
}

} // namespace uuml::ui
