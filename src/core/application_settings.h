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
  Q_PROPERTY(bool snapToGridEnabled READ snapToGridEnabled WRITE
                 setSnapToGridEnabled NOTIFY snapToGridEnabledChanged)
  Q_PROPERTY(bool alignmentGuidesEnabled READ alignmentGuidesEnabled WRITE
                 setAlignmentGuidesEnabled NOTIFY alignmentGuidesEnabledChanged)
  Q_PROPERTY(int gridSpacing READ gridSpacing WRITE setGridSpacing NOTIFY
                 gridSpacingChanged)
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
  static constexpr int kMaximumRecentProjects = 10;

  explicit ApplicationSettings(QObject *parent = nullptr);

  int defaultDistributionGap() const;
  void setDefaultDistributionGap(int gap);
  bool snapToGridEnabled() const;
  void setSnapToGridEnabled(bool enabled);
  bool alignmentGuidesEnabled() const;
  void setAlignmentGuidesEnabled(bool enabled);
  int gridSpacing() const;
  void setGridSpacing(int spacing);
  QVariantList recentProjects() const;
  Q_INVOKABLE void addRecentProject(const QString &projectPath);
  Q_INVOKABLE void clearRecentProjects();
  Q_INVOKABLE void resetDefaults();

signals:
  void defaultDistributionGapChanged();
  void snapToGridEnabledChanged();
  void alignmentGuidesEnabledChanged();
  void gridSpacingChanged();
  void recentProjectsChanged();

private:
  void persistDiagramPreferences() const;
  void persistRecentProjects() const;

  int m_defaultDistributionGap = kDefaultDistributionGap;
  bool m_snapToGridEnabled = kDefaultSnapToGridEnabled;
  bool m_alignmentGuidesEnabled = kDefaultAlignmentGuidesEnabled;
  int m_gridSpacing = kDefaultGridSpacing;
  QStringList m_recentProjectPaths;
};

} // namespace uuml
