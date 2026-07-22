#include "core/application_settings.h"
#include "core/cpp_import.h"
#include "core/json5.h"
#include "core/project_controller.h"
#include "core/project_serializer.h"
#include "core/workspace_controller.h"
#include "ui/text_occlusion.h"
#include "ui/triangle_batch.h"
#include "ui/ui_theme.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPersistentModelIndex>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace uuml;

namespace {

ProjectData createGridProject(int nodeCount) {
  ProjectData project = createStarterProject(QStringLiteral("Large model"));
  Diagram &diagram = project.diagrams.first();
  constexpr int columns = 30;
  for (int index = 0; index < nodeCount; ++index) {
    ModelElement element;
    element.id = QStringLiteral("element-%1").arg(index);
    element.name = QStringLiteral("Element %1").arg(index);
    project.elements.append(element);

    NodePresentation node;
    node.id = QStringLiteral("node-%1").arg(index);
    node.elementId = element.id;
    node.geometry = QRectF(50.0 + (index % columns) * 250.0,
                           50.0 + (index / columns) * 160.0, 205.0, 125.0);
    diagram.nodes.append(node);
  }
  return project;
}

class IsolatedSettingsScope final {
public:
  IsolatedSettingsScope()
      : m_organization(QCoreApplication::organizationName()),
        m_application(QCoreApplication::applicationName()) {
    QCoreApplication::setOrganizationName(
        QStringLiteral("uuml-preferences-test-%1").arg(newId()));
    QCoreApplication::setApplicationName(QStringLiteral("uuml-core-tests"));
  }

  ~IsolatedSettingsScope() {
    QSettings().clear();
    QCoreApplication::setOrganizationName(m_organization);
    QCoreApplication::setApplicationName(m_application);
  }

private:
  QString m_organization;
  QString m_application;
};

void writeTestFile(const QString &path, const QByteArray &contents) {
  QFile file(path);
  QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
  QCOMPARE(file.write(contents), contents.size());
}

} // namespace

class CoreTests final : public QObject {
  Q_OBJECT

private slots:
  void json5Profile();
  void json5SerializationUsesReadableKeys();
  void deterministicRoundTrip();
  void validationFindsBrokenReferences();
  void commandUndoRedo();
  void cppImportUsesClangAndProtectsUserEdits();
  void cppInterfacePatternClassifiesRealization();
  void cppImportScansSourceFolderWithoutBuildMetadata();
  void largeModelGeometryCommandUndoRedo();
  void bulkDiagramPlacementIsOneUndoableCommand();
  void largeDiagramReplacementUsesFirstFreeSlot();
  void deleteElementCommandRestoresCascade();
  void reconnectRelationshipCommandUndoRedo();
  void textCommandsUndoRedo();
  void relationshipAndDiagramDeletionUndoRedo();
  void relationshipTypesAndPresentationRemoval();
  void connectorAnchorUndoRedo();
  void connectorRoutingUndoRedo();
  void connectorBendPointsUndoRedo();
  void multipleDiagramWorkspace();
  void detachedWindowModelAndGeometryRemainStable();
  void closingAllDetachedWindowsReturnsTheirDiagrams();
  void persistedWorkspaceRestoresTabGroups();
  void applicationPreferencesPersist();
  void recentProjectHistoryPersists();
  void themePreferencesPersistAndReset();
  void interruptedSaveRecovery();
  void fullyCoveredTextHasNoVisibleFragments();
  void partialTextCoveragePreservesOnlyExposedArea();
  void triangleGeometryIsSplitOnPrimitiveBoundaries();
};

void CoreTests::json5Profile() {
  const QByteArray source = R"json5(
        // accepted JSON5 profile
        {
          unquoted: 'value',
          list: [1, 2,],
        }
    )json5";
  const auto parsed = Json5::parse(source);
  QVERIFY2(parsed, qPrintable(parsed.error));
  QVERIFY(parsed.hadComments);
  QCOMPARE(
      parsed.document.object().value(QStringLiteral("unquoted")).toString(),
      QStringLiteral("value"));
  QCOMPARE(
      parsed.document.object().value(QStringLiteral("list")).toArray().size(),
      2);
}

void CoreTests::json5SerializationUsesReadableKeys() {
  QJsonObject object;
  object.insert(QStringLiteral("ordinaryKey"), 1);
  object.insert(QStringLiteral("$metadata"), true);
  object.insert(QStringLiteral("hyphenated-key"), 2);
  object.insert(QStringLiteral("123numeric"), 3);

  const QByteArray serialized = Json5::serialize(QJsonDocument(object));
  QVERIFY(serialized.contains("ordinaryKey: 1"));
  QVERIFY(serialized.contains("$metadata: true"));
  QVERIFY(serialized.contains("\"hyphenated-key\": 2"));
  QVERIFY(serialized.contains("\"123numeric\": 3"));

  const auto parsed = Json5::parse(serialized);
  QVERIFY2(parsed, qPrintable(parsed.error));
  QCOMPARE(parsed.document.object(), object);
}

void CoreTests::deterministicRoundTrip() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ProjectData project = createStarterProject(QStringLiteral("Round trip"));
  ModelElement element;
  element.id = newId();
  element.name = QStringLiteral("Service");
  element.attributes = {QStringLiteral("+ port: int")};
  element.extra.insert(QStringLiteral("futureField"), 42);
  project.elements.append(element);
  project.modelExtra.insert(QStringLiteral("futureRoot"),
                            QStringLiteral("retained"));
  NodePresentation node;
  node.id = newId();
  node.elementId = element.id;
  node.geometry = {10, 20, 220, 100};
  project.diagrams[0].nodes.append(node);
  Relationship relationship;
  relationship.id = newId();
  relationship.type = RelationshipType::Composition;
  relationship.name = QStringLiteral("self reference");
  relationship.sourceId = element.id;
  relationship.targetId = element.id;
  project.relationships.append(relationship);
  ConnectorPresentation connector;
  connector.id = newId();
  connector.relationshipId = relationship.id;
  connector.routing = ConnectorRouting::Orthogonal;
  connector.sourceAnchor.side = ConnectorSide::Right;
  connector.sourceAnchor.offset = 0.25;
  connector.sourceAnchor.extra.insert(QStringLiteral("futureAnchorField"),
                                      true);
  connector.targetAnchor.side = ConnectorSide::Bottom;
  connector.targetAnchor.offset = 0.75;
  ConnectorBendPoint bendPoint;
  bendPoint.position = {180.5, 95.25};
  bendPoint.extra.insert(QStringLiteral("futureBendField"),
                         QStringLiteral("retained"));
  connector.bendPoints.append(bendPoint);
  project.diagrams[0].connectors.append(connector);

  const auto firstSave = ProjectSerializer::save(temporary.path(), project);
  QVERIFY(firstSave.ok);
  QVERIFY(!firstSave.unchanged);
  const auto secondSave = ProjectSerializer::save(temporary.path(), project);
  QVERIFY(secondSave.ok);
  QVERIFY(secondSave.unchanged);

  const auto loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  QCOMPARE(loaded.project, project);
  QCOMPARE(loaded.project.elements[0]
               .extra.value(QStringLiteral("futureField"))
               .toInt(),
           42);
}

void CoreTests::validationFindsBrokenReferences() {
  ProjectData project = createStarterProject();
  NodePresentation node;
  node.id = newId();
  node.elementId = QStringLiteral("missing");
  node.geometry = {0, 0, 100, 100};
  project.diagrams[0].nodes.append(node);
  const auto diagnostics = ProjectSerializer::validate(project);
  QVERIFY(std::any_of(
      diagnostics.cbegin(), diagnostics.cend(),
      [](const Diagnostic &diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error &&
               diagnostic.message.contains(QStringLiteral("missing element"));
      }));
}

void CoreTests::commandUndoRedo() {
  ProjectController controller;
  QSignalSpy stateChanges(&controller, &ProjectController::stateChanged);
  QVERIFY(!controller.dirty());
  const QString diagramId = controller.data().diagrams.first().id;
  const QString elementId =
      controller.addElement(QStringLiteral("class"), diagramId);
  QCOMPARE(controller.data().elements.size(), 1);
  QCOMPARE(controller.data().diagrams.first().nodes.size(), 1);
  QVERIFY(controller.canUndo());
  QCOMPARE(controller.undoText(), QStringLiteral("Create class"));
  QVERIFY(controller.dirty());
  QCOMPARE(stateChanges.count(), 1);

  controller.undo();
  QCOMPARE(controller.data().elements.size(), 0);
  QCOMPARE(controller.data().diagrams.first().nodes.size(), 0);
  QCOMPARE(controller.redoText(), QStringLiteral("Create class"));
  QVERIFY(!controller.dirty());
  QCOMPARE(stateChanges.count(), 2);
  controller.redo();
  QCOMPARE(controller.data().elements.first().id, elementId);
  QCOMPARE(stateChanges.count(), 3);

  controller.selectObject(elementId, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("Renamed"));
  QCOMPARE(controller.data().elements.first().name, QStringLiteral("Renamed"));
  controller.undo();
  QVERIFY(controller.data().elements.first().name != QStringLiteral("Renamed"));

  const QString pendingRedo = controller.redoText();
  controller.setSelectedName(controller.selectedName());
  QCOMPARE(controller.redoText(), pendingRedo);
  QCOMPARE(stateChanges.count(), 5);
}

void CoreTests::cppImportUsesClangAndProtectsUserEdits() {
  if (!CppImportService::available())
    QSKIP("This build was configured without libclang");

  QTemporaryDir sourceDirectory;
  QVERIFY(sourceDirectory.isValid());
  const QString sourcePath =
      sourceDirectory.filePath(QStringLiteral("types.cpp"));
  const auto writeSource = [&](bool sourceChanged, bool hasInheritance = true) {
    const QByteArray serviceAddition =
        sourceChanged ? QByteArray("    void stop();\n") : QByteArray{};
    const QByteArray pointAddition =
        sourceChanged ? QByteArray("    int z;\n") : QByteArray{};
    const QByteArray advancedDeclaration =
        hasInheritance
            ? QByteArray("class AdvancedService : public Service {\n")
            : QByteArray("class AdvancedService {\n");
    writeTestFile(sourcePath,
                  QByteArray("namespace demo {\n"
                             "struct Point {\n"
                             "    int x;\n") +
                      pointAddition +
                      QByteArray("private:\n"
                                 "    double y;\n"
                                 "public:\n"
                                 "    double length() const;\n"
                                 "};\n"
                                 "class Service {\n"
                                 "public:\n"
                                 "    static int run(int count);\n") +
                      serviceAddition + QByteArray("};\n") +
                      advancedDeclaration +
                      QByteArray("public:\n"
                                 "    void refresh();\n"
                                 "};\n"
                                 "}\n"));
  };
  writeSource(false);

  QJsonObject command;
  command.insert(QStringLiteral("directory"), sourceDirectory.path());
  command.insert(QStringLiteral("file"), sourcePath);
  command.insert(QStringLiteral("arguments"),
                 QJsonArray{QStringLiteral("clang++"),
                            QStringLiteral("-std=c++20"), sourcePath});
  writeTestFile(
      sourceDirectory.filePath(QStringLiteral("compile_commands.json")),
      QJsonDocument(QJsonArray{command}).toJson(QJsonDocument::Indented));

  const CppImportPreview initial =
      CppImportService::preview(sourceDirectory.path(), {});
  QVERIFY(initial.ok);
  QCOMPARE(initial.symbols.size(), 3);
  QCOMPARE(initial.inheritances.size(), 1);
  QCOMPARE(initial.elementApplicableCount(), 3);
  QCOMPARE(initial.relationshipApplicableCount(), 1);
  QCOMPARE(initial.applicableCount(), 4);
  QCOMPARE(initial.conflictCount(), 0);
  QCOMPARE(initial.inheritances.first().derivedName,
           QStringLiteral("demo::AdvancedService"));
  QCOMPARE(initial.inheritances.first().baseName,
           QStringLiteral("demo::Service"));
  const auto initialPoint = std::find_if(
      initial.items.cbegin(), initial.items.cend(),
      [](const CppImportItem &item) {
        return item.symbol.qualifiedName == QStringLiteral("demo::Point");
      });
  QVERIFY(initialPoint != initial.items.cend());
  QVERIFY(initialPoint->symbol.attributes.contains(QStringLiteral("+ x: int")));
  QVERIFY(
      initialPoint->symbol.attributes.contains(QStringLiteral("- y: double")));
  QVERIFY(initialPoint->symbol.operations.contains(
      QStringLiteral("+ length(): double const")));

  ProjectController controller;
  QCOMPARE(controller.applyCppImportPlan(initial), 4);
  QCOMPARE(controller.data().elements.size(), 3);
  QCOMPARE(controller.data().relationships.size(), 1);
  QVERIFY(controller.data().elements.first().extra.contains(
      QStringLiteral("sourceBinding")));
  QVERIFY(controller.data().relationships.first().extra.contains(
      QStringLiteral("sourceBinding")));
  QVERIFY(controller.data().relationships.first().type ==
          RelationshipType::Generalization);
  QCOMPARE(controller.undoText(), QStringLiteral("Import C++ changes"));
  controller.undo();
  QVERIFY(controller.data().elements.isEmpty());
  QVERIFY(controller.data().relationships.isEmpty());
  controller.redo();
  QCOMPARE(controller.data().elements.size(), 3);
  QCOMPARE(controller.data().relationships.size(), 1);

  const QString diagramId = controller.data().diagrams.first().id;
  const auto serviceElement = std::find_if(
      controller.data().elements.cbegin(), controller.data().elements.cend(),
      [](const ModelElement &element) {
        return element.name == QStringLiteral("demo::Service");
      });
  const auto advancedElement = std::find_if(
      controller.data().elements.cbegin(), controller.data().elements.cend(),
      [](const ModelElement &element) {
        return element.name == QStringLiteral("demo::AdvancedService");
      });
  QVERIFY(serviceElement != controller.data().elements.cend());
  QVERIFY(advancedElement != controller.data().elements.cend());
  controller.selectObject(serviceElement->id, QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);
  QVERIFY(controller.data().diagrams.first().connectors.isEmpty());
  controller.selectObject(advancedElement->id, QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);
  QCOMPARE(controller.data().diagrams.first().connectors.size(), 1);

  ProjectData imported = createStarterProject();
  QCOMPARE(CppImportService::apply(imported, initial), 4);
  QTemporaryDir importedProjectDirectory;
  QVERIFY(importedProjectDirectory.isValid());
  QVERIFY(
      ProjectSerializer::save(importedProjectDirectory.path(), imported).ok);
  const LoadOutcome reloaded =
      ProjectSerializer::load(importedProjectDirectory.path());
  QVERIFY(reloaded.ok);
  imported = reloaded.project;
  QCOMPARE(imported.relationships.size(), 1);
  QVERIFY(imported.relationships.first().extra.contains(
      QStringLiteral("sourceBinding")));

  auto point =
      std::find_if(imported.elements.begin(), imported.elements.end(),
                   [](const ModelElement &element) {
                     return element.name == QStringLiteral("demo::Point");
                   });
  QVERIFY(point != imported.elements.end());
  QVERIFY(point->extra.contains(QStringLiteral("sourceBinding")));
  point->attributes.append(QStringLiteral("+ manual: bool"));
  imported.relationships.first().name =
      QStringLiteral("user relationship label");

  writeSource(true);
  const CppImportPreview changed = CppImportService::preview(
      sourceDirectory.path(), imported.elements, imported.relationships);
  QCOMPARE(changed.conflictCount(), 1);
  QCOMPARE(changed.applicableCount(), 1);
  const auto changedPoint = std::find_if(
      changed.items.cbegin(), changed.items.cend(),
      [](const CppImportItem &item) {
        return item.symbol.qualifiedName == QStringLiteral("demo::Point");
      });
  QVERIFY(changedPoint != changed.items.cend());
  QVERIFY(changedPoint->action == CppImportAction::Conflict);
  QCOMPARE(changed.relationshipItems.size(), 1);
  QVERIFY(changed.relationshipItems.first().action ==
          CppImportAction::UserModified);

  QCOMPARE(CppImportService::apply(imported, changed), 1);
  point = std::find_if(imported.elements.begin(), imported.elements.end(),
                       [](const ModelElement &element) {
                         return element.name == QStringLiteral("demo::Point");
                       });
  QVERIFY(point != imported.elements.end());
  QVERIFY(point->attributes.contains(QStringLiteral("+ manual: bool")));
  QVERIFY(!point->attributes.contains(QStringLiteral("+ z: int")));
  const auto service =
      std::find_if(imported.elements.cbegin(), imported.elements.cend(),
                   [](const ModelElement &element) {
                     return element.name == QStringLiteral("demo::Service");
                   });
  QVERIFY(service != imported.elements.cend());
  QVERIFY(service->operations.contains(QStringLiteral("+ stop(): void")));
  QCOMPARE(imported.relationships.first().name,
           QStringLiteral("user relationship label"));

  writeSource(true, false);
  const CppImportPreview inheritanceRemoved = CppImportService::preview(
      sourceDirectory.path(), imported.elements, imported.relationships);
  QCOMPARE(inheritanceRemoved.inheritances.size(), 0);
  QCOMPARE(inheritanceRemoved.relationshipItems.size(), 1);
  QVERIFY(inheritanceRemoved.relationshipItems.first().action ==
          CppImportAction::MissingSource);
  QCOMPARE(CppImportService::apply(imported, inheritanceRemoved), 0);
  QCOMPARE(imported.relationships.size(), 1);
}

void CoreTests::cppInterfacePatternClassifiesRealization() {
  if (!CppImportService::available())
    QSKIP("This build was configured without libclang");

  QTemporaryDir sourceDirectory;
  QVERIFY(sourceDirectory.isValid());
  const QString sourcePath =
      sourceDirectory.filePath(QStringLiteral("interfaces.cpp"));
  writeTestFile(sourcePath,
                QByteArray("namespace naming {\n"
                           "class IService {\n"
                           "public:\n"
                           "    virtual ~IService() = default;\n"
                           "    virtual void run() = 0;\n"
                           "};\n"
                           "class Base {};\n"
                           "class Worker : public IService, public Base {};\n"
                           "}\n"));

  QJsonObject command;
  command.insert(QStringLiteral("directory"), sourceDirectory.path());
  command.insert(QStringLiteral("file"), sourcePath);
  command.insert(QStringLiteral("arguments"),
                 QJsonArray{QStringLiteral("clang++"),
                            QStringLiteral("-std=c++20"), sourcePath});
  writeTestFile(
      sourceDirectory.filePath(QStringLiteral("compile_commands.json")),
      QJsonDocument(QJsonArray{command}).toJson(QJsonDocument::Indented));

  const CppImportPreview conventional =
      CppImportService::preview(sourceDirectory.path(), {});
  QVERIFY(conventional.ok);
  QCOMPARE(conventional.inheritances.size(), 2);
  const auto relationshipTo = [&](const CppImportPreview &preview,
                                  const QString &baseName) {
    return std::find_if(preview.inheritances.cbegin(),
                        preview.inheritances.cend(),
                        [&](const CppSourceInheritance &inheritance) {
                          return inheritance.baseName == baseName;
                        });
  };
  auto interfaceEdge =
      relationshipTo(conventional, QStringLiteral("naming::IService"));
  auto baseEdge = relationshipTo(conventional, QStringLiteral("naming::Base"));
  QVERIFY(interfaceEdge != conventional.inheritances.cend());
  QVERIFY(baseEdge != conventional.inheritances.cend());
  QVERIFY(interfaceEdge->relationshipType == RelationshipType::Realization);
  QVERIFY(baseEdge->relationshipType == RelationshipType::Generalization);
  QVERIFY(interfaceEdge->classificationReason.contains(
      CppImportOptions::defaultInterfacePattern()));

  ProjectData imported = createStarterProject();
  QCOMPARE(CppImportService::apply(imported, conventional), 5);

  CppImportOptions customOptions;
  customOptions.interfacePattern = QStringLiteral("^Base$");
  const CppImportPreview custom =
      CppImportService::preview(sourceDirectory.path(), imported.elements,
                                imported.relationships, customOptions);
  QVERIFY(custom.ok);
  QCOMPARE(custom.relationshipApplicableCount(), 2);
  for (const auto &item : custom.relationshipItems)
    QVERIFY(item.action == CppImportAction::Update);
  QCOMPARE(CppImportService::apply(imported, custom), 2);

  const auto relationshipTypeForTarget = [&](const QString &targetName) {
    const auto target =
        std::find_if(imported.elements.cbegin(), imported.elements.cend(),
                     [&](const ModelElement &element) {
                       return element.name == targetName;
                     });
    if (target == imported.elements.cend())
      return std::optional<RelationshipType>{};
    const auto relationship = std::find_if(
        imported.relationships.cbegin(), imported.relationships.cend(),
        [&](const Relationship &candidate) {
          return candidate.targetId == target->id;
        });
    if (relationship == imported.relationships.cend())
      return std::optional<RelationshipType>{};
    return std::optional<RelationshipType>{relationship->type};
  };
  const auto interfaceType =
      relationshipTypeForTarget(QStringLiteral("naming::IService"));
  const auto baseType =
      relationshipTypeForTarget(QStringLiteral("naming::Base"));
  QVERIFY(interfaceType.has_value());
  QVERIFY(baseType.has_value());
  QVERIFY(*interfaceType == RelationshipType::Generalization);
  QVERIFY(*baseType == RelationshipType::Realization);

  CppImportOptions invalidOptions;
  invalidOptions.interfacePattern = QStringLiteral("[");
  const CppImportPreview invalid =
      CppImportService::preview(sourceDirectory.path(), {}, {}, invalidOptions);
  QVERIFY(!invalid.ok);
  QVERIFY(!invalid.diagnostics.isEmpty());
}

void CoreTests::cppImportScansSourceFolderWithoutBuildMetadata() {
  if (!CppImportService::available())
    QSKIP("This build was configured without libclang");

  QTemporaryDir sourceDirectory;
  QVERIFY(sourceDirectory.isValid());
  QDir root(sourceDirectory.path());
  QVERIFY(root.mkpath(QStringLiteral("include/model")));
  QVERIFY(root.mkpath(QStringLiteral("src")));
  QVERIFY(root.mkpath(QStringLiteral("build")));

  writeTestFile(root.filePath(QStringLiteral("include/model/IWorker.h")),
                QByteArray("#pragma once\n"
                           "namespace quick {\n"
                           "class IWorker {\n"
                           "public:\n"
                           "    virtual void run() = 0;\n"
                           "};\n"
                           "}\n"));
  writeTestFile(root.filePath(QStringLiteral("include/model/Worker.h")),
                QByteArray("#pragma once\n"
                           "#include \"model/IWorker.h\"\n"
                           "namespace quick {\n"
                           "class Worker : public IWorker {\n"
                           "public:\n"
                           "    void run();\n"
                           "};\n"
                           "}\n"));
  writeTestFile(root.filePath(QStringLiteral("src/Worker.cpp")),
                QByteArray("#include \"model/Worker.h\"\n"
                           "void quick::Worker::run() {}\n"));
  writeTestFile(
      root.filePath(QStringLiteral("include/model/Standalone.h")),
      QByteArray("#pragma once\n"
                 "#include <dependency-not-installed.hpp>\n"
                 "namespace quick { struct Standalone { int value; }; }\n"));
  writeTestFile(root.filePath(QStringLiteral("build/GeneratedNoise.h")),
                QByteArray("class GeneratedNoise {};\n"));

  const CppImportPreview preview =
      CppImportService::preview(sourceDirectory.path(), {});
  QVERIFY(preview.ok);
  QVERIFY(!preview.usedCompilationDatabase);
  QVERIFY(preview.compilationDatabasePath.isEmpty());
  QCOMPARE(preview.symbols.size(), 3);
  QCOMPARE(preview.inheritances.size(), 1);
  QCOMPARE(preview.applicableCount(), 4);
  const auto hasSymbol = [&](const QString &name) {
    return std::any_of(preview.symbols.cbegin(), preview.symbols.cend(),
                       [&](const CppSourceSymbol &symbol) {
                         return symbol.qualifiedName == name;
                       });
  };
  QVERIFY(hasSymbol(QStringLiteral("quick::IWorker")));
  QVERIFY(hasSymbol(QStringLiteral("quick::Worker")));
  QVERIFY(hasSymbol(QStringLiteral("quick::Standalone")));
  QVERIFY(!hasSymbol(QStringLiteral("GeneratedNoise")));
  QVERIFY(preview.inheritances.first().relationshipType ==
          RelationshipType::Realization);
  QVERIFY(std::none_of(preview.diagnostics.cbegin(), preview.diagnostics.cend(),
                       [](const Diagnostic &diagnostic) {
                         return diagnostic.severity ==
                                DiagnosticSeverity::Error;
                       }));
  QVERIFY(std::any_of(preview.diagnostics.cbegin(), preview.diagnostics.cend(),
                      [](const Diagnostic &diagnostic) {
                        return diagnostic.severity ==
                                   DiagnosticSeverity::Warning &&
                               diagnostic.message.contains(
                                   QStringLiteral("dependency-not-installed"));
                      }));
}

void CoreTests::largeModelGeometryCommandUndoRedo() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  constexpr int nodeCount = 600;
  const ProjectData project = createGridProject(nodeCount);
  QVERIFY(ProjectSerializer::save(temporary.path(), project).ok);

  ProjectController controller;
  QVERIFY(controller.openProject(QUrl::fromLocalFile(temporary.path())));
  const ProjectData before = controller.data();
  const auto &node = before.diagrams.first().nodes.at(317);
  controller.updateNodeGeometry(
      before.diagrams.first().id, node.id, node.geometry.x() + 25.0,
      node.geometry.y() - 12.0, node.geometry.width(), node.geometry.height());
  ProjectData after = before;
  after.diagrams.first().nodes[317].geometry.translate(25.0, -12.0);
  QCOMPARE(controller.data(), after);

  controller.undo();
  QCOMPARE(controller.data(), before);
  controller.redo();
  QCOMPARE(controller.data(), after);
}

void CoreTests::bulkDiagramPlacementIsOneUndoableCommand() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString firstElement =
      controller.addElement(QStringLiteral("class"), diagramId);
  const QString secondElement =
      controller.addElement(QStringLiteral("struct"), diagramId);
  const QModelIndex firstTreeIndex = controller.treeModel()->indexForObject(
      firstElement, QStringLiteral("element"));
  const QModelIndex secondTreeIndex = controller.treeModel()->indexForObject(
      secondElement, QStringLiteral("element"));
  QCOMPARE(controller.treeModel()->elementIdsForIndexes(
               {secondTreeIndex, firstTreeIndex}),
           QStringList({firstElement, secondElement}));
  const auto initialNodes = controller.data().diagrams.first().nodes;
  QCOMPARE(initialNodes.size(), 2);
  QVERIFY(!controller
               .createRelationship(diagramId, initialNodes.at(0).id,
                                   initialNodes.at(1).id,
                                   QStringLiteral("dependency"))
               .isEmpty());

  controller.removePresentations(
      diagramId, {initialNodes.at(0).id, initialNodes.at(1).id});
  const ProjectData beforeDrop = controller.data();
  QVERIFY(beforeDrop.diagrams.first().nodes.isEmpty());
  QVERIFY(beforeDrop.diagrams.first().connectors.isEmpty());
  QCOMPARE(beforeDrop.relationships.size(), 1);

  QCOMPARE(controller.addElementsToDiagram(
               diagramId, {firstElement, secondElement}, 100.0, 200.0),
           2);
  const ProjectData afterDrop = controller.data();
  QCOMPARE(afterDrop.diagrams.first().nodes.size(), 2);
  QCOMPARE(afterDrop.diagrams.first().connectors.size(), 1);
  QCOMPARE(afterDrop.diagrams.first().nodes.at(0).geometry,
           QRectF(100.0, 200.0, 220.0, 120.0));
  QCOMPARE(afterDrop.diagrams.first().nodes.at(1).geometry,
           QRectF(350.0, 200.0, 220.0, 120.0));

  // A whole tree drop is one command regardless of how many presentations and
  // automatically materialized connectors it contains.
  controller.undo();
  QCOMPARE(controller.data(), beforeDrop);
  controller.redo();
  QCOMPARE(controller.data(), afterDrop);

  QCOMPARE(controller.addElementsToDiagram(
               diagramId, {firstElement, secondElement}, 0.0, 0.0),
           0);
  QCOMPARE(controller.data(), afterDrop);
}

void CoreTests::largeDiagramReplacementUsesFirstFreeSlot() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), createGridProject(600)).ok);

  ProjectController controller;
  QVERIFY(controller.openProject(QUrl::fromLocalFile(temporary.path())));
  const QString diagramId = controller.data().diagrams.first().id;

  // This is the performance-sample workflow: remove Component001's
  // presentation, then double-click its project-tree row to place it again.
  // The replacement must fill the visible hole instead of being positioned at
  // 50 + nodeCount * 28, far beyond the diagram's content.
  controller.removePresentations(diagramId, {QStringLiteral("node-0")});
  controller.selectObject(QStringLiteral("element-0"),
                          QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);
  const auto &replacedDiagram = controller.data().diagrams.first();
  const auto replaced =
      std::find_if(replacedDiagram.nodes.cbegin(), replacedDiagram.nodes.cend(),
                   [](const NodePresentation &candidate) {
                     return candidate.elementId == QStringLiteral("element-0");
                   });
  QVERIFY(replaced != replacedDiagram.nodes.cend());
  QCOMPARE(replaced->geometry, QRectF(50.0, 50.0, 220.0, 120.0));
  QVERIFY(
      std::none_of(replacedDiagram.nodes.cbegin(), replacedDiagram.nodes.cend(),
                   [&](const NodePresentation &candidate) {
                     return candidate.id != replaced->id &&
                            candidate.geometry.intersects(replaced->geometry);
                   }));
}

void CoreTests::deleteElementCommandRestoresCascade() {
  ProjectController controller;
  const QString firstDiagram = controller.data().diagrams.first().id;
  const QString firstElement =
      controller.addElement(QStringLiteral("class"), firstDiagram);
  const QString secondElement =
      controller.addElement(QStringLiteral("class"), firstDiagram);
  auto nodes = controller.data().diagrams.first().nodes;
  controller.createRelationship(firstDiagram, nodes.at(0).id, nodes.at(1).id,
                                QStringLiteral("dependency"));

  const QString secondDiagram = controller.addDiagram();
  controller.selectObject(firstElement, QStringLiteral("element"));
  controller.addSelectedToDiagram(secondDiagram);
  controller.selectObject(secondElement, QStringLiteral("element"));
  controller.addSelectedToDiagram(secondDiagram);
  nodes = controller.data().diagrams.at(1).nodes;
  controller.createRelationship(secondDiagram, nodes.at(0).id, nodes.at(1).id,
                                QStringLiteral("association"));

  const ProjectData beforeDeletion = controller.data();
  controller.selectObject(firstElement, QStringLiteral("element"));
  controller.deleteSelected();
  QVERIFY(!findElement(controller.data(), firstElement));
  QCOMPARE(controller.data().relationships.size(), 0);
  for (const auto &diagram : controller.data().diagrams) {
    QVERIFY(std::none_of(diagram.nodes.cbegin(), diagram.nodes.cend(),
                         [&](const NodePresentation &node) {
                           return node.elementId == firstElement;
                         }));
    QCOMPARE(diagram.connectors.size(), 0);
  }

  controller.undo();
  QCOMPARE(controller.data(), beforeDeletion);
  controller.redo();
  QVERIFY(!findElement(controller.data(), firstElement));
}

void CoreTests::reconnectRelationshipCommandUndoRedo() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  controller.addElement(QStringLiteral("class"), diagramId);
  controller.addElement(QStringLiteral("class"), diagramId);
  controller.addElement(QStringLiteral("class"), diagramId);
  const auto nodes = controller.data().diagrams.first().nodes;
  const QString connectorId = controller.createRelationship(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("dependency"));
  const auto *connector =
      findConnector(controller.data().diagrams.first(), connectorId);
  QVERIFY(connector);
  const QString relationshipId = connector->relationshipId;
  const Relationship beforeRelationship =
      *findRelationship(controller.data(), relationshipId);
  const ConnectorAnchor beforeAnchor = connector->sourceAnchor;

  const ConnectorAnchor reattachedAnchor{ConnectorSide::Left, 0.25};
  controller.reconnectRelationshipAtAnchor(
      diagramId, connectorId, nodes.at(2).id, true, reattachedAnchor);
  QCOMPARE(findRelationship(controller.data(), relationshipId)->sourceId,
           nodes.at(2).elementId);
  connector = findConnector(controller.data().diagrams.first(), connectorId);
  QCOMPARE(connector->sourceAnchor, reattachedAnchor);

  controller.undo();
  QCOMPARE(*findRelationship(controller.data(), relationshipId),
           beforeRelationship);
  connector = findConnector(controller.data().diagrams.first(), connectorId);
  QCOMPARE(connector->sourceAnchor, beforeAnchor);
  controller.redo();
  QCOMPARE(findRelationship(controller.data(), relationshipId)->sourceId,
           nodes.at(2).elementId);
}

void CoreTests::textCommandsUndoRedo() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString elementId =
      controller.addElement(QStringLiteral("class"), diagramId);
  controller.selectObject(elementId, QStringLiteral("element"));

  const QStringList originalAttributes =
      findElement(controller.data(), elementId)->attributes;
  controller.setSelectedAttributes(
      QStringLiteral("+ first: int\n+ second: int"));
  QCOMPARE(findElement(controller.data(), elementId)->attributes.size(), 2);
  controller.undo();
  QCOMPARE(findElement(controller.data(), elementId)->attributes,
           originalAttributes);
  controller.redo();

  controller.editText(elementId, QStringLiteral("attribute"), 1,
                      QStringLiteral("+ renamed: int"));
  QCOMPARE(findElement(controller.data(), elementId)->attributes.at(1),
           QStringLiteral("+ renamed: int"));
  controller.undo();
  QCOMPARE(findElement(controller.data(), elementId)->attributes.at(1),
           QStringLiteral("+ second: int"));

  const QStringList originalOperations =
      findElement(controller.data(), elementId)->operations;
  controller.setSelectedOperations(QStringLiteral("+ start(): void"));
  QCOMPARE(findElement(controller.data(), elementId)->operations,
           QStringList{QStringLiteral("+ start(): void")});
  controller.undo();
  QCOMPARE(findElement(controller.data(), elementId)->operations,
           originalOperations);

  controller.setSelectedLiterals(QStringLiteral("First\nSecond"));
  QCOMPARE(findElement(controller.data(), elementId)->enumLiterals,
           QStringList({QStringLiteral("First"), QStringLiteral("Second")}));
  controller.undo();
  QVERIFY(findElement(controller.data(), elementId)->enumLiterals.isEmpty());

  const QString originalDiagramName = controller.diagramName(diagramId);
  controller.renameDiagram(diagramId, QStringLiteral("Renamed diagram"));
  QCOMPARE(controller.diagramName(diagramId),
           QStringLiteral("Renamed diagram"));
  controller.undo();
  QCOMPARE(controller.diagramName(diagramId), originalDiagramName);
}

void CoreTests::relationshipAndDiagramDeletionUndoRedo() {
  ProjectController controller;
  const QString firstDiagram = controller.data().diagrams.first().id;
  const QString firstElement =
      controller.addElement(QStringLiteral("class"), firstDiagram);
  controller.addElement(QStringLiteral("class"), firstDiagram);
  const auto nodes = controller.data().diagrams.first().nodes;
  const QString connectorId = controller.createRelationship(
      firstDiagram, nodes.at(0).id, nodes.at(1).id,
      QStringLiteral("association"));
  const auto *connector =
      findConnector(controller.data().diagrams.first(), connectorId);
  QVERIFY(connector);
  const QString relationshipId = connector->relationshipId;

  controller.editText(relationshipId, QStringLiteral("name"), -1,
                      QStringLiteral("owns"));
  QCOMPARE(findRelationship(controller.data(), relationshipId)->name,
           QStringLiteral("owns"));
  controller.undo();
  QVERIFY(findRelationship(controller.data(), relationshipId)->name !=
          QStringLiteral("owns"));

  const ProjectData beforeRelationshipDeletion = controller.data();
  controller.deleteRelationship(relationshipId);
  QVERIFY(!findRelationship(controller.data(), relationshipId));
  QVERIFY(controller.data().diagrams.first().connectors.isEmpty());
  controller.undo();
  QCOMPARE(controller.data(), beforeRelationshipDeletion);

  const QString secondDiagram = controller.addDiagram();
  controller.selectObject(firstElement, QStringLiteral("element"));
  const ProjectData beforePresentation = controller.data();
  controller.addSelectedToDiagram(secondDiagram);
  QCOMPARE(controller.data().diagrams.at(1).nodes.size(), 1);
  controller.undo();
  QCOMPARE(controller.data(), beforePresentation);
  controller.redo();

  const ProjectData beforeDiagramDeletion = controller.data();
  controller.deleteDiagram(secondDiagram);
  QVERIFY(!findDiagram(controller.data(), secondDiagram));
  controller.undo();
  QCOMPARE(controller.data(), beforeDiagramDeletion);
  controller.redo();
  QVERIFY(!findDiagram(controller.data(), secondDiagram));
}

void CoreTests::connectorAnchorUndoRedo() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  controller.addElement(QStringLiteral("class"), diagramId);
  controller.addElement(QStringLiteral("class"), diagramId);
  const auto &nodes = controller.data().diagrams.first().nodes;
  const QString connectorId = controller.createRelationship(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("dependency"));
  QVERIFY(!connectorId.isEmpty());
  const auto connector = [&]() {
    return findConnector(controller.data().diagrams.first(), connectorId);
  };
  QVERIFY(connector());
  const ConnectorAnchor originalSourceAnchor = connector()->sourceAnchor;

  controller.updateConnectorAnchor(diagramId, connectorId, true,
                                   QStringLiteral("right"), 0.25);
  QVERIFY(connector()->sourceAnchor.side == ConnectorSide::Right);
  QCOMPARE(connector()->sourceAnchor.offset, 0.25);

  controller.undo();
  QCOMPARE(connector()->sourceAnchor, originalSourceAnchor);
  controller.redo();
  QVERIFY(connector()->sourceAnchor.side == ConnectorSide::Right);
  QCOMPARE(connector()->sourceAnchor.offset, 0.25);
}

void CoreTests::connectorRoutingUndoRedo() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  controller.addElement(QStringLiteral("class"), diagramId);
  controller.addElement(QStringLiteral("class"), diagramId);
  const auto nodes = controller.data().diagrams.first().nodes;
  const QString connectorId = controller.createRelationshipWithRouting(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("dependency"),
      QStringLiteral("orthogonal"));
  QVERIFY(!connectorId.isEmpty());
  const auto connector = [&]() {
    return findConnector(controller.data().diagrams.first(), connectorId);
  };
  QVERIFY(connector());
  QVERIFY(connector()->routing == ConnectorRouting::Orthogonal);

  controller.setConnectorRouting(diagramId, connectorId,
                                 QStringLiteral("straight"));
  QVERIFY(connector()->routing == ConnectorRouting::Straight);
  QCOMPARE(controller.undoText(),
           QStringLiteral("Use straight connector routing"));
  controller.undo();
  QVERIFY(connector()->routing == ConnectorRouting::Orthogonal);
  controller.redo();
  QVERIFY(connector()->routing == ConnectorRouting::Straight);
}

void CoreTests::connectorBendPointsUndoRedo() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  controller.addElement(QStringLiteral("class"), diagramId);
  controller.addElement(QStringLiteral("class"), diagramId);
  const auto nodes = controller.data().diagrams.first().nodes;
  const QString connectorId = controller.createRelationship(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("association"));
  QVERIFY(!connectorId.isEmpty());
  const auto connector = [&]() {
    return findConnector(controller.data().diagrams.first(), connectorId);
  };

  controller.insertConnectorBendPoint(diagramId, connectorId, 0, 285.0, 210.0);
  QCOMPARE(connector()->bendPoints.size(), 1);
  QCOMPARE(connector()->bendPoints.first().position, QPointF(285.0, 210.0));
  QCOMPARE(controller.undoText(), QStringLiteral("Add connector bend point"));

  controller.insertConnectorBendPoint(diagramId, connectorId, 1, 340.0, 250.0);
  const auto twoBends = connector()->bendPoints;
  QCOMPARE(twoBends.size(), 2);
  controller.undo();
  QCOMPARE(connector()->bendPoints.size(), 1);
  controller.redo();
  QCOMPARE(connector()->bendPoints, twoBends);

  controller.moveConnectorBendPoint(diagramId, connectorId, 0, 270.0, 180.0);
  QCOMPARE(connector()->bendPoints.first().position, QPointF(270.0, 180.0));
  controller.undo();
  QCOMPARE(connector()->bendPoints, twoBends);
  controller.redo();
  const auto movedBends = connector()->bendPoints;

  controller.removeConnectorBendPoint(diagramId, connectorId, 1);
  QCOMPARE(connector()->bendPoints.size(), 1);
  controller.undo();
  QCOMPARE(connector()->bendPoints, movedBends);

  controller.clearConnectorBendPoints(diagramId, connectorId);
  QVERIFY(connector()->bendPoints.isEmpty());
  controller.undo();
  QCOMPARE(connector()->bendPoints, movedBends);

  // Bend points use diagram coordinates. Moving or resizing either endpoint
  // must not rewrite the connector's manually arranged route.
  const QRectF sourceGeometry = nodes.first().geometry;
  controller.updateNodeGeometry(
      diagramId, nodes.first().id, sourceGeometry.x() + 40.0,
      sourceGeometry.y() + 25.0, sourceGeometry.width() + 10.0,
      sourceGeometry.height());
  QCOMPARE(connector()->bendPoints, movedBends);
  controller.undo();
  QCOMPARE(connector()->bendPoints, movedBends);

  const QString thirdElement =
      controller.addElement(QStringLiteral("class"), diagramId);
  const auto &currentNodes = controller.data().diagrams.first().nodes;
  const auto thirdNode =
      std::find_if(currentNodes.cbegin(), currentNodes.cend(),
                   [&](const NodePresentation &node) {
                     return node.elementId == thirdElement;
                   });
  QVERIFY(thirdNode != currentNodes.cend());
  controller.reconnectRelationship(diagramId, connectorId, thirdNode->id,
                                   false);
  QCOMPARE(connector()->bendPoints, movedBends);
  controller.undo();
  QCOMPARE(connector()->bendPoints, movedBends);
}

void CoreTests::relationshipTypesAndPresentationRemoval() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString sourceElement =
      controller.addElement(QStringLiteral("class"), diagramId);
  const QString targetElement =
      controller.addElement(QStringLiteral("class"), diagramId);
  const auto originalNodes = controller.data().diagrams.first().nodes;

  const QStringList types = {
      QStringLiteral("dependency"),     QStringLiteral("realization"),
      QStringLiteral("generalization"), QStringLiteral("association"),
      QStringLiteral("aggregation"),    QStringLiteral("composition")};
  for (const auto &type : types) {
    const QString connectorId = controller.createRelationship(
        diagramId, originalNodes.at(0).id, originalNodes.at(1).id, type);
    QVERIFY(!connectorId.isEmpty());
    const auto &relationship = controller.data().relationships.constLast();
    QCOMPARE(toString(relationship.type), type);
    QCOMPARE(relationship.sourceId, sourceElement);
    QCOMPARE(relationship.targetId, targetElement);
    const auto *connector =
        findConnector(controller.data().diagrams.first(), connectorId);
    QVERIFY(connector);
    QVERIFY(connector->sourceAnchor.side != ConnectorSide::Automatic);
    QVERIFY(connector->targetAnchor.side != ConnectorSide::Automatic);
    QVERIFY(connector->sourceAnchor.offset >= 0.0);
    QVERIFY(connector->sourceAnchor.offset <= 1.0);
    QVERIFY(connector->targetAnchor.offset >= 0.0);
    QVERIFY(connector->targetAnchor.offset <= 1.0);
  }

  const int relationshipCount = controller.data().relationships.size();
  QSet<QString> relationshipIds;
  for (const auto &relationship : controller.data().relationships)
    relationshipIds.insert(relationship.id);
  controller.removePresentations(diagramId, {originalNodes.at(0).id});
  QCOMPARE(controller.data().elements.size(), 2);
  QCOMPARE(controller.data().relationships.size(), relationshipCount);
  QCOMPARE(controller.data().diagrams.first().nodes.size(), 1);
  QCOMPARE(controller.data().diagrams.first().connectors.size(), 0);

  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes.size(), 2);
  QCOMPARE(controller.data().diagrams.first().connectors.size(), types.size());

  // Re-placing a removed presentation is a new command, not an undo. This is
  // the operation invoked by a project-tree double click.
  controller.redo();
  controller.selectObject(sourceElement, QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);
  QCOMPARE(controller.data().diagrams.first().nodes.size(), 2);
  QCOMPARE(controller.data().diagrams.first().connectors.size(), types.size());
  QVERIFY(std::any_of(controller.data().diagrams.first().nodes.cbegin(),
                      controller.data().diagrams.first().nodes.cend(),
                      [&](const NodePresentation &node) {
                        return node.elementId == sourceElement;
                      }));
  QSet<QString> restoredRelationshipIds;
  for (const auto &connector : controller.data().diagrams.first().connectors) {
    restoredRelationshipIds.insert(connector.relationshipId);
    QVERIFY(connector.sourceAnchor.side == ConnectorSide::Automatic);
    QVERIFY(connector.targetAnchor.side == ConnectorSide::Automatic);
  }
  QCOMPARE(restoredRelationshipIds, relationshipIds);

  // Adding related elements to a new diagram materializes its connector
  // presentations as soon as both semantic endpoints are present.
  const QString secondDiagramId = controller.addDiagram();
  controller.selectObject(sourceElement, QStringLiteral("element"));
  controller.addSelectedToDiagram(secondDiagramId);
  QCOMPARE(controller.data().diagrams.constLast().nodes.size(), 1);
  QCOMPARE(controller.data().diagrams.constLast().connectors.size(), 0);

  controller.selectObject(targetElement, QStringLiteral("element"));
  controller.addSelectedToDiagram(secondDiagramId);
  QCOMPARE(controller.data().diagrams.constLast().nodes.size(), 2);
  QCOMPARE(controller.data().diagrams.constLast().connectors.size(),
           types.size());

  controller.undo();
  QCOMPARE(controller.data().diagrams.constLast().nodes.size(), 1);
  QCOMPARE(controller.data().diagrams.constLast().connectors.size(), 0);
  QCOMPARE(controller.data().relationships.size(), relationshipCount);
  controller.redo();
  QCOMPARE(controller.data().diagrams.constLast().nodes.size(), 2);
  QCOMPARE(controller.data().diagrams.constLast().connectors.size(),
           types.size());
}

void CoreTests::multipleDiagramWorkspace() {
  ProjectController controller;
  WorkspaceController workspace(&controller);
  const QString first = controller.data().diagrams.first().id;
  const QString second = controller.addDiagram();
  QCOMPARE(workspace.diagramIdsForHost(workspace.mainHostId()).size(), 2);

  const QString host = workspace.detachDiagram(second, 300, 200);
  QVERIFY(!host.isEmpty());
  QCOMPARE(workspace.diagramIdsForHost(host), QVariantList{second});
  QCOMPARE(workspace.hostForDiagram(first), workspace.mainHostId());
  QCOMPARE(workspace.hostX(host), 300);

  workspace.moveDiagram(first, host);
  QCOMPARE(workspace.diagramIdsForHost(host).size(), 2);

  // Moving inside a host also supplies the ordering used by tab-strip drops.
  workspace.moveDiagram(second, host, 0);
  QCOMPARE(workspace.diagramIdsForHost(host), QVariantList({second, first}));

  workspace.closeHost(host);
  QCOMPARE(workspace.detachedHostIds().size(), 0);
  QCOMPARE(workspace.diagramIdsForHost(workspace.mainHostId()).size(), 2);
}

void CoreTests::detachedWindowModelAndGeometryRemainStable() {
  ProjectController controller;
  WorkspaceController workspace(&controller);
  const QString second = controller.addDiagram();
  const QString third = controller.addDiagram();

  const QString firstHost = workspace.detachDiagram(second, 300, 200);
  QCOMPARE(workspace.rowCount(), 1);
  const QPersistentModelIndex firstHostIndex(workspace.index(0, 0));
  QCOMPARE(
      firstHostIndex.data(WorkspaceController::WindowHostIdRole).toString(),
      firstHost);

  workspace.updateHostGeometry(firstHost, 360, 240, 1024, 720);
  const QString secondHost = workspace.detachDiagram(third, 700, 400);
  QCOMPARE(workspace.rowCount(), 2);
  QVERIFY(firstHostIndex.isValid());

  workspace.moveDiagram(third, firstHost);
  QCOMPARE(workspace.rowCount(), 1);
  QVERIFY(firstHostIndex.isValid());
  QCOMPARE(
      firstHostIndex.data(WorkspaceController::WindowHostIdRole).toString(),
      firstHost);
  QCOMPARE(workspace.hostX(firstHost), 360);
  QCOMPARE(workspace.hostY(firstHost), 240);
  QCOMPARE(workspace.hostWidth(firstHost), 1024);
  QCOMPARE(workspace.hostHeight(firstHost), 720);
  QVERIFY(workspace.hostForDiagram(third) != secondHost);
}

void CoreTests::closingAllDetachedWindowsReturnsTheirDiagrams() {
  ProjectController controller;
  WorkspaceController workspace(&controller);
  const QString second = controller.addDiagram();
  const QString third = controller.addDiagram();
  workspace.detachDiagram(second, 100, 100);
  workspace.detachDiagram(third, 200, 200);
  QCOMPARE(workspace.detachedHostIds().size(), 2);

  workspace.closeAllDetachedHosts();

  QCOMPARE(workspace.detachedHostIds().size(), 0);
  QCOMPARE(workspace.diagramIdsForHost(workspace.mainHostId()).size(), 3);
  QCOMPARE(workspace.hostForDiagram(second), workspace.mainHostId());
  QCOMPARE(workspace.hostForDiagram(third), workspace.mainHostId());
}

void CoreTests::persistedWorkspaceRestoresTabGroups() {
  struct SettingsScope {
    QString organization = QCoreApplication::organizationName();
    QString application = QCoreApplication::applicationName();
    SettingsScope() {
      QCoreApplication::setOrganizationName(
          QStringLiteral("uuml-workspace-test-%1").arg(newId()));
      QCoreApplication::setApplicationName(QStringLiteral("uuml-core-tests"));
    }
    ~SettingsScope() {
      QSettings().clear();
      QCoreApplication::setOrganizationName(organization);
      QCoreApplication::setApplicationName(application);
    }
  } settingsScope;

  ProjectController controller;
  const QString first = controller.data().diagrams.first().id;
  const QString second = controller.addDiagram();
  const QString third = controller.addDiagram();

  {
    WorkspaceController workspace(&controller, true);
    const QString host = workspace.detachDiagram(second, 300, 200);
    workspace.moveDiagram(third, host, 0);
    workspace.updateHostGeometry(host, 410, 260, 1100, 760);
    workspace.setActiveDiagramId(third);
    workspace.setProjectTreeVisible(false);
    workspace.setPropertiesVisible(true);
    workspace.updatePanelWidths(315, 365);
    workspace.updateMainWindowGeometry(120, 90, 1500, 960);
  }

  {
    WorkspaceController restored(&controller, true);
    const QString host = restored.hostForDiagram(third);
    QVERIFY(!host.isEmpty());
    QVERIFY(host != restored.mainHostId());
    QCOMPARE(restored.diagramIdsForHost(host), QVariantList({third, second}));
    QCOMPARE(restored.diagramIdsForHost(restored.mainHostId()),
             QVariantList({first}));
    QCOMPARE(restored.hostX(host), 410);
    QCOMPARE(restored.hostY(host), 260);
    QCOMPARE(restored.hostWidth(host), 1100);
    QCOMPARE(restored.hostHeight(host), 760);
    QCOMPARE(restored.activeDiagramId(), third);
    QVERIFY(!restored.projectTreeVisible());
    QVERIFY(restored.propertiesVisible());
    QCOMPARE(restored.projectTreeWidth(), 315);
    QCOMPARE(restored.propertiesWidth(), 365);
    QCOMPARE(restored.mainWindowX(), 120);
    QCOMPARE(restored.mainWindowY(), 90);
    QCOMPARE(restored.mainWindowWidth(), 1500);
    QCOMPARE(restored.mainWindowHeight(), 960);
  }
}

void CoreTests::applicationPreferencesPersist() {
  IsolatedSettingsScope settingsScope;

  {
    ApplicationSettings settings;
    QCOMPARE(settings.defaultDistributionGap(),
             ApplicationSettings::kDefaultDistributionGap);
    QCOMPARE(settings.snapToGridEnabled(),
             ApplicationSettings::kDefaultSnapToGridEnabled);
    QCOMPARE(settings.alignmentGuidesEnabled(),
             ApplicationSettings::kDefaultAlignmentGuidesEnabled);
    QCOMPARE(settings.gridSpacing(), ApplicationSettings::kDefaultGridSpacing);
    QCOMPARE(settings.defaultConnectorRouting(), QStringLiteral("straight"));
    QCOMPARE(settings.cppInterfacePattern(),
             ApplicationSettings::defaultCppInterfacePattern());
    QCOMPARE(settings.relationshipGestureKeys(),
             ApplicationSettings::defaultRelationshipGestureKeys());
    QSignalSpy changes(&settings,
                       &ApplicationSettings::defaultDistributionGapChanged);
    settings.setDefaultDistributionGap(24);
    settings.setSnapToGridEnabled(false);
    settings.setAlignmentGuidesEnabled(false);
    settings.setGridSpacing(35);
    settings.setDefaultConnectorRouting(QStringLiteral("orthogonal"));
    QVERIFY(settings.setCppInterfacePattern(QStringLiteral("^Abstract.*$")));
    QVERIFY(!settings.setCppInterfacePattern(QStringLiteral("[")));
    QCOMPARE(settings.cppInterfacePattern(), QStringLiteral("^Abstract.*$"));
    QVariantMap gestureKeys =
        ApplicationSettings::defaultRelationshipGestureKeys();
    gestureKeys.insert(QStringLiteral("dependency"), QStringLiteral("X"));
    gestureKeys.insert(QStringLiteral("realization"), QStringLiteral("Y"));
    gestureKeys.insert(QStringLiteral("generalization"), QStringLiteral("Z"));
    QVERIFY(settings.setRelationshipGestureKeys(gestureKeys));
    const QVariantMap acceptedGestureKeys = settings.relationshipGestureKeys();
    gestureKeys.insert(QStringLiteral("composition"), QStringLiteral("X"));
    QVERIFY(!settings.setRelationshipGestureKeys(gestureKeys));
    QCOMPARE(settings.relationshipGestureKeys(), acceptedGestureKeys);
    QCOMPARE(changes.count(), 1);
  }

  {
    ApplicationSettings restored;
    QCOMPARE(restored.defaultDistributionGap(), 24);
    QVERIFY(!restored.snapToGridEnabled());
    QVERIFY(!restored.alignmentGuidesEnabled());
    QCOMPARE(restored.gridSpacing(), 35);
    QCOMPARE(restored.defaultConnectorRouting(), QStringLiteral("orthogonal"));
    QCOMPARE(restored.cppInterfacePattern(), QStringLiteral("^Abstract.*$"));
    QVariantMap expectedGestureKeys =
        ApplicationSettings::defaultRelationshipGestureKeys();
    expectedGestureKeys.insert(QStringLiteral("dependency"),
                               QStringLiteral("X"));
    expectedGestureKeys.insert(QStringLiteral("realization"),
                               QStringLiteral("Y"));
    expectedGestureKeys.insert(QStringLiteral("generalization"),
                               QStringLiteral("Z"));
    QCOMPARE(restored.relationshipGestureKeys(), expectedGestureKeys);
    restored.setDefaultDistributionGap(-10);
    restored.setGridSpacing(-10);
    QCOMPARE(restored.defaultDistributionGap(),
             ApplicationSettings::kMinimumDistributionGap);
    QCOMPARE(restored.gridSpacing(), ApplicationSettings::kMinimumGridSpacing);
    restored.resetDefaults();
    QCOMPARE(restored.defaultDistributionGap(),
             ApplicationSettings::kDefaultDistributionGap);
    QCOMPARE(restored.snapToGridEnabled(),
             ApplicationSettings::kDefaultSnapToGridEnabled);
    QCOMPARE(restored.alignmentGuidesEnabled(),
             ApplicationSettings::kDefaultAlignmentGuidesEnabled);
    QCOMPARE(restored.gridSpacing(), ApplicationSettings::kDefaultGridSpacing);
    QCOMPARE(restored.defaultConnectorRouting(), QStringLiteral("straight"));
    QCOMPARE(restored.cppInterfacePattern(),
             ApplicationSettings::defaultCppInterfacePattern());
    QCOMPARE(restored.relationshipGestureKeys(),
             ApplicationSettings::defaultRelationshipGestureKeys());
  }
}

void CoreTests::recentProjectHistoryPersists() {
  IsolatedSettingsScope settingsScope;
  QTemporaryDir projectDirectory;
  QVERIFY(projectDirectory.isValid());
  QVERIFY(
      ProjectSerializer::save(projectDirectory.path(),
                              createStarterProject(QStringLiteral("Recent")))
          .ok);

  ApplicationSettings settings;
  ProjectController controller;
  QObject::connect(&controller, &ProjectController::projectOpened, &settings,
                   &ApplicationSettings::addRecentProject);
  QSignalSpy opened(&controller, &ProjectController::projectOpened);
  QSignalSpy historyChanges(&settings,
                            &ApplicationSettings::recentProjectsChanged);

  QVERIFY(!controller.openProject(QUrl::fromLocalFile(
      QDir(projectDirectory.path()).filePath(QStringLiteral("missing")))));
  QCOMPARE(opened.count(), 0);
  QVERIFY(settings.recentProjects().isEmpty());

  QVERIFY(controller.openProject(QUrl::fromLocalFile(projectDirectory.path())));
  QCOMPARE(opened.count(), 1);
  QCOMPARE(historyChanges.count(), 1);
  QCOMPARE(settings.recentProjects().first().toMap().value("path").toString(),
           QDir::cleanPath(projectDirectory.path()));

  for (int index = 0; index <= ApplicationSettings::kMaximumRecentProjects;
       ++index) {
    settings.addRecentProject(
        QDir(projectDirectory.path())
            .filePath(QStringLiteral("project-%1.uuml").arg(index)));
  }
  const QVariantList capped = settings.recentProjects();
  QCOMPARE(capped.size(), ApplicationSettings::kMaximumRecentProjects);
  const QString newestPath =
      QDir(projectDirectory.path())
          .filePath(QStringLiteral("project-%1.uuml")
                        .arg(ApplicationSettings::kMaximumRecentProjects));
  QCOMPARE(capped.first().toMap().value("path").toString(),
           QDir::cleanPath(newestPath));

  const QString movedPath = capped.last().toMap().value("path").toString();
  settings.addRecentProject(movedPath);
  QCOMPARE(settings.recentProjects().first().toMap().value("path").toString(),
           movedPath);
  const int changesBeforeNoOp = historyChanges.count();
  settings.addRecentProject(movedPath);
  QCOMPARE(historyChanges.count(), changesBeforeNoOp);

  ApplicationSettings restored;
  QCOMPARE(restored.recentProjects(), settings.recentProjects());
  restored.clearRecentProjects();
  QVERIFY(restored.recentProjects().isEmpty());
  QVERIFY(!QSettings().contains(QStringLiteral("history/recentProjects")));
}

void CoreTests::themePreferencesPersistAndReset() {
  IsolatedSettingsScope settingsScope;
  const QColor customClassFill(QStringLiteral("#123456"));
  const QColor customSelectionOverlay(1, 2, 3, 4);

  {
    ui::UiTheme theme;
    QVERIFY(theme.colorRoles().size() > 30);
    QVERIFY(theme.defaultColor(QStringLiteral("classFill")).isValid());
    QCOMPARE(theme.normalizeColor(QStringLiteral("#aBcDeF")),
             QStringLiteral("#ABCDEF"));
    QCOMPARE(theme.colorText(customSelectionOverlay),
             QStringLiteral("#04010203"));

    QSignalSpy changes(&theme, &ui::UiTheme::paletteChanged);
    QVariantMap colors;
    colors.insert(QStringLiteral("classFill"), customClassFill);
    colors.insert(QStringLiteral("selectionOverlay"), customSelectionOverlay);
    theme.setColors(colors);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(theme.classFill(), customClassFill);
    QCOMPARE(theme.selectionOverlay(), customSelectionOverlay);
    QCOMPARE(ui::uiPalette().classFill, customClassFill);
  }

  {
    ui::UiTheme restored;
    QCOMPARE(restored.classFill(), customClassFill);
    QCOMPARE(restored.selectionOverlay(), customSelectionOverlay);
    const QColor defaultClassFill =
        restored.defaultColor(QStringLiteral("classFill"));
    QVERIFY(defaultClassFill != customClassFill);
    QVariantMap defaults;
    defaults.insert(QStringLiteral("classFill"), defaultClassFill);
    defaults.insert(QStringLiteral("selectionOverlay"),
                    restored.defaultColor(QStringLiteral("selectionOverlay")));
    restored.setColors(defaults);
    QVERIFY(!QSettings().contains(
        QStringLiteral("preferences/theme/colors/classFill")));
    QVERIFY(!QSettings().contains(
        QStringLiteral("preferences/theme/colors/selectionOverlay")));

    restored.setColor(QStringLiteral("classFill"), customClassFill);
    restored.resetDefaultColors();
    QCOMPARE(restored.classFill(), defaultClassFill);
  }

  ui::UiTheme defaults;
  QCOMPARE(defaults.classFill(),
           defaults.defaultColor(QStringLiteral("classFill")));
}

void CoreTests::interruptedSaveRecovery() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  ProjectData project = createStarterProject(QStringLiteral("Recover me"));
  QVERIFY(ProjectSerializer::save(temporary.path(), project).ok);

  const QString recovery =
      QDir(temporary.path()).filePath(QStringLiteral(".uuml-recovery"));
  QVERIFY(QDir().mkpath(recovery));
  struct FilePair {
    QString source;
    QString backup;
  };
  const QList<FilePair> files = {
      {QDir(temporary.path()).filePath(QStringLiteral("manifest.json5")),
       QDir(recovery).filePath(QStringLiteral("manifest.json5"))},
      {QDir(temporary.path()).filePath(QStringLiteral("model/model.json5")),
       QDir(recovery).filePath(QStringLiteral("model.json5"))},
      {QDir(temporary.path())
           .filePath(QStringLiteral("diagrams/diagrams.json5")),
       QDir(recovery).filePath(QStringLiteral("diagrams.json5"))}};
  for (const auto &pair : files)
    QVERIFY(QFile::copy(pair.source, pair.backup));
  QFile marker(QDir(recovery).filePath(QStringLiteral("pending")));
  QVERIFY(marker.open(QIODevice::WriteOnly));
  marker.write("pending\n");
  marker.close();
  QFile corrupted(files.at(1).source);
  QVERIFY(corrupted.open(QIODevice::WriteOnly | QIODevice::Truncate));
  corrupted.write("not valid");
  corrupted.close();

  const auto outcome = ProjectSerializer::load(temporary.path());
  QVERIFY(outcome.ok);
  QVERIFY(outcome.recovered);
  QCOMPARE(outcome.project.name, QStringLiteral("Recover me"));
}

void CoreTests::fullyCoveredTextHasNoVisibleFragments() {
  const QRectF textBounds(10, 10, 100, 20);
  QVERIFY(
      uuml::ui::visibleRectangleFragments(textBounds, {QRectF(0, 0, 200, 200)})
          .isEmpty());
}

void CoreTests::partialTextCoveragePreservesOnlyExposedArea() {
  const QRectF textBounds(0, 0, 100, 100);
  const QRectF occluder(20, 20, 60, 60);
  const auto fragments =
      uuml::ui::visibleRectangleFragments(textBounds, {occluder});

  qreal visibleArea = 0.0;
  for (const QRectF &fragment : fragments) {
    visibleArea += fragment.width() * fragment.height();
    QVERIFY(!fragment.intersects(occluder));
  }
  QCOMPARE(fragments.size(), 4);
  QCOMPARE(visibleArea, 6400.0);
}

void CoreTests::triangleGeometryIsSplitOnPrimitiveBoundaries() {
  const auto batches = uuml::ui::triangleBatches(150'006, 60'000);
  QCOMPARE(batches.size(), 3);
  QCOMPARE(batches.at(0).firstVertex, 0);
  QCOMPARE(batches.at(0).vertexCount, 60'000);
  QCOMPARE(batches.at(1).firstVertex, 60'000);
  QCOMPARE(batches.at(1).vertexCount, 60'000);
  QCOMPARE(batches.at(2).firstVertex, 120'000);
  QCOMPARE(batches.at(2).vertexCount, 30'006);
  for (const auto &batch : batches) {
    QCOMPARE(batch.firstVertex % 3, 0);
    QCOMPARE(batch.vertexCount % 3, 0);
    QVERIFY(batch.vertexCount <= 60'000);
  }
}

QTEST_APPLESS_MAIN(CoreTests)

#include "core_tests.moc"
