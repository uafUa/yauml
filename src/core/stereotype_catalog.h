#pragma once

#include "core/project_data.h"

namespace yauml::stereotype_catalog {

inline const QString kRelationshipApplicability =
    QStringLiteral("relationship");
inline const QString kLocalStereotypeId = QStringLiteral("uml.local");
inline const QString kPrivateStereotypeId = QStringLiteral("uml.private");
inline const QString kApiStereotypeId = QStringLiteral("uml.api");

// New projects and schema migrations copy these conventional UML
// definitions into the project-owned catalog. They are templates only: once
// copied, every definition can be edited or deleted like any other project
// stereotype.
const QList<StereotypeDefinition> &defaultDefinitions();
// The source-visibility definitions were introduced after the initial
// project-owned catalog. Keeping the subset explicit lets schema migration add
// only these entries without recreating older defaults a user deliberately
// deleted.
const QList<StereotypeDefinition> &sourceVisibilityDefinitions();
const StereotypeDefinition *find(const ProjectData &project,
                                 const QString &stereotypeId);
// A migrated project can already contain a user-created stereotype with a
// conventional name and a different ID. Source import resolves the semantic
// definition by stable ID first, then by its case-insensitive name.
const StereotypeDefinition *
findByConventionalIdOrName(const ProjectData &project,
                           const QString &stereotypeId,
                           const QString &conventionalName);
QString applicabilityFor(ElementType type);
bool appliesTo(const StereotypeDefinition &definition,
               const QString &applicability);
QString displayName(const ProjectData &project, const QString &stereotypeId);
QString displayText(const ProjectData &project,
                    const QStringList &stereotypeIds);

} // namespace yauml::stereotype_catalog
