#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

namespace uuml {

// Application-wide preferences that are independent of the currently open
// project. Values are persisted through QSettings and exposed to QML through
// one long-lived instance owned by main().
class ApplicationSettings final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int defaultDistributionGap READ defaultDistributionGap WRITE
                 setDefaultDistributionGap NOTIFY defaultDistributionGapChanged)
  Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY
                 recentProjectsChanged)

public:
  static constexpr int kDefaultDistributionGap = 10;
  static constexpr int kMinimumDistributionGap = 1;
  static constexpr int kMaximumDistributionGap = 1000;
  static constexpr int kMaximumRecentProjects = 10;

  explicit ApplicationSettings(QObject *parent = nullptr);

  int defaultDistributionGap() const;
  void setDefaultDistributionGap(int gap);
  QVariantList recentProjects() const;
  Q_INVOKABLE void addRecentProject(const QString &projectPath);
  Q_INVOKABLE void clearRecentProjects();
  Q_INVOKABLE void resetDefaults();

signals:
  void defaultDistributionGapChanged();
  void recentProjectsChanged();

private:
  void persistDiagramPreferences() const;
  void persistRecentProjects() const;

  int m_defaultDistributionGap = kDefaultDistributionGap;
  QStringList m_recentProjectPaths;
};

} // namespace uuml
