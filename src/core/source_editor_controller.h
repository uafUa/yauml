#pragma once

#include <QObject>
#include <QVariantMap>

namespace yauml {

class ApplicationSettings;
class ProjectController;

// Resolves imported source provenance and opens it in the user's configured
// source editor. Keeping process launch outside QML makes editor discovery,
// quoting, diagnostics, and source-location rules independently testable.
class SourceEditorController final : public QObject {
  Q_OBJECT

public:
  explicit SourceEditorController(ProjectController *project,
                                  ApplicationSettings *settings,
                                  QObject *parent = nullptr);

  Q_INVOKABLE QVariantMap sourceLocation(const QString &objectKind,
                                         const QString &objectId,
                                         int operationIndex = -1) const;
  Q_INVOKABLE bool canOpenObject(const QString &objectKind,
                                 const QString &objectId,
                                 int operationIndex = -1) const;
  Q_INVOKABLE bool openObject(const QString &objectKind,
                              const QString &objectId, int operationIndex = -1);

private:
  struct Location {
    QString filePath;
    int line = 1;
    int column = 1;
  };

  Location resolveLocation(const QString &objectKind, const QString &objectId,
                           int operationIndex) const;
  QString resolveExecutable(const QString &program) const;
  bool launch(const Location &location);
  void warn(const QString &message, const QString &objectId = {}) const;

  ProjectController *m_project = nullptr;
  ApplicationSettings *m_settings = nullptr;
};

} // namespace yauml
