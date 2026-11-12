#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <optional>

namespace yauml {

enum class ActivityNodeType {
  Action,
  Initial,
  Final,
  Decision,
  Merge,
  Fork,
  Join
};

QString toString(ActivityNodeType type);
std::optional<ActivityNodeType> activityNodeTypeFromString(const QString &name);

struct ActivityNode {
  QString id;
  ActivityNodeType type = ActivityNodeType::Action;
  QString name;
  QPointF center;
  QJsonObject extra;

  bool operator==(const ActivityNode &) const = default;
};

struct ActivityFlow {
  QString id;
  QString sourceId;
  QString targetId;
  QString guard;
  QJsonObject extra;

  bool operator==(const ActivityFlow &) const = default;
};

// Activity nodes and control flows are local to their diagram. Their IDs are
// stable references within the graph, independent of the class model.
struct ActivityDiagramData {
  QList<ActivityNode> nodes;
  QList<ActivityFlow> flows;
  QJsonObject extra;

  bool operator==(const ActivityDiagramData &) const = default;
};

bool isValidActivityFlow(ActivityNodeType source, ActivityNodeType target);

} // namespace yauml
