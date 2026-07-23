#include "core/cpp_import_controller.h"

#include "core/application_settings.h"
#include "core/project_controller.h"

#include <QFileInfo>
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
          (!m_preview.sourceRoot.isEmpty() &&
           m_preview.sourceRoot != configuredSourceRoot()));
}

bool CppImportController::canSynchronize() const {
  return !m_busy && !configuredSourceRoot().isEmpty();
}

QString CppImportController::configuredSourceRoot() const {
  return m_project ? m_project->data().cppImport.sourceRoot : QString{};
}

QString CppImportController::previewSourceRoot() const {
  return !m_preview.sourceRoot.isEmpty() ? m_preview.sourceRoot
                                         : m_requestedSourcePath;
}

QString CppImportController::summary() const { return m_summary; }

QString CppImportController::compilationDatabasePath() const {
  return m_preview.compilationDatabasePath;
}

QVariantList CppImportController::previewItems() const {
  return m_previewItems;
}

void CppImportController::preview(const QUrl &sourceOrBuildDirectory) {
  if (m_busy || !m_project)
    return;
  const QString path = sourceOrBuildDirectory.isLocalFile()
                           ? sourceOrBuildDirectory.toLocalFile()
                           : sourceOrBuildDirectory.toString();
  if (path.trimmed().isEmpty())
    return;

  m_requestedSourcePath = QFileInfo(path).absoluteFilePath();
  m_preview = {};
  m_previewItems.clear();
  m_summary = QStringLiteral("Discovering C++ declarations…");
  m_busy = true;
  emit busyChanged();
  emit synchronizationStateChanged();
  emit previewChanged();

  // Copy only semantic elements and relationships, not diagrams or other
  // project state. Planning does not need the potentially large presentation
  // model, and Apply re-plans against current data to avoid stale overwrites.
  const QList<ModelElement> elements = m_project->data().elements;
  const QList<Relationship> relationships = m_project->data().relationships;
  CppImportOptions options;
  options.interfacePattern = m_settings->cppInterfacePattern();
  options.owningPointerTypes = m_settings->cppOwningPointerTypes();
  options.sharedPointerTypes = m_settings->cppSharedPointerTypes();
  m_watcher.setFuture(QtConcurrent::run([path, elements, relationships,
                                         options] {
    return CppImportService::preview(path, elements, relationships, options);
  }));
}

void CppImportController::synchronize() {
  if (!canSynchronize())
    return;
  preview(QUrl::fromLocalFile(configuredSourceRoot()));
}

void CppImportController::applyPreview() {
  if (!canApply() || !m_project)
    return;

  const CppImportPreview currentPlan = CppImportService::replan(
      m_preview, m_project->data().elements, m_project->data().relationships);
  const qsizetype discoveryCount = currentPlan.discoveryDiagnostics.size();
  if (currentPlan.diagnostics.size() > discoveryCount) {
    publishDiagnostics(currentPlan.diagnostics.mid(discoveryCount));
  }
  if (currentPlan.conflictCount() > 0)
    emit attentionRequired();

  const bool sourceConfigured =
      !currentPlan.sourceRoot.isEmpty() &&
      currentPlan.sourceRoot != configuredSourceRoot();
  const int applied = m_project->applyCppImportPlan(currentPlan);
  if (applied > 0) {
    m_project->diagnostics()->addInfo(
        QStringLiteral("cpp-import"),
        QStringLiteral("Imported %1 C++ model change(s)").arg(applied));
  } else if (sourceConfigured) {
    m_project->diagnostics()->addInfo(
        QStringLiteral("cpp-import"),
        QStringLiteral("Configured C++ synchronization from %1")
            .arg(currentPlan.sourceRoot));
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
  m_requestedSourcePath.clear();
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
