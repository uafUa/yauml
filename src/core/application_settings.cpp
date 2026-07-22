#include "core/application_settings.h"

#include "core/cpp_import.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <utility>

namespace uuml {
namespace {

constexpr auto kSettingsGroup = "preferences/diagram";
constexpr auto kDefaultDistributionGapKey = "defaultDistributionGap";
constexpr auto kSnapToGridEnabledKey = "snapToGridEnabled";
constexpr auto kAlignmentGuidesEnabledKey = "alignmentGuidesEnabled";
constexpr auto kGridSpacingKey = "gridSpacing";
constexpr auto kConnectorSettingsGroup = "preferences/connectors";
constexpr auto kDefaultConnectorRoutingKey = "defaultRouting";
constexpr auto kRelationshipGestureKeySuffix = "GestureKey";
constexpr auto kCppImportSettingsGroup = "preferences/cppImport";
constexpr auto kCppInterfacePatternKey = "interfacePattern";
constexpr auto kHistorySettingsGroup = "history";
constexpr auto kRecentProjectsKey = "recentProjects";

int validDistributionGap(int gap) {
  return std::clamp(gap, ApplicationSettings::kMinimumDistributionGap,
                    ApplicationSettings::kMaximumDistributionGap);
}

int validGridSpacing(int spacing) {
  return std::clamp(spacing, ApplicationSettings::kMinimumGridSpacing,
                    ApplicationSettings::kMaximumGridSpacing);
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

QStringList relationshipGestureTypes() {
  return {QStringLiteral("dependency"),     QStringLiteral("realization"),
          QStringLiteral("generalization"), QStringLiteral("association"),
          QStringLiteral("aggregation"),    QStringLiteral("composition")};
}

QVariantMap makeDefaultRelationshipGestureKeys() {
  return {{QStringLiteral("dependency"), QStringLiteral("D")},
          {QStringLiteral("realization"), QStringLiteral("I")},
          {QStringLiteral("generalization"), QStringLiteral("H")},
          {QStringLiteral("association"), QStringLiteral("A")},
          {QStringLiteral("aggregation"), QStringLiteral("G")},
          {QStringLiteral("composition"), QStringLiteral("C")}};
}

bool normalizeRelationshipGestureKeys(const QVariantMap &candidate,
                                      QVariantMap &normalized) {
  QSet<QString> assigned;
  for (const auto &type : relationshipGestureTypes()) {
    const QString key = candidate.value(type).toString().trimmed().toUpper();
    if (key.size() != 1 || !key.front().isLetterOrNumber() ||
        assigned.contains(key))
      return false;
    normalized.insert(type, key);
    assigned.insert(key);
  }
  return true;
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
  m_snapToGridEnabled = settings
                            .value(QLatin1String(kSnapToGridEnabledKey),
                                   kDefaultSnapToGridEnabled)
                            .toBool();
  m_alignmentGuidesEnabled =
      settings
          .value(QLatin1String(kAlignmentGuidesEnabledKey),
                 kDefaultAlignmentGuidesEnabled)
          .toBool();
  m_gridSpacing = validGridSpacing(
      settings.value(QLatin1String(kGridSpacingKey), kDefaultGridSpacing)
          .toInt());
  settings.endGroup();

  settings.beginGroup(QLatin1String(kConnectorSettingsGroup));
  bool routingOk = false;
  m_defaultConnectorRouting = connectorRoutingFromString(
      settings
          .value(QLatin1String(kDefaultConnectorRoutingKey),
                 toString(kDefaultConnectorRouting))
          .toString(),
      &routingOk);
  if (!routingOk)
    m_defaultConnectorRouting = kDefaultConnectorRouting;

  const QVariantMap gestureDefaults = makeDefaultRelationshipGestureKeys();
  QVariantMap storedGestureKeys;
  for (const auto &type : relationshipGestureTypes()) {
    storedGestureKeys.insert(
        type, settings
                  .value(type + QLatin1String(kRelationshipGestureKeySuffix),
                         gestureDefaults.value(type))
                  .toString());
  }
  if (!normalizeRelationshipGestureKeys(storedGestureKeys,
                                        m_relationshipGestureKeys))
    m_relationshipGestureKeys = gestureDefaults;
  settings.endGroup();

  settings.beginGroup(QLatin1String(kCppImportSettingsGroup));
  const QString storedInterfacePattern =
      settings
          .value(QLatin1String(kCppInterfacePatternKey),
                 defaultCppInterfacePattern())
          .toString();
  m_cppInterfacePattern =
      !storedInterfacePattern.isEmpty() &&
              QRegularExpression(storedInterfacePattern).isValid()
          ? storedInterfacePattern
          : defaultCppInterfacePattern();
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

QVariantMap ApplicationSettings::defaultRelationshipGestureKeys() {
  return makeDefaultRelationshipGestureKeys();
}

QString ApplicationSettings::defaultCppInterfacePattern() {
  return CppImportOptions::defaultInterfacePattern();
}

void ApplicationSettings::setDefaultDistributionGap(int gap) {
  const int validGap = validDistributionGap(gap);
  if (m_defaultDistributionGap == validGap)
    return;
  m_defaultDistributionGap = validGap;
  persistDiagramPreferences();
  emit defaultDistributionGapChanged();
}

bool ApplicationSettings::snapToGridEnabled() const {
  return m_snapToGridEnabled;
}

void ApplicationSettings::setSnapToGridEnabled(bool enabled) {
  if (m_snapToGridEnabled == enabled)
    return;
  m_snapToGridEnabled = enabled;
  persistDiagramPreferences();
  emit snapToGridEnabledChanged();
}

bool ApplicationSettings::alignmentGuidesEnabled() const {
  return m_alignmentGuidesEnabled;
}

void ApplicationSettings::setAlignmentGuidesEnabled(bool enabled) {
  if (m_alignmentGuidesEnabled == enabled)
    return;
  m_alignmentGuidesEnabled = enabled;
  persistDiagramPreferences();
  emit alignmentGuidesEnabledChanged();
}

int ApplicationSettings::gridSpacing() const { return m_gridSpacing; }

void ApplicationSettings::setGridSpacing(int spacing) {
  const int validSpacing = validGridSpacing(spacing);
  if (m_gridSpacing == validSpacing)
    return;
  m_gridSpacing = validSpacing;
  persistDiagramPreferences();
  emit gridSpacingChanged();
}

QString ApplicationSettings::defaultConnectorRouting() const {
  return toString(m_defaultConnectorRouting);
}

void ApplicationSettings::setDefaultConnectorRouting(const QString &routing) {
  bool routingOk = false;
  const ConnectorRouting parsed =
      connectorRoutingFromString(routing, &routingOk);
  if (!routingOk || m_defaultConnectorRouting == parsed)
    return;
  m_defaultConnectorRouting = parsed;
  persistConnectorPreferences();
  emit defaultConnectorRoutingChanged();
}

QVariantMap ApplicationSettings::relationshipGestureKeys() const {
  return m_relationshipGestureKeys;
}

bool ApplicationSettings::setRelationshipGestureKeys(const QVariantMap &keys) {
  QVariantMap normalized;
  if (!normalizeRelationshipGestureKeys(keys, normalized))
    return false;
  if (m_relationshipGestureKeys == normalized)
    return true;
  m_relationshipGestureKeys = std::move(normalized);
  persistConnectorPreferences();
  emit relationshipGestureKeysChanged();
  return true;
}

QString ApplicationSettings::cppInterfacePattern() const {
  return m_cppInterfacePattern;
}

bool ApplicationSettings::setCppInterfacePattern(const QString &pattern) {
  if (!isValidCppInterfacePattern(pattern))
    return false;
  if (m_cppInterfacePattern == pattern)
    return true;
  m_cppInterfacePattern = pattern;
  persistCppImportPreferences();
  emit cppInterfacePatternChanged();
  return true;
}

bool ApplicationSettings::isValidCppInterfacePattern(
    const QString &pattern) const {
  return !pattern.isEmpty() && QRegularExpression(pattern).isValid();
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
  setSnapToGridEnabled(kDefaultSnapToGridEnabled);
  setAlignmentGuidesEnabled(kDefaultAlignmentGuidesEnabled);
  setGridSpacing(kDefaultGridSpacing);
  setDefaultConnectorRouting(toString(kDefaultConnectorRouting));
  setRelationshipGestureKeys(makeDefaultRelationshipGestureKeys());
  setCppInterfacePattern(defaultCppInterfacePattern());
}

void ApplicationSettings::persistDiagramPreferences() const {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.setValue(QLatin1String(kDefaultDistributionGapKey),
                    m_defaultDistributionGap);
  settings.setValue(QLatin1String(kSnapToGridEnabledKey), m_snapToGridEnabled);
  settings.setValue(QLatin1String(kAlignmentGuidesEnabledKey),
                    m_alignmentGuidesEnabled);
  settings.setValue(QLatin1String(kGridSpacingKey), m_gridSpacing);
  settings.endGroup();
  settings.sync();
}

void ApplicationSettings::persistConnectorPreferences() const {
  QSettings settings;
  settings.beginGroup(QLatin1String(kConnectorSettingsGroup));
  settings.setValue(QLatin1String(kDefaultConnectorRoutingKey),
                    toString(m_defaultConnectorRouting));
  for (const auto &type : relationshipGestureTypes())
    settings.setValue(type + QLatin1String(kRelationshipGestureKeySuffix),
                      m_relationshipGestureKeys.value(type));
  settings.endGroup();
  settings.sync();
}

void ApplicationSettings::persistCppImportPreferences() const {
  QSettings settings;
  settings.beginGroup(QLatin1String(kCppImportSettingsGroup));
  settings.setValue(QLatin1String(kCppInterfacePatternKey),
                    m_cppInterfacePattern);
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
