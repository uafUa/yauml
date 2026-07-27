#include "core/application_settings.h"
#include "core/connector_port_layout.h"
#include "core/cpp_import.h"
#include "core/json5.h"
#include "core/presentation_layout.h"
#include "core/project_controller.h"
#include "core/project_serializer.h"
#include "core/project_style.h"
#include "core/workspace_controller.h"
#include "ui/text_occlusion.h"
#include "ui/triangle_batch.h"
#include "ui/ui_theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
  void actionIconCatalogIsValid();
  void deterministicRoundTrip();
  void unversionedProjectMigrates();
  void invalidProjectSchemaVersionsAreRejected();
  void projectDiagramStylesPersistResolveAndUndo();
  void cppSynchronizationSourcePersistsAndIsUndoable();
  void saveAsRequiresExplicitProjectReplacement();
  void validationFindsBrokenReferences();
  void commandUndoRedo();
  void cppImportUsesClangAndProtectsUserEdits();
  void cppInterfacePatternClassifiesRealization();
  void cppImportClassifiesMemberOwnershipAndDependencies();
  void cppImportCreatesNestedTypeContainment();
  void cppImportScansSourceFolderWithoutBuildMetadata();
  void largeModelGeometryCommandUndoRedo();
  void bulkDiagramPlacementIsOneUndoableCommand();
  void diagramDropSizingModes();
  void diagramLabelsReflectPackageContext();
  void projectTreeExtendedSelection();
  void projectTreeDeletionAndOrdering();
  void projectTreeQualifiedHierarchy();
  void nestedTypeReassignmentIsSemanticAndUndoable();
  void browserFoldersPersistAndReorganize();
  void folderPresentationsPersistAndRemainPresentationOnly();
  void packagePresentationsAreSemanticAndDiagramMovesArePresentational();
  void nestedPackagePresentationDetachesWithoutSemanticMove();
  void emptyPackageFramesAndAncestorAwareWrappingAreUndoable();
  void diagramNamespaceTargetsFollowSemanticAncestry();
  void largeDiagramReplacementUsesFirstFreeSlot();
  void deleteElementCommandRestoresCascade();
  void reconnectRelationshipCommandUndoRedo();
  void textCommandsUndoRedo();
  void relationshipEndMetadataPersistsAndIsUndoable();
  void relationshipAndDiagramDeletionUndoRedo();
  void relationshipTypesAndPresentationRemoval();
  void connectorAnchorUndoRedo();
  void nodePortSnapPointsUndoRedo();
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

void CoreTests::actionIconCatalogIsValid() {
  const QString catalogPath =
      QFINDTESTDATA("../assets/icons/action-icons.json5");
  QVERIFY2(!catalogPath.isEmpty(), "Action icon catalog was not found");
  QFile catalog(catalogPath);
  QVERIFY2(catalog.open(QIODevice::ReadOnly),
           qPrintable(catalog.errorString()));
  const auto parsed = Json5::parse(catalog.readAll());
  QVERIFY2(parsed, qPrintable(parsed.error));

  const QJsonObject root = parsed.document.object();
  QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 1);

  const QString mainQmlPath = QFINDTESTDATA("../qml/Main.qml");
  QVERIFY2(!mainQmlPath.isEmpty(), "Main.qml was not found");
  const QDir qmlDirectory = QFileInfo(mainQmlPath).dir();
  QByteArray qmlSources;
  for (const QString &qmlFile :
       qmlDirectory.entryList({QStringLiteral("*.qml")}, QDir::Files)) {
    QFile source(qmlDirectory.filePath(qmlFile));
    QVERIFY2(source.open(QIODevice::ReadOnly),
             qPrintable(source.errorString()));
    qmlSources += source.readAll();
    qmlSources += '\n';
  }

  const QJsonObject actions = root.value(QStringLiteral("actions")).toObject();
  QVERIFY(!actions.isEmpty());
  int actionCount = 0;
  for (auto category = actions.begin(); category != actions.end(); ++category) {
    const QJsonObject entries = category.value().toObject();
    QVERIFY2(!entries.isEmpty(), qPrintable(category.key()));
    for (auto action = entries.begin(); action != entries.end(); ++action) {
      const QJsonObject entry = action.value().toObject();
      const QString id = category.key() + u'.' + action.key();
      QVERIFY2(!entry.value(QStringLiteral("label")).toString().isEmpty(),
               qPrintable(id + QStringLiteral(" needs a label")));
      QVERIFY2(entry.value(QStringLiteral("contexts")).isArray(),
               qPrintable(id + QStringLiteral(" needs contexts")));
      QVERIFY2(entry.value(QStringLiteral("svg")).isString(),
               qPrintable(id + QStringLiteral(" needs an svg field")));
      const QByteArray quotedId =
          QByteArray("\"") + id.toUtf8() + QByteArray("\"");
      QVERIFY2(qmlSources.contains(quotedId),
               qPrintable(
                   id + QStringLiteral(" is not connected to a QML command")));
      const QString svg = entry.value(QStringLiteral("svg")).toString();
      if (!svg.isEmpty()) {
        QVERIFY2(QFileInfo::exists(QFileInfo(catalogPath).dir().filePath(svg)),
                 qPrintable(id + QStringLiteral(" references missing ") + svg));
      }
      ++actionCount;
    }
  }
  QVERIFY(actionCount >= 60);

  const QJsonObject treeNodes =
      root.value(QStringLiteral("projectTreeNodes")).toObject();
  QVERIFY(treeNodes.size() >= 10);
  for (auto node = treeNodes.begin(); node != treeNodes.end(); ++node) {
    const QJsonObject entry = node.value().toObject();
    QVERIFY2(!entry.value(QStringLiteral("label")).toString().isEmpty(),
             qPrintable(node.key() + QStringLiteral(" needs a label")));
    QVERIFY2(entry.value(QStringLiteral("match")).isObject(),
             qPrintable(node.key() + QStringLiteral(" needs match rules")));
    QVERIFY2(entry.value(QStringLiteral("svg")).isString(),
             qPrintable(node.key() + QStringLiteral(" needs an svg field")));
  }
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
  project.cppImport.sourceRoot = QStringLiteral("D:/sources/round-trip");
  project.cppImport.extra.insert(QStringLiteral("futureSyncPolicy"),
                                 QStringLiteral("retained"));
  NodePresentation node;
  node.id = newId();
  node.elementId = element.id;
  node.geometry = {10, 20, 220, 100};
  node.horizontalPortSnapPoints = 3;
  node.verticalPortSnapPoints = 5;
  project.diagrams[0].nodes.append(node);
  Relationship relationship;
  relationship.id = newId();
  relationship.type = RelationshipType::Composition;
  relationship.name = QStringLiteral("self reference");
  relationship.sourceId = element.id;
  relationship.targetId = element.id;
  relationship.sourceEnd.role = QStringLiteral("owner");
  relationship.sourceEnd.multiplicity = QStringLiteral("1");
  relationship.sourceEnd.extra.insert(QStringLiteral("futureEndField"), true);
  relationship.targetEnd.role = QStringLiteral("items");
  relationship.targetEnd.multiplicity = QStringLiteral("0..*");
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
  QFile manifest(
      QDir(temporary.path()).filePath(QStringLiteral("manifest.json5")));
  QVERIFY(manifest.open(QIODevice::ReadOnly));
  const QByteArray manifestText = manifest.readAll();
  QVERIFY(manifestText.contains("cppImport: {"));
  QVERIFY(manifestText.contains("sourceRoot:"));

  const auto loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  QCOMPARE(loaded.project, project);
  QCOMPARE(loaded.project.elements[0]
               .extra.value(QStringLiteral("futureField"))
               .toInt(),
           42);

  // Shape/type errors remain visible to the same load path used by the
  // headless validator instead of being silently coerced to empty text.
  const QString modelPath =
      QDir(temporary.path()).filePath(QStringLiteral("model/model.json5"));
  QFile modelFile(modelPath);
  QVERIFY(modelFile.open(QIODevice::ReadOnly));
  QByteArray malformedModel = modelFile.readAll();
  modelFile.close();
  QVERIFY(malformedModel.contains("role: \"owner\""));
  malformedModel.replace("role: \"owner\"", "role: 42");
  writeTestFile(modelPath, malformedModel);
  const auto malformed = ProjectSerializer::load(temporary.path());
  QVERIFY(!malformed.ok);
  QVERIFY(std::any_of(malformed.diagnostics.cbegin(),
                      malformed.diagnostics.cend(),
                      [](const Diagnostic &diagnostic) {
                        return diagnostic.message.contains(
                            QStringLiteral("sourceEnd role must be a string"));
                      }));
}

void CoreTests::unversionedProjectMigrates() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const ProjectData original =
      createStarterProject(QStringLiteral("Legacy project"));
  QVERIFY(ProjectSerializer::save(temporary.path(), original).ok);

  const QString manifestPath =
      QDir(temporary.path()).filePath(QStringLiteral("manifest.json5"));
  QFile manifestFile(manifestPath);
  QVERIFY(manifestFile.open(QIODevice::ReadOnly));
  const auto parsedManifest = Json5::parse(manifestFile.readAll());
  manifestFile.close();
  QVERIFY2(parsedManifest, qPrintable(parsedManifest.error));

  QJsonObject legacyManifest = parsedManifest.document.object();
  legacyManifest.remove(QStringLiteral("schemaVersion"));
  legacyManifest.insert(QStringLiteral("legacyExtension"), true);
  writeTestFile(manifestPath,
                Json5::serialize(QJsonDocument(std::move(legacyManifest))));

  const LoadOutcome migrated = ProjectSerializer::load(temporary.path());
  QVERIFY2(migrated.ok, qPrintable(migrated.diagnostics.isEmpty()
                                       ? QString{}
                                       : migrated.diagnostics.first().message));
  QVERIFY(migrated.migrated);
  QCOMPARE(migrated.project.schemaVersion, kCurrentProjectSchemaVersion);
  QVERIFY(
      migrated.project.manifestExtra.value(QStringLiteral("legacyExtension"))
          .toBool());
  QVERIFY(
      std::any_of(migrated.diagnostics.cbegin(), migrated.diagnostics.cend(),
                  [](const Diagnostic &diagnostic) {
                    return diagnostic.severity == DiagnosticSeverity::Info &&
                           diagnostic.category == QStringLiteral("migration") &&
                           diagnostic.message.contains(
                               QStringLiteral("legacy version 0 to 1"));
                  }));

  // Migration is deliberately in-memory until an explicit save. Once saved,
  // the canonical schema marker is persisted and no migration repeats.
  QVERIFY(ProjectSerializer::save(temporary.path(), migrated.project).ok);
  const LoadOutcome canonical = ProjectSerializer::load(temporary.path());
  QVERIFY(canonical.ok);
  QVERIFY(!canonical.migrated);
  QCOMPARE(canonical.project, migrated.project);
}

void CoreTests::invalidProjectSchemaVersionsAreRejected() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(
              temporary.path(),
              createStarterProject(QStringLiteral("Schema validation")))
              .ok);

  const QString manifestPath =
      QDir(temporary.path()).filePath(QStringLiteral("manifest.json5"));
  QFile manifestFile(manifestPath);
  QVERIFY(manifestFile.open(QIODevice::ReadOnly));
  const auto parsedManifest = Json5::parse(manifestFile.readAll());
  manifestFile.close();
  QVERIFY2(parsedManifest, qPrintable(parsedManifest.error));
  const QJsonObject canonicalManifest = parsedManifest.document.object();

  const QList<QJsonValue> malformedVersions = {QJsonValue(QStringLiteral("1")),
                                               QJsonValue(QJsonValue::Null),
                                               QJsonValue(1.5), QJsonValue(-1)};
  for (const QJsonValue &version : malformedVersions) {
    QJsonObject malformedManifest = canonicalManifest;
    malformedManifest.insert(QStringLiteral("schemaVersion"), version);
    writeTestFile(manifestPath, Json5::serialize(QJsonDocument(
                                    std::move(malformedManifest))));

    const LoadOutcome malformed = ProjectSerializer::load(temporary.path());
    QVERIFY(!malformed.ok);
    QVERIFY(!malformed.migrated);
    QVERIFY(std::any_of(
        malformed.diagnostics.cbegin(), malformed.diagnostics.cend(),
        [](const Diagnostic &diagnostic) {
          return diagnostic.category == QStringLiteral("schema") &&
                 diagnostic.message.contains(
                     QStringLiteral("non-negative integer"));
        }));
  }

  QJsonObject futureManifest = canonicalManifest;
  futureManifest.insert(QStringLiteral("schemaVersion"),
                        kCurrentProjectSchemaVersion + 1);
  writeTestFile(manifestPath,
                Json5::serialize(QJsonDocument(std::move(futureManifest))));
  const LoadOutcome future = ProjectSerializer::load(temporary.path());
  QVERIFY(!future.ok);
  QVERIFY(!future.migrated);
  QVERIFY(std::any_of(
      future.diagnostics.cbegin(), future.diagnostics.cend(),
      [](const Diagnostic &diagnostic) {
        return diagnostic.category == QStringLiteral("schema") &&
               diagnostic.message.contains(QStringLiteral("newer")) &&
               diagnostic.message.contains(QStringLiteral("update uuml"));
      }));
}

void CoreTests::projectDiagramStylesPersistResolveAndUndo() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QVariantMap warmColors{
      {QStringLiteral("fill"), QStringLiteral("#FFF4D6")},
      {QStringLiteral("headerFill"), QStringLiteral("#FFE2A1")},
      {QStringLiteral("border"), QStringLiteral("#725100")},
      {QStringLiteral("primaryText"), QStringLiteral("#2A1C00")},
      {QStringLiteral("secondaryText"), QStringLiteral("#513B0A")},
      {QStringLiteral("divider"), QStringLiteral("#C39B42")}};
  const QVariantMap coolColors{
      {QStringLiteral("fill"), QStringLiteral("#ECF6FF")},
      {QStringLiteral("headerFill"), QStringLiteral("#CDE8FF")},
      {QStringLiteral("border"), QStringLiteral("#315D80")},
      {QStringLiteral("primaryText"), QStringLiteral("#123148")},
      {QStringLiteral("secondaryText"), QStringLiteral("#294C65")},
      {QStringLiteral("divider"), QStringLiteral("#83A9C4")}};
  const QVariantMap alertColors{
      {QStringLiteral("fill"), QStringLiteral("#FFF0F2")},
      {QStringLiteral("headerFill"), QStringLiteral("#FFD1D8")},
      {QStringLiteral("border"), QStringLiteral("#8B2635")},
      {QStringLiteral("primaryText"), QStringLiteral("#54111C")},
      {QStringLiteral("secondaryText"), QStringLiteral("#70202D")},
      {QStringLiteral("divider"), QStringLiteral("#C76B78")}};

  const QString warm =
      controller.saveDiagramStyle({}, QStringLiteral("Warm"), warmColors);
  const QString cool =
      controller.saveDiagramStyle({}, QStringLiteral("Cool"), coolColors);
  const QString alert =
      controller.saveDiagramStyle({}, QStringLiteral("Alert"), alertColors);
  QVERIFY(!warm.isEmpty());
  QVERIFY(!cool.isEmpty());
  QVERIFY(!alert.isEmpty());
  QCOMPARE(controller.diagramStyles().size(), 3);

  // Names identify styles to users, while updates retain the stable UUID.
  QCOMPARE(controller.saveDiagramStyle(cool, QStringLiteral("Cool blue"),
                                       coolColors),
           cool);
  QVERIFY(
      controller.saveDiagramStyle({}, QStringLiteral("cool BLUE"), alertColors)
          .isEmpty());

  const QString elementId =
      controller.addElementAt(QStringLiteral("class"), diagramId, 100, 100);
  const QString folderId = controller.addBrowserFolder(
      QStringLiteral("model"), {}, QStringLiteral("Styled group"));
  const QString elementJson = QString::fromUtf8(
      QJsonDocument(QJsonArray{QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("element")},
                        {QStringLiteral("id"), elementId}}})
          .toJson(QJsonDocument::Compact));
  QVERIFY(controller.moveBrowserItems(elementJson, QStringLiteral("folder"),
                                      folderId));

  const Diagram *diagram = findDiagram(controller.data(), diagramId);
  QVERIFY(diagram);
  const auto nodeIterator =
      std::find_if(diagram->nodes.cbegin(), diagram->nodes.cend(),
                   [&](const NodePresentation &node) {
                     return node.elementId == elementId;
                   });
  QVERIFY(nodeIterator != diagram->nodes.cend());
  const QString nodeId = nodeIterator->id;

  controller.assignStyleToBrowserSubject(QStringLiteral("folder"), folderId,
                                         warm);
  diagram = findDiagram(controller.data(), diagramId);
  const NodePresentation *node = findNode(*diagram, nodeId);
  QCOMPARE(project_style::effectiveStyleForNode(controller.data(), *node)->id,
           warm);

  controller.assignStyleToBrowserSubject(QStringLiteral("element"), elementId,
                                         cool);
  QCOMPARE(project_style::effectiveStyleForNode(controller.data(), *node)->id,
           cool);

  controller.assignStyleToPresentations(diagramId, {nodeId}, alert);
  diagram = findDiagram(controller.data(), diagramId);
  node = findNode(*diagram, nodeId);
  QCOMPARE(project_style::effectiveStyleForNode(controller.data(), *node)->id,
           alert);
  QCOMPARE(controller.diagramStyleAssignmentCount(alert), 1);

  // Removing a style clears its assignments as one undoable operation.
  QVERIFY(controller.deleteDiagramStyle(alert));
  diagram = findDiagram(controller.data(), diagramId);
  node = findNode(*diagram, nodeId);
  QVERIFY(node->styleId.isEmpty());
  QCOMPARE(project_style::effectiveStyleForNode(controller.data(), *node)->id,
           cool);
  controller.undo();
  diagram = findDiagram(controller.data(), diagramId);
  node = findNode(*diagram, nodeId);
  QCOMPARE(node->styleId, alert);
  QCOMPARE(project_style::effectiveStyleForNode(controller.data(), *node)->id,
           alert);

  // Legacy synthetic namespace nodes participate in the same inheritance
  // chain until a materialized package element replaces them.
  const QString qualifiedElement =
      controller.addElement(QStringLiteral("struct"));
  controller.selectObject(qualifiedElement, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("legacy::Imported"));
  controller.assignStyleToBrowserSubject(QStringLiteral("namespace"),
                                         QStringLiteral("legacy"), warm);
  QCOMPARE(project_style::effectiveStyleForSubject(
               controller.data(), QStringLiteral("element"), qualifiedElement)
               ->id,
           warm);

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), controller.data()).ok);
  const LoadOutcome loaded = ProjectSerializer::load(temporary.path());
  QVERIFY2(loaded.ok, qPrintable(loaded.diagnostics.isEmpty()
                                     ? QString{}
                                     : loaded.diagnostics.first().message));
  QCOMPARE(loaded.project, controller.data());
}

void CoreTests::cppSynchronizationSourcePersistsAndIsUndoable() {
  ProjectController controller;
  CppImportPreview preview;
  preview.ok = true;
  preview.sourceRoot = QStringLiteral("D:/sources/configured");

  // Configuring a source is a real project edit even when the source and model
  // are already in sync and there are no semantic records to apply.
  QCOMPARE(controller.applyCppImportPlan(preview), 0);
  QCOMPARE(controller.data().cppImport.sourceRoot, preview.sourceRoot);
  QVERIFY(controller.canUndo());
  QCOMPARE(controller.undoText(),
           QStringLiteral("Configure C++ synchronization"));

  controller.undo();
  QVERIFY(controller.data().cppImport.sourceRoot.isEmpty());
  controller.redo();
  QCOMPARE(controller.data().cppImport.sourceRoot, preview.sourceRoot);
}

void CoreTests::saveAsRequiresExplicitProjectReplacement() {
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const QUrl destination = QUrl::fromLocalFile(temporary.path());

  ProjectController original;
  original.newProject(QStringLiteral("Original"));
  QVERIFY(!original.saveDestinationContainsProject(destination));
  QVERIFY(original.saveProject(destination));
  QVERIFY(!original.saveDestinationContainsProject(destination));

  ProjectController replacement;
  replacement.newProject(QStringLiteral("Replacement"));
  QVERIFY(replacement.saveDestinationContainsProject(destination));
  QVERIFY(!replacement.saveProject(destination));
  QCOMPARE(ProjectSerializer::load(temporary.path()).project.name,
           QStringLiteral("Original"));

  QVERIFY(replacement.saveProject(destination, true));
  const LoadOutcome replaced = ProjectSerializer::load(temporary.path());
  QVERIFY(replaced.ok);
  QCOMPARE(replaced.project.name, QStringLiteral("Replacement"));
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
  QCOMPARE(initial.relationships.size(), 1);
  QCOMPARE(initial.elementApplicableCount(), 4);
  QCOMPARE(initial.relationshipApplicableCount(), 1);
  QCOMPARE(initial.applicableCount(), 5);
  QCOMPARE(initial.conflictCount(), 0);
  QCOMPARE(initial.relationships.first().sourceName,
           QStringLiteral("demo::AdvancedService"));
  QCOMPARE(initial.relationships.first().targetName,
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
  QCOMPARE(controller.applyCppImportPlan(initial), 5);
  QCOMPARE(controller.data().elements.size(), 4);
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
  QCOMPARE(controller.data().elements.size(), 4);
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
  const auto importedPackage = std::find_if(
      controller.data().elements.cbegin(), controller.data().elements.cend(),
      [](const ModelElement &element) {
        return element.type == ElementType::Package &&
               element.name == QStringLiteral("demo");
      });
  QVERIFY(importedPackage != controller.data().elements.cend());
  QCOMPARE(serviceElement->packageId, importedPackage->id);
  QCOMPARE(advancedElement->packageId, importedPackage->id);
  controller.selectObject(serviceElement->id, QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);
  QVERIFY(controller.data().diagrams.first().connectors.isEmpty());
  controller.selectObject(advancedElement->id, QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);
  QCOMPARE(controller.data().diagrams.first().connectors.size(), 1);

  ProjectData imported = createStarterProject();
  QCOMPARE(CppImportService::apply(imported, initial), 5);
  QCOMPARE(imported.cppImport.sourceRoot, initial.sourceRoot);
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
  QCOMPARE(inheritanceRemoved.relationships.size(), 0);
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
  QCOMPARE(conventional.relationships.size(), 2);
  const auto relationshipTo = [&](const CppImportPreview &preview,
                                  const QString &baseName) {
    return std::find_if(preview.relationships.cbegin(),
                        preview.relationships.cend(),
                        [&](const CppSourceRelationship &relationship) {
                          return relationship.targetName == baseName;
                        });
  };
  auto interfaceEdge =
      relationshipTo(conventional, QStringLiteral("naming::IService"));
  auto baseEdge = relationshipTo(conventional, QStringLiteral("naming::Base"));
  QVERIFY(interfaceEdge != conventional.relationships.cend());
  QVERIFY(baseEdge != conventional.relationships.cend());
  QVERIFY(interfaceEdge->relationshipType == RelationshipType::Realization);
  QVERIFY(baseEdge->relationshipType == RelationshipType::Generalization);
  QVERIFY(interfaceEdge->classificationReason.contains(
      CppImportOptions::defaultInterfacePattern()));

  ProjectData imported = createStarterProject();
  QCOMPARE(CppImportService::apply(imported, conventional), 6);

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

void CoreTests::cppImportClassifiesMemberOwnershipAndDependencies() {
  if (!CppImportService::available())
    QSKIP("This build was configured without libclang");

  QTemporaryDir sourceDirectory;
  QVERIFY(sourceDirectory.isValid());
  const QString sourcePath =
      sourceDirectory.filePath(QStringLiteral("relationships.cpp"));
  writeTestFile(sourcePath,
                QByteArray("namespace rel {\n"
                           "template<class T> class OwnerPtr {};\n"
                           "template<class T> class SharedPtr {};\n"
                           "template<class T> class MysteryPtr {};\n"
                           "class ValuePart {};\n"
                           "class RawPart {};\n"
                           "class ReferencePart {};\n"
                           "class OwnedPart {};\n"
                           "class SharedPart {};\n"
                           "class UnknownPart {};\n"
                           "class UsedPart {};\n"
                           "class Consumer {\n"
                           "  ValuePart value;\n"
                           "  RawPart *raw;\n"
                           "  ReferencePart &reference;\n"
                           "  OwnerPtr<OwnedPart> owned;\n"
                           "  SharedPtr<SharedPart> shared;\n"
                           "  MysteryPtr<UnknownPart> unknown;\n"
                           "  UsedPart make(UsedPart input);\n"
                           "};\n"
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

  CppImportOptions options;
  options.owningPointerTypes = {QStringLiteral("rel::OwnerPtr")};
  options.sharedPointerTypes = {QStringLiteral("rel::SharedPtr")};
  const CppImportPreview preview =
      CppImportService::preview(sourceDirectory.path(), {}, {}, options);
  QVERIFY(preview.ok);

  const auto relationshipTo = [&](const QString &targetName) {
    return std::find_if(
        preview.relationships.cbegin(), preview.relationships.cend(),
        [&](const CppSourceRelationship &relationship) {
          return relationship.sourceName == QStringLiteral("rel::Consumer") &&
                 relationship.targetName == targetName;
        });
  };
  const auto verifyType = [&](const QString &targetName,
                              RelationshipType expected,
                              const QString &evidenceKind) {
    const auto relationship = relationshipTo(targetName);
    QVERIFY2(relationship != preview.relationships.cend(),
             qPrintable(
                 QStringLiteral("Missing relationship to %1").arg(targetName)));
    QVERIFY(relationship->relationshipType == expected);
    QCOMPARE(relationship->evidenceKind, evidenceKind);
    QVERIFY(!relationship->classificationReason.isEmpty());
  };
  verifyType(QStringLiteral("rel::ValuePart"), RelationshipType::Composition,
             QStringLiteral("member"));
  verifyType(QStringLiteral("rel::RawPart"), RelationshipType::Aggregation,
             QStringLiteral("member"));
  verifyType(QStringLiteral("rel::ReferencePart"),
             RelationshipType::Aggregation, QStringLiteral("member"));
  verifyType(QStringLiteral("rel::OwnedPart"), RelationshipType::Composition,
             QStringLiteral("member"));
  verifyType(QStringLiteral("rel::SharedPart"), RelationshipType::Aggregation,
             QStringLiteral("member"));
  verifyType(QStringLiteral("rel::UnknownPart"), RelationshipType::Association,
             QStringLiteral("member"));
  verifyType(QStringLiteral("rel::UsedPart"), RelationshipType::Dependency,
             QStringLiteral("signature"));

  ProjectData imported = createStarterProject();
  QCOMPARE(CppImportService::apply(imported, preview),
           preview.applicableCount());
  CppImportOptions reclassifiedOptions = options;
  reclassifiedOptions.owningPointerTypes.clear();
  const CppImportPreview reclassified =
      CppImportService::preview(sourceDirectory.path(), imported.elements,
                                imported.relationships, reclassifiedOptions);
  const auto reclassifiedOwned = std::find_if(
      reclassified.relationshipItems.cbegin(),
      reclassified.relationshipItems.cend(),
      [](const CppRelationshipImportItem &item) {
        return item.source.sourceName == QStringLiteral("rel::Consumer") &&
               item.source.targetName == QStringLiteral("rel::OwnedPart");
      });
  QVERIFY(reclassifiedOwned != reclassified.relationshipItems.cend());
  QVERIFY(reclassifiedOwned->action == CppImportAction::Update);
  QVERIFY(reclassifiedOwned->source.relationshipType ==
          RelationshipType::Association);
  const qsizetype relationshipCount = imported.relationships.size();
  QCOMPARE(CppImportService::apply(imported, reclassified), 1);
  QCOMPARE(imported.relationships.size(), relationshipCount);
  const auto updatedOwned = std::find_if(
      imported.relationships.cbegin(), imported.relationships.cend(),
      [&](const Relationship &relationship) {
        return relationship.id == reclassifiedOwned->existingRelationshipId;
      });
  QVERIFY(updatedOwned != imported.relationships.cend());
  QVERIFY(updatedOwned->type == RelationshipType::Association);
}

void CoreTests::cppImportCreatesNestedTypeContainment() {
  if (!CppImportService::available())
    QSKIP("This build was configured without libclang");

  QTemporaryDir sourceDirectory;
  QVERIFY(sourceDirectory.isValid());
  const QString sourcePath =
      sourceDirectory.filePath(QStringLiteral("nested_types.cpp"));
  writeTestFile(sourcePath, QByteArray("namespace domain {\n"
                                       "class Outer {\n"
                                       "public:\n"
                                       "  struct Inner {};\n"
                                       "};\n"
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

  const CppImportPreview preview =
      CppImportService::preview(sourceDirectory.path(), {});
  QVERIFY(preview.ok);
  const auto containment = std::find_if(
      preview.relationships.cbegin(), preview.relationships.cend(),
      [](const CppSourceRelationship &relationship) {
        return relationship.evidenceKind == QStringLiteral("containment") &&
               relationship.sourceName == QStringLiteral("domain::Outer") &&
               relationship.targetName ==
                   QStringLiteral("domain::Outer::Inner");
      });
  QVERIFY(containment != preview.relationships.cend());
  QVERIFY(containment->relationshipType == RelationshipType::Containment);

  ProjectController controller;
  QCOMPARE(controller.applyCppImportPlan(preview), preview.applicableCount());
  const auto outer = std::find_if(
      controller.data().elements.cbegin(), controller.data().elements.cend(),
      [](const ModelElement &element) {
        return element.name == QStringLiteral("domain::Outer");
      });
  const auto inner = std::find_if(
      controller.data().elements.cbegin(), controller.data().elements.cend(),
      [](const ModelElement &element) {
        return element.name == QStringLiteral("domain::Outer::Inner");
      });
  QVERIFY(outer != controller.data().elements.cend());
  QVERIFY(inner != controller.data().elements.cend());
  QCOMPARE(inner->enclosingTypeId, outer->id);
  QCOMPARE(inner->packageId, outer->packageId);

  const auto importedRelationship = std::find_if(
      controller.data().relationships.cbegin(),
      controller.data().relationships.cend(),
      [&](const Relationship &relationship) {
        return relationship.type == RelationshipType::Containment &&
               relationship.sourceId == outer->id &&
               relationship.targetId == inner->id;
      });
  QVERIFY(importedRelationship != controller.data().relationships.cend());
  QCOMPARE(importedRelationship->name, QStringLiteral("contains"));
  QVERIFY(
      importedRelationship->extra.contains(QStringLiteral("sourceBinding")));

  const QString diagramId = controller.data().diagrams.first().id;
  QCOMPARE(controller.addElementsToDiagram(diagramId, {outer->id, inner->id},
                                           40.0, 40.0),
           2);
  QCOMPARE(controller.data().diagrams.first().connectors.size(), 1);
  QCOMPARE(controller.data().diagrams.first().connectors.first().relationshipId,
           importedRelationship->id);
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
  writeTestFile(root.filePath(QStringLiteral("include/model/Standalone.h")),
                QByteArray("#pragma once\n"
                           "#include <dependency-not-installed.hpp>\n"
                           "namespace quick::detail {\n"
                           "struct Standalone { int value; };\n"
                           "}\n"));
  writeTestFile(root.filePath(QStringLiteral("build/GeneratedNoise.h")),
                QByteArray("class GeneratedNoise {};\n"));

  const CppImportPreview preview =
      CppImportService::preview(sourceDirectory.path(), {});
  QVERIFY(preview.ok);
  QVERIFY(!preview.usedCompilationDatabase);
  QVERIFY(preview.compilationDatabasePath.isEmpty());
  QCOMPARE(preview.symbols.size(), 3);
  QCOMPARE(preview.relationships.size(), 1);
  QCOMPARE(preview.applicableCount(), 6);
  const auto hasSymbol = [&](const QString &name) {
    return std::any_of(preview.symbols.cbegin(), preview.symbols.cend(),
                       [&](const CppSourceSymbol &symbol) {
                         return symbol.qualifiedName == name;
                       });
  };
  QVERIFY(hasSymbol(QStringLiteral("quick::IWorker")));
  QVERIFY(hasSymbol(QStringLiteral("quick::Worker")));
  QVERIFY(hasSymbol(QStringLiteral("quick::detail::Standalone")));
  QVERIFY(!hasSymbol(QStringLiteral("GeneratedNoise")));
  QVERIFY(preview.relationships.first().relationshipType ==
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

  ProjectData imported = createStarterProject();
  QCOMPARE(CppImportService::apply(imported, preview), 6);
  const auto quickPackage =
      std::find_if(imported.elements.cbegin(), imported.elements.cend(),
                   [](const ModelElement &element) {
                     return element.type == ElementType::Package &&
                            element.name == QStringLiteral("quick");
                   });
  const auto detailPackage =
      std::find_if(imported.elements.cbegin(), imported.elements.cend(),
                   [](const ModelElement &element) {
                     return element.type == ElementType::Package &&
                            element.name == QStringLiteral("quick::detail");
                   });
  const auto standalone = std::find_if(
      imported.elements.cbegin(), imported.elements.cend(),
      [](const ModelElement &element) {
        return element.name == QStringLiteral("quick::detail::Standalone");
      });
  QVERIFY(quickPackage != imported.elements.cend());
  QVERIFY(detailPackage != imported.elements.cend());
  QVERIFY(standalone != imported.elements.cend());
  QCOMPARE(detailPackage->packageId, quickPackage->id);
  QCOMPARE(standalone->packageId, detailPackage->id);
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
  const auto *firstModelElement = findElement(afterDrop, firstElement);
  const auto *secondModelElement = findElement(afterDrop, secondElement);
  QVERIFY(firstModelElement);
  QVERIFY(secondModelElement);
  QCOMPARE(afterDrop.diagrams.first().nodes.at(0).geometry,
           QRectF(QPointF(100.0, 200.0),
                  presentation_layout::nodeContentSize(*firstModelElement)));
  QCOMPARE(
      afterDrop.diagrams.first().nodes.at(1).geometry,
      QRectF(
          QPointF(
              100.0 +
                  qMax(presentation_layout::nodeContentSize(*firstModelElement)
                           .width(),
                       presentation_layout::nodeContentSize(*secondModelElement)
                           .width()) +
                  24.0,
              200.0),
          presentation_layout::nodeContentSize(*secondModelElement)));

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

void CoreTests::diagramDropSizingModes() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString first = controller.addElement(QStringLiteral("class"));
  const QString second = controller.addElement(QStringLiteral("struct"));

  QCOMPARE(controller.addElementsToDiagram(diagramId, {first, second}, 20.0,
                                           30.0, QStringLiteral("fixed")),
           2);
  const auto fixedNodes = controller.data().diagrams.first().nodes;
  QCOMPARE(fixedNodes.size(), 2);
  for (const auto &node : fixedNodes)
    QCOMPARE(node.geometry.size(),
             QSizeF(presentation_layout::kFixedNodeWidth,
                    presentation_layout::kFixedNodeHeight));
  QCOMPARE(fixedNodes.at(1).geometry.x(),
           20.0 + presentation_layout::kFixedNodeWidth + 24.0);

  controller.undo();
  QVERIFY(controller.data().diagrams.first().nodes.isEmpty());
  QCOMPARE(controller.addElementsToDiagram(diagramId, {first, second}, 20.0,
                                           30.0, QStringLiteral("content")),
           2);
  const auto &contentNodes = controller.data().diagrams.first().nodes;
  QCOMPARE(contentNodes.at(0).geometry.size(),
           presentation_layout::nodeContentSize(
               *findElement(controller.data(), first)));
  QCOMPARE(contentNodes.at(1).geometry.size(),
           presentation_layout::nodeContentSize(
               *findElement(controller.data(), second)));
}

void CoreTests::diagramLabelsReflectPackageContext() {
  ProjectData project;

  ModelElement company;
  company.id = QStringLiteral("company");
  company.type = ElementType::Package;
  company.name = QStringLiteral("company");
  project.elements.append(company);

  ModelElement model;
  model.id = QStringLiteral("model");
  model.type = ElementType::Package;
  model.name = QStringLiteral("model");
  model.packageId = company.id;
  project.elements.append(model);

  ModelElement order;
  order.id = QStringLiteral("order");
  order.type = ElementType::Class;
  order.name = QStringLiteral("company::model::Order");
  order.packageId = model.id;
  project.elements.append(order);

  QCOMPARE(presentation_layout::fullyQualifiedElementName(project, model),
           QStringLiteral("company::model"));
  QCOMPARE(presentation_layout::fullyQualifiedElementName(project, order),
           QStringLiteral("company::model::Order"));

  // Root presentations retain the stored name. A package frame removes only
  // its own qualified prefix, leaving deeper semantic context visible when
  // the element is shown in an ancestor package.
  QCOMPARE(presentation_layout::elementDisplayNameInPackage(project, order, {}),
           QStringLiteral("company::model::Order"));
  QCOMPARE(presentation_layout::elementDisplayNameInPackage(project, order,
                                                            company.id),
           QStringLiteral("model::Order"));
  QCOMPARE(presentation_layout::elementDisplayNameInPackage(project, order,
                                                            model.id),
           QStringLiteral("Order"));

  ContainerPresentation packageFrame;
  packageFrame.subjectKind = QStringLiteral("package");
  packageFrame.subjectId = model.id;
  ContainerPresentation rootPackageFrame;
  rootPackageFrame.subjectKind = QStringLiteral("package");
  rootPackageFrame.subjectId = company.id;
  QCOMPARE(presentation_layout::containerDisplayName(project, packageFrame),
           QStringLiteral("company::model"));
  QVERIFY(presentation_layout::containerTitleWidth(project, packageFrame) >
          presentation_layout::containerTitleWidth(project, rootPackageFrame));
}

void CoreTests::projectTreeExtendedSelection() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString firstElement =
      controller.addElement(QStringLiteral("class"), diagramId);
  const QString secondElement =
      controller.addElement(QStringLiteral("class"), diagramId);
  const QString thirdElement =
      controller.addElement(QStringLiteral("struct"), diagramId);

  ProjectTreeModel *tree = controller.treeModel();
  QItemSelectionModel selection(tree);
  const QModelIndex first =
      tree->indexForObject(firstElement, QStringLiteral("element"));
  const QModelIndex second =
      tree->indexForObject(secondElement, QStringLiteral("element"));
  const QModelIndex third =
      tree->indexForObject(thirdElement, QStringLiteral("element"));

  tree->selectWithModifiers(&selection, first, Qt::NoModifier);
  QCOMPARE(tree->elementIdsForIndexes(selection.selectedRows()),
           QStringList({firstElement}));

  tree->selectWithModifiers(&selection, second, Qt::ControlModifier);
  QCOMPARE(tree->elementIdsForIndexes(selection.selectedRows()),
           QStringList({firstElement, secondElement}));

  tree->selectWithModifiers(&selection, first, Qt::ControlModifier);
  QCOMPARE(tree->elementIdsForIndexes(selection.selectedRows()),
           QStringList({secondElement}));

  tree->selectWithModifiers(&selection, first, Qt::NoModifier);
  tree->selectWithModifiers(&selection, third, Qt::ShiftModifier);
  QCOMPARE(tree->elementIdsForIndexes(selection.selectedRows()),
           QStringList({firstElement, secondElement, thirdElement}));
}

void CoreTests::projectTreeDeletionAndOrdering() {
  ProjectController controller;
  const QString first = controller.addElement(QStringLiteral("class"));
  const QString second = controller.addElement(QStringLiteral("struct"));
  const QString third = controller.addElement(QStringLiteral("class"));
  ProjectTreeModel *tree = controller.treeModel();

  QCOMPARE(tree->indexForObject(first, QStringLiteral("element")).row(), 0);
  QCOMPARE(tree->indexForObject(second, QStringLiteral("element")).row(), 1);
  QVERIFY(
      controller.canReorderBrowserItem(QStringLiteral("element"), second, -1));
  QVERIFY(controller.reorderBrowserItem(QStringLiteral("element"), second, -1));
  QCOMPARE(tree->indexForObject(second, QStringLiteral("element")).row(), 0);
  QCOMPARE(tree->indexForObject(first, QStringLiteral("element")).row(), 1);
  QVERIFY(
      !controller.canReorderBrowserItem(QStringLiteral("element"), second, -1));

  controller.undo();
  QCOMPARE(tree->indexForObject(first, QStringLiteral("element")).row(), 0);
  QCOMPARE(tree->indexForObject(second, QStringLiteral("element")).row(), 1);
  controller.redo();

  const QString selectedItems = tree->browserItemsJsonForIndexes(
      {tree->indexForObject(second, QStringLiteral("element")),
       tree->indexForObject(first, QStringLiteral("element"))});
  QVERIFY(controller.canReorderBrowserItemsAround(
      selectedItems, QStringLiteral("element"), third));
  QVERIFY(controller.reorderBrowserItemsAround(
      selectedItems, QStringLiteral("element"), third, false));
  QCOMPARE(tree->indexForObject(third, QStringLiteral("element")).row(), 0);
  QCOMPARE(tree->indexForObject(second, QStringLiteral("element")).row(), 1);
  QCOMPARE(tree->indexForObject(first, QStringLiteral("element")).row(), 2);
  QCOMPARE(controller.undoText(),
           QStringLiteral("Reorder 2 project-tree items"));
  controller.undo();
  QCOMPARE(tree->indexForObject(second, QStringLiteral("element")).row(), 0);
  QCOMPARE(tree->indexForObject(first, QStringLiteral("element")).row(), 1);
  QCOMPARE(tree->indexForObject(third, QStringLiteral("element")).row(), 2);

  const ProjectData beforeDelete = controller.data();
  controller.deleteBrowserItems(selectedItems);
  QCOMPARE(controller.data().elements.size(), 1);
  QCOMPARE(controller.data().elements.first().id, third);
  QCOMPARE(controller.undoText(),
           QStringLiteral("Delete 2 project-tree items"));
  controller.undo();
  QCOMPARE(controller.data(), beforeDelete);

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), controller.data()).ok);
  const LoadOutcome loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  QCOMPARE(loaded.project, controller.data());
}

void CoreTests::projectTreeQualifiedHierarchy() {
  ProjectController controller;
  const auto addNamedElement = [&](const QString &type, const QString &name) {
    const QString id = controller.addElement(type);
    controller.selectObject(id, QStringLiteral("element"));
    controller.setSelectedName(name);
    return id;
  };

  const QString outer =
      addNamedElement(QStringLiteral("class"), QStringLiteral("demo::Outer"));
  const QString inner = addNamedElement(QStringLiteral("struct"),
                                        QStringLiteral("demo::Outer::Inner"));
  const QString service =
      addNamedElement(QStringLiteral("class"), QStringLiteral("demo::Service"));
  const QString other =
      addNamedElement(QStringLiteral("class"), QStringLiteral("other::Thing"));

  ProjectTreeModel *tree = controller.treeModel();
  const QModelIndex outerIndex =
      tree->indexForObject(outer, QStringLiteral("element"));
  const QModelIndex innerIndex =
      tree->indexForObject(inner, QStringLiteral("element"));
  const QModelIndex serviceIndex =
      tree->indexForObject(service, QStringLiteral("element"));
  const QModelIndex otherIndex =
      tree->indexForObject(other, QStringLiteral("element"));
  const QModelIndex demoNamespace = outerIndex.parent();

  QVERIFY(demoNamespace.isValid());
  QCOMPARE(tree->data(demoNamespace, Qt::DisplayRole).toString(),
           QStringLiteral("demo"));
  QCOMPARE(tree->data(demoNamespace, ProjectTreeModel::KindRole).toString(),
           QStringLiteral("namespace"));
  QCOMPARE(innerIndex.parent(), outerIndex);
  QCOMPARE(serviceIndex.parent(), demoNamespace);
  QCOMPARE(tree->data(innerIndex, Qt::DisplayRole).toString(),
           QStringLiteral("Inner"));
  QVERIFY(otherIndex.parent() != demoNamespace);

  // Dragging a namespace or an owning type expands to all descendant types;
  // overlapping selections are de-duplicated in stable project order.
  QCOMPARE(tree->elementIdsForIndexes({demoNamespace}),
           QStringList({outer, inner, service}));
  QCOMPARE(tree->elementIdsForIndexes({demoNamespace, outerIndex, innerIndex}),
           QStringList({outer, inner, service}));

  QItemSelectionModel selection(tree);
  tree->selectWithModifiers(&selection, demoNamespace, Qt::NoModifier);
  tree->selectWithModifiers(&selection, otherIndex, Qt::ControlModifier);
  QCOMPARE(tree->elementIdsForIndexes(selection.selectedRows()),
           QStringList({outer, inner, service, other}));
}

void CoreTests::nestedTypeReassignmentIsSemanticAndUndoable() {
  ProjectController controller;
  const QString outer = controller.addElement(QStringLiteral("class"));
  const QString inner = controller.addElement(QStringLiteral("struct"));
  controller.selectObject(outer, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("Outer"));
  controller.selectObject(inner, QStringLiteral("element"));
  // A source-style qualified name must adopt the new owner rather than
  // retaining its stale qualifier after the semantic move.
  controller.setSelectedName(QStringLiteral("Legacy::Inner"));

  const QString innerJson = QString::fromUtf8(
      QJsonDocument(QJsonArray{QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("element")},
                        {QStringLiteral("id"), inner}}})
          .toJson(QJsonDocument::Compact));
  const QString summary = controller.browserMoveSemanticChangeSummary(
      innerJson, QStringLiteral("element"), outer);
  QVERIFY(summary.contains(QStringLiteral("Inner")));
  QVERIFY(summary.contains(QStringLiteral("Outer")));

  const ProjectData before = controller.data();
  QVERIFY(controller.moveBrowserItemsWithSemanticReassignment(
      innerJson, QStringLiteral("element"), outer));
  QCOMPARE(findElement(controller.data(), inner)->enclosingTypeId, outer);
  QCOMPARE(findElement(controller.data(), inner)->packageId,
           findElement(controller.data(), outer)->packageId);
  QCOMPARE(findElement(controller.data(), inner)->browserParent,
           (BrowserParent{QStringLiteral("element"), outer}));
  QCOMPARE(presentation_layout::fullyQualifiedElementName(
               controller.data(), *findElement(controller.data(), inner)),
           QStringLiteral("Outer::Inner"));
  QCOMPARE(
      controller.treeModel()
          ->indexForObject(inner, QStringLiteral("element"))
          .parent(),
      controller.treeModel()->indexForObject(outer, QStringLiteral("element")));
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());

  // The semantic ownership and explicit browser placement are one command.
  controller.undo();
  QCOMPARE(controller.data(), before);
  controller.redo();
  QCOMPARE(findElement(controller.data(), inner)->enclosingTypeId, outer);

  // Once nested, moving the owner into its child is rejected as a browser and
  // semantic ownership cycle before any command reaches the undo stack.
  const QString outerJson = QString::fromUtf8(
      QJsonDocument(QJsonArray{QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("element")},
                        {QStringLiteral("id"), outer}}})
          .toJson(QJsonDocument::Compact));
  QVERIFY(!controller.moveBrowserItemsWithSemanticReassignment(
      outerJson, QStringLiteral("element"), inner));

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), controller.data()).ok);
  const LoadOutcome loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  QCOMPARE(findElement(loaded.project, inner)->enclosingTypeId, outer);
}

void CoreTests::browserFoldersPersistAndReorganize() {
  ProjectController controller;
  const auto addNamedElement = [&](const QString &type, const QString &name) {
    const QString id = controller.addElement(type);
    controller.selectObject(id, QStringLiteral("element"));
    controller.setSelectedName(name);
    return id;
  };
  const auto browserItemsJson =
      [](const QList<QPair<QString, QString>> &items) {
        QJsonArray array;
        for (const auto &[kind, id] : items)
          array.append(QJsonObject{{QStringLiteral("kind"), kind},
                                   {QStringLiteral("id"), id}});
        return QString::fromUtf8(
            QJsonDocument(array).toJson(QJsonDocument::Compact));
      };

  const QString outer =
      addNamedElement(QStringLiteral("class"), QStringLiteral("team::Outer"));
  const QString inner = addNamedElement(QStringLiteral("struct"),
                                        QStringLiteral("team::Outer::Inner"));
  const QString service =
      addNamedElement(QStringLiteral("class"), QStringLiteral("team::Service"));
  const QString folder = controller.addBrowserFolder(
      QStringLiteral("namespace"), QStringLiteral("team"),
      QStringLiteral("Architecture"));
  QVERIFY(!folder.isEmpty());
  const QString subfolder = controller.addBrowserFolder(
      QStringLiteral("folder"), folder, QStringLiteral("Details"));
  QVERIFY(!subfolder.isEmpty());
  const QString nestedNamespaceFolder = controller.addBrowserFolder(
      QStringLiteral("namespace"), QStringLiteral("team::Outer::detail"),
      QStringLiteral("Nested namespace content"));
  QVERIFY(!nestedNamespaceFolder.isEmpty());

  QVERIFY(controller.moveBrowserItems(
      browserItemsJson({{QStringLiteral("element"), outer},
                        {QStringLiteral("element"), service}}),
      QStringLiteral("folder"), folder));
  ProjectTreeModel *tree = controller.treeModel();
  const QModelIndex folderIndex =
      tree->indexForObject(folder, QStringLiteral("folder"));
  const QModelIndex outerIndex =
      tree->indexForObject(outer, QStringLiteral("element"));
  const QModelIndex innerIndex =
      tree->indexForObject(inner, QStringLiteral("element"));
  const QModelIndex serviceIndex =
      tree->indexForObject(service, QStringLiteral("element"));
  QCOMPARE(outerIndex.parent(), folderIndex);
  QCOMPARE(innerIndex.parent(), outerIndex);
  QCOMPARE(serviceIndex.parent(), folderIndex);
  QCOMPARE(tree->elementIdsForIndexes({folderIndex}),
           QStringList({outer, inner, service}));

  // Reparenting is cycle-safe and rejected before an undo command is created.
  QVERIFY(!controller.moveBrowserItems(
      browserItemsJson({{QStringLiteral("folder"), folder}}),
      QStringLiteral("folder"), subfolder));
  QVERIFY(!controller.moveBrowserItems(
      browserItemsJson({{QStringLiteral("element"), outer}}),
      QStringLiteral("folder"), nestedNamespaceFolder));
  QCOMPARE(
      findBrowserFolder(controller.data(), folder)->parent,
      (BrowserParent{QStringLiteral("namespace"), QStringLiteral("team")}));

  controller.renameBrowserFolder(folder, QStringLiteral("Core architecture"));
  QCOMPARE(findBrowserFolder(controller.data(), folder)->name,
           QStringLiteral("Core architecture"));

  controller.deleteBrowserFolder(folder);
  QVERIFY(!findBrowserFolder(controller.data(), folder));
  QCOMPARE(
      findBrowserFolder(controller.data(), subfolder)->parent,
      (BrowserParent{QStringLiteral("namespace"), QStringLiteral("team")}));
  QCOMPARE(
      findElement(controller.data(), outer)->browserParent,
      (BrowserParent{QStringLiteral("namespace"), QStringLiteral("team")}));
  controller.undo();
  QVERIFY(findBrowserFolder(controller.data(), folder));
  QCOMPARE(findBrowserFolder(controller.data(), subfolder)->parent,
           (BrowserParent{QStringLiteral("folder"), folder}));
  QCOMPARE(findElement(controller.data(), outer)->browserParent,
           (BrowserParent{QStringLiteral("folder"), folder}));

  const QString elementOwnedFolder = controller.addBrowserFolder(
      QStringLiteral("element"), outer, QStringLiteral("Owned notes"));
  controller.deleteElement(outer);
  QCOMPARE(findBrowserFolder(controller.data(), elementOwnedFolder)->parent,
           (BrowserParent{QStringLiteral("model"), {}}));
  controller.undo();
  QCOMPARE(findBrowserFolder(controller.data(), elementOwnedFolder)->parent,
           (BrowserParent{QStringLiteral("element"), outer}));

  // Browser organization is project data, not workspace-only UI state.
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), controller.data()).ok);
  const LoadOutcome loaded = ProjectSerializer::load(temporary.path());
  QVERIFY2(loaded.ok, qPrintable(loaded.diagnostics.isEmpty()
                                     ? QString{}
                                     : loaded.diagnostics.first().message));
  QCOMPARE(loaded.project, controller.data());
}

void CoreTests::folderPresentationsPersistAndRemainPresentationOnly() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString first = controller.addElement(QStringLiteral("class"));
  const QString second = controller.addElement(QStringLiteral("struct"));
  const QString folder = controller.addBrowserFolder(
      QStringLiteral("model"), {}, QStringLiteral("Architecture"));
  const QString subfolder = controller.addBrowserFolder(
      QStringLiteral("folder"), folder, QStringLiteral("Details"));
  QVERIFY(!folder.isEmpty());
  QVERIFY(!subfolder.isEmpty());

  const auto itemsJson = [](const QString &kind, const QString &id) {
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("kind"), kind},
                                             {QStringLiteral("id"), id}}})
            .toJson(QJsonDocument::Compact));
  };
  QVERIFY(
      controller.moveBrowserItems(itemsJson(QStringLiteral("element"), first),
                                  QStringLiteral("folder"), folder));
  QVERIFY(
      controller.moveBrowserItems(itemsJson(QStringLiteral("element"), second),
                                  QStringLiteral("folder"), subfolder));

  QCOMPARE(controller.addTreeItemsToDiagram(
               diagramId, {first, second},
               itemsJson(QStringLiteral("folder"), folder), 100.0, 100.0),
           4);
  const ProjectData afterDrop = controller.data();
  const Diagram &diagram = afterDrop.diagrams.first();
  QCOMPARE(diagram.containers.size(), 2);
  QCOMPARE(diagram.nodes.size(), 2);
  const auto rootFrame =
      std::find_if(diagram.containers.cbegin(), diagram.containers.cend(),
                   [&](const ContainerPresentation &candidate) {
                     return candidate.subjectId == folder;
                   });
  const auto childFrame =
      std::find_if(diagram.containers.cbegin(), diagram.containers.cend(),
                   [&](const ContainerPresentation &candidate) {
                     return candidate.subjectId == subfolder;
                   });
  QVERIFY(rootFrame != diagram.containers.cend());
  QVERIFY(childFrame != diagram.containers.cend());
  const auto firstNode =
      std::find_if(diagram.nodes.cbegin(), diagram.nodes.cend(),
                   [&](const NodePresentation &candidate) {
                     return candidate.elementId == first;
                   });
  const auto secondNode =
      std::find_if(diagram.nodes.cbegin(), diagram.nodes.cend(),
                   [&](const NodePresentation &candidate) {
                     return candidate.elementId == second;
                   });
  QVERIFY(firstNode != diagram.nodes.cend());
  QVERIFY(secondNode != diagram.nodes.cend());
  QVERIFY(rootFrame->childPresentationIds.contains(childFrame->id));
  QVERIFY(rootFrame->childPresentationIds.contains(firstNode->id));
  QCOMPARE(childFrame->childPresentationIds, QStringList({secondNode->id}));
  QVERIFY(ProjectSerializer::validate(afterDrop).isEmpty());
  ProjectData cyclic = afterDrop;
  auto *cyclicChild = findContainer(cyclic.diagrams.first(), childFrame->id);
  QVERIFY(cyclicChild);
  cyclicChild->childPresentationIds.append(rootFrame->id);
  QVERIFY(!ProjectSerializer::validate(cyclic).isEmpty());

  controller.editText(folder, QStringLiteral("name"), -1,
                      QStringLiteral("Architecture frame"));
  QCOMPARE(findBrowserFolder(controller.data(), folder)->name,
           QStringLiteral("Architecture frame"));
  controller.undo();
  QCOMPARE(controller.data(), afterDrop);

  controller.undo();
  QVERIFY(controller.data().diagrams.first().containers.isEmpty());
  QVERIFY(controller.data().diagrams.first().nodes.isEmpty());
  controller.redo();
  QCOMPARE(controller.data(), afterDrop);

  controller.removeContainerPresentation(diagramId, rootFrame->id);
  QCOMPARE(controller.data().diagrams.first().containers.size(), 1);
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());
  controller.undo();
  QCOMPARE(controller.data(), afterDrop);

  controller.deleteBrowserFolder(folder);
  QVERIFY(!findBrowserFolder(controller.data(), folder));
  QCOMPARE(controller.data().diagrams.first().containers.size(), 1);
  QCOMPARE(findBrowserFolder(controller.data(), subfolder)->parent,
           (BrowserParent{QStringLiteral("model"), {}}));
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());
  controller.undo();
  QCOMPARE(controller.data(), afterDrop);

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), controller.data()).ok);
  const LoadOutcome loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  QCOMPARE(loaded.project, controller.data());
}

void CoreTests::
    packagePresentationsAreSemanticAndDiagramMovesArePresentational() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString packageId = controller.addElement(QStringLiteral("package"));
  const QString classId = controller.addElement(QStringLiteral("class"));

  QJsonObject movedClass;
  movedClass.insert(QStringLiteral("kind"), QStringLiteral("element"));
  movedClass.insert(QStringLiteral("id"), classId);
  const QString movedItemsJson = QString::fromUtf8(
      QJsonDocument(QJsonArray{movedClass}).toJson(QJsonDocument::Compact));
  QVERIFY(!controller
               .browserMovePackageChangeSummary(
                   movedItemsJson, QStringLiteral("element"), packageId)
               .isEmpty());
  QVERIFY(controller
              .browserMovePackageChangeSummary(
                  movedItemsJson, QStringLiteral("element"), packageId)
              .contains(QStringLiteral("Class1")));
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      movedItemsJson, QStringLiteral("element"), packageId));
  const auto *movedClassElement = findElement(controller.data(), classId);
  QVERIFY(movedClassElement);
  QCOMPARE(movedClassElement->packageId, packageId);
  QCOMPARE(movedClassElement->browserParent,
           (BrowserParent{QStringLiteral("element"), packageId}));

  controller.undo();
  movedClassElement = findElement(controller.data(), classId);
  QVERIFY(movedClassElement);
  QVERIFY(movedClassElement->packageId.isEmpty());
  QVERIFY(movedClassElement->browserParent.kind.isEmpty());
  controller.redo();

  // The project-tree double-click path treats a package as a container and
  // expands its contents; it must never create a class-style package node.
  controller.selectObject(packageId, QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);
  const auto &diagram = controller.data().diagrams.first();
  QCOMPARE(diagram.containers.size(), 1);
  QCOMPARE(diagram.nodes.size(), 1);
  QCOMPARE(diagram.nodes.first().elementId, classId);
  QCOMPARE(diagram.containers.first().subjectKind, QStringLiteral("package"));
  QCOMPARE(diagram.containers.first().subjectId, packageId);
  QCOMPARE(diagram.containers.first().childPresentationIds,
           QStringList{diagram.nodes.first().id});
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());
  const QString nodeId = diagram.nodes.first().id;
  const QString frameId = diagram.containers.first().id;

  QVariantMap unchangedGeometry;
  unchangedGeometry.insert(QStringLiteral("id"), nodeId);
  unchangedGeometry.insert(QStringLiteral("x"),
                           diagram.nodes.first().geometry.x());
  unchangedGeometry.insert(QStringLiteral("y"),
                           diagram.nodes.first().geometry.y());
  unchangedGeometry.insert(QStringLiteral("width"),
                           diagram.nodes.first().geometry.width());
  unchangedGeometry.insert(QStringLiteral("height"),
                           diagram.nodes.first().geometry.height());
  controller.movePresentationsToContainer(
      diagramId, {unchangedGeometry}, {nodeId}, {},
      QStringLiteral("Move diagram element"));
  QCOMPARE(findElement(controller.data(), classId)->packageId, packageId);
  QVERIFY(findContainer(controller.data().diagrams.first(), frameId)
              ->childPresentationIds.isEmpty());
  QCOMPARE(presentation_layout::elementDisplayNameInPackage(
               controller.data(), *findElement(controller.data(), classId), {}),
           QStringLiteral("Package1::Class1"));
  controller.undo();
  QCOMPARE(findElement(controller.data(), classId)->packageId, packageId);
  QCOMPARE(findContainer(controller.data().diagrams.first(), frameId)
               ->childPresentationIds,
           QStringList{nodeId});

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), controller.data()).ok);
  const LoadOutcome loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  QCOMPARE(loaded.project, controller.data());

  controller.undo();
  QVERIFY(controller.data().diagrams.first().containers.isEmpty());
  QVERIFY(controller.data().diagrams.first().nodes.isEmpty());
  controller.redo();
  QCOMPARE(controller.data().diagrams.first().containers.size(), 1);
  QCOMPARE(controller.data().diagrams.first().nodes.size(), 1);
}

void CoreTests::nestedPackagePresentationDetachesWithoutSemanticMove() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString parentPackage =
      controller.addElement(QStringLiteral("package"));
  const QString childPackage = controller.addElement(QStringLiteral("package"));
  controller.selectObject(childPackage, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("Nested"));

  const QString childJson = QString::fromUtf8(
      QJsonDocument(QJsonArray{QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("element")},
                        {QStringLiteral("id"), childPackage}}})
          .toJson(QJsonDocument::Compact));
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      childJson, QStringLiteral("element"), parentPackage));
  controller.selectObject(parentPackage, QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);

  const Diagram &diagram = controller.data().diagrams.first();
  const auto parentFrame =
      std::find_if(diagram.containers.cbegin(), diagram.containers.cend(),
                   [&](const ContainerPresentation &candidate) {
                     return candidate.subjectId == parentPackage;
                   });
  const auto childFrame =
      std::find_if(diagram.containers.cbegin(), diagram.containers.cend(),
                   [&](const ContainerPresentation &candidate) {
                     return candidate.subjectId == childPackage;
                   });
  QVERIFY(parentFrame != diagram.containers.cend());
  QVERIFY(childFrame != diagram.containers.cend());
  QVERIFY(parentFrame->childPresentationIds.contains(childFrame->id));

  QVariantMap unchangedGeometry{
      {QStringLiteral("id"), childFrame->id},
      {QStringLiteral("x"), childFrame->geometry.x()},
      {QStringLiteral("y"), childFrame->geometry.y()},
      {QStringLiteral("width"), childFrame->geometry.width()},
      {QStringLiteral("height"), childFrame->geometry.height()}};
  controller.movePresentationsToContainer(
      diagramId, {unchangedGeometry}, {childFrame->id}, {},
      QStringLiteral("Detach namespace presentation"));
  QCOMPARE(findElement(controller.data(), childPackage)->packageId,
           parentPackage);
  QVERIFY(!findContainer(controller.data().diagrams.first(), parentFrame->id)
               ->childPresentationIds.contains(childFrame->id));
  controller.undo();
  QVERIFY(findContainer(controller.data().diagrams.first(), parentFrame->id)
              ->childPresentationIds.contains(childFrame->id));

  const ProjectData beforeDelete = controller.data();
  controller.deleteElement(parentPackage);
  QCOMPARE(findElement(controller.data(), childPackage)->packageId, QString{});
  QCOMPARE(controller.data().diagrams.first().containers.size(), 1);
  QCOMPARE(controller.data().diagrams.first().containers.first().subjectId,
           childPackage);
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());
  controller.undo();
  QCOMPARE(controller.data(), beforeDelete);
}

void CoreTests::emptyPackageFramesAndAncestorAwareWrappingAreUndoable() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString outerPackage = controller.addElement(QStringLiteral("package"));
  controller.selectObject(outerPackage, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("A"));
  const QString innerPackage = controller.addElement(QStringLiteral("package"));
  controller.selectObject(innerPackage, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("B"));
  const QString typeId = controller.addElement(QStringLiteral("class"));
  controller.selectObject(typeId, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("Thing"));

  const auto browserItemJson = [](const QString &id) {
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{QJsonObject{
                          {QStringLiteral("kind"), QStringLiteral("element")},
                          {QStringLiteral("id"), id}}})
            .toJson(QJsonDocument::Compact));
  };
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      browserItemJson(innerPackage), QStringLiteral("element"), outerPackage));
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      browserItemJson(typeId), QStringLiteral("element"), innerPackage));

  // An explicit empty-frame action presents only the selected namespace, even
  // though it owns semantic descendants in the project browser.
  QCOMPARE(controller.addEmptyPackageToDiagram(diagramId, outerPackage), 1);
  QCOMPARE(controller.data().diagrams.first().containers.size(), 1);
  QVERIFY(controller.data().diagrams.first().nodes.isEmpty());
  const QString outerFrameId =
      controller.data().diagrams.first().containers.first().id;
  QVERIFY(controller.data()
              .diagrams.first()
              .containers.first()
              .childPresentationIds.isEmpty());

  QCOMPARE(controller.addElementsToDiagram(diagramId, {typeId}, 100.0, 110.0),
           1);
  const QString nodeId = controller.data().diagrams.first().nodes.first().id;
  const auto *type = findElement(controller.data(), typeId);
  QVERIFY(type);
  QCOMPARE(presentation_layout::elementDisplayNameInPackage(controller.data(),
                                                            *type, {}),
           QStringLiteral("A::B::Thing"));

  const auto *node = findNode(controller.data().diagrams.first(), nodeId);
  QVERIFY(node);
  const QVariantMap geometry{
      {QStringLiteral("id"), nodeId},
      {QStringLiteral("x"), node->geometry.x()},
      {QStringLiteral("y"), node->geometry.y()},
      {QStringLiteral("width"), node->geometry.width()},
      {QStringLiteral("height"), node->geometry.height()}};
  controller.movePresentationsToContainer(
      diagramId, {geometry}, {nodeId}, outerFrameId,
      QStringLiteral("Show type in ancestor namespace"));
  QCOMPARE(findElement(controller.data(), typeId)->packageId, innerPackage);
  QCOMPARE(presentation_layout::elementDisplayNameInPackage(
               controller.data(), *findElement(controller.data(), typeId),
               outerPackage),
           QStringLiteral("B::Thing"));

  QVERIFY(controller.canWrapPresentationInPackage(diagramId, nodeId));
  QVERIFY(controller.wrapPresentationInPackage(diagramId, nodeId));
  const Diagram &wrapped = controller.data().diagrams.first();
  const auto innerFrame = std::find_if(
      wrapped.containers.cbegin(), wrapped.containers.cend(),
      [&](const ContainerPresentation &candidate) {
        return candidate.subjectKind == QStringLiteral("package") &&
               candidate.subjectId == innerPackage;
      });
  QVERIFY(innerFrame != wrapped.containers.cend());
  const QString innerFrameId = innerFrame->id;
  QCOMPARE(innerFrame->childPresentationIds, QStringList{nodeId});
  QVERIFY(findContainer(wrapped, outerFrameId)
              ->childPresentationIds.contains(innerFrameId));
  QVERIFY(!findContainer(wrapped, outerFrameId)
               ->childPresentationIds.contains(nodeId));
  QCOMPARE(findElement(controller.data(), typeId)->packageId, innerPackage);
  QCOMPARE(presentation_layout::elementDisplayNameInPackage(
               controller.data(), *findElement(controller.data(), typeId),
               innerPackage),
           QStringLiteral("Thing"));
  QVERIFY(!controller.canWrapPresentationInPackage(diagramId, nodeId));
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());

  controller.undo();
  QVERIFY(std::none_of(controller.data().diagrams.first().containers.cbegin(),
                       controller.data().diagrams.first().containers.cend(),
                       [&](const ContainerPresentation &candidate) {
                         return candidate.subjectKind ==
                                    QStringLiteral("package") &&
                                candidate.subjectId == innerPackage;
                       }));
  QVERIFY(findContainer(controller.data().diagrams.first(), outerFrameId)
              ->childPresentationIds.contains(nodeId));
  controller.redo();
  QVERIFY(std::any_of(controller.data().diagrams.first().containers.cbegin(),
                      controller.data().diagrams.first().containers.cend(),
                      [&](const ContainerPresentation &candidate) {
                        return candidate.subjectKind ==
                                   QStringLiteral("package") &&
                               candidate.subjectId == innerPackage;
                      }));
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());
}

void CoreTests::diagramNamespaceTargetsFollowSemanticAncestry() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString outerPackage = controller.addElement(QStringLiteral("package"));
  const QString directPackage =
      controller.addElement(QStringLiteral("package"));
  const QString descendantPackage =
      controller.addElement(QStringLiteral("package"));
  const QString unrelatedPackage =
      controller.addElement(QStringLiteral("package"));
  const QString typeId = controller.addElement(QStringLiteral("class"));

  const auto browserItemJson = [](const QString &id) {
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{QJsonObject{
                          {QStringLiteral("kind"), QStringLiteral("element")},
                          {QStringLiteral("id"), id}}})
            .toJson(QJsonDocument::Compact));
  };
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      browserItemJson(directPackage), QStringLiteral("element"), outerPackage));
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      browserItemJson(descendantPackage), QStringLiteral("element"),
      directPackage));
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      browserItemJson(typeId), QStringLiteral("element"), directPackage));

  QCOMPARE(controller.addEmptyPackageToDiagram(diagramId, outerPackage), 1);
  QCOMPARE(controller.addEmptyPackageToDiagram(diagramId, directPackage), 1);
  QCOMPARE(controller.addEmptyPackageToDiagram(diagramId, descendantPackage),
           1);
  QCOMPARE(controller.addEmptyPackageToDiagram(diagramId, unrelatedPackage), 1);
  QCOMPARE(controller.addElementsToDiagram(diagramId, {typeId}, 420.0, 380.0),
           1);

  const Diagram &diagram = controller.data().diagrams.first();
  const QString nodeId = diagram.nodes.first().id;
  const auto frameIdFor = [&](const QString &packageId) {
    const auto frame = std::find_if(
        diagram.containers.cbegin(), diagram.containers.cend(),
        [&](const ContainerPresentation &candidate) {
          return candidate.subjectKind == QStringLiteral("package") &&
                 candidate.subjectId == packageId;
        });
    return frame != diagram.containers.cend() ? frame->id : QString{};
  };
  const QString outerFrameId = frameIdFor(outerPackage);
  const QString directFrameId = frameIdFor(directPackage);
  const QString descendantFrameId = frameIdFor(descendantPackage);
  const QString unrelatedFrameId = frameIdFor(unrelatedPackage);
  QVERIFY(!outerFrameId.isEmpty());
  QVERIFY(!directFrameId.isEmpty());
  QVERIFY(!descendantFrameId.isEmpty());
  QVERIFY(!unrelatedFrameId.isEmpty());

  QVERIFY(controller.canMovePresentationsToContainer(diagramId, {nodeId}, {}));
  QVERIFY(controller.canMovePresentationsToContainer(diagramId, {nodeId},
                                                     directFrameId));
  QVERIFY(controller.canMovePresentationsToContainer(diagramId, {nodeId},
                                                     outerFrameId));
  QVERIFY(!controller.canMovePresentationsToContainer(diagramId, {nodeId},
                                                      descendantFrameId));
  QVERIFY(!controller.canMovePresentationsToContainer(diagramId, {nodeId},
                                                      unrelatedFrameId));

  const ProjectData beforeRejectedMove = controller.data();
  const auto *node = findNode(diagram, nodeId);
  QVERIFY(node);
  const QVariantMap movedGeometry{
      {QStringLiteral("id"), nodeId},
      {QStringLiteral("x"), node->geometry.x() + 50.0},
      {QStringLiteral("y"), node->geometry.y() + 30.0},
      {QStringLiteral("width"), node->geometry.width()},
      {QStringLiteral("height"), node->geometry.height()}};
  controller.movePresentationsToContainer(
      diagramId, {movedGeometry}, {nodeId}, unrelatedFrameId,
      QStringLiteral("Rejected unrelated namespace move"));
  QCOMPARE(controller.data(), beforeRejectedMove);

  controller.movePresentationsToContainer(
      diagramId, {movedGeometry}, {nodeId}, outerFrameId,
      QStringLiteral("Show type in ancestor namespace"));
  QCOMPARE(findElement(controller.data(), typeId)->packageId, directPackage);
  QVERIFY(findContainer(controller.data().diagrams.first(), outerFrameId)
              ->childPresentationIds.contains(nodeId));
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());
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
  const auto *replacedElement =
      findElement(controller.data(), QStringLiteral("element-0"));
  QVERIFY(replacedElement);
  QCOMPARE(replaced->geometry,
           QRectF(QPointF(50.0, 50.0),
                  presentation_layout::nodeContentSize(*replacedElement)));
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

void CoreTests::relationshipEndMetadataPersistsAndIsUndoable() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString sourceElement =
      controller.addElement(QStringLiteral("class"), diagramId);
  const QString targetElement =
      controller.addElement(QStringLiteral("class"), diagramId);
  const auto &nodes = controller.data().diagrams.first().nodes;
  const auto sourceNode =
      std::find_if(nodes.cbegin(), nodes.cend(),
                   [&sourceElement](const NodePresentation &node) {
                     return node.elementId == sourceElement;
                   });
  const auto targetNode =
      std::find_if(nodes.cbegin(), nodes.cend(),
                   [&targetElement](const NodePresentation &node) {
                     return node.elementId == targetElement;
                   });
  QVERIFY(sourceNode != nodes.cend());
  QVERIFY(targetNode != nodes.cend());
  const QString connectorId = controller.createRelationship(
      diagramId, sourceNode->id, targetNode->id, QStringLiteral("association"));
  const auto *connector =
      findConnector(controller.data().diagrams.first(), connectorId);
  QVERIFY(connector);
  const QString relationshipId = connector->relationshipId;

  controller.editText(relationshipId, QStringLiteral("sourceRole"), -1,
                      QStringLiteral("owner"));
  controller.editText(relationshipId, QStringLiteral("sourceMultiplicity"), -1,
                      QStringLiteral("1"));
  controller.editText(relationshipId, QStringLiteral("targetRole"), -1,
                      QStringLiteral("items"));
  controller.editText(relationshipId, QStringLiteral("targetMultiplicity"), -1,
                      QStringLiteral("0..*"));
  const auto relationship = [&]() {
    return findRelationship(controller.data(), relationshipId);
  };
  QCOMPARE(relationship()->sourceEnd.role, QStringLiteral("owner"));
  QCOMPARE(relationship()->sourceEnd.multiplicity, QStringLiteral("1"));
  QCOMPARE(relationship()->targetEnd.role, QStringLiteral("items"));
  QCOMPARE(relationship()->targetEnd.multiplicity, QStringLiteral("0..*"));
  QCOMPARE(controller.undoText(), QStringLiteral("Edit target multiplicity"));

  controller.undo();
  QVERIFY(relationship()->targetEnd.multiplicity.isEmpty());
  controller.redo();
  QCOMPARE(relationship()->targetEnd.multiplicity, QStringLiteral("0..*"));

  // End values are optional: clearing one is a compact command and undo
  // restores only that field.
  controller.editText(relationshipId, QStringLiteral("sourceRole"), -1, {});
  QVERIFY(relationship()->sourceEnd.role.isEmpty());
  controller.undo();
  QCOMPARE(relationship()->sourceEnd.role, QStringLiteral("owner"));

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  const auto saved =
      ProjectSerializer::save(temporary.path(), controller.data());
  QVERIFY2(saved.ok, qPrintable(saved.diagnostics.isEmpty()
                                    ? QString()
                                    : saved.diagnostics.first().message));
  const auto loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  const auto *loadedRelationship =
      findRelationship(loaded.project, relationshipId);
  QVERIFY(loadedRelationship);
  QCOMPARE(loadedRelationship->sourceEnd, relationship()->sourceEnd);
  QCOMPARE(loadedRelationship->targetEnd, relationship()->targetEnd);
  QVERIFY(ProjectSerializer::validate(loaded.project).isEmpty());
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

void CoreTests::nodePortSnapPointsUndoRedo() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  controller.addElement(QStringLiteral("class"), diagramId);
  const QString nodeId = controller.data().diagrams.first().nodes.first().id;
  const auto node = [&]() {
    return findNode(controller.data().diagrams.first(), nodeId);
  };

  QCOMPARE(node()->horizontalPortSnapPoints,
           connector_ports::kDefaultSnapPointCount);
  QCOMPARE(node()->verticalPortSnapPoints,
           connector_ports::kDefaultSnapPointCount);
  controller.setNodePortSnapPoints(diagramId, nodeId, 3, 5);
  QCOMPARE(node()->horizontalPortSnapPoints, 3);
  QCOMPARE(node()->verticalPortSnapPoints, 5);
  QCOMPARE(controller.undoText(),
           QStringLiteral("Change connector snap points"));

  controller.undo();
  QCOMPARE(node()->horizontalPortSnapPoints, 1);
  QCOMPARE(node()->verticalPortSnapPoints, 1);
  controller.redo();
  QCOMPARE(node()->horizontalPortSnapPoints, 3);
  QCOMPARE(node()->verticalPortSnapPoints, 5);

  // Even external values cannot create a layout without a center point.
  controller.setNodePortSnapPoints(diagramId, nodeId, 4, 100);
  QCOMPARE(node()->horizontalPortSnapPoints, 5);
  QCOMPARE(node()->verticalPortSnapPoints,
           connector_ports::kMaximumSnapPointCount);
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
      QStringLiteral("aggregation"),    QStringLiteral("composition"),
      QStringLiteral("containment")};
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
    QCOMPARE(settings.diagramItemSizingMode(), QStringLiteral("content"));
    QCOMPARE(settings.defaultConnectorRouting(), QStringLiteral("straight"));
    QCOMPARE(settings.cppInterfacePattern(),
             ApplicationSettings::defaultCppInterfacePattern());
    QCOMPARE(settings.cppOwningPointerTypes(),
             ApplicationSettings::defaultCppOwningPointerTypes());
    QCOMPARE(settings.cppSharedPointerTypes(),
             ApplicationSettings::defaultCppSharedPointerTypes());
    QCOMPARE(settings.packageReassignmentPolicy(), QStringLiteral("ask"));
    QCOMPARE(settings.relationshipGestureKeys(),
             ApplicationSettings::defaultRelationshipGestureKeys());
    QSignalSpy changes(&settings,
                       &ApplicationSettings::defaultDistributionGapChanged);
    settings.setDefaultDistributionGap(24);
    settings.setSnapToGridEnabled(false);
    settings.setAlignmentGuidesEnabled(false);
    settings.setGridSpacing(35);
    settings.setDiagramItemSizingMode(QStringLiteral("fixed"));
    settings.setDefaultConnectorRouting(QStringLiteral("orthogonal"));
    settings.setPackageReassignmentPolicy(QStringLiteral("allow"));
    QVERIFY(settings.setCppInterfacePattern(QStringLiteral("^Abstract.*$")));
    QVERIFY(!settings.setCppInterfacePattern(QStringLiteral("[")));
    QCOMPARE(settings.cppInterfacePattern(), QStringLiteral("^Abstract.*$"));
    settings.setCppPointerTypes(
        {QStringLiteral(" custom::Owner<> "), QStringLiteral("custom::Owner")},
        {QStringLiteral("custom::Shared"), QStringLiteral("custom::Owner")});
    QCOMPARE(settings.cppOwningPointerTypes(),
             QStringList({QStringLiteral("custom::Owner")}));
    QCOMPARE(settings.cppSharedPointerTypes(),
             QStringList({QStringLiteral("custom::Shared")}));
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
    QCOMPARE(restored.diagramItemSizingMode(), QStringLiteral("fixed"));
    QCOMPARE(restored.defaultConnectorRouting(), QStringLiteral("orthogonal"));
    QCOMPARE(restored.cppInterfacePattern(), QStringLiteral("^Abstract.*$"));
    QCOMPARE(restored.cppOwningPointerTypes(),
             QStringList({QStringLiteral("custom::Owner")}));
    QCOMPARE(restored.cppSharedPointerTypes(),
             QStringList({QStringLiteral("custom::Shared")}));
    QCOMPARE(restored.packageReassignmentPolicy(), QStringLiteral("allow"));
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
    restored.setDiagramItemSizingMode(QStringLiteral("invalid"));
    restored.setPackageReassignmentPolicy(QStringLiteral("invalid"));
    QCOMPARE(restored.defaultDistributionGap(),
             ApplicationSettings::kMinimumDistributionGap);
    QCOMPARE(restored.gridSpacing(), ApplicationSettings::kMinimumGridSpacing);
    QCOMPARE(restored.diagramItemSizingMode(), QStringLiteral("content"));
    QCOMPARE(restored.packageReassignmentPolicy(), QStringLiteral("ask"));
    restored.resetDefaults();
    QCOMPARE(restored.defaultDistributionGap(),
             ApplicationSettings::kDefaultDistributionGap);
    QCOMPARE(restored.snapToGridEnabled(),
             ApplicationSettings::kDefaultSnapToGridEnabled);
    QCOMPARE(restored.alignmentGuidesEnabled(),
             ApplicationSettings::kDefaultAlignmentGuidesEnabled);
    QCOMPARE(restored.gridSpacing(), ApplicationSettings::kDefaultGridSpacing);
    QCOMPARE(restored.diagramItemSizingMode(), QStringLiteral("content"));
    QCOMPARE(restored.defaultConnectorRouting(), QStringLiteral("straight"));
    QCOMPARE(restored.cppInterfacePattern(),
             ApplicationSettings::defaultCppInterfacePattern());
    QCOMPARE(restored.cppOwningPointerTypes(),
             ApplicationSettings::defaultCppOwningPointerTypes());
    QCOMPARE(restored.cppSharedPointerTypes(),
             ApplicationSettings::defaultCppSharedPointerTypes());
    QCOMPARE(restored.packageReassignmentPolicy(), QStringLiteral("ask"));
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
