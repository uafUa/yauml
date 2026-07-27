#include "core/project_schema.h"

#include "core/project_schema_version.h"
#include "core/stereotype_catalog.h"

#include <QJsonArray>
#include <QSet>
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

QJsonObject stereotypeDefinitionJson(const StereotypeDefinition &definition) {
  QJsonArray applicableTo;
  for (const QString &value : definition.applicableTo)
    applicableTo.append(value);
  return {{QStringLiteral("id"), definition.id},
          {QStringLiteral("name"), definition.name},
          {QStringLiteral("applicableTo"), applicableTo}};
}

void prependMissingDefinitions(ProjectJsonDocuments &documents,
                               const QList<StereotypeDefinition> &definitions) {
  const QJsonValue catalogValue =
      documents.model.value(QStringLiteral("stereotypes"));
  if (catalogValue.isUndefined() || catalogValue.isArray()) {
    const QJsonArray existing = catalogValue.toArray();
    QSet<QString> existingIds;
    QSet<QString> existingNames;
    for (const QJsonValue &value : existing) {
      if (!value.isObject())
        continue;
      const QJsonObject object = value.toObject();
      existingIds.insert(object.value(QStringLiteral("id")).toString());
      existingNames.insert(object.value(QStringLiteral("name"))
                               .toString()
                               .trimmed()
                               .toCaseFolded());
    }

    QJsonArray migrated;
    for (const auto &definition : definitions) {
      if (!existingIds.contains(definition.id) &&
          !existingNames.contains(definition.name.trimmed().toCaseFolded()))
        migrated.append(stereotypeDefinitionJson(definition));
    }
    for (const QJsonValue &value : existing)
      migrated.append(value);
    documents.model.insert(QStringLiteral("stereotypes"), migrated);
  }
  // Preserve malformed values for the normal load validator to report rather
  // than silently replacing user data during migration.
}

void migrateVersionOneToTwo(ProjectJsonDocuments &documents) {
  // In schema 1 the conventional UML entries lived in the application and
  // only custom entries were serialized. Schema 2 makes the whole catalog
  // project-owned. Copy each missing default once, retaining all custom entries
  // and their order after the seeded section.
  prependMissingDefinitions(documents,
                            stereotype_catalog::defaultDefinitions());
  documents.manifest.insert(QStringLiteral("schemaVersion"), 2);
}

void migrateVersionTwoToThree(ProjectJsonDocuments &documents) {
  // Schema 3 adds conventional source-visibility stereotypes. Do not reseed
  // unrelated catalog entries that a user may have deliberately deleted. A
  // same-named custom entry is also retained as the project's definition.
  prependMissingDefinitions(documents,
                            stereotype_catalog::sourceVisibilityDefinitions());
  documents.manifest.insert(QStringLiteral("schemaVersion"), 3);
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
    case 1:
      migrateVersionOneToTwo(outcome.documents);
      version = 2;
      break;
    case 2:
      migrateVersionTwoToThree(outcome.documents);
      version = 3;
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
        QStringLiteral("Migrated project schema from version %1 to %2 "
                       "in memory; save the project to persist the upgrade")
            .arg(outcome.sourceVersion)
            .arg(version)));
  }
  outcome.ok = true;
  return outcome;
}

} // namespace uuml
