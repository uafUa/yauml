#include "core/application_settings.h"
#include "core/project_controller.h"
#include "ui/connector_routing.h"
#include "ui/diagram_arrangement.h"
#include "ui/diagram_canvas.h"
#include "ui/diagram_snapping.h"
#include "ui/relationship_style.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QtTest>

using namespace uuml;

namespace {

class TestDiagramCanvas final : public DiagramCanvas {
public:
  using DiagramCanvas::DiagramCanvas;

  void press(const QPointF &position,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(QEvent::MouseButtonPress, position, position,
                      Qt::LeftButton, Qt::LeftButton, modifiers);
    mousePressEvent(&event);
  }

  void move(const QPointF &position,
            Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(QEvent::MouseMove, position, position, Qt::NoButton,
                      Qt::LeftButton, modifiers);
    mouseMoveEvent(&event);
  }

  void release(const QPointF &position,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(QEvent::MouseButtonRelease, position, position,
                      Qt::LeftButton, Qt::NoButton, modifiers);
    mouseReleaseEvent(&event);
  }

  void rightClick(const QPointF &position) {
    QMouseEvent event(QEvent::MouseButtonPress, position, position,
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    mousePressEvent(&event);
  }

  void key(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, key, modifiers);
    keyPressEvent(&event);
  }

  void drag(const QPointF &start, const QPointF &end,
            Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    press(start, modifiers);
    move(end, modifiers);
    release(end, modifiers);
  }
};

void populate(ProjectController &controller, int count) {
  const QString diagramId = controller.data().diagrams.first().id;
  for (int index = 0; index < count; ++index)
    controller.addElement(QStringLiteral("class"), diagramId);
}

void configureCanvas(TestDiagramCanvas &canvas, ProjectController &controller) {
  canvas.setWidth(900);
  canvas.setHeight(600);
  canvas.setProject(&controller);
  canvas.setDiagramId(controller.data().diagrams.first().id);
  // Most interaction tests assert exact free-drag deltas. Snapping is enabled
  // explicitly by its focused tests below.
  canvas.setSnapToGridEnabled(false);
  canvas.setAlignmentGuidesEnabled(false);
}

} // namespace

class DiagramCanvasTests final : public QObject {
  Q_OBJECT

private slots:
  void arrangementGeometryRulesAreDeterministic();
  void snappingGeometryRulesAreDeterministic();
  void connectorRoutingGeometryIsOrthogonalAndTracksBends();
  void relationshipStylesUseUmlDecorations();
  void lassoSelectsAndMovesMultipleNodesAsOneCommand();
  void lassoModifiersAddAndToggleSelection();
  void arrangementAndNudgingAreUndoableTransactions();
  void contextCreationUsesTheClickedDiagramAndPosition();
  void connectorBendPointCanBeAddedMovedAndRemoved();
  void edgeGestureCreatesCancelsAndSupportsSelfConnections();
  void connectorEndpointsDragToReattachAndCancel();
  void liveDragSnappingIsUndoableAndAltSuppressesIt();
};

void DiagramCanvasTests::arrangementGeometryRulesAreDeterministic() {
  const QList<ui::DiagramNodeGeometry> nodes = {
      {QStringLiteral("first"), QRectF(10, 20, 100, 40)},
      {QStringLiteral("middle"), QRectF(200, 100, 150, 60)},
      {QStringLiteral("last"), QRectF(500, 300, 200, 80)},
  };

  const auto aligned = ui::arrangeDiagramNodes(
      nodes, ui::ArrangementOperation::AlignHorizontalCenter, 10.0);
  QCOMPARE(aligned.at(0).geometry.center().x(), 355.0);
  QCOMPARE(aligned.at(1).geometry.center().x(), 355.0);
  QCOMPARE(aligned.at(2).geometry.center().x(), 355.0);

  const auto sameSize =
      ui::arrangeDiagramNodes(nodes, ui::ArrangementOperation::MatchSize, 10.0);
  for (const auto &node : sameSize)
    QCOMPARE(node.geometry.size(), QSizeF(200, 80));

  const auto horizontal = ui::arrangeDiagramNodes(
      nodes, ui::ArrangementOperation::DistributeHorizontally, 10.0);
  QCOMPARE(horizontal.at(0).geometry, nodes.at(0).geometry);
  QCOMPARE(horizontal.at(1).geometry.left(), 200.0);
  QCOMPARE(horizontal.at(2).geometry.left(), 440.0);

  const auto vertical = ui::arrangeDiagramNodes(
      nodes, ui::ArrangementOperation::DistributeVertically, 10.0);
  QCOMPARE(vertical.at(0).geometry, nodes.at(0).geometry);
  QCOMPARE(vertical.at(1).geometry.top(), 100.0);
  QCOMPARE(vertical.at(2).geometry.top(), 200.0);

  const QList<ui::DiagramNodeGeometry> overlapping = {
      {QStringLiteral("first"), QRectF(0, 0, 100, 40)},
      {QStringLiteral("middle"), QRectF(50, 0, 100, 40)},
      {QStringLiteral("last"), QRectF(100, 0, 100, 40)},
  };
  const auto fallback = ui::arrangeDiagramNodes(
      overlapping, ui::ArrangementOperation::DistributeHorizontally, 12.0);
  QCOMPARE(fallback.at(0).geometry.left(), 0.0);
  QCOMPARE(fallback.at(1).geometry.left(), 112.0);
  QCOMPARE(fallback.at(2).geometry.left(), 224.0);
  QVERIFY(!ui::arrangementOperationFromKey(QStringLiteral("unknown")));
}

void DiagramCanvasTests::connectorRoutingGeometryIsOrthogonalAndTracksBends() {
  const QPointF source(0.0, 0.0);
  const QPointF target(100.0, 50.0);
  const auto straight = ui::buildConnectorRoute(
      source, {{30.0, 20.0}}, target, ConnectorRouting::Straight,
      ConnectorSide::Right, ConnectorSide::Left);
  QCOMPARE(straight.points, QVector<QPointF>({source, {30.0, 20.0}, target}));
  QCOMPARE(straight.bendPointRouteIndices, QVector<int>({1}));

  const auto orthogonal = ui::buildConnectorRoute(
      source, {{30.0, 20.0}}, target, ConnectorRouting::Orthogonal,
      ConnectorSide::Right, ConnectorSide::Left);
  QVERIFY(orthogonal.points.size() >= 4);
  QCOMPARE(orthogonal.points.first(), source);
  QCOMPARE(orthogonal.points.last(), target);
  QCOMPARE(orthogonal.bendPointRouteIndices.size(), 1);
  QCOMPARE(orthogonal.points.at(orthogonal.bendPointRouteIndices.first()),
           QPointF(30.0, 20.0));
  for (qsizetype index = 1; index < orthogonal.points.size(); ++index) {
    const QPointF previous = orthogonal.points.at(index - 1);
    const QPointF current = orthogonal.points.at(index);
    QVERIFY(qFuzzyCompare(previous.x(), current.x()) ||
            qFuzzyCompare(previous.y(), current.y()));
  }

  // Rebuilding from changed endpoints is what keeps the automatic elbows
  // attached while nodes are interactively moved or resized.
  const auto moved = ui::buildConnectorRoute(
      {20.0, 10.0}, {{30.0, 20.0}}, {140.0, 80.0}, ConnectorRouting::Orthogonal,
      ConnectorSide::Right, ConnectorSide::Left);
  QCOMPARE(moved.points.first(), QPointF(20.0, 10.0));
  QCOMPARE(moved.points.last(), QPointF(140.0, 80.0));
  QCOMPARE(moved.points.at(moved.bendPointRouteIndices.first()),
           QPointF(30.0, 20.0));
}

void DiagramCanvasTests::relationshipStylesUseUmlDecorations() {
  using ui::RelationshipDecoration;
  using ui::RelationshipLineStyle;

  const auto dependency =
      ui::relationshipVisualStyle(RelationshipType::Dependency);
  QVERIFY(dependency.line == RelationshipLineStyle::Dashed);
  QVERIFY(dependency.target == RelationshipDecoration::OpenArrow);

  const auto generalization =
      ui::relationshipVisualStyle(RelationshipType::Generalization);
  QVERIFY(generalization.line == RelationshipLineStyle::Solid);
  QVERIFY(generalization.target == RelationshipDecoration::HollowTriangle);

  const auto realization =
      ui::relationshipVisualStyle(RelationshipType::Realization);
  QVERIFY(realization.line == RelationshipLineStyle::Dashed);
  QVERIFY(realization.target == RelationshipDecoration::HollowTriangle);

  const auto association =
      ui::relationshipVisualStyle(RelationshipType::Association);
  QVERIFY(association.target == RelationshipDecoration::OpenArrow);

  const auto aggregation =
      ui::relationshipVisualStyle(RelationshipType::Aggregation);
  QVERIFY(aggregation.source == RelationshipDecoration::HollowDiamond);
  QVERIFY(aggregation.target == RelationshipDecoration::None);

  const auto composition =
      ui::relationshipVisualStyle(RelationshipType::Composition);
  QVERIFY(composition.source == RelationshipDecoration::FilledDiamond);
  QVERIFY(composition.target == RelationshipDecoration::None);
}

void DiagramCanvasTests::snappingGeometryRulesAreDeterministic() {
  const QList<ui::DiagramNodeGeometry> moving = {
      {QStringLiteral("moving"), QRectF(10, 10, 100, 60)}};
  const QList<ui::DiagramNodeGeometry> stationary = {
      {QStringLiteral("stationary"), QRectF(200, 20, 100, 60)}};

  ui::DiagramSnapOptions alignmentOptions;
  alignmentOptions.snapToGrid = false;
  alignmentOptions.snapToAlignment = true;
  alignmentOptions.tolerance = 6.0;
  const auto aligned =
      ui::snapDiagramMove(moving, stationary, QStringLiteral("moving"),
                          QPointF(86, 8), alignmentOptions);
  QCOMPARE(aligned.delta, QPointF(90, 10));
  QCOMPARE(aligned.guides.size(), 2);
  QCOMPARE(aligned.guides.at(0).x1(), 200.0);
  QCOMPARE(aligned.guides.at(0).x2(), 200.0);
  QCOMPARE(aligned.guides.at(1).y1(), 20.0);
  QCOMPARE(aligned.guides.at(1).y2(), 20.0);

  ui::DiagramSnapOptions gridOptions;
  gridOptions.snapToGrid = true;
  gridOptions.snapToAlignment = false;
  gridOptions.gridSpacing = 20.0;
  gridOptions.tolerance = 4.0;
  const auto onGrid =
      ui::snapDiagramMove(moving, stationary, QStringLiteral("moving"),
                          QPointF(27, 33), gridOptions);
  QCOMPARE(onGrid.delta, QPointF(30, 30));
  QVERIFY(onGrid.guides.isEmpty());

  const auto resized = ui::snapDiagramBottomRightResize(
      QRectF(10, 10, 186, 66), stationary, QSizeF(120, 60), alignmentOptions);
  QCOMPARE(resized.geometry, QRectF(10, 10, 190, 70));
  QCOMPARE(resized.guides.size(), 2);
  QCOMPARE(resized.guides.at(0).x1(), 200.0);
  QCOMPARE(resized.guides.at(0).x2(), 200.0);
  QCOMPARE(resized.guides.at(1).y1(), 80.0);
  QCOMPARE(resized.guides.at(1).y2(), 80.0);

  const auto resizedOnGrid = ui::snapDiagramBottomRightResize(
      QRectF(10, 10, 127, 93), stationary, QSizeF(120, 60), gridOptions);
  QCOMPARE(resizedOnGrid.geometry, QRectF(10, 10, 130, 90));
  QVERIFY(resizedOnGrid.guides.isEmpty());
}

void DiagramCanvasTests::lassoSelectsAndMovesMultipleNodesAsOneCommand() {
  ProjectController controller;
  populate(controller, 2);
  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);

  const auto before = controller.data().diagrams.first().nodes;
  // Canvas view coordinates include the default (30, 30) pan. This rectangle
  // intersects both nodes at scene x=50 and x=300.
  canvas.drag({70, 70}, {560, 210});
  QCOMPARE(canvas.selectedNodeCount(), 2);

  canvas.drag({130, 130}, {150, 145});
  const auto &after = controller.data().diagrams.first().nodes;
  QCOMPARE(after.at(0).geometry, before.at(0).geometry.translated(20, 15));
  QCOMPARE(after.at(1).geometry, before.at(1).geometry.translated(20, 15));
  QCOMPARE(controller.undoText(),
           QStringLiteral("Move or resize diagram elements"));

  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes, before);
}

void DiagramCanvasTests::lassoModifiersAddAndToggleSelection() {
  ProjectController controller;
  populate(controller, 3);
  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);

  // Select the first node, add the second with Shift-lasso, then toggle the
  // first out with Ctrl-lasso. Selection order remains diagram order.
  canvas.drag({130, 130}, {130, 130});
  QCOMPARE(canvas.selectedNodeCount(), 1);
  canvas.drag({310, 70}, {560, 210}, Qt::ShiftModifier);
  QCOMPARE(canvas.selectedNodeCount(), 2);
  canvas.drag({70, 70}, {310, 210}, Qt::ControlModifier);
  QCOMPARE(canvas.selectedNodeCount(), 1);

  // A modified click on empty space is not a selection-clearing gesture.
  canvas.drag({800, 500}, {800, 500}, Qt::ControlModifier);
  QCOMPARE(canvas.selectedNodeCount(), 1);
}

void DiagramCanvasTests::arrangementAndNudgingAreUndoableTransactions() {
  ProjectController controller;
  populate(controller, 3);
  const QString diagramId = controller.data().diagrams.first().id;
  const auto initialNodes = controller.data().diagrams.first().nodes;
  controller.updateNodeGeometry(diagramId, initialNodes.at(1).id,
                                initialNodes.at(1).geometry.x(), 100,
                                initialNodes.at(1).geometry.width(),
                                initialNodes.at(1).geometry.height());
  controller.updateNodeGeometry(diagramId, initialNodes.at(2).id,
                                initialNodes.at(2).geometry.x(), 160,
                                initialNodes.at(2).geometry.width(),
                                initialNodes.at(2).geometry.height());
  const auto beforeArrangement = controller.data().diagrams.first().nodes;

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  canvas.drag({70, 70}, {820, 330});
  QCOMPARE(canvas.selectedNodeCount(), 3);

  canvas.arrangeSelection(QStringLiteral("alignTop"));
  const auto &aligned = controller.data().diagrams.first().nodes;
  QCOMPARE(aligned.at(0).geometry.top(), 50.0);
  QCOMPARE(aligned.at(1).geometry.top(), 50.0);
  QCOMPARE(aligned.at(2).geometry.top(), 50.0);
  QCOMPARE(controller.undoText(), QStringLiteral("Align top"));
  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes, beforeArrangement);

  canvas.key(Qt::Key_Right);
  const auto afterHorizontalNudge = controller.data().diagrams.first().nodes;
  for (qsizetype index = 0; index < beforeArrangement.size(); ++index) {
    QCOMPARE(afterHorizontalNudge.at(index).geometry,
             beforeArrangement.at(index).geometry.translated(1, 0));
  }
  QCOMPARE(controller.undoText(), QStringLiteral("Nudge diagram elements"));

  canvas.key(Qt::Key_Down, Qt::ShiftModifier);
  const auto afterLargeNudge = controller.data().diagrams.first().nodes;
  for (qsizetype index = 0; index < beforeArrangement.size(); ++index) {
    QCOMPARE(afterLargeNudge.at(index).geometry,
             beforeArrangement.at(index).geometry.translated(1, 10));
  }
  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes, afterHorizontalNudge);
  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes, beforeArrangement);
}

void DiagramCanvasTests::contextCreationUsesTheClickedDiagramAndPosition() {
  ProjectController controller;
  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  QSignalSpy menuRequests(&canvas, &DiagramCanvas::contextMenuRequested);

  canvas.rightClick({700, 400});
  QCOMPARE(menuRequests.count(), 1);
  QCOMPARE(menuRequests.first().at(0).toString(), QStringLiteral("canvas"));
  canvas.createElementAtContextPosition(QStringLiteral("class"));

  const auto &diagram = controller.data().diagrams.first();
  QCOMPARE(diagram.nodes.size(), 1);
  // The default canvas pan is (30, 30), so the clicked scene center is
  // (670, 370). Context creation centers the standard node at that point.
  QCOMPARE(diagram.nodes.first().geometry, QRectF(560.0, 310.0, 220.0, 120.0));
  QCOMPARE(canvas.selectedNodeCount(), 1);
}

void DiagramCanvasTests::connectorBendPointCanBeAddedMovedAndRemoved() {
  ProjectController controller;
  populate(controller, 2);
  const QString diagramId = controller.data().diagrams.first().id;
  const auto nodes = controller.data().diagrams.first().nodes;
  const QString connectorId = controller.createRelationship(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("association"));
  QVERIFY(!connectorId.isEmpty());

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  QSignalSpy menuRequests(&canvas, &DiagramCanvas::contextMenuRequested);

  // Node edges are at scene x=270 and x=300. The default pan adds 30 pixels,
  // so this right-click targets the connector midpoint in view coordinates.
  canvas.rightClick({315, 140});
  QCOMPARE(menuRequests.constLast().at(0).toString(),
           QStringLiteral("connector"));
  canvas.addBendPointAtContextPosition();
  const auto connector = [&]() {
    return findConnector(controller.data().diagrams.first(), connectorId);
  };
  QCOMPARE(connector()->bendPoints.size(), 1);
  QCOMPARE(connector()->bendPoints.first().position, QPointF(285.0, 110.0));
  QVERIFY(canvas.bendPointSelected());

  canvas.drag({315, 140}, {315, 200});
  QCOMPARE(connector()->bendPoints.first().position, QPointF(285.0, 170.0));
  QCOMPARE(controller.undoText(), QStringLiteral("Move connector bend point"));
  controller.undo();
  QCOMPARE(connector()->bendPoints.first().position, QPointF(285.0, 110.0));
  controller.redo();
  QCOMPARE(connector()->bendPoints.first().position, QPointF(285.0, 170.0));

  // Hit testing follows both polyline segments rather than the obsolete direct
  // line between endpoints.
  canvas.rightClick({307.5, 170});
  QCOMPARE(menuRequests.constLast().at(0).toString(),
           QStringLiteral("connector"));
  canvas.rightClick({315, 200});
  QVERIFY(canvas.bendPointSelected());
  canvas.removeSelectedBendPoint();
  QVERIFY(connector()->bendPoints.isEmpty());
  controller.undo();
  QCOMPARE(connector()->bendPoints.first().position, QPointF(285.0, 170.0));
}

void DiagramCanvasTests::edgeGestureCreatesCancelsAndSupportsSelfConnections() {
  ProjectController controller;
  populate(controller, 2);
  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  canvas.setDefaultConnectorRouting(QStringLiteral("orthogonal"));

  // The default pan is (30, 30). Hold the first node's right edge, press D,
  // and release on the second node's left edge.
  canvas.press({300.0, 140.0});
  canvas.key(Qt::Key_D);
  QVERIFY(!canvas.connectorInteractionPrompt().isEmpty());
  canvas.move({330.0, 140.0});
  canvas.release({330.0, 140.0});
  QCOMPARE(controller.data().relationships.size(), 1);
  QVERIFY(controller.data().relationships.first().type ==
          RelationshipType::Dependency);
  const auto &firstConnector =
      controller.data().diagrams.first().connectors.first();
  QVERIFY(firstConnector.routing == ConnectorRouting::Orthogonal);
  QVERIFY(firstConnector.sourceAnchor.side == ConnectorSide::Right);
  QVERIFY(firstConnector.targetAnchor.side == ConnectorSide::Left);
  controller.undo();
  QVERIFY(controller.data().relationships.isEmpty());
  controller.redo();
  QCOMPARE(controller.data().relationships.size(), 1);

  // Empty drops and Escape discard the candidate without creating an undoable
  // model change.
  canvas.clearCanvasSelection();
  canvas.press({160.0, 80.0});
  canvas.key(Qt::Key_A);
  canvas.move({700.0, 400.0});
  canvas.release({700.0, 400.0});
  QCOMPARE(controller.data().relationships.size(), 1);

  canvas.clearCanvasSelection();
  canvas.press({160.0, 80.0});
  canvas.key(Qt::Key_G);
  canvas.key(Qt::Key_Escape);
  canvas.move({330.0, 140.0});
  canvas.release({330.0, 140.0});
  QCOMPARE(controller.data().relationships.size(), 1);

  // A connection may return to its source node. The command creates persisted
  // outside bend points so the self-connection is visible around the element.
  canvas.clearCanvasSelection();
  canvas.press({160.0, 200.0});
  canvas.key(Qt::Key_C);
  canvas.move({160.0, 80.0});
  canvas.release({160.0, 80.0});
  QCOMPARE(controller.data().relationships.size(), 2);
  const auto &selfRelationship = controller.data().relationships.constLast();
  QVERIFY(selfRelationship.type == RelationshipType::Composition);
  QCOMPARE(selfRelationship.sourceId, selfRelationship.targetId);
  const auto &selfConnector =
      controller.data().diagrams.first().connectors.constLast();
  QVERIFY(!selfConnector.bendPoints.isEmpty());

  // The canvas consumes the atomically validated configurable key map.
  QVariantMap customKeys =
      ApplicationSettings::defaultRelationshipGestureKeys();
  customKeys.insert(QStringLiteral("dependency"), QStringLiteral("X"));
  customKeys.insert(QStringLiteral("realization"), QStringLiteral("D"));
  canvas.setRelationshipGestureKeys(customKeys);
  canvas.clearCanvasSelection();
  canvas.press({300.0, 140.0});
  canvas.key(Qt::Key_D);
  canvas.move({330.0, 140.0});
  canvas.release({330.0, 140.0});
  QCOMPARE(controller.data().relationships.size(), 3);
  QVERIFY(controller.data().relationships.constLast().type ==
          RelationshipType::Realization);
}

void DiagramCanvasTests::connectorEndpointsDragToReattachAndCancel() {
  ProjectController controller;
  populate(controller, 3);
  const QString diagramId = controller.data().diagrams.first().id;
  const auto nodes = controller.data().diagrams.first().nodes;
  const QString connectorId = controller.createRelationship(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("association"));
  QVERIFY(!connectorId.isEmpty());

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  // Select the connector between the first two horizontally arranged nodes.
  canvas.rightClick({315.0, 140.0});
  QVERIFY(canvas.connectorSelected());

  const auto relationship = [&]() {
    const auto *connector =
        findConnector(controller.data().diagrams.first(), connectorId);
    return connector
               ? findRelationship(controller.data(), connector->relationshipId)
               : nullptr;
  };
  const auto connector = [&]() {
    return findConnector(controller.data().diagrams.first(), connectorId);
  };

  // Drag the target handle from the second node to the third node. The drop
  // point is its left-edge midpoint; pan adds 30 view pixels to scene values.
  canvas.press({330.0, 140.0});
  QVERIFY(!canvas.connectorInteractionPrompt().isEmpty());
  canvas.move({580.0, 140.0});
  canvas.release({580.0, 140.0});
  QVERIFY(relationship());
  QCOMPARE(relationship()->targetId, nodes.at(2).elementId);
  QVERIFY(connector()->targetAnchor.side == ConnectorSide::Left);
  QCOMPARE(connector()->targetAnchor.offset, 0.5);
  QCOMPARE(controller.undoText(), QStringLiteral("Reconnect target"));

  controller.undo();
  QCOMPARE(relationship()->targetId, nodes.at(1).elementId);
  controller.redo();
  QCOMPARE(relationship()->targetId, nodes.at(2).elementId);

  // Empty drops and Escape only discard the preview. Neither creates an undo
  // entry nor changes the original semantic endpoint or its port.
  const ProjectData beforeCancelledDrags = controller.data();
  const QString undoTextBeforeCancelledDrags = controller.undoText();
  canvas.press({580.0, 140.0});
  canvas.move({820.0, 430.0});
  canvas.release({820.0, 430.0});
  QCOMPARE(controller.data(), beforeCancelledDrags);
  QCOMPARE(controller.undoText(), undoTextBeforeCancelledDrags);

  canvas.press({580.0, 140.0});
  canvas.move({300.0, 140.0});
  canvas.key(Qt::Key_Escape);
  canvas.release({300.0, 140.0});
  QCOMPARE(controller.data(), beforeCancelledDrags);
  QCOMPARE(controller.undoText(), undoTextBeforeCancelledDrags);

  // Moving an endpoint around its current element remains a focused port move
  // command rather than rewriting the relationship endpoint.
  canvas.drag({300.0, 140.0}, {190.0, 200.0});
  QCOMPARE(relationship()->sourceId, nodes.at(0).elementId);
  QVERIFY(connector()->sourceAnchor.side == ConnectorSide::Bottom);
  QCOMPARE(connector()->sourceAnchor.offset, 0.5);
  QCOMPARE(controller.undoText(), QStringLiteral("Move connector source port"));

  // Reattaching to the other endpoint creates a self-connection. An external
  // persisted loop keeps it visible, and the whole change is one command.
  canvas.drag({580.0, 140.0}, {190.0, 80.0});
  QCOMPARE(relationship()->sourceId, relationship()->targetId);
  QVERIFY(connector()->targetAnchor.side == ConnectorSide::Top);
  QVERIFY(!connector()->bendPoints.isEmpty());
  QCOMPARE(controller.undoText(), QStringLiteral("Reconnect target"));
  controller.undo();
  QCOMPARE(relationship()->targetId, nodes.at(2).elementId);
  QVERIFY(connector()->bendPoints.isEmpty());
  controller.redo();
  QCOMPARE(relationship()->sourceId, relationship()->targetId);
  QVERIFY(!connector()->bendPoints.isEmpty());
}

void DiagramCanvasTests::liveDragSnappingIsUndoableAndAltSuppressesIt() {
  ProjectController controller;
  populate(controller, 2);
  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  canvas.setSnapToGridEnabled(true);
  canvas.setAlignmentGuidesEnabled(true);
  canvas.setGridSpacing(20);

  const auto before = controller.data().diagrams.first().nodes;
  // A raw (26, 3) move puts the first node's right edge four units from the
  // second node's left edge. Alignment wins the grid tie on X, while the top
  // edges align on Y, producing a snapped (30, 0) move.
  canvas.drag({130, 130}, {156, 133});
  QCOMPARE(controller.data().diagrams.first().nodes.at(0).geometry,
           before.at(0).geometry.translated(30, 0));
  QCOMPARE(controller.undoText(),
           QStringLiteral("Move or resize diagram elements"));
  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes, before);

  canvas.drag({130, 130}, {156, 133}, Qt::AltModifier);
  QCOMPARE(controller.data().diagrams.first().nodes.at(0).geometry,
           before.at(0).geometry.translated(26, 3));
  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes, before);

  // The lower-right resize handle uses the same snapping behavior. The raw
  // right edge is four units from the second node's left edge and the raw
  // bottom edge is three units from its bottom edge.
  canvas.drag({295, 195}, {321, 198});
  QCOMPARE(controller.data().diagrams.first().nodes.at(0).geometry,
           QRectF(50, 50, 250, 120));
  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes, before);

  canvas.drag({295, 195}, {321, 198}, Qt::AltModifier);
  QCOMPARE(controller.data().diagrams.first().nodes.at(0).geometry,
           QRectF(50, 50, 246, 123));
  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes, before);
}

QTEST_MAIN(DiagramCanvasTests)

#include "diagram_canvas_tests.moc"
