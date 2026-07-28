#pragma once

#include "core/diagnostic_model.h"

#include <QJsonObject>

namespace yauml {

// Migrations operate on all persisted documents together. This keeps future
// cross-file changes atomic and prevents migration details from leaking into
// the domain-model deserializer.
struct ProjectJsonDocuments {
  QJsonObject manifest;
  QJsonObject model;
  QJsonObject diagrams;
};

struct SchemaMigrationOutcome {
  ProjectJsonDocuments documents;
  QList<Diagnostic> diagnostics;
  int sourceVersion = -1;
  bool ok = false;
  bool migrated = false;
};

class ProjectSchemaMigrator final {
public:
  static SchemaMigrationOutcome migrate(ProjectJsonDocuments documents);
};

} // namespace yauml
