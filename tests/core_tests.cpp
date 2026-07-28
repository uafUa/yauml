#include "core/application_settings.h"
#include "core/connector_port_layout.h"
#include "core/cpp_import.h"
#include "core/cpp_import_matching.h"
#include "core/diagram_filter.h"
#include "core/json5.h"
#include "core/presentation_layout.h"
#include "core/project_controller.h"
#include "core/project_serializer.h"
#include "core/project_serializer_test_support.h"
#include "core/project_style.h"
#include "core/stereotype_catalog.h"
#include "core/update_manifest.h"
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
  void stereotypeCatalogSeedMigrationAndEmptyPersistence();
  void invalidProjectSchemaVersionsAreRejected();
  void projectDiagramStylesPersistResolveAndUndo();
  void cppSynchronizationSourcePersistsAndIsUndoable();
  void saveAsRequiresExplicitProjectReplacement();
  void externalProjectChangesRequireExplicitOverwrite();
  void validationFindsBrokenReferences();
  void commandUndoRedo();
  void cppRenameMatchingRequiresUniqueHighConfidence();
  void cppImportPreservesIdsAcrossRenamesAndMoves();
  void cppImportUsesClangAndProtectsUserEdits();
  void cppImportAssignsLocalStereotypeToImplementationTypes();
  void cppInterfacePatternClassifiesRealization();
  void cppImportClassifiesMemberOwnershipAndDependencies();
  void cppImportCreatesNestedTypeContainment();
  void cppImportScansSourceFolderWithoutBuildMetadata();
  void cppImportCombinesSelectedSourceFolders();
  void largeModelGeometryCommandUndoRedo();
  void bulkDiagramPlacementIsOneUndoableCommand();
  void diagramCompartmentVisibilityPersistsAndIsUndoable();
  void diagramFiltersPersistMatchAndUndo();
  void relatedTypeNeighborhoodActionsAreDirectionalAndUndoable();
  void diagramDropSizingModes();
  void diagramLabelsReflectPackageContext();
  void projectTreeExtendedSelection();
  void projectTreeIncrementalWildcardSearch();
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
  void connectorAnnotationPlacementsPersistAndAreUndoable();
  void stereotypeCatalogAssignmentsPersistAndAreUndoable();
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
  void updateManifestValidationAndVersionComparison();
  void applicationPreferencesMigrateLegacyCppPointerTypes();
  void recentProjectHistoryPersists();
  void themePreferencesPersistAndReset();
  void interruptedSaveRecovery();
  void invalidSaveRecoveryIsNonDestructive();
  void saveFaultBoundariesRemainRecoverable();
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

    const auto verifyAssignedIconExists = [&](const QString &field,
                                              bool required) {
      const QJsonValue value = entry.value(field);
      QVERIFY2(!required || value.isString(),
               qPrintable(node.key() + QStringLiteral(" needs a ") + field +
                          QStringLiteral(" field")));
      if (!value.isString() || value.toString().isEmpty())
        return;
      const QString svg = value.toString();
      QVERIFY2(QFileInfo::exists(QFileInfo(catalogPath).dir().filePath(svg)),
               qPrintable(node.key() + u'.' + field +
                          QStringLiteral(" references missing ") + svg));
    };
    verifyAssignedIconExists(QStringLiteral("svg"), true);
    verifyAssignedIconExists(QStringLiteral("expandedSvg"), false);
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
  StereotypeDefinition stereotype;
  stereotype.id = newId();
  stereotype.name = QStringLiteral("audited");
  stereotype.applicableTo = {QStringLiteral("class"),
                             QStringLiteral("relationship")};
  stereotype.extra.insert(QStringLiteral("futureCatalogField"),
                          QStringLiteral("retained"));
  project.stereotypeDefinitions.append(stereotype);
  element.stereotypeIds = {QStringLiteral("uml.interface"), stereotype.id};
  project.elements.append(element);
  project.modelExtra.insert(QStringLiteral("futureRoot"),
                            QStringLiteral("retained"));
  project.cppImport.sourceRoots = {QStringLiteral("D:/sources/round-trip"),
                                   QStringLiteral("D:/shared/contracts")};
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
  relationship.stereotypeIds = {QStringLiteral("uml.trace"), stereotype.id};
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
  ConnectorAnnotationPlacement namePlacement;
  namePlacement.routePosition = 0.4;
  namePlacement.tangentOffset = 8.0;
  namePlacement.normalOffset = -14.0;
  namePlacement.extra.insert(QStringLiteral("futurePlacementField"), true);
  connector.annotationPlacements.insert(QStringLiteral("name"), namePlacement);
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
  QVERIFY(manifestText.contains("sourceRoots:"));

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
                               QStringLiteral("version 0 to %1")
                                   .arg(kCurrentProjectSchemaVersion));
                  }));

  // Migration is deliberately in-memory until an explicit save. Once saved,
  // the canonical schema marker is persisted and no migration repeats.
  QVERIFY(ProjectSerializer::save(temporary.path(), migrated.project).ok);
  const LoadOutcome canonical = ProjectSerializer::load(temporary.path());
  QVERIFY(canonical.ok);
  QVERIFY(!canonical.migrated);
  QCOMPARE(canonical.project, migrated.project);
}

void CoreTests::stereotypeCatalogSeedMigrationAndEmptyPersistence() {
  QTemporaryDir legacyDirectory;
  QVERIFY(legacyDirectory.isValid());

  ProjectData legacy = createStarterProject(QStringLiteral("Schema 1 catalog"));
  StereotypeDefinition custom;
  custom.id = newId();
  custom.name = QStringLiteral("audited");
  custom.applicableTo = {QStringLiteral("class")};
  legacy.stereotypeDefinitions.append(custom);
  ModelElement element;
  element.id = newId();
  element.name = QStringLiteral("Service");
  element.stereotypeIds = {QStringLiteral("uml.interface"), custom.id};
  legacy.elements.append(element);
  QVERIFY(ProjectSerializer::save(legacyDirectory.path(), legacy).ok);

  const QString manifestPath =
      QDir(legacyDirectory.path()).filePath(QStringLiteral("manifest.json5"));
  QFile manifestFile(manifestPath);
  QVERIFY(manifestFile.open(QIODevice::ReadOnly));
  const auto parsedManifest = Json5::parse(manifestFile.readAll());
  manifestFile.close();
  QVERIFY2(parsedManifest, qPrintable(parsedManifest.error));
  QJsonObject manifest = parsedManifest.document.object();
  manifest.insert(QStringLiteral("schemaVersion"), 1);
  writeTestFile(manifestPath,
                Json5::serialize(QJsonDocument(std::move(manifest))));

  // Schema 1 stored only definitions created by the user; conventional UML
  // definitions were supplied by the application at runtime.
  const QString modelPath = QDir(legacyDirectory.path())
                                .filePath(QStringLiteral("model/model.json5"));
  QFile modelFile(modelPath);
  QVERIFY(modelFile.open(QIODevice::ReadOnly));
  const auto parsedModel = Json5::parse(modelFile.readAll());
  modelFile.close();
  QVERIFY2(parsedModel, qPrintable(parsedModel.error));
  QJsonObject model = parsedModel.document.object();
  const QJsonArray savedCatalog =
      model.value(QStringLiteral("stereotypes")).toArray();
  QVERIFY(!savedCatalog.isEmpty());
  QJsonArray customOnly;
  for (const QJsonValue &value : savedCatalog) {
    if (value.toObject().value(QStringLiteral("id")).toString() == custom.id)
      customOnly.append(value);
  }
  QCOMPARE(customOnly.size(), 1);
  model.insert(QStringLiteral("stereotypes"), customOnly);
  writeTestFile(modelPath, Json5::serialize(QJsonDocument(std::move(model))));

  const LoadOutcome migrated = ProjectSerializer::load(legacyDirectory.path());
  QVERIFY(migrated.ok);
  QVERIFY(migrated.migrated);
  QCOMPARE(migrated.project.schemaVersion, kCurrentProjectSchemaVersion);
  QCOMPARE(migrated.project.stereotypeDefinitions.size(),
           stereotype_catalog::defaultDefinitions().size() + 1);
  QVERIFY(findStereotypeDefinition(migrated.project,
                                   QStringLiteral("uml.interface")));
  QVERIFY(findStereotypeDefinition(migrated.project, custom.id));
  QCOMPARE(findElement(migrated.project, element.id)->stereotypeIds,
           element.stereotypeIds);

  // Schema 3 adds source-visibility conventions without duplicating an
  // existing same-named custom stereotype or restoring unrelated definitions.
  QTemporaryDir schemaTwoDirectory;
  QVERIFY(schemaTwoDirectory.isValid());
  ProjectData schemaTwo =
      createStarterProject(QStringLiteral("Schema 2 catalog"));
  schemaTwo.stereotypeDefinitions.removeIf(
      [](const StereotypeDefinition &definition) {
        return definition.id == stereotype_catalog::kLocalStereotypeId ||
               definition.id == stereotype_catalog::kPrivateStereotypeId ||
               definition.id == stereotype_catalog::kApiStereotypeId;
      });
  StereotypeDefinition customLocal;
  customLocal.id = newId();
  customLocal.name = QStringLiteral("LOCAL");
  customLocal.applicableTo = {QStringLiteral("class")};
  schemaTwo.stereotypeDefinitions.append(customLocal);
  QVERIFY(ProjectSerializer::save(schemaTwoDirectory.path(), schemaTwo).ok);

  const QString schemaTwoManifestPath =
      QDir(schemaTwoDirectory.path())
          .filePath(QStringLiteral("manifest.json5"));
  QFile schemaTwoManifestFile(schemaTwoManifestPath);
  QVERIFY(schemaTwoManifestFile.open(QIODevice::ReadOnly));
  const auto parsedSchemaTwoManifest =
      Json5::parse(schemaTwoManifestFile.readAll());
  schemaTwoManifestFile.close();
  QVERIFY2(parsedSchemaTwoManifest, qPrintable(parsedSchemaTwoManifest.error));
  QJsonObject schemaTwoManifest = parsedSchemaTwoManifest.document.object();
  schemaTwoManifest.insert(QStringLiteral("schemaVersion"), 2);
  writeTestFile(schemaTwoManifestPath,
                Json5::serialize(QJsonDocument(std::move(schemaTwoManifest))));

  const LoadOutcome schemaThree =
      ProjectSerializer::load(schemaTwoDirectory.path());
  QVERIFY(schemaThree.ok);
  QVERIFY(schemaThree.migrated);
  QCOMPARE(schemaThree.project.schemaVersion, kCurrentProjectSchemaVersion);
  QCOMPARE(schemaThree.project.stereotypeDefinitions.size(),
           stereotype_catalog::defaultDefinitions().size());
  QVERIFY(findStereotypeDefinition(schemaThree.project, customLocal.id));
  QVERIFY(!findStereotypeDefinition(schemaThree.project,
                                    stereotype_catalog::kLocalStereotypeId));
  QVERIFY(findStereotypeDefinition(schemaThree.project,
                                   stereotype_catalog::kPrivateStereotypeId));
  QVERIFY(findStereotypeDefinition(schemaThree.project,
                                   stereotype_catalog::kApiStereotypeId));
  CppImportOptions migratedOptions;
  configureCppImportStereotypes(migratedOptions, schemaThree.project);
  QCOMPARE(migratedOptions.localTypeStereotypeId, customLocal.id);
  QCOMPARE(migratedOptions.localTypeStereotypeApplicableTo,
           customLocal.applicableTo);

  // In the current schema an empty array is intentional and must not be
  // repopulated on every load after a user removes all seeded definitions.
  QTemporaryDir emptyDirectory;
  QVERIFY(emptyDirectory.isValid());
  ProjectData empty = createStarterProject(QStringLiteral("Empty catalog"));
  empty.stereotypeDefinitions.clear();
  QVERIFY(ProjectSerializer::save(emptyDirectory.path(), empty).ok);
  const LoadOutcome emptyReloaded =
      ProjectSerializer::load(emptyDirectory.path());
  QVERIFY(emptyReloaded.ok);
  QVERIFY(!emptyReloaded.migrated);
  QVERIFY(emptyReloaded.project.stereotypeDefinitions.isEmpty());
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
  preview.sourceRoots = {QStringLiteral("D:/sources/configured"),
                         QStringLiteral("D:/shared/contracts")};

  // Configuring a source is a real project edit even when the source and model
  // are already in sync and there are no semantic records to apply.
  QCOMPARE(controller.applyCppImportPlan(preview), 0);
  QCOMPARE(controller.data().cppImport.sourceRoots, preview.sourceRoots);
  QVERIFY(controller.canUndo());
  QCOMPARE(controller.undoText(),
           QStringLiteral("Configure C++ synchronization"));

  controller.undo();
  QVERIFY(controller.data().cppImport.sourceRoots.isEmpty());
  controller.redo();
  QCOMPARE(controller.data().cppImport.sourceRoots, preview.sourceRoots);

  // Loading projects saved by the earlier single-root implementation remains
  // supported and canonicalizes the setting on the next save.
  QTemporaryDir legacyDirectory;
  QVERIFY(legacyDirectory.isValid());
  QVERIFY(
      ProjectSerializer::save(legacyDirectory.path(), controller.data()).ok);
  const QString manifestPath =
      QDir(legacyDirectory.path()).filePath(QStringLiteral("manifest.json5"));
  QFile manifestFile(manifestPath);
  QVERIFY(manifestFile.open(QIODevice::ReadOnly));
  const auto parsedManifest = Json5::parse(manifestFile.readAll());
  manifestFile.close();
  QVERIFY2(parsedManifest, qPrintable(parsedManifest.error));
  QJsonObject manifest = parsedManifest.document.object();
  QJsonObject cppImport =
      manifest.value(QStringLiteral("cppImport")).toObject();
  cppImport.remove(QStringLiteral("sourceRoots"));
  cppImport.insert(QStringLiteral("sourceRoot"),
                   QStringLiteral("D:/legacy/source"));
  manifest.insert(QStringLiteral("cppImport"), cppImport);
  writeTestFile(manifestPath,
                Json5::serialize(QJsonDocument(std::move(manifest))));
  const LoadOutcome legacy = ProjectSerializer::load(legacyDirectory.path());
  QVERIFY(legacy.ok);
  QCOMPARE(legacy.project.cppImport.sourceRoots,
           QStringList{QStringLiteral("D:/legacy/source")});
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

void CoreTests::externalProjectChangesRequireExplicitOverwrite() {
  QTemporaryDir projectDirectory;
  QTemporaryDir saveAsDirectory;
  QVERIFY(projectDirectory.isValid());
  QVERIFY(saveAsDirectory.isValid());

  ProjectData original =
      createStarterProject(QStringLiteral("External change guard"));
  QVERIFY(ProjectSerializer::save(projectDirectory.path(), original).ok);
  const LoadOutcome loaded = ProjectSerializer::load(projectDirectory.path());
  QVERIFY(loaded.ok);
  QVERIFY(loaded.revision.isValid());

  const QString modelPath = QDir(projectDirectory.path())
                                .filePath(QStringLiteral("model/model.json5"));
  QFile externalModelEdit(modelPath);
  QVERIFY(externalModelEdit.open(QIODevice::Append));
  QCOMPARE(externalModelEdit.write("\n  \n"), 4);
  externalModelEdit.close();
  QFile preservedModel(modelPath);
  QVERIFY(preservedModel.open(QIODevice::ReadOnly));
  const QByteArray externallyEditedBytes = preservedModel.readAll();

  ProjectData edited = loaded.project;
  edited.name = QStringLiteral("Saved after explicit decision");
  const SaveOutcome guarded = ProjectSerializer::save(
      projectDirectory.path(), edited, loaded.revision);
  QVERIFY(!guarded.ok);
  QVERIFY(guarded.externalChangesDetected);
  QCOMPARE(guarded.externallyChangedFiles,
           QStringList{QStringLiteral("model/model.json5")});
  preservedModel.close();
  QVERIFY(preservedModel.open(QIODevice::ReadOnly));
  QCOMPARE(preservedModel.readAll(), externallyEditedBytes);
  preservedModel.close();

  // An expected revision belongs only to its original directory. Save As is a
  // new destination and must not inherit a false conflict from the old path.
  const SaveOutcome savedAs = ProjectSerializer::save(
      saveAsDirectory.path(), edited, loaded.revision);
  QVERIFY(savedAs.ok);
  QVERIFY(!savedAs.externalChangesDetected);

  const SaveOutcome overwritten = ProjectSerializer::save(
      projectDirectory.path(), edited, loaded.revision, true);
  QVERIFY(overwritten.ok);
  QVERIFY(overwritten.revision.isValid());
  const LoadOutcome afterOverwrite =
      ProjectSerializer::load(projectDirectory.path());
  QVERIFY(afterOverwrite.ok);
  QCOMPARE(afterOverwrite.project.name, edited.name);

  ProjectController controller;
  QVERIFY(controller.openProject(
      QUrl::fromLocalFile(projectDirectory.path())));
  controller.renameDiagram(controller.data().diagrams.first().id,
                           QStringLiteral("Locally renamed diagram"));
  QVERIFY(controller.dirty());

  const QString diagramsPath =
      QDir(projectDirectory.path())
          .filePath(QStringLiteral("diagrams/diagrams.json5"));
  QFile externalDiagramEdit(diagramsPath);
  QVERIFY(externalDiagramEdit.open(QIODevice::Append));
  QCOMPARE(externalDiagramEdit.write("\n"), 1);
  externalDiagramEdit.close();

  QSignalSpy conflictDetected(
      &controller, &ProjectController::externalProjectChangeDetected);
  QVERIFY(!controller.saveProject());
  QCOMPARE(conflictDetected.count(), 1);
  QCOMPARE(controller.externallyChangedProjectFiles(),
           QStringList{QStringLiteral("diagrams/diagrams.json5")});
  QVERIFY(controller.dirty());

  QVERIFY(controller.overwriteExternallyChangedProject());
  QVERIFY(!controller.dirty());
  QVERIFY(controller.externallyChangedProjectFiles().isEmpty());
  QCOMPARE(ProjectSerializer::load(projectDirectory.path())
               .project.diagrams.first()
               .name,
           QStringLiteral("Locally renamed diagram"));

  controller.renameDiagram(controller.data().diagrams.first().id,
                           QStringLiteral("Unsaved local diagram"));
  ProjectData externalVersion =
      ProjectSerializer::load(projectDirectory.path()).project;
  externalVersion.diagrams.first().name =
      QStringLiteral("Externally renamed diagram");
  QVERIFY(
      ProjectSerializer::save(projectDirectory.path(), externalVersion).ok);
  QVERIFY(!controller.saveProject());
  QVERIFY(controller.dirty());
  QVERIFY(controller.reloadProjectFromDisk());
  QVERIFY(!controller.dirty());
  QCOMPARE(controller.data().diagrams.first().name,
           QStringLiteral("Externally renamed diagram"));
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

void CoreTests::cppRenameMatchingRequiresUniqueHighConfidence() {
  CppImportedDeclaration previous;
  previous.elementId = QStringLiteral("element-old");
  previous.symbolId = QStringLiteral("symbol-old");
  previous.qualifiedName = QStringLiteral("old_space::Service");
  previous.filePath = QStringLiteral("C:/repo/service.hpp");
  previous.line = 20;
  previous.elementType = ElementType::Class;
  previous.attributes = {QStringLiteral("- state: int")};
  previous.operations = {QStringLiteral("+ run(): void")};

  CppSourceSymbol renamed;
  renamed.symbolId = QStringLiteral("symbol-new");
  renamed.qualifiedName = QStringLiteral("new_space::RenamedService");
  renamed.filePath = previous.filePath;
  renamed.line = previous.line;
  renamed.elementType = previous.elementType;
  renamed.attributes = previous.attributes;
  renamed.operations = previous.operations;

  QList<CppDeclarationMatch> matches =
      matchRenamedCppDeclarations({renamed}, {previous});
  QCOMPARE(matches.size(), 1);
  QCOMPARE(matches.first().sourceSymbolId, renamed.symbolId);
  QCOMPARE(matches.first().previousSymbolId, previous.symbolId);
  QCOMPARE(matches.first().elementId, previous.elementId);

  CppSourceSymbol namespaceMoved = renamed;
  namespaceMoved.symbolId = QStringLiteral("symbol-namespace-moved");
  namespaceMoved.qualifiedName = QStringLiteral("new_space::Service");
  namespaceMoved.line += 3;
  matches = matchRenamedCppDeclarations({namespaceMoved}, {previous});
  QCOMPARE(matches.size(), 1);

  CppSourceSymbol fileMoved = renamed;
  fileMoved.symbolId = QStringLiteral("symbol-file-moved");
  fileMoved.qualifiedName = previous.qualifiedName;
  fileMoved.filePath = QStringLiteral("C:/repo/public/service.hpp");
  matches = matchRenamedCppDeclarations({fileMoved}, {previous});
  QCOMPARE(matches.size(), 1);

  CppImportedDeclaration equallyLikely = previous;
  equallyLikely.elementId = QStringLiteral("element-other");
  equallyLikely.symbolId = QStringLiteral("symbol-other");
  matches = matchRenamedCppDeclarations({renamed}, {previous, equallyLikely});
  QVERIFY(matches.isEmpty());

  CppSourceSymbol wrongKind = renamed;
  wrongKind.elementType = ElementType::Struct;
  matches = matchRenamedCppDeclarations({wrongKind}, {previous});
  QVERIFY(matches.isEmpty());

  CppImportedDeclaration weakPrevious = previous;
  weakPrevious.attributes.clear();
  weakPrevious.operations.clear();
  CppSourceSymbol weakMove = fileMoved;
  weakMove.attributes.clear();
  weakMove.operations.clear();
  matches = matchRenamedCppDeclarations({weakMove}, {weakPrevious});
  QVERIFY(matches.isEmpty());

  CppSourceSymbol locationOnly = renamed;
  locationOnly.attributes.clear();
  locationOnly.operations.clear();
  locationOnly.qualifiedName = QStringLiteral("unrelated::Replacement");
  matches = matchRenamedCppDeclarations({locationOnly}, {weakPrevious});
  QVERIFY(matches.isEmpty());
}

void CoreTests::cppImportPreservesIdsAcrossRenamesAndMoves() {
  if (!CppImportService::available())
    QSKIP("This build was configured without libclang");

  QTemporaryDir sourceDirectory;
  QVERIFY(sourceDirectory.isValid());
  const QString sourcePath =
      sourceDirectory.filePath(QStringLiteral("renames.cpp"));
  const auto writeSource = [&](const QByteArray &namespaceName,
                               const QByteArray &baseName,
                               const QByteArray &derivedName) {
    writeTestFile(sourcePath, QByteArray("namespace ") + namespaceName +
                                  QByteArray(" {\n"
                                             "class ") +
                                  baseName +
                                  QByteArray(" {\n"
                                             "public:\n"
                                             "    virtual void run();\n"
                                             "};\n"
                                             "class ") +
                                  derivedName + QByteArray(" : public ") +
                                  baseName +
                                  QByteArray(" {\n"
                                             "public:\n"
                                             "    void work();\n"
                                             "};\n"
                                             "}\n"));
  };
  writeSource("old_space", "Base", "Worker");

  QJsonObject command;
  command.insert(QStringLiteral("directory"), sourceDirectory.path());
  command.insert(QStringLiteral("file"), sourcePath);
  command.insert(QStringLiteral("arguments"),
                 QJsonArray{QStringLiteral("clang++"),
                            QStringLiteral("-std=c++20"), sourcePath});
  writeTestFile(
      sourceDirectory.filePath(QStringLiteral("compile_commands.json")),
      QJsonDocument(QJsonArray{command}).toJson(QJsonDocument::Indented));

  ProjectController controller;
  CppImportPreview initial = CppImportService::preview(
      sourceDirectory.path(), controller.data().elements,
      controller.data().relationships);
  QVERIFY(initial.ok);
  QCOMPARE(controller.applyCppImportPlan(initial), 4);

  const auto importedElement = [&](const QString &name) {
    return std::find_if(
        controller.data().elements.cbegin(), controller.data().elements.cend(),
        [&](const ModelElement &element) { return element.name == name; });
  };
  const auto oldBase = importedElement(QStringLiteral("old_space::Base"));
  const auto oldWorker = importedElement(QStringLiteral("old_space::Worker"));
  const auto oldPackage = importedElement(QStringLiteral("old_space"));
  QVERIFY(oldBase != controller.data().elements.cend());
  QVERIFY(oldWorker != controller.data().elements.cend());
  QVERIFY(oldPackage != controller.data().elements.cend());
  const QString baseId = oldBase->id;
  const QString workerId = oldWorker->id;
  const QString packageId = oldPackage->id;

  const QString diagramId = controller.data().diagrams.first().id;
  QCOMPARE(controller.addElementsToDiagram(diagramId, {baseId, workerId}, 40.0,
                                           40.0),
           2);
  QCOMPARE(controller.data().diagrams.first().connectors.size(), 1);
  QSet<QString> relationshipIds;
  for (const auto &relationship : controller.data().relationships)
    relationshipIds.insert(relationship.id);
  QSet<QString> connectorIds;
  for (const auto &connector : controller.data().diagrams.first().connectors)
    connectorIds.insert(connector.id);

  writeSource("new_space", "Foundation", "Agent");

  // A simultaneous user edit and source rename is still a normal explicit
  // three-way conflict; matching does not weaken user authority.
  controller.editText(workerId, QStringLiteral("name"), -1,
                      QStringLiteral("Manually named worker"));
  const CppImportPreview conflictPreview = CppImportService::preview(
      sourceDirectory.path(), controller.data().elements,
      controller.data().relationships);
  const auto conflictedAgent = std::find_if(
      conflictPreview.items.cbegin(), conflictPreview.items.cend(),
      [](const CppImportItem &item) {
        return item.symbol.qualifiedName == QStringLiteral("new_space::Agent");
      });
  QVERIFY(conflictedAgent != conflictPreview.items.cend());
  QVERIFY(conflictedAgent->action == CppImportAction::Conflict);
  QVERIFY(conflictedAgent->isResolvableConflict());
  QCOMPARE(conflictedAgent->existingElementId, workerId);
  controller.undo();

  ProjectData packageConflictProject = controller.data();
  auto manuallyNamedPackage =
      std::find_if(packageConflictProject.elements.begin(),
                   packageConflictProject.elements.end(),
                   [&](const ModelElement &element) {
                     return element.id == packageId;
                   });
  QVERIFY(manuallyNamedPackage != packageConflictProject.elements.end());
  manuallyNamedPackage->name = QStringLiteral("Manually named package");
  const CppImportPreview packageConflict = CppImportService::preview(
      sourceDirectory.path(), packageConflictProject.elements,
      packageConflictProject.relationships);
  const auto conflictedPackage = std::find_if(
      packageConflict.items.cbegin(), packageConflict.items.cend(),
      [](const CppImportItem &item) {
        return item.symbol.qualifiedName == QStringLiteral("new_space");
      });
  QVERIFY(conflictedPackage != packageConflict.items.cend());
  QVERIFY(conflictedPackage->action == CppImportAction::Conflict);
  QCOMPARE(conflictedPackage->existingElementId, packageId);

  // Applying the independently safe type updates must not make a deferred
  // package conflict lose its inferred identity on the next preview.
  QVERIFY(CppImportService::apply(packageConflictProject, packageConflict) > 0);
  const CppImportPreview repeatedPackageConflict = CppImportService::preview(
      sourceDirectory.path(), packageConflictProject.elements,
      packageConflictProject.relationships);
  const auto repeatedPackage = std::find_if(
      repeatedPackageConflict.items.cbegin(),
      repeatedPackageConflict.items.cend(), [](const CppImportItem &item) {
        return item.symbol.qualifiedName == QStringLiteral("new_space");
      });
  QVERIFY(repeatedPackage != repeatedPackageConflict.items.cend());
  QVERIFY(repeatedPackage->action == CppImportAction::Conflict);
  QCOMPARE(repeatedPackage->existingElementId, packageId);

  const ProjectData beforeRename = controller.data();
  const CppImportPreview renamed = CppImportService::preview(
      sourceDirectory.path(), controller.data().elements,
      controller.data().relationships);
  QVERIFY(renamed.ok);
  const auto renamedItem = [&](const QString &name) {
    return std::find_if(renamed.items.cbegin(), renamed.items.cend(),
                        [&](const CppImportItem &item) {
                          return item.symbol.qualifiedName == name;
                        });
  };
  const auto renamedBase = renamedItem(QStringLiteral("new_space::Foundation"));
  const auto renamedWorker = renamedItem(QStringLiteral("new_space::Agent"));
  const auto renamedPackage = renamedItem(QStringLiteral("new_space"));
  QVERIFY(renamedBase != renamed.items.cend());
  QVERIFY(renamedWorker != renamed.items.cend());
  QVERIFY(renamedPackage != renamed.items.cend());
  QVERIFY(renamedBase->action == CppImportAction::Update);
  QVERIFY(renamedWorker->action == CppImportAction::Update);
  QVERIFY(renamedPackage->action == CppImportAction::Update);
  QCOMPARE(renamedBase->existingElementId, baseId);
  QCOMPARE(renamedWorker->existingElementId, workerId);
  QCOMPARE(renamedPackage->existingElementId, packageId);
  QCOMPARE(renamedBase->desiredElement.id, baseId);
  QCOMPARE(renamedWorker->desiredElement.id, workerId);
  QCOMPARE(renamedPackage->desiredElement.id, packageId);
  QVERIFY(std::none_of(renamed.items.cbegin(), renamed.items.cend(),
                       [](const CppImportItem &item) {
                         return item.action == CppImportAction::MissingSource &&
                                (item.symbol.qualifiedName ==
                                     QStringLiteral("old_space::Base") ||
                                 item.symbol.qualifiedName ==
                                     QStringLiteral("old_space::Worker"));
                       }));

  QCOMPARE(renamed.relationshipItems.size(), 1);
  for (const auto &item : renamed.relationshipItems) {
    QVERIFY(item.action == CppImportAction::Update);
    QVERIFY(relationshipIds.contains(item.existingRelationshipId));
    QCOMPARE(item.desiredRelationship.id, item.existingRelationshipId);
  }

  QCOMPARE(controller.applyCppImportPlan(renamed), 4);
  const auto newBase = importedElement(QStringLiteral("new_space::Foundation"));
  const auto newWorker = importedElement(QStringLiteral("new_space::Agent"));
  const auto newPackage = importedElement(QStringLiteral("new_space"));
  QVERIFY(newBase != controller.data().elements.cend());
  QVERIFY(newWorker != controller.data().elements.cend());
  QVERIFY(newPackage != controller.data().elements.cend());
  QCOMPARE(newBase->id, baseId);
  QCOMPARE(newWorker->id, workerId);
  QCOMPARE(newPackage->id, packageId);
  QCOMPARE(controller.data().elements.size(), 3);
  QCOMPARE(newBase->extra.value(QStringLiteral("sourceBinding"))
               .toObject()
               .value(QStringLiteral("symbolId"))
               .toString(),
           renamedBase->symbol.symbolId);
  QSet<QString> updatedRelationshipIds;
  for (const auto &relationship : controller.data().relationships)
    updatedRelationshipIds.insert(relationship.id);
  QCOMPARE(updatedRelationshipIds, relationshipIds);
  QCOMPARE(controller.data()
               .relationships.first()
               .extra.value(QStringLiteral("sourceBinding"))
               .toObject()
               .value(QStringLiteral("symbolId"))
               .toString(),
           renamed.relationshipItems.first().source.symbolId);
  QSet<QString> updatedConnectorIds;
  for (const auto &connector : controller.data().diagrams.first().connectors)
    updatedConnectorIds.insert(connector.id);
  QCOMPARE(updatedConnectorIds, connectorIds);

  const CppImportPreview synchronized = CppImportService::preview(
      sourceDirectory.path(), controller.data().elements,
      controller.data().relationships);
  QCOMPARE(synchronized.applicableCount(), 0);
  QCOMPARE(synchronized.conflictCount(), 0);
  const auto synchronizedAgent = std::find_if(
      synchronized.items.cbegin(), synchronized.items.cend(),
      [](const CppImportItem &item) {
        return item.symbol.qualifiedName == QStringLiteral("new_space::Agent");
      });
  QVERIFY(synchronizedAgent != synchronized.items.cend());
  QVERIFY(synchronizedAgent->action == CppImportAction::Unchanged);
  QCOMPARE(synchronized.relationshipItems.size(), 1);
  QVERIFY(synchronized.relationshipItems.first().action ==
          CppImportAction::Unchanged);

  controller.undo();
  QCOMPARE(controller.data(), beforeRename);
  controller.redo();
  const auto redoneBase =
      importedElement(QStringLiteral("new_space::Foundation"));
  const auto redoneWorker = importedElement(QStringLiteral("new_space::Agent"));
  QVERIFY(redoneBase != controller.data().elements.cend());
  QVERIFY(redoneWorker != controller.data().elements.cend());
  QCOMPARE(redoneBase->id, baseId);
  QCOMPARE(redoneWorker->id, workerId);
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
  QCOMPARE(imported.cppImport.sourceRoots, QStringList{initial.sourceRoot});
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
  QVERIFY(changedPoint->isResolvableConflict());
  QCOMPARE(changed.resolvableConflictCount(), 1);
  QCOMPARE(changed.resolvedConflictCount(), 0);
  QCOMPARE(changed.unresolvedConflictCount(), 1);
  CppImportPreview ambiguousPreview;
  CppImportItem ambiguousItem;
  ambiguousItem.action = CppImportAction::Conflict;
  ambiguousItem.symbol.symbolId = QStringLiteral("duplicate-binding");
  ambiguousPreview.items.append(ambiguousItem);
  ambiguousPreview.resolveAllConflicts(
      CppImportConflictResolution::UseSource);
  QCOMPARE(ambiguousPreview.conflictCount(), 1);
  QCOMPARE(ambiguousPreview.resolvableConflictCount(), 0);
  QCOMPARE(ambiguousPreview.resolvedConflictCount(), 0);
  QCOMPARE(ambiguousPreview.applicableCount(), 0);
  QCOMPARE(changed.relationshipItems.size(), 1);
  QVERIFY(changed.relationshipItems.first().action ==
          CppImportAction::UserModified);

  CppImportPreview keepModelPlan = changed;
  QVERIFY(keepModelPlan.setConflictResolution(
      changedPoint->conflictKey(), CppImportConflictResolution::KeepModel));
  QCOMPARE(keepModelPlan.resolvedConflictCount(), 1);
  QCOMPARE(keepModelPlan.unresolvedConflictCount(), 0);
  ProjectData modelWins = imported;
  QCOMPARE(CppImportService::apply(modelWins, keepModelPlan), 2);
  const auto retainedPoint =
      std::find_if(modelWins.elements.cbegin(), modelWins.elements.cend(),
                   [](const ModelElement &element) {
                     return element.name == QStringLiteral("demo::Point");
                   });
  QVERIFY(retainedPoint != modelWins.elements.cend());
  QVERIFY(retainedPoint->attributes.contains(QStringLiteral("+ manual: bool")));
  QVERIFY(!retainedPoint->attributes.contains(QStringLiteral("+ z: int")));
  const CppImportPreview afterKeep = CppImportService::preview(
      sourceDirectory.path(), modelWins.elements, modelWins.relationships);
  QCOMPARE(afterKeep.conflictCount(), 0);
  const auto acknowledgedPoint = std::find_if(
      afterKeep.items.cbegin(), afterKeep.items.cend(),
      [](const CppImportItem &item) {
        return item.symbol.qualifiedName == QStringLiteral("demo::Point");
      });
  QVERIFY(acknowledgedPoint != afterKeep.items.cend());
  QVERIFY(acknowledgedPoint->action == CppImportAction::UserModified);

  CppImportPreview useSourcePlan = changed;
  QVERIFY(useSourcePlan.setConflictResolution(
      changedPoint->conflictKey(), CppImportConflictResolution::UseSource));
  ProjectData sourceWins = imported;
  QCOMPARE(CppImportService::apply(sourceWins, useSourcePlan), 2);
  const auto sourcePoint =
      std::find_if(sourceWins.elements.cbegin(), sourceWins.elements.cend(),
                   [](const ModelElement &element) {
                     return element.name == QStringLiteral("demo::Point");
                   });
  QVERIFY(sourcePoint != sourceWins.elements.cend());
  QVERIFY(sourcePoint->attributes.contains(QStringLiteral("+ z: int")));
  QVERIFY(!sourcePoint->attributes.contains(QStringLiteral("+ manual: bool")));

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

  CppImportOptions realizationOptions;
  realizationOptions.interfacePattern = QStringLiteral("^Service$");
  const CppImportPreview relationshipChanged = CppImportService::preview(
      sourceDirectory.path(), imported.elements, imported.relationships,
      realizationOptions);
  const auto relationshipConflict = std::find_if(
      relationshipChanged.relationshipItems.cbegin(),
      relationshipChanged.relationshipItems.cend(),
      [](const CppRelationshipImportItem &item) {
        return item.action == CppImportAction::Conflict;
      });
  QVERIFY(relationshipConflict != relationshipChanged.relationshipItems.cend());
  QVERIFY(relationshipConflict->isResolvableConflict());
  CppImportPreview relationshipSourcePlan = relationshipChanged;
  QVERIFY(relationshipSourcePlan.setConflictResolution(
      relationshipConflict->conflictKey(),
      CppImportConflictResolution::UseSource));
  ProjectData relationshipSourceWins = imported;
  QCOMPARE(CppImportService::apply(relationshipSourceWins,
                                   relationshipSourcePlan),
           1);
  QCOMPARE(relationshipSourceWins.relationships.first().name, QString{});
  QVERIFY(relationshipSourceWins.relationships.first().type ==
          RelationshipType::Realization);

  QVERIFY(
      ProjectSerializer::save(importedProjectDirectory.path(), imported).ok);
  ProjectController resolutionController;
  QVERIFY(resolutionController.openProject(
      QUrl::fromLocalFile(importedProjectDirectory.path())));
  const ProjectData beforeResolution = resolutionController.data();
  QCOMPARE(resolutionController.applyCppImportPlan(relationshipSourcePlan), 1);
  QVERIFY(resolutionController.data().relationships.first().type ==
          RelationshipType::Realization);
  resolutionController.undo();
  QCOMPARE(resolutionController.data(), beforeResolution);
  resolutionController.redo();
  QVERIFY(resolutionController.data().relationships.first().type ==
          RelationshipType::Realization);

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

void CoreTests::cppImportAssignsLocalStereotypeToImplementationTypes() {
  if (!CppImportService::available())
    QSKIP("This build was configured without libclang");

  QTemporaryDir sourceDirectory;
  QVERIFY(sourceDirectory.isValid());
  const QString headerPath =
      sourceDirectory.filePath(QStringLiteral("public.hpp"));
  const QString sourcePath =
      sourceDirectory.filePath(QStringLiteral("implementation.cpp"));
  writeTestFile(headerPath, QByteArray("#pragma once\n"
                                       "class ApiType {};\n"));
  writeTestFile(sourcePath, QByteArray("#include \"public.hpp\"\n"
                                       "class LocalType {};\n"
                                       "struct LocalStruct {};\n"));

  QJsonObject command;
  command.insert(QStringLiteral("directory"), sourceDirectory.path());
  command.insert(QStringLiteral("file"), sourcePath);
  command.insert(QStringLiteral("arguments"),
                 QJsonArray{QStringLiteral("clang++"),
                            QStringLiteral("-std=c++20"), sourcePath});
  writeTestFile(
      sourceDirectory.filePath(QStringLiteral("compile_commands.json")),
      QJsonDocument(QJsonArray{command}).toJson(QJsonDocument::Indented));

  ProjectController controller;
  CppImportOptions options;
  configureCppImportStereotypes(options, controller.data());
  QCOMPARE(options.localTypeStereotypeId,
           stereotype_catalog::kLocalStereotypeId);

  const CppImportPreview initial = CppImportService::preview(
      sourceDirectory.path(), controller.data().elements,
      controller.data().relationships, options);
  QVERIFY(initial.ok);
  QCOMPARE(initial.elementApplicableCount(), 3);
  const auto desiredType = [&](const QString &name) -> const ModelElement * {
    const auto item =
        std::find_if(initial.items.cbegin(), initial.items.cend(),
                     [&](const CppImportItem &candidate) {
                       return candidate.symbol.qualifiedName == name;
                     });
    return item == initial.items.cend() ? nullptr : &item->desiredElement;
  };
  const ModelElement *apiType = desiredType(QStringLiteral("ApiType"));
  const ModelElement *localType = desiredType(QStringLiteral("LocalType"));
  const ModelElement *localStruct = desiredType(QStringLiteral("LocalStruct"));
  QVERIFY(apiType);
  QVERIFY(localType);
  QVERIFY(localStruct);
  QVERIFY(
      !apiType->stereotypeIds.contains(stereotype_catalog::kLocalStereotypeId));
  QVERIFY(localType->stereotypeIds.contains(
      stereotype_catalog::kLocalStereotypeId));
  QVERIFY(localStruct->stereotypeIds.contains(
      stereotype_catalog::kLocalStereotypeId));

  QCOMPARE(controller.applyCppImportPlan(initial), 3);
  QCOMPARE(controller.undoText(), QStringLiteral("Import C++ changes"));
  controller.undo();
  QVERIFY(controller.data().elements.isEmpty());
  controller.redo();

  const auto importedLocal = std::find_if(
      controller.data().elements.cbegin(), controller.data().elements.cend(),
      [](const ModelElement &element) {
        return element.name == QStringLiteral("LocalType");
      });
  QVERIFY(importedLocal != controller.data().elements.cend());
  const QString localTypeId = importedLocal->id;
  controller.editText(localTypeId, QStringLiteral("name"), -1,
                      QStringLiteral("Manually named local type"));
  controller.assignStereotypes(QStringLiteral("element"), localTypeId,
                               {stereotype_catalog::kLocalStereotypeId,
                                stereotype_catalog::kApiStereotypeId});

  // Moving the declaration to a public header removes only the import-owned
  // local classification. The independently assigned API classification is
  // user-owned and must survive synchronization.
  writeTestFile(headerPath, QByteArray("#pragma once\n"
                                       "class ApiType {};\n"
                                       "class LocalType {};\n"));
  writeTestFile(sourcePath, QByteArray("#include \"public.hpp\"\n"));
  const CppImportPreview moved = CppImportService::preview(
      sourceDirectory.path(), controller.data().elements,
      controller.data().relationships, options);
  QVERIFY(moved.ok);
  const auto movedLocal = std::find_if(
      moved.items.cbegin(), moved.items.cend(), [](const CppImportItem &item) {
        return item.symbol.qualifiedName == QStringLiteral("LocalType");
      });
  QVERIFY(movedLocal != moved.items.cend());
  QVERIFY(movedLocal->action == CppImportAction::Update);
  QVERIFY(!movedLocal->desiredElement.stereotypeIds.contains(
      stereotype_catalog::kLocalStereotypeId));
  QVERIFY(movedLocal->desiredElement.stereotypeIds.contains(
      stereotype_catalog::kApiStereotypeId));
  QCOMPARE(movedLocal->desiredElement.name,
           QStringLiteral("Manually named local type"));

  QCOMPARE(controller.applyCppImportPlan(moved), 1);
  const auto *updatedLocal = findElement(controller.data(), localTypeId);
  QVERIFY(updatedLocal);
  QVERIFY(!updatedLocal->stereotypeIds.contains(
      stereotype_catalog::kLocalStereotypeId));
  QVERIFY(updatedLocal->stereotypeIds.contains(
      stereotype_catalog::kApiStereotypeId));
  QCOMPARE(updatedLocal->name, QStringLiteral("Manually named local type"));
  controller.undo();
  updatedLocal = findElement(controller.data(), localTypeId);
  QVERIFY(updatedLocal->stereotypeIds.contains(
      stereotype_catalog::kLocalStereotypeId));
  QVERIFY(updatedLocal->stereotypeIds.contains(
      stereotype_catalog::kApiStereotypeId));
  controller.redo();
  updatedLocal = findElement(controller.data(), localTypeId);
  QVERIFY(!updatedLocal->stereotypeIds.contains(
      stereotype_catalog::kLocalStereotypeId));
  QVERIFY(updatedLocal->stereotypeIds.contains(
      stereotype_catalog::kApiStereotypeId));
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
  writeTestFile(
      sourcePath,
      QByteArray("namespace rel {\n"
                 "template<class T> class OwnerPtr {};\n"
                 "template<class T> class SharedPtr {};\n"
                 "template<class T> class MysteryPtr {};\n"
                 "template<class T> class Sequence {};\n"
                 "template<class K, class V> class Lookup {};\n"
                 "template<class T, int N> class Fixed {};\n"
                 "class ValuePart {};\n"
                 "class RawPart {};\n"
                 "class ReferencePart {};\n"
                 "class OwnedPart {};\n"
                 "class SharedPart {};\n"
                 "class UnknownPart {};\n"
                 "class UsedPart {};\n"
                 "class ManyPart {};\n"
                 "class MappedPart {};\n"
                 "class FixedPart {};\n"
                 "class NestedSharedPart {};\n"
                 "class DuplicatePart {};\n"
                 "class Consumer {\n"
                 "  ValuePart value;\n"
                 "  RawPart *raw;\n"
                 "  ReferencePart &reference;\n"
                 "  OwnerPtr<OwnedPart> owned;\n"
                 "  SharedPtr<SharedPart> shared;\n"
                 "  MysteryPtr<UnknownPart> unknown;\n"
                 "  Sequence<ManyPart> many;\n"
                 "  Lookup<ValuePart, MappedPart> mapped;\n"
                 "  Fixed<FixedPart, 4> fixed;\n"
                 "  Sequence<SharedPtr<NestedSharedPart>> sharedMany;\n"
                 "  DuplicatePart duplicateA;\n"
                 "  DuplicatePart duplicateB;\n"
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
  options.memberTypeRules = {
      {QStringLiteral("rel::OwnerPtr"), RelationshipType::Composition,
       QStringLiteral("0..1"), 1},
      {QStringLiteral("rel::SharedPtr"), RelationshipType::Aggregation,
       QStringLiteral("0..1"), 1},
      {QStringLiteral("rel::Sequence"), RelationshipType::Composition,
       QStringLiteral("0..*"), 1},
      {QStringLiteral("rel::Lookup"), RelationshipType::Composition,
       QStringLiteral("0..*"), 2},
      {QStringLiteral("rel::Fixed"), RelationshipType::Composition,
       QStringLiteral("{2}"), 1},
  };
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
                              const QString &evidenceKind,
                              const QString &role = {},
                              const QString &multiplicity = {}) {
    const auto relationship = relationshipTo(targetName);
    QVERIFY2(relationship != preview.relationships.cend(),
             qPrintable(
                 QStringLiteral("Missing relationship to %1").arg(targetName)));
    QVERIFY(relationship->relationshipType == expected);
    QCOMPARE(relationship->evidenceKind, evidenceKind);
    QCOMPARE(relationship->sourceRole, role);
    QCOMPARE(relationship->sourceMultiplicity, multiplicity);
    QVERIFY(!relationship->classificationReason.isEmpty());
  };
  verifyType(QStringLiteral("rel::ValuePart"), RelationshipType::Composition,
             QStringLiteral("member"), QStringLiteral("value"),
             QStringLiteral("1"));
  verifyType(QStringLiteral("rel::RawPart"), RelationshipType::Aggregation,
             QStringLiteral("member"), QStringLiteral("raw"),
             QStringLiteral("0..1"));
  verifyType(QStringLiteral("rel::ReferencePart"),
             RelationshipType::Aggregation, QStringLiteral("member"),
             QStringLiteral("reference"), QStringLiteral("1"));
  verifyType(QStringLiteral("rel::OwnedPart"), RelationshipType::Composition,
             QStringLiteral("member"), QStringLiteral("owned"),
             QStringLiteral("0..1"));
  verifyType(QStringLiteral("rel::SharedPart"), RelationshipType::Aggregation,
             QStringLiteral("member"), QStringLiteral("shared"),
             QStringLiteral("0..1"));
  verifyType(QStringLiteral("rel::UnknownPart"), RelationshipType::Association,
             QStringLiteral("member"), QStringLiteral("unknown"),
             QStringLiteral("1"));
  verifyType(QStringLiteral("rel::UsedPart"), RelationshipType::Dependency,
             QStringLiteral("signature"));
  verifyType(QStringLiteral("rel::ManyPart"), RelationshipType::Composition,
             QStringLiteral("member"), QStringLiteral("many"),
             QStringLiteral("0..*"));
  verifyType(QStringLiteral("rel::MappedPart"), RelationshipType::Composition,
             QStringLiteral("member"), QStringLiteral("mapped"),
             QStringLiteral("0..*"));
  verifyType(QStringLiteral("rel::FixedPart"), RelationshipType::Composition,
             QStringLiteral("member"), QStringLiteral("fixed"),
             QStringLiteral("4"));
  verifyType(QStringLiteral("rel::NestedSharedPart"),
             RelationshipType::Aggregation, QStringLiteral("member"),
             QStringLiteral("sharedMany"), QStringLiteral("0..*"));

  QList<CppSourceRelationship> duplicateRelationships;
  std::copy_if(
      preview.relationships.cbegin(), preview.relationships.cend(),
      std::back_inserter(duplicateRelationships),
      [](const CppSourceRelationship &relationship) {
        return relationship.sourceName == QStringLiteral("rel::Consumer") &&
               relationship.targetName == QStringLiteral("rel::DuplicatePart");
      });
  QCOMPARE(duplicateRelationships.size(), 2);
  QCOMPARE(QSet<QString>({duplicateRelationships.at(0).sourceRole,
                          duplicateRelationships.at(1).sourceRole}),
           QSet<QString>(
               {QStringLiteral("duplicateA"), QStringLiteral("duplicateB")}));
  QVERIFY(duplicateRelationships.at(0).symbolId !=
          duplicateRelationships.at(1).symbolId);

  ProjectData imported = createStarterProject();
  QCOMPARE(CppImportService::apply(imported, preview),
           preview.applicableCount());
  const auto importedOwned = std::find_if(
      imported.relationships.cbegin(), imported.relationships.cend(),
      [&](const Relationship &relationship) {
        const auto target = std::find_if(
            imported.elements.cbegin(), imported.elements.cend(),
            [&](const ModelElement &element) {
              return element.id == relationship.targetId &&
                     element.name == QStringLiteral("rel::OwnedPart");
            });
        return target != imported.elements.cend();
      });
  QVERIFY(importedOwned != imported.relationships.cend());
  QCOMPARE(importedOwned->sourceEnd.role, QStringLiteral("owned"));
  QCOMPARE(importedOwned->sourceEnd.multiplicity, QStringLiteral("0..1"));

  CppImportOptions reclassifiedOptions = options;
  reclassifiedOptions.memberTypeRules.removeFirst();
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
                                       "private:\n"
                                       "  class Hidden {};\n"
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

  ProjectController controller;
  CppImportOptions options;
  configureCppImportStereotypes(options, controller.data());
  const CppImportPreview preview = CppImportService::preview(
      sourceDirectory.path(), controller.data().elements,
      controller.data().relationships, options);
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

  const auto hiddenItem = std::find_if(
      preview.items.cbegin(), preview.items.cend(),
      [](const CppImportItem &item) {
        return item.symbol.qualifiedName ==
               QStringLiteral("domain::Outer::Hidden");
      });
  QVERIFY(hiddenItem != preview.items.cend());
  QVERIFY(hiddenItem->symbol.privateNestedType);
  QVERIFY(hiddenItem->desiredElement.stereotypeIds.contains(
      stereotype_catalog::kPrivateStereotypeId));

  const auto publicInnerItem = std::find_if(
      preview.items.cbegin(), preview.items.cend(),
      [](const CppImportItem &item) {
        return item.symbol.qualifiedName ==
               QStringLiteral("domain::Outer::Inner");
      });
  QVERIFY(publicInnerItem != preview.items.cend());
  QVERIFY(!publicInnerItem->symbol.privateNestedType);
  QVERIFY(!publicInnerItem->desiredElement.stereotypeIds.contains(
      stereotype_catalog::kPrivateStereotypeId));

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
  QVERIFY(importedRelationship->name.isEmpty());
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

  QList<CppImportProgress> progressUpdates;
  const CppImportPreview preview =
      CppImportService::preview(sourceDirectory.path(), {}, {}, {},
                                [&](const CppImportProgress &progress) {
                                  progressUpdates.append(progress);
                                });
  QVERIFY(preview.ok);
  QVERIFY(!preview.usedCompilationDatabase);
  QVERIFY(preview.compilationDatabasePath.isEmpty());
  QCOMPARE(preview.symbols.size(), 3);
  QCOMPARE(preview.relationships.size(), 1);
  QCOMPARE(preview.applicableCount(), 6);
  QVERIFY(!progressUpdates.isEmpty());
  QCOMPARE(progressUpdates.first().stage, CppImportProgressStage::Preparing);
  QCOMPARE(progressUpdates.last().stage,
           CppImportProgressStage::PlanningChanges);
  QVERIFY(std::any_of(progressUpdates.cbegin(), progressUpdates.cend(),
                      [](const CppImportProgress &progress) {
                        return progress.stage ==
                               CppImportProgressStage::DiscoveringSources;
                      }));
  const auto completedParsing = std::find_if(
      progressUpdates.crbegin(), progressUpdates.crend(),
      [](const CppImportProgress &progress) {
        return progress.stage == CppImportProgressStage::ParsingSources &&
               progress.total > 0 && progress.completed == progress.total;
      });
  QVERIFY(completedParsing != progressUpdates.crend());
  QVERIFY(std::any_of(progressUpdates.cbegin(), progressUpdates.cend(),
                      [](const CppImportProgress &progress) {
                        return progress.stage ==
                                   CppImportProgressStage::ParsingSources &&
                               !progress.detail.isEmpty();
                      }));
  QVERIFY(std::any_of(progressUpdates.cbegin(), progressUpdates.cend(),
                      [](const CppImportProgress &progress) {
                        return progress.stage ==
                               CppImportProgressStage::AnalyzingModel;
                      }));
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

void CoreTests::cppImportCombinesSelectedSourceFolders() {
  if (!CppImportService::available())
    QSKIP("This build was configured without libclang");

  QTemporaryDir sourceDirectory;
  QVERIFY(sourceDirectory.isValid());
  QDir root(sourceDirectory.path());
  QVERIFY(root.mkpath(QStringLiteral("alpha/nested")));
  QVERIFY(root.mkpath(QStringLiteral("beta")));
  QVERIFY(root.mkpath(QStringLiteral("ignored")));

  writeTestFile(root.filePath(QStringLiteral("beta/Beta.h")),
                QByteArray("#pragma once\n"
                           "namespace selected { struct Beta {}; }\n"));
  writeTestFile(root.filePath(QStringLiteral("alpha/Alpha.h")),
                QByteArray("#pragma once\n"
                           "#include \"../beta/Beta.h\"\n"
                           "namespace selected {\n"
                           "class Alpha { Beta *part; };\n"
                           "}\n"));
  writeTestFile(root.filePath(QStringLiteral("ignored/Noise.h")),
                QByteArray("class Noise {};\n"));

  const QString alphaPath = root.filePath(QStringLiteral("alpha"));
  const QString betaPath = root.filePath(QStringLiteral("beta"));
  const CppImportPreview preview = CppImportService::preview(
      QStringList{alphaPath, root.filePath(QStringLiteral("alpha/nested")),
                  betaPath},
      {});

  QVERIFY(preview.ok);
  QCOMPARE(preview.sourceRoots, QStringList({QDir::cleanPath(alphaPath),
                                             QDir::cleanPath(betaPath)}));
  const auto hasSymbol = [&](const QString &name) {
    return std::any_of(preview.symbols.cbegin(), preview.symbols.cend(),
                       [&](const CppSourceSymbol &symbol) {
                         return symbol.qualifiedName == name;
                       });
  };
  QVERIFY(hasSymbol(QStringLiteral("selected::Alpha")));
  QVERIFY(hasSymbol(QStringLiteral("selected::Beta")));
  QVERIFY(!hasSymbol(QStringLiteral("Noise")));
  QVERIFY(std::any_of(
      preview.relationships.cbegin(), preview.relationships.cend(),
      [](const CppSourceRelationship &relationship) {
        return relationship.sourceName == QStringLiteral("selected::Alpha") &&
               relationship.targetName == QStringLiteral("selected::Beta") &&
               relationship.relationshipType == RelationshipType::Aggregation;
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

void CoreTests::diagramCompartmentVisibilityPersistsAndIsUndoable() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString firstElementId =
      controller.addElement(QStringLiteral("class"), diagramId);
  controller.addElement(QStringLiteral("struct"), diagramId);
  const auto nodes = controller.data().diagrams.first().nodes;
  const QStringList nodeIds = {nodes.at(0).id, nodes.at(1).id};

  controller.setDiagramCompartmentVisible(diagramId,
                                          QStringLiteral("attributes"), false);
  QCOMPARE(controller.data().diagrams.first().showAttributes, false);
  controller.setNodesCompartmentVisibility(
      diagramId, nodeIds, QStringLiteral("attributes"),
      QStringLiteral("show"));
  controller.setNodesCompartmentVisibility(
      diagramId, nodeIds, QStringLiteral("operations"), QStringLiteral("hide"));

  for (const QString &nodeId : nodeIds) {
    const auto *node = findNode(controller.data().diagrams.first(), nodeId);
    QVERIFY(node);
    QVERIFY(node->showAttributes == std::optional<bool>(true));
    QVERIFY(node->showOperations == std::optional<bool>(false));
  }
  QCOMPARE(controller.undoText(),
           QStringLiteral("Set selected operations visibility"));
  controller.undo();
  for (const QString &nodeId : nodeIds)
    QVERIFY(!findNode(controller.data().diagrams.first(), nodeId)
                 ->showOperations);
  controller.redo();
  for (const QString &nodeId : nodeIds)
    QVERIFY(findNode(controller.data().diagrams.first(), nodeId)
                ->showOperations == std::optional<bool>(false));

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), controller.data()).ok);
  const LoadOutcome loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  QCOMPARE(loaded.project, controller.data());
  QCOMPARE(findElement(loaded.project, firstElementId)->attributes.size(), 1);
}

void CoreTests::diagramFiltersPersistMatchAndUndo() {
  ProjectData project = createStarterProject(QStringLiteral("Filtered"));
  ModelElement service;
  service.id = QStringLiteral("service");
  service.name = QStringLiteral("api::InvoiceService");
  service.type = ElementType::Class;
  service.stereotypeIds = {stereotype_catalog::kApiStereotypeId};
  service.attributes = {QStringLiteral("- repository: Repository")};
  service.operations = {QStringLiteral("+ serializeInvoice(): string")};

  ModelElement localStruct;
  localStruct.id = QStringLiteral("local-struct");
  localStruct.name = QStringLiteral("detail::InvoiceRecord");
  localStruct.type = ElementType::Struct;
  localStruct.stereotypeIds = {stereotype_catalog::kLocalStereotypeId};
  localStruct.attributes = {QStringLiteral("+ sequence: int")};

  DiagramFilter filter;
  filter.excludedElementTypes = {QStringLiteral("struct")};
  filter.includedStereotypeIds = {
      stereotype_catalog::kApiStereotypeId,
      QStringLiteral("stale-stereotype-id")};
  filter.namePattern = QStringLiteral("*Service");
  filter.memberPattern = QStringLiteral("serialize*");
  QVERIFY(diagram_filter::isActive(filter));
  QVERIFY(diagram_filter::matchesElement(project, service, filter));
  QVERIFY(!diagram_filter::matchesElement(project, localStruct, filter));

  filter.excludeMemberMatches = true;
  QVERIFY(!diagram_filter::matchesElement(project, service, filter));
  filter.excludeMemberMatches = false;
  filter.excludedStereotypeIds = {stereotype_catalog::kApiStereotypeId};
  QVERIFY(!diagram_filter::matchesElement(project, service, filter));

  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  QVariantMap values;
  values.insert(QStringLiteral("excludedElementTypes"),
                QStringList{QStringLiteral("struct"),
                            QStringLiteral("enumeration")});
  values.insert(QStringLiteral("includedStereotypeIds"),
                QStringList{stereotype_catalog::kPrivateStereotypeId,
                            stereotype_catalog::kLocalStereotypeId});
  values.insert(QStringLiteral("namePattern"), QStringLiteral("I*"));
  values.insert(QStringLiteral("memberPattern"), QStringLiteral("*token*"));
  values.insert(QStringLiteral("excludeMemberMatches"), true);
  QVERIFY(controller.setDiagramFilter(diagramId, values));
  QCOMPARE(controller.undoText(), QStringLiteral("Filter diagram"));
  QCOMPARE(controller.diagramFilter(diagramId).value(
               QStringLiteral("excludedElementTypes")),
           values.value(QStringLiteral("excludedElementTypes")));

  const DiagramFilter applied = controller.data().diagrams.first().filter;
  controller.undo();
  QVERIFY(!diagram_filter::isActive(controller.data().diagrams.first().filter));
  controller.redo();
  QCOMPARE(controller.data().diagrams.first().filter, applied);

  ProjectData persisted = controller.data();
  persisted.diagrams.first().filter.extra.insert(
      QStringLiteral("futureFilterOption"), true);
  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), persisted).ok);
  const LoadOutcome loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  QCOMPARE(loaded.project.diagrams.first().filter,
           persisted.diagrams.first().filter);
}

void CoreTests::relatedTypeNeighborhoodActionsAreDirectionalAndUndoable() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString incoming =
      controller.addElement(QStringLiteral("class"), diagramId);
  const QString focus =
      controller.addElement(QStringLiteral("class"), diagramId);
  const QString outgoing =
      controller.addElement(QStringLiteral("struct"), diagramId);
  const auto initialNodes = controller.data().diagrams.first().nodes;
  QCOMPARE(initialNodes.size(), 3);

  QVERIFY(!controller
               .createRelationship(diagramId, initialNodes.at(0).id,
                                   initialNodes.at(1).id,
                                   QStringLiteral("dependency"))
               .isEmpty());
  QVERIFY(!controller
               .createRelationship(diagramId, initialNodes.at(1).id,
                                   initialNodes.at(2).id,
                                   QStringLiteral("aggregation"))
               .isEmpty());
  controller.removePresentations(
      diagramId, {initialNodes.at(0).id, initialNodes.at(2).id});
  const QString focusNodeId =
      controller.data().diagrams.first().nodes.first().id;
  QCOMPARE(controller.data().diagrams.first().nodes.first().elementId, focus);

  QCOMPARE(controller.relatedElementCountForDiagram(diagramId, focusNodeId,
                                                    QStringLiteral("incoming")),
           1);
  QCOMPARE(controller.relatedElementCountForDiagram(diagramId, focusNodeId,
                                                    QStringLiteral("outgoing")),
           1);
  QCOMPARE(controller.addRelatedElementsToDiagram(diagramId, focusNodeId,
                                                  QStringLiteral("incoming")),
           1);
  QVERIFY(std::any_of(controller.data().diagrams.first().nodes.cbegin(),
                      controller.data().diagrams.first().nodes.cend(),
                      [&](const NodePresentation &node) {
                        return node.elementId == incoming;
                      }));
  QVERIFY(std::none_of(controller.data().diagrams.first().nodes.cbegin(),
                       controller.data().diagrams.first().nodes.cend(),
                       [&](const NodePresentation &node) {
                         return node.elementId == outgoing;
                       }));
  QCOMPARE(controller.data().diagrams.first().connectors.size(), 1);

  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes.size(), 1);
  controller.redo();
  QCOMPARE(controller.data().diagrams.first().nodes.size(), 2);

  QCOMPARE(controller.addRelatedElementsToDiagram(diagramId, focusNodeId,
                                                  QStringLiteral("outgoing")),
           1);
  QCOMPARE(controller.data().diagrams.first().nodes.size(), 3);
  QCOMPARE(controller.data().diagrams.first().connectors.size(), 2);
  QCOMPARE(controller.relatedElementCountForDiagram(diagramId, focusNodeId,
                                                    QStringLiteral("incoming")),
           0);
  QCOMPARE(controller.relatedElementCountForDiagram(diagramId, focusNodeId,
                                                    QStringLiteral("outgoing")),
           0);
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

void CoreTests::projectTreeIncrementalWildcardSearch() {
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
  const QString diagramId = controller.data().diagrams.first().id;
  controller.renameDiagram(diagramId, QStringLiteral("API Overview"));

  ProjectTreeModel *tree = controller.treeModel();
  QSignalSpy patternChanged(tree, &ProjectTreeModel::searchPatternChanged);
  QCOMPARE(tree->rowCount(), 2);

  tree->setSearchPattern(QStringLiteral("inner"));
  QCOMPARE(patternChanged.count(), 1);
  QCOMPARE(tree->searchPattern(), QStringLiteral("inner"));
  QCOMPARE(tree->rowCount(), 1);
  const QModelIndex modelRoot = tree->index(0, 0);
  QCOMPARE(tree->data(modelRoot, Qt::DisplayRole).toString(),
           QStringLiteral("Model"));
  QVERIFY(tree->indexForObject(inner, QStringLiteral("element")).isValid());
  QVERIFY(!tree->indexForObject(service, QStringLiteral("element")).isValid());
  QVERIFY(
      !tree->indexForObject(diagramId, QStringLiteral("diagram")).isValid());

  // Ancestors remain visible for context. Their drag semantics continue to
  // include hidden descendants, so search never changes the represented model.
  const QModelIndex outerIndex =
      tree->indexForObject(outer, QStringLiteral("element"));
  QVERIFY(outerIndex.isValid());
  const QModelIndex demoNamespace = outerIndex.parent();
  QVERIFY(demoNamespace.isValid());
  QCOMPARE(tree->elementIdsForIndexes({demoNamespace}),
           QStringList({outer, inner, service}));

  tree->setSearchPattern(QStringLiteral("*serv?ce"));
  QVERIFY(tree->indexForObject(service, QStringLiteral("element")).isValid());
  QVERIFY(!tree->indexForObject(inner, QStringLiteral("element")).isValid());
  QVERIFY(!tree->indexForObject(other, QStringLiteral("element")).isValid());

  // Qualified paths can be searched even though tree rows show only their
  // unqualified labels.
  tree->setSearchPattern(QStringLiteral("DEMO::*"));
  QVERIFY(tree->indexForObject(outer, QStringLiteral("element")).isValid());
  QVERIFY(tree->indexForObject(inner, QStringLiteral("element")).isValid());
  QVERIFY(tree->indexForObject(service, QStringLiteral("element")).isValid());
  QVERIFY(!tree->indexForObject(other, QStringLiteral("element")).isValid());

  tree->setSearchPattern(QStringLiteral("api overview"));
  QCOMPARE(tree->rowCount(), 1);
  const QModelIndex diagramRoot = tree->index(0, 0);
  QCOMPARE(tree->data(diagramRoot, Qt::DisplayRole).toString(),
           QStringLiteral("Diagrams"));
  QVERIFY(tree->indexForObject(diagramId, QStringLiteral("diagram")).isValid());
  QVERIFY(!tree->indexForObject(outer, QStringLiteral("element")).isValid());

  tree->setSearchPattern(QStringLiteral("no such item"));
  QCOMPARE(tree->rowCount(), 0);
  QVERIFY(
      !tree->indexForObject(diagramId, QStringLiteral("diagram")).isValid());

  tree->setSearchPattern({});
  QCOMPARE(tree->rowCount(), 2);
  QVERIFY(tree->indexForObject(outer, QStringLiteral("element")).isValid());
  QVERIFY(tree->indexForObject(other, QStringLiteral("element")).isValid());
  QVERIFY(tree->indexForObject(diagramId, QStringLiteral("diagram")).isValid());
  QCOMPARE(patternChanged.count(), 6);
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

void CoreTests::connectorAnnotationPlacementsPersistAndAreUndoable() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  controller.addElement(QStringLiteral("class"), diagramId);
  controller.addElement(QStringLiteral("class"), diagramId);
  const auto nodes = controller.data().diagrams.first().nodes;
  const QString connectorId = controller.createRelationship(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("association"));
  QVERIFY(!connectorId.isEmpty());
  const auto *createdConnector =
      findConnector(controller.data().diagrams.first(), connectorId);
  QVERIFY(createdConnector);
  controller.editText(createdConnector->relationshipId, QStringLiteral("name"),
                      -1, QStringLiteral("named relationship"));

  const auto connector = [&]() {
    return findConnector(controller.data().diagrams.first(), connectorId);
  };
  controller.setConnectorAnnotationPlacement(
      diagramId, connectorId, QStringLiteral("name"), 0.35, 12.0, -18.0);
  QVERIFY(connector()->annotationPlacements.contains(QStringLiteral("name")));
  const ConnectorAnnotationPlacement expected =
      connector()->annotationPlacements.value(QStringLiteral("name"));
  QCOMPARE(expected.routePosition, 0.35);
  QCOMPARE(expected.tangentOffset, 12.0);
  QCOMPARE(expected.normalOffset, -18.0);
  QCOMPARE(controller.undoText(), QStringLiteral("Move connector annotation"));

  controller.undo();
  QVERIFY(connector()->annotationPlacements.isEmpty());
  controller.redo();
  QCOMPARE(connector()->annotationPlacements.value(QStringLiteral("name")),
           expected);

  controller.resetConnectorAnnotationPlacement(diagramId, connectorId,
                                               QStringLiteral("name"));
  QVERIFY(connector()->annotationPlacements.isEmpty());
  controller.undo();
  QCOMPARE(connector()->annotationPlacements.value(QStringLiteral("name")),
           expected);

  controller.setConnectorAnnotationPlacement(
      diagramId, connectorId, QStringLiteral("sourceRole"), 0.2, 0.0, 24.0);
  QCOMPARE(connector()->annotationPlacements.size(), 2);
  controller.resetConnectorAnnotationPlacements(diagramId, connectorId);
  QVERIFY(connector()->annotationPlacements.isEmpty());
  QCOMPARE(controller.undoText(),
           QStringLiteral("Reset connector annotation positions"));
  controller.undo();
  QCOMPARE(connector()->annotationPlacements.size(), 2);

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), controller.data()).ok);
  const LoadOutcome loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  const auto *loadedConnector =
      findConnector(loaded.project.diagrams.first(), connectorId);
  QVERIFY(loadedConnector);
  // sourceRole has no semantic text, so its otherwise stale placement is not
  // serialized. The visible relationship name retains its manual position.
  QCOMPARE(loadedConnector->annotationPlacements.size(), 1);
  QCOMPARE(loadedConnector->annotationPlacements.value(QStringLiteral("name")),
           expected);
}

void CoreTests::stereotypeCatalogAssignmentsPersistAndAreUndoable() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString sourceElement =
      controller.addElement(QStringLiteral("class"), diagramId);
  const QString targetElement =
      controller.addElement(QStringLiteral("class"), diagramId);
  const auto nodes = controller.data().diagrams.first().nodes;
  const QString connectorId = controller.createRelationship(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("dependency"));
  const auto *connector =
      findConnector(controller.data().diagrams.first(), connectorId);
  QVERIFY(connector);
  const QString relationshipId = connector->relationshipId;

  const QString customId = controller.saveProjectStereotype(
      {}, QStringLiteral("audited"),
      {QStringLiteral("class"), QStringLiteral("relationship")});
  QVERIFY(!customId.isEmpty());
  QCOMPARE(controller.stereotypeCatalog().size(),
           stereotype_catalog::defaultDefinitions().size() + 1);

  // Seeded UML entries are ordinary project definitions after creation.
  const auto *abstractDefinition = findStereotypeDefinition(
      controller.data(), QStringLiteral("uml.abstract"));
  QVERIFY(abstractDefinition);
  const QString abstractId = abstractDefinition->id;
  const QStringList abstractApplicability = abstractDefinition->applicableTo;
  QCOMPARE(controller.saveProjectStereotype(abstractId,
                                            QStringLiteral("abstract type"),
                                            abstractApplicability),
           abstractId);
  QCOMPARE(findStereotypeDefinition(controller.data(),
                                    QStringLiteral("uml.abstract"))
               ->name,
           QStringLiteral("abstract type"));
  controller.undo();
  QCOMPARE(findStereotypeDefinition(controller.data(),
                                    QStringLiteral("uml.abstract"))
               ->name,
           QStringLiteral("abstract"));
  QVERIFY(controller.deleteProjectStereotype(QStringLiteral("uml.abstract")));
  QVERIFY(!findStereotypeDefinition(controller.data(),
                                    QStringLiteral("uml.abstract")));
  controller.undo();
  QVERIFY(findStereotypeDefinition(controller.data(),
                                   QStringLiteral("uml.abstract")));

  controller.assignStereotypes(QStringLiteral("element"), sourceElement,
                               {QStringLiteral("uml.interface"), customId});
  QCOMPARE(findElement(controller.data(), sourceElement)->stereotypeIds,
           QStringList({QStringLiteral("uml.interface"), customId}));
  QCOMPARE(controller.undoText(), QStringLiteral("Assign stereotypes"));
  controller.undo();
  QVERIFY(
      findElement(controller.data(), sourceElement)->stereotypeIds.isEmpty());
  controller.redo();

  controller.assignStereotypes(QStringLiteral("relationship"), relationshipId,
                               {QStringLiteral("uml.trace"), customId});
  QCOMPARE(findRelationship(controller.data(), relationshipId)->stereotypeIds,
           QStringList({QStringLiteral("uml.trace"), customId}));
  controller.selectObject(sourceElement, QStringLiteral("element"));
  QCOMPARE(controller.selectedStereotypes(),
           QStringLiteral("«interface, audited»"));
  QVERIFY(presentation_layout::nodeContentSize(
              controller.data(), *findElement(controller.data(), sourceElement))
              .height() > presentation_layout::nodeContentSize(
                              *findElement(controller.data(), sourceElement))
                              .height());

  // An edit cannot silently make existing assignments invalid.
  QVERIFY(controller
              .saveProjectStereotype(customId, QStringLiteral("audited"),
                                     {QStringLiteral("package")})
              .isEmpty());
  QCOMPARE(
      findStereotypeDefinition(controller.data(), customId)->applicableTo,
      QStringList({QStringLiteral("class"), QStringLiteral("relationship")}));

  QCOMPARE(controller.stereotypeAssignmentCount(customId), 2);
  QVERIFY(controller.deleteProjectStereotype(customId));
  QVERIFY(!findStereotypeDefinition(controller.data(), customId));
  QVERIFY(!findElement(controller.data(), sourceElement)
               ->stereotypeIds.contains(customId));
  QVERIFY(!findRelationship(controller.data(), relationshipId)
               ->stereotypeIds.contains(customId));
  controller.undo();
  QVERIFY(findStereotypeDefinition(controller.data(), customId));
  QVERIFY(findElement(controller.data(), sourceElement)
              ->stereotypeIds.contains(customId));
  QVERIFY(findRelationship(controller.data(), relationshipId)
              ->stereotypeIds.contains(customId));

  QTemporaryDir temporary;
  QVERIFY(temporary.isValid());
  QVERIFY(ProjectSerializer::save(temporary.path(), controller.data()).ok);
  const LoadOutcome loaded = ProjectSerializer::load(temporary.path());
  QVERIFY(loaded.ok);
  QCOMPARE(loaded.project, controller.data());
  QVERIFY(ProjectSerializer::validate(loaded.project).isEmpty());

  ProjectData invalid = loaded.project;
  findElement(invalid, targetElement)
      ->stereotypeIds.append(QStringLiteral("uml.trace"));
  const auto diagnostics = ProjectSerializer::validate(invalid);
  QVERIFY(std::any_of(diagnostics.cbegin(), diagnostics.cend(),
                      [](const Diagnostic &diagnostic) {
                        return diagnostic.message.contains(
                            QStringLiteral("does not apply to class"));
                      }));
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
  QVERIFY(findRelationship(controller.data(), relationshipId)->name.isEmpty());

  controller.editText(relationshipId, QStringLiteral("name"), -1,
                      QStringLiteral("owns"));
  QCOMPARE(findRelationship(controller.data(), relationshipId)->name,
           QStringLiteral("owns"));
  controller.editText(relationshipId, QStringLiteral("name"), -1,
                      QStringLiteral("   "));
  QVERIFY(findRelationship(controller.data(), relationshipId)->name.isEmpty());
  controller.undo();
  QCOMPARE(findRelationship(controller.data(), relationshipId)->name,
           QStringLiteral("owns"));
  controller.undo();
  QVERIFY(findRelationship(controller.data(), relationshipId)->name.isEmpty());

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
    QVERIFY(relationship.name.isEmpty());
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
  const auto &restoredDiagram = controller.data().diagrams.first();
  for (const auto &connector : restoredDiagram.connectors) {
    restoredRelationshipIds.insert(connector.relationshipId);
    QVERIFY(connector.sourceAnchor.side != ConnectorSide::Automatic);
    QVERIFY(connector.targetAnchor.side != ConnectorSide::Automatic);
    const auto *relationship =
        findRelationship(controller.data(), connector.relationshipId);
    QVERIFY(relationship);
    const auto sourceNode =
        std::find_if(restoredDiagram.nodes.cbegin(),
                     restoredDiagram.nodes.cend(),
                     [&](const NodePresentation &node) {
                       return node.elementId == relationship->sourceId;
                     });
    const auto targetNode =
        std::find_if(restoredDiagram.nodes.cbegin(),
                     restoredDiagram.nodes.cend(),
                     [&](const NodePresentation &node) {
                       return node.elementId == relationship->targetId;
                     });
    QVERIFY(sourceNode != restoredDiagram.nodes.cend());
    QVERIFY(targetNode != restoredDiagram.nodes.cend());
    const int sourcePointCount = connector_ports::snapPointCountForSide(
        *sourceNode, connector.sourceAnchor.side);
    const int targetPointCount = connector_ports::snapPointCountForSide(
        *targetNode, connector.targetAnchor.side);
    QCOMPARE(sourcePointCount, types.size());
    QCOMPARE(targetPointCount, types.size());
    QVERIFY(connector_ports::snapOffsets(sourcePointCount)
                .contains(connector.sourceAnchor.offset));
    QVERIFY(connector_ports::snapOffsets(targetPointCount)
                .contains(connector.targetAnchor.offset));
  }
  QCOMPARE(restoredRelationshipIds, relationshipIds);
  controller.undo();
  const auto &withoutRestoredSource = controller.data().diagrams.first();
  QCOMPARE(withoutRestoredSource.nodes.size(), 1);
  QCOMPARE(withoutRestoredSource.nodes.first().horizontalPortSnapPoints, 1);
  QCOMPARE(withoutRestoredSource.nodes.first().verticalPortSnapPoints, 1);
  controller.redo();

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
    QCOMPARE(settings.cppMemberTypeRules(),
             ApplicationSettings::defaultCppMemberTypeRules());
    QCOMPARE(settings.contextToolboxConfiguration(),
             ApplicationSettings::defaultContextToolboxConfiguration());
    QCOMPARE(settings.packageReassignmentPolicy(), QStringLiteral("ask"));
    QVERIFY(settings.automaticUpdateChecksEnabled());
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
    settings.setAutomaticUpdateChecksEnabled(false);
    QVERIFY(settings.setCppInterfacePattern(QStringLiteral("^Abstract.*$")));
    QVERIFY(!settings.setCppInterfacePattern(QStringLiteral("[")));
    QCOMPARE(settings.cppInterfacePattern(), QStringLiteral("^Abstract.*$"));
    const QVariantList customMemberRules = {
        QVariantMap{
            {QStringLiteral("typeName"), QStringLiteral(" custom::Owner<> ")},
            {QStringLiteral("relationshipType"), QStringLiteral("composition")},
            {QStringLiteral("multiplicity"), QStringLiteral("0..*")},
            {QStringLiteral("targetArgument"), 2}},
        QVariantMap{
            {QStringLiteral("typeName"), QStringLiteral("custom::Shared")},
            {QStringLiteral("relationshipType"), QStringLiteral("aggregation")},
            {QStringLiteral("multiplicity"), QStringLiteral("0..1")},
            {QStringLiteral("targetArgument"), 1}},
    };
    QVERIFY(settings.setCppMemberTypeRules(customMemberRules));
    const QVariantList normalizedMemberRules = {
        QVariantMap{
            {QStringLiteral("typeName"), QStringLiteral("custom::Owner")},
            {QStringLiteral("relationshipType"), QStringLiteral("composition")},
            {QStringLiteral("multiplicity"), QStringLiteral("0..*")},
            {QStringLiteral("targetArgument"), 2}},
        QVariantMap{
            {QStringLiteral("typeName"), QStringLiteral("custom::Shared")},
            {QStringLiteral("relationshipType"), QStringLiteral("aggregation")},
            {QStringLiteral("multiplicity"), QStringLiteral("0..1")},
            {QStringLiteral("targetArgument"), 1}},
    };
    QCOMPARE(settings.cppMemberTypeRules(), normalizedMemberRules);
    QVariantMap toolboxConfiguration =
        ApplicationSettings::defaultContextToolboxConfiguration();
    QVariantList selectionActions =
        toolboxConfiguration.value(QStringLiteral("selection")).toList();
    QVariantMap disabledStyle = selectionActions.takeLast().toMap();
    disabledStyle.insert(QStringLiteral("enabled"), false);
    selectionActions.prepend(disabledStyle);
    toolboxConfiguration.insert(QStringLiteral("selection"), selectionActions);
    QVERIFY(settings.setContextToolboxConfiguration(toolboxConfiguration));
    QCOMPARE(settings.contextToolboxConfiguration()
                 .value(QStringLiteral("selection"))
                 .toList()
                 .first()
                 .toMap()
                 .value(QStringLiteral("actionId"))
                 .toString(),
             QStringLiteral("style.assignNamed"));
    QVERIFY(!settings.contextToolboxConfiguration()
                 .value(QStringLiteral("selection"))
                 .toList()
                 .first()
                 .toMap()
                 .value(QStringLiteral("enabled"))
                 .toBool());
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
    const QVariantList expectedMemberRules = {
        QVariantMap{
            {QStringLiteral("typeName"), QStringLiteral("custom::Owner")},
            {QStringLiteral("relationshipType"), QStringLiteral("composition")},
            {QStringLiteral("multiplicity"), QStringLiteral("0..*")},
            {QStringLiteral("targetArgument"), 2}},
        QVariantMap{
            {QStringLiteral("typeName"), QStringLiteral("custom::Shared")},
            {QStringLiteral("relationshipType"), QStringLiteral("aggregation")},
            {QStringLiteral("multiplicity"), QStringLiteral("0..1")},
            {QStringLiteral("targetArgument"), 1}},
    };
    QCOMPARE(restored.cppMemberTypeRules(), expectedMemberRules);
    const QVariantList restoredSelectionActions =
        restored.contextToolboxConfiguration()
            .value(QStringLiteral("selection"))
            .toList();
    QCOMPARE(restoredSelectionActions.first()
                 .toMap()
                 .value(QStringLiteral("actionId"))
                 .toString(),
             QStringLiteral("style.assignNamed"));
    QVERIFY(!restoredSelectionActions.first()
                 .toMap()
                 .value(QStringLiteral("enabled"))
                 .toBool());
    QCOMPARE(restored.packageReassignmentPolicy(), QStringLiteral("allow"));
    QVERIFY(!restored.automaticUpdateChecksEnabled());
    const QDateTime initialCheck =
        QDateTime(QDate(2026, 7, 1), QTime(12, 0), Qt::UTC);
    restored.setAutomaticUpdateChecksEnabled(true);
    QVERIFY(restored.automaticUpdateCheckDue(initialCheck));
    restored.recordUpdateCheck(initialCheck);
    QVERIFY(!restored.automaticUpdateCheckDue(initialCheck.addSecs(23 * 3600)));
    QVERIFY(restored.automaticUpdateCheckDue(initialCheck.addSecs(24 * 3600)));
    restored.setAutomaticUpdateChecksEnabled(false);
    QVERIFY(!restored.automaticUpdateCheckDue(initialCheck.addDays(2)));
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
    QCOMPARE(restored.cppMemberTypeRules(),
             ApplicationSettings::defaultCppMemberTypeRules());
    QCOMPARE(restored.contextToolboxConfiguration(),
             ApplicationSettings::defaultContextToolboxConfiguration());
    QCOMPARE(restored.packageReassignmentPolicy(), QStringLiteral("ask"));
    QCOMPARE(restored.relationshipGestureKeys(),
             ApplicationSettings::defaultRelationshipGestureKeys());
    QVERIFY(restored.automaticUpdateChecksEnabled());
  }
}

void CoreTests::updateManifestValidationAndVersionComparison() {
  const QByteArray valid = R"json(
    {
      "schemaVersion": 1,
      "channel": "stable",
      "version": "1.2.3",
      "repositoryUrl":
        "https://uafua.github.io/yauml/updates/windows/x64/stable/",
      "releaseNotesUrl":
        "https://github.com/uafUa/yauml/releases/tag/v1.2.3"
    }
  )json";

  const UpdateManifestOutcome parsed = parseUpdateManifest(valid);
  QVERIFY2(parsed, qPrintable(parsed.error));
  QCOMPARE(parsed.manifest->version, QStringLiteral("1.2.3"));
  QCOMPARE(parsed.manifest->channel, QStringLiteral("stable"));

  QVERIFY(isNewerApplicationVersion(QStringLiteral("1.2.3"),
                                    QStringLiteral("1.2.2")));
  QVERIFY(!isNewerApplicationVersion(QStringLiteral("1.2.3"),
                                     QStringLiteral("1.2.3")));
  QVERIFY(!isNewerApplicationVersion(QStringLiteral("1.2"),
                                     QStringLiteral("1.1.9")));

  QVERIFY(!parseUpdateManifest(QByteArrayLiteral("{")).manifest);
  QVERIFY(!parseUpdateManifest(
               QByteArray(valid).replace("\"schemaVersion\": 1",
                                         "\"schemaVersion\": 2"))
               .manifest);
  QVERIFY(!parseUpdateManifest(
               QByteArray(valid).replace("\"channel\": \"stable\"",
                                         "\"channel\": \"preview\""))
               .manifest);
  QVERIFY(!parseUpdateManifest(
               QByteArray(valid).replace("\"version\": \"1.2.3\"",
                                         "\"version\": \"1.2\""))
               .manifest);
  QVERIFY(!parseUpdateManifest(
               QByteArray(valid).replace("https://uafua.github.io",
                                         "http://uafua.github.io"))
               .manifest);
}

void CoreTests::applicationPreferencesMigrateLegacyCppPointerTypes() {
  IsolatedSettingsScope settingsScope;
  {
    QSettings legacy;
    legacy.beginGroup(QStringLiteral("preferences/cppImport"));
    legacy.setValue(QStringLiteral("owningPointerTypes"),
                    QStringList({QStringLiteral("custom::Owner")}));
    legacy.setValue(QStringLiteral("sharedPointerTypes"),
                    QStringList({QStringLiteral("custom::Shared")}));
    legacy.endGroup();
  }

  ApplicationSettings migrated;
  const QList<CppMemberTypeRule> rules = migrated.cppMemberTypeRuleValues();
  const auto findRule = [&](const QString &typeName) {
    return std::find_if(rules.cbegin(), rules.cend(),
                        [&](const CppMemberTypeRule &rule) {
                          return rule.typeName == typeName;
                        });
  };

  const auto owner = findRule(QStringLiteral("custom::Owner"));
  QVERIFY(owner != rules.cend());
  QVERIFY(owner->relationshipType == RelationshipType::Composition);
  QCOMPARE(owner->multiplicity, QStringLiteral("0..1"));

  const auto shared = findRule(QStringLiteral("custom::Shared"));
  QVERIFY(shared != rules.cend());
  QVERIFY(shared->relationshipType == RelationshipType::Aggregation);
  QCOMPARE(shared->multiplicity, QStringLiteral("0..1"));

  // New standard-container defaults are added even for an existing profile.
  const auto vector = findRule(QStringLiteral("std::vector"));
  QVERIFY(vector != rules.cend());
  QVERIFY(vector->relationshipType == RelationshipType::Composition);
  QCOMPARE(vector->multiplicity, QStringLiteral("0..*"));
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
  QVERIFY(!QFileInfo::exists(marker.fileName()));
  QVERIFY(!QDir(recovery).exists());
  QVERIFY(
      std::any_of(outcome.diagnostics.cbegin(), outcome.diagnostics.cend(),
                  [](const Diagnostic &diagnostic) {
                    return diagnostic.category == QStringLiteral("recovery") &&
                           diagnostic.severity == DiagnosticSeverity::Warning;
                  }));
}

void CoreTests::invalidSaveRecoveryIsNonDestructive() {
  const auto exerciseInvalidRecovery = [](bool removeBackup,
                                          const QString &caseName) {
    QTemporaryDir temporary;
    if (!temporary.isValid())
      return false;
    const ProjectData project =
        createStarterProject(QStringLiteral("Recovery source"));
    if (!ProjectSerializer::save(temporary.path(), project).ok)
      return false;

    const QDir root(temporary.path());
    const QString recovery = root.filePath(QStringLiteral(".uuml-recovery"));
    if (!QDir().mkpath(recovery))
      return false;
    struct RecoveryPair {
      QString target;
      QString backup;
    };
    const QList<RecoveryPair> files = {
        {root.filePath(QStringLiteral("manifest.json5")),
         QDir(recovery).filePath(QStringLiteral("manifest.json5"))},
        {root.filePath(QStringLiteral("model/model.json5")),
         QDir(recovery).filePath(QStringLiteral("model.json5"))},
        {root.filePath(QStringLiteral("diagrams/diagrams.json5")),
         QDir(recovery).filePath(QStringLiteral("diagrams.json5"))}};
    for (const auto &file : files) {
      if (!QFile::copy(file.target, file.backup))
        return false;
    }

    if (removeBackup) {
      if (!QFile::remove(files.at(1).backup))
        return false;
    } else {
      writeTestFile(files.at(1).backup,
                    QByteArrayLiteral("{ malformed recovery"));
    }
    writeTestFile(QDir(recovery).filePath(QStringLiteral("pending")),
                  QByteArrayLiteral("pending\n"));

    QMap<QString, QByteArray> liveContents;
    for (qsizetype index = 0; index < files.size(); ++index) {
      const QByteArray sentinel =
          QStringLiteral("%1 live file %2").arg(caseName).arg(index).toUtf8();
      writeTestFile(files.at(index).target, sentinel);
      liveContents.insert(files.at(index).target, sentinel);
    }

    const auto outcome = ProjectSerializer::load(temporary.path());
    if (outcome.ok || outcome.recovered)
      return false;
    const bool hasRecoveryError = std::any_of(
        outcome.diagnostics.cbegin(), outcome.diagnostics.cend(),
        [](const Diagnostic &diagnostic) {
          return diagnostic.category == QStringLiteral("recovery") &&
                 diagnostic.severity == DiagnosticSeverity::Error;
        });
    if (!hasRecoveryError ||
        !QFileInfo::exists(QDir(recovery).filePath(QStringLiteral("pending"))))
      return false;

    for (const auto &file : files) {
      QFile current(file.target);
      if (!current.open(QIODevice::ReadOnly) ||
          current.readAll() != liveContents.value(file.target))
        return false;
    }
    return true;
  };

  QVERIFY2(exerciseInvalidRecovery(true, QStringLiteral("missing-backup")),
           "A missing recovery backup modified live project files");
  QVERIFY2(exerciseInvalidRecovery(false, QStringLiteral("malformed-backup")),
           "A malformed recovery backup modified live project files");
}

void CoreTests::saveFaultBoundariesRemainRecoverable() {
  using test_support::ProjectWriteBoundary;
  using test_support::ProjectWritePurpose;
  using test_support::ProjectWriteStage;
  using test_support::ScopedProjectWriteFaultInjector;

  const auto updatedProject = [](const ProjectData &original) {
    ProjectData updated = original;
    updated.name = QStringLiteral("Updated after fault");

    ModelElement element;
    element.id = newId();
    element.name = QStringLiteral("Added type");
    updated.elements.append(element);

    NodePresentation node;
    node.id = newId();
    node.elementId = element.id;
    node.geometry = QRectF(80.0, 90.0, 220.0, 120.0);
    updated.diagrams.first().nodes.append(node);
    return updated;
  };
  const auto pendingMarker = [](const QString &root) {
    return QDir(root).filePath(
        QStringLiteral(".uuml-recovery/pending"));
  };
  const auto caseLabel = [](ProjectWritePurpose purpose,
                            ProjectWriteStage stage, int ordinal) {
    return QStringLiteral("purpose=%1 stage=%2 file=%3")
        .arg(static_cast<int>(purpose))
        .arg(static_cast<int>(stage))
        .arg(ordinal);
  };

  const QList<ProjectWriteStage> stages = {
      ProjectWriteStage::Open, ProjectWriteStage::Write,
      ProjectWriteStage::Commit};
  const QList<QPair<ProjectWritePurpose, int>> saveWrites = {
      {ProjectWritePurpose::RecoveryBackup, 3},
      {ProjectWritePurpose::RecoveryMarker, 1},
      {ProjectWritePurpose::ProjectFile, 3}};

  // Every save-side atomic-write boundary must either leave the old files
  // untouched (backup/marker preparation) or retain a pending recovery set
  // capable of restoring the old project after partial live-file replacement.
  for (const auto &[purpose, fileCount] : saveWrites) {
    for (const ProjectWriteStage stage : stages) {
      for (int ordinal = 0; ordinal < fileCount; ++ordinal) {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const ProjectData original =
            createStarterProject(QStringLiteral("Fault source"));
        const ProjectData updated = updatedProject(original);
        QVERIFY(ProjectSerializer::save(temporary.path(), original).ok);

        int matchingBoundary = 0;
        SaveOutcome failedSave;
        {
          ScopedProjectWriteFaultInjector fault(
              [&](const ProjectWriteBoundary &boundary) {
                if (boundary.purpose != purpose || boundary.stage != stage)
                  return false;
                return matchingBoundary++ == ordinal;
              });
          failedSave = ProjectSerializer::save(temporary.path(), updated);
        }

        const QString label = caseLabel(purpose, stage, ordinal);
        QVERIFY2(!failedSave.ok, qPrintable(label + QStringLiteral(
                                                       " unexpectedly saved")));
        const bool liveWriteFailed =
            purpose == ProjectWritePurpose::ProjectFile;
        QCOMPARE(QFileInfo::exists(pendingMarker(temporary.path())),
                 liveWriteFailed);

        const LoadOutcome recovered =
            ProjectSerializer::load(temporary.path());
        QVERIFY2(recovered.ok,
                 qPrintable(label + QStringLiteral(" did not load")));
        QCOMPARE(recovered.project, original);
        QCOMPARE(recovered.recovered, liveWriteFailed);

        // The scoped injector must not leak. A normal retry writes the complete
        // new generation and removes any partial recovery directory.
        QVERIFY2(ProjectSerializer::save(temporary.path(), updated).ok,
                 qPrintable(label + QStringLiteral(" did not retry")));
        const LoadOutcome retried =
            ProjectSerializer::load(temporary.path());
        QVERIFY(retried.ok);
        QCOMPARE(retried.project, updated);
        QVERIFY(!QFileInfo::exists(pendingMarker(temporary.path())));
      }
    }
  }

  // Recovery itself is also a three-file atomic sequence. A fault at any
  // restore boundary keeps the complete backup set and marker so the next
  // launch retries the recovery from the beginning.
  for (const ProjectWriteStage stage : stages) {
    for (int ordinal = 0; ordinal < 3; ++ordinal) {
      QTemporaryDir temporary;
      QVERIFY(temporary.isValid());
      const ProjectData original =
          createStarterProject(QStringLiteral("Restore source"));
      const ProjectData updated = updatedProject(original);
      QVERIFY(ProjectSerializer::save(temporary.path(), original).ok);

      int liveCommit = 0;
      {
        ScopedProjectWriteFaultInjector interrupt(
            [&](const ProjectWriteBoundary &boundary) {
              if (boundary.purpose != ProjectWritePurpose::ProjectFile ||
                  boundary.stage != ProjectWriteStage::Commit)
                return false;
              // The manifest has committed when model commit is interrupted,
              // producing a genuine mixed live generation.
              return liveCommit++ == 1;
            });
        QVERIFY(!ProjectSerializer::save(temporary.path(), updated).ok);
      }
      QVERIFY(QFileInfo::exists(pendingMarker(temporary.path())));

      int matchingRestore = 0;
      LoadOutcome failedRecovery;
      {
        ScopedProjectWriteFaultInjector recoveryFault(
            [&](const ProjectWriteBoundary &boundary) {
              if (boundary.purpose != ProjectWritePurpose::RecoveryRestore ||
                  boundary.stage != stage)
                return false;
              return matchingRestore++ == ordinal;
            });
        failedRecovery = ProjectSerializer::load(temporary.path());
      }

      const QString label =
          caseLabel(ProjectWritePurpose::RecoveryRestore, stage, ordinal);
      QVERIFY2(!failedRecovery.ok,
               qPrintable(label + QStringLiteral(" unexpectedly recovered")));
      QVERIFY(!failedRecovery.recovered);
      QVERIFY(QFileInfo::exists(pendingMarker(temporary.path())));

      const LoadOutcome retry = ProjectSerializer::load(temporary.path());
      QVERIFY2(retry.ok, qPrintable(label + QStringLiteral(" did not retry")));
      QVERIFY(retry.recovered);
      QCOMPARE(retry.project, original);
      QVERIFY(!QFileInfo::exists(pendingMarker(temporary.path())));
    }
  }
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
