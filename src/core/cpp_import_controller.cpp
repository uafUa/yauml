#include "core/cpp_import_controller.h"

#include "core/application_settings.h"
#include "core/project_controller.h"

#include <QFileInfo>
#include <QPointer>
#include <QtConcurrentRun>
#include <algorithm>

namespace uuml {

CppImportController::CppImportController(ProjectController *project,
                                         ApplicationSettings *settings,
                                         QObject *parent)
    : QObject(parent), m_project(project), m_settings(settings) {
  Q_ASSERT(m_project);
  Q_ASSERT(m_settings);
  connect(&m_watcher, &QFutureWatcher<CppImportPreview>::finished, this,
          &CppImportController::finishPreview);
  connect(m_project, &ProjectController::projectChanged, this,
          &CppImportController::synchronizationStateChanged);
}

bool CppImportController::busy() const { return m_busy; }

bool CppImportController::canApply() const {
  return !m_busy && m_preview.ok &&
         (m_preview.applicableCount() > 0 ||
          (!m_preview.sourceRoots.isEmpty() &&
           m_preview.sourceRoots != configuredSourceRoots()));
}

bool CppImportController::canSynchronize() const {
  return !m_busy && !configuredSourceRoots().isEmpty();
}

QStringList CppImportController::configuredSourceRoots() const {
  return m_project ? m_project->data().cppImport.sourceRoots : QStringList{};
}

QString CppImportController::configuredSourceRoot() const {
  const QStringList roots = configuredSourceRoots();
  return roots.isEmpty() ? QString{} : roots.first();
}

QString CppImportController::configuredSourceRootsText() const {
  return configuredSourceRoots().join(u'\n');
}

QStringList CppImportController::previewSourceRoots() const {
  return !m_preview.sourceRoots.isEmpty() ? m_preview.sourceRoots
                                          : m_requestedSourcePaths;
}

QString CppImportController::previewSourceRoot() const {
  const QStringList roots = previewSourceRoots();
  return roots.isEmpty() ? QString{} : roots.first();
}

QString CppImportController::previewSourceRootsText() const {
  return previewSourceRoots().join(u'\n');
}

QString CppImportController::summary() const { return m_summary; }

QString CppImportController::compilationDatabasePath() const {
  return m_preview.compilationDatabasePath;
}

QVariantList CppImportController::previewItems() const {
  return m_previewItems;
}

QString CppImportController::progressText() const { return m_progressText; }

QString CppImportController::progressDetail() const { return m_progressDetail; }

int CppImportController::progressValue() const { return m_progressValue; }

int CppImportController::progressMaximum() const { return m_progressMaximum; }

void CppImportController::preview(const QUrl &sourceOrBuildDirectory) {
  const QString path = sourceOrBuildDirectory.isLocalFile()
                           ? sourceOrBuildDirectory.toLocalFile()
                           : sourceOrBuildDirectory.toString();
  previewPaths({path});
}

void CppImportController::previewSources(
    const QVariantList &sourceDirectories) {
  QStringList paths;
  paths.reserve(sourceDirectories.size());
  for (const QVariant &value : sourceDirectories) {
    const QUrl url = value.canConvert<QUrl>() ? value.toUrl() : QUrl{};
    const QString path =
        url.isValid() ? (url.isLocalFile() ? url.toLocalFile() : url.toString())
                      : value.toString();
    if (!path.trimmed().isEmpty())
      paths.append(path);
  }
  previewPaths(paths);
}

void CppImportController::previewPaths(const QStringList &sourceDirectories) {
  if (m_busy || !m_project)
    return;
  QStringList paths;
  for (const QString &path : sourceDirectories) {
    if (!path.trimmed().isEmpty())
      paths.append(QFileInfo(path).absoluteFilePath());
  }
  if (paths.isEmpty())
    return;

  m_requestedSourcePaths = paths;
  m_preview = {};
  m_previewItems.clear();
  m_summary = QStringLiteral("Discovering C++ declarations…");
  resetProgress();
  m_progressText = QStringLiteral("Preparing C++ synchronization");
  m_busy = true;
  emit busyChanged();
  emit synchronizationStateChanged();
  emit progressChanged();
  emit previewChanged();

  // Copy only semantic elements and relationships, not diagrams or other
  // project state. Planning does not need the potentially large presentation
  // model, and Apply re-plans against current data to avoid stale overwrites.
  const QList<ModelElement> elements = m_project->data().elements;
  const QList<Relationship> relationships = m_project->data().relationships;
  CppImportOptions options;
  options.interfacePattern = m_settings->cppInterfacePattern();
  options.memberTypeRules = m_settings->cppMemberTypeRuleValues();
  configureCppImportStereotypes(options, m_project->data());
  const quint64 generation = ++m_previewGeneration;
  const QPointer<CppImportController> controller(this);
  const CppImportProgressCallback progress =
      [controller, generation](const CppImportProgress &update) {
        if (!controller)
          return;
        QMetaObject::invokeMethod(
            controller,
            [controller, generation, update] {
              if (!controller || !controller->m_busy ||
                  controller->m_previewGeneration != generation)
                return;
              controller->updateProgress(update);
            },
            Qt::QueuedConnection);
      };
  m_watcher.setFuture(
      QtConcurrent::run([paths, elements, relationships, options, progress] {
        return CppImportService::preview(paths, elements, relationships,
                                         options, progress);
      }));
}

void CppImportController::synchronize() {
  if (!canSynchronize())
    return;
  previewPaths(configuredSourceRoots());
}

void CppImportController::applyPreview() {
  if (!canApply() || !m_project)
    return;

  // The project-owned catalog may have changed while Clang discovery was
  // running. Refresh its derived-stereotype policy before the final re-plan.
  CppImportPreview currentDiscovery = m_preview;
  configureCppImportStereotypes(currentDiscovery.optionsUsed,
                                m_project->data());
  const CppImportPreview currentPlan =
      CppImportService::replan(currentDiscovery, m_project->data().elements,
                               m_project->data().relationships);
  const qsizetype discoveryCount = currentPlan.discoveryDiagnostics.size();
  if (currentPlan.diagnostics.size() > discoveryCount) {
    publishDiagnostics(currentPlan.diagnostics.mid(discoveryCount));
  }
  if (currentPlan.conflictCount() > 0)
    emit attentionRequired();

  const bool sourceConfigured =
      !currentPlan.sourceRoots.isEmpty() &&
      currentPlan.sourceRoots != configuredSourceRoots();
  const int applied = m_project->applyCppImportPlan(currentPlan);
  if (applied > 0) {
    m_project->diagnostics()->addInfo(
        QStringLiteral("cpp-import"),
        QStringLiteral("Imported %1 C++ model change(s)").arg(applied));
  } else if (sourceConfigured) {
    m_project->diagnostics()->addInfo(
        QStringLiteral("cpp-import"),
        QStringLiteral("Configured C++ synchronization from %1 source "
                       "director%2")
            .arg(currentPlan.sourceRoots.size())
            .arg(currentPlan.sourceRoots.size() == 1 ? QStringLiteral("y")
                                                     : QStringLiteral("ies")));
  }
  m_preview = CppImportService::replan(currentPlan, m_project->data().elements,
                                       m_project->data().relationships);
  rebuildViewState();
  emit previewChanged();
  emit importApplied(applied);
}

void CppImportController::clearPreview() {
  if (m_busy)
    return;
  m_preview = {};
  m_previewItems.clear();
  m_summary.clear();
  m_requestedSourcePaths.clear();
  resetProgress();
  emit progressChanged();
  emit previewChanged();
}

void CppImportController::finishPreview() {
  m_preview = m_watcher.result();
  m_busy = false;
  publishDiagnostics(m_preview.diagnostics);
  rebuildViewState();
  emit busyChanged();
  emit synchronizationStateChanged();
  emit previewChanged();

  const bool hasError =
      std::any_of(m_preview.diagnostics.cbegin(), m_preview.diagnostics.cend(),
                  [](const Diagnostic &diagnostic) {
                    return diagnostic.severity == DiagnosticSeverity::Error;
                  });
  if (hasError || m_preview.conflictCount() > 0)
    emit attentionRequired();
}

void CppImportController::publishDiagnostics(
    const QList<Diagnostic> &diagnostics) {
  if (!m_project)
    return;
  for (const auto &diagnostic : diagnostics)
    m_project->diagnostics()->add(diagnostic);
}

void CppImportController::updateProgress(const CppImportProgress &progress) {
  if (m_progressText == progress.message &&
      m_progressDetail == progress.detail &&
      m_progressValue == progress.completed &&
      m_progressMaximum == progress.total)
    return;
  m_progressText = progress.message;
  m_progressDetail = progress.detail;
  m_progressValue = progress.completed;
  m_progressMaximum = progress.total;
  emit progressChanged();
}

void CppImportController::resetProgress() {
  m_progressText.clear();
  m_progressDetail.clear();
  m_progressValue = 0;
  m_progressMaximum = 0;
}

void CppImportController::rebuildViewState() {
  m_previewItems.clear();
  m_previewItems.reserve(m_preview.items.size() +
                         m_preview.relationshipItems.size());
  QHash<CppImportAction, int> counts;
  for (const auto &item : m_preview.items) {
    ++counts[item.action];
    QVariantMap value;
    value.insert(QStringLiteral("action"), toString(item.action));
    value.insert(QStringLiteral("name"), item.symbol.qualifiedName);
    value.insert(QStringLiteral("type"), toString(item.symbol.elementType));
    value.insert(QStringLiteral("file"), item.symbol.filePath);
    value.insert(QStringLiteral("line"), item.symbol.line);
    value.insert(QStringLiteral("message"), item.message);
    m_previewItems.append(value);
  }
  for (const auto &item : m_preview.relationshipItems) {
    ++counts[item.action];
    QVariantMap value;
    value.insert(QStringLiteral("action"), toString(item.action));
    value.insert(QStringLiteral("name"),
                 QStringLiteral("%1 → %2").arg(item.source.sourceName,
                                               item.source.targetName));
    value.insert(QStringLiteral("type"),
                 toString(item.source.relationshipType));
    value.insert(QStringLiteral("classification"),
                 item.source.classificationReason);
    value.insert(QStringLiteral("file"), item.source.filePath);
    value.insert(QStringLiteral("line"), item.source.line);
    value.insert(QStringLiteral("message"), item.message);
    m_previewItems.append(value);
  }

  if (!m_preview.ok) {
    m_summary = QStringLiteral("C++ preview could not be completed");
    return;
  }
  const QString discoveryMode = m_preview.usedCompilationDatabase
                                    ? QStringLiteral("Compilation database")
                                    : QStringLiteral("Best-effort source scan");
  m_summary = QStringLiteral("%1 — %2 type(s), %3 relationship(s): %4 "
                             "new, %5 updated, %6 conflicts, %7 unchanged or "
                             "user-owned")
                  .arg(discoveryMode)
                  .arg(m_preview.symbols.size())
                  .arg(m_preview.relationships.size())
                  .arg(counts.value(CppImportAction::Create))
                  .arg(counts.value(CppImportAction::Update))
                  .arg(counts.value(CppImportAction::Conflict))
                  .arg(counts.value(CppImportAction::Unchanged) +
                       counts.value(CppImportAction::UserModified) +
                       counts.value(CppImportAction::MissingSource));
}

} // namespace uuml
