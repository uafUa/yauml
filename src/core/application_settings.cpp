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

namespace yauml {
namespace {

constexpr auto kSettingsGroup = "preferences/diagram";
constexpr auto kDefaultDistributionGapKey = "defaultDistributionGap";
constexpr auto kSnapToGridEnabledKey = "snapToGridEnabled";
constexpr auto kAlignmentGuidesEnabledKey = "alignmentGuidesEnabled";
constexpr auto kGridSpacingKey = "gridSpacing";
constexpr auto kDiagramItemSizingModeKey = "itemSizingMode";
constexpr auto kConnectorSettingsGroup = "preferences/connectors";
constexpr auto kDefaultConnectorRoutingKey = "defaultRouting";
constexpr auto kRelationshipGestureKeySuffix = "GestureKey";
constexpr auto kCppImportSettingsGroup = "preferences/cppImport";
constexpr auto kCppInterfacePatternKey = "interfacePattern";
constexpr auto kCppMemberTypeRulesKey = "memberTypeRules";
// Read-only migration keys used by builds before the rule table existed.
constexpr auto kCppOwningPointerTypesKey = "owningPointerTypes";
constexpr auto kCppSharedPointerTypesKey = "sharedPointerTypes";
constexpr auto kContextToolboxSettingsGroup = "preferences/contextToolboxes";
constexpr auto kContextToolboxConfigurationKey = "configuration";
constexpr auto kModelingSettingsGroup = "preferences/modeling";
constexpr auto kPackageReassignmentPolicyKey = "packageReassignmentPolicy";
constexpr auto kUpdateSettingsGroup = "preferences/updates";
constexpr auto kAutomaticUpdateChecksEnabledKey = "automaticChecksEnabled";
constexpr auto kLastUpdateCheckUtcKey = "lastCheckUtc";
constexpr auto kHistorySettingsGroup = "history";
constexpr auto kRecentProjectsKey = "recentProjects";
constexpr auto kProjectTreeSettingsGroup = "preferences/projectTree";
constexpr auto kProjectTreeColumnsKey = "columns";
constexpr auto kProjectTreeColumnWidthsKey = "columnWidths";
constexpr auto kProjectTreeRelationshipsVisibleKey = "relationshipsVisible";
constexpr auto kSourceEditorSettingsGroup = "preferences/sourceEditor";
constexpr auto kSourceEditorCommandKey = "command";
constexpr auto kSourceEditorDoubleClickEnabledKey = "doubleClickEnabled";
constexpr auto kLegacyScopeMigratedKey =
    "migration/previousProductSettingsImported";
constexpr qint64 kAutomaticUpdateCheckIntervalSeconds = 24 * 60 * 60;

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
          QStringLiteral("aggregation"),    QStringLiteral("composition"),
          QStringLiteral("containment")};
}

QVariantMap makeDefaultRelationshipGestureKeys() {
  return {{QStringLiteral("dependency"), QStringLiteral("D")},
          {QStringLiteral("realization"), QStringLiteral("I")},
          {QStringLiteral("generalization"), QStringLiteral("H")},
          {QStringLiteral("association"), QStringLiteral("A")},
          {QStringLiteral("aggregation"), QStringLiteral("G")},
          {QStringLiteral("composition"), QStringLiteral("C")},
          {QStringLiteral("containment"), QStringLiteral("N")}};
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

QString normalizedPackageReassignmentPolicy(const QString &candidate) {
  const QString normalized = candidate.trimmed().toLower();
  if (normalized == QStringLiteral("disallow") ||
      normalized == QStringLiteral("allow"))
    return normalized;
  return QStringLiteral("ask");
}

QString normalizedDiagramItemSizingMode(const QString &candidate) {
  return candidate.trimmed().toLower() == QStringLiteral("fixed")
             ? QStringLiteral("fixed")
             : QStringLiteral("content");
}

QStringList normalizedProjectTreeColumns(const QStringList &candidate) {
  static const QStringList allowed{
      QStringLiteral("name"),       QStringLiteral("sourceDirectory"),
      QStringLiteral("sourceFile"), QStringLiteral("stereotypes"),
      QStringLiteral("type"),       QStringLiteral("qualifiedName")};
  QStringList normalized{QStringLiteral("name")};
  for (const QString &column : candidate) {
    // `sourcePath` was the combined directory/file column used by the first
    // configurable-column build. Expand it once while reading or assigning
    // that legacy preference.
    if (column == QStringLiteral("sourcePath")) {
      if (!normalized.contains(QStringLiteral("sourceDirectory")))
        normalized.append(QStringLiteral("sourceDirectory"));
      if (!normalized.contains(QStringLiteral("sourceFile")))
        normalized.append(QStringLiteral("sourceFile"));
    } else if (column != QStringLiteral("name") && allowed.contains(column) &&
               !normalized.contains(column))
      normalized.append(column);
  }
  return normalized;
}

QVariantMap makeDefaultProjectTreeColumnWidths() {
  return {{QStringLiteral("name"), 210},
          {QStringLiteral("sourceDirectory"), 200},
          {QStringLiteral("sourceFile"), 160},
          {QStringLiteral("stereotypes"), 160},
          {QStringLiteral("type"), 120},
          {QStringLiteral("qualifiedName"), 240}};
}

QVariantMap normalizedProjectTreeColumnWidths(const QVariantMap &candidate) {
  constexpr int kMinimumColumnWidth = 48;
  constexpr int kMaximumColumnWidth = 1600;
  QVariantMap normalized = makeDefaultProjectTreeColumnWidths();
  for (auto iterator = candidate.cbegin(); iterator != candidate.cend();
       ++iterator) {
    if (!normalized.contains(iterator.key()))
      continue;
    bool ok = false;
    const int width = iterator.value().toInt(&ok);
    if (ok)
      normalized.insert(iterator.key(), std::clamp(width, kMinimumColumnWidth,
                                                   kMaximumColumnWidth));
  }
  return normalized;
}

QStringList normalizedCppTypeNames(const QStringList &candidates) {
  QStringList normalized;
  for (QString candidate : candidates) {
    candidate = candidate.trimmed();
    while (candidate.startsWith(QStringLiteral("::")))
      candidate.remove(0, 2);
    candidate.remove(QRegularExpression(QStringLiteral("\\s+")));
    const qsizetype templateStart = candidate.indexOf(u'<');
    if (templateStart >= 0)
      candidate.truncate(templateStart);
    if (!candidate.isEmpty() && !normalized.contains(candidate))
      normalized.append(std::move(candidate));
  }
  return normalized;
}

QVariantMap memberTypeRuleVariant(const CppMemberTypeRule &rule) {
  return {{QStringLiteral("typeName"), rule.typeName},
          {QStringLiteral("relationshipType"), toString(rule.relationshipType)},
          {QStringLiteral("multiplicity"), rule.multiplicity},
          {QStringLiteral("targetArgument"), rule.targetArgument}};
}

QVariantList memberTypeRuleVariants(const QList<CppMemberTypeRule> &rules) {
  QVariantList variants;
  variants.reserve(rules.size());
  for (const auto &rule : rules)
    variants.append(memberTypeRuleVariant(rule));
  return variants;
}

QList<CppMemberTypeRule>
normalizedMemberTypeRules(const QVariantList &candidates) {
  QList<CppMemberTypeRule> normalized;
  QSet<QString> seenTypes;
  for (const QVariant &candidate : candidates) {
    const QVariantMap values = candidate.toMap();
    const QStringList normalizedNames = normalizedCppTypeNames(
        {values.value(QStringLiteral("typeName")).toString()});
    if (normalizedNames.isEmpty())
      continue;

    bool relationshipOk = false;
    const RelationshipType relationshipType = relationshipTypeFromString(
        values.value(QStringLiteral("relationshipType")).toString(),
        &relationshipOk);
    if (!relationshipOk || (relationshipType != RelationshipType::Aggregation &&
                            relationshipType != RelationshipType::Composition))
      continue;

    const QString typeName = normalizedNames.first();
    if (seenTypes.contains(typeName))
      continue;
    seenTypes.insert(typeName);

    CppMemberTypeRule rule;
    rule.typeName = typeName;
    rule.relationshipType = relationshipType;
    rule.multiplicity =
        values.value(QStringLiteral("multiplicity")).toString().trimmed();
    rule.targetArgument = std::clamp(
        values.value(QStringLiteral("targetArgument"), 1).toInt(), 1, 16);
    normalized.append(std::move(rule));
  }
  return normalized;
}

void mergeLegacyRules(QList<CppMemberTypeRule> &rules,
                      const QStringList &typeNames,
                      RelationshipType relationshipType) {
  for (const QString &typeName : normalizedCppTypeNames(typeNames)) {
    const auto existing =
        std::find_if(rules.begin(), rules.end(), [&](const auto &rule) {
          return rule.typeName == typeName;
        });
    if (existing != rules.end()) {
      existing->relationshipType = relationshipType;
      continue;
    }
    rules.append({typeName, relationshipType, QStringLiteral("0..1"), 1});
  }
}

QVariantList toolboxEntries(std::initializer_list<const char *> actionIds) {
  QVariantList entries;
  entries.reserve(static_cast<qsizetype>(actionIds.size()));
  for (const char *actionId : actionIds) {
    entries.append(
        QVariantMap{{QStringLiteral("actionId"), QString::fromLatin1(actionId)},
                    {QStringLiteral("enabled"), true}});
  }
  return entries;
}

QVariantMap makeDefaultContextToolboxConfiguration() {
  return {
      {QStringLiteral("relationship"),
       toolboxEntries(
           {"createRelationship.dependency", "createRelationship.realization",
            "createRelationship.generalization",
            "createRelationship.association", "createRelationship.aggregation",
            "createRelationship.composition",
            "createRelationship.containment"})},
      {QStringLiteral("selection"),
       toolboxEntries({"arrange.autoLayoutSelection", "arrange.alignLeft",
                       "arrange.alignHorizontalCenters", "arrange.alignRight",
                       "arrange.alignTop", "arrange.alignVerticalCenters",
                       "arrange.alignBottom", "arrange.matchWidth",
                       "arrange.matchHeight", "arrange.matchSize",
                       "arrange.distributeHorizontally",
                       "arrange.distributeVertically", "arrange.fitToContent",
                       "style.assignNamed"})},
      {QStringLiteral("connector"),
       toolboxEntries(
           {"connector.routeStraight", "connector.routeOrthogonal",
            "connector.routeAroundObstacles", "connector.optimizeEndsAndRoute",
            "connector.editName", "connector.editSourceRole",
            "connector.editSourceMultiplicity", "connector.editTargetRole",
            "connector.editTargetMultiplicity", "connector.editStereotypes",
            "connector.resetAnnotationPositions"})},
      {QStringLiteral("presentation"),
       toolboxEntries({"presentation.editName", "source.open",
                       "presentation.attributesVisibility",
                       "presentation.operationsVisibility",
                       "presentation.operationSignatureMode",
                       "presentation.connectorSnapPoints",
                       "arrange.fitToContent", "style.assignNamed",
                       "presentation.routeInternalConnectors",
                       "presentation.optimizeInternalConnectorEndsAndRoute",
                       "presentation.addIncomingRelatedTypes",
                       "presentation.addOutgoingRelatedTypes",
                       "presentation.wrapInNamespace"})},
  };
}

QVariantMap
normalizedContextToolboxConfiguration(const QVariantMap &candidate) {
  const QVariantMap defaults = makeDefaultContextToolboxConfiguration();
  QVariantMap normalized;

  for (auto group = defaults.cbegin(); group != defaults.cend(); ++group) {
    const QVariantList defaultEntries = group.value().toList();
    QSet<QString> knownActionIds;
    for (const QVariant &entryValue : defaultEntries)
      knownActionIds.insert(
          entryValue.toMap().value(QStringLiteral("actionId")).toString());

    QSet<QString> seenActionIds;
    QVariantList entries;
    for (const QVariant &entryValue : candidate.value(group.key()).toList()) {
      const QVariantMap entry = entryValue.toMap();
      const QString actionId =
          entry.value(QStringLiteral("actionId")).toString();
      if (!knownActionIds.contains(actionId) ||
          seenActionIds.contains(actionId))
        continue;
      entries.append(
          QVariantMap{{QStringLiteral("actionId"), actionId},
                      {QStringLiteral("enabled"),
                       entry.value(QStringLiteral("enabled"), true).toBool()}});
      seenActionIds.insert(actionId);
    }

    // A newly introduced command must appear for existing installations. An
    // explicit disabled entry remains disabled; only genuinely unknown entries
    // are appended with their product default.
    for (const QVariant &entryValue : defaultEntries) {
      const QVariantMap entry = entryValue.toMap();
      const QString actionId =
          entry.value(QStringLiteral("actionId")).toString();
      if (seenActionIds.contains(actionId))
        continue;
      entries.append(entry);
    }
    normalized.insert(group.key(), entries);
  }
  return normalized;
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
  m_diagramItemSizingMode = normalizedDiagramItemSizingMode(
      settings
          .value(QLatin1String(kDiagramItemSizingModeKey),
                 QLatin1String(kDefaultDiagramItemSizingMode))
          .toString());
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
  if (settings.contains(QLatin1String(kCppMemberTypeRulesKey))) {
    m_cppMemberTypeRules = normalizedMemberTypeRules(
        settings.value(QLatin1String(kCppMemberTypeRulesKey)).toList());
  } else {
    // Existing installations only stored two pointer lists. Start from the
    // richer defaults so the newly supported standard containers work
    // immediately, then merge any custom pointer templates the user added.
    m_cppMemberTypeRules = CppImportOptions::defaultMemberTypeRules();
    mergeLegacyRules(
        m_cppMemberTypeRules,
        settings.value(QLatin1String(kCppOwningPointerTypesKey)).toStringList(),
        RelationshipType::Composition);
    mergeLegacyRules(
        m_cppMemberTypeRules,
        settings.value(QLatin1String(kCppSharedPointerTypesKey)).toStringList(),
        RelationshipType::Aggregation);
  }
  settings.endGroup();

  settings.beginGroup(QLatin1String(kContextToolboxSettingsGroup));
  m_contextToolboxConfiguration = normalizedContextToolboxConfiguration(
      settings.value(QLatin1String(kContextToolboxConfigurationKey)).toMap());
  settings.endGroup();

  settings.beginGroup(QLatin1String(kModelingSettingsGroup));
  m_packageReassignmentPolicy = normalizedPackageReassignmentPolicy(
      settings
          .value(QLatin1String(kPackageReassignmentPolicyKey),
                 defaultPackageReassignmentPolicy())
          .toString());
  settings.endGroup();

  settings.beginGroup(QLatin1String(kUpdateSettingsGroup));
  m_automaticUpdateChecksEnabled =
      settings
          .value(QLatin1String(kAutomaticUpdateChecksEnabledKey),
                 kDefaultAutomaticUpdateChecksEnabled)
          .toBool();
  m_lastUpdateCheckUtc = settings.value(QLatin1String(kLastUpdateCheckUtcKey))
                             .toDateTime()
                             .toUTC();
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

  settings.beginGroup(QLatin1String(kProjectTreeSettingsGroup));
  m_projectTreeColumns = normalizedProjectTreeColumns(
      settings
          .value(QLatin1String(kProjectTreeColumnsKey),
                 defaultProjectTreeColumns())
          .toStringList());
  m_projectTreeColumnWidths = normalizedProjectTreeColumnWidths(
      settings.value(QLatin1String(kProjectTreeColumnWidthsKey)).toMap());
  m_projectTreeRelationshipsVisible =
      settings
          .value(QLatin1String(kProjectTreeRelationshipsVisibleKey),
                 kDefaultProjectTreeRelationshipsVisible)
          .toBool();
  settings.endGroup();

  settings.beginGroup(QLatin1String(kSourceEditorSettingsGroup));
  m_sourceEditorCommand = settings
                              .value(QLatin1String(kSourceEditorCommandKey),
                                     QLatin1String(kDefaultSourceEditorCommand))
                              .toString()
                              .trimmed();
  if (m_sourceEditorCommand.isEmpty())
    m_sourceEditorCommand = QLatin1String(kDefaultSourceEditorCommand);
  m_sourceEditorDoubleClickEnabled =
      settings
          .value(QLatin1String(kSourceEditorDoubleClickEnabledKey),
                 kDefaultSourceEditorDoubleClickEnabled)
          .toBool();
  settings.endGroup();
}

void ApplicationSettings::migrateLegacyScope(const QString &legacyOrganization,
                                             const QString &legacyApplication) {
  QSettings current;
  if (current.value(QLatin1String(kLegacyScopeMigratedKey)).toBool())
    return;

  QSettings legacy(QSettings::NativeFormat, QSettings::UserScope,
                   legacyOrganization, legacyApplication);
  for (const QString &key : legacy.allKeys()) {
    if (!current.contains(key))
      current.setValue(key, legacy.value(key));
  }
  current.setValue(QLatin1String(kLegacyScopeMigratedKey), true);
  current.sync();
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

QVariantList ApplicationSettings::defaultCppMemberTypeRules() {
  return memberTypeRuleVariants(CppImportOptions::defaultMemberTypeRules());
}

QVariantMap ApplicationSettings::defaultContextToolboxConfiguration() {
  return makeDefaultContextToolboxConfiguration();
}

QString ApplicationSettings::defaultPackageReassignmentPolicy() {
  return QStringLiteral("ask");
}

QStringList ApplicationSettings::defaultProjectTreeColumns() {
  return {QStringLiteral("name"), QStringLiteral("sourceDirectory"),
          QStringLiteral("sourceFile"), QStringLiteral("stereotypes")};
}

QVariantMap ApplicationSettings::defaultProjectTreeColumnWidths() {
  return makeDefaultProjectTreeColumnWidths();
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

QString ApplicationSettings::diagramItemSizingMode() const {
  return m_diagramItemSizingMode;
}

void ApplicationSettings::setDiagramItemSizingMode(const QString &mode) {
  const QString normalized = normalizedDiagramItemSizingMode(mode);
  if (m_diagramItemSizingMode == normalized)
    return;
  m_diagramItemSizingMode = normalized;
  persistDiagramPreferences();
  emit diagramItemSizingModeChanged();
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

QVariantList ApplicationSettings::cppMemberTypeRules() const {
  return memberTypeRuleVariants(m_cppMemberTypeRules);
}

QVariantList ApplicationSettings::cppMemberTypeRuleDefaults() const {
  return defaultCppMemberTypeRules();
}

QList<CppMemberTypeRule> ApplicationSettings::cppMemberTypeRuleValues() const {
  return m_cppMemberTypeRules;
}

bool ApplicationSettings::setCppMemberTypeRules(const QVariantList &rules) {
  const QList<CppMemberTypeRule> normalized = normalizedMemberTypeRules(rules);
  if (normalized.size() != rules.size())
    return false;
  if (m_cppMemberTypeRules == normalized)
    return true;
  m_cppMemberTypeRules = normalized;
  persistCppImportPreferences();
  emit cppMemberTypeRulesChanged();
  return true;
}

QVariantMap ApplicationSettings::contextToolboxConfiguration() const {
  return m_contextToolboxConfiguration;
}

QVariantMap ApplicationSettings::contextToolboxDefaults() const {
  return defaultContextToolboxConfiguration();
}

bool ApplicationSettings::setContextToolboxConfiguration(
    const QVariantMap &configuration) {
  const QVariantMap normalized =
      normalizedContextToolboxConfiguration(configuration);
  if (m_contextToolboxConfiguration == normalized)
    return true;
  m_contextToolboxConfiguration = normalized;
  persistContextToolboxPreferences();
  emit contextToolboxConfigurationChanged();
  return true;
}

QString ApplicationSettings::packageReassignmentPolicy() const {
  return m_packageReassignmentPolicy;
}

void ApplicationSettings::setPackageReassignmentPolicy(const QString &policy) {
  const QString normalized = normalizedPackageReassignmentPolicy(policy);
  if (m_packageReassignmentPolicy == normalized)
    return;
  m_packageReassignmentPolicy = normalized;
  persistModelingPreferences();
  emit packageReassignmentPolicyChanged();
}

bool ApplicationSettings::automaticUpdateChecksEnabled() const {
  return m_automaticUpdateChecksEnabled;
}

void ApplicationSettings::setAutomaticUpdateChecksEnabled(bool enabled) {
  if (m_automaticUpdateChecksEnabled == enabled)
    return;
  m_automaticUpdateChecksEnabled = enabled;
  persistUpdatePreferences();
  emit automaticUpdateChecksEnabledChanged();
}

bool ApplicationSettings::automaticUpdateCheckDue(const QDateTime &now) const {
  if (!m_automaticUpdateChecksEnabled)
    return false;
  const QDateTime utcNow = now.toUTC();
  return !m_lastUpdateCheckUtc.isValid() || m_lastUpdateCheckUtc > utcNow ||
         m_lastUpdateCheckUtc.secsTo(utcNow) >=
             kAutomaticUpdateCheckIntervalSeconds;
}

void ApplicationSettings::recordUpdateCheck(const QDateTime &timestamp) {
  m_lastUpdateCheckUtc = timestamp.toUTC();
  persistUpdatePreferences();
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
  setDiagramItemSizingMode(QLatin1String(kDefaultDiagramItemSizingMode));
  setDefaultConnectorRouting(toString(kDefaultConnectorRouting));
  setRelationshipGestureKeys(makeDefaultRelationshipGestureKeys());
  setCppInterfacePattern(defaultCppInterfacePattern());
  setCppMemberTypeRules(defaultCppMemberTypeRules());
  setContextToolboxConfiguration(defaultContextToolboxConfiguration());
  setPackageReassignmentPolicy(defaultPackageReassignmentPolicy());
  setAutomaticUpdateChecksEnabled(kDefaultAutomaticUpdateChecksEnabled);
  setProjectTreeColumns(defaultProjectTreeColumns());
  setProjectTreeColumnWidths(defaultProjectTreeColumnWidths());
  setProjectTreeRelationshipsVisible(kDefaultProjectTreeRelationshipsVisible);
  setSourceEditorCommand(QLatin1String(kDefaultSourceEditorCommand));
  setSourceEditorDoubleClickEnabled(kDefaultSourceEditorDoubleClickEnabled);
}

QStringList ApplicationSettings::projectTreeColumns() const {
  return m_projectTreeColumns;
}

void ApplicationSettings::setProjectTreeColumns(const QStringList &columns) {
  const QStringList normalized = normalizedProjectTreeColumns(columns);
  if (m_projectTreeColumns == normalized)
    return;
  m_projectTreeColumns = normalized;
  persistProjectTreePreferences();
  emit projectTreeColumnsChanged();
}

QVariantMap ApplicationSettings::projectTreeColumnWidths() const {
  return m_projectTreeColumnWidths;
}

void ApplicationSettings::setProjectTreeColumnWidths(
    const QVariantMap &widths) {
  const QVariantMap normalized = normalizedProjectTreeColumnWidths(widths);
  if (m_projectTreeColumnWidths == normalized)
    return;
  m_projectTreeColumnWidths = normalized;
  persistProjectTreePreferences();
  emit projectTreeColumnWidthsChanged();
}

bool ApplicationSettings::projectTreeRelationshipsVisible() const {
  return m_projectTreeRelationshipsVisible;
}

void ApplicationSettings::setProjectTreeRelationshipsVisible(bool visible) {
  if (m_projectTreeRelationshipsVisible == visible)
    return;
  m_projectTreeRelationshipsVisible = visible;
  persistProjectTreePreferences();
  emit projectTreeRelationshipsVisibleChanged();
}

QString ApplicationSettings::sourceEditorCommand() const {
  return m_sourceEditorCommand;
}

void ApplicationSettings::setSourceEditorCommand(const QString &command) {
  QString normalized = command.trimmed();
  if (normalized.isEmpty())
    normalized = QLatin1String(kDefaultSourceEditorCommand);
  if (m_sourceEditorCommand == normalized)
    return;
  m_sourceEditorCommand = normalized;
  persistSourceEditorPreferences();
  emit sourceEditorCommandChanged();
}

bool ApplicationSettings::sourceEditorDoubleClickEnabled() const {
  return m_sourceEditorDoubleClickEnabled;
}

void ApplicationSettings::setSourceEditorDoubleClickEnabled(bool enabled) {
  if (m_sourceEditorDoubleClickEnabled == enabled)
    return;
  m_sourceEditorDoubleClickEnabled = enabled;
  persistSourceEditorPreferences();
  emit sourceEditorDoubleClickEnabledChanged();
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
  settings.setValue(QLatin1String(kDiagramItemSizingModeKey),
                    m_diagramItemSizingMode);
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
  settings.setValue(QLatin1String(kCppMemberTypeRulesKey),
                    memberTypeRuleVariants(m_cppMemberTypeRules));
  settings.remove(QLatin1String(kCppOwningPointerTypesKey));
  settings.remove(QLatin1String(kCppSharedPointerTypesKey));
  settings.endGroup();
  settings.sync();
}

void ApplicationSettings::persistContextToolboxPreferences() const {
  QSettings settings;
  settings.beginGroup(QLatin1String(kContextToolboxSettingsGroup));
  settings.setValue(QLatin1String(kContextToolboxConfigurationKey),
                    m_contextToolboxConfiguration);
  settings.endGroup();
  settings.sync();
}

void ApplicationSettings::persistModelingPreferences() const {
  QSettings settings;
  settings.beginGroup(QLatin1String(kModelingSettingsGroup));
  settings.setValue(QLatin1String(kPackageReassignmentPolicyKey),
                    m_packageReassignmentPolicy);
  settings.endGroup();
  settings.sync();
}

void ApplicationSettings::persistUpdatePreferences() const {
  QSettings settings;
  settings.beginGroup(QLatin1String(kUpdateSettingsGroup));
  settings.setValue(QLatin1String(kAutomaticUpdateChecksEnabledKey),
                    m_automaticUpdateChecksEnabled);
  if (m_lastUpdateCheckUtc.isValid())
    settings.setValue(QLatin1String(kLastUpdateCheckUtcKey),
                      m_lastUpdateCheckUtc);
  else
    settings.remove(QLatin1String(kLastUpdateCheckUtcKey));
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

void ApplicationSettings::persistProjectTreePreferences() const {
  QSettings settings;
  settings.beginGroup(QLatin1String(kProjectTreeSettingsGroup));
  settings.setValue(QLatin1String(kProjectTreeColumnsKey),
                    m_projectTreeColumns);
  settings.setValue(QLatin1String(kProjectTreeColumnWidthsKey),
                    m_projectTreeColumnWidths);
  settings.setValue(QLatin1String(kProjectTreeRelationshipsVisibleKey),
                    m_projectTreeRelationshipsVisible);
  settings.endGroup();
  settings.sync();
}

void ApplicationSettings::persistSourceEditorPreferences() const {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSourceEditorSettingsGroup));
  settings.setValue(QLatin1String(kSourceEditorCommandKey),
                    m_sourceEditorCommand);
  settings.setValue(QLatin1String(kSourceEditorDoubleClickEnabledKey),
                    m_sourceEditorDoubleClickEnabled);
  settings.endGroup();
  settings.sync();
}

} // namespace yauml
