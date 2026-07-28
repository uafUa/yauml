#include "core/update_manifest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QVersionNumber>

#include <utility>

namespace uuml {
namespace {

std::optional<QVersionNumber> strictVersion(const QString &text) {
  qsizetype suffixIndex = 0;
  const QVersionNumber version =
      QVersionNumber::fromString(text.trimmed(), &suffixIndex);
  if (version.isNull() || suffixIndex != text.trimmed().size() ||
      version.segmentCount() < 3)
    return std::nullopt;
  return version;
}

bool isSecureHttpUrl(const QUrl &url) {
  return url.isValid() && url.scheme() == QStringLiteral("https") &&
         !url.host().isEmpty();
}

} // namespace

UpdateManifestOutcome parseUpdateManifest(const QByteArray &document) {
  QJsonParseError parseError;
  const QJsonDocument json = QJsonDocument::fromJson(document, &parseError);
  if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
    return {{},
            QStringLiteral("Invalid update manifest: %1")
                .arg(parseError.errorString())};
  }

  const QJsonObject object = json.object();
  if (object.value(QStringLiteral("schemaVersion")).toInt() != 1)
    return {{}, QStringLiteral("Unsupported update manifest schema")};

  UpdateManifest manifest;
  manifest.channel = object.value(QStringLiteral("channel")).toString();
  manifest.version = object.value(QStringLiteral("version")).toString();
  manifest.repositoryUrl =
      QUrl(object.value(QStringLiteral("repositoryUrl")).toString());
  manifest.releaseNotesUrl =
      QUrl(object.value(QStringLiteral("releaseNotesUrl")).toString());

  if (manifest.channel != QStringLiteral("stable"))
    return {{}, QStringLiteral("Unsupported update channel")};
  if (!strictVersion(manifest.version))
    return {{}, QStringLiteral("Invalid update version")};
  if (!isSecureHttpUrl(manifest.repositoryUrl))
    return {{}, QStringLiteral("Invalid update repository URL")};
  if (!isSecureHttpUrl(manifest.releaseNotesUrl))
    return {{}, QStringLiteral("Invalid release notes URL")};

  return {std::move(manifest), {}};
}

bool isNewerApplicationVersion(const QString &candidate,
                               const QString &current) {
  const auto candidateVersion = strictVersion(candidate);
  const auto currentVersion = strictVersion(current);
  return candidateVersion && currentVersion &&
         QVersionNumber::compare(*candidateVersion, *currentVersion) > 0;
}

} // namespace uuml
