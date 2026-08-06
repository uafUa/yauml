#include "app/icon_registry.h"
#include "app/source_folder_picker.h"
#include "app/update_controller.h"
#include "core/application_settings.h"
#include "core/cpp_import_controller.h"
#include "core/project_controller.h"
#include "core/source_editor_controller.h"
#include "core/workspace_controller.h"
#include "ui/diagram_canvas.h"
#include "ui/diagram_image_exporter.h"
#include "ui/ui_theme.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWindow>

#include <cstdio>

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

} // namespace

int main(int argc, char *argv[]) {
  // Connector and shape geometry is rendered as batched triangles. Requesting
  // multisampling before the GUI application exists smooths their edges on all
  // scene-graph backends that support it, without creating per-item QML shapes.
  QSurfaceFormat surfaceFormat = QSurfaceFormat::defaultFormat();
  surfaceFormat.setSamples(4);
  QSurfaceFormat::setDefaultFormat(surfaceFormat);

  QQuickStyle::setStyle(QStringLiteral("Fusion"));
  QGuiApplication application(argc, argv);
  application.setApplicationName(QStringLiteral("yauml"));
  application.setApplicationDisplayName(QStringLiteral("yauml"));
  application.setApplicationVersion(QStringLiteral(YAUML_VERSION));
  application.setOrganizationName(QStringLiteral("yauml"));
  application.setWindowIcon(QIcon(QStringLiteral(":/branding/yaml-icon.png")));
  migratePreviousProductSettings();

  qmlRegisterType<yauml::DiagramCanvas>("Yauml.Native", 1, 0, "DiagramCanvas");
  qmlRegisterType<yauml::DiagramImageExporter>("Yauml.Native", 1, 0,
                                               "DiagramImageExporter");

  yauml::ProjectController project;
  yauml::ApplicationSettings applicationSettings;
  yauml::SourceEditorController sourceEditor(&project, &applicationSettings);
  yauml::CppImportController cppImport(&project, &applicationSettings);
  yauml::WorkspaceController workspace(&project, true);
  yauml::ui::SourceFolderPicker sourceFolderPicker;
  yauml::ui::IconRegistry iconRegistry;
  yauml::ui::UiTheme uiTheme;
  yauml::ui::UpdateController updateController(
      application.applicationVersion(),
      QUrl(QStringLiteral(YAUML_UPDATE_MANIFEST_URL)), &applicationSettings,
      project.diagnostics());
  if (application.arguments().contains(QStringLiteral("--smoke-test")))
    workspace.setProjectTreeVisible(true);
  for (const QString &error : iconRegistry.errors())
    project.diagnostics()->addWarning(QStringLiteral("icons"), error);
  QObject::connect(&project, &yauml::ProjectController::projectOpened,
                   &applicationSettings,
                   &yauml::ApplicationSettings::addRecentProject);
  QObject::connect(
      &sourceFolderPicker, &yauml::ui::SourceFolderPicker::errorOccurred,
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
  if (application.arguments().contains(QStringLiteral("--smoke-test"))) {
    QObject::connect(&engine, &QQmlEngine::warnings, &application,
                     [](const QList<QQmlError> &warnings) {
                       for (const auto &warning : warnings)
                         std::fprintf(stderr, "%s\n",
                                      qPrintable(warning.toString()));
                       std::fflush(stderr);
                     });
  }
  engine.rootContext()->setContextProperty(QStringLiteral("projectController"),
                                           &project);
  engine.rootContext()->setContextProperty(
      QStringLiteral("workspaceController"), &workspace);
  engine.rootContext()->setContextProperty(
      QStringLiteral("applicationSettings"), &applicationSettings);
  engine.rootContext()->setContextProperty(
      QStringLiteral("sourceEditorController"), &sourceEditor);
  engine.rootContext()->setContextProperty(
      QStringLiteral("cppImportController"), &cppImport);
  engine.rootContext()->setContextProperty(QStringLiteral("sourceFolderPicker"),
                                           &sourceFolderPicker);
  engine.rootContext()->setContextProperty(QStringLiteral("uiTheme"), &uiTheme);
  engine.rootContext()->setContextProperty(QStringLiteral("iconRegistry"),
                                           &iconRegistry);
  engine.rootContext()->setContextProperty(QStringLiteral("updateController"),
                                           &updateController);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("Yauml"), QStringLiteral("Main"));
  if (!application.arguments().contains(QStringLiteral("--smoke-test"))) {
    QTimer::singleShot(1500, &updateController,
                       &yauml::ui::UpdateController::checkAutomaticallyIfDue);
  }
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
          auto *projectTree = rootObject ? rootObject->findChild<QObject *>(
                                               QStringLiteral("projectTree"))
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
              !preferences || !tabs || !projectTree ||
              !QMetaObject::invokeMethod(preferences, "open")) {
            std::fprintf(stderr,
                         "UI smoke setup failed: folder=%d style=%d "
                         "stereotype=%d dropdown=%d preferences=%d tabs=%d\n",
                         folderDialog != nullptr, styleDialog != nullptr,
                         stereotypeDialog != nullptr,
                         stereotypeDropdown != nullptr, preferences != nullptr,
                         tabs != nullptr);
            std::fflush(stderr);
            QCoreApplication::exit(1);
            return;
          }
          tabs->setProperty("currentIndex", 2);
          // Instantiate both newly separated settings pages. Hidden
          // StackLayout children do not necessarily create all ListView
          // delegates, so visit each page during the smoke test.
          QTimer::singleShot(100, tabs,
                             [tabs] { tabs->setProperty("currentIndex", 3); });
          QTimer::singleShot(150, projectTree, [projectTree] {
            QMetaObject::invokeMethod(projectTree, "expandAllBranches");
          });
        });
    QTimer::singleShot(500, &application, [&engine] {
      const auto roots = engine.rootObjects();
      QObject *rootObject = roots.isEmpty() ? nullptr : roots.first();
      QList<QObject *> treeIcons;
      const auto collectTreeIcons = [&](const auto &self,
                                        QQuickItem *item) -> void {
        if (!item)
          return;
        if (item->objectName() == QStringLiteral("projectTreeIcon"))
          treeIcons.append(item);
        for (QQuickItem *child : item->childItems())
          self(self, child);
      };
      auto *quickWindow = qobject_cast<QQuickWindow *>(rootObject);
      collectTreeIcons(collectTreeIcons,
                       quickWindow ? quickWindow->contentItem() : nullptr);
      int assignedIcons = 0;
      int readyIcons = 0;
      for (QObject *icon : treeIcons) {
        if (icon->property("source").toUrl().isEmpty())
          continue;
        ++assignedIcons;
        if (icon->property("status").toInt() == 1 &&
            icon->property("visible").toBool())
          ++readyIcons;
      }
      if (assignedIcons == 0 || readyIcons != assignedIcons) {
        std::fprintf(stderr,
                     "UI smoke project-tree icons failed: assigned=%d "
                     "ready=%d delegates=%lld\n",
                     assignedIcons, readyIcons,
                     static_cast<long long>(treeIcons.size()));
        std::fflush(stderr);
        QCoreApplication::exit(1);
      }
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
