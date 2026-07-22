#pragma once

#include "core/project_data.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace uuml {

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
  Q_PROPERTY(
      QString defaultConnectorRouting READ defaultConnectorRouting WRITE
          setDefaultConnectorRouting NOTIFY defaultConnectorRoutingChanged)
  Q_PROPERTY(QVariantMap relationshipGestureKeys READ relationshipGestureKeys
                 NOTIFY relationshipGestureKeysChanged)
  Q_PROPERTY(QString cppInterfacePattern READ cppInterfacePattern NOTIFY
                 cppInterfacePatternChanged)
  Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY
                 recentProjectsChanged)

public:
  static constexpr int kDefaultDistributionGap = 10;
  static constexpr int kMinimumDistributionGap = 1;
  static constexpr int kMaximumDistributionGap = 1000;
  static constexpr bool kDefaultSnapToGridEnabled = true;
  static constexpr bool kDefaultAlignmentGuidesEnabled = true;
  static constexpr int kDefaultGridSpacing = 20;
  static constexpr int kMinimumGridSpacing = 5;
  static constexpr int kMaximumGridSpacing = 200;
  static constexpr ConnectorRouting kDefaultConnectorRouting =
      ConnectorRouting::Straight;
  static constexpr int kMaximumRecentProjects = 10;

  static QVariantMap defaultRelationshipGestureKeys();
  static QString defaultCppInterfacePattern();

  explicit ApplicationSettings(QObject *parent = nullptr);

  int defaultDistributionGap() const;
  void setDefaultDistributionGap(int gap);
  bool snapToGridEnabled() const;
  void setSnapToGridEnabled(bool enabled);
  bool alignmentGuidesEnabled() const;
  void setAlignmentGuidesEnabled(bool enabled);
  int gridSpacing() const;
  void setGridSpacing(int spacing);
  QString defaultConnectorRouting() const;
  void setDefaultConnectorRouting(const QString &routing);
  QVariantMap relationshipGestureKeys() const;
  Q_INVOKABLE bool setRelationshipGestureKeys(const QVariantMap &keys);
  QString cppInterfacePattern() const;
  Q_INVOKABLE bool setCppInterfacePattern(const QString &pattern);
  Q_INVOKABLE bool isValidCppInterfacePattern(const QString &pattern) const;
  QVariantList recentProjects() const;
  Q_INVOKABLE void addRecentProject(const QString &projectPath);
  Q_INVOKABLE void clearRecentProjects();
  Q_INVOKABLE void resetDefaults();

signals:
  void defaultDistributionGapChanged();
  void snapToGridEnabledChanged();
  void alignmentGuidesEnabledChanged();
  void gridSpacingChanged();
  void defaultConnectorRoutingChanged();
  void relationshipGestureKeysChanged();
  void cppInterfacePatternChanged();
  void recentProjectsChanged();

private:
  void persistDiagramPreferences() const;
  void persistConnectorPreferences() const;
  void persistCppImportPreferences() const;
  void persistRecentProjects() const;

  int m_defaultDistributionGap = kDefaultDistributionGap;
  bool m_snapToGridEnabled = kDefaultSnapToGridEnabled;
  bool m_alignmentGuidesEnabled = kDefaultAlignmentGuidesEnabled;
  int m_gridSpacing = kDefaultGridSpacing;
  ConnectorRouting m_defaultConnectorRouting = kDefaultConnectorRouting;
  QVariantMap m_relationshipGestureKeys;
  QString m_cppInterfacePattern;
  QStringList m_recentProjectPaths;
};

} // namespace uuml
