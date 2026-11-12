#include "core/project_controller.h"

#include "core/project_commands.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace yauml {
namespace {

const ActivityDiagramData *activityGraph(const ProjectData &project,
                                         const QString &diagramId) {
  const auto *diagram = findDiagram(project, diagramId);
  return diagram && diagram->type == DiagramType::Activity ? &diagram->activity
                                                           : nullptr;
}

template <typename Record>
qsizetype indexOfActivityRecord(const QList<Record> &records,
                                const QString &id) {
  const auto it =
      std::find_if(records.cbegin(), records.cend(),
                   [&](const Record &item) { return item.id == id; });
  return it == records.cend() ? -1 : it - records.cbegin();
}

template <typename Record> struct ActivityChange {
  qsizetype row;
  std::optional<Record> before;
  std::optional<Record> after;
};

// Changes are recorded in original row order. Removing from the end and
// inserting from the start preserves unaffected rows and exact undo order.
template <typename Record>
void applyChanges(QList<Record> &records,
                  const QList<ActivityChange<Record>> &changes, bool forward) {
  for (auto it = changes.crbegin(); it != changes.crend(); ++it)
    if (forward ? it->before.has_value() : it->after.has_value())
      records.removeAt(it->row);
  for (const auto &change : changes) {
    const auto &record = forward ? change.after : change.before;
    if (record)
      records.insert(change.row, *record);
  }
}

class EditActivityGraphCommand final : public ProjectCommand {
public:
  EditActivityGraphCommand(ProjectController *controller, QString diagramId,
                           const QString &description,
                           QList<ActivityChange<ActivityNode>> nodes,
                           QList<ActivityChange<ActivityFlow>> flows)
      : ProjectCommand(controller, description),
        m_diagramId(std::move(diagramId)), m_nodes(std::move(nodes)),
        m_flows(std::move(flows)) {}

private:
  bool affectsProjectTree() const override { return false; }
  void execute(ProjectData &project) override { apply(project, true); }
  void revert(ProjectData &project) override { apply(project, false); }

  void apply(ProjectData &project, bool forward) {
    if (auto *diagram = findDiagram(project, m_diagramId)) {
      applyChanges(diagram->activity.nodes, m_nodes, forward);
      applyChanges(diagram->activity.flows, m_flows, forward);
    }
  }

  QString m_diagramId;
  QList<ActivityChange<ActivityNode>> m_nodes;
  QList<ActivityChange<ActivityFlow>> m_flows;
};

using NodeChanges = QList<ActivityChange<ActivityNode>>;
using FlowChanges = QList<ActivityChange<ActivityFlow>>;

bool isFinite(const QPointF &point) {
  return std::isfinite(point.x()) && std::isfinite(point.y());
}

} // namespace

QString ProjectController::addActivityDiagram() {
  Diagram diagram;
  diagram.id = newId();
  diagram.name = tr("Activity Diagram %1").arg(m_data.diagrams.size() + 1);
  diagram.type = DiagramType::Activity;
  diagram.activity.nodes.append(
      {newId(), ActivityNodeType::Initial, {}, {160.0, 100.0}, {}});
  const QString id = diagram.id;
  pushCommand(
      std::make_unique<CreateDiagramCommand>(this, m_data, std::move(diagram)));
  selectObject(id, QStringLiteral("diagram"));
  return id;
}

QString ProjectController::addActivityNode(const QString &diagramId,
                                           ActivityNodeType type,
                                           const QPointF &center,
                                           const QString &flowSourceId) {
  const auto *graph = activityGraph(m_data, diagramId);
  if (!graph || !isFinite(center) || toString(type).isEmpty())
    return {};
  if (!flowSourceId.isEmpty()) {
    const auto source = indexOfActivityRecord(graph->nodes, flowSourceId);
    if (source < 0 || !isValidActivityFlow(graph->nodes.at(source).type, type))
      return {};
  }
  const ActivityNode node{newId(),
                          type,
                          type == ActivityNodeType::Action ? tr("Action")
                                                           : QString{},
                          center,
                          {}};
  FlowChanges flows;
  if (!flowSourceId.isEmpty())
    flows.append({graph->flows.size(), std::nullopt,
                  ActivityFlow{newId(), flowSourceId, node.id, {}, {}}});
  pushCommand(std::make_unique<EditActivityGraphCommand>(
      this, diagramId,
      flowSourceId.isEmpty() ? tr("Add activity node")
                             : tr("Add connected activity node"),
      NodeChanges{{graph->nodes.size(), std::nullopt, node}},
      std::move(flows)));
  return node.id;
}

QString ProjectController::addActivityFlow(const QString &diagramId,
                                           const QString &sourceId,
                                           const QString &targetId) {
  const auto *graph = activityGraph(m_data, diagramId);
  if (!graph)
    return {};
  const auto source = indexOfActivityRecord(graph->nodes, sourceId);
  const auto target = indexOfActivityRecord(graph->nodes, targetId);
  if (source < 0 || target < 0 ||
      !isValidActivityFlow(graph->nodes.at(source).type,
                           graph->nodes.at(target).type))
    return {};
  const ActivityFlow flow{newId(), sourceId, targetId, {}, {}};
  pushCommand(std::make_unique<EditActivityGraphCommand>(
      this, diagramId, tr("Add control flow"), NodeChanges{},
      FlowChanges{{graph->flows.size(), std::nullopt, flow}}));
  return flow.id;
}

bool ProjectController::moveActivityNode(const QString &diagramId,
                                         const QString &nodeId,
                                         const QPointF &center) {
  return moveActivityNodes(diagramId, {{nodeId, center}},
                           tr("Move activity node"));
}

bool ProjectController::moveActivityNodes(
    const QString &diagramId, const QHash<QString, QPointF> &centers,
    const QString &description) {
  const auto *graph = activityGraph(m_data, diagramId);
  if (!graph || centers.isEmpty())
    return false;
  NodeChanges nodes;
  for (qsizetype row = 0; row < graph->nodes.size(); ++row) {
    const auto &before = graph->nodes.at(row);
    const auto center = centers.constFind(before.id);
    if (center == centers.cend() || *center == before.center)
      continue;
    if (!isFinite(*center))
      return false;
    ActivityNode after = before;
    after.center = *center;
    nodes.append({row, before, std::move(after)});
  }
  if (nodes.isEmpty())
    return false;
  pushCommand(std::make_unique<EditActivityGraphCommand>(
      this, diagramId, description, std::move(nodes), FlowChanges{}));
  return true;
}

bool ProjectController::editActivityItemText(const QString &diagramId,
                                             const QString &itemId,
                                             const QString &text) {
  const auto *graph = activityGraph(m_data, diagramId);
  if (!graph)
    return false;
  const QString value = text.trimmed();
  if (const auto row = indexOfActivityRecord(graph->nodes, itemId); row >= 0) {
    const ActivityNode before = graph->nodes.at(row);
    if (before.type != ActivityNodeType::Action || before.name == value ||
        value.isEmpty())
      return false;
    ActivityNode after = before;
    after.name = value;
    pushCommand(std::make_unique<EditActivityGraphCommand>(
        this, diagramId, tr("Rename action"), NodeChanges{{row, before, after}},
        FlowChanges{}));
    return true;
  }
  const auto row = indexOfActivityRecord(graph->flows, itemId);
  if (row < 0 || graph->flows.at(row).guard == value)
    return false;
  const ActivityFlow before = graph->flows.at(row);
  ActivityFlow after = before;
  after.guard = value;
  pushCommand(std::make_unique<EditActivityGraphCommand>(
      this, diagramId, tr("Edit flow guard"), NodeChanges{},
      FlowChanges{{row, before, after}}));
  return true;
}

bool ProjectController::deleteActivityItem(const QString &diagramId,
                                           const QString &itemId) {
  return deleteActivityItems(diagramId, {itemId});
}

bool ProjectController::deleteActivityItems(const QString &diagramId,
                                            const QStringList &itemIds) {
  const auto *graph = activityGraph(m_data, diagramId);
  if (!graph || itemIds.isEmpty())
    return false;
  const QSet<QString> ids(itemIds.cbegin(), itemIds.cend());
  NodeChanges nodes;
  FlowChanges flows;
  for (qsizetype row = 0; row < graph->nodes.size(); ++row) {
    const auto &node = graph->nodes.at(row);
    if (ids.contains(node.id))
      nodes.append({row, node, std::nullopt});
  }
  for (qsizetype row = 0; row < graph->flows.size(); ++row) {
    const auto &flow = graph->flows.at(row);
    if (ids.contains(flow.id) || ids.contains(flow.sourceId) ||
        ids.contains(flow.targetId))
      flows.append({row, flow, std::nullopt});
  }
  if (nodes.isEmpty() && flows.isEmpty())
    return false;
  pushCommand(std::make_unique<EditActivityGraphCommand>(
      this, diagramId, tr("Delete activity selection"), std::move(nodes),
      std::move(flows)));
  return true;
}

} // namespace yauml
