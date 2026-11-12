#include "core/activity_diagram.h"

#include <array>
#include <utility>

namespace yauml {
namespace {

constexpr std::array nodeNames{
    std::pair{ActivityNodeType::Action, "action"},
    std::pair{ActivityNodeType::Initial, "initial"},
    std::pair{ActivityNodeType::Final, "final"},
    std::pair{ActivityNodeType::Decision, "decision"},
    std::pair{ActivityNodeType::Merge, "merge"},
    std::pair{ActivityNodeType::Fork, "fork"},
    std::pair{ActivityNodeType::Join, "join"}};

} // namespace

QString toString(ActivityNodeType type) {
  for (const auto &[value, name] : nodeNames)
    if (value == type)
      return QString::fromLatin1(name);
  return {};
}

std::optional<ActivityNodeType>
activityNodeTypeFromString(const QString &name) {
  for (const auto &[value, candidate] : nodeNames)
    if (name == QLatin1String(candidate))
      return value;
  return std::nullopt;
}

bool isValidActivityFlow(ActivityNodeType source, ActivityNodeType target) {
  return source != ActivityNodeType::Final &&
         target != ActivityNodeType::Initial;
}

} // namespace yauml
