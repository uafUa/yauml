#pragma once

#include "core/project_data.h"

#include <QVariantMap>

namespace yauml::diagram_filter {

// Returns true when at least one persisted criterion can affect visibility.
bool isActive(const DiagramFilter &filter);

// Filters semantic element presentations. Container frames remain visible to
// preserve hierarchy and clipping context; their child nodes are evaluated
// independently by this function.
bool matchesElement(const ProjectData &project, const ModelElement &element,
                    const DiagramFilter &filter);

QVariantMap toVariantMap(const DiagramFilter &filter);
DiagramFilter fromVariantMap(const QVariantMap &values);

} // namespace yauml::diagram_filter
