#include "core/cpp_import.h"

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
constexpr auto kBindingVersion = 1;

Diagnostic importDiagnostic(DiagnosticSeverity severity, const QString &message,
                            const QString &elementId = {}) {
  return {severity, QStringLiteral("cpp-import"), message, elementId};
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
  return element;
}

bool sourceOwnedStateEquals(const ModelElement &left,
                            const ModelElement &right) {
  return left.type == right.type && left.name == right.name &&
         left.attributes == right.attributes &&
         left.operations == right.operations &&
         left.packageId == right.packageId;
}

QJsonObject relationshipSnapshot(const Relationship &relationship) {
  QJsonObject snapshot;
  snapshot.insert(QStringLiteral("type"), toString(relationship.type));
  snapshot.insert(QStringLiteral("name"), relationship.name);
  snapshot.insert(QStringLiteral("sourceId"), relationship.sourceId);
  snapshot.insert(QStringLiteral("targetId"), relationship.targetId);
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
  return relationship;
}

bool sourceOwnedStateEquals(const Relationship &left,
                            const Relationship &right) {
  return left.type == right.type && left.name == right.name &&
         left.sourceId == right.sourceId && left.targetId == right.targetId;
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

ModelElement sourceElement(const CppSourceSymbol &symbol,
                           const QString &desiredId, const QString &packageId,
                           const ModelElement *existing = nullptr) {
  ModelElement element = existing ? *existing : ModelElement{};
  if (!existing)
    element.id = desiredId;
  element.type = symbol.elementType;
  element.name = symbol.qualifiedName;
  element.packageId = packageId;
  element.attributes = symbol.attributes;
  element.operations = symbol.operations;
  element.enumLiterals.clear();

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
  if (source.relationshipType == RelationshipType::Realization)
    relationship.name = QStringLiteral("implements");
  else if (source.relationshipType == RelationshipType::Generalization)
    relationship.name = QStringLiteral("inherits");
  else
    relationship.name.clear();
  relationship.sourceId = sourceElementId;
  relationship.targetId = targetElementId;

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
  for (const auto &symbol : plannedSymbols) {
    QString id;
    if (const auto *bound = byBinding.value(symbol.symbolId, nullptr)) {
      if (!duplicateBindings.contains(symbol.symbolId))
        id = bound->id;
    } else if (const auto *sameName =
                   byName.value(symbol.qualifiedName, nullptr)) {
      if (sourceBinding(*sameName).isEmpty() &&
          symbol.elementType == ElementType::Package &&
          sameName->type == ElementType::Package)
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
  }

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
            packageIdByNamespacePath.value(symbol.namespacePath));
        item.message = symbol.elementType == ElementType::Package
                           ? QStringLiteral("New C++ namespace package")
                           : QStringLiteral("New C++ type");
      }
      result.items.append(std::move(item));
      continue;
    }

    item.existingElementId = existing->id;
    item.desiredElement = sourceElement(
        symbol, existing->id,
        packageIdByNamespacePath.value(symbol.namespacePath), existing);
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
    } else if (userChanged && !sourceChanged) {
      item.action = CppImportAction::UserModified;
      item.message = QStringLiteral("User-edited model retained");
    } else if (sourceChanged || (converged && userChanged)) {
      item.action = CppImportAction::Update;
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
  TypeUseContext context = TypeUseContext::Signature;
  QString memberName;
  QStringList wrapperTypes;
  bool pointerOrReference = false;
  QString filePath;
  int line = 0;
};

struct TypeTarget {
  QString symbolId;
  QStringList wrapperTypes;
  bool pointerOrReference = false;
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

void collectTypeTargets(CXType type, QStringList wrappers, bool indirect,
                        QList<TypeTarget> &targets, int depth = 0) {
  if (depth > 12 || type.kind == CXType_Invalid)
    return;

  const CXType canonical = clang_getCanonicalType(type);
  switch (canonical.kind) {
  case CXType_Pointer:
  case CXType_LValueReference:
  case CXType_RValueReference:
  case CXType_MemberPointer:
    collectTypeTargets(clang_getPointeeType(canonical), std::move(wrappers),
                       true, targets, depth + 1);
    return;
  case CXType_ConstantArray:
  case CXType_IncompleteArray:
  case CXType_VariableArray:
  case CXType_DependentSizedArray:
    collectTypeTargets(clang_getArrayElementType(canonical),
                       std::move(wrappers), indirect, targets, depth + 1);
    return;
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
    if (!wrapper.isEmpty() && !wrappers.contains(wrapper))
      wrappers.append(wrapper);
    for (int index = 0; index < templateArgumentCount; ++index) {
      collectTypeTargets(
          clang_Type_getTemplateArgumentAsType(templateType, index), wrappers,
          indirect, targets, depth + 1);
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
  targets.append({symbolId, std::move(wrappers), indirect});
}

void appendTypeUses(CXCursor declaration, CXType type,
                    TypeUseContext useContext, const QString &memberName,
                    const CppSourceSymbol &source,
                    QList<DiscoveredTypeUse> &uses) {
  QList<TypeTarget> targets;
  collectTypeTargets(type, {}, false, targets);
  int line = 0;
  const QString filePath = cursorFilePath(declaration, &line);
  for (auto &target : targets) {
    DiscoveredTypeUse use;
    use.sourceSymbolId = source.symbolId;
    use.targetSymbolId = std::move(target.symbolId);
    use.context = useContext;
    use.memberName = memberName;
    use.wrapperTypes = std::move(target.wrapperTypes);
    use.pointerOrReference = target.pointerOrReference;
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
  QString sourceRoot;
  QHash<QString, CppSourceSymbol> *symbols;
  QList<DiscoveredTypeUse> *typeUses;
  QList<Diagnostic> *diagnostics;
  QSet<QString> *declarationFiles;
};

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
  if (filePath.isEmpty() || !pathIsWithin(filePath, context.sourceRoot))
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

QString commonDirectory(const QStringList &filePaths) {
  if (filePaths.isEmpty())
    return {};
  QStringList common =
      QDir::fromNativeSeparators(QFileInfo(filePaths.first()).absolutePath())
          .split(u'/', Qt::SkipEmptyParts);
  for (const QString &path : filePaths.mid(1)) {
    const QStringList parts =
        QDir::fromNativeSeparators(QFileInfo(path).absolutePath())
            .split(u'/', Qt::SkipEmptyParts);
    int shared = 0;
    while (shared < common.size() && shared < parts.size() &&
           common.at(shared).compare(parts.at(shared), Qt::CaseInsensitive) ==
               0)
      ++shared;
    common = common.mid(0, shared);
  }
#ifdef Q_OS_WIN
  if (!common.isEmpty() && common.first().endsWith(u':'))
    return common.join(u'/') + u'/';
#endif
  return u'/' + common.join(u'/');
}

QString inferSourceRoot(const QString &requestedPath,
                        const QString &databasePath,
                        const QStringList &sourceFiles) {
  const QFileInfo requested(requestedPath);
  const QString requestedDirectory =
      normalizedPath(requested.isDir() ? requested.absoluteFilePath()
                                       : requested.absolutePath());
  const QString databaseDirectory = QFileInfo(databasePath).absolutePath();
  if (requested.isDir() &&
      requestedDirectory != normalizedPath(databaseDirectory) &&
      pathIsWithin(databasePath, requestedDirectory))
    return requestedDirectory;

  QString root = commonDirectory(sourceFiles);
  if (root.isEmpty())
    root = databaseDirectory;
  for (QDir candidate(root); candidate.exists();) {
    if (QFileInfo::exists(
            candidate.filePath(QStringLiteral("CMakeLists.txt"))) ||
        QFileInfo::exists(candidate.filePath(QStringLiteral(".git"))))
      return normalizedPath(candidate.absolutePath());
    if (!candidate.cdUp())
      break;
  }
  return normalizedPath(root);
}

struct CompileCommand {
  QString directory;
  QString filePath;
  QStringList arguments;
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

bool scanSourceCommands(const QString &searchPath,
                        QList<CompileCommand> &commands,
                        QStringList &sourceFiles, QString &sourceRoot,
                        QList<Diagnostic> &diagnostics) {
  constexpr qsizetype kMaximumSourceFiles = 10'000;
  constexpr int kMaximumDirectoryDepth = 32;

  const QFileInfo input(searchPath);
  if (!input.exists()) {
    diagnostics.append(importDiagnostic(
        DiagnosticSeverity::Error,
        QStringLiteral("The selected C++ path does not exist: %1")
            .arg(searchPath)));
    return false;
  }
  sourceRoot = normalizedPath(input.isDir() ? input.absoluteFilePath()
                                            : input.absolutePath());

  QStringList implementations;
  QStringList headers;
  if (input.isFile() && (isCppImplementationFile(input.absoluteFilePath()) ||
                         isCppHeaderFile(input.absoluteFilePath()))) {
    (isCppHeaderFile(input.absoluteFilePath()) ? headers : implementations)
        .append(normalizedPath(input.absoluteFilePath()));
  } else if (input.isDir()) {
    QList<QPair<QString, int>> pending{{sourceRoot, 0}};
    while (!pending.isEmpty() &&
           implementations.size() + headers.size() < kMaximumSourceFiles) {
      const auto [directoryPath, depth] = pending.takeFirst();
      const QDir directory(directoryPath);
      for (const QFileInfo &entry :
           directory.entryInfoList(QDir::Dirs | QDir::Files |
                                       QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                   QDir::Name | QDir::IgnoreCase)) {
        if (entry.isDir()) {
          if (depth < kMaximumDirectoryDepth &&
              !excludedSourceDirectory(entry.fileName())) {
            pending.append({entry.absoluteFilePath(), depth + 1});
          }
          continue;
        }
        if (isCppImplementationFile(entry.absoluteFilePath()))
          implementations.append(normalizedPath(entry.absoluteFilePath()));
        else if (isCppHeaderFile(entry.absoluteFilePath()))
          headers.append(normalizedPath(entry.absoluteFilePath()));
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
  }

  std::sort(implementations.begin(), implementations.end());
  std::sort(headers.begin(), headers.end());
  sourceFiles = implementations + headers;
  if (sourceFiles.isEmpty()) {
    diagnostics.append(importDiagnostic(
        DiagnosticSeverity::Error,
        QStringLiteral("No C++ source or header files were found below %1")
            .arg(sourceRoot)));
    return false;
  }

  QStringList includeRoots{sourceRoot};
  for (const QString &candidateName :
       {QStringLiteral("src"), QStringLiteral("source"),
        QStringLiteral("include"), QStringLiteral("inc")}) {
    const QString candidate = QDir(sourceRoot).filePath(candidateName);
    if (QFileInfo(candidate).isDir())
      includeRoots.append(normalizedPath(candidate));
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
    commands.append(std::move(command));
  }
  diagnostics.append(importDiagnostic(
      DiagnosticSeverity::Info,
      QStringLiteral("No compilation database was found; using a best-effort "
                     "scan of %1 C++ source and header file(s)")
          .arg(sourceFiles.size())));
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

CppImportPreview discover(const QString &searchPath,
                          const CppImportOptions &options) {
  CppImportPreview preview;
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
  preview.compilationDatabasePath =
      findCompilationDatabase(searchPath, preview.discoveryDiagnostics, false);
  QList<CompileCommand> commands;
  QStringList sourceFiles;
  if (!preview.compilationDatabasePath.isEmpty()) {
    preview.usedCompilationDatabase = true;
    if (!loadCompilationCommands(preview.compilationDatabasePath, commands,
                                 sourceFiles, preview.discoveryDiagnostics)) {
      preview.diagnostics = preview.discoveryDiagnostics;
      return preview;
    }
    preview.sourceRoot = inferSourceRoot(
        searchPath, preview.compilationDatabasePath, sourceFiles);
  } else if (!scanSourceCommands(searchPath, commands, sourceFiles,
                                 preview.sourceRoot,
                                 preview.discoveryDiagnostics)) {
    preview.diagnostics = preview.discoveryDiagnostics;
    return preview;
  }

  QHash<QString, CppSourceSymbol> symbols;
  QList<DiscoveredTypeUse> typeUses;
  CXIndex index = clang_createIndex(1, 0);
  int parsedTranslationUnits = 0;
  int suppressedDiagnosticCount = 0;
  QSet<QString> seenDiagnosticMessages;
  QSet<QString> declarationFiles;
  const bool bestEffort = !preview.usedCompilationDatabase;
  for (const auto &command : commands) {
    // Source translation units normally expose all declarations in the headers
    // they include. Avoid reparsing those headers as standalone units; headers
    // not reached from any source file are still parsed later in the list.
    if (bestEffort && isCppHeaderFile(command.filePath) &&
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
      preview.discoveryDiagnostics.append(importDiagnostic(
          bestEffort ? DiagnosticSeverity::Warning : DiagnosticSeverity::Error,
          QStringLiteral("Clang could not parse %1 (error %2)")
              .arg(command.filePath)
              .arg(static_cast<int>(error))));
      if (translationUnit)
        clang_disposeTranslationUnit(translationUnit);
      continue;
    }
    ++parsedTranslationUnits;
    appendTranslationUnitDiagnostics(
        translationUnit, preview.discoveryDiagnostics, bestEffort,
        seenDiagnosticMessages, suppressedDiagnosticCount);
    AstVisitorContext visitor{preview.sourceRoot, &symbols, &typeUses,
                              &preview.discoveryDiagnostics, &declarationFiles};
    clang_visitChildren(clang_getTranslationUnitCursor(translationUnit),
                        visitAst, &visitor);
    clang_disposeTranslationUnit(translationUnit);
  }
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
        bestEffort
            ? QStringLiteral("Clang could not parse any discovered C++ file")
            : QStringLiteral(
                  "Clang could not parse any compilation database entry")));
    preview.diagnostics = preview.discoveryDiagnostics;
    return preview;
  }

  preview.symbols = symbols.values();
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

  auto normalizedTypeSet = [](const QStringList &types) {
    QSet<QString> normalized;
    for (const QString &type : types) {
      const QString name = normalizedTemplateName(type);
      if (!name.isEmpty())
        normalized.insert(name);
    }
    return normalized;
  };
  const QSet<QString> owningPointerTypes =
      normalizedTypeSet(options.owningPointerTypes);
  const QSet<QString> sharedPointerTypes =
      normalizedTypeSet(options.sharedPointerTypes);
  const auto matchingWrapper = [](const QStringList &wrappers,
                                  const QSet<QString> &configured) {
    for (const QString &wrapper : wrappers) {
      for (const QString &candidate : configured) {
        if (wrapper == candidate ||
            (!candidate.contains(QStringLiteral("::")) &&
             wrapper.section(QStringLiteral("::"), -1) == candidate))
          return wrapper;
      }
    }
    return QString{};
  };
  const auto pairKey = [](const QString &sourceId, const QString &targetId) {
    return sourceId + u'\x1f' + targetId;
  };

  struct InferredRelationship {
    DiscoveredTypeUse use;
    RelationshipType type = RelationshipType::Dependency;
    QString reason;
    int strength = 0;
  };
  QHash<QString, InferredRelationship> memberRelationships;
  QHash<QString, InferredRelationship> signatureRelationships;
  QSet<QString> inheritedPairs;
  for (const auto &relationship : std::as_const(preview.relationships)) {
    inheritedPairs.insert(
        pairKey(relationship.sourceSymbolId, relationship.targetSymbolId));
  }

  for (const auto &use : std::as_const(typeUses)) {
    if (use.sourceSymbolId == use.targetSymbolId ||
        !symbols.contains(use.sourceSymbolId) ||
        !symbols.contains(use.targetSymbolId))
      continue;
    const QString key = pairKey(use.sourceSymbolId, use.targetSymbolId);
    if (inheritedPairs.contains(key))
      continue;

    InferredRelationship candidate;
    candidate.use = use;
    if (use.context == TypeUseContext::Signature) {
      candidate.type = RelationshipType::Dependency;
      candidate.strength = 1;
      candidate.reason =
          QStringLiteral("Referenced by an operation parameter or return type");
      signatureRelationships.tryInsert(key, std::move(candidate));
      continue;
    }

    const QString owningWrapper =
        matchingWrapper(use.wrapperTypes, owningPointerTypes);
    const QString sharedWrapper =
        matchingWrapper(use.wrapperTypes, sharedPointerTypes);
    if (!owningWrapper.isEmpty()) {
      candidate.type = RelationshipType::Composition;
      candidate.strength = 4;
      candidate.reason =
          QStringLiteral("Member %1 uses configured owning pointer type %2")
              .arg(use.memberName, owningWrapper);
    } else if (!sharedWrapper.isEmpty()) {
      candidate.type = RelationshipType::Aggregation;
      candidate.strength = 3;
      candidate.reason =
          QStringLiteral("Member %1 uses configured shared pointer type %2")
              .arg(use.memberName, sharedWrapper);
    } else if (use.pointerOrReference) {
      candidate.type = RelationshipType::Aggregation;
      candidate.strength = 3;
      candidate.reason =
          QStringLiteral("Member %1 is a raw pointer or reference")
              .arg(use.memberName);
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

    const auto existing = memberRelationships.constFind(key);
    if (existing == memberRelationships.cend() ||
        candidate.strength > existing->strength) {
      memberRelationships.insert(key, std::move(candidate));
    }
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
    relationship.classificationReason = candidate.reason;
    relationship.filePath = candidate.use.filePath;
    relationship.line = candidate.use.line;
    // Member and signature evidence share one identity. Moving a type from a
    // field into an operation signature therefore reclassifies the existing
    // relationship instead of leaving a missing structural edge and creating
    // a duplicate dependency.
    relationship.symbolId =
        QStringLiteral("cpp:type-use:%1->%2")
            .arg(relationship.sourceSymbolId, relationship.targetSymbolId);
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
    if (!memberRelationships.contains(iterator.key()))
      appendInferred(iterator.value(), QStringLiteral("signature"));
  }

  std::sort(preview.relationships.begin(), preview.relationships.end(),
            [](const CppSourceRelationship &left,
               const CppSourceRelationship &right) {
              if (left.sourceName != right.sourceName)
                return left.sourceName < right.sourceName;
              if (left.targetName != right.targetName)
                return left.targetName < right.targetName;
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

QString CppImportOptions::defaultInterfacePattern() {
  return QStringLiteral("^I[A-Z].*$");
}

QStringList CppImportOptions::defaultOwningPointerTypes() {
  return {QStringLiteral("std::unique_ptr")};
}

QStringList CppImportOptions::defaultSharedPointerTypes() {
  return {QStringLiteral("std::shared_ptr")};
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

bool CppImportService::available() { return UUML_HAS_LIBCLANG != 0; }

CppImportPreview
CppImportService::preview(const QString &searchPath,
                          const QList<ModelElement> &existingElements,
                          const QList<Relationship> &existingRelationships,
                          const CppImportOptions &options) {
#if UUML_HAS_LIBCLANG
  return planImport(discover(searchPath, options), existingElements,
                    existingRelationships);
#else
  Q_UNUSED(searchPath)
  Q_UNUSED(existingElements)
  Q_UNUSED(existingRelationships)
  Q_UNUSED(options)
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
  if (preview.ok && !preview.sourceRoot.isEmpty())
    project.cppImport.sourceRoot = preview.sourceRoot;
  int applied = 0;
  for (const auto &item : preview.items) {
    if (!item.isApplicable())
      continue;
    if (auto *existing = findElement(project, item.desiredElement.id))
      *existing = item.desiredElement;
    else
      project.elements.append(item.desiredElement);
    ++applied;
  }
  for (const auto &item : preview.relationshipItems) {
    if (!item.isApplicable())
      continue;
    if (auto *existing = findRelationship(project, item.desiredRelationship.id))
      *existing = item.desiredRelationship;
    else
      project.relationships.append(item.desiredRelationship);
    ++applied;
  }
  return applied;
}

} // namespace uuml
