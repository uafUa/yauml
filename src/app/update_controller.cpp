#include "app/update_controller.h"

#include "core/application_settings.h"
#include "core/diagnostic_model.h"
#include "core/update_manifest.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QScopeGuard>
#include <QTimer>

#include <utility>

namespace uuml::ui {
namespace {

constexpr int kNetworkTimeoutMilliseconds = 15000;

} // namespace

UpdateController::UpdateController(QString currentVersion, QUrl manifestUrl,
                                   ApplicationSettings *settings,
                                   DiagnosticModel *diagnostics,
                                   QObject *parent)
    : QObject(parent), m_currentVersion(std::move(currentVersion)),
      m_manifestUrl(std::move(manifestUrl)), m_settings(settings),
      m_diagnostics(diagnostics), m_network(new QNetworkAccessManager(this)) {
  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
          &UpdateController::launchScheduledUpdater);
}

QString UpdateController::currentVersion() const { return m_currentVersion; }

QString UpdateController::availableVersion() const {
  return m_availableVersion;
}

QUrl UpdateController::releaseNotesUrl() const { return m_releaseNotesUrl; }

bool UpdateController::checking() const { return m_checking; }

bool UpdateController::updateAvailable() const {
  return !m_availableVersion.isEmpty();
}

bool UpdateController::maintenanceToolAvailable() const {
  return QFileInfo::exists(maintenanceToolPath());
}

QString UpdateController::statusMessage() const { return m_statusMessage; }

void UpdateController::checkForUpdates(bool userInitiated) {
  if (m_checking)
    return;
  if (!m_manifestUrl.isValid()) {
    const QString message = tr("Update service is not configured.");
    setStatusMessage(message);
    emit checkFinished(userInitiated, false, true, message);
    return;
  }

  m_checking = true;
  emit checkingChanged();
  setStatusMessage(tr("Checking for updates…"));

  QNetworkRequest request(m_manifestUrl);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("uuml/%1").arg(m_currentVersion));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply *reply = m_network->get(request);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, userInitiated] {
            finishCheck(reply, userInitiated);
          });

  const QPointer<QNetworkReply> guardedReply(reply);
  QTimer::singleShot(kNetworkTimeoutMilliseconds, reply, [guardedReply] {
    if (guardedReply && guardedReply->isRunning())
      guardedReply->abort();
  });
}

void UpdateController::checkAutomaticallyIfDue() {
  // Development build trees do not have a maintenance tool. Avoid surprising
  // network traffic there; portable builds retain the explicit Help action.
  if (!maintenanceToolAvailable() || !m_settings ||
      !m_settings->automaticUpdateCheckDue())
    return;
  checkForUpdates(false);
}

bool UpdateController::scheduleUpdaterOnExit() {
  if (!maintenanceToolAvailable())
    return false;
  m_updaterScheduled = true;
  return true;
}

bool UpdateController::openReleasePage() {
  return m_releaseNotesUrl.isValid() &&
         QDesktopServices::openUrl(m_releaseNotesUrl);
}

void UpdateController::finishCheck(QNetworkReply *reply, bool userInitiated) {
  const auto cleanup = qScopeGuard([reply] { reply->deleteLater(); });
  m_checking = false;
  emit checkingChanged();
  if (m_settings)
    m_settings->recordUpdateCheck();

  if (reply->error() != QNetworkReply::NoError) {
    const QString message =
        tr("Could not check for updates: %1").arg(reply->errorString());
    setStatusMessage(message);
    emit checkFinished(userInitiated, false, true, message);
    return;
  }

  const UpdateManifestOutcome outcome = parseUpdateManifest(reply->readAll());
  if (!outcome) {
    const QString message =
        tr("Could not check for updates: %1").arg(outcome.error);
    setStatusMessage(message);
    emit checkFinished(userInitiated, false, true, message);
    return;
  }

  const bool available =
      isNewerApplicationVersion(outcome.manifest->version, m_currentVersion);
  const QString nextVersion = available ? outcome.manifest->version : QString{};
  const QUrl nextReleaseNotes =
      available ? outcome.manifest->releaseNotesUrl : QUrl{};
  if (m_availableVersion != nextVersion ||
      m_releaseNotesUrl != nextReleaseNotes) {
    m_availableVersion = nextVersion;
    m_releaseNotesUrl = nextReleaseNotes;
    emit updateAvailabilityChanged();
  }

  const QString message =
      available
          ? tr("Version %1 is available.").arg(m_availableVersion)
          : tr("You are using the latest version (%1).").arg(m_currentVersion);
  setStatusMessage(message);
  emit checkFinished(userInitiated, available, false, message);
}

void UpdateController::setStatusMessage(const QString &message) {
  if (m_statusMessage == message)
    return;
  m_statusMessage = message;
  emit statusChanged();
}

QString UpdateController::maintenanceToolPath() const {
#ifdef Q_OS_WIN
  constexpr auto fileName = "maintenancetool.exe";
#else
  constexpr auto fileName = "maintenancetool";
#endif
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(QLatin1String(fileName));
}

void UpdateController::launchScheduledUpdater() {
  if (!m_updaterScheduled)
    return;
  m_updaterScheduled = false;
  if (!QProcess::startDetached(maintenanceToolPath(),
                               {QStringLiteral("--start-updater")}) &&
      m_diagnostics) {
    m_diagnostics->addError(
        QStringLiteral("updates"),
        tr("The maintenance tool could not be started."));
  }
}

} // namespace uuml::ui
