#include "core/connector_port_layout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace yauml::connector_ports {

bool isValidSnapPointCount(int count) {
  return count >= kDefaultSnapPointCount && count <= kMaximumSnapPointCount &&
         count % 2 == 1;
}

int normalizedSnapPointCount(int count) {
  count = std::clamp(count, kDefaultSnapPointCount, kMaximumSnapPointCount);
  if (count % 2 == 0)
    count += count < kMaximumSnapPointCount ? 1 : -1;
  return count;
}

QVector<qreal> snapOffsets(int count) {
  count = normalizedSnapPointCount(count);
  QVector<qreal> offsets;
  offsets.reserve(count);
  const qreal divisor = static_cast<qreal>(count + 1);
  for (int index = 1; index <= count; ++index)
    offsets.append(static_cast<qreal>(index) / divisor);
  return offsets;
}

qreal snapOffset(qreal freeOffset, qreal sideLength, int pointCount,
                 qreal tolerance, bool *snapped) {
  if (snapped)
    *snapped = false;
  freeOffset = std::clamp(freeOffset, 0.0, 1.0);
  if (sideLength <= 0.0 || tolerance < 0.0)
    return freeOffset;

  qreal nearestOffset = freeOffset;
  qreal nearestDistance = std::numeric_limits<qreal>::max();
  for (const qreal candidate : snapOffsets(pointCount)) {
    const qreal distance = std::abs(candidate - freeOffset) * sideLength;
    if (distance < nearestDistance) {
      nearestDistance = distance;
      nearestOffset = candidate;
    }
  }
  if (nearestDistance > tolerance)
    return freeOffset;
  if (snapped)
    *snapped = true;
  return nearestOffset;
}

qreal remapAttachedOffset(qreal offset, int beforePointCount,
                          int afterPointCount, bool *attached) {
  if (attached)
    *attached = false;
  const QVector<qreal> before = snapOffsets(beforePointCount);
  const QVector<qreal> after = snapOffsets(afterPointCount);
  constexpr qreal kAttachedOffsetTolerance = 0.000001;
  int beforeIndex = -1;
  for (int index = 0; index < before.size(); ++index) {
    if (std::abs(before.at(index) - offset) <= kAttachedOffsetTolerance) {
      beforeIndex = index;
      break;
    }
  }
  if (beforeIndex < 0)
    return offset;

  if (attached)
    *attached = true;
  const qsizetype ordinal = beforeIndex - before.size() / 2;
  const qsizetype afterIndex =
      std::clamp(after.size() / 2 + ordinal, qsizetype{0}, after.size() - 1);
  return after.at(afterIndex);
}

int snapPointCountForSide(const NodePresentation &node, ConnectorSide side) {
  switch (side) {
  case ConnectorSide::Top:
  case ConnectorSide::Bottom:
    return normalizedSnapPointCount(node.horizontalPortSnapPoints);
  case ConnectorSide::Right:
  case ConnectorSide::Left:
    return normalizedSnapPointCount(node.verticalPortSnapPoints);
  case ConnectorSide::Automatic:
    return kDefaultSnapPointCount;
  }
  return kDefaultSnapPointCount;
}

} // namespace yauml::connector_ports
