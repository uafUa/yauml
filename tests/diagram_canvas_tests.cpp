#include "core/project_controller.h"
#include "ui/diagram_arrangement.h"
#include "ui/diagram_canvas.h"

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
}

} // namespace

class DiagramCanvasTests final : public QObject {
  Q_OBJECT

private slots:
  void arrangementGeometryRulesAreDeterministic();
  void lassoSelectsAndMovesMultipleNodesAsOneCommand();
  void lassoModifiersAddAndToggleSelection();
  void arrangementAndNudgingAreUndoableTransactions();
  void contextCreationUsesTheClickedDiagramAndPosition();
  void connectorBendPointCanBeAddedMovedAndRemoved();
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

QTEST_MAIN(DiagramCanvasTests)

#include "diagram_canvas_tests.moc"
