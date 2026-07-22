#include "core/application_settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>

namespace uuml {
namespace {

constexpr auto kSettingsGroup = "preferences/diagram";
constexpr auto kDefaultDistributionGapKey = "defaultDistributionGap";
constexpr auto kHistorySettingsGroup = "history";
constexpr auto kRecentProjectsKey = "recentProjects";

int validDistributionGap(int gap) {
  return std::clamp(gap, ApplicationSettings::kMinimumDistributionGap,
                    ApplicationSettings::kMaximumDistributionGap);
}

QString normalizedProjectPath(const QString &path) {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty())
    return {};
  return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

bool pathsMatch(const QString &left, const QString &right) {
#ifdef Q_OS_WIN
  constexpr auto caseSensitivity = Qt::CaseInsensitive;
#else
  constexpr auto caseSensitivity = Qt::CaseSensitive;
#endif
  return left.compare(right, caseSensitivity) == 0;
}

} // namespace

ApplicationSettings::ApplicationSettings(QObject *parent) : QObject(parent) {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  m_defaultDistributionGap =
      validDistributionGap(settings
                               .value(QLatin1String(kDefaultDistributionGapKey),
                                      kDefaultDistributionGap)
                               .toInt());
  settings.endGroup();

  settings.beginGroup(QLatin1String(kHistorySettingsGroup));
  const QStringList storedPaths =
      settings.value(QLatin1String(kRecentProjectsKey)).toStringList();
  settings.endGroup();
  for (const auto &storedPath : storedPaths) {
    const QString normalized = normalizedProjectPath(storedPath);
    const bool duplicate =
        std::any_of(m_recentProjectPaths.cbegin(), m_recentProjectPaths.cend(),
                    [&normalized](const QString &existing) {
                      return pathsMatch(existing, normalized);
                    });
    if (normalized.isEmpty() || duplicate)
      continue;
    m_recentProjectPaths.append(normalized);
    if (m_recentProjectPaths.size() == kMaximumRecentProjects)
      break;
  }
}

int ApplicationSettings::defaultDistributionGap() const {
  return m_defaultDistributionGap;
}

void ApplicationSettings::setDefaultDistributionGap(int gap) {
  const int validGap = validDistributionGap(gap);
  if (m_defaultDistributionGap == validGap)
    return;
  m_defaultDistributionGap = validGap;
  persistDiagramPreferences();
  emit defaultDistributionGapChanged();
}

QVariantList ApplicationSettings::recentProjects() const {
  QVariantList entries;
  entries.reserve(m_recentProjectPaths.size());
  for (const auto &path : m_recentProjectPaths) {
    QVariantMap entry;
    const QString directoryName = QFileInfo(path).fileName();
    entry.insert(QStringLiteral("name"),
                 directoryName.isEmpty() ? path : directoryName);
    entry.insert(QStringLiteral("path"), path);
    entry.insert(QStringLiteral("displayPath"), QDir::toNativeSeparators(path));
    entry.insert(QStringLiteral("url"), QUrl::fromLocalFile(path));
    entries.append(entry);
  }
  return entries;
}

void ApplicationSettings::addRecentProject(const QString &projectPath) {
  const QString normalized = normalizedProjectPath(projectPath);
  if (normalized.isEmpty())
    return;
  if (!m_recentProjectPaths.isEmpty() &&
      pathsMatch(m_recentProjectPaths.first(), normalized))
    return;

  for (qsizetype index = m_recentProjectPaths.size(); index-- > 0;) {
    if (pathsMatch(m_recentProjectPaths.at(index), normalized))
      m_recentProjectPaths.removeAt(index);
  }
  m_recentProjectPaths.prepend(normalized);
  while (m_recentProjectPaths.size() > kMaximumRecentProjects)
    m_recentProjectPaths.removeLast();

  persistRecentProjects();
  emit recentProjectsChanged();
}

void ApplicationSettings::clearRecentProjects() {
  if (m_recentProjectPaths.isEmpty())
    return;
  m_recentProjectPaths.clear();
  persistRecentProjects();
  emit recentProjectsChanged();
}

void ApplicationSettings::resetDefaults() {
  setDefaultDistributionGap(kDefaultDistributionGap);
}

void ApplicationSettings::persistDiagramPreferences() const {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.setValue(QLatin1String(kDefaultDistributionGapKey),
                    m_defaultDistributionGap);
  settings.endGroup();
  settings.sync();
}

void ApplicationSettings::persistRecentProjects() const {
  QSettings settings;
  settings.beginGroup(QLatin1String(kHistorySettingsGroup));
  if (m_recentProjectPaths.isEmpty())
    settings.remove(QLatin1String(kRecentProjectsKey));
  else
    settings.setValue(QLatin1String(kRecentProjectsKey), m_recentProjectPaths);
  settings.endGroup();
  settings.sync();
}

} // namespace uuml
