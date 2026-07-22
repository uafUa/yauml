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

  QString interfacePattern = defaultInterfacePattern();
};

// A compiler-owned description of one C++ record. Keeping discovery separate
// from planning allows an asynchronous preview to be safely re-planned against
// the latest user model immediately before Apply is pressed.
struct CppSourceSymbol {
  QString symbolId;
  QString qualifiedName;
  ElementType elementType = ElementType::Class;
  QStringList attributes;
  QStringList operations;
  QStringList baseSymbolIds;
  QString filePath;
  int line = 0;
  int column = 0;

  bool operator==(const CppSourceSymbol &) const = default;
};

// Direct inheritance discovered from a CXXBaseSpecifier. Both endpoints use
// Clang identities rather than names so namespace moves and same-named types do
// not accidentally connect unrelated model elements.
struct CppSourceInheritance {
  QString symbolId;
  QString derivedSymbolId;
  QString baseSymbolId;
  QString derivedName;
  QString baseName;
  RelationshipType relationshipType = RelationshipType::Generalization;
  QString classificationReason;
  QString filePath;
  int line = 0;

  bool operator==(const CppSourceInheritance &) const = default;
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
  CppSourceInheritance source;
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
  QList<CppSourceInheritance> inheritances;
  QList<CppImportItem> items;
  QList<CppRelationshipImportItem> relationshipItems;
  // Discovery diagnostics are retained separately so Apply can re-plan
  // against current model state without duplicating Clang work or messages.
  QList<Diagnostic> discoveryDiagnostics;
  QList<Diagnostic> diagnostics;
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
