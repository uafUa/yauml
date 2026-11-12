#include "ui/diagram_canvas.h"

#include "ui/activity_diagram_layout.h"
#include "ui/diagram_arrangement.h"

#include <QKeySequence>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <utility>

namespace yauml {
namespace {

constexpr qreal kActivityDragThreshold = 4.0;

} // namespace

bool DiagramCanvas::isActivityDiagram() const {
  const auto *current = diagram();
  return current && current->type == DiagramType::Activity;
}

ui::ActivityDiagramLayout DiagramCanvas::activityLayout() const {
  const auto *current = diagram();
  if (!current || current->type != DiagramType::Activity)
    return {};
  return ui::buildActivityDiagramLayout(current->activity, m_previewGeometry);
}

QRectF DiagramCanvas::activityNodeGeometry(const ActivityNode &node) const {
  const auto preview = m_previewGeometry.constFind(node.id);
  return preview == m_previewGeometry.cend()
             ? ui::activityNodeBounds(node.type, node.center)
             : *preview;
}

QList<ui::DiagramNodeGeometry>
DiagramCanvas::activityNodeGeometries(bool selectionOnly) const {
  QList<ui::DiagramNodeGeometry> result;
  if (!isActivityDiagram())
    return result;
  const auto &nodes = diagram()->activity.nodes;
  if (!selectionOnly) {
    result.reserve(nodes.size());
    for (const auto &node : nodes)
      result.append({node.id, activityNodeGeometry(node)});
    return result;
  }
  QHash<QString, const ActivityNode *> byId;
  for (const auto &node : nodes)
    byId.insert(node.id, &node);
  for (const QString &id : m_selectedActivityItems)
    if (const auto *node = byId.value(id))
      result.append({id, activityNodeGeometry(*node)});
  return result;
}

int DiagramCanvas::selectedActivityItemCount() const {
  return m_selectedActivityItems.size();
}

int DiagramCanvas::selectedActivityNodeCount() const {
  if (!isActivityDiagram() || m_selectedActivityItems.isEmpty())
    return 0;
  const QSet<QString> ids(m_selectedActivityItems.cbegin(),
                          m_selectedActivityItems.cend());
  const auto &nodes = diagram()->activity.nodes;
  return std::count_if(nodes.cbegin(), nodes.cend(),
                       [&](const auto &node) { return ids.contains(node.id); });
}

bool DiagramCanvas::previewActivityLayout(
    const QString &scope, const ui::AutomaticLayoutOptions &options) {
  if (scope == QStringLiteral("container"))
    return false;
  const auto nodes =
      activityNodeGeometries(scope == QStringLiteral("selection"));
  if (nodes.size() < 2)
    return false;
  QList<ui::DiagramLayoutEdge> edges;
  for (const auto &flow : diagram()->activity.flows)
    edges.append({flow.sourceId, flow.targetId});
  const auto arranged =
      ui::automaticallyLayoutDiagramNodes(nodes, edges, options);
  if (arranged.isEmpty())
    return false;
  for (const auto &node : arranged)
    m_previewGeometry.insert(node.id, node.geometry);
  m_automaticLayoutPreviewActive = true;
  m_automaticLayoutPreviewCount = arranged.size();
  m_sceneDirty = true;
  m_textDirty = true;
  emit automaticLayoutPreviewChanged();
  update();
  return true;
}

void DiagramCanvas::commitActivityPositions(
    const QHash<QString, QRectF> &geometries, const QString &description) {
  if (!m_project || geometries.isEmpty())
    return;
  QHash<QString, QPointF> centers;
  for (auto it = geometries.cbegin(); it != geometries.cend(); ++it)
    centers.insert(it.key(), it->center());
  m_project->moveActivityNodes(m_diagramId, centers, description);
}

void DiagramCanvas::refreshActivityCanvas() {
  m_sceneDirty = true;
  m_textDirty = true;
  emit canvasSelectionChanged();
  update();
}

QPointF DiagramCanvas::snapActivityPoint(QPointF point) const {
  if (m_snapToGridEnabled && m_gridSpacing > 0.0) {
    point.setX(std::round(point.x() / m_gridSpacing) * m_gridSpacing);
    point.setY(std::round(point.y() / m_gridSpacing) * m_gridSpacing);
  }
  return point;
}

void DiagramCanvas::addActivityNode(const QString &type,
                                    bool atViewportCenter) {
  const auto nodeType = activityNodeTypeFromString(type);
  if (!isActivityDiagram() || !nodeType)
    return;

  QPointF center = snapActivityPoint(
      atViewportCenter ? toScene({width() / 2.0, height() / 2.0})
                       : m_contextScenePoint);
  if (atViewportCenter) {
    const auto layout = activityLayout();
    const qreal step = m_snapToGridEnabled && m_gridSpacing > 0.0
                           ? std::ceil(72.0 / m_gridSpacing) * m_gridSpacing
                           : 72.0;
    // Repeated additions from the tab menu should remain visible instead of
    // creating a stack of nodes at the same coordinates.
    while (std::any_of(layout.nodes.cbegin(), layout.nodes.cend(),
                       [&](const auto &item) {
                         return item.bounds.adjusted(-84.0, -30.0, 84.0, 30.0)
                             .contains(center);
                       }))
      center.ry() += step;
  }
  const QString id = m_project->addActivityNode(m_diagramId, *nodeType, center);
  if (!id.isEmpty()) {
    m_selectedActivityItems = {id};
    forceActiveFocus();
    refreshActivityCanvas();
  }
}

void DiagramCanvas::selectActivityItemAt(const QPointF &scenePoint) {
  const auto layout = activityLayout();
  const auto *node = ui::hitActivityNode(layout, scenePoint);
  const auto *flow =
      node ? nullptr : ui::hitActivityFlow(layout, scenePoint, 7.0 / m_zoom);
  const QString id = node ? node->id : flow ? flow->id : QString{};
  m_selectedActivityItems = id.isEmpty() ? QStringList{} : QStringList{id};
  refreshActivityCanvas();
}

void DiagramCanvas::selectAllActivityItems() {
  if (!isActivityDiagram())
    return;
  m_selectedActivityItems.clear();
  for (const auto &node : diagram()->activity.nodes)
    m_selectedActivityItems.append(node.id);
  for (const auto &flow : diagram()->activity.flows)
    m_selectedActivityItems.append(flow.id);
  refreshActivityCanvas();
}

void DiagramCanvas::pruneActivitySelection() {
  if (!isActivityDiagram() ||
      (m_selectedActivityItems.isEmpty() && m_activityLassoBase.isEmpty()))
    return;
  const auto &graph = diagram()->activity;
  QSet<QString> valid;
  for (const auto &node : graph.nodes)
    valid.insert(node.id);
  for (const auto &flow : graph.flows)
    valid.insert(flow.id);
  const auto missing = [&](const QString &id) { return !valid.contains(id); };
  m_selectedActivityItems.removeIf(missing);
  m_activityLassoBase.removeIf(missing);
}

void DiagramCanvas::activityMousePress(QMouseEvent &event) {
  cancelActivityGesture();
  const auto layout = activityLayout();
  const auto *node = ui::hitActivityNode(layout, m_pressScene);
  const auto *flow =
      node ? nullptr : ui::hitActivityFlow(layout, m_pressScene, 7.0 / m_zoom);
  const QString id = node ? node->id : flow ? flow->id : QString{};
  if (event.button() == Qt::RightButton) {
    if (!id.isEmpty() && !m_selectedActivityItems.contains(id)) {
      m_selectedActivityItems = {id};
      refreshActivityCanvas();
    }
    m_contextScenePoint = m_pressScene;
    emit contextMenuRequested(id.isEmpty() ? QStringLiteral("activity")
                                           : QStringLiteral("activity-item"),
                              m_pressView.x(), m_pressView.y());
  } else if (event.button() == Qt::LeftButton) {
    const bool toggle = event.modifiers().testFlag(Qt::ControlModifier);
    if (id.isEmpty()) {
      m_activityLassoBase = m_selectedActivityItems;
      m_lassoOrigin = m_pressScene;
      m_lassoModifiers = event.modifiers();
      m_interaction = Interaction::LassoActivity;
    } else {
      const bool wasSelected = m_selectedActivityItems.contains(id);
      if (toggle && wasSelected) {
        m_selectedActivityItems.removeAll(id);
        refreshActivityCanvas();
        event.accept();
        return;
      }
      if (!toggle && !wasSelected)
        m_selectedActivityItems.clear();
      // A click on a member of a group makes it the alignment reference.
      m_selectedActivityItems.removeAll(id);
      m_selectedActivityItems.append(id);
      if (node) {
        m_activityGestureNode = id;
        if (!toggle && event.modifiers().testFlag(Qt::ShiftModifier)) {
          m_interaction = Interaction::CreateActivityFlow;
        } else {
          m_interaction = Interaction::MoveActivityNode;
          m_originalGeometry.clear();
          for (const auto &geometry : activityNodeGeometries(true))
            m_originalGeometry.insert(geometry.id, geometry.geometry);
        }
      }
      refreshActivityCanvas();
    }
  }
  event.accept();
}

void DiagramCanvas::activityMouseMove(QMouseEvent &event) {
  if (m_interaction != Interaction::MoveActivityNode &&
      m_interaction != Interaction::CreateActivityFlow &&
      m_interaction != Interaction::LassoActivity)
    return;
  if (!m_activityGestureMoved &&
      QLineF(m_pressView, event.position()).length() < kActivityDragThreshold)
    return;
  m_activityGestureMoved = true;
  if (m_interaction == Interaction::LassoActivity) {
    m_lassoActive = true;
    updateActivityLassoSelection(m_lastPointerScene);
  } else if (m_interaction == Interaction::MoveActivityNode) {
    const QPointF origin =
        m_originalGeometry.value(m_activityGestureNode).center();
    QPointF center = origin + m_lastPointerScene - m_pressScene;
    if (!event.modifiers().testFlag(Qt::AltModifier))
      center = snapActivityPoint(center);
    const QPointF delta = center - origin;
    for (auto it = m_originalGeometry.cbegin(); it != m_originalGeometry.cend();
         ++it)
      m_previewGeometry.insert(it.key(), it->translated(delta));
  }
  m_sceneDirty = true;
  m_textDirty = true;
  update();
  event.accept();
}

void DiagramCanvas::activityMouseRelease(QMouseEvent &event) {
  if (event.button() != Qt::LeftButton)
    return;
  activityMouseMove(event);
  if (m_interaction == Interaction::LassoActivity) {
    if (!m_lassoActive && !m_lassoModifiers.testFlag(Qt::ControlModifier) &&
        !m_lassoModifiers.testFlag(Qt::ShiftModifier))
      m_selectedActivityItems.clear();
    cancelActivityGesture(false);
    event.accept();
    return;
  }
  const QString sourceId = m_activityGestureNode;
  QString targetId;
  bool offerDestination = false;
  if (m_activityGestureMoved &&
      m_interaction == Interaction::CreateActivityFlow) {
    const auto layout = activityLayout();
    if (const auto *target = ui::hitActivityNode(layout, m_lastPointerScene))
      targetId = target->id;
    else
      offerDestination = std::any_of(
          layout.nodes.cbegin(), layout.nodes.cend(), [&](const auto &node) {
            return node.id == sourceId && node.type != ActivityNodeType::Final;
          });
  }
  const bool moved =
      m_activityGestureMoved && m_interaction == Interaction::MoveActivityNode;
  const auto movedGeometries =
      moved ? std::move(m_previewGeometry) : QHash<QString, QRectF>{};
  cancelActivityGesture();
  if (moved) {
    commitActivityPositions(movedGeometries, tr("Move activity selection"));
  } else if (!targetId.isEmpty()) {
    const QString flowId =
        m_project->addActivityFlow(m_diagramId, sourceId, targetId);
    if (!flowId.isEmpty()) {
      m_selectedActivityItems = {flowId};
      refreshActivityCanvas();
    }
  } else if (offerDestination) {
    requestConnectionDestination({sourceId, m_lastPointerScene, {}, {}});
  }
  event.accept();
}

void DiagramCanvas::cancelActivityGesture(bool restoreLassoSelection) {
  if (m_interaction != Interaction::MoveActivityNode &&
      m_interaction != Interaction::CreateActivityFlow &&
      m_interaction != Interaction::LassoActivity)
    return;
  if (m_interaction == Interaction::MoveActivityNode) {
    m_originalGeometry.clear();
    m_previewGeometry.clear();
  } else if (m_interaction == Interaction::LassoActivity) {
    if (restoreLassoSelection)
      m_selectedActivityItems = m_activityLassoBase;
    m_activityLassoBase.clear();
    resetLassoState();
  }
  m_interaction = Interaction::None;
  m_activityGestureNode.clear();
  m_activityGestureMoved = false;
  refreshActivityCanvas();
}

bool DiagramCanvas::activitySelectionEditable() const {
  if (!isActivityDiagram() || m_selectedActivityItems.size() != 1)
    return false;
  const auto &id = m_selectedActivityItems.first();
  const auto &graph = diagram()->activity;
  return std::any_of(graph.nodes.cbegin(), graph.nodes.cend(),
                     [&](const auto &node) {
                       return node.id == id &&
                              node.type == ActivityNodeType::Action;
                     }) ||
         std::any_of(graph.flows.cbegin(), graph.flows.cend(),
                     [&](const auto &flow) { return flow.id == id; });
}

void DiagramCanvas::editSelectedActivityItem() {
  if (!activitySelectionEditable())
    return;
  const QString id = m_selectedActivityItems.first();
  cancelActivityGesture();
  const auto layout = activityLayout();
  const auto requestEdit = [&](const QString &text, const QRectF &sceneRect) {
    const QRectF view = toView(sceneRect);
    emit editRequested(id, QStringLiteral("activityText"), -1, text, view.x(),
                       view.y(), view.width(), view.height(),
                       std::max(1.0, 13.0 * m_zoom), false);
  };
  for (const auto &node : layout.nodes) {
    if (node.id == id) {
      QRectF label = node.bounds.adjusted(8.0, 0.0, -8.0, 0.0);
      label.setHeight(22.0);
      label.moveCenter(node.bounds.center());
      requestEdit(node.name, label);
      return;
    }
  }
  for (const auto &flow : layout.flows)
    if (flow.id == id) {
      requestEdit(flow.guard, flow.labelBounds);
      return;
    }
}

void DiagramCanvas::deleteSelectedActivityItems() {
  if (!isActivityDiagram() || m_selectedActivityItems.isEmpty())
    return;
  cancelActivityGesture();
  const auto ids = std::exchange(m_selectedActivityItems, QStringList{});
  m_project->deleteActivityItems(m_diagramId, ids);
  refreshActivityCanvas();
}

bool DiagramCanvas::handleActivityKey(QKeyEvent &event) {
  if (event.matches(QKeySequence::SelectAll)) {
    selectAllActivityItems();
  } else if (event.key() == Qt::Key_F2) {
    editSelectedActivityItem();
  } else if (event.key() == Qt::Key_Delete) {
    deleteSelectedActivityItems();
  } else if (event.key() == Qt::Key_Escape) {
    cancelActivityGesture();
  } else if (event.modifiers() == Qt::NoModifier ||
             event.modifiers() == Qt::ShiftModifier) {
    const qreal distance = event.modifiers() == Qt::ShiftModifier ? 10.0 : 1.0;
    switch (event.key()) {
    case Qt::Key_Left:
      nudgeSelection(-distance, 0.0);
      break;
    case Qt::Key_Right:
      nudgeSelection(distance, 0.0);
      break;
    case Qt::Key_Up:
      nudgeSelection(0.0, -distance);
      break;
    case Qt::Key_Down:
      nudgeSelection(0.0, distance);
      break;
    default:
      return false;
    }
  } else {
    return false;
  }
  event.accept();
  return true;
}

} // namespace yauml
