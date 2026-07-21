#pragma once

#include "core/diagnostic_model.h"
#include "core/project_data.h"

namespace uuml {

struct LoadOutcome {
  ProjectData project;
  QList<Diagnostic> diagnostics;
  bool ok = false;
  bool recovered = false;
};

struct SaveOutcome {
  QList<Diagnostic> diagnostics;
  bool ok = false;
  bool unchanged = false;
};

class ProjectSerializer {
public:
  static LoadOutcome load(const QString &projectPath);
  static SaveOutcome save(const QString &projectPath,
                          const ProjectData &project);
  static QList<Diagnostic> validate(const ProjectData &project);
  static QString normalizeProjectPath(const QString &path);
};

} // namespace uuml
