#pragma once

#include "core/diagnostic_model.h"
#include "core/project_data.h"

#include <QByteArray>

namespace yauml {

struct SequenceDiagramImportOutcome {
  Diagram diagram;
  QList<Diagnostic> diagnostics;
  bool ok = false;
};

// Parses the compact, public sequence interchange format into the canonical
// project model. The service is deliberately independent from QML so Codex,
// the CLI, and a future GUI import action all share the exact same validation.
class SequenceDiagramImportService {
public:
  static SequenceDiagramImportOutcome parse(const QByteArray &contents,
                                            const ProjectData &project);
  static SequenceDiagramImportOutcome load(const QString &filePath,
                                           const ProjectData &project);

  // Appends only an already validated outcome. Keeping mutation separate from
  // parsing leaves the future GUI command free to wrap this operation in one
  // undo entry without reimplementing the import rules.
  static bool append(ProjectData &project,
                     const SequenceDiagramImportOutcome &outcome,
                     QList<Diagnostic> *diagnostics = nullptr);
};

} // namespace yauml
