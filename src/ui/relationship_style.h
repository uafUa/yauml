#pragma once

#include "core/project_data.h"

namespace uuml::ui {

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

RelationshipVisualStyle relationshipVisualStyle(RelationshipType type);

} // namespace uuml::ui
