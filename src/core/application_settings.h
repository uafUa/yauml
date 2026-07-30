#pragma once

#include "core/cpp_import.h"

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace yauml {

// Application-wide preferences that are independent of the currently open
// project. Values are persisted through QSettings and exposed to QML through
// one long-lived instance owned by main().
class ApplicationSettings final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int defaultDistributionGap READ defaultDistributionGap WRITE
                 setDefaultDistributionGap NOTIFY defaultDistributionGapChanged)
  Q_PROPERTY(bool snapToGridEnabled READ snapToGridEnabled WRITE
                 setSnapToGridEnabled NOTIFY snapToGridEnabledChanged)
  Q_PROPERTY(bool alignmentGuidesEnabled READ alignmentGuidesEnabled WRITE
                 setAlignmentGuidesEnabled NOTIFY alignmentGuidesEnabledChanged)
  Q_PROPERTY(int gridSpacing READ gridSpacing WRITE setGridSpacing NOTIFY
                 gridSpacingChanged)
  Q_PROPERTY(QString diagramItemSizingMode READ diagramItemSizingMode WRITE
                 setDiagramItemSizingMode NOTIFY diagramItemSizingModeChanged)
  Q_PROPERTY(
      QString defaultConnectorRouting READ defaultConnectorRouting WRITE
          setDefaultConnectorRouting NOTIFY defaultConnectorRoutingChanged)
  Q_PROPERTY(QVariantMap relationshipGestureKeys READ relationshipGestureKeys
                 NOTIFY relationshipGestureKeysChanged)
  Q_PROPERTY(QString cppInterfacePattern READ cppInterfacePattern NOTIFY
                 cppInterfacePatternChanged)
  Q_PROPERTY(QVariantList cppMemberTypeRules READ cppMemberTypeRules NOTIFY
                 cppMemberTypeRulesChanged)
  Q_PROPERTY(
      QVariantMap contextToolboxConfiguration READ contextToolboxConfiguration
          NOTIFY contextToolboxConfigurationChanged)
  Q_PROPERTY(
      QString packageReassignmentPolicy READ packageReassignmentPolicy WRITE
          setPackageReassignmentPolicy NOTIFY packageReassignmentPolicyChanged)
  Q_PROPERTY(bool automaticUpdateChecksEnabled READ automaticUpdateChecksEnabled
                 WRITE setAutomaticUpdateChecksEnabled NOTIFY
                     automaticUpdateChecksEnabledChanged)
  Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY
                 recentProjectsChanged)
  Q_PROPERTY(QStringList projectTreeColumns READ projectTreeColumns WRITE
                 setProjectTreeColumns NOTIFY projectTreeColumnsChanged)
  Q_PROPERTY(
      QVariantMap projectTreeColumnWidths READ projectTreeColumnWidths WRITE
          setProjectTreeColumnWidths NOTIFY projectTreeColumnWidthsChanged)
  Q_PROPERTY(
      bool projectTreeRelationshipsVisible READ projectTreeRelationshipsVisible
          WRITE setProjectTreeRelationshipsVisible NOTIFY
              projectTreeRelationshipsVisibleChanged)
  Q_PROPERTY(QString sourceEditorCommand READ sourceEditorCommand WRITE
                 setSourceEditorCommand NOTIFY sourceEditorCommandChanged)
  Q_PROPERTY(
      bool sourceEditorDoubleClickEnabled READ sourceEditorDoubleClickEnabled
          WRITE setSourceEditorDoubleClickEnabled NOTIFY
              sourceEditorDoubleClickEnabledChanged)

public:
  static constexpr int kDefaultDistributionGap = 10;
  static constexpr int kMinimumDistributionGap = 1;
  static constexpr int kMaximumDistributionGap = 1000;
  static constexpr bool kDefaultSnapToGridEnabled = true;
  static constexpr bool kDefaultAlignmentGuidesEnabled = true;
  static constexpr int kDefaultGridSpacing = 20;
  static constexpr int kMinimumGridSpacing = 5;
  static constexpr int kMaximumGridSpacing = 200;
  static constexpr auto kDefaultDiagramItemSizingMode = "content";
  static constexpr ConnectorRouting kDefaultConnectorRouting =
      ConnectorRouting::Straight;
  static constexpr bool kDefaultAutomaticUpdateChecksEnabled = true;
  static constexpr bool kDefaultProjectTreeRelationshipsVisible = true;
  static constexpr auto kDefaultSourceEditorCommand = "code";
  static constexpr bool kDefaultSourceEditorDoubleClickEnabled = false;
  static constexpr int kMaximumRecentProjects = 10;

  static QVariantMap defaultRelationshipGestureKeys();
  static QString defaultCppInterfacePattern();
  static QVariantList defaultCppMemberTypeRules();
  static QVariantMap defaultContextToolboxConfiguration();
  static QString defaultPackageReassignmentPolicy();
  static QStringList defaultProjectTreeColumns();
  static QVariantMap defaultProjectTreeColumnWidths();
  // Copies settings from a previous product identity exactly once. Existing
  // values in the current scope always win, and the legacy scope is retained
  // so downgrading does not lose preferences.
  static void migrateLegacyScope(const QString &legacyOrganization,
                                 const QString &legacyApplication);

  explicit ApplicationSettings(QObject *parent = nullptr);

  int defaultDistributionGap() const;
  void setDefaultDistributionGap(int gap);
  bool snapToGridEnabled() const;
  void setSnapToGridEnabled(bool enabled);
  bool alignmentGuidesEnabled() const;
  void setAlignmentGuidesEnabled(bool enabled);
  int gridSpacing() const;
  void setGridSpacing(int spacing);
  QString diagramItemSizingMode() const;
  void setDiagramItemSizingMode(const QString &mode);
  QString defaultConnectorRouting() const;
  void setDefaultConnectorRouting(const QString &routing);
  QVariantMap relationshipGestureKeys() const;
  Q_INVOKABLE bool setRelationshipGestureKeys(const QVariantMap &keys);
  QString cppInterfacePattern() const;
  Q_INVOKABLE bool setCppInterfacePattern(const QString &pattern);
  Q_INVOKABLE bool isValidCppInterfacePattern(const QString &pattern) const;
  QVariantList cppMemberTypeRules() const;
  Q_INVOKABLE QVariantList cppMemberTypeRuleDefaults() const;
  QList<CppMemberTypeRule> cppMemberTypeRuleValues() const;
  Q_INVOKABLE bool setCppMemberTypeRules(const QVariantList &rules);
  QVariantMap contextToolboxConfiguration() const;
  Q_INVOKABLE QVariantMap contextToolboxDefaults() const;
  Q_INVOKABLE bool
  setContextToolboxConfiguration(const QVariantMap &configuration);
  QString packageReassignmentPolicy() const;
  void setPackageReassignmentPolicy(const QString &policy);
  bool automaticUpdateChecksEnabled() const;
  void setAutomaticUpdateChecksEnabled(bool enabled);
  bool automaticUpdateCheckDue(
      const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
  void recordUpdateCheck(
      const QDateTime &timestamp = QDateTime::currentDateTimeUtc());
  QVariantList recentProjects() const;
  QStringList projectTreeColumns() const;
  void setProjectTreeColumns(const QStringList &columns);
  QVariantMap projectTreeColumnWidths() const;
  void setProjectTreeColumnWidths(const QVariantMap &widths);
  bool projectTreeRelationshipsVisible() const;
  void setProjectTreeRelationshipsVisible(bool visible);
  QString sourceEditorCommand() const;
  void setSourceEditorCommand(const QString &command);
  bool sourceEditorDoubleClickEnabled() const;
  void setSourceEditorDoubleClickEnabled(bool enabled);
  Q_INVOKABLE void addRecentProject(const QString &projectPath);
  Q_INVOKABLE void clearRecentProjects();
  Q_INVOKABLE void resetDefaults();

signals:
  void defaultDistributionGapChanged();
  void snapToGridEnabledChanged();
  void alignmentGuidesEnabledChanged();
  void gridSpacingChanged();
  void diagramItemSizingModeChanged();
  void defaultConnectorRoutingChanged();
  void relationshipGestureKeysChanged();
  void cppInterfacePatternChanged();
  void cppMemberTypeRulesChanged();
  void contextToolboxConfigurationChanged();
  void packageReassignmentPolicyChanged();
  void automaticUpdateChecksEnabledChanged();
  void recentProjectsChanged();
  void projectTreeColumnsChanged();
  void projectTreeColumnWidthsChanged();
  void projectTreeRelationshipsVisibleChanged();
  void sourceEditorCommandChanged();
  void sourceEditorDoubleClickEnabledChanged();

private:
  void persistDiagramPreferences() const;
  void persistConnectorPreferences() const;
  void persistCppImportPreferences() const;
  void persistContextToolboxPreferences() const;
  void persistModelingPreferences() const;
  void persistUpdatePreferences() const;
  void persistRecentProjects() const;
  void persistProjectTreePreferences() const;
  void persistSourceEditorPreferences() const;

  int m_defaultDistributionGap = kDefaultDistributionGap;
  bool m_snapToGridEnabled = kDefaultSnapToGridEnabled;
  bool m_alignmentGuidesEnabled = kDefaultAlignmentGuidesEnabled;
  int m_gridSpacing = kDefaultGridSpacing;
  QString m_diagramItemSizingMode = QStringLiteral("content");
  ConnectorRouting m_defaultConnectorRouting = kDefaultConnectorRouting;
  QVariantMap m_relationshipGestureKeys;
  QString m_cppInterfacePattern;
  QList<CppMemberTypeRule> m_cppMemberTypeRules;
  QVariantMap m_contextToolboxConfiguration;
  QString m_packageReassignmentPolicy = QStringLiteral("ask");
  bool m_automaticUpdateChecksEnabled = kDefaultAutomaticUpdateChecksEnabled;
  QDateTime m_lastUpdateCheckUtc;
  QStringList m_recentProjectPaths;
  QStringList m_projectTreeColumns;
  QVariantMap m_projectTreeColumnWidths;
  bool m_projectTreeRelationshipsVisible =
      kDefaultProjectTreeRelationshipsVisible;
  QString m_sourceEditorCommand = QStringLiteral("code");
  bool m_sourceEditorDoubleClickEnabled =
      kDefaultSourceEditorDoubleClickEnabled;
};

} // namespace yauml
