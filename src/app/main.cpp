#include "app/icon_registry.h"
#include "app/source_folder_picker.h"
#include "core/application_settings.h"
#include "core/cpp_import.h"
#include "core/cpp_import_controller.h"
#include "core/project_controller.h"
#include "core/project_serializer.h"
#include "core/workspace_controller.h"
#include "ui/diagram_canvas.h"
#include "ui/ui_theme.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>
#include <QWindow>

namespace {

void writeDiagnostics(const QList<uuml::Diagnostic> &diagnostics,
                      QTextStream &out, QTextStream &err) {
  for (const auto &diagnostic : diagnostics) {
    QTextStream &stream =
        diagnostic.severity == uuml::DiagnosticSeverity::Error ? err : out;
    stream << uuml::toString(diagnostic.severity).toUpper() << " ["
           << diagnostic.category << "] " << diagnostic.message;
    if (!diagnostic.elementId.isEmpty())
      stream << " (" << diagnostic.elementId << ")";
    stream << '\n';
  }
}

int runValidation(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  application.setApplicationName(QStringLiteral("uuml"));
  QTextStream out(stdout);
  QTextStream err(stderr);
  const QStringList arguments = application.arguments();
  if (arguments.size() < 3) {
    err << "Usage: uuml validate <project-directory>\n";
    return 64;
  }

  const auto outcome = uuml::ProjectSerializer::load(arguments.at(2));
  writeDiagnostics(outcome.diagnostics, out, err);
  if (outcome.ok) {
    out << "Valid uuml project: " << outcome.project.name << " ("
        << outcome.project.elements.size() << " elements, "
        << outcome.project.diagrams.size() << " diagrams)\n";
    return 0;
  }
  return 2;
}

int runCppImportCommand(int argc, char *argv[], bool apply) {
  QCoreApplication application(argc, argv);
  application.setApplicationName(QStringLiteral("uuml"));
  application.setOrganizationName(QStringLiteral("uuml"));
  QTextStream out(stdout);
  QTextStream err(stderr);
  const QStringList arguments = application.arguments();
  if (arguments.size() < 3) {
    err << "Usage: uuml " << (apply ? "cpp-import" : "cpp-preview")
        << " <project-directory> "
           "[--conflicts=unresolved|keep-model|use-source] "
        << (apply ? "[--overwrite-external-changes] " : "")
        << "[source-directory ...]\n";
    return 64;
  }

  const QString projectPath = arguments.at(2);
  const auto load = uuml::ProjectSerializer::load(projectPath);
  writeDiagnostics(load.diagnostics, out, err);
  if (!load.ok)
    return 2;

  uuml::CppImportConflictResolution conflictResolution =
      uuml::CppImportConflictResolution::Unresolved;
  bool overwriteExternalChanges = false;
  QStringList requestedSourcePaths;
  const QString conflictOption = QStringLiteral("--conflicts=");
  for (qsizetype index = 3; index < arguments.size(); ++index) {
    const QString argument = arguments.at(index);
    if (argument.startsWith(conflictOption)) {
      bool ok = false;
      conflictResolution = uuml::cppImportConflictResolutionFromString(
          argument.mid(conflictOption.size()), &ok);
      if (!ok) {
        err << "Unknown C++ conflict resolution: " << argument << '\n';
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

  uuml::ApplicationSettings settings;
  uuml::CppImportOptions options;
  options.interfacePattern = settings.cppInterfacePattern();
  options.memberTypeRules = settings.cppMemberTypeRuleValues();
  uuml::configureCppImportStereotypes(options, load.project);
  uuml::CppImportPreview preview = uuml::CppImportService::preview(
      sourcePaths, load.project.elements, load.project.relationships, options);
  preview.resolveAllConflicts(conflictResolution);
  writeDiagnostics(preview.diagnostics, out, err);
  for (const auto &item : preview.items) {
    out << uuml::toString(item.action).toUpper() << " "
        << item.symbol.qualifiedName;
    if (!item.symbol.filePath.isEmpty())
      out << " — " << item.symbol.filePath << ':' << item.symbol.line;
    if (!item.message.isEmpty())
      out << " — " << item.message;
    if (item.action == uuml::CppImportAction::Conflict)
      out << " — "
          << (item.isResolvableConflict()
                  ? QStringLiteral("resolution: %1")
                        .arg(uuml::toString(item.resolution))
                  : QStringLiteral("manual repair required"));
    out << '\n';
  }
  for (const auto &item : preview.relationshipItems) {
    out << uuml::toString(item.action).toUpper() << " "
        << item.source.sourceName << " -> " << item.source.targetName << " ("
        << uuml::toString(item.source.relationshipType) << ')';
    if (!item.source.filePath.isEmpty())
      out << " — " << item.source.filePath << ':' << item.source.line;
    if (!item.message.isEmpty())
      out << " — " << item.message;
    if (!item.source.classificationReason.isEmpty())
      out << " — " << item.source.classificationReason;
    if (item.action == uuml::CppImportAction::Conflict)
      out << " — "
          << (item.isResolvableConflict()
                  ? QStringLiteral("resolution: %1")
                        .arg(uuml::toString(item.resolution))
                  : QStringLiteral("manual repair required"));
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
        << preview.unresolvedConflictCount() << " unresolved\n";
    return preview.unresolvedConflictCount() > 0 ? 3 : 0;
  }

  uuml::ProjectData imported = load.project;
  const QStringList previousSourceRoots = imported.cppImport.sourceRoots;
  const int appliedCount = uuml::CppImportService::apply(imported, preview);
  if (appliedCount > 0 ||
      imported.cppImport.sourceRoots != previousSourceRoots) {
    const auto save = uuml::ProjectSerializer::save(
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
  out << '\n';
  return preview.unresolvedConflictCount() > 0 ? 3 : 0;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("validate"))
    return runValidation(argc, argv);
  if (argc > 1 &&
      QString::fromLocal8Bit(argv[1]) == QStringLiteral("cpp-preview"))
    return runCppImportCommand(argc, argv, false);
  if (argc > 1 &&
      QString::fromLocal8Bit(argv[1]) == QStringLiteral("cpp-import"))
    return runCppImportCommand(argc, argv, true);

  // Connector and shape geometry is rendered as batched triangles. Requesting
  // multisampling before the GUI application exists smooths their edges on all
  // scene-graph backends that support it, without creating per-item QML shapes.
  QSurfaceFormat surfaceFormat = QSurfaceFormat::defaultFormat();
  surfaceFormat.setSamples(4);
  QSurfaceFormat::setDefaultFormat(surfaceFormat);

  QQuickStyle::setStyle(QStringLiteral("Fusion"));
  QGuiApplication application(argc, argv);
  application.setApplicationName(QStringLiteral("uuml"));
  application.setApplicationDisplayName(QStringLiteral("u uml"));
  application.setOrganizationName(QStringLiteral("uuml"));

  qmlRegisterType<uuml::DiagramCanvas>("Uuml.Native", 1, 0, "DiagramCanvas");

  uuml::ProjectController project;
  uuml::ApplicationSettings applicationSettings;
  uuml::CppImportController cppImport(&project, &applicationSettings);
  uuml::WorkspaceController workspace(&project, true);
  uuml::ui::SourceFolderPicker sourceFolderPicker;
  uuml::ui::IconRegistry iconRegistry;
  uuml::ui::UiTheme uiTheme;
  for (const QString &error : iconRegistry.errors())
    project.diagnostics()->addWarning(QStringLiteral("icons"), error);
  QObject::connect(&project, &uuml::ProjectController::projectOpened,
                   &applicationSettings,
                   &uuml::ApplicationSettings::addRecentProject);
  QObject::connect(
      &sourceFolderPicker, &uuml::ui::SourceFolderPicker::errorOccurred,
      &project, [&project](const QString &message) {
        project.diagnostics()->addError(QStringLiteral("cpp-import"), message);
      });
  if (application.arguments().size() > 1) {
    const QString candidate = application.arguments().at(1);
    if (!candidate.startsWith(u'-'))
      project.openProject(
          QUrl::fromLocalFile(QFileInfo(candidate).absoluteFilePath()));
  }

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("projectController"),
                                           &project);
  engine.rootContext()->setContextProperty(
      QStringLiteral("workspaceController"), &workspace);
  engine.rootContext()->setContextProperty(
      QStringLiteral("applicationSettings"), &applicationSettings);
  engine.rootContext()->setContextProperty(
      QStringLiteral("cppImportController"), &cppImport);
  engine.rootContext()->setContextProperty(QStringLiteral("sourceFolderPicker"),
                                           &sourceFolderPicker);
  engine.rootContext()->setContextProperty(QStringLiteral("uiTheme"), &uiTheme);
  engine.rootContext()->setContextProperty(QStringLiteral("iconRegistry"),
                                           &iconRegistry);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("Uuml"), QStringLiteral("Main"));
  if (application.arguments().contains(QStringLiteral("--smoke-test"))) {
    if (!iconRegistry.isValid()) {
      qWarning().noquote() << QStringLiteral("Icon registry errors:\n%1")
                                  .arg(iconRegistry.errors().join(u'\n'));
      return 1;
    }
    const bool supportsDetachedWindows =
        QGuiApplication::platformName() != QStringLiteral("offscreen") &&
        QGuiApplication::platformName() != QStringLiteral("minimal");
    QTimer::singleShot(
        0, &application,
        [&project, &workspace, &engine, &uiTheme, supportsDetachedWindows] {
          const QString firstDiagram = project.data().diagrams.first().id;
          const QString smokeElement =
              project.addElement(QStringLiteral("class"), firstDiagram);
          const QString secondDiagram = project.addDiagram();
          project.addElement(QStringLiteral("enumeration"), secondDiagram);
          const QString smokeFolder = project.addBrowserFolder(
              QStringLiteral("model"), {}, QStringLiteral("Smoke Folder"));
          project.saveDiagramStyle(
              {}, QStringLiteral("Smoke style"),
              {{QStringLiteral("fill"), uiTheme.classFill()},
               {QStringLiteral("headerFill"), uiTheme.panelHeader()},
               {QStringLiteral("border"), uiTheme.nodeBorder()},
               {QStringLiteral("primaryText"), uiTheme.nodeTitleText()},
               {QStringLiteral("secondaryText"), uiTheme.bodyText()},
               {QStringLiteral("divider"), uiTheme.compartmentDivider()}});
          project.addTreeItemsToDiagram(
              firstDiagram, {},
              QStringLiteral(R"([{"kind":"folder","id":"%1"}])")
                  .arg(smokeFolder),
              100.0, 100.0);
          project.selectObject(smokeElement, QStringLiteral("element"));
          if (supportsDetachedWindows)
            workspace.detachDiagram(secondDiagram, 80, 80);

          // Exercise creation of the Preferences color delegates, not only
          // compilation of the closed dialog. This catches runtime model-role
          // and layout errors in the settings page during the UI smoke test.
          const auto roots = engine.rootObjects();
          QObject *rootObject = roots.isEmpty() ? nullptr : roots.first();
          auto *preferences = rootObject
                                  ? rootObject->findChild<QObject *>(
                                        QStringLiteral("preferencesDialog"))
                                  : nullptr;
          auto *folderDialog = rootObject
                                   ? rootObject->findChild<QObject *>(
                                         QStringLiteral("folderNameDialog"))
                                   : nullptr;
          auto *styleDialog = rootObject
                                  ? rootObject->findChild<QObject *>(
                                        QStringLiteral("browserStyleDialog"))
                                  : nullptr;
          auto *stereotypeDialog =
              rootObject ? rootObject->findChild<QObject *>(
                               QStringLiteral("projectStereotypeDialog"))
                         : nullptr;
          auto *stereotypeDropdown =
              rootObject ? rootObject->findChild<QObject *>(
                               QStringLiteral("propertyStereotypeDropdown"))
                         : nullptr;
          auto *tabs = rootObject ? rootObject->findChild<QObject *>(
                                        QStringLiteral("preferencesTabs"))
                                  : nullptr;
          if (!folderDialog ||
              !QMetaObject::invokeMethod(folderDialog, "open") ||
              !QMetaObject::invokeMethod(folderDialog, "close") ||
              !styleDialog ||
              !QMetaObject::invokeMethod(styleDialog, "openManager") ||
              !QMetaObject::invokeMethod(styleDialog, "close") ||
              !stereotypeDialog ||
              !QMetaObject::invokeMethod(stereotypeDialog, "openManager") ||
              !QMetaObject::invokeMethod(stereotypeDialog, "close") ||
              !stereotypeDropdown ||
              !QMetaObject::invokeMethod(stereotypeDropdown, "openBelow") ||
              !QMetaObject::invokeMethod(stereotypeDropdown,
                                         "cancelDropdown") ||
              !preferences || !tabs ||
              !QMetaObject::invokeMethod(preferences, "open")) {
            QCoreApplication::exit(1);
            return;
          }
          tabs->setProperty("currentIndex", 2);
          // Instantiate both newly separated settings pages. Hidden
          // StackLayout children do not necessarily create all ListView
          // delegates, so visit each page during the smoke test.
          QTimer::singleShot(100, tabs,
                             [tabs] { tabs->setProperty("currentIndex", 3); });
        });
    QTimer::singleShot(750, &application, [&engine] {
      const auto roots = engine.rootObjects();
      auto *window =
          roots.isEmpty() ? nullptr : qobject_cast<QWindow *>(roots.first());
      if (!window) {
        QCoreApplication::exit(1);
        return;
      }
      // Exercise the same main-window close path used interactively. The smoke
      // mutation intentionally makes the project dirty, so authorize closure
      // rather than waiting for a modal user choice in automation.
      window->setProperty("closeAuthorized", true);
      window->close();
    });
    // A failed orderly shutdown should fail the smoke test instead of hanging
    // the test runner indefinitely.
    QTimer::singleShot(5000, &application, [] { QCoreApplication::exit(2); });
  }
  return application.exec();
}
