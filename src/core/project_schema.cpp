#include "core/project_schema.h"

#include "core/project_schema_version.h"

#include <cmath>
#include <limits>
#include <utility>

namespace uuml {
namespace {

Diagnostic schemaError(const QString &message) {
  return {DiagnosticSeverity::Error, QStringLiteral("schema"), message};
}

Diagnostic migrationInfo(const QString &message) {
  return {DiagnosticSeverity::Info, QStringLiteral("migration"), message};
}

bool readSchemaVersion(const QJsonObject &manifest, int &version,
                       QList<Diagnostic> &diagnostics) {
  const QJsonValue value = manifest.value(QStringLiteral("schemaVersion"));
  if (value.isUndefined()) {
    // Early POC projects used the current three-file shape before an explicit
    // version field was introduced. Treat only a genuinely absent field as
    // that legacy version; null, strings, and fractions remain malformed.
    version = 0;
    return true;
  }

  const double numericVersion = value.toDouble(-1.0);
  const bool representable = std::isfinite(numericVersion) &&
                             numericVersion >= 0.0 &&
                             numericVersion <= std::numeric_limits<int>::max();
  if (!value.isDouble() || !representable ||
      numericVersion != std::floor(numericVersion)) {
    diagnostics.append(schemaError(QStringLiteral(
        "Project schemaVersion must be a non-negative integer")));
    return false;
  }

  version = static_cast<int>(numericVersion);
  return true;
}

void migrateVersionZeroToOne(ProjectJsonDocuments &documents) {
  // Version zero is the unversioned form of the same three-document schema.
  // Explicitly inserting the version is still a real migration: downstream
  // code receives one canonical representation and the next save persists it.
  documents.manifest.insert(QStringLiteral("schemaVersion"), 1);
}

} // namespace

SchemaMigrationOutcome
ProjectSchemaMigrator::migrate(ProjectJsonDocuments documents) {
  SchemaMigrationOutcome outcome;
  outcome.documents = std::move(documents);

  if (!readSchemaVersion(outcome.documents.manifest, outcome.sourceVersion,
                         outcome.diagnostics))
    return outcome;

  if (outcome.sourceVersion > kCurrentProjectSchemaVersion) {
    outcome.diagnostics.append(schemaError(
        QStringLiteral("Project schema version %1 is newer than the supported "
                       "version %2; update uuml before opening this project")
            .arg(outcome.sourceVersion)
            .arg(kCurrentProjectSchemaVersion)));
    return outcome;
  }

  int version = outcome.sourceVersion;
  while (version < kCurrentProjectSchemaVersion) {
    switch (version) {
    case 0:
      migrateVersionZeroToOne(outcome.documents);
      version = 1;
      break;
    default:
      outcome.diagnostics.append(schemaError(
          QStringLiteral("No migration path exists from schema version %1")
              .arg(version)));
      return outcome;
    }
  }

  outcome.migrated = outcome.sourceVersion != version;
  if (outcome.migrated) {
    outcome.diagnostics.append(migrationInfo(
        QStringLiteral("Migrated project schema from legacy version %1 to %2 "
                       "in memory; save the project to persist the upgrade")
            .arg(outcome.sourceVersion)
            .arg(version)));
  }
  outcome.ok = true;
  return outcome;
}

} // namespace uuml
