#pragma once

#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace yauml {

class ApplicationSettings;
class DiagnosticModel;

namespace ui {

// Coordinates non-blocking update checks with the installed IFW maintenance
// tool. The controller never installs silently: it only reports availability
// and schedules the user-approved updater after the application has completed
// its normal save-and-close flow.
class UpdateController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
  Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY
                 updateAvailabilityChanged)
  Q_PROPERTY(QUrl releaseNotesUrl READ releaseNotesUrl NOTIFY
                 updateAvailabilityChanged)
  Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
  Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY
                 updateAvailabilityChanged)
  Q_PROPERTY(bool maintenanceToolAvailable READ maintenanceToolAvailable
                 CONSTANT)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)

public:
  UpdateController(QString currentVersion, QUrl manifestUrl,
                   ApplicationSettings *settings, DiagnosticModel *diagnostics,
                   QObject *parent = nullptr);

  QString currentVersion() const;
  QString availableVersion() const;
  QUrl releaseNotesUrl() const;
  bool checking() const;
  bool updateAvailable() const;
  bool maintenanceToolAvailable() const;
  QString statusMessage() const;

  Q_INVOKABLE void checkForUpdates(bool userInitiated);
  Q_INVOKABLE void checkAutomaticallyIfDue();
  Q_INVOKABLE bool scheduleUpdaterOnExit();
  Q_INVOKABLE bool openReleasePage();

signals:
  void checkingChanged();
  void updateAvailabilityChanged();
  void statusChanged();
  void checkFinished(bool userInitiated, bool updateAvailable, bool failed,
                     const QString &message);

private:
  void finishCheck(QNetworkReply *reply, bool userInitiated);
  void setStatusMessage(const QString &message);
  QString maintenanceToolPath() const;
  void launchScheduledUpdater();

  QString m_currentVersion;
  QUrl m_manifestUrl;
  ApplicationSettings *m_settings = nullptr;
  DiagnosticModel *m_diagnostics = nullptr;
  QNetworkAccessManager *m_network = nullptr;
  QString m_availableVersion;
  QUrl m_releaseNotesUrl;
  QString m_statusMessage;
  bool m_checking = false;
  bool m_updaterScheduled = false;
};

} // namespace ui
} // namespace yauml
