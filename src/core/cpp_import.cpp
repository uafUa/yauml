#include "core/cpp_import.h"

#include "core/cpp_import_matching.h"
#include "core/model_operation.h"
#include "core/stereotype_catalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

#ifndef YAUML_HAS_LIBCLANG
#define YAUML_HAS_LIBCLANG 0
#endif

#if YAUML_HAS_LIBCLANG
#include <clang-c/CXCompilationDatabase.h>
#include <clang-c/Index.h>
#endif

namespace yauml {
namespace {

constexpr auto kBindingKey = "sourceBinding";
constexpr auto kBindingLanguage = "cpp";
constexpr auto kBindingVersion = 3;
constexpr auto kManagedStereotypeIdsKey = "managedStereotypeIds";

Diagnostic importDiagnostic(DiagnosticSeverity severity, const QString &message,
                            const QString &elementId = {}) {
  return {severity, QStringLiteral("cpp-import"), message, elementId};
}

void reportProgress(const CppImportProgressCallback &callback,
                    CppImportProgressStage stage, const QString &message,
                    const QString &detail = {}, int completed = 0,
                    int total = 0) {
  if (callback)
    callback({stage, message, detail, completed, total});
}

QString normalizedPath(const QString &path) {
  QFileInfo info(path);
  const QString canonical = info.canonicalFilePath();
  return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath()
                                             : canonical);
}

QString comparablePath(const QString &path) {
  QString comparable = QDir::fromNativeSeparators(normalizedPath(path));
#ifdef Q_OS_WIN
  comparable = comparable.toLower();
#endif
  return comparable;
}

bool pathIsWithin(const QString &path, const QString &directory) {
  const QString candidate = comparablePath(path);
  QString root = comparablePath(directory);
  if (!root.endsWith(u'/'))
    root += u'/';
  return candidate == root.chopped(1) || candidate.startsWith(root);
}

bool pathIsWithinAny(const QString &path, const QStringList &directories) {
  return std::any_of(
      directories.cbegin(), directories.cend(),
      [&](const QString &directory) { return pathIsWithin(path, directory); });
}

void removeRelationshipAndPresentations(ProjectData &project,
                                        const QString &relationshipId) {
  project.relationships.erase(
      std::remove_if(project.relationships.begin(), project.relationships.end(),
                     [&](const Relationship &relationship) {
                       return relationship.id == relationshipId;
                     }),
      project.relationships.end());
  for (auto &diagram : project.diagrams) {
    diagram.connectors.erase(
        std::remove_if(diagram.connectors.begin(), diagram.connectors.end(),
                       [&](const ConnectorPresentation &connector) {
                         return connector.relationshipId == relationshipId;
                       }),
        diagram.connectors.end());
  }
}

// Headless import uses the same cleanup semantics as the undoable GUI command.
// Keep this mutation focused on references owned by an element; unrelated
// project data and presentation geometry remain untouched.
void removeElementAndReferences(ProjectData &project,
                                const QString &elementId) {
  const ModelElement *removed = findElement(project, elementId);
  if (!removed)
    return;
  const QString parentPackageId = removed->packageId;
  const QString parentTypeId = removed->enclosingTypeId;

  QStringList relationshipIds;
  for (const auto &relationship : std::as_const(project.relationships)) {
    if (relationship.sourceId == elementId ||
        relationship.targetId == elementId)
      relationshipIds.append(relationship.id);
  }
  for (const QString &relationshipId : std::as_const(relationshipIds))
    removeRelationshipAndPresentations(project, relationshipId);

  for (auto &diagram : project.diagrams) {
    QSet<QString> removedPresentationIds;
    diagram.nodes.erase(std::remove_if(diagram.nodes.begin(),
                                       diagram.nodes.end(),
                                       [&](const NodePresentation &node) {
                                         if (node.elementId != elementId)
                                           return false;
                                         removedPresentationIds.insert(node.id);
                                         return true;
                                       }),
                        diagram.nodes.end());
    diagram.containers.erase(
        std::remove_if(diagram.containers.begin(), diagram.containers.end(),
                       [&](const ContainerPresentation &container) {
                         if (container.subjectKind !=
                                 QStringLiteral("package") ||
                             container.subjectId != elementId)
                           return false;
                         removedPresentationIds.insert(container.id);
                         return true;
                       }),
        diagram.containers.end());
    if (!removedPresentationIds.isEmpty()) {
      for (auto &container : diagram.containers) {
        for (const QString &presentationId :
             std::as_const(removedPresentationIds))
          container.childPresentationIds.removeAll(presentationId);
      }
    }
  }

  for (auto &element : project.elements) {
    if (element.id == elementId)
      continue;
    if (element.packageId == elementId)
      element.packageId = parentPackageId;
    if (element.enclosingTypeId == elementId)
      element.enclosingTypeId = parentTypeId;
    if (element.browserParent.kind == QStringLiteral("element") &&
        element.browserParent.id == elementId)
      element.browserParent = {};
  }
  for (auto &folder : project.browserFolders) {
    if (folder.parent.kind == QStringLiteral("element") &&
        folder.parent.id == elementId)
      folder.parent = {QStringLiteral("model"), {}};
  }
  project.browserItemOrder.removeAll(
      QStringLiteral("element:%1").arg(elementId));
  project.elements.erase(std::remove_if(project.elements.begin(),
                                        project.elements.end(),
                                        [&](const ModelElement &element) {
                                          return element.id == elementId;
                                        }),
                         project.elements.end());
}

QStringList normalizeSourceRoots(const QStringList &searchPaths,
                                 QList<Diagnostic> &diagnostics) {
  QStringList roots;
  QSet<QString> seen;
  for (const QString &searchPath : searchPaths) {
    const QString trimmed = searchPath.trimmed();
    if (trimmed.isEmpty())
      continue;
    const QFileInfo input(trimmed);
    if (!input.exists() || !input.isDir()) {
      diagnostics.append(importDiagnostic(
          DiagnosticSeverity::Error,
          QStringLiteral("The selected C++ source directory does not exist: %1")
              .arg(trimmed)));
      continue;
    }
    const QString normalized = normalizedPath(input.absoluteFilePath());
    const QString comparable = comparablePath(normalized);
    if (!seen.contains(comparable)) {
      roots.append(normalized);
      seen.insert(comparable);
    }
  }

  // Selecting both a parent and one of its descendants must not scan or parse
  // the descendant twice. Prefer the broader root regardless of click order.
  QStringList minimalRoots;
  for (const QString &candidate : std::as_const(roots)) {
    const bool covered =
        std::any_of(roots.cbegin(), roots.cend(), [&](const QString &other) {
          return comparablePath(candidate) != comparablePath(other) &&
                 pathIsWithin(candidate, other);
        });
    if (!covered)
      minimalRoots.append(candidate);
  }
  return minimalRoots;
}

QString findCompilationDatabase(const QString &searchPath,
                                QList<Diagnostic> &diagnostics,
                                bool reportMissing) {
  const QFileInfo input(searchPath);
  if (input.isFile() &&
      input.fileName().compare(QStringLiteral("compile_commands.json"),
                               Qt::CaseInsensitive) == 0)
    return normalizedPath(input.absoluteFilePath());

  QDir directory(input.isDir() ? input.absoluteFilePath()
                               : input.absolutePath());
  QStringList candidates;
  QList<QPair<QString, int>> pending{{directory.absolutePath(), 0}};
  while (!pending.isEmpty()) {
    const auto [currentPath, depth] = pending.takeFirst();
    const QDir current(currentPath);
    const QString candidate =
        current.filePath(QStringLiteral("compile_commands.json"));
    if (QFileInfo::exists(candidate))
      candidates.append(normalizedPath(candidate));
    if (depth >= 4)
      continue;
    for (const QFileInfo &child :
         current.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                               QDir::Name | QDir::IgnoreCase)) {
      if (child.fileName() == QStringLiteral(".git"))
        continue;
      pending.append({child.absoluteFilePath(), depth + 1});
    }
  }
  candidates.removeDuplicates();
  std::sort(candidates.begin(), candidates.end());
  if (!candidates.isEmpty()) {
    if (candidates.size() > 1) {
      diagnostics.append(importDiagnostic(
          DiagnosticSeverity::Warning,
          QStringLiteral("Several compilation databases were found; using %1")
              .arg(candidates.first())));
    }
    return candidates.first();
  }

  // Selecting a nested source directory should still find a database at the
  // project root, but a database inside the explicitly selected tree takes
  // precedence over an unrelated ancestor.
  QDir parent = directory;
  while (parent.cdUp()) {
    const QString candidate =
        parent.filePath(QStringLiteral("compile_commands.json"));
    if (QFileInfo::exists(candidate))
      return normalizedPath(candidate);
  }
  if (reportMissing) {
    diagnostics.append(importDiagnostic(
        DiagnosticSeverity::Error,
        QStringLiteral("No compile_commands.json was found at or below %1")
            .arg(directory.absolutePath())));
  }
  return {};
}

QJsonObject elementSnapshot(const ModelElement &element) {
  QJsonObject snapshot;
  snapshot.insert(QStringLiteral("type"), toString(element.type));
  snapshot.insert(QStringLiteral("name"), element.name);
  snapshot.insert(QStringLiteral("attributes"),
                  QJsonArray::fromStringList(element.attributes));
  snapshot.insert(QStringLiteral("operations"),
                  modelOperationsToJson(element.operations));
  snapshot.insert(QStringLiteral("packageId"), element.packageId);
  snapshot.insert(QStringLiteral("enclosingTypeId"), element.enclosingTypeId);
  return snapshot;
}

QStringList stringList(const QJsonValue &value) {
  QStringList result;
  for (const auto &entry : value.toArray())
    if (entry.isString())
      result.append(entry.toString());
  return result;
}

ModelElement elementFromSnapshot(const QJsonObject &snapshot) {
  ModelElement element;
  bool typeOk = false;
  element.type = elementTypeFromString(
      snapshot.value(QStringLiteral("type")).toString(), &typeOk);
  if (!typeOk)
    element.type = ElementType::Class;
  element.name = snapshot.value(QStringLiteral("name")).toString();
  element.attributes = stringList(snapshot.value(QStringLiteral("attributes")));
  element.operations =
      modelOperationsFromJson(snapshot.value(QStringLiteral("operations")))
          .value_or(QList<ModelOperation>{});
  element.packageId = snapshot.value(QStringLiteral("packageId")).toString();
  element.enclosingTypeId =
      snapshot.value(QStringLiteral("enclosingTypeId")).toString();
  return element;
}

bool sourceOwnedStateEquals(const ModelElement &left,
                            const ModelElement &right) {
  return left.type == right.type && left.name == right.name &&
         left.attributes == right.attributes &&
         modelOperationsSemanticallyEqual(left.operations, right.operations) &&
         left.packageId == right.packageId &&
         left.enclosingTypeId == right.enclosingTypeId;
}

QJsonObject relationshipSnapshot(const Relationship &relationship) {
  QJsonObject snapshot;
  snapshot.insert(QStringLiteral("type"), toString(relationship.type));
  snapshot.insert(QStringLiteral("name"), relationship.name);
  snapshot.insert(QStringLiteral("sourceId"), relationship.sourceId);
  snapshot.insert(QStringLiteral("targetId"), relationship.targetId);
  snapshot.insert(QStringLiteral("sourceRole"), relationship.sourceEnd.role);
  snapshot.insert(QStringLiteral("sourceMultiplicity"),
                  relationship.sourceEnd.multiplicity);
  return snapshot;
}

Relationship relationshipFromSnapshot(const QJsonObject &snapshot) {
  Relationship relationship;
  bool typeOk = false;
  relationship.type = relationshipTypeFromString(
      snapshot.value(QStringLiteral("type")).toString(), &typeOk);
  if (!typeOk)
    relationship.type = RelationshipType::Generalization;
  relationship.name = snapshot.value(QStringLiteral("name")).toString();
  relationship.sourceId = snapshot.value(QStringLiteral("sourceId")).toString();
  relationship.targetId = snapshot.value(QStringLiteral("targetId")).toString();
  relationship.sourceEnd.role =
      snapshot.value(QStringLiteral("sourceRole")).toString();
  relationship.sourceEnd.multiplicity =
      snapshot.value(QStringLiteral("sourceMultiplicity")).toString();
  return relationship;
}

bool sourceOwnedStateEquals(const Relationship &left,
                            const Relationship &right) {
  return left.type == right.type && left.name == right.name &&
         left.sourceId == right.sourceId && left.targetId == right.targetId &&
         left.sourceEnd.role == right.sourceEnd.role &&
         left.sourceEnd.multiplicity == right.sourceEnd.multiplicity;
}

QJsonObject sourceBinding(const ModelElement &element) {
  const QJsonValue value =
      element.extra.value(QString::fromLatin1(kBindingKey));
  if (!value.isObject())
    return {};
  const QJsonObject binding = value.toObject();
  return binding.value(QStringLiteral("language")).toString() ==
                 QString::fromLatin1(kBindingLanguage)
             ? binding
             : QJsonObject{};
}

QJsonObject sourceBinding(const Relationship &relationship) {
  const QJsonValue value =
      relationship.extra.value(QString::fromLatin1(kBindingKey));
  if (!value.isObject())
    return {};
  const QJsonObject binding = value.toObject();
  return binding.value(QStringLiteral("language")).toString() ==
                 QString::fromLatin1(kBindingLanguage)
             ? binding
             : QJsonObject{};
}

bool isCppImplementationFile(const QString &path);

ModelElement sourceElement(const CppSourceSymbol &symbol,
                           const QString &desiredId, const QString &packageId,
                           const QString &enclosingTypeId,
                           const CppImportOptions &options,
                           const ModelElement *existing = nullptr) {
  ModelElement element = existing ? *existing : ModelElement{};
  if (!existing)
    element.id = desiredId;

  // Only IDs explicitly recorded as import-managed are removed here. All
  // other assignments remain user-owned and survive every synchronization.
  const QJsonObject previousBinding =
      existing ? sourceBinding(*existing) : QJsonObject{};
  const QStringList previouslyManaged = stringList(
      previousBinding.value(QString::fromLatin1(kManagedStereotypeIdsKey)));

  element.type = symbol.elementType;
  element.name = symbol.qualifiedName;
  element.packageId = packageId;
  element.enclosingTypeId = enclosingTypeId;
  element.attributes = symbol.attributes;
  element.operations = symbol.operations;
  element.enumLiterals.clear();

  QStringList managedStereotypeIds;
  const QString applicability =
      stereotype_catalog::applicabilityFor(symbol.elementType);
  if (symbol.elementType != ElementType::Package &&
      isCppImplementationFile(symbol.filePath) &&
      !options.localTypeStereotypeId.isEmpty() &&
      options.localTypeStereotypeApplicableTo.contains(applicability)) {
    managedStereotypeIds.append(options.localTypeStereotypeId);
  }
  if (symbol.privateNestedType && !options.privateTypeStereotypeId.isEmpty() &&
      options.privateTypeStereotypeApplicableTo.contains(applicability) &&
      !managedStereotypeIds.contains(options.privateTypeStereotypeId)) {
    managedStereotypeIds.append(options.privateTypeStereotypeId);
  }
  for (const QString &stereotypeId : previouslyManaged)
    if (!managedStereotypeIds.contains(stereotypeId))
      element.stereotypeIds.removeAll(stereotypeId);
  for (const QString &stereotypeId : managedStereotypeIds)
    if (!element.stereotypeIds.contains(stereotypeId))
      element.stereotypeIds.append(stereotypeId);

  QJsonObject binding;
  binding.insert(QStringLiteral("version"), kBindingVersion);
  binding.insert(QStringLiteral("language"),
                 QString::fromLatin1(kBindingLanguage));
  binding.insert(QStringLiteral("symbolId"), symbol.symbolId);
  binding.insert(QStringLiteral("qualifiedName"), symbol.qualifiedName);
  binding.insert(QStringLiteral("file"), symbol.filePath);
  binding.insert(QStringLiteral("line"), symbol.line);
  binding.insert(QStringLiteral("column"), symbol.column);
  binding.insert(QStringLiteral("lastImported"), elementSnapshot(element));
  if (!managedStereotypeIds.isEmpty()) {
    binding.insert(QString::fromLatin1(kManagedStereotypeIdsKey),
                   QJsonArray::fromStringList(managedStereotypeIds));
  }
  element.extra.insert(QString::fromLatin1(kBindingKey), binding);
  return element;
}

Relationship sourceRelationship(const CppSourceRelationship &source,
                                const QString &sourceElementId,
                                const QString &targetElementId,
                                const Relationship *existing = nullptr) {
  Relationship relationship = existing ? *existing : Relationship{};
  if (!existing)
    relationship.id = newId();
  relationship.type = source.relationshipType;
  // The relationship type and its endpoint decoration already carry the UML
  // semantics. A label is optional user content, not an import-generated
  // restatement such as "implements" or "contains".
  relationship.name.clear();
  relationship.sourceId = sourceElementId;
  relationship.targetId = targetElementId;
  relationship.sourceEnd.role = source.sourceRole;
  relationship.sourceEnd.multiplicity = source.sourceMultiplicity;

  QJsonObject binding;
  binding.insert(QStringLiteral("version"), kBindingVersion);
  binding.insert(QStringLiteral("language"),
                 QString::fromLatin1(kBindingLanguage));
  binding.insert(QStringLiteral("kind"), source.evidenceKind);
  binding.insert(QStringLiteral("symbolId"), source.symbolId);
  binding.insert(QStringLiteral("sourceSymbolId"), source.sourceSymbolId);
  binding.insert(QStringLiteral("targetSymbolId"), source.targetSymbolId);
  // Retain the old endpoint keys for inheritance bindings so projects written
  // by this version remain intelligible to older builds.
  if (source.evidenceKind == QStringLiteral("inheritance")) {
    binding.insert(QStringLiteral("derivedSymbolId"), source.sourceSymbolId);
    binding.insert(QStringLiteral("baseSymbolId"), source.targetSymbolId);
  }
  binding.insert(QStringLiteral("classification"),
                 toString(source.relationshipType));
  binding.insert(QStringLiteral("classificationReason"),
                 source.classificationReason);
  binding.insert(QStringLiteral("file"), source.filePath);
  binding.insert(QStringLiteral("line"), source.line);
  binding.insert(QStringLiteral("lastImported"),
                 relationshipSnapshot(relationship));
  relationship.extra.insert(QString::fromLatin1(kBindingKey), binding);
  return relationship;
}

CppImportPreview planImport(const CppImportPreview &discovery,
                            const QList<ModelElement> &existingElements,
                            const QList<Relationship> &existingRelationships) {
  CppImportPreview result = discovery;
  result.items.clear();
  result.relationshipItems.clear();
  result.diagnostics = result.discoveryDiagnostics;

  QHash<QString, const ModelElement *> byBinding;
  QHash<QString, const ModelElement *> byName;
  QSet<QString> duplicateBindings;
  for (const auto &element : existingElements) {
    byName.insert(element.name, &element);
    const QJsonObject binding = sourceBinding(element);
    const QString symbolId =
        binding.value(QStringLiteral("symbolId")).toString();
    if (symbolId.isEmpty())
      continue;
    if (byBinding.contains(symbolId)) {
      duplicateBindings.insert(symbolId);
      result.diagnostics.append(importDiagnostic(
          DiagnosticSeverity::Error,
          QStringLiteral("Multiple model elements are bound to C++ symbol %1")
              .arg(symbolId),
          element.id));
    } else {
      byBinding.insert(symbolId, &element);
    }
  }

  // Materialize C++ namespaces as ordinary UML packages. These synthetic
  // symbols participate in the same source-binding/conflict machinery as
  // imported types, so package moves remain user-authoritative.
  QHash<QString, CppSourceSymbol> packageSymbolByPath;
  for (const auto &symbol : result.symbols) {
    QString path = symbol.namespacePath;
    while (!path.isEmpty()) {
      if (!packageSymbolByPath.contains(path)) {
        CppSourceSymbol packageSymbol;
        packageSymbol.symbolId = QStringLiteral("cpp-namespace:%1").arg(path);
        packageSymbol.qualifiedName = path;
        packageSymbol.elementType = ElementType::Package;
        packageSymbol.filePath = symbol.filePath;
        packageSymbol.line = symbol.line;
        const int separator = path.lastIndexOf(QStringLiteral("::"));
        packageSymbol.namespacePath =
            separator >= 0 ? path.left(separator) : QString{};
        packageSymbolByPath.insert(path, std::move(packageSymbol));
      }
      const int separator = path.lastIndexOf(QStringLiteral("::"));
      path = separator >= 0 ? path.left(separator) : QString{};
    }
  }
  QList<CppSourceSymbol> plannedSymbols = packageSymbolByPath.values();
  std::stable_sort(
      plannedSymbols.begin(), plannedSymbols.end(),
      [](const CppSourceSymbol &left, const CppSourceSymbol &right) {
        const int leftDepth = left.qualifiedName.count(QStringLiteral("::"));
        const int rightDepth = right.qualifiedName.count(QStringLiteral("::"));
        return leftDepth == rightDepth
                   ? left.qualifiedName < right.qualifiedName
                   : leftDepth < rightDepth;
      });
  plannedSymbols.append(result.symbols);

  // Clang identities normally provide exact synchronization. A declaration
  // rename or namespace/file move can change that identity, so compare only
  // unmatched current declarations with unmatched prior import baselines.
  // User-edited model fields are never used as source evidence.
  QSet<QString> currentSymbolIds;
  for (const auto &symbol : plannedSymbols)
    currentSymbolIds.insert(symbol.symbolId);

  QList<CppImportedDeclaration> importedMatchCandidates;
  for (const auto &element : existingElements) {
    const QJsonObject binding = sourceBinding(element);
    const QString symbolId =
        binding.value(QStringLiteral("symbolId")).toString();
    const QJsonObject lastObject =
        binding.value(QStringLiteral("lastImported")).toObject();
    if (symbolId.isEmpty() || currentSymbolIds.contains(symbolId) ||
        duplicateBindings.contains(symbolId) || lastObject.isEmpty())
      continue;
    const ModelElement baseline = elementFromSnapshot(lastObject);
    if (baseline.type == ElementType::Package)
      continue;
    importedMatchCandidates.append(
        {element.id, symbolId, baseline.name,
         binding.value(QStringLiteral("file")).toString(),
         binding.value(QStringLiteral("line")).toInt(), baseline.type,
         baseline.attributes, baseline.operations});
  }

  QList<CppSourceSymbol> sourceMatchCandidates;
  for (const auto &symbol : result.symbols) {
    if (byBinding.contains(symbol.symbolId) ||
        duplicateBindings.contains(symbol.symbolId))
      continue;
    const ModelElement *sameName = byName.value(symbol.qualifiedName, nullptr);
    if (sameName && sourceBinding(*sameName).isEmpty())
      continue;
    sourceMatchCandidates.append(symbol);
  }

  QHash<QString, CppDeclarationMatch> declarationMatchBySourceSymbol;
  QSet<QString> matchedPreviousSymbolIds;
  for (const auto &match : matchRenamedCppDeclarations(
           sourceMatchCandidates, importedMatchCandidates)) {
    declarationMatchBySourceSymbol.insert(match.sourceSymbolId, match);
    matchedPreviousSymbolIds.insert(match.previousSymbolId);
  }

  // A namespace has no independent body to compare. Infer its identity only
  // from already-unambiguous declaration matches below it, and require a
  // one-to-one mapping in both directions. Namespace merges and splits remain
  // explicit new/missing package records.
  QHash<QString, const ModelElement *> elementById;
  for (const auto &element : existingElements)
    elementById.insert(element.id, &element);
  QHash<QString, QSet<QString>> oldPackageIdsByNewSymbol;
  QHash<QString, QSet<QString>> newSymbolsByOldPackageId;
  for (const auto &source : result.symbols) {
    if (duplicateBindings.contains(source.symbolId))
      continue;
    const ModelElement *previous = byBinding.value(source.symbolId, nullptr);
    if (!previous) {
      const auto match =
          declarationMatchBySourceSymbol.constFind(source.symbolId);
      if (match != declarationMatchBySourceSymbol.cend()) {
        previous = byBinding.value(match->previousSymbolId, nullptr);
      }
    }
    if (!previous)
      continue;
    const QJsonObject previousBaseline =
        sourceBinding(*previous)
            .value(QStringLiteral("lastImported"))
            .toObject();
    QString oldPackageId = elementFromSnapshot(previousBaseline).packageId;
    QString newNamespacePath = source.namespacePath;
    while (!oldPackageId.isEmpty() && !newNamespacePath.isEmpty()) {
      const ModelElement *oldPackage = elementById.value(oldPackageId, nullptr);
      if (!oldPackage)
        break;
      const QJsonObject oldBinding = sourceBinding(*oldPackage);
      const QString oldPackageSymbol =
          oldBinding.value(QStringLiteral("symbolId")).toString();
      const ModelElement oldBaseline = elementFromSnapshot(
          oldBinding.value(QStringLiteral("lastImported")).toObject());
      const QString newPackageSymbol =
          QStringLiteral("cpp-namespace:%1").arg(newNamespacePath);
      if (oldBaseline.type != ElementType::Package ||
          oldPackageSymbol.isEmpty() ||
          currentSymbolIds.contains(oldPackageSymbol) ||
          duplicateBindings.contains(oldPackageSymbol) ||
          !packageSymbolByPath.contains(newNamespacePath))
        break;
      oldPackageIdsByNewSymbol[newPackageSymbol].insert(oldPackageId);
      newSymbolsByOldPackageId[oldPackageId].insert(newPackageSymbol);

      const qsizetype separator =
          newNamespacePath.lastIndexOf(QStringLiteral("::"));
      newNamespacePath =
          separator >= 0 ? newNamespacePath.left(separator) : QString{};
      oldPackageId = oldBaseline.packageId;
    }
  }
  for (auto candidate = oldPackageIdsByNewSymbol.cbegin();
       candidate != oldPackageIdsByNewSymbol.cend(); ++candidate) {
    if (candidate.value().size() != 1 || byBinding.contains(candidate.key()))
      continue;
    const QString oldPackageId = *candidate.value().cbegin();
    if (newSymbolsByOldPackageId.value(oldPackageId).size() != 1)
      continue;
    const ModelElement *oldPackage = elementById.value(oldPackageId, nullptr);
    if (!oldPackage)
      continue;
    const QString oldPackageSymbol =
        sourceBinding(*oldPackage).value(QStringLiteral("symbolId")).toString();
    declarationMatchBySourceSymbol.insert(
        candidate.key(),
        {candidate.key(), oldPackageSymbol, oldPackageId, 100});
    matchedPreviousSymbolIds.insert(oldPackageSymbol);
  }

  QHash<QString, QString> plannedElementIdBySymbol;
  QHash<QString, QString> packageIdByNamespacePath;
  QHash<QString, QString> typeIdByQualifiedName;
  for (const auto &symbol : plannedSymbols) {
    QString id;
    if (const auto *bound = byBinding.value(symbol.symbolId, nullptr)) {
      if (!duplicateBindings.contains(symbol.symbolId))
        id = bound->id;
    } else if (const auto match =
                   declarationMatchBySourceSymbol.constFind(symbol.symbolId);
               match != declarationMatchBySourceSymbol.cend()) {
      id = match->elementId;
    } else if (const auto *sameName =
                   byName.value(symbol.qualifiedName, nullptr)) {
      // The same-name declaration remains an explicit conflict and is never
      // rebound automatically. Reusing its ID only for dependency planning
      // keeps child package/enclosing references valid if the user chooses to
      // apply otherwise-independent descendants.
      if (sourceBinding(*sameName).isEmpty() &&
          sameName->type == symbol.elementType)
        id = sameName->id;
      else
        id = newId();
    } else {
      id = newId();
    }
    if (!id.isEmpty())
      plannedElementIdBySymbol.insert(symbol.symbolId, id);
    if (symbol.elementType == ElementType::Package && !id.isEmpty())
      packageIdByNamespacePath.insert(symbol.qualifiedName, id);
    else if (!id.isEmpty() &&
             !typeIdByQualifiedName.contains(symbol.qualifiedName))
      typeIdByQualifiedName.insert(symbol.qualifiedName, id);
  }
  const auto enclosingTypeIdFor = [&](const CppSourceSymbol &symbol) {
    if (symbol.elementType == ElementType::Package)
      return QString{};
    const auto parentOf = [](const QString &qualifiedName) {
      const qsizetype separator =
          qualifiedName.lastIndexOf(QStringLiteral("::"));
      return separator >= 0 ? qualifiedName.left(separator) : QString{};
    };
    QString parentName = parentOf(symbol.qualifiedName);
    while (!parentName.isEmpty() && parentName != symbol.namespacePath) {
      const QString ownerId = typeIdByQualifiedName.value(parentName);
      if (!ownerId.isEmpty())
        return ownerId;
      parentName = parentOf(parentName);
    }
    return QString{};
  };

  QSet<QString> discoveredIds;
  for (const auto &symbol : plannedSymbols) {
    discoveredIds.insert(symbol.symbolId);
    CppImportItem item;
    item.symbol = symbol;

    if (duplicateBindings.contains(symbol.symbolId)) {
      item.action = CppImportAction::Conflict;
      item.message =
          QStringLiteral("Duplicate source binding requires manual repair");
      result.items.append(std::move(item));
      continue;
    }

    const auto declarationMatch =
        declarationMatchBySourceSymbol.constFind(symbol.symbolId);
    const bool identityMatched =
        declarationMatch != declarationMatchBySourceSymbol.cend();
    const ModelElement *existing = byBinding.value(symbol.symbolId, nullptr);
    if (!existing && identityMatched) {
      existing = byBinding.value(declarationMatch->previousSymbolId, nullptr);
      if (existing) {
        result.diagnostics.append(importDiagnostic(
            DiagnosticSeverity::Info,
            QStringLiteral("Matched C++ declaration %1 to the previously "
                           "imported %2 after a rename or move")
                .arg(symbol.qualifiedName,
                     sourceBinding(*existing)
                         .value(QStringLiteral("qualifiedName"))
                         .toString()),
            existing->id));
      }
    }
    if (!existing) {
      const ModelElement *sameName =
          byName.value(symbol.qualifiedName, nullptr);
      if (sameName && sourceBinding(*sameName).isEmpty()) {
        item.action = CppImportAction::Conflict;
        item.existingElementId = sameName->id;
        item.existingElement = *sameName;
        item.desiredElement = sourceElement(
            symbol, sameName->id,
            packageIdByNamespacePath.value(symbol.namespacePath),
            enclosingTypeIdFor(symbol), result.optionsUsed, sameName);
        item.message =
            QStringLiteral("An unbound user element already has this name; it "
                           "was not rebound automatically");
        result.diagnostics.append(importDiagnostic(
            DiagnosticSeverity::Warning,
            QStringLiteral(
                "C++ symbol %1 conflicts with an unbound user element")
                .arg(symbol.qualifiedName),
            sameName->id));
      } else {
        item.action = CppImportAction::Create;
        item.desiredElement = sourceElement(
            symbol, plannedElementIdBySymbol.value(symbol.symbolId),
            packageIdByNamespacePath.value(symbol.namespacePath),
            enclosingTypeIdFor(symbol), result.optionsUsed);
        item.message = symbol.elementType == ElementType::Package
                           ? QStringLiteral("New C++ namespace package")
                           : QStringLiteral("New C++ type");
      }
      result.items.append(std::move(item));
      continue;
    }

    item.existingElementId = existing->id;
    item.existingElement = *existing;
    item.desiredElement =
        sourceElement(symbol, existing->id,
                      packageIdByNamespacePath.value(symbol.namespacePath),
                      enclosingTypeIdFor(symbol), result.optionsUsed, existing);
    const QJsonObject binding = sourceBinding(*existing);
    const QJsonObject lastObject =
        binding.value(QStringLiteral("lastImported")).toObject();
    if (lastObject.isEmpty()) {
      item.action = CppImportAction::Conflict;
      item.message = QStringLiteral("The binding has no import baseline");
      result.diagnostics.append(importDiagnostic(
          DiagnosticSeverity::Warning,
          QStringLiteral("C++ binding for %1 has no last-imported snapshot")
              .arg(symbol.qualifiedName),
          existing->id));
      result.items.append(std::move(item));
      continue;
    }

    const ModelElement lastImported = elementFromSnapshot(lastObject);
    const bool userChanged = !sourceOwnedStateEquals(*existing, lastImported);
    const bool sourceChanged =
        !sourceOwnedStateEquals(item.desiredElement, lastImported);
    const bool converged =
        sourceOwnedStateEquals(*existing, item.desiredElement);
    const QStringList previouslyManaged = stringList(
        binding.value(QString::fromLatin1(kManagedStereotypeIdsKey)));
    const QJsonObject desiredBinding = sourceBinding(item.desiredElement);
    const QStringList currentlyManaged = stringList(
        desiredBinding.value(QString::fromLatin1(kManagedStereotypeIdsKey)));
    const bool sourceStereotypesChanged =
        existing->stereotypeIds != item.desiredElement.stereotypeIds ||
        previouslyManaged != currentlyManaged;
    if (userChanged && sourceChanged && !converged) {
      item.action = CppImportAction::Conflict;
      item.message = QStringLiteral("Source and user edits both changed this "
                                    "type; user version retained");
      result.diagnostics.append(importDiagnostic(
          DiagnosticSeverity::Warning,
          QStringLiteral("C++ import conflict for %1; the user-edited model "
                         "remains authoritative")
              .arg(symbol.qualifiedName),
          existing->id));
    } else if (userChanged && !sourceChanged &&
               (sourceStereotypesChanged || identityMatched)) {
      // Apply the explicit source-derived classification without overwriting
      // unrelated user edits to source-synchronized fields. A matched
      // file-only move similarly refreshes provenance while preserving the
      // user-owned model. The baseline remains the source snapshot, so later
      // conflicts are still detected.
      ModelElement stereotypeOnlyUpdate = *existing;
      if (sourceStereotypesChanged) {
        stereotypeOnlyUpdate.stereotypeIds = item.desiredElement.stereotypeIds;
      }
      stereotypeOnlyUpdate.extra = item.desiredElement.extra;
      item.desiredElement = std::move(stereotypeOnlyUpdate);
      item.action = CppImportAction::Update;
      item.message = identityMatched
                         ? QStringLiteral("Refresh matched C++ declaration "
                                          "binding; user-edited model retained")
                         : QStringLiteral("Refresh source-derived stereotypes; "
                                          "user-edited model retained");
    } else if (userChanged && !sourceChanged) {
      item.action = CppImportAction::UserModified;
      item.message = QStringLiteral("User-edited model retained");
    } else if (identityMatched || sourceChanged || sourceStereotypesChanged ||
               (converged && userChanged)) {
      item.action = CppImportAction::Update;
      if (identityMatched)
        item.message = QStringLiteral("Matched C++ declaration rename or move");
      else if (sourceStereotypesChanged && !sourceChanged)
        item.message = QStringLiteral("Source-derived stereotypes changed");
      else
        item.message = converged ? QStringLiteral("Refresh source baseline")
                                 : QStringLiteral("Source changed");
    } else {
      item.action = CppImportAction::Unchanged;
      item.message = QStringLiteral("Already synchronized");
    }
    result.items.append(std::move(item));
  }

  for (const auto &element : existingElements) {
    const QJsonObject binding = sourceBinding(element);
    const QString symbolId =
        binding.value(QStringLiteral("symbolId")).toString();
    if (symbolId.isEmpty() || discoveredIds.contains(symbolId) ||
        matchedPreviousSymbolIds.contains(symbolId))
      continue;
    CppImportItem item;
    item.action = CppImportAction::MissingSource;
    item.existingElementId = element.id;
    item.desiredElement = element;
    item.symbol.symbolId = symbolId;
    item.symbol.qualifiedName = element.name;
    item.symbol.filePath = binding.value(QStringLiteral("file")).toString();
    item.symbol.line = binding.value(QStringLiteral("line")).toInt();
    const bool sourceRootRemoved =
        !item.symbol.filePath.isEmpty() &&
        pathIsWithinAny(item.symbol.filePath, result.previousSourceRoots) &&
        !pathIsWithinAny(item.symbol.filePath, result.sourceRoots);
    item.action = sourceRootRemoved ? CppImportAction::OutOfScope
                                    : CppImportAction::MissingSource;
    item.message =
        sourceRootRemoved
            ? QStringLiteral("Source is outside the selected folders; choose "
                             "whether to remove it or keep it as manual")
            : QStringLiteral(
                  "Previously imported declaration was not matched in this "
                  "scan; kept in the model for safety");
    result.items.append(std::move(item));
  }

  // Resolve relationship endpoints only after element planning has assigned
  // IDs to newly imported declarations. Relationships remain semantic model
  // data; diagram connectors are created later by the existing presentation
  // logic when both endpoint elements are placed on a diagram.
  QHash<QString, QString> elementIdBySymbol;
  for (auto binding = byBinding.cbegin(); binding != byBinding.cend();
       ++binding) {
    if (!duplicateBindings.contains(binding.key()))
      elementIdBySymbol.insert(binding.key(), binding.value()->id);
  }
  for (const auto &item : result.items) {
    if (item.action == CppImportAction::Create)
      elementIdBySymbol.insert(item.symbol.symbolId, item.desiredElement.id);
    else if (declarationMatchBySourceSymbol.contains(item.symbol.symbolId) &&
             !item.existingElementId.isEmpty())
      elementIdBySymbol.insert(item.symbol.symbolId, item.existingElementId);
  }

  QHash<QString, const Relationship *> relationshipsByBinding;
  QHash<QString, QString> elementNameById;
  for (const auto &element : existingElements)
    elementNameById.insert(element.id, element.name);
  for (const auto &item : result.items)
    if (item.action == CppImportAction::Create)
      elementNameById.insert(item.desiredElement.id, item.desiredElement.name);

  QSet<QString> duplicateRelationshipBindings;
  for (const auto &relationship : existingRelationships) {
    const QJsonObject binding = sourceBinding(relationship);
    const QString kind = binding.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("inheritance") &&
        kind != QStringLiteral("containment") &&
        kind != QStringLiteral("member") && kind != QStringLiteral("signature"))
      continue;
    const QString symbolId =
        binding.value(QStringLiteral("symbolId")).toString();
    if (symbolId.isEmpty())
      continue;
    if (relationshipsByBinding.contains(symbolId)) {
      duplicateRelationshipBindings.insert(symbolId);
      result.diagnostics.append(importDiagnostic(
          DiagnosticSeverity::Error,
          QStringLiteral("Multiple model relationships are bound to C++ "
                         "source relationship %1")
              .arg(symbolId),
          relationship.id));
    } else {
      relationshipsByBinding.insert(symbolId, &relationship);
    }
  }

  QSet<QString> currentRelationshipSymbolIds;
  for (const auto &source : result.relationships)
    currentRelationshipSymbolIds.insert(source.symbolId);
  QSet<QString> matchedPreviousRelationshipIds;
  QSet<QString> matchedRelationshipModelIds;

  QSet<QString> discoveredRelationshipIds;
  for (const auto &source : result.relationships) {
    discoveredRelationshipIds.insert(source.symbolId);
    CppRelationshipImportItem item;
    item.source = source;

    if (duplicateRelationshipBindings.contains(source.symbolId)) {
      item.action = CppImportAction::Conflict;
      item.message =
          QStringLiteral("Duplicate source binding requires manual repair");
      result.relationshipItems.append(std::move(item));
      continue;
    }

    const QString sourceElementId =
        elementIdBySymbol.value(source.sourceSymbolId);
    const QString targetElementId =
        elementIdBySymbol.value(source.targetSymbolId);
    if (sourceElementId.isEmpty() || targetElementId.isEmpty()) {
      item.action = CppImportAction::Conflict;
      item.message = QStringLiteral(
          "A relationship endpoint could not be bound to a model element");
      result.diagnostics.append(importDiagnostic(
          DiagnosticSeverity::Warning,
          QStringLiteral("C++ relationship %1 → %2 was not imported because "
                         "an endpoint has an unresolved element conflict")
              .arg(source.sourceName, source.targetName)));
      result.relationshipItems.append(std::move(item));
      continue;
    }

    const Relationship *existing =
        relationshipsByBinding.value(source.symbolId, nullptr);
    bool identityMatched = false;
    if (!existing) {
      QList<const Relationship *> candidates;
      for (const auto &candidate : existingRelationships) {
        const QJsonObject binding = sourceBinding(candidate);
        const QString previousSymbolId =
            binding.value(QStringLiteral("symbolId")).toString();
        if (previousSymbolId.isEmpty() ||
            currentRelationshipSymbolIds.contains(previousSymbolId) ||
            duplicateRelationshipBindings.contains(previousSymbolId) ||
            matchedRelationshipModelIds.contains(candidate.id) ||
            binding.value(QStringLiteral("kind")).toString() !=
                source.evidenceKind)
          continue;
        const QJsonObject lastObject =
            binding.value(QStringLiteral("lastImported")).toObject();
        if (lastObject.isEmpty())
          continue;
        const Relationship baseline = relationshipFromSnapshot(lastObject);
        if (baseline.sourceId != sourceElementId ||
            baseline.targetId != targetElementId)
          continue;
        if (source.evidenceKind == QStringLiteral("member") &&
            baseline.sourceEnd.role != source.sourceRole)
          continue;
        candidates.append(&candidate);
      }
      if (candidates.size() == 1) {
        existing = candidates.first();
        identityMatched = true;
        const QString previousSymbolId = sourceBinding(*existing)
                                             .value(QStringLiteral("symbolId"))
                                             .toString();
        matchedPreviousRelationshipIds.insert(previousSymbolId);
        matchedRelationshipModelIds.insert(existing->id);
        result.diagnostics.append(importDiagnostic(
            DiagnosticSeverity::Info,
            QStringLiteral("Matched C++ relationship %1 → %2 after a "
                           "declaration rename or move")
                .arg(source.sourceName, source.targetName),
            existing->id));
      }
    }
    if (!existing) {
      const auto sameRelationship = std::find_if(
          existingRelationships.cbegin(), existingRelationships.cend(),
          [&](const Relationship &relationship) {
            return sourceBinding(relationship).isEmpty() &&
                   relationship.type == source.relationshipType &&
                   relationship.sourceId == sourceElementId &&
                   relationship.targetId == targetElementId;
          });
      if (sameRelationship != existingRelationships.cend()) {
        item.action = CppImportAction::Conflict;
        item.existingRelationshipId = sameRelationship->id;
        item.existingRelationship = *sameRelationship;
        item.desiredRelationship = sourceRelationship(
            source, sourceElementId, targetElementId, &*sameRelationship);
        item.message = QStringLiteral(
            "An unbound user relationship already represents this source "
            "relationship; it was not rebound automatically");
        result.diagnostics.append(importDiagnostic(
            DiagnosticSeverity::Warning,
            QStringLiteral("C++ relationship %1 → %2 conflicts with an "
                           "unbound user relationship")
                .arg(source.sourceName, source.targetName),
            sameRelationship->id));
      } else {
        item.action = CppImportAction::Create;
        item.desiredRelationship =
            sourceRelationship(source, sourceElementId, targetElementId);
        item.message = QStringLiteral("New C++ %1 relationship")
                           .arg(toString(source.relationshipType));
      }
      result.relationshipItems.append(std::move(item));
      continue;
    }

    item.existingRelationshipId = existing->id;
    item.existingRelationship = *existing;
    item.desiredRelationship =
        sourceRelationship(source, sourceElementId, targetElementId, existing);
    const QJsonObject binding = sourceBinding(*existing);
    const QJsonObject lastObject =
        binding.value(QStringLiteral("lastImported")).toObject();
    if (lastObject.isEmpty()) {
      item.action = CppImportAction::Conflict;
      item.message = QStringLiteral("The binding has no import baseline");
      result.diagnostics.append(importDiagnostic(
          DiagnosticSeverity::Warning,
          QStringLiteral("C++ relationship binding for %1 → %2 has no "
                         "last-imported snapshot")
              .arg(source.sourceName, source.targetName),
          existing->id));
      result.relationshipItems.append(std::move(item));
      continue;
    }

    const Relationship lastImported = relationshipFromSnapshot(lastObject);
    const bool userChanged = !sourceOwnedStateEquals(*existing, lastImported);
    const bool sourceChanged =
        !sourceOwnedStateEquals(item.desiredRelationship, lastImported);
    const bool converged =
        sourceOwnedStateEquals(*existing, item.desiredRelationship);
    const QJsonObject desiredBinding = sourceBinding(item.desiredRelationship);
    const bool sourceBindingChanged = binding != desiredBinding;
    if (userChanged && sourceChanged && !converged) {
      item.action = CppImportAction::Conflict;
      item.message = QStringLiteral(
          "Source and user edits both changed this relationship; user version "
          "retained");
      result.diagnostics.append(importDiagnostic(
          DiagnosticSeverity::Warning,
          QStringLiteral("C++ relationship conflict for %1 → %2; the "
                         "user-edited model remains authoritative")
              .arg(source.sourceName, source.targetName),
          existing->id));
    } else if (userChanged && !sourceChanged &&
               (identityMatched || sourceBindingChanged)) {
      // Source locations and classification evidence are navigation metadata,
      // not user-owned UML fields. Refresh them even when the user has named
      // or otherwise edited the relationship, while retaining those edits.
      Relationship bindingOnlyUpdate = *existing;
      bindingOnlyUpdate.extra = item.desiredRelationship.extra;
      item.desiredRelationship = std::move(bindingOnlyUpdate);
      item.action = CppImportAction::Update;
      item.message =
          identityMatched
              ? QStringLiteral(
                    "Refresh matched C++ relationship binding; user-edited "
                    "model retained")
              : QStringLiteral(
                    "Refresh C++ relationship source binding; user-edited "
                    "model retained");
    } else if (userChanged && !sourceChanged) {
      item.action = CppImportAction::UserModified;
      item.message = QStringLiteral("User-edited relationship retained");
    } else if (identityMatched || sourceChanged || sourceBindingChanged ||
               (converged && userChanged)) {
      item.action = CppImportAction::Update;
      if (identityMatched)
        item.message =
            QStringLiteral("Matched relationship after C++ declaration "
                           "rename or move");
      else if (sourceBindingChanged && !sourceChanged)
        item.message =
            QStringLiteral("Refresh C++ relationship source binding");
      else
        item.message = converged ? QStringLiteral("Refresh source baseline")
                                 : QStringLiteral("Source changed");
    } else {
      item.action = CppImportAction::Unchanged;
      item.message = QStringLiteral("Already synchronized");
    }
    result.relationshipItems.append(std::move(item));
  }

  for (const auto &relationship : existingRelationships) {
    const QJsonObject binding = sourceBinding(relationship);
    const QString kind = binding.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("inheritance") &&
        kind != QStringLiteral("containment") &&
        kind != QStringLiteral("member") && kind != QStringLiteral("signature"))
      continue;
    const QString symbolId =
        binding.value(QStringLiteral("symbolId")).toString();
    if (symbolId.isEmpty() || discoveredRelationshipIds.contains(symbolId) ||
        matchedPreviousRelationshipIds.contains(symbolId))
      continue;
    CppRelationshipImportItem item;
    item.action = CppImportAction::MissingSource;
    item.existingRelationshipId = relationship.id;
    item.desiredRelationship = relationship;
    item.source.symbolId = symbolId;
    item.source.evidenceKind = kind;
    item.source.sourceSymbolId =
        binding.value(QStringLiteral("sourceSymbolId")).toString();
    item.source.targetSymbolId =
        binding.value(QStringLiteral("targetSymbolId")).toString();
    if (item.source.sourceSymbolId.isEmpty()) {
      item.source.sourceSymbolId =
          binding.value(QStringLiteral("derivedSymbolId")).toString();
    }
    if (item.source.targetSymbolId.isEmpty()) {
      item.source.targetSymbolId =
          binding.value(QStringLiteral("baseSymbolId")).toString();
    }
    item.source.filePath = binding.value(QStringLiteral("file")).toString();
    item.source.line = binding.value(QStringLiteral("line")).toInt();
    item.source.relationshipType = relationship.type;
    item.source.classificationReason =
        binding.value(QStringLiteral("classificationReason")).toString();
    item.source.sourceName = elementNameById.value(relationship.sourceId);
    item.source.targetName = elementNameById.value(relationship.targetId);
    const bool sourceRootRemoved =
        !item.source.filePath.isEmpty() &&
        pathIsWithinAny(item.source.filePath, result.previousSourceRoots) &&
        !pathIsWithinAny(item.source.filePath, result.sourceRoots);
    item.action = sourceRootRemoved ? CppImportAction::OutOfScope
                                    : CppImportAction::MissingSource;
    item.message =
        sourceRootRemoved
            ? QStringLiteral("Source is outside the selected folders; choose "
                             "whether to remove it or keep it as manual")
            : QStringLiteral(
                  "Previously imported relationship was not matched in this "
                  "scan; kept in the model for safety");
    result.relationshipItems.append(std::move(item));
  }

  return result;
}

#if YAUML_HAS_LIBCLANG

QString takeString(CXString string) {
  const char *characters = clang_getCString(string);
  const QString result = characters ? QString::fromUtf8(characters) : QString{};
  clang_disposeString(string);
  return result;
}

QString cursorSpelling(CXCursor cursor) {
  return takeString(clang_getCursorSpelling(cursor));
}

QString typeSpelling(CXType type) {
  return takeString(clang_getTypeSpelling(type));
}

QString cursorFilePath(CXCursor cursor, int *line = nullptr,
                       int *column = nullptr) {
  CXFile file = nullptr;
  unsigned sourceLine = 0;
  unsigned sourceColumn = 0;
  unsigned offset = 0;
  clang_getSpellingLocation(clang_getCursorLocation(cursor), &file, &sourceLine,
                            &sourceColumn, &offset);
  if (line)
    *line = static_cast<int>(sourceLine);
  if (column)
    *column = static_cast<int>(sourceColumn);
  return file ? normalizedPath(takeString(clang_getFileName(file))) : QString{};
}

QString accessPrefix(CX_CXXAccessSpecifier access, bool structDefault) {
  switch (access) {
  case CX_CXXPublic:
    return QStringLiteral("+");
  case CX_CXXProtected:
    return QStringLiteral("#");
  case CX_CXXPrivate:
    return QStringLiteral("-");
  case CX_CXXInvalidAccessSpecifier:
    return structDefault ? QStringLiteral("+") : QStringLiteral("-");
  }
  return QStringLiteral("-");
}

MemberVisibility accessVisibility(CX_CXXAccessSpecifier access,
                                  bool structDefault) {
  switch (access) {
  case CX_CXXPublic:
    return MemberVisibility::Public;
  case CX_CXXProtected:
    return MemberVisibility::Protected;
  case CX_CXXPrivate:
    return MemberVisibility::Private;
  case CX_CXXInvalidAccessSpecifier:
    return structDefault ? MemberVisibility::Public : MemberVisibility::Private;
  }
  return MemberVisibility::Private;
}

bool supportedSemanticScope(CXCursor cursor) {
  for (CXCursor parent = clang_getCursorSemanticParent(cursor);
       !clang_Cursor_isNull(parent);
       parent = clang_getCursorSemanticParent(parent)) {
    const CXCursorKind kind = clang_getCursorKind(parent);
    if (kind == CXCursor_TranslationUnit)
      return true;
    if (kind != CXCursor_Namespace && kind != CXCursor_ClassDecl &&
        kind != CXCursor_StructDecl && kind != CXCursor_ClassTemplate)
      return false;
  }
  return true;
}

QString qualifiedName(CXCursor cursor) {
  QStringList parts;
  const QString ownName = cursorSpelling(cursor);
  if (ownName.isEmpty())
    return {};
  parts.prepend(ownName);
  for (CXCursor parent = clang_getCursorSemanticParent(cursor);
       !clang_Cursor_isNull(parent);
       parent = clang_getCursorSemanticParent(parent)) {
    const CXCursorKind kind = clang_getCursorKind(parent);
    if (kind == CXCursor_TranslationUnit)
      break;
    if (kind == CXCursor_Namespace || kind == CXCursor_ClassDecl ||
        kind == CXCursor_StructDecl || kind == CXCursor_ClassTemplate) {
      const QString name = cursorSpelling(parent);
      if (!name.isEmpty())
        parts.prepend(name);
    }
  }
  return parts.join(QStringLiteral("::"));
}

QString namespacePath(CXCursor cursor) {
  QStringList parts;
  for (CXCursor parent = clang_getCursorSemanticParent(cursor);
       !clang_Cursor_isNull(parent);
       parent = clang_getCursorSemanticParent(parent)) {
    const CXCursorKind kind = clang_getCursorKind(parent);
    if (kind == CXCursor_TranslationUnit)
      break;
    if (kind != CXCursor_Namespace)
      continue;
    const QString name = cursorSpelling(parent);
    if (!name.isEmpty())
      parts.prepend(name);
  }
  return parts.join(QStringLiteral("::"));
}

enum class TypeUseContext {
  Member,
  Signature,
};

struct DiscoveredTypeUse {
  QString sourceSymbolId;
  QString targetSymbolId;
  QString memberSymbolId;
  TypeUseContext context = TypeUseContext::Signature;
  QString memberName;
  QStringList wrapperTypes;
  QList<QStringList> wrapperArguments;
  QList<int> templateArgumentPath;
  QStringList structuralMultiplicities;
  bool rawPointer = false;
  bool reference = false;
  QString filePath;
  int line = 0;
};

struct TypeTarget {
  QString symbolId;
  QStringList wrapperTypes;
  QList<QStringList> wrapperArguments;
  QList<int> templateArgumentPath;
  QStringList structuralMultiplicities;
  bool rawPointer = false;
  bool reference = false;
};

struct TypeTraversal {
  QStringList wrapperTypes;
  QList<QStringList> wrapperArguments;
  QList<int> templateArgumentPath;
  QStringList structuralMultiplicities;
  bool rawPointer = false;
  bool reference = false;
};

QString normalizedTemplateName(QString name) {
  name = name.trimmed();
  for (const QString &prefix :
       {QStringLiteral("const "), QStringLiteral("volatile "),
        QStringLiteral("class "), QStringLiteral("struct ")}) {
    while (name.startsWith(prefix))
      name.remove(0, prefix.size());
  }
  const qsizetype templateStart = name.indexOf(u'<');
  if (templateStart >= 0)
    name.truncate(templateStart);
  name.remove(QRegularExpression(QStringLiteral("\\s+")));
  while (name.startsWith(QStringLiteral("::")))
    name.remove(0, 2);
  return name;
}

QString templateName(CXType type) {
  if (clang_Type_getNumTemplateArguments(type) < 0) {
    type = clang_getCanonicalType(type);
    if (clang_Type_getNumTemplateArguments(type) < 0)
      return {};
  }
  const CXCursor declaration = clang_getTypeDeclaration(type);
  if (!clang_Cursor_isNull(declaration)) {
    const QString declaredName = qualifiedName(declaration);
    if (!declaredName.isEmpty())
      return normalizedTemplateName(declaredName);
  }
  return normalizedTemplateName(typeSpelling(type));
}

QStringList templateArgumentSpellings(const QString &spelling) {
  const qsizetype opening = spelling.indexOf(u'<');
  const qsizetype closing = spelling.lastIndexOf(u'>');
  if (opening < 0 || closing <= opening)
    return {};

  QStringList arguments;
  qsizetype start = opening + 1;
  int angleDepth = 0;
  int groupingDepth = 0;
  for (qsizetype index = start; index < closing; ++index) {
    const QChar character = spelling.at(index);
    if (character == u'<')
      ++angleDepth;
    else if (character == u'>')
      --angleDepth;
    else if (character == u'(' || character == u'[' || character == u'{')
      ++groupingDepth;
    else if (character == u')' || character == u']' || character == u'}')
      --groupingDepth;
    else if (character == u',' && angleDepth == 0 && groupingDepth == 0) {
      arguments.append(spelling.mid(start, index - start).trimmed());
      start = index + 1;
    }
  }
  arguments.append(spelling.mid(start, closing - start).trimmed());
  return arguments;
}

void collectTypeTargets(CXType type, TypeTraversal traversal,
                        QList<TypeTarget> &targets, int depth = 0) {
  if (depth > 12 || type.kind == CXType_Invalid)
    return;

  const CXType canonical = clang_getCanonicalType(type);
  switch (canonical.kind) {
  case CXType_Pointer:
  case CXType_MemberPointer:
    traversal.rawPointer = true;
    collectTypeTargets(clang_getPointeeType(canonical), std::move(traversal),
                       targets, depth + 1);
    return;
  case CXType_LValueReference:
  case CXType_RValueReference:
    traversal.reference = true;
    collectTypeTargets(clang_getPointeeType(canonical), std::move(traversal),
                       targets, depth + 1);
    return;
  case CXType_ConstantArray:
  case CXType_IncompleteArray:
  case CXType_VariableArray:
  case CXType_DependentSizedArray: {
    const long long size = clang_getArraySize(canonical);
    traversal.structuralMultiplicities.append(
        size >= 0 ? QString::number(size) : QStringLiteral("0..*"));
    collectTypeTargets(clang_getArrayElementType(canonical),
                       std::move(traversal), targets, depth + 1);
    return;
  }
  default:
    break;
  }

  CXType templateType = type;
  int templateArgumentCount = clang_Type_getNumTemplateArguments(templateType);
  if (templateArgumentCount < 0) {
    templateType = canonical;
    templateArgumentCount = clang_Type_getNumTemplateArguments(templateType);
  }
  if (templateArgumentCount >= 0) {
    const QString wrapper = templateName(templateType);
    const QStringList argumentSpellings =
        templateArgumentSpellings(typeSpelling(templateType));
    for (int index = 0; index < templateArgumentCount; ++index) {
      TypeTraversal nested = traversal;
      if (!wrapper.isEmpty()) {
        nested.wrapperTypes.append(wrapper);
        nested.wrapperArguments.append(argumentSpellings);
        nested.templateArgumentPath.append(index + 1);
      }
      collectTypeTargets(
          clang_Type_getTemplateArgumentAsType(templateType, index),
          std::move(nested), targets, depth + 1);
    }
    return;
  }

  const CXCursor declaration = clang_getTypeDeclaration(canonical);
  if (clang_Cursor_isNull(declaration))
    return;
  const CXCursorKind declarationKind = clang_getCursorKind(declaration);
  if (declarationKind != CXCursor_ClassDecl &&
      declarationKind != CXCursor_StructDecl &&
      declarationKind != CXCursor_ClassTemplate &&
      declarationKind != CXCursor_ClassTemplatePartialSpecialization)
    return;
  const QString symbolId = takeString(clang_getCursorUSR(declaration));
  if (symbolId.isEmpty())
    return;
  TypeTarget target;
  target.symbolId = symbolId;
  target.wrapperTypes = std::move(traversal.wrapperTypes);
  target.wrapperArguments = std::move(traversal.wrapperArguments);
  target.templateArgumentPath = std::move(traversal.templateArgumentPath);
  target.structuralMultiplicities =
      std::move(traversal.structuralMultiplicities);
  target.rawPointer = traversal.rawPointer;
  target.reference = traversal.reference;
  targets.append(std::move(target));
}

void appendTypeUses(CXCursor declaration, CXType type,
                    TypeUseContext useContext, const QString &memberName,
                    const CppSourceSymbol &source,
                    QList<DiscoveredTypeUse> &uses) {
  QList<TypeTarget> targets;
  collectTypeTargets(type, {}, targets);
  int line = 0;
  const QString filePath = cursorFilePath(declaration, &line);
  const QString memberSymbolId =
      useContext == TypeUseContext::Member
          ? takeString(clang_getCursorUSR(declaration))
          : QString{};
  for (auto &target : targets) {
    DiscoveredTypeUse use;
    use.sourceSymbolId = source.symbolId;
    use.targetSymbolId = std::move(target.symbolId);
    use.memberSymbolId = memberSymbolId;
    use.context = useContext;
    use.memberName = memberName;
    use.wrapperTypes = std::move(target.wrapperTypes);
    use.wrapperArguments = std::move(target.wrapperArguments);
    use.templateArgumentPath = std::move(target.templateArgumentPath);
    use.structuralMultiplicities = std::move(target.structuralMultiplicities);
    use.rawPointer = target.rawPointer;
    use.reference = target.reference;
    use.filePath = filePath;
    use.line = line;
    uses.append(std::move(use));
  }
}

struct MemberVisitorContext {
  CppSourceSymbol *symbol;
  QList<DiscoveredTypeUse> *typeUses;
  bool structDefault;
  // Libclang can collapse distinct overloads to the same USR while recovering
  // from missing third-party types (for example QPointF and QRectF both
  // becoming int). Retain the original tokenized declaration for the rare
  // collision group so every imported operation still receives a stable,
  // deterministic identity.
  QHash<QString, QList<int>> operationIndexesByBaseId;
  QHash<QString, QStringList> operationDiscriminatorsByBaseId;
};

QString operationDiscriminator(CXCursor cursor,
                               const ModelOperation &operation) {
  QStringList tokens;
  CXToken *cursorTokens = nullptr;
  unsigned tokenCount = 0;
  const CXTranslationUnit translationUnit =
      clang_Cursor_getTranslationUnit(cursor);
  clang_tokenize(translationUnit, clang_getCursorExtent(cursor), &cursorTokens,
                 &tokenCount);
  for (unsigned index = 0; index < tokenCount; ++index) {
    const QString token = takeString(
        clang_getTokenSpelling(translationUnit, cursorTokens[index]));
    if (token == QStringLiteral("{") || token == QStringLiteral(";"))
      break;
    if (!token.isEmpty())
      tokens.append(token);
  }
  if (cursorTokens)
    clang_disposeTokens(translationUnit, cursorTokens, tokenCount);

  if (!tokens.isEmpty())
    return tokens.join(u' ');

  // Tokenization should always succeed for an AST cursor. Keep a deterministic
  // recovery path for malformed translation units without ever returning an
  // empty discriminator that could silently discard a distinct overload.
  return QStringLiteral("%1|%2:%3")
      .arg(modelOperationSignature(operation), operation.sourceFile)
      .arg(operation.sourceLine);
}

QString disambiguatedOperationId(const QString &baseId,
                                 const QString &discriminator) {
  const QByteArray digest = QCryptographicHash::hash(discriminator.toUtf8(),
                                                     QCryptographicHash::Sha256)
                                .toHex()
                                .left(16);
  return QStringLiteral("%1:overload:%2")
      .arg(baseId, QString::fromLatin1(digest));
}

void appendOperation(MemberVisitorContext &context, ModelOperation operation,
                     CXCursor cursor) {
  const QString baseId = operation.id;
  const QString discriminator = operationDiscriminator(cursor, operation);
  QList<int> &indexes = context.operationIndexesByBaseId[baseId];
  QStringList &discriminators = context.operationDiscriminatorsByBaseId[baseId];

  const qsizetype duplicateIndex = discriminators.indexOf(discriminator);
  if (duplicateIndex >= 0) {
    // The same declaration can be visited more than once in a recovering AST.
    // It is one semantic operation and must not become a duplicate model row.
    return;
  }

  if (!indexes.isEmpty()) {
    // The first operation initially keeps Clang's ordinary USR. Only a proven
    // collision changes the IDs, limiting churn in already synchronized
    // projects while making every member of the collision group unambiguous.
    if (indexes.size() == 1) {
      ModelOperation &first = context.symbol->operations[indexes.first()];
      first.id = disambiguatedOperationId(baseId, discriminators.first());
    }
    operation.id = disambiguatedOperationId(baseId, discriminator);
  }

  indexes.append(context.symbol->operations.size());
  discriminators.append(discriminator);
  context.symbol->operations.append(std::move(operation));
}

CXChildVisitResult visitRecordMember(CXCursor cursor, CXCursor,
                                     CXClientData clientData) {
  auto &context = *static_cast<MemberVisitorContext *>(clientData);
  const CXCursorKind kind = clang_getCursorKind(cursor);
  if (kind == CXCursor_CXXBaseSpecifier) {
    CXCursor declaration = clang_getCursorReferenced(cursor);
    if (clang_Cursor_isNull(declaration))
      declaration = clang_getTypeDeclaration(
          clang_getCanonicalType(clang_getCursorType(cursor)));
    const QString baseSymbolId = takeString(clang_getCursorUSR(declaration));
    if (!baseSymbolId.isEmpty() &&
        !context.symbol->baseSymbolIds.contains(baseSymbolId))
      context.symbol->baseSymbolIds.append(baseSymbolId);
    return CXChildVisit_Continue;
  }
  const CX_CXXAccessSpecifier access = clang_getCXXAccessSpecifier(cursor);
  const QString prefix = accessPrefix(access, context.structDefault);
  if (kind == CXCursor_FieldDecl) {
    const QString name = cursorSpelling(cursor);
    if (!name.isEmpty()) {
      context.symbol->attributes.append(
          QStringLiteral("%1 %2: %3")
              .arg(prefix, name, typeSpelling(clang_getCursorType(cursor))));
      appendTypeUses(cursor, clang_getCursorType(cursor),
                     TypeUseContext::Member, name, *context.symbol,
                     *context.typeUses);
    }
    return CXChildVisit_Continue;
  }
  if (kind != CXCursor_CXXMethod && kind != CXCursor_Constructor &&
      kind != CXCursor_Destructor)
    return CXChildVisit_Continue;

  const QString name = cursorSpelling(cursor);
  if (name.isEmpty())
    return CXChildVisit_Continue;
  ModelOperation operation;
  operation.id = takeString(clang_getCursorUSR(cursor));
  if (operation.id.isEmpty()) {
    operation.id = QStringLiteral("%1:operation:%2")
                       .arg(context.symbol->symbolId,
                            typeSpelling(clang_getCursorType(cursor)));
  }
  operation.name = name;
  operation.visibility = accessVisibility(access, context.structDefault);
  if (kind == CXCursor_Constructor)
    operation.kind = OperationKind::Constructor;
  else if (kind == CXCursor_Destructor)
    operation.kind = OperationKind::Destructor;
  operation.sourceFile =
      cursorFilePath(cursor, &operation.sourceLine, &operation.sourceColumn);

  const int argumentCount = clang_Cursor_getNumArguments(cursor);
  for (int index = 0; index < argumentCount; ++index) {
    const CXCursor argument = clang_Cursor_getArgument(cursor, index);
    OperationParameter parameter;
    parameter.name = cursorSpelling(argument);
    parameter.type = typeSpelling(clang_getCursorType(argument));
    operation.parameters.append(std::move(parameter));
    appendTypeUses(argument, clang_getCursorType(argument),
                   TypeUseContext::Signature, name, *context.symbol,
                   *context.typeUses);
  }

  if (kind == CXCursor_CXXMethod) {
    appendTypeUses(cursor, clang_getCursorResultType(cursor),
                   TypeUseContext::Signature, name, *context.symbol,
                   *context.typeUses);
    operation.returnType = typeSpelling(clang_getCursorResultType(cursor));
    if (clang_CXXMethod_isConst(cursor))
      operation.modifiers.append(QStringLiteral("const"));
    if (clang_CXXMethod_isVirtual(cursor))
      operation.modifiers.append(QStringLiteral("virtual"));
    if (clang_CXXMethod_isPureVirtual(cursor))
      operation.modifiers.append(QStringLiteral("abstract"));
    if (clang_CXXMethod_isStatic(cursor))
      operation.modifiers.append(QStringLiteral("static"));
  }
  appendOperation(context, std::move(operation), cursor);
  return CXChildVisit_Continue;
}

struct AstVisitorContext {
  QStringList sourceRoots;
  QHash<QString, CppSourceSymbol> *symbols;
  QList<DiscoveredTypeUse> *typeUses;
  QList<Diagnostic> *diagnostics;
  QSet<QString> *declarationFiles;
};

bool isPrivateNestedType(CXCursor cursor) {
  const CXCursor parent = clang_getCursorSemanticParent(cursor);
  CXCursorKind parentKind = clang_getCursorKind(parent);
  if (parentKind == CXCursor_ClassTemplate)
    parentKind = clang_getTemplateCursorKind(parent);
  const bool nestedInRecord =
      parentKind == CXCursor_ClassDecl || parentKind == CXCursor_StructDecl ||
      parentKind == CXCursor_ClassTemplatePartialSpecialization;
  if (!nestedInRecord)
    return false;

  const CX_CXXAccessSpecifier access = clang_getCXXAccessSpecifier(cursor);
  if (access == CX_CXXPrivate)
    return true;
  if (access == CX_CXXPublic || access == CX_CXXProtected)
    return false;

  // Some libclang versions report InvalidAccessSpecifier when access is
  // implicit. C++ class members default to private and struct members to
  // public, so retain the language rule as a deterministic fallback.
  return parentKind == CXCursor_ClassDecl;
}

CXChildVisitResult visitAst(CXCursor cursor, CXCursor,
                            CXClientData clientData) {
  auto &context = *static_cast<AstVisitorContext *>(clientData);
  CXCursorKind kind = clang_getCursorKind(cursor);
  if (kind != CXCursor_ClassDecl && kind != CXCursor_StructDecl &&
      kind != CXCursor_ClassTemplate)
    return CXChildVisit_Recurse;
  if (!clang_isCursorDefinition(cursor) || !supportedSemanticScope(cursor) ||
      clang_Location_isInSystemHeader(clang_getCursorLocation(cursor)))
    return CXChildVisit_Recurse;

  int line = 0;
  int column = 0;
  const QString filePath = cursorFilePath(cursor, &line, &column);
  if (filePath.isEmpty() || !pathIsWithinAny(filePath, context.sourceRoots))
    return CXChildVisit_Recurse;
  context.declarationFiles->insert(comparablePath(filePath));
  const QString name = qualifiedName(cursor);
  if (name.isEmpty())
    return CXChildVisit_Recurse;

  if (kind == CXCursor_ClassTemplate)
    kind = clang_getTemplateCursorKind(cursor);
  CppSourceSymbol symbol;
  symbol.qualifiedName = name;
  symbol.namespacePath = namespacePath(cursor);
  symbol.elementType =
      kind == CXCursor_StructDecl ? ElementType::Struct : ElementType::Class;
  symbol.filePath = filePath;
  symbol.line = line;
  symbol.column = column;
  symbol.privateNestedType = isPrivateNestedType(cursor);
  symbol.symbolId = takeString(clang_getCursorUSR(cursor));
  if (symbol.symbolId.isEmpty())
    symbol.symbolId = QStringLiteral("cpp:%1#%2").arg(filePath, name);

  MemberVisitorContext memberContext{&symbol, context.typeUses,
                                     symbol.elementType == ElementType::Struct};
  clang_visitChildren(cursor, visitRecordMember, &memberContext);

  const auto existing = context.symbols->constFind(symbol.symbolId);
  if (existing == context.symbols->cend()) {
    context.symbols->insert(symbol.symbolId, std::move(symbol));
  } else if (*existing != symbol) {
    context.diagnostics->append(importDiagnostic(
        DiagnosticSeverity::Warning,
        QStringLiteral("C++ symbol %1 has different declarations across "
                       "translation units; using the first")
            .arg(name)));
  }
  return CXChildVisit_Recurse;
}

QString commonSourceRoot(const QStringList &sourceRoots) {
  if (sourceRoots.isEmpty())
    return {};
  if (sourceRoots.size() == 1)
    return sourceRoots.first();

  QString common = sourceRoots.first();
  while (!std::all_of(
      sourceRoots.cbegin(), sourceRoots.cend(),
      [&](const QString &root) { return pathIsWithin(root, common); })) {
    QDir parent(common);
    if (!parent.cdUp())
      return sourceRoots.first();
    const QString next = normalizedPath(parent.absolutePath());
    if (comparablePath(next) == comparablePath(common))
      return sourceRoots.first();
    common = next;
  }
  return common;
}

struct CompileCommand {
  QString directory;
  QString filePath;
  QStringList arguments;
  // Commands synthesized by the folder scanner intentionally tolerate
  // incomplete include paths. Compilation-database commands are authoritative
  // and keep parser failures as errors.
  bool bestEffort = false;
};

bool isCppImplementationFile(const QString &path) {
  static const QSet<QString> extensions{
      QStringLiteral("cpp"), QStringLiteral("cc"),  QStringLiteral("cxx"),
      QStringLiteral("c++"), QStringLiteral("ixx"), QStringLiteral("cppm")};
  return extensions.contains(QFileInfo(path).suffix().toLower());
}

bool isCppHeaderFile(const QString &path) {
  static const QSet<QString> extensions{
      QStringLiteral("h"),   QStringLiteral("hh"),  QStringLiteral("hpp"),
      QStringLiteral("hxx"), QStringLiteral("inl"), QStringLiteral("ipp"),
      QStringLiteral("tpp")};
  return extensions.contains(QFileInfo(path).suffix().toLower());
}

bool excludedSourceDirectory(const QString &name) {
  const QString lower = name.toLower();
  static const QSet<QString> exactNames{
      QStringLiteral(".git"),        QStringLiteral(".hg"),
      QStringLiteral(".svn"),        QStringLiteral(".cache"),
      QStringLiteral("build"),       QStringLiteral("out"),
      QStringLiteral("dist"),        QStringLiteral("node_modules"),
      QStringLiteral("third_party"), QStringLiteral("third-party"),
      QStringLiteral("external"),    QStringLiteral("extern"),
      QStringLiteral("vendor")};
  return exactNames.contains(lower) ||
         lower.startsWith(QStringLiteral("build-")) ||
         lower.startsWith(QStringLiteral("cmake-build-"));
}

bool loadCompilationCommands(const QString &databasePath,
                             QList<CompileCommand> &commands,
                             QStringList &sourceFiles,
                             QList<Diagnostic> &diagnostics) {
  CXCompilationDatabase_Error databaseError = CXCompilationDatabase_NoError;
  const QByteArray databaseDirectory =
      QFileInfo(databasePath).absolutePath().toUtf8();
  CXCompilationDatabase database = clang_CompilationDatabase_fromDirectory(
      databaseDirectory.constData(), &databaseError);
  if (!database || databaseError != CXCompilationDatabase_NoError) {
    diagnostics.append(importDiagnostic(
        DiagnosticSeverity::Error,
        QStringLiteral("Clang could not load %1").arg(databasePath)));
    if (database)
      clang_CompilationDatabase_dispose(database);
    return false;
  }

  QSet<QString> seenFiles;
  CXCompileCommands allCommands =
      clang_CompilationDatabase_getAllCompileCommands(database);
  const unsigned commandCount = clang_CompileCommands_getSize(allCommands);
  commands.reserve(static_cast<qsizetype>(commandCount));
  for (unsigned commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
    CXCompileCommand command =
        clang_CompileCommands_getCommand(allCommands, commandIndex);
    CompileCommand value;
    value.directory =
        normalizedPath(takeString(clang_CompileCommand_getDirectory(command)));
    QString filePath = takeString(clang_CompileCommand_getFilename(command));
    if (!QFileInfo(filePath).isAbsolute())
      filePath = QDir(value.directory).filePath(filePath);
    value.filePath = normalizedPath(filePath);
    const QString comparable = comparablePath(value.filePath);
    if (seenFiles.contains(comparable))
      continue;
    seenFiles.insert(comparable);
    sourceFiles.append(value.filePath);
    const unsigned argumentCount = clang_CompileCommand_getNumArgs(command);
    for (unsigned argumentIndex = 0; argumentIndex < argumentCount;
         ++argumentIndex) {
      value.arguments.append(
          takeString(clang_CompileCommand_getArg(command, argumentIndex)));
    }
    commands.append(std::move(value));
  }
  clang_CompileCommands_dispose(allCommands);
  clang_CompilationDatabase_dispose(database);

  if (!commands.isEmpty())
    return true;
  diagnostics.append(importDiagnostic(
      DiagnosticSeverity::Error,
      QStringLiteral("The compilation database contains no commands")));
  return false;
}

bool scanSourceCommands(const QStringList &sourceRoots,
                        QList<CompileCommand> &commands,
                        QStringList &sourceFiles, QString &sourceRoot,
                        QList<Diagnostic> &diagnostics) {
  constexpr qsizetype kMaximumSourceFiles = 10'000;
  constexpr int kMaximumDirectoryDepth = 32;

  if (sourceRoots.isEmpty()) {
    diagnostics.append(importDiagnostic(
        DiagnosticSeverity::Error,
        QStringLiteral("Select at least one C++ source directory")));
    return false;
  }
  sourceRoot = commonSourceRoot(sourceRoots);

  QStringList implementations;
  QStringList headers;
  QList<QPair<QString, int>> pending;
  for (const QString &root : sourceRoots)
    pending.append({root, 0});
  QSet<QString> seenDirectories;
  QSet<QString> seenFiles;
  while (!pending.isEmpty() &&
         implementations.size() + headers.size() < kMaximumSourceFiles) {
    const auto [directoryPath, depth] = pending.takeFirst();
    const QString comparableDirectory = comparablePath(directoryPath);
    if (seenDirectories.contains(comparableDirectory))
      continue;
    seenDirectories.insert(comparableDirectory);

    const QDir directory(directoryPath);
    for (const QFileInfo &entry : directory.entryInfoList(
             QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
             QDir::Name | QDir::IgnoreCase)) {
      if (entry.isDir()) {
        if (depth < kMaximumDirectoryDepth &&
            !excludedSourceDirectory(entry.fileName())) {
          pending.append({entry.absoluteFilePath(), depth + 1});
        }
        continue;
      }
      if (!isCppImplementationFile(entry.absoluteFilePath()) &&
          !isCppHeaderFile(entry.absoluteFilePath()))
        continue;
      const QString normalized = normalizedPath(entry.absoluteFilePath());
      const QString comparable = comparablePath(normalized);
      if (seenFiles.contains(comparable))
        continue;
      seenFiles.insert(comparable);
      (isCppHeaderFile(normalized) ? headers : implementations)
          .append(normalized);
      if (implementations.size() + headers.size() >= kMaximumSourceFiles)
        break;
    }
  }
  if (!pending.isEmpty()) {
    diagnostics.append(importDiagnostic(
        DiagnosticSeverity::Warning,
        QStringLiteral("C++ source scan reached the %1-file safety limit; "
                       "the preview is partial")
            .arg(kMaximumSourceFiles)));
  }

  std::sort(implementations.begin(), implementations.end());
  std::sort(headers.begin(), headers.end());
  sourceFiles = implementations + headers;
  if (sourceFiles.isEmpty()) {
    diagnostics.append(importDiagnostic(
        DiagnosticSeverity::Error,
        QStringLiteral("No C++ source or header files were found in the "
                       "selected source directories")));
    return false;
  }

  // The common parent supports sibling selections whose includes are written
  // relative to their shared source directory. Declaration filtering still
  // ensures that unselected sibling folders are not imported.
  QStringList includeRoots{sourceRoot};
  includeRoots.append(sourceRoots);
  for (const QString &root : sourceRoots) {
    for (const QString &candidateName :
         {QStringLiteral("src"), QStringLiteral("source"),
          QStringLiteral("include"), QStringLiteral("inc")}) {
      const QString candidate = QDir(root).filePath(candidateName);
      if (QFileInfo(candidate).isDir())
        includeRoots.append(normalizedPath(candidate));
    }
  }
  includeRoots.removeDuplicates();

  commands.reserve(sourceFiles.size());
  for (const QString &filePath : sourceFiles) {
    CompileCommand command;
    command.directory = sourceRoot;
    command.filePath = filePath;
    command.arguments = {QStringLiteral("clang++"),
                         QStringLiteral("-std=c++20")};
    for (const QString &includeRoot : includeRoots)
      command.arguments.append(QStringLiteral("-I%1").arg(includeRoot));
    if (isCppHeaderFile(filePath))
      command.arguments.append(
          {QStringLiteral("-x"), QStringLiteral("c++-header")});
    command.arguments.append(filePath);
    command.bestEffort = true;
    commands.append(std::move(command));
  }
  diagnostics.append(importDiagnostic(
      DiagnosticSeverity::Info,
      QStringLiteral(
          "No compilation database was found; using a best-effort "
          "scan of %1 C++ source and header file(s) from %2 selected "
          "source director%3")
          .arg(sourceFiles.size())
          .arg(sourceRoots.size())
          .arg(sourceRoots.size() == 1 ? QStringLiteral("y")
                                       : QStringLiteral("ies"))));
  return true;
}

bool sameFileArgument(const QString &argument, const QString &filePath,
                      const QString &workingDirectory) {
  if (argument.startsWith(u'-'))
    return false;
  const QString resolved = QFileInfo(argument).isAbsolute()
                               ? argument
                               : QDir(workingDirectory).filePath(argument);
  return comparablePath(resolved) == comparablePath(filePath);
}

QStringList parserArguments(const CompileCommand &command) {
  QStringList result;
  if (command.arguments.isEmpty())
    return result;
  result.append(command.arguments.first());
  result.append(QStringLiteral("-working-directory=%1").arg(command.directory));
  for (int index = 1; index < command.arguments.size(); ++index) {
    const QString argument = command.arguments.at(index);
    if (sameFileArgument(argument, command.filePath, command.directory) ||
        argument == QStringLiteral("-c") || argument == QStringLiteral("/c"))
      continue;
    if (argument == QStringLiteral("-o") || argument == QStringLiteral("-MF") ||
        argument == QStringLiteral("-MT") ||
        argument == QStringLiteral("-MQ")) {
      ++index;
      continue;
    }
    if (argument.startsWith(QStringLiteral("/Fo"), Qt::CaseInsensitive) ||
        argument.startsWith(QStringLiteral("/Fd"), Qt::CaseInsensitive) ||
        argument.startsWith(QStringLiteral("/Fe"), Qt::CaseInsensitive) ||
        argument.startsWith(QStringLiteral("-o")))
      continue;
    if (argument.startsWith(u'@') && !QFileInfo(argument.mid(1)).isAbsolute()) {
      result.append(u'@' + QDir(command.directory).filePath(argument.mid(1)));
      continue;
    }
    result.append(argument);
  }
  return result;
}

void appendTranslationUnitDiagnostics(CXTranslationUnit translationUnit,
                                      QList<Diagnostic> &diagnostics,
                                      bool bestEffort,
                                      QSet<QString> &seenMessages,
                                      int &suppressedCount) {
  constexpr qsizetype kMaximumDiagnostics = 100;
  const unsigned count = clang_getNumDiagnostics(translationUnit);
  for (unsigned index = 0; index < count; ++index) {
    CXDiagnostic diagnostic = clang_getDiagnostic(translationUnit, index);
    const CXDiagnosticSeverity clangSeverity =
        clang_getDiagnosticSeverity(diagnostic);
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    if (clangSeverity == CXDiagnostic_Warning)
      severity = DiagnosticSeverity::Warning;
    else if (clangSeverity == CXDiagnostic_Error ||
             clangSeverity == CXDiagnostic_Fatal)
      severity = DiagnosticSeverity::Error;
    if (clangSeverity != CXDiagnostic_Ignored &&
        clangSeverity != CXDiagnostic_Note) {
      QString message = takeString(clang_formatDiagnostic(
          diagnostic, clang_defaultDiagnosticDisplayOptions()));
      if (bestEffort && severity == DiagnosticSeverity::Error) {
        severity = DiagnosticSeverity::Warning;
        message.prepend(QStringLiteral("Best-effort source scan: "));
      }
      if (!seenMessages.contains(message)) {
        if (seenMessages.size() < kMaximumDiagnostics) {
          seenMessages.insert(message);
          diagnostics.append(importDiagnostic(severity, message));
        } else {
          ++suppressedCount;
        }
      }
    }
    clang_disposeDiagnostic(diagnostic);
  }
}

CppImportPreview discover(const QStringList &searchPaths,
                          const CppImportOptions &options,
                          const CppImportProgressCallback &progress) {
  CppImportPreview preview;
  preview.optionsUsed = options;
  reportProgress(progress, CppImportProgressStage::Preparing,
                 QStringLiteral("Preparing C++ synchronization"));
  const QRegularExpression interfacePattern(options.interfacePattern);
  if (options.interfacePattern.isEmpty() || !interfacePattern.isValid()) {
    preview.discoveryDiagnostics.append(importDiagnostic(
        DiagnosticSeverity::Error,
        QStringLiteral("The C++ interface pattern is not a valid, non-empty "
                       "regular expression: %1")
            .arg(options.interfacePattern)));
    preview.diagnostics = preview.discoveryDiagnostics;
    return preview;
  }
  preview.sourceRoots =
      normalizeSourceRoots(searchPaths, preview.discoveryDiagnostics);
  if (preview.sourceRoots.isEmpty()) {
    if (preview.discoveryDiagnostics.isEmpty()) {
      preview.discoveryDiagnostics.append(importDiagnostic(
          DiagnosticSeverity::Error,
          QStringLiteral("Select at least one C++ source directory")));
    }
    preview.diagnostics = preview.discoveryDiagnostics;
    return preview;
  }
  preview.sourceRoot = commonSourceRoot(preview.sourceRoots);
  reportProgress(
      progress, CppImportProgressStage::DiscoveringSources,
      QStringLiteral("Discovering C++ sources"),
      QStringLiteral("Inspecting build metadata for %1 source director%2")
          .arg(preview.sourceRoots.size())
          .arg(preview.sourceRoots.size() == 1 ? QStringLiteral("y")
                                               : QStringLiteral("ies")));

  QList<CompileCommand> commands;
  QStringList sourceFiles;
  QStringList rootsWithoutDatabase;
  for (const QString &sourceRoot : std::as_const(preview.sourceRoots)) {
    const QString database = findCompilationDatabase(
        sourceRoot, preview.discoveryDiagnostics, false);
    if (database.isEmpty())
      rootsWithoutDatabase.append(sourceRoot);
    else if (!preview.compilationDatabasePaths.contains(database))
      preview.compilationDatabasePaths.append(database);
  }

  QSet<QString> seenCommandFiles;
  for (const QString &database :
       std::as_const(preview.compilationDatabasePaths)) {
    QList<CompileCommand> databaseCommands;
    QStringList databaseSourceFiles;
    if (!loadCompilationCommands(database, databaseCommands,
                                 databaseSourceFiles,
                                 preview.discoveryDiagnostics)) {
      preview.diagnostics = preview.discoveryDiagnostics;
      return preview;
    }
    for (auto &command : databaseCommands) {
      const QString comparable = comparablePath(command.filePath);
      if (seenCommandFiles.contains(comparable))
        continue;
      seenCommandFiles.insert(comparable);
      commands.append(std::move(command));
    }
    sourceFiles.append(databaseSourceFiles);
  }

  if (!preview.compilationDatabasePaths.isEmpty()) {
    preview.usedCompilationDatabase = true;
    preview.compilationDatabasePath = preview.compilationDatabasePaths.first();
  }

  if (!rootsWithoutDatabase.isEmpty()) {
    reportProgress(
        progress, CppImportProgressStage::DiscoveringSources,
        QStringLiteral("Discovering C++ sources"),
        QStringLiteral("Scanning selected folders for C++ source files"));
    QList<CompileCommand> scannedCommands;
    QStringList scannedFiles;
    QString scannedRoot;
    if (!scanSourceCommands(rootsWithoutDatabase, scannedCommands, scannedFiles,
                            scannedRoot, preview.discoveryDiagnostics)) {
      preview.diagnostics = preview.discoveryDiagnostics;
      return preview;
    }
    for (auto &command : scannedCommands) {
      const QString comparable = comparablePath(command.filePath);
      if (seenCommandFiles.contains(comparable))
        continue;
      seenCommandFiles.insert(comparable);
      commands.append(std::move(command));
    }
    sourceFiles.append(scannedFiles);
  }

  if (commands.isEmpty()) {
    preview.discoveryDiagnostics.append(importDiagnostic(
        DiagnosticSeverity::Error,
        QStringLiteral("No C++ compilation commands could be prepared for "
                       "the selected source directories")));
    preview.diagnostics = preview.discoveryDiagnostics;
    return preview;
  }
  sourceFiles.removeDuplicates();
  reportProgress(progress, CppImportProgressStage::ParsingSources,
                 QStringLiteral("Parsing C++ sources"), {}, 0,
                 static_cast<int>(commands.size()));

  QHash<QString, CppSourceSymbol> symbols;
  QList<DiscoveredTypeUse> typeUses;
  CXIndex index = clang_createIndex(1, 0);
  int parsedTranslationUnits = 0;
  int suppressedDiagnosticCount = 0;
  QSet<QString> seenDiagnosticMessages;
  QSet<QString> declarationFiles;
  for (qsizetype commandIndex = 0; commandIndex < commands.size();
       ++commandIndex) {
    const auto &command = commands.at(commandIndex);
    reportProgress(progress, CppImportProgressStage::ParsingSources,
                   QStringLiteral("Parsing C++ sources"), command.filePath,
                   static_cast<int>(commandIndex),
                   static_cast<int>(commands.size()));
    // Source translation units normally expose all declarations in the headers
    // they include. Avoid reparsing those headers as standalone units; headers
    // not reached from any source file are still parsed later in the list.
    if (command.bestEffort && isCppHeaderFile(command.filePath) &&
        declarationFiles.contains(comparablePath(command.filePath)))
      continue;
    const QStringList arguments = parserArguments(command);
    QList<QByteArray> utf8Arguments;
    utf8Arguments.reserve(arguments.size());
    for (const QString &argument : arguments)
      utf8Arguments.append(argument.toUtf8());
    QVector<const char *> argumentPointers;
    argumentPointers.reserve(utf8Arguments.size());
    for (const QByteArray &argument : utf8Arguments)
      argumentPointers.append(argument.constData());

    const QByteArray sourceFile = command.filePath.toUtf8();
    CXTranslationUnit translationUnit = nullptr;
    const CXErrorCode error = clang_parseTranslationUnit2FullArgv(
        index, sourceFile.constData(), argumentPointers.constData(),
        static_cast<int>(argumentPointers.size()), nullptr, 0,
        CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_KeepGoing,
        &translationUnit);
    if (error != CXError_Success || !translationUnit) {
      preview.discoveryDiagnostics.append(
          importDiagnostic(command.bestEffort ? DiagnosticSeverity::Warning
                                              : DiagnosticSeverity::Error,
                           QStringLiteral("Clang could not parse %1 (error %2)")
                               .arg(command.filePath)
                               .arg(static_cast<int>(error))));
      if (translationUnit)
        clang_disposeTranslationUnit(translationUnit);
      continue;
    }
    ++parsedTranslationUnits;
    appendTranslationUnitDiagnostics(
        translationUnit, preview.discoveryDiagnostics, command.bestEffort,
        seenDiagnosticMessages, suppressedDiagnosticCount);
    AstVisitorContext visitor{preview.sourceRoots, &symbols, &typeUses,
                              &preview.discoveryDiagnostics, &declarationFiles};
    clang_visitChildren(clang_getTranslationUnitCursor(translationUnit),
                        visitAst, &visitor);
    clang_disposeTranslationUnit(translationUnit);
  }
  reportProgress(progress, CppImportProgressStage::ParsingSources,
                 QStringLiteral("Parsing C++ sources"), {},
                 static_cast<int>(commands.size()),
                 static_cast<int>(commands.size()));
  clang_disposeIndex(index);

  if (suppressedDiagnosticCount > 0) {
    preview.discoveryDiagnostics.append(importDiagnostic(
        DiagnosticSeverity::Warning,
        QStringLiteral("%1 additional C++ parser diagnostic(s) were "
                       "suppressed")
            .arg(suppressedDiagnosticCount)));
  }

  if (parsedTranslationUnits == 0) {
    preview.discoveryDiagnostics.append(importDiagnostic(
        DiagnosticSeverity::Error,
        !preview.usedCompilationDatabase
            ? QStringLiteral("Clang could not parse any discovered C++ file")
            : QStringLiteral(
                  "Clang could not parse any compilation database entry")));
    preview.diagnostics = preview.discoveryDiagnostics;
    return preview;
  }

  preview.symbols = symbols.values();
  reportProgress(progress, CppImportProgressStage::AnalyzingModel,
                 QStringLiteral("Analyzing imported model"),
                 QStringLiteral("%1 type declaration(s) discovered")
                     .arg(preview.symbols.size()));
  std::sort(preview.symbols.begin(), preview.symbols.end(),
            [](const CppSourceSymbol &left, const CppSourceSymbol &right) {
              const int byName = left.qualifiedName.compare(
                  right.qualifiedName, Qt::CaseInsensitive);
              if (byName != 0)
                return byName < 0;
              if (left.filePath != right.filePath)
                return left.filePath < right.filePath;
              return left.line < right.line;
            });
  for (const auto &derived : preview.symbols) {
    for (const QString &baseSymbolId : derived.baseSymbolIds) {
      const auto base = symbols.constFind(baseSymbolId);
      if (base == symbols.cend())
        continue;
      CppSourceRelationship inheritance;
      inheritance.sourceSymbolId = derived.symbolId;
      inheritance.targetSymbolId = baseSymbolId;
      inheritance.symbolId =
          QStringLiteral("cpp:inheritance:%1->%2")
              .arg(inheritance.sourceSymbolId, inheritance.targetSymbolId);
      inheritance.sourceName = derived.qualifiedName;
      inheritance.targetName = base->qualifiedName;
      inheritance.evidenceKind = QStringLiteral("inheritance");
      const QString unqualifiedBaseName =
          inheritance.targetName.section(QStringLiteral("::"), -1);
      if (interfacePattern.match(unqualifiedBaseName).hasMatch()) {
        inheritance.relationshipType = RelationshipType::Realization;
        inheritance.classificationReason =
            QStringLiteral("Base name %1 matches interface pattern %2")
                .arg(unqualifiedBaseName, options.interfacePattern);
      } else {
        inheritance.relationshipType = RelationshipType::Generalization;
        inheritance.classificationReason =
            QStringLiteral("Base name %1 does not match interface pattern %2")
                .arg(unqualifiedBaseName, options.interfacePattern);
      }
      inheritance.filePath = derived.filePath;
      inheritance.line = derived.line;
      preview.relationships.append(std::move(inheritance));
    }
  }

  QHash<QString, const CppSourceSymbol *> symbolsByQualifiedName;
  for (const auto &symbol : preview.symbols)
    symbolsByQualifiedName.insert(symbol.qualifiedName, &symbol);
  const auto parentQualifiedName = [](const QString &name) {
    const qsizetype separator = name.lastIndexOf(QStringLiteral("::"));
    return separator >= 0 ? name.left(separator) : QString{};
  };
  for (const auto &nested : preview.symbols) {
    QString ownerName = parentQualifiedName(nested.qualifiedName);
    const CppSourceSymbol *owner = nullptr;
    while (!ownerName.isEmpty() && ownerName != nested.namespacePath) {
      owner = symbolsByQualifiedName.value(ownerName, nullptr);
      if (owner)
        break;
      ownerName = parentQualifiedName(ownerName);
    }
    if (!owner)
      continue;

    CppSourceRelationship containment;
    containment.sourceSymbolId = owner->symbolId;
    containment.targetSymbolId = nested.symbolId;
    containment.symbolId =
        QStringLiteral("cpp:containment:%1->%2")
            .arg(containment.sourceSymbolId, containment.targetSymbolId);
    containment.sourceName = owner->qualifiedName;
    containment.targetName = nested.qualifiedName;
    containment.evidenceKind = QStringLiteral("containment");
    containment.relationshipType = RelationshipType::Containment;
    containment.classificationReason =
        QStringLiteral("Type is declared inside another C++ type");
    containment.filePath = nested.filePath;
    containment.line = nested.line;
    preview.relationships.append(std::move(containment));
  }

  QList<CppMemberTypeRule> memberTypeRules;
  for (CppMemberTypeRule rule : options.memberTypeRules) {
    rule.typeName = normalizedTemplateName(rule.typeName);
    if (!rule.typeName.isEmpty())
      memberTypeRules.append(std::move(rule));
  }
  const auto matchingRule =
      [&](const QString &wrapper) -> const CppMemberTypeRule * {
    for (const auto &rule : memberTypeRules) {
      if (wrapper == rule.typeName ||
          (!rule.typeName.contains(QStringLiteral("::")) &&
           wrapper.section(QStringLiteral("::"), -1) == rule.typeName))
        return &rule;
    }
    return nullptr;
  };
  const auto resolvedRuleMultiplicity = [](QString multiplicity,
                                           const QStringList &arguments) {
    static const QRegularExpression placeholder(
        QStringLiteral("\\{([1-9][0-9]*)\\}"));
    for (QRegularExpressionMatch match = placeholder.match(multiplicity);
         match.hasMatch(); match = placeholder.match(multiplicity)) {
      const int argument = match.captured(1).toInt();
      QString replacement;
      if (argument <= arguments.size()) {
        const QRegularExpressionMatch integer =
            QRegularExpression(QStringLiteral("^\\s*([0-9]+)"))
                .match(arguments.at(argument - 1));
        if (integer.hasMatch())
          replacement = integer.captured(1);
      }
      if (replacement.isEmpty())
        replacement = QStringLiteral("0..*");
      multiplicity.replace(match.capturedStart(), match.capturedLength(),
                           replacement);
    }
    return multiplicity.trimmed();
  };
  struct MultiplicityRange {
    qint64 minimum = 1;
    qint64 maximum = 1;
    bool unbounded = false;
    bool valid = true;
  };
  const auto parseMultiplicity = [](const QString &value) {
    MultiplicityRange result;
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("1"))
      return result;
    const QRegularExpressionMatch single =
        QRegularExpression(QStringLiteral("^([0-9]+)$")).match(trimmed);
    if (single.hasMatch()) {
      result.minimum = result.maximum = single.captured(1).toLongLong();
      return result;
    }
    const QRegularExpressionMatch range =
        QRegularExpression(QStringLiteral("^([0-9]+)\\.\\.([0-9]+|\\*)$"))
            .match(trimmed);
    if (!range.hasMatch()) {
      result.valid = false;
      return result;
    }
    result.minimum = range.captured(1).toLongLong();
    result.unbounded = range.captured(2) == QStringLiteral("*");
    if (!result.unbounded)
      result.maximum = range.captured(2).toLongLong();
    return result;
  };
  const auto combinedMultiplicity = [&](const QStringList &parts) -> QString {
    MultiplicityRange combined;
    QString firstUnparsed;
    for (const QString &part : parts) {
      const MultiplicityRange parsed = parseMultiplicity(part);
      if (!parsed.valid) {
        if (firstUnparsed.isEmpty())
          firstUnparsed = part.trimmed();
        continue;
      }
      combined.minimum *= parsed.minimum;
      combined.unbounded = combined.unbounded || parsed.unbounded;
      if (!combined.unbounded)
        combined.maximum *= parsed.maximum;
    }
    if (!firstUnparsed.isEmpty())
      return firstUnparsed;
    if (combined.unbounded)
      return QStringLiteral("%1..*").arg(combined.minimum);
    if (combined.minimum == combined.maximum)
      return QString::number(combined.minimum);
    return QStringLiteral("%1..%2").arg(combined.minimum).arg(combined.maximum);
  };
  const auto pairKey = [](const QString &sourceId, const QString &targetId) {
    return sourceId + u'\x1f' + targetId;
  };
  const auto memberIdentity = [](const DiscoveredTypeUse &use) {
    if (!use.memberSymbolId.isEmpty())
      return use.memberSymbolId;
    return QStringLiteral("%1|%2|%3|%4")
        .arg(use.sourceSymbolId, use.filePath, QString::number(use.line),
             use.memberName);
  };

  struct InferredRelationship {
    DiscoveredTypeUse use;
    RelationshipType type = RelationshipType::Dependency;
    QString multiplicity;
    QString reason;
    int strength = 0;
  };
  QHash<QString, InferredRelationship> memberRelationships;
  QHash<QString, InferredRelationship> signatureRelationships;
  QSet<QString> memberPairs;
  QSet<QString> inheritedPairs;
  for (const auto &relationship : std::as_const(preview.relationships)) {
    if (relationship.evidenceKind == QStringLiteral("inheritance"))
      inheritedPairs.insert(
          pairKey(relationship.sourceSymbolId, relationship.targetSymbolId));
  }

  for (const auto &use : std::as_const(typeUses)) {
    if (use.sourceSymbolId == use.targetSymbolId ||
        !symbols.contains(use.sourceSymbolId) ||
        !symbols.contains(use.targetSymbolId))
      continue;
    const QString endpointPair =
        pairKey(use.sourceSymbolId, use.targetSymbolId);
    if (inheritedPairs.contains(endpointPair))
      continue;

    InferredRelationship candidate;
    candidate.use = use;
    if (use.context == TypeUseContext::Signature) {
      candidate.type = RelationshipType::Dependency;
      candidate.strength = 1;
      candidate.reason =
          QStringLiteral("Referenced by an operation parameter or return type");
      signatureRelationships.tryInsert(endpointPair, std::move(candidate));
      continue;
    }

    bool selectedTarget = true;
    bool matchedConfiguredRule = false;
    QStringList matchedWrappers;
    QStringList multiplicityParts = use.structuralMultiplicities;
    for (qsizetype wrapperIndex = 0; wrapperIndex < use.wrapperTypes.size();
         ++wrapperIndex) {
      const CppMemberTypeRule *rule =
          matchingRule(use.wrapperTypes.at(wrapperIndex));
      if (!rule)
        continue;
      matchedConfiguredRule = true;
      if (use.templateArgumentPath.value(wrapperIndex, 1) !=
          rule->targetArgument) {
        selectedTarget = false;
        break;
      }
      candidate.type = rule->relationshipType;
      candidate.strength =
          rule->relationshipType == RelationshipType::Composition ? 4 : 3;
      matchedWrappers.append(use.wrapperTypes.at(wrapperIndex));
      const QString multiplicity = resolvedRuleMultiplicity(
          rule->multiplicity, use.wrapperArguments.value(wrapperIndex));
      if (!multiplicity.isEmpty())
        multiplicityParts.append(multiplicity);
    }
    if (!selectedTarget)
      continue;

    // Raw indirection is inside any template wrappers and therefore has the
    // final say on ownership. Outer collection multiplicities are retained.
    if (use.rawPointer) {
      candidate.type = RelationshipType::Aggregation;
      candidate.strength = 3;
      multiplicityParts.append(QStringLiteral("0..1"));
      candidate.reason =
          QStringLiteral("Member %1 is a raw pointer").arg(use.memberName);
    } else if (use.reference) {
      candidate.type = RelationshipType::Aggregation;
      candidate.strength = 3;
      multiplicityParts.append(QStringLiteral("1"));
      candidate.reason =
          QStringLiteral("Member %1 is a reference").arg(use.memberName);
    } else if (matchedConfiguredRule) {
      candidate.reason =
          QStringLiteral("Member %1 matches configured type rule(s): %2")
              .arg(use.memberName, matchedWrappers.join(QStringLiteral(", ")));
    } else if (!use.wrapperTypes.isEmpty()) {
      candidate.type = RelationshipType::Association;
      candidate.strength = 2;
      candidate.reason =
          QStringLiteral("Member %1 uses unclassified wrapper type %2")
              .arg(use.memberName, use.wrapperTypes.first());
    } else {
      candidate.type = RelationshipType::Composition;
      candidate.strength = 4;
      candidate.reason = QStringLiteral("Member %1 stores the type by value")
                             .arg(use.memberName);
    }
    candidate.multiplicity = combinedMultiplicity(multiplicityParts);

    const QString key = pairKey(memberIdentity(use), use.targetSymbolId);
    const auto existing = memberRelationships.constFind(key);
    if (existing == memberRelationships.cend() ||
        candidate.strength > existing->strength) {
      memberRelationships.insert(key, std::move(candidate));
    }
    memberPairs.insert(endpointPair);
  }

  QHash<QString, QString> legacyMemberIdentity;
  QHash<QString, int> memberPairCounts;
  for (auto iterator = memberRelationships.cbegin();
       iterator != memberRelationships.cend(); ++iterator) {
    const QString endpointPair =
        pairKey(iterator->use.sourceSymbolId, iterator->use.targetSymbolId);
    ++memberPairCounts[endpointPair];
    const QString identity = memberIdentity(iterator->use);
    if (!legacyMemberIdentity.contains(endpointPair) ||
        identity < legacyMemberIdentity.value(endpointPair))
      legacyMemberIdentity.insert(endpointPair, identity);
  }

  const auto appendInferred = [&](const InferredRelationship &candidate,
                                  const QString &kind) {
    const auto source = symbols.constFind(candidate.use.sourceSymbolId);
    const auto target = symbols.constFind(candidate.use.targetSymbolId);
    if (source == symbols.cend() || target == symbols.cend())
      return;
    CppSourceRelationship relationship;
    relationship.sourceSymbolId = source->symbolId;
    relationship.targetSymbolId = target->symbolId;
    relationship.sourceName = source->qualifiedName;
    relationship.targetName = target->qualifiedName;
    relationship.evidenceKind = kind;
    relationship.relationshipType = candidate.type;
    if (kind == QStringLiteral("member")) {
      relationship.sourceRole = candidate.use.memberName;
      relationship.sourceMultiplicity = candidate.multiplicity;
    }
    relationship.classificationReason = candidate.reason;
    relationship.filePath = candidate.use.filePath;
    relationship.line = candidate.use.line;
    const QString endpointPair =
        pairKey(relationship.sourceSymbolId, relationship.targetSymbolId);
    const bool useLegacyIdentity = kind != QStringLiteral("member") ||
                                   memberPairCounts.value(endpointPair) == 1 ||
                                   memberIdentity(candidate.use) ==
                                       legacyMemberIdentity.value(endpointPair);
    if (useLegacyIdentity) {
      // Preserve the original identity for the common one-field case and for
      // one deterministic field when several fields share the same type.
      relationship.symbolId =
          QStringLiteral("cpp:type-use:%1->%2")
              .arg(relationship.sourceSymbolId, relationship.targetSymbolId);
    } else {
      relationship.symbolId =
          QStringLiteral("cpp:member-use:%1->%2")
              .arg(memberIdentity(candidate.use), relationship.targetSymbolId);
    }
    preview.relationships.append(std::move(relationship));
  };
  for (auto iterator = memberRelationships.cbegin();
       iterator != memberRelationships.cend(); ++iterator) {
    appendInferred(iterator.value(), QStringLiteral("member"));
  }
  for (auto iterator = signatureRelationships.cbegin();
       iterator != signatureRelationships.cend(); ++iterator) {
    // A structural member relationship conveys more information than a
    // signature-only dependency between the same pair.
    if (!memberPairs.contains(iterator.key()))
      appendInferred(iterator.value(), QStringLiteral("signature"));
  }

  std::sort(preview.relationships.begin(), preview.relationships.end(),
            [](const CppSourceRelationship &left,
               const CppSourceRelationship &right) {
              if (left.sourceName != right.sourceName)
                return left.sourceName < right.sourceName;
              if (left.targetName != right.targetName)
                return left.targetName < right.targetName;
              if (left.sourceRole != right.sourceRole)
                return left.sourceRole < right.sourceRole;
              return left.evidenceKind < right.evidenceKind;
            });
  preview.ok = true;
  preview.diagnostics = preview.discoveryDiagnostics;
  return preview;
}

#endif

} // namespace

QString toString(CppImportAction action) {
  switch (action) {
  case CppImportAction::Create:
    return QStringLiteral("create");
  case CppImportAction::Update:
    return QStringLiteral("update");
  case CppImportAction::Conflict:
    return QStringLiteral("conflict");
  case CppImportAction::Unchanged:
    return QStringLiteral("unchanged");
  case CppImportAction::UserModified:
    return QStringLiteral("user-modified");
  case CppImportAction::MissingSource:
    return QStringLiteral("missing-source");
  case CppImportAction::OutOfScope:
    return QStringLiteral("out-of-scope");
  }
  return QStringLiteral("unchanged");
}

QString toString(CppImportConflictResolution resolution) {
  switch (resolution) {
  case CppImportConflictResolution::Unresolved:
    return QStringLiteral("unresolved");
  case CppImportConflictResolution::KeepModel:
    return QStringLiteral("keep-model");
  case CppImportConflictResolution::UseSource:
    return QStringLiteral("use-source");
  }
  return QStringLiteral("unresolved");
}

CppImportConflictResolution
cppImportConflictResolutionFromString(const QString &value, bool *ok) {
  const QString normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("unresolved")) {
    if (ok)
      *ok = true;
    return CppImportConflictResolution::Unresolved;
  }
  if (normalized == QStringLiteral("keep-model")) {
    if (ok)
      *ok = true;
    return CppImportConflictResolution::KeepModel;
  }
  if (normalized == QStringLiteral("use-source")) {
    if (ok)
      *ok = true;
    return CppImportConflictResolution::UseSource;
  }
  if (ok)
    *ok = false;
  return CppImportConflictResolution::Unresolved;
}

QString toString(CppImportOutOfScopeResolution resolution) {
  switch (resolution) {
  case CppImportOutOfScopeResolution::Unresolved:
    return QStringLiteral("unresolved");
  case CppImportOutOfScopeResolution::Remove:
    return QStringLiteral("remove");
  case CppImportOutOfScopeResolution::KeepManual:
    return QStringLiteral("keep-manual");
  }
  return QStringLiteral("unresolved");
}

CppImportOutOfScopeResolution
cppImportOutOfScopeResolutionFromString(const QString &value, bool *ok) {
  const QString normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("unresolved")) {
    if (ok)
      *ok = true;
    return CppImportOutOfScopeResolution::Unresolved;
  }
  if (normalized == QStringLiteral("remove")) {
    if (ok)
      *ok = true;
    return CppImportOutOfScopeResolution::Remove;
  }
  if (normalized == QStringLiteral("keep-manual")) {
    if (ok)
      *ok = true;
    return CppImportOutOfScopeResolution::KeepManual;
  }
  if (ok)
    *ok = false;
  return CppImportOutOfScopeResolution::Unresolved;
}

QString toString(CppImportMissingSourceResolution resolution) {
  switch (resolution) {
  case CppImportMissingSourceResolution::Keep:
    return QStringLiteral("keep");
  case CppImportMissingSourceResolution::Remove:
    return QStringLiteral("remove");
  case CppImportMissingSourceResolution::KeepManual:
    return QStringLiteral("keep-manual");
  }
  return QStringLiteral("keep");
}

CppImportMissingSourceResolution
cppImportMissingSourceResolutionFromString(const QString &value, bool *ok) {
  const QString normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("keep")) {
    if (ok)
      *ok = true;
    return CppImportMissingSourceResolution::Keep;
  }
  if (normalized == QStringLiteral("remove")) {
    if (ok)
      *ok = true;
    return CppImportMissingSourceResolution::Remove;
  }
  if (normalized == QStringLiteral("keep-manual")) {
    if (ok)
      *ok = true;
    return CppImportMissingSourceResolution::KeepManual;
  }
  if (ok)
    *ok = false;
  return CppImportMissingSourceResolution::Keep;
}

QString CppImportItem::conflictKey() const {
  return symbol.symbolId.isEmpty()
             ? QString{}
             : QStringLiteral("element:%1").arg(symbol.symbolId);
}

QString CppImportItem::outOfScopeKey() const {
  return existingElementId.isEmpty()
             ? QString{}
             : QStringLiteral("out-of-scope:element:%1").arg(existingElementId);
}

QString CppImportItem::missingSourceKey() const {
  return existingElementId.isEmpty()
             ? QString{}
             : QStringLiteral("missing-source:element:%1")
                   .arg(existingElementId);
}

bool CppImportItem::isResolvableConflict() const {
  return action == CppImportAction::Conflict && existingElement.has_value() &&
         !desiredElement.id.isEmpty() &&
         existingElement->id == desiredElement.id;
}

bool CppImportItem::isOutOfScope() const {
  return action == CppImportAction::OutOfScope && !existingElementId.isEmpty();
}

bool CppImportItem::isOutOfScopeResolved() const {
  return isOutOfScope() &&
         outOfScopeResolution != CppImportOutOfScopeResolution::Unresolved;
}

bool CppImportItem::isMissingSource() const {
  return action == CppImportAction::MissingSource &&
         !existingElementId.isEmpty();
}

bool CppImportItem::shouldRemove() const {
  return (isOutOfScope() &&
          outOfScopeResolution == CppImportOutOfScopeResolution::Remove) ||
         (isMissingSource() &&
          missingSourceResolution == CppImportMissingSourceResolution::Remove);
}

bool CppImportItem::isApplicable() const {
  return action == CppImportAction::Create ||
         action == CppImportAction::Update ||
         (isOutOfScope() &&
          outOfScopeResolution == CppImportOutOfScopeResolution::KeepManual) ||
         (isMissingSource() &&
          missingSourceResolution ==
              CppImportMissingSourceResolution::KeepManual) ||
         (isResolvableConflict() &&
          resolution != CppImportConflictResolution::Unresolved);
}

ModelElement CppImportItem::appliedElement() const {
  Q_ASSERT(isApplicable());
  if (isOutOfScope() || isMissingSource()) {
    Q_ASSERT(
        (isOutOfScope() &&
         outOfScopeResolution == CppImportOutOfScopeResolution::KeepManual) ||
        (isMissingSource() &&
         missingSourceResolution ==
             CppImportMissingSourceResolution::KeepManual));
    ModelElement retained = desiredElement;
    retained.extra.remove(QString::fromLatin1(kBindingKey));
    return retained;
  }
  if (action != CppImportAction::Conflict ||
      resolution == CppImportConflictResolution::UseSource)
    return desiredElement;

  Q_ASSERT(isResolvableConflict());
  Q_ASSERT(resolution == CppImportConflictResolution::KeepModel);
  ModelElement retained = *existingElement;
  // Acknowledging "Keep model" advances only the source baseline and
  // provenance. Future synchronization therefore sees the retained fields as
  // an intentional user override instead of reporting the same conflict
  // forever.
  retained.extra.insert(QString::fromLatin1(kBindingKey),
                        sourceBinding(desiredElement));
  return retained;
}

QString CppRelationshipImportItem::conflictKey() const {
  return source.symbolId.isEmpty()
             ? QString{}
             : QStringLiteral("relationship:%1").arg(source.symbolId);
}

QString CppRelationshipImportItem::outOfScopeKey() const {
  return existingRelationshipId.isEmpty()
             ? QString{}
             : QStringLiteral("out-of-scope:relationship:%1")
                   .arg(existingRelationshipId);
}

QString CppRelationshipImportItem::missingSourceKey() const {
  return existingRelationshipId.isEmpty()
             ? QString{}
             : QStringLiteral("missing-source:relationship:%1")
                   .arg(existingRelationshipId);
}

bool CppRelationshipImportItem::isResolvableConflict() const {
  return action == CppImportAction::Conflict &&
         existingRelationship.has_value() &&
         !desiredRelationship.id.isEmpty() &&
         existingRelationship->id == desiredRelationship.id;
}

bool CppRelationshipImportItem::isOutOfScope() const {
  return action == CppImportAction::OutOfScope &&
         !existingRelationshipId.isEmpty();
}

bool CppRelationshipImportItem::isOutOfScopeResolved() const {
  return isOutOfScope() &&
         outOfScopeResolution != CppImportOutOfScopeResolution::Unresolved;
}

bool CppRelationshipImportItem::isMissingSource() const {
  return action == CppImportAction::MissingSource &&
         !existingRelationshipId.isEmpty();
}

bool CppRelationshipImportItem::shouldRemove() const {
  return (isOutOfScope() &&
          outOfScopeResolution == CppImportOutOfScopeResolution::Remove) ||
         (isMissingSource() &&
          missingSourceResolution == CppImportMissingSourceResolution::Remove);
}

bool CppRelationshipImportItem::isApplicable() const {
  return action == CppImportAction::Create ||
         action == CppImportAction::Update ||
         (isOutOfScope() &&
          outOfScopeResolution == CppImportOutOfScopeResolution::KeepManual) ||
         (isMissingSource() &&
          missingSourceResolution ==
              CppImportMissingSourceResolution::KeepManual) ||
         (isResolvableConflict() &&
          resolution != CppImportConflictResolution::Unresolved);
}

Relationship CppRelationshipImportItem::appliedRelationship() const {
  Q_ASSERT(isApplicable());
  if (isOutOfScope() || isMissingSource()) {
    Q_ASSERT(
        (isOutOfScope() &&
         outOfScopeResolution == CppImportOutOfScopeResolution::KeepManual) ||
        (isMissingSource() &&
         missingSourceResolution ==
             CppImportMissingSourceResolution::KeepManual));
    Relationship retained = desiredRelationship;
    retained.extra.remove(QString::fromLatin1(kBindingKey));
    return retained;
  }
  if (action != CppImportAction::Conflict ||
      resolution == CppImportConflictResolution::UseSource)
    return desiredRelationship;

  Q_ASSERT(isResolvableConflict());
  Q_ASSERT(resolution == CppImportConflictResolution::KeepModel);
  Relationship retained = *existingRelationship;
  retained.extra.insert(QString::fromLatin1(kBindingKey),
                        sourceBinding(desiredRelationship));
  return retained;
}

QString CppImportOptions::defaultInterfacePattern() {
  return QStringLiteral("^I[A-Z].*$");
}

QList<CppMemberTypeRule> CppImportOptions::defaultMemberTypeRules() {
  using enum RelationshipType;
  const auto rule = [](const char *typeName, RelationshipType relationshipType,
                       const char *multiplicity, int targetArgument = 1) {
    return CppMemberTypeRule{QString::fromLatin1(typeName), relationshipType,
                             QString::fromLatin1(multiplicity), targetArgument};
  };

  // Containers own their elements. Map-like containers point at their mapped
  // value (argument 2), not their key. {2} reads a non-type template argument,
  // allowing std::array<T, N> to produce an exact multiplicity.
  return {
      rule("std::unique_ptr", Composition, "0..1"),
      rule("std::shared_ptr", Aggregation, "0..1"),
      rule("std::optional", Composition, "0..1"),
      rule("std::vector", Composition, "0..*"),
      rule("std::deque", Composition, "0..*"),
      rule("std::list", Composition, "0..*"),
      rule("std::forward_list", Composition, "0..*"),
      rule("std::array", Composition, "{2}"),
      rule("std::inplace_vector", Composition, "0..{2}"),
      rule("std::hive", Composition, "0..*"),
      rule("std::basic_string", Composition, "0..*"),
      rule("std::valarray", Composition, "0..*"),
      rule("std::set", Composition, "0..*"),
      rule("std::multiset", Composition, "0..*"),
      rule("std::unordered_set", Composition, "0..*"),
      rule("std::unordered_multiset", Composition, "0..*"),
      rule("std::flat_set", Composition, "0..*"),
      rule("std::flat_multiset", Composition, "0..*"),
      rule("std::map", Composition, "0..*", 2),
      rule("std::multimap", Composition, "0..*", 2),
      rule("std::unordered_map", Composition, "0..*", 2),
      rule("std::unordered_multimap", Composition, "0..*", 2),
      rule("std::flat_map", Composition, "0..*", 2),
      rule("std::flat_multimap", Composition, "0..*", 2),
      rule("std::stack", Composition, "0..*"),
      rule("std::queue", Composition, "0..*"),
      rule("std::priority_queue", Composition, "0..*"),
  };
}

void configureCppImportStereotypes(CppImportOptions &options,
                                   const ProjectData &project) {
  options.localTypeStereotypeId.clear();
  options.localTypeStereotypeApplicableTo.clear();
  options.privateTypeStereotypeId.clear();
  options.privateTypeStereotypeApplicableTo.clear();
  const auto *localDefinition = stereotype_catalog::findByConventionalIdOrName(
      project, stereotype_catalog::kLocalStereotypeId, QStringLiteral("local"));
  if (localDefinition) {
    options.localTypeStereotypeId = localDefinition->id;
    options.localTypeStereotypeApplicableTo = localDefinition->applicableTo;
  }
  const auto *privateDefinition =
      stereotype_catalog::findByConventionalIdOrName(
          project, stereotype_catalog::kPrivateStereotypeId,
          QStringLiteral("private"));
  if (privateDefinition) {
    options.privateTypeStereotypeId = privateDefinition->id;
    options.privateTypeStereotypeApplicableTo = privateDefinition->applicableTo;
  }
}

int CppImportPreview::elementApplicableCount() const {
  return static_cast<int>(std::count_if(
      items.cbegin(), items.cend(),
      [](const CppImportItem &item) { return item.isApplicable(); }));
}

int CppImportPreview::relationshipApplicableCount() const {
  return static_cast<int>(
      std::count_if(relationshipItems.cbegin(), relationshipItems.cend(),
                    [](const CppRelationshipImportItem &item) {
                      return item.isApplicable();
                    }));
}

int CppImportPreview::applicableCount() const {
  const auto removalCount = [](const auto &values) {
    return static_cast<int>(
        std::count_if(values.cbegin(), values.cend(),
                      [](const auto &item) { return item.shouldRemove(); }));
  };
  return elementApplicableCount() + relationshipApplicableCount() +
         removalCount(items) + removalCount(relationshipItems);
}

int CppImportPreview::conflictCount() const {
  const int elementConflicts = static_cast<int>(std::count_if(
      items.cbegin(), items.cend(), [](const CppImportItem &item) {
        return item.action == CppImportAction::Conflict;
      }));
  const int relationshipConflicts = static_cast<int>(
      std::count_if(relationshipItems.cbegin(), relationshipItems.cend(),
                    [](const CppRelationshipImportItem &item) {
                      return item.action == CppImportAction::Conflict;
                    }));
  return elementConflicts + relationshipConflicts;
}

int CppImportPreview::resolvableConflictCount() const {
  const int elementConflicts = static_cast<int>(std::count_if(
      items.cbegin(), items.cend(),
      [](const CppImportItem &item) { return item.isResolvableConflict(); }));
  const int relationshipConflicts = static_cast<int>(
      std::count_if(relationshipItems.cbegin(), relationshipItems.cend(),
                    [](const CppRelationshipImportItem &item) {
                      return item.isResolvableConflict();
                    }));
  return elementConflicts + relationshipConflicts;
}

int CppImportPreview::resolvedConflictCount() const {
  const int elementConflicts = static_cast<int>(std::count_if(
      items.cbegin(), items.cend(), [](const CppImportItem &item) {
        return item.isResolvableConflict() &&
               item.resolution != CppImportConflictResolution::Unresolved;
      }));
  const int relationshipConflicts = static_cast<int>(std::count_if(
      relationshipItems.cbegin(), relationshipItems.cend(),
      [](const CppRelationshipImportItem &item) {
        return item.isResolvableConflict() &&
               item.resolution != CppImportConflictResolution::Unresolved;
      }));
  return elementConflicts + relationshipConflicts;
}

int CppImportPreview::unresolvedConflictCount() const {
  return conflictCount() - resolvedConflictCount();
}

int CppImportPreview::missingSourceCount() const {
  const auto count = [](const auto &values) {
    return static_cast<int>(
        std::count_if(values.cbegin(), values.cend(),
                      [](const auto &item) { return item.isMissingSource(); }));
  };
  return count(items) + count(relationshipItems);
}

int CppImportPreview::selectedMissingSourceCount() const {
  const auto count = [](const auto &values) {
    return static_cast<int>(
        std::count_if(values.cbegin(), values.cend(), [](const auto &item) {
          return item.isMissingSource() &&
                 item.missingSourceResolution !=
                     CppImportMissingSourceResolution::Keep;
        }));
  };
  return count(items) + count(relationshipItems);
}

int CppImportPreview::outOfScopeCount() const {
  const auto count = [](const auto &values) {
    return static_cast<int>(
        std::count_if(values.cbegin(), values.cend(),
                      [](const auto &item) { return item.isOutOfScope(); }));
  };
  return count(items) + count(relationshipItems);
}

int CppImportPreview::resolvedOutOfScopeCount() const {
  const auto count = [](const auto &values) {
    return static_cast<int>(
        std::count_if(values.cbegin(), values.cend(), [](const auto &item) {
          return item.isOutOfScopeResolved();
        }));
  };
  return count(items) + count(relationshipItems);
}

int CppImportPreview::unresolvedOutOfScopeCount() const {
  return outOfScopeCount() - resolvedOutOfScopeCount();
}

bool CppImportPreview::setConflictResolution(
    const QString &conflictKey, CppImportConflictResolution resolution) {
  for (auto &item : items) {
    if (item.conflictKey() != conflictKey)
      continue;
    if (!item.isResolvableConflict())
      return false;
    item.resolution = resolution;
    return true;
  }
  for (auto &item : relationshipItems) {
    if (item.conflictKey() != conflictKey)
      continue;
    if (!item.isResolvableConflict())
      return false;
    item.resolution = resolution;
    return true;
  }
  return false;
}

void CppImportPreview::resolveAllConflicts(
    CppImportConflictResolution resolution) {
  for (auto &item : items)
    if (item.isResolvableConflict())
      item.resolution = resolution;
  for (auto &item : relationshipItems)
    if (item.isResolvableConflict())
      item.resolution = resolution;
}

bool CppImportPreview::setMissingSourceResolution(
    const QString &missingSourceKey,
    CppImportMissingSourceResolution resolution) {
  for (auto &item : items) {
    if (item.missingSourceKey() != missingSourceKey)
      continue;
    if (!item.isMissingSource())
      return false;
    item.missingSourceResolution = resolution;
    return true;
  }
  for (auto &item : relationshipItems) {
    if (item.missingSourceKey() != missingSourceKey)
      continue;
    if (!item.isMissingSource())
      return false;
    item.missingSourceResolution = resolution;
    return true;
  }
  return false;
}

void CppImportPreview::resolveAllMissingSources(
    CppImportMissingSourceResolution resolution) {
  for (auto &item : items)
    if (item.isMissingSource())
      item.missingSourceResolution = resolution;
  for (auto &item : relationshipItems)
    if (item.isMissingSource())
      item.missingSourceResolution = resolution;
}

bool CppImportPreview::setOutOfScopeResolution(
    const QString &outOfScopeKey, CppImportOutOfScopeResolution resolution) {
  for (auto &item : items) {
    if (item.outOfScopeKey() != outOfScopeKey)
      continue;
    if (!item.isOutOfScope())
      return false;
    item.outOfScopeResolution = resolution;
    return true;
  }
  for (auto &item : relationshipItems) {
    if (item.outOfScopeKey() != outOfScopeKey)
      continue;
    if (!item.isOutOfScope())
      return false;
    item.outOfScopeResolution = resolution;
    return true;
  }
  return false;
}

void CppImportPreview::resolveAllOutOfScope(
    CppImportOutOfScopeResolution resolution) {
  for (auto &item : items)
    if (item.isOutOfScope())
      item.outOfScopeResolution = resolution;
  for (auto &item : relationshipItems)
    if (item.isOutOfScope())
      item.outOfScopeResolution = resolution;
}

bool CppImportService::available() { return YAUML_HAS_LIBCLANG != 0; }

CppImportPreview
CppImportService::preview(const QString &searchPath,
                          const QList<ModelElement> &existingElements,
                          const QList<Relationship> &existingRelationships,
                          const CppImportOptions &options,
                          const CppImportProgressCallback &progress) {
  return preview(QStringList{searchPath}, existingElements,
                 existingRelationships, options, progress);
}

CppImportPreview
CppImportService::preview(const QStringList &searchPaths,
                          const QList<ModelElement> &existingElements,
                          const QList<Relationship> &existingRelationships,
                          const CppImportOptions &options,
                          const CppImportProgressCallback &progress) {
#if YAUML_HAS_LIBCLANG
  CppImportPreview discovery = discover(searchPaths, options, progress);
  reportProgress(progress, CppImportProgressStage::PlanningChanges,
                 QStringLiteral("Planning synchronization changes"));
  return planImport(discovery, existingElements, existingRelationships);
#else
  Q_UNUSED(searchPaths)
  Q_UNUSED(existingElements)
  Q_UNUSED(existingRelationships)
  Q_UNUSED(options)
  Q_UNUSED(progress)
  CppImportPreview preview;
  preview.discoveryDiagnostics.append(importDiagnostic(
      DiagnosticSeverity::Error,
      QStringLiteral("C++ import is unavailable because this build was "
                     "configured without libclang")));
  preview.diagnostics = preview.discoveryDiagnostics;
  return preview;
#endif
}

CppImportPreview
CppImportService::replan(const CppImportPreview &discovery,
                         const QList<ModelElement> &existingElements,
                         const QList<Relationship> &existingRelationships) {
  return planImport(discovery, existingElements, existingRelationships);
}

int CppImportService::apply(ProjectData &project,
                            const CppImportPreview &preview) {
  if (!preview.ok || preview.unresolvedOutOfScopeCount() > 0)
    return 0;

  int applied = 0;
  for (const auto &item : preview.items) {
    if (!item.isApplicable())
      continue;
    ModelElement appliedElement = item.appliedElement();
    if (auto *existing = findElement(project, appliedElement.id))
      *existing = appliedElement;
    else
      project.elements.append(std::move(appliedElement));
    ++applied;
  }
  for (const auto &item : preview.relationshipItems) {
    if (!item.isApplicable())
      continue;
    Relationship appliedRelationship = item.appliedRelationship();
    if (auto *existing = findRelationship(project, appliedRelationship.id))
      *existing = appliedRelationship;
    else
      project.relationships.append(std::move(appliedRelationship));
    ++applied;
  }

  QSet<QString> removedElementIds;
  for (const auto &item : preview.items)
    if (item.shouldRemove())
      removedElementIds.insert(item.existingElementId);
  QSet<QString> removedRelationshipIds;
  for (const auto &item : preview.relationshipItems)
    if (item.shouldRemove())
      removedRelationshipIds.insert(item.existingRelationshipId);

  // Synthetic namespace packages can be shared by declarations from roots
  // that remain selected. Retain and detach such a package instead of
  // invalidating still-managed children.
  for (auto &element : project.elements) {
    if (!removedElementIds.contains(element.id) ||
        element.type != ElementType::Package)
      continue;
    const bool stillUsed =
        std::any_of(project.elements.cbegin(), project.elements.cend(),
                    [&](const ModelElement &candidate) {
                      return candidate.id != element.id &&
                             !removedElementIds.contains(candidate.id) &&
                             (candidate.packageId == element.id ||
                              candidate.enclosingTypeId == element.id);
                    });
    if (stillUsed) {
      removedElementIds.remove(element.id);
      element.extra.remove(QString::fromLatin1(kBindingKey));
      ++applied;
    }
  }

  for (const QString &relationshipId : std::as_const(removedRelationshipIds)) {
    if (findRelationship(project, relationshipId)) {
      removeRelationshipAndPresentations(project, relationshipId);
      ++applied;
    }
  }

  QStringList orderedElementIds;
  const auto appendElements = [&](bool packages) {
    for (const auto &element : std::as_const(project.elements)) {
      if (removedElementIds.contains(element.id) &&
          (element.type == ElementType::Package) == packages)
        orderedElementIds.append(element.id);
    }
  };
  appendElements(false);
  appendElements(true);
  for (const QString &elementId : std::as_const(orderedElementIds)) {
    if (findElement(project, elementId)) {
      removeElementAndReferences(project, elementId);
      ++applied;
    }
  }

  const QStringList roots =
      !preview.sourceRoots.isEmpty()
          ? preview.sourceRoots
          : (preview.sourceRoot.isEmpty() ? QStringList{}
                                          : QStringList{preview.sourceRoot});
  if (!roots.isEmpty())
    project.cppImport.sourceRoots = roots;
  return applied;
}

} // namespace yauml
