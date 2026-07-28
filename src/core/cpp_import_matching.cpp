#include "core/cpp_import_matching.h"

#include <QDir>
#include <QFileInfo>

namespace uuml {
namespace {

constexpr int kMinimumMatchConfidence = 75;

QString comparablePath(const QString &path) {
  if (path.trimmed().isEmpty())
    return {};
  QString result = QDir::fromNativeSeparators(
      QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
  result = result.toLower();
#endif
  return result;
}

QString unqualifiedName(const QString &qualifiedName) {
  return qualifiedName.section(QStringLiteral("::"), -1);
}

int matchConfidence(const CppSourceSymbol &source,
                    const CppImportedDeclaration &imported) {
  if (source.elementType != imported.elementType)
    return 0;

  int confidence = 0;
  const QString sourcePath = comparablePath(source.filePath);
  const QString importedPath = comparablePath(imported.filePath);
  const bool sameFile = !sourcePath.isEmpty() && sourcePath == importedPath;
  const bool sameQualifiedName =
      source.qualifiedName == imported.qualifiedName;
  const bool sameUnqualifiedName =
      unqualifiedName(source.qualifiedName) ==
      unqualifiedName(imported.qualifiedName);
  const bool attributesMatch =
      !source.attributes.isEmpty() && source.attributes == imported.attributes;
  const bool operationsMatch =
      !source.operations.isEmpty() && source.operations == imported.operations;

  // Location, name, and structure are independent evidence categories. Never
  // preserve identity based on only one of them, even when the numeric score
  // would otherwise reach the threshold.
  const int independentEvidence =
      (sameFile ? 1 : 0) +
      ((sameQualifiedName || sameUnqualifiedName) ? 1 : 0) +
      ((attributesMatch || operationsMatch) ? 1 : 0);
  if (independentEvidence < 2)
    return 0;

  if (sameFile) {
    confidence += 40;
    if (source.line > 0 && imported.line > 0) {
      const int lineDistance = qAbs(source.line - imported.line);
      if (lineDistance == 0)
        confidence += 35;
      else if (lineDistance <= 4)
        confidence += 25;
      else if (lineDistance <= 12)
        confidence += 15;
    }
  }

  if (sameQualifiedName) {
    // A Clang identity can change after moving a declaration between files
    // even when its language-level name remains stable.
    confidence += 60;
  } else if (sameUnqualifiedName) {
    confidence += 25;
  }

  if (attributesMatch)
    confidence += 15;
  if (operationsMatch)
    confidence += 15;
  if (attributesMatch && operationsMatch)
    confidence += 10;
  return confidence;
}

struct BestCandidate {
  int index = -1;
  int confidence = 0;
  bool tied = false;
};

void considerCandidate(BestCandidate &best, int index, int confidence) {
  if (confidence > best.confidence) {
    best = {index, confidence, false};
  } else if (confidence > 0 && confidence == best.confidence) {
    best.tied = true;
  }
}

} // namespace

QList<CppDeclarationMatch> matchRenamedCppDeclarations(
    const QList<CppSourceSymbol> &sourceDeclarations,
    const QList<CppImportedDeclaration> &importedDeclarations) {
  QList<BestCandidate> bestImportedForSource(sourceDeclarations.size());
  QList<BestCandidate> bestSourceForImported(importedDeclarations.size());

  for (qsizetype sourceIndex = 0; sourceIndex < sourceDeclarations.size();
       ++sourceIndex) {
    for (qsizetype importedIndex = 0;
         importedIndex < importedDeclarations.size(); ++importedIndex) {
      const int confidence =
          matchConfidence(sourceDeclarations.at(sourceIndex),
                          importedDeclarations.at(importedIndex));
      considerCandidate(bestImportedForSource[sourceIndex],
                        static_cast<int>(importedIndex), confidence);
      considerCandidate(bestSourceForImported[importedIndex],
                        static_cast<int>(sourceIndex), confidence);
    }
  }

  QList<CppDeclarationMatch> matches;
  for (qsizetype sourceIndex = 0; sourceIndex < sourceDeclarations.size();
       ++sourceIndex) {
    const BestCandidate &sourceBest = bestImportedForSource.at(sourceIndex);
    if (sourceBest.index < 0 || sourceBest.tied ||
        sourceBest.confidence < kMinimumMatchConfidence)
      continue;
    const BestCandidate &importedBest =
        bestSourceForImported.at(sourceBest.index);
    if (importedBest.tied ||
        importedBest.index != static_cast<int>(sourceIndex))
      continue;

    const CppSourceSymbol &source = sourceDeclarations.at(sourceIndex);
    const CppImportedDeclaration &imported =
        importedDeclarations.at(sourceBest.index);
    matches.append({source.symbolId, imported.symbolId, imported.elementId,
                    sourceBest.confidence});
  }
  return matches;
}

} // namespace uuml
