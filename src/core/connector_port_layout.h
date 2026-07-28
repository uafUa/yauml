#pragma once

#include "core/project_data.h"

#include <QVector>

namespace yauml::connector_ports {

// Odd counts retain a point at the exact center of every side. Keeping the
// upper bound modest avoids unusably dense markers on small presentations.
inline constexpr int kDefaultSnapPointCount = 1;
inline constexpr int kMaximumSnapPointCount = 31;

bool isValidSnapPointCount(int count);
int normalizedSnapPointCount(int count);

// Returns evenly spaced relative offsets excluding the corners. For example,
// three points produce 0.25, 0.5, and 0.75.
QVector<qreal> snapOffsets(int count);

// Snaps a free relative offset when its physical distance along the side is
// within tolerance. The returned value remains unchanged when no point is
// close enough.
qreal snapOffset(qreal freeOffset, qreal sideLength, int pointCount,
                 qreal tolerance, bool *snapped = nullptr);

// Moves an offset that lies on the old snap grid to the corresponding logical
// marker on a resized grid. Markers are identified by their signed ordinal
// around the center point, so adding markers retains left/right or top/bottom
// ordering. Freely placed offsets are returned unchanged.
qreal remapAttachedOffset(qreal offset, int beforePointCount,
                          int afterPointCount, bool *attached = nullptr);

int snapPointCountForSide(const NodePresentation &node, ConnectorSide side);

} // namespace yauml::connector_ports
