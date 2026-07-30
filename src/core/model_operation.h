#pragma once

#include "core/project_data.h"

#include <QJsonArray>
#include <QJsonObject>
#include <optional>

namespace yauml {

// Parses and formats the editable UML-like operation notation used by the
// diagram. Structured C++ import bypasses this parser; it exists for legacy
// migration and the deliberately convenient text editors.
ModelOperation modelOperationFromSignature(const QString &signature,
                                           const QString &stableId = {});
ModelOperation modelOperationWithEditedSignature(const ModelOperation &existing,
                                                 const QString &signature);

QString modelOperationSignature(
    const ModelOperation &operation,
    OperationSignatureMode mode = OperationSignatureMode::Full);
QStringList modelOperationSignatures(
    const QList<ModelOperation> &operations,
    OperationSignatureMode mode = OperationSignatureMode::Full);
QString modelOperationsText(const QList<ModelOperation> &operations);
QList<ModelOperation>
modelOperationsFromText(const QString &text,
                        const QList<ModelOperation> &existing = {});

// Source identities and locations are synchronization metadata, not part of
// the UML operation signature. Import matching uses this comparison so a
// declaration rename or line movement does not masquerade as a member edit.
bool modelOperationSemanticallyEqual(const ModelOperation &left,
                                     const ModelOperation &right);
bool modelOperationsSemanticallyEqual(const QList<ModelOperation> &left,
                                      const QList<ModelOperation> &right);

QJsonObject modelOperationToJson(const ModelOperation &operation);
QJsonArray modelOperationsToJson(const QList<ModelOperation> &operations);
std::optional<ModelOperation>
modelOperationFromJson(const QJsonValue &value,
                       QString *errorMessage = nullptr);
std::optional<QList<ModelOperation>>
modelOperationsFromJson(const QJsonValue &value,
                        QString *errorMessage = nullptr);

} // namespace yauml
