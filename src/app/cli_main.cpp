#include "core/application_settings.h"
#include "core/cpp_import.h"
#include "core/project_serializer.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

QString previousProductIdentity() {
  // Keep the previous identifier out of new user-facing strings while still
  // allowing a one-time QSettings migration after the product rename.
  return QStringLiteral("u") + QStringLiteral("uml");
}

void migratePreviousProductSettings() {
  const QString legacyIdentity = previousProductIdentity();
  yauml::ApplicationSettings::migrateLegacyScope(legacyIdentity,
                                                 legacyIdentity);
}

void writeDiagnostics(const QList<yauml::Diagnostic> &diagnostics,
                      QTextStream &out, QTextStream &err) {
  for (const auto &diagnostic : diagnostics) {
    QTextStream &stream =
        diagnostic.severity == yauml::DiagnosticSeverity::Error ? err : out;
    stream << yauml::toString(diagnostic.severity).toUpper() << " ["
           << diagnostic.category << "] " << diagnostic.message;
    if (!diagnostic.elementId.isEmpty())
      stream << " (" << diagnostic.elementId << ")";
    stream << '\n';
  }
}

int runValidation(const QStringList &arguments, QTextStream &out,
                  QTextStream &err) {
  if (arguments.size() < 3) {
    err << "Usage: yauml-cli validate <project-directory>\n";
    return 64;
  }

  const auto outcome = yauml::ProjectSerializer::load(arguments.at(2));
  writeDiagnostics(outcome.diagnostics, out, err);
  if (outcome.ok) {
    out << "Valid yauml project: " << outcome.project.name << " ("
        << outcome.project.elements.size() << " elements, "
        << outcome.project.diagrams.size() << " diagrams)\n";
    return 0;
  }
  return 2;
}

int runCppImportCommand(const QStringList &arguments, bool apply,
                        QTextStream &out, QTextStream &err) {
  // Import classification is intentionally shared with the GUI preferences.
  // Migrate the previous product scope before reading those settings.
  migratePreviousProductSettings();

  if (arguments.size() < 3) {
    err << "Usage: yauml-cli " << (apply ? "cpp-import" : "cpp-preview")
        << " <project-directory> "
           "[--conflicts=unresolved|keep-model|use-source] "
           "[--missing-source=keep|remove|keep-manual] "
           "[--out-of-scope=unresolved|remove|keep-manual] "
        << (apply ? "[--overwrite-external-changes] " : "")
        << "[source-directory ...]\n";
    return 64;
  }

  const QString projectPath = arguments.at(2);
  const auto load = yauml::ProjectSerializer::load(projectPath);
  writeDiagnostics(load.diagnostics, out, err);
  if (!load.ok)
    return 2;

  yauml::CppImportConflictResolution conflictResolution =
      yauml::CppImportConflictResolution::Unresolved;
  yauml::CppImportMissingSourceResolution missingSourceResolution =
      yauml::CppImportMissingSourceResolution::Keep;
  yauml::CppImportOutOfScopeResolution outOfScopeResolution =
      yauml::CppImportOutOfScopeResolution::Unresolved;
  bool overwriteExternalChanges = false;
  QStringList requestedSourcePaths;
  const QString conflictOption = QStringLiteral("--conflicts=");
  const QString missingSourceOption = QStringLiteral("--missing-source=");
  const QString outOfScopeOption = QStringLiteral("--out-of-scope=");
  for (qsizetype index = 3; index < arguments.size(); ++index) {
    const QString argument = arguments.at(index);
    if (argument.startsWith(conflictOption)) {
      bool ok = false;
      conflictResolution = yauml::cppImportConflictResolutionFromString(
          argument.mid(conflictOption.size()), &ok);
      if (!ok) {
        err << "Unknown C++ conflict resolution: " << argument << '\n';
        return 64;
      }
    } else if (argument.startsWith(missingSourceOption)) {
      bool ok = false;
      missingSourceResolution =
          yauml::cppImportMissingSourceResolutionFromString(
              argument.mid(missingSourceOption.size()), &ok);
      if (!ok) {
        err << "Unknown missing-source resolution: " << argument << '\n';
        return 64;
      }
    } else if (argument.startsWith(outOfScopeOption)) {
      bool ok = false;
      outOfScopeResolution = yauml::cppImportOutOfScopeResolutionFromString(
          argument.mid(outOfScopeOption.size()), &ok);
      if (!ok) {
        err << "Unknown out-of-scope resolution: " << argument << '\n';
        return 64;
      }
    } else if (apply &&
               argument == QStringLiteral("--overwrite-external-changes")) {
      overwriteExternalChanges = true;
    } else if (argument.startsWith(QStringLiteral("--"))) {
      err << "Unknown option: " << argument << '\n';
      return 64;
    } else {
      requestedSourcePaths.append(argument);
    }
  }
  const QStringList sourcePaths = requestedSourcePaths.isEmpty()
                                      ? load.project.cppImport.sourceRoots
                                      : requestedSourcePaths;
  if (sourcePaths.isEmpty()) {
    err << "No C++ source directories were provided or configured for this "
           "project\n";
    return 64;
  }

  yauml::ApplicationSettings settings;
  yauml::CppImportOptions options;
  options.interfacePattern = settings.cppInterfacePattern();
  options.memberTypeRules = settings.cppMemberTypeRuleValues();
  yauml::configureCppImportStereotypes(options, load.project);
  yauml::CppImportPreview preview = yauml::CppImportService::preview(
      sourcePaths, load.project.elements, load.project.relationships, options);
  preview.previousSourceRoots = load.project.cppImport.sourceRoots;
  preview = yauml::CppImportService::replan(preview, load.project.elements,
                                            load.project.relationships);
  preview.resolveAllConflicts(conflictResolution);
  preview.resolveAllMissingSources(missingSourceResolution);
  preview.resolveAllOutOfScope(outOfScopeResolution);
  writeDiagnostics(preview.diagnostics, out, err);
  for (const auto &item : preview.items) {
    out << yauml::toString(item.action).toUpper() << " "
        << item.symbol.qualifiedName;
    if (!item.symbol.filePath.isEmpty())
      out << " — " << item.symbol.filePath << ':' << item.symbol.line;
    if (!item.message.isEmpty())
      out << " — " << item.message;
    if (item.action == yauml::CppImportAction::Conflict)
      out << " — "
          << (item.isResolvableConflict()
                  ? QStringLiteral("resolution: %1")
                        .arg(yauml::toString(item.resolution))
                  : QStringLiteral("manual repair required"));
    if (item.action == yauml::CppImportAction::OutOfScope)
      out << " — decision: " << yauml::toString(item.outOfScopeResolution);
    if (item.action == yauml::CppImportAction::MissingSource)
      out << " — decision: " << yauml::toString(item.missingSourceResolution);
    out << '\n';
  }
  for (const auto &item : preview.relationshipItems) {
    out << yauml::toString(item.action).toUpper() << " "
        << item.source.sourceName << " -> " << item.source.targetName << " ("
        << yauml::toString(item.source.relationshipType) << ')';
    if (!item.source.filePath.isEmpty())
      out << " — " << item.source.filePath << ':' << item.source.line;
    if (!item.message.isEmpty())
      out << " — " << item.message;
    if (!item.source.classificationReason.isEmpty())
      out << " — " << item.source.classificationReason;
    if (item.action == yauml::CppImportAction::Conflict)
      out << " — "
          << (item.isResolvableConflict()
                  ? QStringLiteral("resolution: %1")
                        .arg(yauml::toString(item.resolution))
                  : QStringLiteral("manual repair required"));
    if (item.action == yauml::CppImportAction::OutOfScope)
      out << " — decision: " << yauml::toString(item.outOfScopeResolution);
    if (item.action == yauml::CppImportAction::MissingSource)
      out << " — decision: " << yauml::toString(item.missingSourceResolution);
    out << '\n';
  }
  if (!preview.ok)
    return 2;

  if (!apply) {
    out << "C++ preview ("
        << (preview.usedCompilationDatabase ? "compilation database"
                                            : "best-effort source scan")
        << "): " << preview.symbols.size() << " type(s), "
        << preview.relationships.size() << " relationship(s), "
        << preview.applicableCount() << " applicable change(s), "
        << preview.conflictCount() << " conflict(s), "
        << preview.unresolvedConflictCount() << " unresolved conflict(s), "
        << preview.missingSourceCount() << " not found in scan, "
        << preview.selectedMissingSourceCount() << " selected for cleanup, "
        << preview.outOfScopeCount() << " out-of-scope item(s), "
        << preview.unresolvedOutOfScopeCount() << " unresolved\n";
    return preview.unresolvedConflictCount() > 0 ||
                   preview.unresolvedOutOfScopeCount() > 0
               ? 3
               : 0;
  }

  yauml::ProjectData imported = load.project;
  const QStringList previousSourceRoots = imported.cppImport.sourceRoots;
  const int appliedCount = yauml::CppImportService::apply(imported, preview);
  if (appliedCount > 0 ||
      imported.cppImport.sourceRoots != previousSourceRoots) {
    const auto save = yauml::ProjectSerializer::save(
        projectPath, imported, load.revision, overwriteExternalChanges);
    writeDiagnostics(save.diagnostics, out, err);
    if (!save.ok)
      return save.externalChangesDetected ? 4 : 2;
  }
  out << "Imported " << appliedCount << " C++ model change(s)";
  if (preview.resolvedConflictCount() > 0)
    out << "; " << preview.resolvedConflictCount() << " conflict(s) resolved";
  if (preview.unresolvedConflictCount() > 0)
    out << "; " << preview.unresolvedConflictCount()
        << " conflict(s) remain unresolved";
  if (preview.unresolvedOutOfScopeCount() > 0)
    out << "; " << preview.unresolvedOutOfScopeCount()
        << " out-of-scope item(s) remain unresolved";
  if (preview.selectedMissingSourceCount() > 0)
    out << "; " << preview.selectedMissingSourceCount()
        << " not-found item(s) resolved";
  out << '\n';
  return preview.unresolvedConflictCount() > 0 ||
                 preview.unresolvedOutOfScopeCount() > 0
             ? 3
             : 0;
}

void writeUsage(QTextStream &err) {
  err << "Usage: yauml-cli <command> [arguments]\n"
         "Commands:\n"
         "  validate     Validate a yauml project\n"
         "  cpp-preview  Preview C++ source synchronization\n"
         "  cpp-import   Apply C++ source synchronization\n";
}

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  // Keep the settings identity aligned with the GUI even though the binary has
  // a distinct name; headless import must use the same pointer/type rules.
  application.setApplicationName(QStringLiteral("yauml"));
  application.setApplicationVersion(QStringLiteral(YAUML_VERSION));
  application.setOrganizationName(QStringLiteral("yauml"));

  QTextStream out(stdout);
  QTextStream err(stderr);
  const QStringList arguments = application.arguments();
  if (arguments.size() < 2) {
    writeUsage(err);
    return 64;
  }

  const QString command = arguments.at(1);
  if (command == QStringLiteral("validate"))
    return runValidation(arguments, out, err);
  if (command == QStringLiteral("cpp-preview"))
    return runCppImportCommand(arguments, false, out, err);
  if (command == QStringLiteral("cpp-import"))
    return runCppImportCommand(arguments, true, out, err);

  err << "Unknown command: " << command << '\n';
  writeUsage(err);
  return 64;
}
