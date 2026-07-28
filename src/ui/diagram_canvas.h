#pragma once

#include "core/project_controller.h"
#include "ui/connector_routing.h"

#include <QHash>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QQuickItem>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

namespace uuml {

struct ConnectorPresentation;
struct ContainerPresentation;
struct Diagram;
struct NodePresentation;

class DiagramCanvas : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(ProjectController *project READ project WRITE setProject NOTIFY
                 projectChanged)
  Q_PROPERTY(QString diagramId READ diagramId WRITE setDiagramId NOTIFY
                 diagramIdChanged)
  Q_PROPERTY(qreal zoom READ zoom NOTIFY viewportChanged)
  Q_PROPERTY(bool filterActive READ filterActive NOTIFY diagramFilterChanged)
  Q_PROPERTY(
      int visibleNodeCount READ visibleNodeCount NOTIFY diagramFilterChanged)
  Q_PROPERTY(int totalNodeCount READ totalNodeCount NOTIFY diagramFilterChanged)
  Q_PROPERTY(int selectedNodeCount READ selectedNodeCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(int selectedContainerCount READ selectedContainerCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool connectorSelected READ connectorSelected NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool containerSelected READ containerSelected NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool bendPointSelected READ bendPointSelected NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool selectedConnectorHasBendPoints READ
                 selectedConnectorHasBendPoints NOTIFY canvasSelectionChanged)
  Q_PROPERTY(bool selectedConnectorHasManualAnnotationPositions READ
                 selectedConnectorHasManualAnnotationPositions NOTIFY
                     canvasSelectionChanged)
  Q_PROPERTY(
      bool contextAnnotationHasManualPosition READ
          contextAnnotationHasManualPosition NOTIFY canvasSelectionChanged)
  Q_PROPERTY(QString connectorInteractionPrompt READ connectorInteractionPrompt
                 NOTIFY canvasSelectionChanged)
  Q_PROPERTY(int defaultDistributionGap READ defaultDistributionGap WRITE
                 setDefaultDistributionGap NOTIFY defaultDistributionGapChanged)
  Q_PROPERTY(bool snapToGridEnabled READ snapToGridEnabled WRITE
                 setSnapToGridEnabled NOTIFY snapToGridEnabledChanged)
  Q_PROPERTY(bool alignmentGuidesEnabled READ alignmentGuidesEnabled WRITE
                 setAlignmentGuidesEnabled NOTIFY alignmentGuidesEnabledChanged)
  Q_PROPERTY(int gridSpacing READ gridSpacing WRITE setGridSpacing NOTIFY
                 gridSpacingChanged)
  Q_PROPERTY(QString diagramItemSizingMode READ diagramItemSizingMode WRITE
                 setDiagramItemSizingMode NOTIFY diagramItemSizingModeChanged)
  Q_PROPERTY(
      QString defaultConnectorRouting READ defaultConnectorRouting WRITE
          setDefaultConnectorRouting NOTIFY defaultConnectorRoutingChanged)
  Q_PROPERTY(QString selectedConnectorRouting READ selectedConnectorRouting
                 NOTIFY canvasSelectionChanged)
  Q_PROPERTY(int selectedHorizontalPortSnapPoints READ
                 selectedHorizontalPortSnapPoints NOTIFY canvasSelectionChanged)
  Q_PROPERTY(int selectedVerticalPortSnapPoints READ
                 selectedVerticalPortSnapPoints NOTIFY canvasSelectionChanged)
  Q_PROPERTY(bool diagramAttributesVisible READ diagramAttributesVisible NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool diagramOperationsVisible READ diagramOperationsVisible NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(QString selectedAttributesVisibility READ
                 selectedAttributesVisibility NOTIFY canvasSelectionChanged)
  Q_PROPERTY(QString selectedOperationsVisibility READ
                 selectedOperationsVisibility NOTIFY canvasSelectionChanged)
  Q_PROPERTY(int incomingRelatedTypeCount READ incomingRelatedTypeCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(int outgoingRelatedTypeCount READ outgoingRelatedTypeCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool canWrapSelectionInPackage READ canWrapSelectionInPackage
                 NOTIFY canvasSelectionChanged)
  Q_PROPERTY(QString selectedStyleId READ selectedStyleId NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(
      QVariantMap relationshipGestureKeys READ relationshipGestureKeys WRITE
          setRelationshipGestureKeys NOTIFY relationshipGestureKeysChanged)
  Q_PROPERTY(bool relationshipToolboxCandidate READ relationshipToolboxCandidate
                 NOTIFY relationshipToolboxCandidateChanged)
  Q_PROPERTY(QString relationshipToolboxNodeId READ relationshipToolboxNodeId
                 NOTIFY relationshipToolboxCandidateChanged)
  Q_PROPERTY(QString relationshipToolboxEdge READ relationshipToolboxEdge NOTIFY
                 relationshipToolboxCandidateChanged)
  Q_PROPERTY(
      QPointF relationshipToolboxViewAnchor READ relationshipToolboxViewAnchor
          NOTIFY relationshipToolboxCandidateChanged)
  Q_PROPERTY(
      QPointF relationshipToolboxSceneAnchor READ relationshipToolboxSceneAnchor
          NOTIFY relationshipToolboxCandidateChanged)
  Q_PROPERTY(bool arrangementToolboxCandidate READ arrangementToolboxCandidate
                 NOTIFY arrangementToolboxCandidateChanged)
  Q_PROPERTY(QString arrangementToolboxNodeId READ arrangementToolboxNodeId
                 NOTIFY arrangementToolboxCandidateChanged)
  Q_PROPERTY(
      QPointF arrangementToolboxViewAnchor READ arrangementToolboxViewAnchor
          NOTIFY arrangementToolboxCandidateChanged)
  Q_PROPERTY(bool connectorToolboxCandidate READ connectorToolboxCandidate
                 NOTIFY connectorToolboxCandidateChanged)
  Q_PROPERTY(
      QString connectorToolboxConnectorId READ connectorToolboxConnectorId
          NOTIFY connectorToolboxCandidateChanged)
  Q_PROPERTY(QPointF connectorToolboxViewAnchor READ connectorToolboxViewAnchor
                 NOTIFY connectorToolboxCandidateChanged)
  Q_PROPERTY(bool presentationToolboxCandidate READ presentationToolboxCandidate
                 NOTIFY presentationToolboxCandidateChanged)
  Q_PROPERTY(QString presentationToolboxPresentationId READ
                 presentationToolboxPresentationId NOTIFY
                     presentationToolboxCandidateChanged)
  Q_PROPERTY(QString presentationToolboxKind READ presentationToolboxKind NOTIFY
                 presentationToolboxCandidateChanged)
  Q_PROPERTY(
      QPointF presentationToolboxViewAnchor READ presentationToolboxViewAnchor
          NOTIFY presentationToolboxCandidateChanged)

public:
  explicit DiagramCanvas(QQuickItem *parent = nullptr);

  ProjectController *project() const;
  void setProject(ProjectController *project);
  QString diagramId() const;
  void setDiagramId(const QString &diagramId);
  qreal zoom() const;
  bool filterActive() const;
  int visibleNodeCount() const;
  int totalNodeCount() const;
  int selectedNodeCount() const;
  int selectedContainerCount() const;
  bool connectorSelected() const;
  bool containerSelected() const;
  bool bendPointSelected() const;
  bool selectedConnectorHasBendPoints() const;
  bool selectedConnectorHasManualAnnotationPositions() const;
  bool contextAnnotationHasManualPosition() const;
  QString connectorInteractionPrompt() const;
  int defaultDistributionGap() const;
  void setDefaultDistributionGap(int gap);
  bool snapToGridEnabled() const;
  void setSnapToGridEnabled(bool enabled);
  bool alignmentGuidesEnabled() const;
  void setAlignmentGuidesEnabled(bool enabled);
  int gridSpacing() const;
  void setGridSpacing(int spacing);
  QString diagramItemSizingMode() const;
  void setDiagramItemSizingMode(const QString &mode);
  QString defaultConnectorRouting() const;
  void setDefaultConnectorRouting(const QString &routing);
  QString selectedConnectorRouting() const;
  int selectedHorizontalPortSnapPoints() const;
  int selectedVerticalPortSnapPoints() const;
  bool diagramAttributesVisible() const;
  bool diagramOperationsVisible() const;
  QString selectedAttributesVisibility() const;
  QString selectedOperationsVisibility() const;
  int incomingRelatedTypeCount() const;
  int outgoingRelatedTypeCount() const;
  bool canWrapSelectionInPackage() const;
  QString selectedStyleId() const;
  QVariantMap relationshipGestureKeys() const;
  void setRelationshipGestureKeys(const QVariantMap &keys);
  bool relationshipToolboxCandidate() const;
  QString relationshipToolboxNodeId() const;
  QString relationshipToolboxEdge() const;
  QPointF relationshipToolboxViewAnchor() const;
  QPointF relationshipToolboxSceneAnchor() const;
  bool arrangementToolboxCandidate() const;
  QString arrangementToolboxNodeId() const;
  QPointF arrangementToolboxViewAnchor() const;
  bool connectorToolboxCandidate() const;
  QString connectorToolboxConnectorId() const;
  QPointF connectorToolboxViewAnchor() const;
  bool presentationToolboxCandidate() const;
  QString presentationToolboxPresentationId() const;
  QString presentationToolboxKind() const;
  QPointF presentationToolboxViewAnchor() const;

  Q_INVOKABLE void fitToContent();
  Q_INVOKABLE QVariantMap diagramFilter() const;
  Q_INVOKABLE void setDiagramFilter(const QVariantMap &filter);
  Q_INVOKABLE void clearDiagramFilter();
  Q_INVOKABLE void addElementsAt(const QStringList &elementIds, qreal x,
                                 qreal y);
  Q_INVOKABLE void addTreeItemsAt(const QStringList &elementIds,
                                  const QString &subjectsJson, qreal x,
                                  qreal y);
  Q_INVOKABLE void createElementAtContextPosition(const QString &type);
  Q_INVOKABLE void createElementAtViewportCenter(const QString &type);
  Q_INVOKABLE void createRelationship(const QString &type);
  Q_INVOKABLE void cancelConnectorInteraction();
  Q_INVOKABLE void addBendPointAtContextPosition();
  Q_INVOKABLE void removeSelectedBendPoint();
  Q_INVOKABLE void clearSelectedConnectorBendPoints();
  Q_INVOKABLE void resetContextAnnotationPosition();
  Q_INVOKABLE void resetSelectedConnectorAnnotationPositions();
  Q_INVOKABLE void
  editSelectedConnectorAnnotation(const QString &annotationField);
  Q_INVOKABLE void editSelectedPresentationName();
  Q_INVOKABLE void setSelectedConnectorRouting(const QString &routing);
  Q_INVOKABLE void setSelectedPortSnapPoints(int horizontalPointCount,
                                             int verticalPointCount);
  Q_INVOKABLE void setDiagramCompartmentVisible(const QString &compartment,
                                                bool visible);
  Q_INVOKABLE void setSelectedCompartmentVisibility(const QString &compartment,
                                                    const QString &visibility);
  Q_INVOKABLE void addRelatedTypes(const QString &direction);
  Q_INVOKABLE void fitSelectionToContent();
  Q_INVOKABLE void wrapSelectionInPackage();
  Q_INVOKABLE void assignStyleToSelection(const QString &styleId);
  Q_INVOKABLE void arrangeSelection(const QString &operation);
  Q_INVOKABLE void nudgeSelection(qreal deltaX, qreal deltaY);
  Q_INVOKABLE void removeSelectedPresentations();
  Q_INVOKABLE void deleteSelectedConnector();
  Q_INVOKABLE void clearCanvasSelection();
  Q_INVOKABLE void refreshTheme();
  Q_INVOKABLE bool beginToolboxRelationship(const QString &relationshipType,
                                            const QString &sourceNodeId,
                                            qreal sourceSceneX,
                                            qreal sourceSceneY);
  Q_INVOKABLE void updateToolboxRelationship(qreal viewX, qreal viewY,
                                             bool suppressSnapping = false);
  Q_INVOKABLE void finishToolboxRelationship(qreal viewX, qreal viewY,
                                             bool suppressSnapping = false);

signals:
  void projectChanged();
  void diagramIdChanged();
  void viewportChanged();
  void diagramFilterChanged();
  void canvasSelectionChanged();
  void defaultDistributionGapChanged();
  void snapToGridEnabledChanged();
  void alignmentGuidesEnabledChanged();
  void gridSpacingChanged();
  void diagramItemSizingModeChanged();
  void defaultConnectorRoutingChanged();
  void relationshipGestureKeysChanged();
  void relationshipToolboxCandidateChanged();
  void arrangementToolboxCandidateChanged();
  void connectorToolboxCandidateChanged();
  void presentationToolboxCandidateChanged();
  // Actual pointer/selection interactions dismiss every contextual surface.
  // Model state changes deliberately do not: a toolbox remains usable for a
  // sequence of compact commands while the selection stays unchanged.
  void contextToolboxesDismissRequested();
  void contextMenuRequested(const QString &target, qreal x, qreal y);
  void editRequested(const QString &objectId, const QString &field, int index,
                     const QString &text, qreal x, qreal y, qreal width,
                     qreal height, qreal fontPixelSize, bool fontBold);
  void stereotypeEditRequested(const QString &objectId,
                               const QString &objectKind, qreal x, qreal y,
                               qreal width, qreal height);

protected:
  QSGNode *updatePaintNode(QSGNode *oldNode,
                           UpdatePaintNodeData *updatePaintNodeData) override;
  void geometryChange(const QRectF &newGeometry,
                      const QRectF &oldGeometry) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void hoverMoveEvent(QHoverEvent *event) override;
  void hoverLeaveEvent(QHoverEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  enum class Interaction {
    None,
    Move,
    Resize,
    Pan,
    Lasso,
    MoveSourcePort,
    MoveTargetPort,
    MoveBendPoint,
    MoveAnnotation,
    CreateConnector
  };
  enum class ResizeHandle {
    None,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
  };
  struct ConnectorEndpoints {
    QPointF source;
    QPointF target;
    QVector<QPointF> bendPoints;
    ConnectorRouting routing = ConnectorRouting::Straight;
    ConnectorSide sourceSide = ConnectorSide::Automatic;
    ConnectorSide targetSide = ConnectorSide::Automatic;
    bool valid = false;
  };
  struct TextHit {
    QString objectId;
    QString field;
    int index = -1;
    QString text;
    QRectF sceneRect;
    bool fontBold = false;
  };
  struct AnnotationHit {
    QString connectorId;
    QString relationshipId;
    QString key;
    QRectF sceneRect;
  };

  const Diagram *diagram() const;
  bool nodePassesFilter(const NodePresentation &node) const;
  bool connectorPassesFilter(const ConnectorPresentation &connector) const;
  bool pruneFilteredSelection();
  QRectF nodeGeometry(const NodePresentation &node) const;
  QRectF containerGeometry(const ContainerPresentation &container) const;
  QRectF containerChildViewport(const ContainerPresentation &container) const;
  QRectF presentationClipRect(const QString &presentationId,
                              bool *hasClip) const;
  QRectF visibleNodeGeometry(const NodePresentation &node) const;
  QRectF visibleContainerGeometry(const ContainerPresentation &container) const;
  ResizeHandle resizeHandleAt(const QRectF &geometry,
                              const QPointF &scenePoint) const;
  static Qt::Edges resizeEdges(ResizeHandle handle);
  QPointF toScene(const QPointF &point) const;
  QPointF toView(const QPointF &point) const;
  QRectF toView(const QRectF &rect) const;
  const NodePresentation *hitNode(const QPointF &scenePoint) const;
  const ContainerPresentation *hitContainer(const QPointF &scenePoint) const;
  const ContainerPresentation *
  hitContainer(const QPointF &scenePoint,
               const QSet<QString> &excludedPresentationIds) const;
  const ConnectorPresentation *hitConnector(const QPointF &scenePoint) const;
  TextHit hitText(const QPointF &scenePoint) const;
  AnnotationHit hitConnectorAnnotation(const QPointF &scenePoint) const;
  QRectF textLineRect(const QRectF &nodeRect, int line,
                      qreal headerHeight) const;
  ConnectorEndpoints
  connectorEndpoints(const ConnectorPresentation &connector) const;
  ui::ConnectorRoute
  connectorRoute(const ConnectorPresentation &connector) const;
  const NodePresentation *endpointNode(const ConnectorPresentation &connector,
                                       bool source) const;
  bool hitSelectedPort(const QPointF &scenePoint, bool &source) const;
  int hitBendPoint(const ConnectorPresentation &connector,
                   const QPointF &scenePoint) const;
  int nearestConnectorSegment(const ConnectorPresentation &connector,
                              const QPointF &scenePoint) const;
  void updateEndpointDrag(const QPointF &scenePoint, bool suppressSnapping);
  void commitEndpointDrag();
  void cancelEndpointDrag();
  void updateBendPointPreview(const QPointF &scenePoint);
  void commitBendPointPreview();
  void updateAnnotationPreview(const QPointF &scenePoint);
  void commitAnnotationPreview();
  void cancelAnnotationPreview();
  bool startConnectorGesture(const QString &relationshipType);
  void updateRelationshipToolboxCandidate(const QPointF &viewPoint);
  void clearRelationshipToolboxCandidate();
  void updateArrangementToolboxCandidate(const QPointF &viewPoint);
  void refreshArrangementToolboxAnchor();
  void clearArrangementToolboxCandidate(bool clearAnchor = false);
  void updateConnectorToolboxCandidate(const QPointF &viewPoint);
  void refreshConnectorToolboxAnchor();
  void clearConnectorToolboxCandidate(bool clearAnchor = false);
  void updatePresentationToolboxCandidate(const QPointF &viewPoint);
  void refreshPresentationToolboxAnchor();
  void clearPresentationToolboxCandidate(bool clearAnchor = false);
  void updateConnectorGesture(const QPointF &scenePoint, bool suppressSnapping);
  void commitConnectorGesture(const QPointF &scenePoint, bool suppressSnapping);
  void cancelConnectorGesture();
  QString relationshipTypeForGestureKey(const QKeyEvent &event) const;
  void commitGeometryPreview();
  void updateLassoSelection(const QPointF &scenePoint);
  void finishLassoSelection();
  void cancelLassoSelection();
  void resetLassoState();
  void selectAllInContext();
  void synchronizeProjectSelection();
  QString selectedCompartmentVisibility(bool attributes) const;
  void captureSelectedGeometry();
  void clearContainerSelection();
  void selectOnlyContainer(const QString &containerId);
  void createElementAt(const QString &type, const QPointF &sceneCenter);
  void selectNode(const QString &nodeId, bool toggle);
  void selectConnector(const QString &connectorId, bool preserveNodes);

  ProjectController *m_project = nullptr;
  QString m_diagramId;
  qreal m_zoom = 1.0;
  QPointF m_pan = {30, 30};
  QSet<QString> m_selectedNodes;
  // QSet provides cheap membership checks for rendering; this list preserves
  // click order so relationship source and target are deterministic.
  QStringList m_selectedNodeOrder;
  QSet<QString> m_selectedContainers;
  // A container is "active" only when it was selected on its own. Multi-item
  // selection still highlights containers but must not expose single-item
  // editing commands or redefine the Ctrl+A scope.
  QString m_selectedContainer;
  QString m_selectedConnector;
  Interaction m_interaction = Interaction::None;
  QPointF m_pressView;
  QPointF m_pressScene;
  QPointF m_lastPointerScene;
  QPointF m_originalPan;
  QPointF m_lassoOrigin;
  QPointF m_contextScenePoint;
  QRectF m_lassoRect;
  QSet<QString> m_lassoBaseNodes;
  QStringList m_lassoBaseNodeOrder;
  QSet<QString> m_lassoBaseContainers;
  QString m_lassoBaseContainer;
  QString m_lassoBaseConnector;
  int m_lassoBaseBendPoint = -1;
  Qt::KeyboardModifiers m_lassoModifiers = Qt::NoModifier;
  bool m_lassoActive = false;
  QString m_interactionNode;
  ResizeHandle m_resizeHandle = ResizeHandle::None;
  QHash<QString, QRectF> m_originalGeometry;
  QHash<QString, QRectF> m_previewGeometry;
  // Endpoint drags are presentation-only until a valid drop commits one
  // command. An empty target ID means the end is provisionally detached and
  // follows m_endpointDragPoint directly.
  QString m_endpointDragTargetNode;
  QPointF m_endpointDragPoint;
  ConnectorAnchor m_endpointDragAnchor;
  bool m_endpointDragActive = false;
  bool m_endpointDragSnapped = false;
  QList<ConnectorBendPoint> m_bendPointPreview;
  int m_selectedBendPoint = -1;
  int m_contextSegment = -1;
  QString m_contextAnnotationKey;
  bool m_bendPointPreviewActive = false;
  QString m_annotationDragConnector;
  QString m_annotationDragKey;
  QPointF m_annotationDragOffset;
  ConnectorAnnotationPlacement m_annotationPreview;
  bool m_annotationPreviewActive = false;
  bool m_annotationDragMoved = false;
  bool m_leftButtonPressed = false;
  QString m_connectorGestureSourceNode;
  QString m_connectorGestureType;
  ConnectorAnchor m_connectorGestureSourceAnchor;
  QString m_connectorGestureTargetNode;
  ConnectorAnchor m_connectorGestureTargetAnchor;
  QPointF m_connectorGestureTargetPoint;
  bool m_connectorGestureSourceSnapped = false;
  bool m_connectorGestureTargetSnapped = false;
  Qt::KeyboardModifiers m_pressModifiers = Qt::NoModifier;
  bool m_sceneDirty = true;
  // Selection-only changes rebuild colored geometry but can retain the
  // expensive text atlases. Model and geometry changes set both dirty flags.
  bool m_textDirty = true;
  int m_defaultDistributionGap;
  bool m_snapToGridEnabled;
  bool m_alignmentGuidesEnabled;
  int m_gridSpacing;
  QString m_diagramItemSizingMode = QStringLiteral("content");
  ConnectorRouting m_defaultConnectorRouting;
  QVariantMap m_relationshipGestureKeys;
  bool m_relationshipToolboxCandidate = false;
  QString m_relationshipToolboxNodeId;
  QString m_relationshipToolboxEdge;
  QPointF m_relationshipToolboxViewAnchor;
  QPointF m_relationshipToolboxSceneAnchor;
  bool m_arrangementToolboxCandidate = false;
  QString m_arrangementToolboxNodeId;
  QPointF m_arrangementToolboxViewAnchor;
  bool m_connectorToolboxCandidate = false;
  QString m_connectorToolboxConnectorId;
  QPointF m_connectorToolboxViewAnchor;
  bool m_presentationToolboxCandidate = false;
  QString m_presentationToolboxPresentationId;
  QString m_presentationToolboxKind;
  QPointF m_presentationToolboxViewAnchor;
  QVector<QLineF> m_alignmentGuides;
  quint64 m_themeRevision = 0;
};

} // namespace uuml
