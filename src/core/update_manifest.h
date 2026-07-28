#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <optional>

namespace uuml {

// Small application-facing description published beside the Qt Installer
// Framework repository. Keeping this separate from IFW's internal Updates.xml
// schema gives the application a stable place for release notes and channel
// metadata while IFW remains the authority that downloads and verifies files.
struct UpdateManifest {
  QString channel;
  QString version;
  QUrl repositoryUrl;
  QUrl releaseNotesUrl;
};

struct UpdateManifestOutcome {
  std::optional<UpdateManifest> manifest;
  QString error;

  explicit operator bool() const { return manifest.has_value(); }
};

UpdateManifestOutcome parseUpdateManifest(const QByteArray &document);
bool isNewerApplicationVersion(const QString &candidate,
                               const QString &current);

} // namespace uuml
