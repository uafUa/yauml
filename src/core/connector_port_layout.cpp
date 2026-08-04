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

QVector<qreal> availableSnapOffsets(int beforePointCount, int afterPointCount,
                                    const QList<qreal> &occupiedBeforeOffsets) {
  const QVector<qreal> offsets = snapOffsets(afterPointCount);
  QVector<bool> occupied(offsets.size(), false);
  for (const qreal beforeOffset : occupiedBeforeOffsets) {
    bool attached = false;
    const qreal afterOffset = remapAttachedOffset(
        beforeOffset, beforePointCount, afterPointCount, &attached);
    if (!attached)
      continue;
    constexpr qreal kOffsetTolerance = 0.000001;
    for (int index = 0; index < offsets.size(); ++index) {
      if (std::abs(offsets.at(index) - afterOffset) <= kOffsetTolerance) {
        occupied[index] = true;
        break;
      }
    }
  }

  QVector<qreal> available;
  available.reserve(offsets.size());
  for (int index = 0; index < offsets.size(); ++index) {
    if (!occupied.at(index))
      available.append(offsets.at(index));
  }
  return available;
}

QVector<qreal> spreadAcrossAvailableOffsets(const QVector<qreal> &available,
                                            qsizetype requestedCount) {
  QVector<qreal> result;
  if (requestedCount <= 0 || available.size() < requestedCount)
    return result;
  result.reserve(requestedCount);
  if (requestedCount == 1) {
    result.append(available.at(available.size() / 2));
    return result;
  }

  // Use the complete available range while retaining strict ordering. This
  // avoids bunching a two-connector fan around one end merely because every
  // side has an odd number of snap points.
  int previousIndex = -1;
  for (qsizetype index = 0; index < requestedCount; ++index) {
    const qreal proportionalIndex = static_cast<qreal>(index) *
                                    (available.size() - 1) /
                                    (requestedCount - 1);
    int selectedIndex = qRound(proportionalIndex);
    selectedIndex = std::max(selectedIndex, previousIndex + 1);
    const int lastAllowed =
        available.size() - static_cast<int>(requestedCount - index);
    selectedIndex = std::min(selectedIndex, lastAllowed);
    result.append(available.at(selectedIndex));
    previousIndex = selectedIndex;
  }
  return result;
}

} // namespace yauml::connector_ports
