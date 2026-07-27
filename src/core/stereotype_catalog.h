#pragma once

#include "core/project_data.h"

namespace uuml::stereotype_catalog {

inline const QString kRelationshipApplicability =
    QStringLiteral("relationship");

// New projects and the schema-1 migration copy these conventional UML
// definitions into the project-owned catalog. They are templates only: once
// copied, every definition can be edited or deleted like any other project
// stereotype.
const QList<StereotypeDefinition> &defaultDefinitions();
const StereotypeDefinition *find(const ProjectData &project,
                                 const QString &stereotypeId);
QString applicabilityFor(ElementType type);
bool appliesTo(const StereotypeDefinition &definition,
               const QString &applicability);
QString displayName(const ProjectData &project, const QString &stereotypeId);
QString displayText(const ProjectData &project,
                    const QStringList &stereotypeIds);

} // namespace uuml::stereotype_catalog
