#pragma once

#include "core/diagnostic_model.h"
#include "core/project_data.h"

#include <QList>
#include <QString>

namespace uuml {

enum class CppImportAction {
  Create,
  Update,
  Conflict,
  Unchanged,
  UserModified,
  MissingSource
};

QString toString(CppImportAction action);

struct CppImportOptions {
  static QString defaultInterfacePattern();
  static QStringList defaultOwningPointerTypes();
  static QStringList defaultSharedPointerTypes();

  QString interfacePattern = defaultInterfacePattern();
  QStringList owningPointerTypes = defaultOwningPointerTypes();
  QStringList sharedPointerTypes = defaultSharedPointerTypes();
  // Source-derived stereotypes are resolved from the project-owned catalog by
  // configureCppImportStereotypes(). Empty values deliberately disable the
  // rule, for example when a project has deleted the conventional definition.
  QString localTypeStereotypeId;
  QStringList localTypeStereotypeApplicableTo;
};

void configureCppImportStereotypes(CppImportOptions &options,
                                   const ProjectData &project);

// A compiler-owned description of one C++ record. Keeping discovery separate
// from planning allows an asynchronous preview to be safely re-planned against
// the latest user model immediately before Apply is pressed.
struct CppSourceSymbol {
  QString symbolId;
  QString qualifiedName;
  // Only language namespaces are represented here. Enclosing record names
  // remain part of qualifiedName so nested C++ types stay nested UML types
  // rather than being mistaken for packages.
  QString namespacePath;
  ElementType elementType = ElementType::Class;
  QStringList attributes;
  QStringList operations;
  QStringList baseSymbolIds;
  QString filePath;
  int line = 0;
  int column = 0;

  bool operator==(const CppSourceSymbol &) const = default;
};

// A semantic relationship inferred from C++ source. Both endpoints use Clang
// identities rather than names so namespace moves and same-named types do not
// accidentally connect unrelated model elements. evidenceKind describes the
// evidence that produced the relationship and is persisted for synchronization.
struct CppSourceRelationship {
  QString symbolId;
  QString sourceSymbolId;
  QString targetSymbolId;
  QString sourceName;
  QString targetName;
  QString evidenceKind;
  RelationshipType relationshipType = RelationshipType::Generalization;
  QString classificationReason;
  QString filePath;
  int line = 0;

  bool operator==(const CppSourceRelationship &) const = default;
};

struct CppImportItem {
  CppImportAction action = CppImportAction::Unchanged;
  CppSourceSymbol symbol;
  ModelElement desiredElement;
  QString existingElementId;
  QString message;

  bool isApplicable() const {
    return action == CppImportAction::Create ||
           action == CppImportAction::Update;
  }
};

struct CppRelationshipImportItem {
  CppImportAction action = CppImportAction::Unchanged;
  CppSourceRelationship source;
  Relationship desiredRelationship;
  QString existingRelationshipId;
  QString message;

  bool isApplicable() const {
    return action == CppImportAction::Create ||
           action == CppImportAction::Update;
  }
};

struct CppImportPreview {
  QString compilationDatabasePath;
  QString sourceRoot;
  QList<CppSourceSymbol> symbols;
  QList<CppSourceRelationship> relationships;
  QList<CppImportItem> items;
  QList<CppRelationshipImportItem> relationshipItems;
  // Discovery diagnostics are retained separately so Apply can re-plan
  // against current model state without duplicating Clang work or messages.
  QList<Diagnostic> discoveryDiagnostics;
  QList<Diagnostic> diagnostics;
  // Re-planning reuses Clang discovery but must retain the project-specific
  // source-derived stereotype policy used to materialize desired elements.
  CppImportOptions optionsUsed;
  bool usedCompilationDatabase = false;
  bool ok = false;

  int elementApplicableCount() const;
  int relationshipApplicableCount() const;
  int applicableCount() const;
  int conflictCount() const;
};

class CppImportService {
public:
  static bool available();
  static CppImportPreview
  preview(const QString &searchPath,
          const QList<ModelElement> &existingElements,
          const QList<Relationship> &existingRelationships = {},
          const CppImportOptions &options = {});
  static CppImportPreview
  replan(const CppImportPreview &discovery,
         const QList<ModelElement> &existingElements,
         const QList<Relationship> &existingRelationships = {});
  static int apply(ProjectData &project, const CppImportPreview &preview);
};

} // namespace uuml
