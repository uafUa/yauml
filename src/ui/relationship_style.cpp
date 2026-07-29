#include "ui/relationship_style.h"

#include <cmath>

namespace yauml::ui {

RelationshipVisualStyle relationshipVisualStyle(RelationshipType type) {
  switch (type) {
  case RelationshipType::Dependency:
    return {RelationshipLineStyle::Dashed, RelationshipDecoration::None,
            RelationshipDecoration::OpenArrow};
  case RelationshipType::Generalization:
    return {RelationshipLineStyle::Solid, RelationshipDecoration::None,
            RelationshipDecoration::HollowTriangle};
  case RelationshipType::Realization:
    return {RelationshipLineStyle::Dashed, RelationshipDecoration::None,
            RelationshipDecoration::HollowTriangle};
  case RelationshipType::Association:
    return {RelationshipLineStyle::Solid, RelationshipDecoration::None,
            RelationshipDecoration::OpenArrow};
  case RelationshipType::Aggregation:
    return {RelationshipLineStyle::Solid, RelationshipDecoration::HollowDiamond,
            RelationshipDecoration::None};
  case RelationshipType::Composition:
    return {RelationshipLineStyle::Solid, RelationshipDecoration::FilledDiamond,
            RelationshipDecoration::None};
  case RelationshipType::Containment:
    // UML nesting uses a circle-plus marker at the containing namespace/type.
    return {RelationshipLineStyle::Solid, RelationshipDecoration::CirclePlus,
            RelationshipDecoration::None};
  }
  return {};
}

RelationshipDashPattern relationshipDashPattern(qreal zoom) {
  if (zoom <= 0.0)
    return {};

  // Store the pattern in scene units so the transformed result remains an
  // 8-pixel dash followed by a 5-pixel gap at every zoom level.
  return {8.0 / zoom, 5.0 / zoom};
}

qsizetype relationshipDashSegmentCount(qreal lineLength, qreal zoom) {
  if (lineLength <= 0.0)
    return 0;

  const RelationshipDashPattern pattern = relationshipDashPattern(zoom);
  const qreal period = pattern.dashLength + pattern.gapLength;
  if (period <= 0.0)
    return 0;

  // Do not cap the count by stretching the pattern: connector length must
  // never affect the visible dash or gap length.
  return static_cast<qsizetype>(std::ceil(lineLength / period));
}

} // namespace yauml::ui
