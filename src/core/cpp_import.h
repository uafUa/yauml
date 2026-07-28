#pragma once

#include "core/diagnostic_model.h"
#include "core/project_data.h"

#include <QList>
#include <QString>
#include <functional>

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

enum class CppImportProgressStage {
  Preparing,
  DiscoveringSources,
  ParsingSources,
  AnalyzingModel,
  PlanningChanges
};

// Progress is intentionally independent from the GUI. The synchronous
// headless importer may ignore it, while asynchronous callers can marshal
// updates onto their UI thread. A zero total denotes an indeterminate phase.
struct CppImportProgress {
  CppImportProgressStage stage = CppImportProgressStage::Preparing;
  QString message;
  QString detail;
  int completed = 0;
  int total = 0;

  bool operator==(const CppImportProgress &) const = default;
};

using CppImportProgressCallback =
    std::function<void(const CppImportProgress &)>;

// Describes how a C++ member wrapper maps to UML. The template argument is
// one-based because that is how users refer to arguments in documentation
// (for example, value type 2 in std::map<Key, Value>).
struct CppMemberTypeRule {
  QString typeName;
  RelationshipType relationshipType = RelationshipType::Composition;
  QString multiplicity;
  int targetArgument = 1;

  bool operator==(const CppMemberTypeRule &) const = default;
};

struct CppImportOptions {
  static QString defaultInterfacePattern();
  static QList<CppMemberTypeRule> defaultMemberTypeRules();

  QString interfacePattern = defaultInterfacePattern();
  QList<CppMemberTypeRule> memberTypeRules = defaultMemberTypeRules();
  // Source-derived stereotypes are resolved from the project-owned catalog by
  // configureCppImportStereotypes(). Empty values deliberately disable the
  // rule, for example when a project has deleted the conventional definition.
  QString localTypeStereotypeId;
  QStringList localTypeStereotypeApplicableTo;
  QString privateTypeStereotypeId;
  QStringList privateTypeStereotypeApplicableTo;
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
  // Access belongs to the declaration itself rather than its enclosing type.
  // It is retained through planning so a private nested declaration can carry
  // a project-owned «private» stereotype without making access a UML type.
  bool privateNestedType = false;

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
  // Member fields annotate the source end, following the convention already
  // used by the diagram relationship editor.
  QString sourceRole;
  QString sourceMultiplicity;
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
  QStringList compilationDatabasePaths;
  // The exact roots selected by the user. sourceRoot remains the common
  // discovery root for compatibility with older callers and diagnostics.
  QStringList sourceRoots;
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
          const CppImportOptions &options = {},
          const CppImportProgressCallback &progress = {});
  static CppImportPreview
  preview(const QStringList &searchPaths,
          const QList<ModelElement> &existingElements,
          const QList<Relationship> &existingRelationships = {},
          const CppImportOptions &options = {},
          const CppImportProgressCallback &progress = {});
  static CppImportPreview
  replan(const CppImportPreview &discovery,
         const QList<ModelElement> &existingElements,
         const QList<Relationship> &existingRelationships = {});
  static int apply(ProjectData &project, const CppImportPreview &preview);
};

} // namespace uuml
