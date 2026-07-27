#include "core/application_settings.h"
#include "core/connector_port_layout.h"
#include "core/presentation_layout.h"
#include "core/project_controller.h"
#include "core/project_serializer.h"
#include "ui/connector_routing.h"
#include "ui/diagram_arrangement.h"
#include "ui/diagram_canvas.h"
#include "ui/diagram_clipping.h"
#include "ui/diagram_snapping.h"
#include "ui/relationship_style.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
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

  void doubleClick(const QPointF &position) {
    QMouseEvent event(QEvent::MouseButtonDblClick, position, position,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mouseDoubleClickEvent(&event);
  }

  void hover(const QPointF &position, const QPointF &oldPosition = {}) {
    QHoverEvent event(QEvent::HoverMove, position, position, oldPosition);
    hoverMoveEvent(&event);
  }

  void leave(const QPointF &position = {}) {
    QHoverEvent event(QEvent::HoverLeave, position, position, position);
    hoverLeaveEvent(&event);
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

void useStandardInteractionGeometry(ProjectController &controller) {
  const QString diagramId = controller.data().diagrams.first().id;
  const auto nodes = controller.data().diagrams.first().nodes;
  for (qsizetype index = 0; index < nodes.size(); ++index) {
    controller.updateNodeGeometry(diagramId, nodes.at(index).id,
                                  50.0 + index * 250.0, 50.0, 220.0, 120.0);
  }
}

} // namespace

class DiagramCanvasTests final : public QObject {
  Q_OBJECT

private slots:
  void arrangementGeometryRulesAreDeterministic();
  void snappingGeometryRulesAreDeterministic();
  void nestedContainerClippingAndOverflowAreDeterministic();
  void connectorRoutingGeometryIsOrthogonalAndTracksBends();
  void relationshipStylesUseUmlDecorations();
  void lassoSelectsAndMovesMultipleNodesAsOneCommand();
  void lassoModifiersAddAndToggleSelection();
  void lassoStartsOnEmptyContainerBody();
  void contextualSelectAllUsesContainerScope();
  void clippedChildIsOnlyInteractiveInsideContainer();
  void arrangementAndNudgingAreUndoableTransactions();
  void contextCreationUsesTheClickedDiagramAndPosition();
  void packageCreationUsesAContainerFrame();
  void fitToContentUsesMeasuredElementSizeAndIsUndoable();
  void fitToContentUsesTheRenderedNamespaceRelativeName();
  void connectorBendPointCanBeAddedMovedAndRemoved();
  void relationshipEndAnnotationsAreEditable();
  void connectorAnnotationsMoveResetAndExposeStereotypes();
  void edgeGestureCreatesCancelsAndSupportsSelfConnections();
  void relationshipToolboxRequiresSelectedEdgeAndCreatesConnector();
  void multiSelectionToolboxTracksArrangementCommands();
  void connectorToolboxEditsRoutingAndAnnotations();
  void presentationToolboxTracksNodesContainersAndPriorities();
  void inPlaceNameEditorUsesRenderedName();
  void connectorEndpointsDragToReattachAndCancel();
  void connectorPortsSnapAndRemainFreelyPlaceable();
  void liveDragSnappingIsUndoableAndAltSuppressesIt();
  void folderContainerMovesDescendantsAndResizesIndependently();
  void nestedPackageDiagramMovesRemainPresentational();
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

  const auto containment =
      ui::relationshipVisualStyle(RelationshipType::Containment);
  QVERIFY(containment.source == RelationshipDecoration::CirclePlus);
  QVERIFY(containment.target == RelationshipDecoration::None);
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

void DiagramCanvasTests::nestedContainerClippingAndOverflowAreDeterministic() {
  Diagram diagram;
  ContainerPresentation parent;
  parent.id = QStringLiteral("parent");
  parent.subjectKind = QStringLiteral("folder");
  parent.geometry = QRectF(0, 0, 240, 160);
  parent.childPresentationIds = {QStringLiteral("child")};
  diagram.containers.append(parent);

  ContainerPresentation child;
  child.id = QStringLiteral("child");
  child.subjectKind = QStringLiteral("folder");
  child.geometry = QRectF(20, 50, 260, 140);
  child.childPresentationIds = {QStringLiteral("node")};
  diagram.containers.append(child);

  NodePresentation node;
  node.id = QStringLiteral("node");
  node.geometry = QRectF(220, 140, 80, 80);
  diagram.nodes.append(node);

  const ui::DiagramClipLayout layout(diagram);
  QCOMPARE(layout.childViewport(parent), QRectF(2, 34, 236, 124));
  QCOMPARE(layout.clipFor(child.id).rect, QRectF(2, 34, 236, 124));
  QVERIFY(layout.clipFor(child.id).active);
  QCOMPARE(layout.clipFor(node.id).rect, QRectF(22, 84, 216, 74));

  // A moving subtree is detached from its old owner's clip immediately. Its
  // descendants are still clipped by the moving root itself.
  const QSet<QString> detachedChild = {child.id};
  QVERIFY(!layout.clipFor(child.id, detachedChild).active);
  QCOMPARE(layout.clipFor(node.id, detachedChild).rect,
           QRectF(22, 84, 256, 104));

  const auto parentOverflow = layout.overflowEdges(parent);
  QVERIFY(parentOverflow.testFlag(ui::ContainerOverflowEdge::Right));
  QVERIFY(parentOverflow.testFlag(ui::ContainerOverflowEdge::Bottom));
  QVERIFY(!parentOverflow.testFlag(ui::ContainerOverflowEdge::Left));
  QVERIFY(!parentOverflow.testFlag(ui::ContainerOverflowEdge::Top));

  const auto childOverflow = layout.overflowEdges(child);
  QVERIFY(childOverflow.testFlag(ui::ContainerOverflowEdge::Right));
  QVERIFY(childOverflow.testFlag(ui::ContainerOverflowEdge::Bottom));

  // A live resize preview changes both the clip and the edge indicators
  // without mutating persisted presentation geometry.
  const QHash<QString, QRectF> preview = {{parent.id, QRectF(0, 0, 300, 220)}};
  const ui::DiagramClipLayout previewLayout(diagram, preview);
  QVERIFY(!previewLayout.overflowEdges(parent));
  QCOMPARE(previewLayout.clipFor(node.id).rect, QRectF(22, 84, 256, 104));
  QCOMPARE(parent.geometry, QRectF(0, 0, 240, 160));
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
  QCOMPARE(controller.undoText(), QStringLiteral("Move diagram elements"));

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
  const auto &nodes = controller.data().diagrams.first().nodes;
  const auto viewPoint = [](const QPointF &scenePoint) {
    return scenePoint + QPointF(30, 30);
  };
  canvas.drag(viewPoint(nodes.at(0).geometry.center()),
              viewPoint(nodes.at(0).geometry.center()));
  QCOMPARE(canvas.selectedNodeCount(), 1);
  canvas.drag(viewPoint(nodes.at(1).geometry.topLeft() - QPointF(6, 6)),
              viewPoint(nodes.at(1).geometry.bottomRight() + QPointF(6, 6)),
              Qt::ShiftModifier);
  QCOMPARE(canvas.selectedNodeCount(), 2);
  canvas.drag(viewPoint(nodes.at(0).geometry.topLeft() - QPointF(6, 6)),
              viewPoint(nodes.at(0).geometry.bottomRight() + QPointF(6, 6)),
              Qt::ControlModifier);
  QCOMPARE(canvas.selectedNodeCount(), 1);

  // A modified click on empty space is not a selection-clearing gesture.
  canvas.drag({800, 500}, {800, 500}, Qt::ControlModifier);
  QCOMPARE(canvas.selectedNodeCount(), 1);
}

void DiagramCanvasTests::lassoStartsOnEmptyContainerBody() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString first = controller.addElement(QStringLiteral("class"));
  const QString second = controller.addElement(QStringLiteral("struct"));
  const QString folder = controller.addBrowserFolder(
      QStringLiteral("model"), {}, QStringLiteral("Selection group"));
  const auto itemJson = [](const QString &kind, const QString &id) {
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("kind"), kind},
                                             {QStringLiteral("id"), id}}})
            .toJson(QJsonDocument::Compact));
  };
  QVERIFY(
      controller.moveBrowserItems(itemJson(QStringLiteral("element"), first),
                                  QStringLiteral("folder"), folder));
  QVERIFY(
      controller.moveBrowserItems(itemJson(QStringLiteral("element"), second),
                                  QStringLiteral("folder"), folder));
  QCOMPARE(controller.addTreeItemsToDiagram(
               diagramId, {first, second},
               itemJson(QStringLiteral("folder"), folder), 100.0, 100.0),
           3);

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  const Diagram before = controller.data().diagrams.first();
  QCOMPARE(before.containers.size(), 1);
  QCOMPARE(before.nodes.size(), 2);
  const QRectF frame = before.containers.first().geometry;
  const QPointF sceneStart(frame.left() + 8.0, frame.bottom() - 8.0);
  for (const auto &node : before.nodes)
    QVERIFY(!node.geometry.contains(sceneStart));

  // Start in empty container body space and sweep across its contents. View
  // coordinates include the canvas's default pan.
  const QPointF viewPan(30.0, 30.0);
  const QPointF sceneEnd(frame.right() - 8.0, frame.top() + 32.0);
  canvas.drag(sceneStart + viewPan, sceneEnd + viewPan);

  QCOMPARE(canvas.selectedNodeCount(), 2);
  QCOMPARE(controller.data().diagrams.first(), before);
}

void DiagramCanvasTests::contextualSelectAllUsesContainerScope() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString first = controller.addElement(QStringLiteral("class"));
  const QString second = controller.addElement(QStringLiteral("struct"));
  const QString diagramRoot =
      controller.addElement(QStringLiteral("class"), diagramId);
  const QString fourth = controller.addElement(QStringLiteral("class"));
  const QString folder = controller.addBrowserFolder(
      QStringLiteral("model"), {}, QStringLiteral("Selection scope"));
  const QString subfolder = controller.addBrowserFolder(
      QStringLiteral("folder"), folder, QStringLiteral("Nested scope"));
  const auto itemJson = [](const QString &kind, const QString &id) {
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("kind"), kind},
                                             {QStringLiteral("id"), id}}})
            .toJson(QJsonDocument::Compact));
  };
  QVERIFY(
      controller.moveBrowserItems(itemJson(QStringLiteral("element"), first),
                                  QStringLiteral("folder"), folder));
  QVERIFY(
      controller.moveBrowserItems(itemJson(QStringLiteral("element"), second),
                                  QStringLiteral("folder"), subfolder));
  QVERIFY(
      controller.moveBrowserItems(itemJson(QStringLiteral("element"), fourth),
                                  QStringLiteral("folder"), folder));
  QCOMPARE(controller.addTreeItemsToDiagram(
               diagramId, {first, second, fourth},
               itemJson(QStringLiteral("folder"), folder), 180.0, 120.0),
           5);

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  const Diagram &diagram = controller.data().diagrams.first();
  QCOMPARE(diagram.nodes.size(), 4);
  QCOMPARE(diagram.containers.size(), 2);
  const auto rootFrame =
      std::find_if(diagram.containers.cbegin(), diagram.containers.cend(),
                   [&](const ContainerPresentation &container) {
                     return container.subjectId == folder;
                   });
  QVERIFY(rootFrame != diagram.containers.cend());
  const QPointF viewPan(30.0, 30.0);
  const QPointF frameHeader =
      rootFrame->geometry.topLeft() + QPointF(10.0, 10.0) +
      viewPan;
  canvas.press(frameHeader);
  canvas.release(frameHeader);
  QVERIFY(canvas.containerSelected());

  canvas.key(Qt::Key_A, Qt::ControlModifier);
  QCOMPARE(canvas.selectedNodeCount(), 2);
  QCOMPARE(canvas.selectedContainerCount(), 1);

  canvas.clearCanvasSelection();
  canvas.key(Qt::Key_A, Qt::ControlModifier);
  QCOMPARE(canvas.selectedNodeCount(), 1);
  QCOMPARE(canvas.selectedContainerCount(), 1);

  canvas.clearCanvasSelection();
  const auto child =
      std::find_if(diagram.nodes.cbegin(), diagram.nodes.cend(),
                   [&](const NodePresentation &node) {
                     return node.elementId == first;
                   });
  QVERIFY(child != diagram.nodes.cend());
  canvas.press(child->geometry.center() + viewPan);
  canvas.release(child->geometry.center() + viewPan);
  QCOMPARE(canvas.selectedNodeCount(), 1);
  canvas.key(Qt::Key_A, Qt::ControlModifier);
  QCOMPARE(canvas.selectedNodeCount(), 2);
  QCOMPARE(canvas.selectedContainerCount(), 1);

  canvas.clearCanvasSelection();
  const auto nestedChild =
      std::find_if(diagram.nodes.cbegin(), diagram.nodes.cend(),
                   [&](const NodePresentation &node) {
                     return node.elementId == second;
                   });
  QVERIFY(nestedChild != diagram.nodes.cend());
  canvas.press(nestedChild->geometry.center() + viewPan);
  canvas.release(nestedChild->geometry.center() + viewPan);
  canvas.key(Qt::Key_A, Qt::ControlModifier);
  QCOMPARE(canvas.selectedNodeCount(), 1);
  QCOMPARE(canvas.selectedContainerCount(), 0);

  const Diagram beforeRemoval = controller.data().diagrams.first();
  const QString rootContainerId = rootFrame->id;
  canvas.clearCanvasSelection();
  canvas.key(Qt::Key_A, Qt::ControlModifier);
  canvas.key(Qt::Key_Delete);
  const Diagram &afterRemoval = controller.data().diagrams.first();
  QVERIFY(!findContainer(afterRemoval, rootContainerId));
  QVERIFY(std::none_of(
      afterRemoval.nodes.cbegin(), afterRemoval.nodes.cend(),
      [&](const NodePresentation &node) {
        return node.elementId == diagramRoot;
      }));
  controller.undo();
  QCOMPARE(controller.data().diagrams.first(), beforeRemoval);
}

void DiagramCanvasTests::clippedChildIsOnlyInteractiveInsideContainer() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString elementId = controller.addElement(QStringLiteral("class"));
  const QString folderId = controller.addBrowserFolder(
      QStringLiteral("model"), {}, QStringLiteral("Viewport"));
  const QString subjectJson = QString::fromUtf8(
      QJsonDocument(QJsonArray{QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("folder")},
                        {QStringLiteral("id"), folderId}}})
          .toJson(QJsonDocument::Compact));
  const QString elementJson = QString::fromUtf8(
      QJsonDocument(QJsonArray{QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("element")},
                        {QStringLiteral("id"), elementId}}})
          .toJson(QJsonDocument::Compact));
  QVERIFY(controller.moveBrowserItems(elementJson, QStringLiteral("folder"),
                                      folderId));
  QCOMPARE(controller.addTreeItemsToDiagram(diagramId, {elementId}, subjectJson,
                                            100.0, 100.0),
           2);

  const Diagram &initial = controller.data().diagrams.first();
  const QRectF frame = initial.containers.first().geometry;
  const QString nodeId = initial.nodes.first().id;
  const qreal nodeTop =
      frame.top() + presentation_layout::kContainerHeaderHeight + 10.0;
  controller.updateNodeGeometry(diagramId, nodeId, frame.right() + 20.0,
                                nodeTop, 120.0, 60.0);

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  const QPointF viewPan(30.0, 30.0);
  const QRectF hiddenNode =
      controller.data().diagrams.first().nodes.first().geometry;
  canvas.drag(hiddenNode.center() + viewPan, hiddenNode.center() + viewPan);
  QCOMPARE(canvas.selectedNodeCount(), 0);

  // Once part of the same node re-enters the container viewport, that visible
  // strip becomes interactive while the overflowing portion remains clipped.
  controller.updateNodeGeometry(diagramId, nodeId, frame.right() - 20.0,
                                nodeTop, 120.0, 60.0);
  const QPointF visiblePoint(frame.right() - 10.0, nodeTop + 20.0);
  canvas.drag(visiblePoint + viewPan, visiblePoint + viewPan);
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
  // (670, 370). Context creation centers the content-sized node there.
  const auto *element =
      findElement(controller.data(), diagram.nodes.first().elementId);
  QVERIFY(element);
  const QRectF expected(
      QPointF(670.0, 370.0) -
          QPointF(presentation_layout::nodeContentSize(*element).width() / 2.0,
                  presentation_layout::nodeContentSize(*element).height() /
                      2.0),
      presentation_layout::nodeContentSize(*element));
  QCOMPARE(diagram.nodes.first().geometry, expected);
  QCOMPARE(canvas.selectedNodeCount(), 1);
}

void DiagramCanvasTests::packageCreationUsesAContainerFrame() {
  ProjectController controller;
  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);

  canvas.rightClick({700, 400});
  canvas.createElementAtContextPosition(QStringLiteral("package"));

  const auto &project = controller.data();
  const auto &diagram = project.diagrams.first();
  QCOMPARE(project.elements.size(), 1);
  QVERIFY(project.elements.first().type == ElementType::Package);
  QVERIFY(diagram.nodes.isEmpty());
  QCOMPARE(diagram.containers.size(), 1);
  QCOMPARE(diagram.containers.first().subjectKind, QStringLiteral("package"));
  QCOMPARE(diagram.containers.first().subjectId, project.elements.first().id);
  QCOMPARE(diagram.containers.first().geometry.center(), QPointF(670, 370));
  QVERIFY(canvas.containerSelected());
  QCOMPARE(controller.undoText(), QStringLiteral("Create package"));

  controller.undo();
  QVERIFY(controller.data().elements.isEmpty());
  QVERIFY(controller.data().diagrams.first().containers.isEmpty());
  controller.redo();
  QCOMPARE(controller.data().elements.size(), 1);
  QCOMPARE(controller.data().diagrams.first().containers.size(), 1);
}

void DiagramCanvasTests::fitToContentUsesMeasuredElementSizeAndIsUndoable() {
  ProjectController controller;
  populate(controller, 1);
  const QString diagramId = controller.data().diagrams.first().id;
  const auto originalNode = controller.data().diagrams.first().nodes.first();
  controller.updateNodeGeometry(diagramId, originalNode.id,
                                originalNode.geometry.x(),
                                originalNode.geometry.y(), 460.0, 280.0);
  const QRectF oversized =
      controller.data().diagrams.first().nodes.first().geometry;

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  canvas.rightClick(oversized.center() + QPointF(30.0, 30.0));
  canvas.fitSelectionToContent();

  const auto &fittedNode = controller.data().diagrams.first().nodes.first();
  const auto *element = findElement(controller.data(), fittedNode.elementId);
  QVERIFY(element);
  QCOMPARE(fittedNode.geometry.topLeft(), oversized.topLeft());
  QCOMPARE(fittedNode.geometry.size(),
           presentation_layout::nodeContentSize(*element));
  QCOMPARE(controller.undoText(),
           QStringLiteral("Fit presentation to content"));

  controller.undo();
  QCOMPARE(controller.data().diagrams.first().nodes.first().geometry,
           oversized);
}

void DiagramCanvasTests::fitToContentUsesTheRenderedNamespaceRelativeName() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString outerPackage = controller.addElement(QStringLiteral("package"));
  const QString innerPackage = controller.addElement(QStringLiteral("package"));
  const QString typeId = controller.addElement(QStringLiteral("class"));
  const auto rename = [&](const QString &id, const QString &name) {
    controller.selectObject(id, QStringLiteral("element"));
    controller.setSelectedName(name);
  };
  rename(outerPackage, QStringLiteral("VeryLongOuterNamespaceName"));
  rename(innerPackage, QStringLiteral("AnotherVeryLongNestedNamespaceName"));
  rename(typeId, QStringLiteral("Compact"));

  const auto itemJson = [](const QString &id) {
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{QJsonObject{
                          {QStringLiteral("kind"), QStringLiteral("element")},
                          {QStringLiteral("id"), id}}})
            .toJson(QJsonDocument::Compact));
  };
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      itemJson(innerPackage), QStringLiteral("element"), outerPackage));
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      itemJson(typeId), QStringLiteral("element"), innerPackage));
  controller.selectObject(outerPackage, QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);

  const Diagram &initialDiagram = controller.data().diagrams.first();
  const auto node =
      std::find_if(initialDiagram.nodes.cbegin(), initialDiagram.nodes.cend(),
                   [&](const NodePresentation &candidate) {
                     return candidate.elementId == typeId;
                   });
  QVERIFY(node != initialDiagram.nodes.cend());
  const QString nodeId = node->id;

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  canvas.rightClick(node->geometry.center() + QPointF(30.0, 30.0));
  QCOMPARE(canvas.selectedNodeCount(), 1);
  controller.updateNodeGeometry(diagramId, nodeId, node->geometry.x(),
                                node->geometry.y(), 700.0, 280.0);

  canvas.fitSelectionToContent();

  const Diagram &fittedDiagram = controller.data().diagrams.first();
  const auto *fitted = findNode(fittedDiagram, nodeId);
  const auto *element = findElement(controller.data(), typeId);
  QVERIFY(fitted);
  QVERIFY(element);
  QCOMPARE(
      presentation_layout::containingPackageElementId(fittedDiagram, nodeId),
      innerPackage);
  QCOMPARE(fitted->geometry.size(),
           presentation_layout::nodeContentSizeForDisplayName(
               controller.data(), *element, QStringLiteral("Compact"),
               fitted->showAttributes.value_or(fittedDiagram.showAttributes),
               fitted->showOperations.value_or(fittedDiagram.showOperations)));
  QVERIFY(fitted->geometry.width() <
          presentation_layout::nodeContentSizeForDisplayName(
              controller.data(), *element,
              presentation_layout::fullyQualifiedElementName(controller.data(),
                                                             *element),
              fitted->showAttributes.value_or(fittedDiagram.showAttributes),
              fitted->showOperations.value_or(fittedDiagram.showOperations))
              .width());
}

void DiagramCanvasTests::connectorBendPointCanBeAddedMovedAndRemoved() {
  ProjectController controller;
  populate(controller, 2);
  useStandardInteractionGeometry(controller);
  const QString diagramId = controller.data().diagrams.first().id;
  const auto nodes = controller.data().diagrams.first().nodes;
  controller.updateNodeGeometry(diagramId, nodes.at(1).id, 500.0, 50.0, 220.0,
                                120.0);
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

void DiagramCanvasTests::relationshipEndAnnotationsAreEditable() {
  ProjectController controller;
  populate(controller, 2);
  const QString diagramId = controller.data().diagrams.first().id;
  const auto nodes = controller.data().diagrams.first().nodes;
  controller.updateNodeGeometry(diagramId, nodes.at(0).id, 50.0, 50.0, 220.0,
                                120.0);
  controller.updateNodeGeometry(diagramId, nodes.at(1).id, 500.0, 50.0, 220.0,
                                120.0);
  const QString connectorId = controller.createRelationship(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("association"));
  const auto *connector =
      findConnector(controller.data().diagrams.first(), connectorId);
  QVERIFY(connector);
  const QString relationshipId = connector->relationshipId;
  controller.editText(relationshipId, QStringLiteral("sourceRole"), -1,
                      QStringLiteral("owner"));
  controller.editText(relationshipId, QStringLiteral("sourceMultiplicity"), -1,
                      QStringLiteral("1"));
  controller.editText(relationshipId, QStringLiteral("targetRole"), -1,
                      QStringLiteral("items"));
  controller.editText(relationshipId, QStringLiteral("targetMultiplicity"), -1,
                      QStringLiteral("0..*"));

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  QSignalSpy edits(&canvas, &DiagramCanvas::editRequested);

  // The route runs from scene (270, 110) to (500, 110). Automatic end labels
  // are inset 38 scene pixels and offset 14 pixels from the line. The default
  // canvas pan contributes another (30, 30) in view coordinates.
  const QList<QPair<QPointF, QString>> expectedHits = {
      {{338.0, 154.0}, QStringLiteral("sourceRole")},
      {{338.0, 126.0}, QStringLiteral("sourceMultiplicity")},
      {{492.0, 154.0}, QStringLiteral("targetRole")},
      {{492.0, 126.0}, QStringLiteral("targetMultiplicity")}};
  for (const auto &[position, field] : expectedHits) {
    canvas.doubleClick(position);
    QCOMPARE(edits.count(), 1);
    QCOMPARE(edits.takeFirst().at(1).toString(), field);
  }
}

void DiagramCanvasTests::connectorAnnotationsMoveResetAndExposeStereotypes() {
  ProjectController controller;
  populate(controller, 2);
  useStandardInteractionGeometry(controller);
  const QString diagramId = controller.data().diagrams.first().id;
  const auto nodes = controller.data().diagrams.first().nodes;
  controller.updateNodeGeometry(diagramId, nodes.at(1).id, 500.0, 50.0, 220.0,
                                120.0);
  const QString connectorId = controller.createRelationship(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("association"));
  const auto connector = [&]() {
    return findConnector(controller.data().diagrams.first(), connectorId);
  };
  QVERIFY(connector());
  const QString relationshipId = connector()->relationshipId;
  controller.editText(relationshipId, QStringLiteral("name"), -1,
                      QStringLiteral("associated with"));

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);

  // Scene route: (270, 110) to (500, 110). The "associated with" annotation
  // starts at scene (385, 110), and the canvas contributes its default
  // (30, 30) pan.
  const QPointF automaticName(415.0, 140.0);
  const QPointF movedName(415.0, 180.0);
  canvas.drag(automaticName, movedName);
  const auto placement =
      connector()->annotationPlacements.value(QStringLiteral("name"));
  QVERIFY(connector()->annotationPlacements.contains(QStringLiteral("name")));
  QVERIFY(placement.routePosition >= 0.0 && placement.routePosition <= 1.0);
  QVERIFY(qAbs(placement.normalOffset - 40.0) < 0.01);
  QCOMPARE(controller.undoText(), QStringLiteral("Move connector annotation"));

  controller.undo();
  QVERIFY(connector()->annotationPlacements.isEmpty());
  controller.redo();
  QCOMPARE(connector()->annotationPlacements.value(QStringLiteral("name")),
           placement);

  canvas.rightClick(movedName);
  QVERIFY(canvas.contextAnnotationHasManualPosition());
  canvas.resetContextAnnotationPosition();
  QVERIFY(connector()->annotationPlacements.isEmpty());
  controller.undo();
  QCOMPARE(connector()->annotationPlacements.value(QStringLiteral("name")),
           placement);

  // Escape restores the committed placement and does not add history.
  canvas.press(movedName);
  canvas.move({415.0, 220.0});
  canvas.key(Qt::Key_Escape);
  canvas.release({415.0, 220.0});
  QCOMPARE(connector()->annotationPlacements.value(QStringLiteral("name")),
           placement);
  QCOMPARE(controller.undoText(), QStringLiteral("Move connector annotation"));

  const QString sourceElement = nodes.at(0).elementId;
  controller.assignStereotypes(QStringLiteral("element"), sourceElement,
                               {QStringLiteral("uml.interface")});
  controller.assignStereotypes(QStringLiteral("relationship"), relationshipId,
                               {QStringLiteral("uml.trace")});
  QSignalSpy stereotypeEdits(&canvas, &DiagramCanvas::stereotypeEditRequested);

  // Element stereotypes occupy the line immediately above the bold name.
  canvas.doubleClick({190.0, 90.0});
  QCOMPARE(stereotypeEdits.count(), 1);
  const auto elementEdit = stereotypeEdits.takeFirst();
  QCOMPARE(elementEdit.size(), 6);
  QCOMPARE(elementEdit.at(0).toString(), sourceElement);
  QCOMPARE(elementEdit.at(1).toString(), QStringLiteral("element"));
  QVERIFY(elementEdit.at(4).toReal() > 0.0);
  QVERIFY(elementEdit.at(5).toReal() > 0.0);

  // The relationship stereotype is independently movable and invokes the
  // catalog assignment UI when double-clicked.
  canvas.doubleClick({415.0, 127.0});
  QCOMPARE(stereotypeEdits.count(), 1);
  const auto edit = stereotypeEdits.takeFirst();
  QCOMPARE(edit.size(), 6);
  QCOMPARE(edit.at(0).toString(), relationshipId);
  QCOMPARE(edit.at(1).toString(), QStringLiteral("relationship"));
  QVERIFY(edit.at(4).toReal() > 0.0);
  QVERIFY(edit.at(5).toReal() > 0.0);
}

void DiagramCanvasTests::edgeGestureCreatesCancelsAndSupportsSelfConnections() {
  ProjectController controller;
  populate(controller, 2);
  useStandardInteractionGeometry(controller);
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

void DiagramCanvasTests::
    relationshipToolboxRequiresSelectedEdgeAndCreatesConnector() {
  ProjectController controller;
  populate(controller, 2);
  useStandardInteractionGeometry(controller);
  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  const auto nodes = controller.data().diagrams.first().nodes;

  // Hover alone is intentionally insufficient: a toolbox candidate exists
  // only on the perimeter of a presentation that is already selected.
  canvas.hover({300.0, 140.0});
  QVERIFY(!canvas.relationshipToolboxCandidate());

  canvas.press({190.0, 140.0});
  canvas.release({190.0, 140.0});
  QCOMPARE(canvas.selectedNodeCount(), 1);
  canvas.hover({190.0, 140.0});
  QVERIFY(!canvas.relationshipToolboxCandidate());
  canvas.hover({300.0, 140.0}, {190.0, 140.0});
  QVERIFY(canvas.relationshipToolboxCandidate());
  QCOMPARE(canvas.relationshipToolboxNodeId(), nodes.at(0).id);
  QCOMPARE(canvas.relationshipToolboxEdge(), QStringLiteral("right"));
  QCOMPARE(canvas.relationshipToolboxSceneAnchor(), QPointF(270.0, 110.0));

  // Retain the chosen side while travelling through a corner. This prevents
  // tiny diagonal movements from closing and reopening the toolbox on the
  // adjacent side.
  canvas.hover({190.0, 83.0}, {300.0, 140.0});
  QCOMPARE(canvas.relationshipToolboxEdge(), QStringLiteral("top"));
  canvas.hover({294.0, 83.0}, {190.0, 83.0});
  QCOMPARE(canvas.relationshipToolboxEdge(), QStringLiteral("top"));
  canvas.hover({298.0, 84.0}, {294.0, 83.0});
  QCOMPARE(canvas.relationshipToolboxEdge(), QStringLiteral("top"));
  canvas.hover({298.0, 110.0}, {298.0, 84.0});
  QCOMPARE(canvas.relationshipToolboxEdge(), QStringLiteral("right"));

  // The QML toolbar forwards its press-drag-release coordinates to these
  // methods. Creation still uses the ordinary command-backed relationship
  // path and the same snapped perimeter anchors as keyboard edge gestures.
  QVERIFY(canvas.beginToolboxRelationship(
      QStringLiteral("realization"), canvas.relationshipToolboxNodeId(),
      canvas.relationshipToolboxSceneAnchor().x(),
      canvas.relationshipToolboxSceneAnchor().y()));
  canvas.updateToolboxRelationship(330.0, 140.0);
  canvas.finishToolboxRelationship(330.0, 140.0);
  QCOMPARE(controller.data().relationships.size(), 1);
  QVERIFY(controller.data().relationships.first().type ==
          RelationshipType::Realization);
  const auto &connector = controller.data().diagrams.first().connectors.first();
  QVERIFY(connector.sourceAnchor.side == ConnectorSide::Right);
  QVERIFY(connector.targetAnchor.side == ConnectorSide::Left);
}

void DiagramCanvasTests::multiSelectionToolboxTracksArrangementCommands() {
  ProjectController controller;
  populate(controller, 2);
  useStandardInteractionGeometry(controller);
  const auto nodes = controller.data().diagrams.first().nodes;
  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);

  canvas.press({190.0, 140.0});
  canvas.release({190.0, 140.0});
  canvas.press({440.0, 140.0}, Qt::ControlModifier);
  canvas.release({440.0, 140.0}, Qt::ControlModifier);
  QCOMPARE(canvas.selectedNodeCount(), 2);

  // The arrangement surface is contextual: it appears over the selected
  // group, not merely because several nodes happen to be selected.
  canvas.hover({700.0, 400.0});
  QVERIFY(!canvas.arrangementToolboxCandidate());
  canvas.hover({190.0, 140.0});
  QVERIFY(canvas.arrangementToolboxCandidate());
  QCOMPARE(canvas.arrangementToolboxNodeId(), nodes.at(0).id);
  QCOMPARE(canvas.arrangementToolboxViewAnchor(), QPointF(190.0, 80.0));
  canvas.hover({440.0, 140.0}, {190.0, 140.0});
  QCOMPARE(canvas.arrangementToolboxNodeId(), nodes.at(1).id);
  QCOMPARE(canvas.arrangementToolboxViewAnchor(), QPointF(440.0, 80.0));

  // Selected edges still belong to the relationship toolbox. Moving back to
  // the body restores the mutually exclusive arrangement candidate.
  canvas.hover({300.0, 140.0}, {440.0, 140.0});
  QVERIFY(canvas.relationshipToolboxCandidate());
  QVERIFY(!canvas.arrangementToolboxCandidate());
  canvas.hover({440.0, 140.0}, {300.0, 140.0});
  QVERIFY(!canvas.relationshipToolboxCandidate());
  QVERIFY(canvas.arrangementToolboxCandidate());
  QCOMPARE(canvas.arrangementToolboxNodeId(), nodes.at(1).id);

  // The QML buttons invoke the existing command-backed arrangement actions.
  // The anchor follows the hovered item through committed geometry changes so
  // several operations can be performed without reselecting the group.
  canvas.arrangeSelection(QStringLiteral("alignLeft"));
  QCOMPARE(canvas.arrangementToolboxViewAnchor(), QPointF(190.0, 80.0));
  QVERIFY(controller.canUndo());
  controller.undo();
  QCOMPARE(canvas.arrangementToolboxViewAnchor(), QPointF(440.0, 80.0));

  canvas.clearCanvasSelection();
  QVERIFY(!canvas.arrangementToolboxCandidate());
  QVERIFY(canvas.arrangementToolboxNodeId().isEmpty());
  QVERIFY(canvas.arrangementToolboxViewAnchor().isNull());
}

void DiagramCanvasTests::connectorToolboxEditsRoutingAndAnnotations() {
  ProjectController controller;
  populate(controller, 2);
  useStandardInteractionGeometry(controller);
  const QString diagramId = controller.data().diagrams.first().id;
  const auto nodes = controller.data().diagrams.first().nodes;
  const QString connectorId = controller.createRelationship(
      diagramId, nodes.at(0).id, nodes.at(1).id, QStringLiteral("association"));
  QVERIFY(!connectorId.isEmpty());

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);

  // A connector toolbox is contextual: merely having a connector in the
  // diagram is insufficient. The pointer must be over the selected route or
  // one of its annotations.
  canvas.hover({315.0, 140.0});
  QVERIFY(!canvas.connectorToolboxCandidate());
  canvas.press({315.0, 140.0});
  canvas.release({315.0, 140.0});
  QVERIFY(canvas.connectorSelected());
  canvas.hover({700.0, 400.0}, {315.0, 140.0});
  QVERIFY(!canvas.connectorToolboxCandidate());
  canvas.hover({315.0, 140.0}, {700.0, 400.0});
  QVERIFY(canvas.connectorToolboxCandidate());
  QCOMPARE(canvas.connectorToolboxConnectorId(), connectorId);
  QCOMPARE(canvas.connectorToolboxViewAnchor(), QPointF(315.0, 140.0));

  canvas.setSelectedConnectorRouting(QStringLiteral("orthogonal"));
  QCOMPARE(canvas.selectedConnectorRouting(), QStringLiteral("orthogonal"));
  QCOMPARE(controller.undoText(),
           QStringLiteral("Use orthogonal connector routing"));
  QVERIFY(canvas.connectorToolboxCandidate());
  controller.undo();
  QCOMPARE(canvas.selectedConnectorRouting(), QStringLiteral("straight"));

  // Optional annotations without rendered text can still be created from the
  // toolbox. The canvas computes their normal automatic location and opens the
  // same in-place editor used by a text double-click.
  QSignalSpy textEdits(&canvas, &DiagramCanvas::editRequested);
  canvas.editSelectedConnectorAnnotation(QStringLiteral("sourceRole"));
  QCOMPARE(textEdits.count(), 1);
  const auto edit = textEdits.takeFirst();
  const auto *connector =
      findConnector(controller.data().diagrams.first(), connectorId);
  QVERIFY(connector);
  QCOMPARE(edit.at(0).toString(), connector->relationshipId);
  QCOMPARE(edit.at(1).toString(), QStringLiteral("sourceRole"));
  QCOMPARE(edit.at(3).toString(), QString{});
  QVERIFY(edit.at(6).toReal() > 0.0);
  QVERIFY(edit.at(7).toReal() > 0.0);

  QSignalSpy stereotypeEdits(&canvas, &DiagramCanvas::stereotypeEditRequested);
  canvas.editSelectedConnectorAnnotation(QStringLiteral("stereotypes"));
  QCOMPARE(stereotypeEdits.count(), 1);
  QCOMPARE(stereotypeEdits.first().at(0).toString(), connector->relationshipId);
  QCOMPARE(stereotypeEdits.first().at(1).toString(),
           QStringLiteral("relationship"));

  controller.setConnectorAnnotationPlacement(
      diagramId, connectorId, QStringLiteral("name"), 0.5, 0.0, 30.0);
  QVERIFY(canvas.selectedConnectorHasManualAnnotationPositions());
  canvas.resetSelectedConnectorAnnotationPositions();
  QVERIFY(!canvas.selectedConnectorHasManualAnnotationPositions());
  QCOMPARE(controller.undoText(),
           QStringLiteral("Reset connector annotation positions"));
  controller.undo();
  QVERIFY(canvas.selectedConnectorHasManualAnnotationPositions());

  canvas.key(Qt::Key_Escape);
  QVERIFY(!canvas.connectorToolboxCandidate());
}

void DiagramCanvasTests::
    presentationToolboxTracksNodesContainersAndPriorities() {
  ProjectController controller;
  populate(controller, 2);
  useStandardInteractionGeometry(controller);
  const QString diagramId = controller.data().diagrams.first().id;
  const auto initialNodes = controller.data().diagrams.first().nodes;
  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);

  canvas.press({190.0, 140.0});
  canvas.release({190.0, 140.0});
  canvas.hover({190.0, 140.0});
  QVERIFY(canvas.presentationToolboxCandidate());
  QCOMPARE(canvas.presentationToolboxPresentationId(), initialNodes.at(0).id);
  QCOMPARE(canvas.presentationToolboxKind(), QStringLiteral("node"));
  QCOMPARE(canvas.presentationToolboxViewAnchor(), QPointF(190.0, 80.0));

  QSignalSpy edits(&canvas, &DiagramCanvas::editRequested);
  canvas.editSelectedPresentationName();
  QCOMPARE(edits.count(), 1);
  QCOMPARE(edits.first().at(0).toString(), initialNodes.at(0).elementId);
  QCOMPARE(edits.first().at(1).toString(), QStringLiteral("name"));
  QVERIFY(edits.first().at(6).toReal() > 0.0);
  QVERIFY(edits.first().at(7).toReal() > 0.0);

  // A selected edge belongs exclusively to relationship creation.
  canvas.hover({300.0, 140.0}, {190.0, 140.0});
  QVERIFY(canvas.relationshipToolboxCandidate());
  QVERIFY(!canvas.presentationToolboxCandidate());

  // Multi-selection uses the arrangement palette rather than the single
  // presentation commands.
  canvas.press({440.0, 140.0}, Qt::ControlModifier);
  canvas.release({440.0, 140.0}, Qt::ControlModifier);
  canvas.hover({440.0, 140.0});
  QVERIFY(canvas.arrangementToolboxCandidate());
  QVERIFY(!canvas.presentationToolboxCandidate());

  canvas.clearCanvasSelection();
  const QString folderId = controller.addBrowserFolder(
      QStringLiteral("model"), {}, QStringLiteral("Toolbox group"));
  const QString folderJson = QString::fromUtf8(
      QJsonDocument(QJsonArray{QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("folder")},
                        {QStringLiteral("id"), folderId}}})
          .toJson(QJsonDocument::Compact));
  QCOMPARE(
      controller.addTreeItemsToDiagram(diagramId, {}, folderJson, 650.0, 300.0),
      1);
  const auto &container =
      controller.data().diagrams.first().containers.constLast();
  const QPointF viewPan(30.0, 30.0);
  const QPointF headerPoint =
      container.geometry.topLeft() + QPointF(12.0, 10.0) + viewPan;
  canvas.press(headerPoint);
  canvas.release(headerPoint);
  QVERIFY(canvas.containerSelected());
  canvas.hover(headerPoint);
  QVERIFY(canvas.presentationToolboxCandidate());
  QCOMPARE(canvas.presentationToolboxPresentationId(), container.id);
  QCOMPARE(canvas.presentationToolboxKind(), QStringLiteral("container"));
  QCOMPARE(canvas.presentationToolboxViewAnchor(),
           QPointF(container.geometry.center().x() + viewPan.x(),
                   container.geometry.top() + viewPan.y()));

  edits.clear();
  canvas.editSelectedPresentationName();
  QCOMPARE(edits.count(), 1);
  QCOMPARE(edits.first().at(0).toString(), folderId);
  QCOMPARE(edits.first().at(1).toString(), QStringLiteral("name"));

  // Container interiors remain diagram workspace; only the selected frame
  // header advertises container-level commands.
  canvas.hover(container.geometry.center() + viewPan, headerPoint);
  QVERIFY(!canvas.presentationToolboxCandidate());
  canvas.hover(headerPoint, container.geometry.center() + viewPan);
  QVERIFY(canvas.presentationToolboxCandidate());
  canvas.key(Qt::Key_Escape);
  QVERIFY(!canvas.presentationToolboxCandidate());
}

void DiagramCanvasTests::inPlaceNameEditorUsesRenderedName() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString packageId = controller.addElement(QStringLiteral("package"));
  controller.selectObject(packageId, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("domain"));
  const QString typeId = controller.addElement(QStringLiteral("class"));
  controller.selectObject(typeId, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("domain::Service"));

  const QString typeJson = QString::fromUtf8(
      QJsonDocument(QJsonArray{QJsonObject{
                        {QStringLiteral("kind"), QStringLiteral("element")},
                        {QStringLiteral("id"), typeId}}})
          .toJson(QJsonDocument::Compact));
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      typeJson, QStringLiteral("element"), packageId));
  controller.selectObject(packageId, QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);

  const Diagram &diagram = controller.data().diagrams.first();
  const auto node =
      std::find_if(diagram.nodes.cbegin(), diagram.nodes.cend(),
                   [&](const NodePresentation &candidate) {
                     return candidate.elementId == typeId;
                   });
  QVERIFY(node != diagram.nodes.cend());

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  const QPointF header =
      node->geometry.topLeft() + QPointF(node->geometry.width() / 2.0, 12.0) +
      QPointF(30.0, 30.0);
  canvas.press(header);
  canvas.release(header);

  QSignalSpy edits(&canvas, &DiagramCanvas::editRequested);
  canvas.editSelectedPresentationName();
  QCOMPARE(edits.count(), 1);
  QCOMPARE(edits.takeFirst().at(3).toString(), QStringLiteral("Service"));

  canvas.doubleClick(header);
  QCOMPARE(edits.count(), 1);
  QCOMPARE(edits.takeFirst().at(3).toString(), QStringLiteral("Service"));
}

void DiagramCanvasTests::connectorEndpointsDragToReattachAndCancel() {
  ProjectController controller;
  populate(controller, 3);
  useStandardInteractionGeometry(controller);
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

void DiagramCanvasTests::connectorPortsSnapAndRemainFreelyPlaceable() {
  const QVector<qreal> offsets = connector_ports::snapOffsets(3);
  QCOMPARE(offsets, QVector<qreal>({0.25, 0.5, 0.75}));

  ProjectController controller;
  populate(controller, 2);
  useStandardInteractionGeometry(controller);
  const QString diagramId = controller.data().diagrams.first().id;
  const auto nodes = controller.data().diagrams.first().nodes;
  controller.setNodePortSnapPoints(diagramId, nodes.at(0).id, 1, 3);
  controller.setNodePortSnapPoints(diagramId, nodes.at(1).id, 1, 3);

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);

  // Pan is (30, 30). Both raw points are four view pixels from a quarter or
  // three-quarter marker on the vertical sides, inside the magnetic threshold.
  canvas.press({300.0, 114.0});
  canvas.key(Qt::Key_D);
  canvas.move({330.0, 166.0});
  canvas.release({330.0, 166.0});
  QCOMPARE(controller.data().relationships.size(), 1);
  const QString connectorId =
      controller.data().diagrams.first().connectors.first().id;
  const auto connector = [&]() {
    return findConnector(controller.data().diagrams.first(), connectorId);
  };
  QVERIFY(connector()->sourceAnchor.side == ConnectorSide::Right);
  QCOMPARE(connector()->sourceAnchor.offset, 0.25);
  QVERIFY(connector()->targetAnchor.side == ConnectorSide::Left);
  QCOMPARE(connector()->targetAnchor.offset, 0.75);

  // Alt explicitly suppresses magnetism, allowing a connector end to remain
  // at any free perimeter offset even inside the snap threshold.
  canvas.drag({300.0, 110.0}, {300.0, 114.0}, Qt::AltModifier);
  QCOMPARE(connector()->sourceAnchor.offset, 34.0 / 120.0);

  // Relative offsets make a snapped port follow the same point as the
  // presentation grows; no per-connector resize update is required.
  controller.undo();
  QCOMPARE(connector()->sourceAnchor.offset, 0.25);
  controller.updateNodeGeometry(diagramId, nodes.at(0).id, 50.0, 50.0, 220.0,
                                240.0);
  QCOMPARE(connector()->sourceAnchor.offset, 0.25);
}

void DiagramCanvasTests::liveDragSnappingIsUndoableAndAltSuppressesIt() {
  ProjectController controller;
  populate(controller, 2);
  useStandardInteractionGeometry(controller);
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
  QCOMPARE(controller.undoText(), QStringLiteral("Move diagram elements"));
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

void DiagramCanvasTests::
    folderContainerMovesDescendantsAndResizesIndependently() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString first = controller.addElement(QStringLiteral("class"));
  const QString second = controller.addElement(QStringLiteral("struct"));
  const QString folder = controller.addBrowserFolder(
      QStringLiteral("model"), {}, QStringLiteral("Architecture"));
  const QString subfolder = controller.addBrowserFolder(
      QStringLiteral("folder"), folder, QStringLiteral("Details"));
  const auto itemJson = [](const QString &kind, const QString &id) {
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{QJsonObject{{QStringLiteral("kind"), kind},
                                             {QStringLiteral("id"), id}}})
            .toJson(QJsonDocument::Compact));
  };
  QVERIFY(
      controller.moveBrowserItems(itemJson(QStringLiteral("element"), first),
                                  QStringLiteral("folder"), folder));
  QVERIFY(
      controller.moveBrowserItems(itemJson(QStringLiteral("element"), second),
                                  QStringLiteral("folder"), subfolder));
  QCOMPARE(controller.addTreeItemsToDiagram(
               diagramId, {first, second},
               itemJson(QStringLiteral("folder"), folder), 100.0, 100.0),
           4);

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  const ProjectData beforeMove = controller.data();
  const Diagram &beforeDiagram = beforeMove.diagrams.first();
  const auto rootFrame = std::find_if(
      beforeDiagram.containers.cbegin(), beforeDiagram.containers.cend(),
      [&](const ContainerPresentation &candidate) {
        return candidate.subjectId == folder;
      });
  const auto childFrame = std::find_if(
      beforeDiagram.containers.cbegin(), beforeDiagram.containers.cend(),
      [&](const ContainerPresentation &candidate) {
        return candidate.subjectId == subfolder;
      });
  const auto firstNode =
      std::find_if(beforeDiagram.nodes.cbegin(), beforeDiagram.nodes.cend(),
                   [&](const NodePresentation &candidate) {
                     return candidate.elementId == first;
                   });
  const auto secondNode =
      std::find_if(beforeDiagram.nodes.cbegin(), beforeDiagram.nodes.cend(),
                   [&](const NodePresentation &candidate) {
                     return candidate.elementId == second;
                   });
  QVERIFY(rootFrame != beforeDiagram.containers.cend());
  QVERIFY(childFrame != beforeDiagram.containers.cend());
  QVERIFY(firstNode != beforeDiagram.nodes.cend());
  QVERIFY(secondNode != beforeDiagram.nodes.cend());
  const QString rootFrameId = rootFrame->id;
  const QString childFrameId = childFrame->id;
  const QPointF headerPoint =
      rootFrame->geometry.topLeft() + QPointF(10, 10) + QPointF(30, 30);
  canvas.drag(headerPoint, headerPoint + QPointF(20, 15));
  const Diagram &movedDiagram = controller.data().diagrams.first();
  for (const auto &container : beforeDiagram.containers) {
    const auto *moved = findContainer(movedDiagram, container.id);
    QVERIFY(moved);
    QCOMPARE(moved->geometry, container.geometry.translated(20, 15));
  }
  for (const auto &node : beforeDiagram.nodes) {
    const auto *moved = findNode(movedDiagram, node.id);
    QVERIFY(moved);
    QCOMPARE(moved->geometry, node.geometry.translated(20, 15));
  }
  controller.undo();
  QCOMPARE(controller.data(), beforeMove);

  const QPointF resizePoint =
      rootFrame->geometry.bottomRight() - QPointF(4, 4) + QPointF(30, 30);
  canvas.drag(resizePoint, resizePoint + QPointF(30, 20));
  const Diagram &resizedDiagram = controller.data().diagrams.first();
  QCOMPARE(findContainer(resizedDiagram, rootFrameId)->geometry,
           rootFrame->geometry.adjusted(0, 0, 30, 20));
  for (const auto &node : beforeDiagram.nodes)
    QCOMPARE(findNode(resizedDiagram, node.id)->geometry, node.geometry);
  controller.undo();
  QCOMPARE(controller.data(), beforeMove);

  // Membership changes only when the completed drag is dropped. Geometry and
  // ownership share one command, so one undo restores both exactly.
  const QPointF viewPan(30, 30);
  canvas.drag(firstNode->geometry.center() + viewPan,
              childFrame->geometry.center() + viewPan);
  const ProjectData movedIntoChild = controller.data();
  const Diagram &childDropDiagram = movedIntoChild.diagrams.first();
  QVERIFY(!findContainer(childDropDiagram, rootFrameId)
               ->childPresentationIds.contains(firstNode->id));
  QVERIFY(findContainer(childDropDiagram, childFrameId)
              ->childPresentationIds.contains(firstNode->id));
  QVERIFY(ProjectSerializer::validate(movedIntoChild).isEmpty());
  controller.undo();
  QCOMPARE(controller.data(), beforeMove);
  controller.redo();
  QCOMPARE(controller.data(), movedIntoChild);
  controller.undo();
  QCOMPARE(controller.data(), beforeMove);

  const QPointF outsideRoot(rootFrame->geometry.right() + 120,
                            rootFrame->geometry.bottom() + 120);
  canvas.drag(secondNode->geometry.center() + viewPan, outsideRoot + viewPan);
  const Diagram &rootDropDiagram = controller.data().diagrams.first();
  QVERIFY(!findContainer(rootDropDiagram, childFrameId)
               ->childPresentationIds.contains(secondNode->id));
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());
  controller.undo();
  QCOMPARE(controller.data(), beforeMove);

  const QPointF childHeader =
      childFrame->geometry.topLeft() + QPointF(10, 10) + viewPan;
  canvas.drag(childHeader, outsideRoot + viewPan);
  const Diagram &detachedFrameDiagram = controller.data().diagrams.first();
  QVERIFY(!findContainer(detachedFrameDiagram, rootFrameId)
               ->childPresentationIds.contains(childFrameId));
  QVERIFY(findContainer(detachedFrameDiagram, childFrameId)
              ->childPresentationIds.contains(secondNode->id));
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());
  controller.undo();
  QCOMPARE(controller.data(), beforeMove);

  QSignalSpy editRequested(&canvas, &DiagramCanvas::editRequested);
  canvas.doubleClick(headerPoint);
  QCOMPARE(editRequested.size(), 1);
  QCOMPARE(editRequested.first().at(0).toString(), folder);

  canvas.drag(headerPoint, headerPoint);
  QVERIFY(canvas.containerSelected());
  canvas.key(Qt::Key_Delete);
  QCOMPARE(controller.data().diagrams.first().containers.size(), 1);
  QCOMPARE(controller.data().diagrams.first().nodes.size(), 2);
  controller.undo();
  QCOMPARE(controller.data(), beforeMove);
}

void DiagramCanvasTests::nestedPackageDiagramMovesRemainPresentational() {
  ProjectController controller;
  const QString diagramId = controller.data().diagrams.first().id;
  const QString parentPackage =
      controller.addElement(QStringLiteral("package"));
  const QString childPackage = controller.addElement(QStringLiteral("package"));
  const QString unrelatedPackage =
      controller.addElement(QStringLiteral("package"));
  const QString childType = controller.addElement(QStringLiteral("class"));
  const auto browserItemJson = [](const QString &id) {
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{QJsonObject{
                          {QStringLiteral("kind"), QStringLiteral("element")},
                          {QStringLiteral("id"), id}}})
            .toJson(QJsonDocument::Compact));
  };
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      browserItemJson(childPackage), QStringLiteral("element"), parentPackage));
  QVERIFY(controller.moveBrowserItemsWithPackageReassignment(
      browserItemJson(childType), QStringLiteral("element"), childPackage));
  controller.selectObject(parentPackage, QStringLiteral("element"));
  controller.addSelectedToDiagram(diagramId);
  QCOMPARE(controller.addEmptyPackageToDiagram(diagramId, unrelatedPackage), 1);

  const auto unrelatedFrameBeforeMove =
      std::find_if(controller.data().diagrams.first().containers.cbegin(),
                   controller.data().diagrams.first().containers.cend(),
                   [&](const ContainerPresentation &candidate) {
                     return candidate.subjectId == unrelatedPackage;
                   });
  QVERIFY(unrelatedFrameBeforeMove !=
          controller.data().diagrams.first().containers.cend());
  const QVariantMap unrelatedGeometry{
      {QStringLiteral("id"), unrelatedFrameBeforeMove->id},
      {QStringLiteral("x"), 600.0},
      {QStringLiteral("y"), 80.0},
      {QStringLiteral("width"), 240.0},
      {QStringLiteral("height"), 200.0}};
  controller.updatePresentationGeometries(
      diagramId, {unrelatedGeometry},
      QStringLiteral("Position unrelated namespace test frame"));

  TestDiagramCanvas canvas;
  configureCanvas(canvas, controller);
  const Diagram before = controller.data().diagrams.first();
  const auto parentFrame =
      std::find_if(before.containers.cbegin(), before.containers.cend(),
                   [&](const ContainerPresentation &candidate) {
                     return candidate.subjectId == parentPackage;
                   });
  const auto childFrame =
      std::find_if(before.containers.cbegin(), before.containers.cend(),
                   [&](const ContainerPresentation &candidate) {
                     return candidate.subjectId == childPackage;
                   });
  const auto unrelatedFrame =
      std::find_if(before.containers.cbegin(), before.containers.cend(),
                   [&](const ContainerPresentation &candidate) {
                     return candidate.subjectId == unrelatedPackage;
                   });
  const auto childNode =
      std::find_if(before.nodes.cbegin(), before.nodes.cend(),
                   [&](const NodePresentation &candidate) {
                     return candidate.elementId == childType;
                   });
  QVERIFY(parentFrame != before.containers.cend());
  QVERIFY(childFrame != before.containers.cend());
  QVERIFY(unrelatedFrame != before.containers.cend());
  QVERIFY(childNode != before.nodes.cend());

  // A namespace frame visually asserts semantic containment. Reject a drop
  // into an unrelated namespace and restore the complete preview unchanged.
  const ProjectData beforeRejectedDrop = controller.data();
  const QPointF viewPan(30, 30);
  canvas.drag(childNode->geometry.center() + viewPan,
              unrelatedFrame->geometry.center() + viewPan);
  QCOMPARE(controller.data(), beforeRejectedDrop);

  const QPointF start =
      childFrame->geometry.topLeft() + QPointF(10, 10) + viewPan;
  canvas.drag(start, start + QPointF(10, 8));

  QCOMPARE(findElement(controller.data(), childPackage)->packageId,
           parentPackage);
  QCOMPARE(findContainer(controller.data().diagrams.first(), childFrame->id)
               ->geometry,
           childFrame->geometry.translated(10, 8));

  const auto *movedChild =
      findContainer(controller.data().diagrams.first(), childFrame->id);
  QVERIFY(movedChild);
  const QPointF detachedStart =
      movedChild->geometry.topLeft() + QPointF(10, 10) + QPointF(30, 30);
  canvas.drag(detachedStart, QPointF(800, 520));

  QCOMPARE(findElement(controller.data(), childPackage)->packageId,
           parentPackage);
  const auto *currentParent =
      findContainer(controller.data().diagrams.first(), parentFrame->id);
  QVERIFY(currentParent);
  QVERIFY(!currentParent->childPresentationIds.contains(childFrame->id));
  QVERIFY(ProjectSerializer::validate(controller.data()).isEmpty());
}

QTEST_MAIN(DiagramCanvasTests)

#include "diagram_canvas_tests.moc"
