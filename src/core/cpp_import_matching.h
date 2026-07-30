#pragma once

#include "core/cpp_import.h"

#include <QList>
#include <QString>

namespace yauml {

// A source declaration captured by the previous successful import. Matching
// deliberately uses the baseline rather than the current model fields because
// users remain authoritative and may have renamed or edited the model.
struct CppImportedDeclaration {
  QString elementId;
  QString symbolId;
  QString qualifiedName;
  QString filePath;
  int line = 0;
  ElementType elementType = ElementType::Class;
  QStringList attributes;
  QList<ModelOperation> operations;

  bool operator==(const CppImportedDeclaration &) const = default;
};

struct CppDeclarationMatch {
  QString sourceSymbolId;
  QString previousSymbolId;
  QString elementId;
  int confidence = 0;

  bool operator==(const CppDeclarationMatch &) const = default;
};

// Returns only unique, mutual-best matches above a conservative confidence
// threshold. Ambiguous declarations are intentionally left unmatched so the
// import preview reports separate new/missing records instead of guessing.
QList<CppDeclarationMatch> matchRenamedCppDeclarations(
    const QList<CppSourceSymbol> &sourceDeclarations,
    const QList<CppImportedDeclaration> &importedDeclarations);

} // namespace yauml
