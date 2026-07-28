#include "ui/relationship_style.h"

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

} // namespace yauml::ui
