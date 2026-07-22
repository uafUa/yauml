#include "core/application_settings.h"
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
  for (const auto &diagnostic : outcome.diagnostics) {
    QTextStream &stream =
        diagnostic.severity == uuml::DiagnosticSeverity::Error ? err : out;
    stream << uuml::toString(diagnostic.severity).toUpper() << " ["
           << diagnostic.category << "] " << diagnostic.message;
    if (!diagnostic.elementId.isEmpty())
      stream << " (" << diagnostic.elementId << ")";
    stream << '\n';
  }
  if (outcome.ok) {
    out << "Valid uuml project: " << outcome.project.name << " ("
        << outcome.project.elements.size() << " elements, "
        << outcome.project.diagrams.size() << " diagrams)\n";
    return 0;
  }
  return 2;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("validate"))
    return runValidation(argc, argv);

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
  uuml::WorkspaceController workspace(&project, true);
  uuml::ApplicationSettings applicationSettings;
  uuml::ui::UiTheme uiTheme;
  QObject::connect(&project, &uuml::ProjectController::projectOpened,
                   &applicationSettings,
                   &uuml::ApplicationSettings::addRecentProject);
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
  engine.rootContext()->setContextProperty(QStringLiteral("uiTheme"), &uiTheme);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("Uuml"), QStringLiteral("Main"));
  if (application.arguments().contains(QStringLiteral("--smoke-test"))) {
    const bool supportsDetachedWindows =
        QGuiApplication::platformName() != QStringLiteral("offscreen") &&
        QGuiApplication::platformName() != QStringLiteral("minimal");
    QTimer::singleShot(
        0, &application,
        [&project, &workspace, &engine, supportsDetachedWindows] {
          const QString firstDiagram = project.data().diagrams.first().id;
          project.addElement(QStringLiteral("class"), firstDiagram);
          const QString secondDiagram = project.addDiagram();
          project.addElement(QStringLiteral("enumeration"), secondDiagram);
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
          auto *tabs = rootObject ? rootObject->findChild<QObject *>(
                                        QStringLiteral("preferencesTabs"))
                                  : nullptr;
          if (!preferences || !tabs ||
              !QMetaObject::invokeMethod(preferences, "open")) {
            QCoreApplication::exit(1);
            return;
          }
          tabs->setProperty("currentIndex", 1);
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
