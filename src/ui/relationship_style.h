#pragma once

#include "core/project_data.h"

#include <QtGlobal>

namespace yauml::ui {

enum class RelationshipLineStyle { Solid, Dashed };
enum class RelationshipDecoration {
  None,
  OpenArrow,
  HollowTriangle,
  HollowDiamond,
  FilledDiamond,
  CirclePlus
};

struct RelationshipVisualStyle {
  RelationshipLineStyle line = RelationshipLineStyle::Solid;
  RelationshipDecoration source = RelationshipDecoration::None;
  RelationshipDecoration target = RelationshipDecoration::None;

  bool operator==(const RelationshipVisualStyle &) const = default;
};

struct RelationshipDashPattern {
  qreal dashLength = 0.0;
  qreal gapLength = 0.0;
};

RelationshipVisualStyle relationshipVisualStyle(RelationshipType type);
RelationshipDashPattern relationshipDashPattern(qreal zoom);
qsizetype relationshipDashSegmentCount(qreal lineLength, qreal zoom);

} // namespace yauml::ui
