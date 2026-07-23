#pragma once

#include "core/project_data.h"

namespace uuml::project_style {

// Returns an explicitly assigned style ID for a project-browser subject.
// Supported kinds are "element"/"package", "folder", and "namespace".
QString explicitStyleId(const ProjectData &project, const QString &kind,
                        const QString &id);

// Resolves the nearest named style through the effective project-browser
// hierarchy, including the subject itself.
const DiagramStyle *effectiveStyleForSubject(const ProjectData &project,
                                             const QString &kind,
                                             const QString &id);

// Presentation assignment has the highest priority, followed by its subject
// and the subject's nearest styled browser ancestor.
const DiagramStyle *effectiveStyleForNode(const ProjectData &project,
                                          const NodePresentation &node);
const DiagramStyle *
effectiveStyleForContainer(const ProjectData &project,
                           const ContainerPresentation &container);

} // namespace uuml::project_style
