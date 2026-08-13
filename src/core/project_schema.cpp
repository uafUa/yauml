#include "core/project_schema.h"

#include "core/model_operation.h"
#include "core/project_schema_version.h"
#include "core/stereotype_catalog.h"

#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>
#include <utility>

namespace yauml {
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

QJsonArray migrateOperations(const QJsonValue &value,
                             const QString &elementId) {
  QJsonArray migrated;
  const QJsonArray operations = value.toArray();
  for (int index = 0; index < operations.size(); ++index) {
    const QJsonValue operation = operations.at(index);
    if (operation.isString()) {
      const QString stableId =
          QStringLiteral("%1:operation:%2").arg(elementId).arg(index + 1);
      migrated.append(modelOperationToJson(
          modelOperationFromSignature(operation.toString(), stableId)));
    } else {
      // Preserve malformed and extension values for normal schema validation.
      migrated.append(operation);
    }
  }
  return migrated;
}

void migrateVersionThreeToFour(ProjectJsonDocuments &documents) {
  // Schema 4 promotes operation signatures from presentation strings to
  // semantic records. Convert both the live element and its C++ import
  // baseline with identical deterministic IDs so the first synchronization
  // after migration does not report every operation as changed.
  QJsonArray elements =
      documents.model.value(QStringLiteral("elements")).toArray();
  for (int elementIndex = 0; elementIndex < elements.size(); ++elementIndex) {
    if (!elements.at(elementIndex).isObject())
      continue;
    QJsonObject element = elements.at(elementIndex).toObject();
    QString elementId = element.value(QStringLiteral("id")).toString();
    if (elementId.isEmpty())
      elementId = QStringLiteral("legacy-element-%1").arg(elementIndex + 1);
    element.insert(QStringLiteral("operations"),
                   migrateOperations(
                       element.value(QStringLiteral("operations")), elementId));

    QJsonObject binding =
        element.value(QStringLiteral("sourceBinding")).toObject();
    if (!binding.isEmpty()) {
      QJsonObject baseline =
          binding.value(QStringLiteral("lastImported")).toObject();
      if (!baseline.isEmpty()) {
        baseline.insert(
            QStringLiteral("operations"),
            migrateOperations(baseline.value(QStringLiteral("operations")),
                              elementId));
        binding.insert(QStringLiteral("lastImported"), baseline);
        element.insert(QStringLiteral("sourceBinding"), binding);
      }
    }
    elements.replace(elementIndex, element);
  }
  documents.model.insert(QStringLiteral("elements"), elements);
  documents.manifest.insert(QStringLiteral("schemaVersion"), 4);
}

void migrateVersionFourToFive(ProjectJsonDocuments &documents) {
  // Schema 5 introduces multiple diagram kinds. All preceding diagrams are
  // class diagrams, so make that fact explicit before the deserializer starts
  // requiring a kind. Sequence-specific data is additive and needs no legacy
  // conversion.
  QJsonArray diagrams =
      documents.diagrams.value(QStringLiteral("diagrams")).toArray();
  for (int index = 0; index < diagrams.size(); ++index) {
    if (!diagrams.at(index).isObject())
      continue;
    QJsonObject diagram = diagrams.at(index).toObject();
    if (!diagram.contains(QStringLiteral("type")))
      diagram.insert(QStringLiteral("type"), QStringLiteral("class"));
    diagrams.replace(index, diagram);
  }
  documents.diagrams.insert(QStringLiteral("diagrams"), diagrams);
  documents.manifest.insert(QStringLiteral("schemaVersion"), 5);
}

// Combined fragments are optional sequence-diagram records. Version six is
// additive, so legacy documents need no materialized changes.
void migrateVersionFiveToSix(ProjectJsonDocuments &documents) {
  documents.manifest.insert(QStringLiteral("schemaVersion"), 6);
}

// Sequence-message operation links are optional. Version seven only advances
// the manifest marker; absent links remain deliberately unlinked.
void migrateVersionSixToSeven(ProjectJsonDocuments &documents) {
  documents.manifest.insert(QStringLiteral("schemaVersion"), 7);
}

void migrateVersionSevenToEight(ProjectJsonDocuments &documents) {
  // Schema 8 generalizes the fixed first/second representation into an
  // ordered operand list. Materialize the old fields before deserialization;
  // this keeps the runtime model and serializer free of compatibility paths.
  QJsonArray diagrams =
      documents.diagrams.value(QStringLiteral("diagrams")).toArray();
  for (int diagramIndex = 0; diagramIndex < diagrams.size(); ++diagramIndex) {
    if (!diagrams.at(diagramIndex).isObject())
      continue;
    QJsonObject diagram = diagrams.at(diagramIndex).toObject();
    QJsonObject sequence = diagram.value(QStringLiteral("sequence")).toObject();
    QJsonArray fragments =
        sequence.value(QStringLiteral("fragments")).toArray();
    for (int fragmentIndex = 0; fragmentIndex < fragments.size();
         ++fragmentIndex) {
      if (!fragments.at(fragmentIndex).isObject())
        continue;
      QJsonObject fragment = fragments.at(fragmentIndex).toObject();
      if (!fragment.value(QStringLiteral("operands")).isArray()) {
        QJsonArray operands;
        QJsonObject first;
        first.insert(QStringLiteral("startMessageId"),
                     fragment.value(QStringLiteral("startMessageId")));
        if (fragment.contains(QStringLiteral("guard")))
          first.insert(QStringLiteral("guard"),
                       fragment.value(QStringLiteral("guard")));
        operands.append(first);

        const QString alternativeStart =
            fragment.value(QStringLiteral("alternativeStartMessageId"))
                .toString();
        if (!alternativeStart.isEmpty()) {
          QJsonObject alternative;
          alternative.insert(QStringLiteral("startMessageId"),
                             alternativeStart);
          if (fragment.contains(QStringLiteral("alternativeGuard")))
            alternative.insert(
                QStringLiteral("guard"),
                fragment.value(QStringLiteral("alternativeGuard")));
          operands.append(alternative);
        }
        fragment.insert(QStringLiteral("operands"), operands);
      }
      fragment.remove(QStringLiteral("guard"));
      fragment.remove(QStringLiteral("alternativeStartMessageId"));
      fragment.remove(QStringLiteral("alternativeGuard"));
      fragments.replace(fragmentIndex, fragment);
    }
    if (!fragments.isEmpty())
      sequence.insert(QStringLiteral("fragments"), fragments);
    diagram.insert(QStringLiteral("sequence"), sequence);
    diagrams.replace(diagramIndex, diagram);
  }
  documents.diagrams.insert(QStringLiteral("diagrams"), diagrams);
  documents.manifest.insert(QStringLiteral("schemaVersion"), 8);
}

// A message's source activation is optional. Schema 9 records the exact bar
// selected by pointer-authored messages; legacy messages retain the historical
// innermost-active-bar fallback without requiring synthesized references.
void migrateVersionEightToNine(ProjectJsonDocuments &documents) {
  documents.manifest.insert(QStringLiteral("schemaVersion"), 9);
}

// Target-operation presentation is an optional sequence-diagram setting.
// Existing diagrams retain the historical behavior of showing only their
// free-form message labels, represented by the new `hidden` default.
void migrateVersionNineToTen(ProjectJsonDocuments &documents) {
  documents.manifest.insert(QStringLiteral("schemaVersion"), 10);
}

// New sequence diagrams show message-kind stereotypes by default. Existing
// diagrams retain their previous appearance by persisting the hidden state.
void migrateVersionTenToEleven(ProjectJsonDocuments &documents) {
  QJsonArray diagrams =
      documents.diagrams.value(QStringLiteral("diagrams")).toArray();
  for (qsizetype index = 0; index < diagrams.size(); ++index) {
    QJsonObject diagram = diagrams.at(index).toObject();
    if (diagram.value(QStringLiteral("type")).toString() !=
        QStringLiteral("sequence")) {
      continue;
    }
    QJsonObject sequence = diagram.value(QStringLiteral("sequence")).toObject();
    sequence.insert(QStringLiteral("showMessageStereotypes"), false);
    diagram.insert(QStringLiteral("sequence"), sequence);
    diagrams.replace(index, diagram);
  }
  documents.diagrams.insert(QStringLiteral("diagrams"), diagrams);
  documents.manifest.insert(QStringLiteral("schemaVersion"), 11);
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
                       "version %2; update yauml before opening this project")
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
    case 3:
      migrateVersionThreeToFour(outcome.documents);
      version = 4;
      break;
    case 4:
      migrateVersionFourToFive(outcome.documents);
      version = 5;
      break;
    case 5:
      migrateVersionFiveToSix(outcome.documents);
      version = 6;
      break;
    case 6:
      migrateVersionSixToSeven(outcome.documents);
      version = 7;
      break;
    case 7:
      migrateVersionSevenToEight(outcome.documents);
      version = 8;
      break;
    case 8:
      migrateVersionEightToNine(outcome.documents);
      version = 9;
      break;
    case 9:
      migrateVersionNineToTen(outcome.documents);
      version = 10;
      break;
    case 10:
      migrateVersionTenToEleven(outcome.documents);
      version = 11;
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

} // namespace yauml
