#include "core/cpp_import_controller.h"

#include "core/application_settings.h"
#include "core/model_operation.h"
#include "core/project_controller.h"

#include <QFileInfo>
#include <QPointer>
#include <QSet>
#include <QtConcurrentRun>
#include <algorithm>

namespace yauml {
namespace {

QString summarizedValues(const QStringList &values) {
  constexpr qsizetype maximumVisibleValues = 3;
  QStringList visible = values.mid(0, maximumVisibleValues);
  QString summary = visible.join(QStringLiteral(", "));
  if (values.size() > maximumVisibleValues) {
    summary +=
        QStringLiteral(" (+%1 more)").arg(values.size() - maximumVisibleValues);
  }
  return summary;
}

QString memberDifference(const QString &label, const QStringList &modelValues,
                         const QStringList &sourceValues) {
  QStringList sourceOnly;
  QStringList modelOnly;
  for (const QString &value : sourceValues)
    if (!modelValues.contains(value))
      sourceOnly.append(value);
  for (const QString &value : modelValues)
    if (!sourceValues.contains(value))
      modelOnly.append(value);
  if (sourceOnly.isEmpty() && modelOnly.isEmpty())
    return {};

  QStringList parts;
  if (!sourceOnly.isEmpty()) {
    parts.append(
        QStringLiteral("source adds %1").arg(summarizedValues(sourceOnly)));
  }
  if (!modelOnly.isEmpty()) {
    parts.append(
        QStringLiteral("model-only %1").arg(summarizedValues(modelOnly)));
  }
  return QStringLiteral("%1: %2").arg(label, parts.join(QStringLiteral("; ")));
}

QString elementConflictDetail(const CppImportItem &item) {
  if (!item.isResolvableConflict())
    return {};
  const ModelElement &model = *item.existingElement;
  const ModelElement &source = item.desiredElement;
  QStringList differences;
  if (model.name != source.name) {
    differences.append(
        QStringLiteral("Name: %1 → %2").arg(model.name, source.name));
  }
  if (model.type != source.type) {
    differences.append(QStringLiteral("Kind: %1 → %2")
                           .arg(toString(model.type), toString(source.type)));
  }
  const QString attributes = memberDifference(
      QStringLiteral("Attributes"), model.attributes, source.attributes);
  if (!attributes.isEmpty())
    differences.append(attributes);
  const QString operations = memberDifference(
      QStringLiteral("Operations"), modelOperationSignatures(model.operations),
      modelOperationSignatures(source.operations));
  if (!operations.isEmpty())
    differences.append(operations);
  if (model.packageId != source.packageId ||
      model.enclosingTypeId != source.enclosingTypeId) {
    differences.append(QStringLiteral("Namespace or enclosing type changed"));
  }
  return differences.join(u'\n');
}

QString relationshipConflictDetail(const CppRelationshipImportItem &item) {
  if (!item.isResolvableConflict())
    return {};
  const Relationship &model = *item.existingRelationship;
  const Relationship &source = item.desiredRelationship;
  QStringList differences;
  if (model.type != source.type) {
    differences.append(QStringLiteral("Kind: %1 → %2")
                           .arg(toString(model.type), toString(source.type)));
  }
  if (model.name != source.name) {
    differences.append(
        QStringLiteral("Name: “%1” → “%2”").arg(model.name, source.name));
  }
  if (model.sourceEnd.role != source.sourceEnd.role) {
    differences.append(QStringLiteral("Source role: “%1” → “%2”")
                           .arg(model.sourceEnd.role, source.sourceEnd.role));
  }
  if (model.sourceEnd.multiplicity != source.sourceEnd.multiplicity) {
    differences.append(
        QStringLiteral("Source cardinality: “%1” → “%2”")
            .arg(model.sourceEnd.multiplicity, source.sourceEnd.multiplicity));
  }
  return differences.join(u'\n');
}

} // namespace

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
         m_preview.unresolvedOutOfScopeCount() == 0 &&
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

int CppImportController::conflictCount() const {
  return m_preview.conflictCount();
}

int CppImportController::resolvableConflictCount() const {
  return m_preview.resolvableConflictCount();
}

int CppImportController::unresolvedConflictCount() const {
  return m_preview.unresolvedConflictCount();
}

int CppImportController::missingSourceCount() const {
  return m_preview.missingSourceCount();
}

int CppImportController::selectedMissingSourceCount() const {
  return m_preview.selectedMissingSourceCount();
}

int CppImportController::outOfScopeCount() const {
  return m_preview.outOfScopeCount();
}

int CppImportController::unresolvedOutOfScopeCount() const {
  return m_preview.unresolvedOutOfScopeCount();
}

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
  m_conflictResolutions.clear();
  m_missingSourceResolutions.clear();
  m_outOfScopeResolutions.clear();
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
  const QStringList previousSourceRoots = configuredSourceRoots();
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
  m_watcher.setFuture(QtConcurrent::run(
      [paths, elements, relationships, previousSourceRoots, options, progress] {
        CppImportPreview preview = CppImportService::preview(
            paths, elements, relationships, options, progress);
        preview.previousSourceRoots = previousSourceRoots;
        return CppImportService::replan(preview, elements, relationships);
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
  CppImportPreview resolvedPlan = currentPlan;
  for (auto iterator = m_conflictResolutions.cbegin();
       iterator != m_conflictResolutions.cend(); ++iterator) {
    resolvedPlan.setConflictResolution(iterator.key(), iterator.value());
  }
  for (auto iterator = m_missingSourceResolutions.cbegin();
       iterator != m_missingSourceResolutions.cend(); ++iterator) {
    resolvedPlan.setMissingSourceResolution(iterator.key(), iterator.value());
  }
  for (auto iterator = m_outOfScopeResolutions.cbegin();
       iterator != m_outOfScopeResolutions.cend(); ++iterator) {
    resolvedPlan.setOutOfScopeResolution(iterator.key(), iterator.value());
  }
  const qsizetype discoveryCount = resolvedPlan.discoveryDiagnostics.size();
  if (resolvedPlan.diagnostics.size() > discoveryCount) {
    QSet<QString> resolvedSubjectIds;
    for (const auto &item : resolvedPlan.items) {
      if (item.resolution != CppImportConflictResolution::Unresolved)
        resolvedSubjectIds.insert(item.existingElementId);
    }
    for (const auto &item : resolvedPlan.relationshipItems) {
      if (item.resolution != CppImportConflictResolution::Unresolved)
        resolvedSubjectIds.insert(item.existingRelationshipId);
    }
    QList<Diagnostic> diagnostics;
    for (const auto &diagnostic :
         resolvedPlan.diagnostics.mid(discoveryCount)) {
      const bool supersededConflict =
          resolvedSubjectIds.contains(diagnostic.elementId) &&
          diagnostic.message.contains(QStringLiteral("conflict"),
                                      Qt::CaseInsensitive);
      if (!supersededConflict)
        diagnostics.append(diagnostic);
    }
    publishDiagnostics(diagnostics);
  }
  if (resolvedPlan.unresolvedConflictCount() > 0)
    emit attentionRequired();

  const bool sourceConfigured =
      !resolvedPlan.sourceRoots.isEmpty() &&
      resolvedPlan.sourceRoots != configuredSourceRoots();
  const int resolvedConflicts = resolvedPlan.resolvedConflictCount();
  const int applied = m_project->applyCppImportPlan(resolvedPlan);
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
  if (resolvedConflicts > 0) {
    m_project->diagnostics()->addInfo(
        QStringLiteral("cpp-import"),
        QStringLiteral("Resolved %1 C++ synchronization conflict(s)")
            .arg(resolvedConflicts));
  }
  m_conflictResolutions.clear();
  m_missingSourceResolutions.clear();
  m_outOfScopeResolutions.clear();
  resolvedPlan.previousSourceRoots = configuredSourceRoots();
  m_preview = CppImportService::replan(resolvedPlan, m_project->data().elements,
                                       m_project->data().relationships);
  rebuildViewState();
  emit previewChanged();
  emit importApplied(applied);
}

void CppImportController::setConflictResolution(
    const QString &conflictKey, const QString &resolutionValue) {
  bool ok = false;
  const CppImportConflictResolution resolution =
      cppImportConflictResolutionFromString(resolutionValue, &ok);
  if (!ok || !m_preview.setConflictResolution(conflictKey, resolution))
    return;
  if (resolution == CppImportConflictResolution::Unresolved)
    m_conflictResolutions.remove(conflictKey);
  else
    m_conflictResolutions.insert(conflictKey, resolution);
  rebuildViewState();
  emit previewChanged();
}

void CppImportController::resolveAllConflicts(const QString &resolutionValue) {
  bool ok = false;
  const CppImportConflictResolution resolution =
      cppImportConflictResolutionFromString(resolutionValue, &ok);
  if (!ok)
    return;
  m_preview.resolveAllConflicts(resolution);
  m_conflictResolutions.clear();
  const auto remember = [&](const auto &items) {
    for (const auto &item : items) {
      if (item.isResolvableConflict() &&
          item.resolution != CppImportConflictResolution::Unresolved) {
        m_conflictResolutions.insert(item.conflictKey(), item.resolution);
      }
    }
  };
  remember(m_preview.items);
  remember(m_preview.relationshipItems);
  rebuildViewState();
  emit previewChanged();
}

void CppImportController::setMissingSourceResolution(
    const QString &missingSourceKey, const QString &resolutionValue) {
  bool ok = false;
  const CppImportMissingSourceResolution resolution =
      cppImportMissingSourceResolutionFromString(resolutionValue, &ok);
  if (!ok ||
      !m_preview.setMissingSourceResolution(missingSourceKey, resolution))
    return;
  if (resolution == CppImportMissingSourceResolution::Keep)
    m_missingSourceResolutions.remove(missingSourceKey);
  else
    m_missingSourceResolutions.insert(missingSourceKey, resolution);
  rebuildViewState();
  emit previewChanged();
}

void CppImportController::resolveAllMissingSources(
    const QString &resolutionValue) {
  bool ok = false;
  const CppImportMissingSourceResolution resolution =
      cppImportMissingSourceResolutionFromString(resolutionValue, &ok);
  if (!ok)
    return;
  m_preview.resolveAllMissingSources(resolution);
  m_missingSourceResolutions.clear();
  if (resolution != CppImportMissingSourceResolution::Keep) {
    const auto remember = [&](const auto &items) {
      for (const auto &item : items) {
        if (item.isMissingSource()) {
          m_missingSourceResolutions.insert(item.missingSourceKey(),
                                            item.missingSourceResolution);
        }
      }
    };
    remember(m_preview.items);
    remember(m_preview.relationshipItems);
  }
  rebuildViewState();
  emit previewChanged();
}

void CppImportController::setOutOfScopeResolution(
    const QString &outOfScopeKey, const QString &resolutionValue) {
  bool ok = false;
  const CppImportOutOfScopeResolution resolution =
      cppImportOutOfScopeResolutionFromString(resolutionValue, &ok);
  if (!ok || !m_preview.setOutOfScopeResolution(outOfScopeKey, resolution))
    return;
  if (resolution == CppImportOutOfScopeResolution::Unresolved)
    m_outOfScopeResolutions.remove(outOfScopeKey);
  else
    m_outOfScopeResolutions.insert(outOfScopeKey, resolution);
  rebuildViewState();
  emit previewChanged();
}

void CppImportController::resolveAllOutOfScope(const QString &resolutionValue) {
  bool ok = false;
  const CppImportOutOfScopeResolution resolution =
      cppImportOutOfScopeResolutionFromString(resolutionValue, &ok);
  if (!ok)
    return;
  m_preview.resolveAllOutOfScope(resolution);
  m_outOfScopeResolutions.clear();
  const auto remember = [&](const auto &items) {
    for (const auto &item : items) {
      if (item.isOutOfScopeResolved()) {
        m_outOfScopeResolutions.insert(item.outOfScopeKey(),
                                       item.outOfScopeResolution);
      }
    }
  };
  remember(m_preview.items);
  remember(m_preview.relationshipItems);
  rebuildViewState();
  emit previewChanged();
}

void CppImportController::clearPreview() {
  if (m_busy)
    return;
  m_preview = {};
  m_previewItems.clear();
  m_summary.clear();
  m_requestedSourcePaths.clear();
  m_conflictResolutions.clear();
  m_missingSourceResolutions.clear();
  m_outOfScopeResolutions.clear();
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
  if (hasError || m_preview.conflictCount() > 0 ||
      m_preview.outOfScopeCount() > 0)
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
  QHash<QString, int> relationshipCountByElement;
  QHash<QString, int> presentationCountByElement;
  QHash<QString, int> connectorCountByRelationship;
  if (m_project &&
      (m_preview.outOfScopeCount() > 0 || m_preview.missingSourceCount() > 0)) {
    for (const auto &relationship : m_project->data().relationships) {
      ++relationshipCountByElement[relationship.sourceId];
      if (relationship.targetId != relationship.sourceId)
        ++relationshipCountByElement[relationship.targetId];
    }
    for (const auto &diagram : m_project->data().diagrams) {
      for (const auto &node : diagram.nodes)
        ++presentationCountByElement[node.elementId];
      for (const auto &container : diagram.containers) {
        if (container.subjectKind == QStringLiteral("package"))
          ++presentationCountByElement[container.subjectId];
      }
      for (const auto &connector : diagram.connectors)
        ++connectorCountByRelationship[connector.relationshipId];
    }
  }
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
    value.insert(QStringLiteral("conflictKey"), item.conflictKey());
    value.insert(QStringLiteral("resolvable"), item.isResolvableConflict());
    value.insert(QStringLiteral("resolution"), toString(item.resolution));
    value.insert(QStringLiteral("missingSourceKey"), item.missingSourceKey());
    value.insert(QStringLiteral("missingSourceResolution"),
                 toString(item.missingSourceResolution));
    value.insert(QStringLiteral("outOfScopeKey"), item.outOfScopeKey());
    value.insert(QStringLiteral("outOfScopeResolution"),
                 toString(item.outOfScopeResolution));
    if (item.isOutOfScope() || item.isMissingSource()) {
      value.insert(
          QStringLiteral("impact"),
          QStringLiteral("Removing this item also removes %1 relationship(s) "
                         "and %2 diagram presentation(s)")
              .arg(relationshipCountByElement.value(item.existingElementId))
              .arg(presentationCountByElement.value(item.existingElementId)));
    }
    value.insert(QStringLiteral("resolutionDetail"),
                 elementConflictDetail(item));
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
    value.insert(QStringLiteral("conflictKey"), item.conflictKey());
    value.insert(QStringLiteral("resolvable"), item.isResolvableConflict());
    value.insert(QStringLiteral("resolution"), toString(item.resolution));
    value.insert(QStringLiteral("missingSourceKey"), item.missingSourceKey());
    value.insert(QStringLiteral("missingSourceResolution"),
                 toString(item.missingSourceResolution));
    value.insert(QStringLiteral("outOfScopeKey"), item.outOfScopeKey());
    value.insert(QStringLiteral("outOfScopeResolution"),
                 toString(item.outOfScopeResolution));
    if (item.isOutOfScope() || item.isMissingSource()) {
      value.insert(QStringLiteral("impact"),
                   QStringLiteral("Removing this relationship also removes %1 "
                                  "connector presentation(s)")
                       .arg(connectorCountByRelationship.value(
                           item.existingRelationshipId)));
    }
    value.insert(QStringLiteral("resolutionDetail"),
                 relationshipConflictDetail(item));
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
                             "user-owned, %8 not found in scan, %9 outside "
                             "selected folders")
                  .arg(discoveryMode)
                  .arg(m_preview.symbols.size())
                  .arg(m_preview.relationships.size())
                  .arg(counts.value(CppImportAction::Create))
                  .arg(counts.value(CppImportAction::Update))
                  .arg(counts.value(CppImportAction::Conflict))
                  .arg(counts.value(CppImportAction::Unchanged) +
                       counts.value(CppImportAction::UserModified))
                  .arg(counts.value(CppImportAction::MissingSource))
                  .arg(counts.value(CppImportAction::OutOfScope));
  if (m_preview.conflictCount() > 0) {
    m_summary +=
        QStringLiteral(" — %1 of %2 conflict(s) selected for resolution")
            .arg(m_preview.resolvedConflictCount())
            .arg(m_preview.conflictCount());
  }
  if (m_preview.outOfScopeCount() > 0) {
    m_summary +=
        QStringLiteral(" — %1 of %2 out-of-scope item(s) selected for cleanup")
            .arg(m_preview.resolvedOutOfScopeCount())
            .arg(m_preview.outOfScopeCount());
  }
  if (m_preview.selectedMissingSourceCount() > 0) {
    m_summary +=
        QStringLiteral(" — %1 of %2 not-found item(s) selected for cleanup")
            .arg(m_preview.selectedMissingSourceCount())
            .arg(m_preview.missingSourceCount());
  }
}

} // namespace yauml
