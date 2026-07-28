#pragma once

#include "core/diagnostic_model.h"
#include "core/project_data.h"

#include <QByteArray>
#include <QMap>
#include <QStringList>

namespace yauml {

// Byte-level fingerprints of the files that produced an in-memory project.
// They are deliberately kept outside ProjectData: revisions describe the
// persistence session, not user model state, and must never make a project
// dirty or enter undo history.
struct ProjectFileRevision {
  QString rootPath;
  QMap<QString, QByteArray> fileDigests;

  bool isValid() const;
  bool operator==(const ProjectFileRevision &) const = default;
};

struct LoadOutcome {
  ProjectData project;
  ProjectFileRevision revision;
  QList<Diagnostic> diagnostics;
  bool ok = false;
  bool recovered = false;
  bool migrated = false;
};

struct SaveOutcome {
  ProjectFileRevision revision;
  QList<Diagnostic> diagnostics;
  QStringList externallyChangedFiles;
  bool ok = false;
  bool unchanged = false;
  bool externalChangesDetected = false;
};

class ProjectSerializer {
public:
  static LoadOutcome load(const QString &projectPath);
  static SaveOutcome save(const QString &projectPath,
                          const ProjectData &project,
                          const ProjectFileRevision &expectedRevision = {},
                          bool overwriteExternalChanges = false);
  static QList<Diagnostic> validate(const ProjectData &project);
  static QString normalizeProjectPath(const QString &path);
};

} // namespace yauml
