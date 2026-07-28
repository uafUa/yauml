#include "core/cpp_import.h"

#include "core/stereotype_catalog.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

#ifndef UUML_HAS_LIBCLANG
#define UUML_HAS_LIBCLANG 0
#endif

#if UUML_HAS_LIBCLANG
#include <clang-c/CXCompilationDatabase.h>
#include <clang-c/Index.h>
#endif

namespace uuml {
namespace {

constexpr auto kBindingKey = "sourceBinding";
constexpr auto kBindingLanguage = "cpp";
constexpr auto kBindingVersion = 2;
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
                  QJsonArray::fromStringList(element.operations));
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
  element.operations = stringList(snapshot.value(QStringLiteral("operations")));
  element.packageId = snapshot.value(QStringLiteral("packageId")).toString();
  element.enclosingTypeId =
      snapshot.value(QStringLiteral("enclosingTypeId")).toString();
  return element;
}

bool sourceOwnedStateEquals(const ModelElement &left,
                            const ModelElement &right) {
  return left.type == right.type && left.name == right.name &&
         left.attributes == right.attributes &&
         left.operations == right.operations &&
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
  if (symbol.privateNestedType &&
      !options.privateTypeStereotypeId.isEmpty() &&
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

  QHash<QString, QString> plannedElementIdBySymbol;
  QHash<QString, QString> packageIdByNamespacePath;
  QHash<QString, QString> typeIdByQualifiedName;
  for (const auto &symbol : plannedSymbols) {
    QString id;
    if (const auto *bound = byBinding.value(symbol.symbolId, nullptr)) {
      if (!duplicateBindings.contains(symbol.symbolId))
        id = bound->id;
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

    const ModelElement *existing = byBinding.value(symbol.symbolId, nullptr);
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
    } else if (userChanged && !sourceChanged && sourceStereotypesChanged) {
      // Apply the explicit source-derived classification without overwriting
      // unrelated user edits to source-synchronized fields. The baseline
      // remains the source snapshot, so later conflicts are still detected.
      ModelElement stereotypeOnlyUpdate = *existing;
      stereotypeOnlyUpdate.stereotypeIds = item.desiredElement.stereotypeIds;
      stereotypeOnlyUpdate.extra = item.desiredElement.extra;
      item.desiredElement = std::move(stereotypeOnlyUpdate);
      item.action = CppImportAction::Update;
      item.message =
          QStringLiteral("Refresh source-derived stereotypes; user-edited "
                         "model retained");
    } else if (userChanged && !sourceChanged) {
      item.action = CppImportAction::UserModified;
      item.message = QStringLiteral("User-edited model retained");
    } else if (sourceChanged || sourceStereotypesChanged ||
               (converged && userChanged)) {
      item.action = CppImportAction::Update;
      if (sourceStereotypesChanged && !sourceChanged)
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
    if (symbolId.isEmpty() || discoveredIds.contains(symbolId))
      continue;
    CppImportItem item;
    item.action = CppImportAction::MissingSource;
    item.existingElementId = element.id;
    item.desiredElement = element;
    item.symbol.symbolId = symbolId;
    item.symbol.qualifiedName = element.name;
    item.symbol.filePath = binding.value(QStringLiteral("file")).toString();
    item.symbol.line = binding.value(QStringLiteral("line")).toInt();
    item.message =
        QStringLiteral("Source declaration was not discovered; model retained");
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
    } else if (userChanged && !sourceChanged) {
      item.action = CppImportAction::UserModified;
      item.message = QStringLiteral("User-edited relationship retained");
    } else if (sourceChanged || (converged && userChanged)) {
      item.action = CppImportAction::Update;
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
    if (symbolId.isEmpty() || discoveredRelationshipIds.contains(symbolId))
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
    item.message = QStringLiteral(
        "Source relationship was not discovered; model retained");
    result.relationshipItems.append(std::move(item));
  }

  return result;
}

#if UUML_HAS_LIBCLANG

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
};

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
  const QString prefix =
      accessPrefix(clang_getCXXAccessSpecifier(cursor), context.structDefault);
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
  QStringList parameters;
  const int argumentCount = clang_Cursor_getNumArguments(cursor);
  for (int index = 0; index < argumentCount; ++index) {
    const CXCursor argument = clang_Cursor_getArgument(cursor, index);
    QString argumentName = cursorSpelling(argument);
    const QString argumentType = typeSpelling(clang_getCursorType(argument));
    if (argumentName.isEmpty())
      parameters.append(argumentType);
    else
      parameters.append(
          QStringLiteral("%1: %2").arg(argumentName, argumentType));
    appendTypeUses(argument, clang_getCursorType(argument),
                   TypeUseContext::Signature, name, *context.symbol,
                   *context.typeUses);
  }

  QString operation =
      QStringLiteral("%1 %2(%3)")
          .arg(prefix, name, parameters.join(QStringLiteral(", ")));
  if (kind == CXCursor_CXXMethod) {
    appendTypeUses(cursor, clang_getCursorResultType(cursor),
                   TypeUseContext::Signature, name, *context.symbol,
                   *context.typeUses);
    operation += QStringLiteral(": %1").arg(
        typeSpelling(clang_getCursorResultType(cursor)));
    if (clang_CXXMethod_isConst(cursor))
      operation += QStringLiteral(" const");
    if (clang_CXXMethod_isStatic(cursor))
      operation += QStringLiteral(" {static}");
  }
  context.symbol->operations.append(std::move(operation));
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

QString CppImportItem::conflictKey() const {
  return symbol.symbolId.isEmpty()
             ? QString{}
             : QStringLiteral("element:%1").arg(symbol.symbolId);
}

bool CppImportItem::isResolvableConflict() const {
  return action == CppImportAction::Conflict && existingElement.has_value() &&
         !desiredElement.id.isEmpty() &&
         existingElement->id == desiredElement.id;
}

bool CppImportItem::isApplicable() const {
  return action == CppImportAction::Create ||
         action == CppImportAction::Update ||
         (isResolvableConflict() &&
          resolution != CppImportConflictResolution::Unresolved);
}

ModelElement CppImportItem::appliedElement() const {
  Q_ASSERT(isApplicable());
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

bool CppRelationshipImportItem::isResolvableConflict() const {
  return action == CppImportAction::Conflict &&
         existingRelationship.has_value() &&
         !desiredRelationship.id.isEmpty() &&
         existingRelationship->id == desiredRelationship.id;
}

bool CppRelationshipImportItem::isApplicable() const {
  return action == CppImportAction::Create ||
         action == CppImportAction::Update ||
         (isResolvableConflict() &&
          resolution != CppImportConflictResolution::Unresolved);
}

Relationship CppRelationshipImportItem::appliedRelationship() const {
  Q_ASSERT(isApplicable());
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
    options.privateTypeStereotypeApplicableTo =
        privateDefinition->applicableTo;
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
  return elementApplicableCount() + relationshipApplicableCount();
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

bool CppImportService::available() { return UUML_HAS_LIBCLANG != 0; }

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
#if UUML_HAS_LIBCLANG
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
  if (preview.ok) {
    const QStringList roots =
        !preview.sourceRoots.isEmpty()
            ? preview.sourceRoots
            : (preview.sourceRoot.isEmpty() ? QStringList{}
                                            : QStringList{preview.sourceRoot});
    if (!roots.isEmpty())
      project.cppImport.sourceRoots = roots;
  }
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
  return applied;
}

} // namespace uuml
