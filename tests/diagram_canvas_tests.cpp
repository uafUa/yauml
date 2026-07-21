#include "core/project_controller.h"
#include "ui/diagram_canvas.h"

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
  void lassoSelectsAndMovesMultipleNodesAsOneCommand();
  void lassoModifiersAddAndToggleSelection();
  void contextCreationUsesTheClickedDiagramAndPosition();
};

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

QTEST_MAIN(DiagramCanvasTests)

#include "diagram_canvas_tests.moc"
