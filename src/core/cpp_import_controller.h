#pragma once

#include "core/cpp_import.h"

#include <QFutureWatcher>
#include <QObject>
#include <QUrl>
#include <QVariantList>

namespace uuml {

class ApplicationSettings;
class ProjectController;

// QML-facing orchestration only. Clang discovery and import planning remain in
// CppImportService so the GUI and headless commands use identical behavior.
class CppImportController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(bool canApply READ canApply NOTIFY previewChanged)
  Q_PROPERTY(bool canSynchronize READ canSynchronize NOTIFY
                 synchronizationStateChanged)
  Q_PROPERTY(QStringList configuredSourceRoots READ configuredSourceRoots NOTIFY
                 synchronizationStateChanged)
  Q_PROPERTY(QString configuredSourceRoot READ configuredSourceRoot NOTIFY
                 synchronizationStateChanged)
  Q_PROPERTY(QString configuredSourceRootsText READ configuredSourceRootsText
                 NOTIFY synchronizationStateChanged)
  Q_PROPERTY(QStringList previewSourceRoots READ previewSourceRoots NOTIFY
                 previewChanged)
  Q_PROPERTY(
      QString previewSourceRoot READ previewSourceRoot NOTIFY previewChanged)
  Q_PROPERTY(QString previewSourceRootsText READ previewSourceRootsText NOTIFY
                 previewChanged)
  Q_PROPERTY(QString summary READ summary NOTIFY previewChanged)
  Q_PROPERTY(QString compilationDatabasePath READ compilationDatabasePath NOTIFY
                 previewChanged)
  Q_PROPERTY(QVariantList previewItems READ previewItems NOTIFY previewChanged)

public:
  explicit CppImportController(ProjectController *project,
                               ApplicationSettings *settings,
                               QObject *parent = nullptr);

  bool busy() const;
  bool canApply() const;
  bool canSynchronize() const;
  QStringList configuredSourceRoots() const;
  QString configuredSourceRoot() const;
  QString configuredSourceRootsText() const;
  QStringList previewSourceRoots() const;
  QString previewSourceRoot() const;
  QString previewSourceRootsText() const;
  QString summary() const;
  QString compilationDatabasePath() const;
  QVariantList previewItems() const;

  Q_INVOKABLE void preview(const QUrl &sourceOrBuildDirectory);
  Q_INVOKABLE void previewSources(const QVariantList &sourceDirectories);
  Q_INVOKABLE void synchronize();
  Q_INVOKABLE void applyPreview();
  Q_INVOKABLE void clearPreview();

signals:
  void busyChanged();
  void previewChanged();
  void synchronizationStateChanged();
  void attentionRequired();
  void importApplied(int count);

private:
  void previewPaths(const QStringList &sourceDirectories);
  void finishPreview();
  void publishDiagnostics(const QList<Diagnostic> &diagnostics);
  void rebuildViewState();

  ProjectController *m_project;
  ApplicationSettings *m_settings;
  QFutureWatcher<CppImportPreview> m_watcher;
  CppImportPreview m_preview;
  QVariantList m_previewItems;
  QString m_summary;
  QStringList m_requestedSourcePaths;
  bool m_busy = false;
};

} // namespace uuml
