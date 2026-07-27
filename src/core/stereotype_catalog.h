#pragma once

#include "core/project_data.h"

namespace uuml::stereotype_catalog {

inline const QString kRelationshipApplicability =
    QStringLiteral("relationship");

const QList<StereotypeDefinition> &commonDefinitions();
bool isCommon(const QString &stereotypeId);
const StereotypeDefinition *find(const ProjectData &project,
                                 const QString &stereotypeId);
QString applicabilityFor(ElementType type);
bool appliesTo(const StereotypeDefinition &definition,
               const QString &applicability);
QString displayName(const ProjectData &project, const QString &stereotypeId);
QString displayText(const ProjectData &project,
                    const QStringList &stereotypeIds);

} // namespace uuml::stereotype_catalog
