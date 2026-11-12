#pragma once

#include "core/project_controller.h"
#include "core/workspace_controller.h"
#include "ui/connector_routing.h"

#include <QHash>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QQuickItem>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

namespace yauml {

namespace ui {
class DiagramClipLayout;
struct ActivityDiagramLayout;
struct AutomaticLayoutOptions;
struct DiagramNodeGeometry;
struct SequenceDiagramLayout;
struct SequenceMessageLayout;
}

struct ConnectorPresentation;
struct ContainerPresentation;
struct Diagram;
struct NotePresentation;
struct NodePresentation;

class DiagramCanvas : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(ProjectController *project READ project WRITE setProject NOTIFY
                 projectChanged)
  Q_PROPERTY(WorkspaceController *workspace READ workspace WRITE setWorkspace
                 NOTIFY workspaceChanged)
  Q_PROPERTY(QString diagramId READ diagramId WRITE setDiagramId NOTIFY
                 diagramIdChanged)
  Q_PROPERTY(QString diagramType READ diagramType NOTIFY diagramIdChanged)
  Q_PROPERTY(bool activitySelectionEditable READ activitySelectionEditable
                 NOTIFY canvasSelectionChanged)
  Q_PROPERTY(int selectedActivityItemCount READ selectedActivityItemCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(int selectedActivityNodeCount READ selectedActivityNodeCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(qreal zoom READ zoom NOTIFY viewportChanged)
  Q_PROPERTY(bool filterActive READ filterActive NOTIFY diagramFilterChanged)
  Q_PROPERTY(
      int visibleNodeCount READ visibleNodeCount NOTIFY diagramFilterChanged)
  Q_PROPERTY(int totalNodeCount READ totalNodeCount NOTIFY diagramFilterChanged)
  Q_PROPERTY(int selectedNodeCount READ selectedNodeCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(int selectedContainerCount READ selectedContainerCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(
      int selectedContainerInternalConnectorCount READ
          selectedContainerInternalConnectorCount NOTIFY canvasSelectionChanged)
  Q_PROPERTY(
      int selectedContainerChildPresentationCount READ
          selectedContainerChildPresentationCount NOTIFY canvasSelectionChanged)
  Q_PROPERTY(int selectedConnectorCount READ selectedConnectorCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(int selectedNoteCount READ selectedNoteCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(int selectedSequenceParticipantCount READ
                 selectedSequenceParticipantCount NOTIFY canvasSelectionChanged)
  Q_PROPERTY(int selectedSequenceMessageCount READ selectedSequenceMessageCount
                 NOTIFY canvasSelectionChanged)
  Q_PROPERTY(QString selectedSequenceMessageId READ selectedSequenceMessageId
                 NOTIFY canvasSelectionChanged)
  Q_PROPERTY(int selectedSequenceActivationCount READ
                 selectedSequenceActivationCount NOTIFY canvasSelectionChanged)
  Q_PROPERTY(
      int selectedSequenceCombinedFragmentCount READ
          selectedSequenceCombinedFragmentCount NOTIFY canvasSelectionChanged)
  Q_PROPERTY(bool noteSelected READ noteSelected NOTIFY canvasSelectionChanged)
  Q_PROPERTY(bool canReattachSelectedConnectorEnds READ
                 canReattachSelectedConnectorEnds NOTIFY canvasSelectionChanged)
  Q_PROPERTY(bool canReattachSelectedConnectorEndsToFacingSides READ
                 canReattachSelectedConnectorEndsToFacingSides NOTIFY
                     canvasSelectionChanged)
  Q_PROPERTY(bool canShiftSelectedConnectorEnds READ
                 canShiftSelectedConnectorEnds NOTIFY canvasSelectionChanged)
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
  Q_PROPERTY(bool typeIconsVisible READ typeIconsVisible WRITE
                 setTypeIconsVisible NOTIFY typeIconsVisibleChanged)
  Q_PROPERTY(QVariantMap typeIconSources READ typeIconSources WRITE
                 setTypeIconSources NOTIFY typeIconSourcesChanged)
  Q_PROPERTY(
      bool sourceEditorDoubleClickEnabled READ sourceEditorDoubleClickEnabled
          WRITE setSourceEditorDoubleClickEnabled NOTIFY
              sourceEditorDoubleClickEnabledChanged)
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
  Q_PROPERTY(bool diagramTypeIconsVisible READ diagramTypeIconsVisible NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(QString selectedAttributesVisibility READ
                 selectedAttributesVisibility NOTIFY canvasSelectionChanged)
  Q_PROPERTY(QString selectedOperationsVisibility READ
                 selectedOperationsVisibility NOTIFY canvasSelectionChanged)
  Q_PROPERTY(QString diagramOperationSignatureMode READ
                 diagramOperationSignatureMode NOTIFY canvasSelectionChanged)
  Q_PROPERTY(
      QString sequenceTargetOperationDisplayMode READ
          sequenceTargetOperationDisplayMode NOTIFY canvasSelectionChanged)
  Q_PROPERTY(
      bool sequenceMessageStereotypesVisible READ
          sequenceMessageStereotypesVisible NOTIFY canvasSelectionChanged)
  Q_PROPERTY(QString selectedOperationSignatureMode READ
                 selectedOperationSignatureMode NOTIFY canvasSelectionChanged)
  Q_PROPERTY(int incomingRelatedTypeCount READ incomingRelatedTypeCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(int outgoingRelatedTypeCount READ outgoingRelatedTypeCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool canWrapSelectionInPackage READ canWrapSelectionInPackage
                 NOTIFY canvasSelectionChanged)
  Q_PROPERTY(QString selectedStyleId READ selectedStyleId NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool automaticLayoutPreviewActive READ automaticLayoutPreviewActive
                 NOTIFY automaticLayoutPreviewChanged)
  Q_PROPERTY(int automaticLayoutPreviewCount READ automaticLayoutPreviewCount
                 NOTIFY automaticLayoutPreviewChanged)
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
  WorkspaceController *workspace() const;
  void setWorkspace(WorkspaceController *workspace);
  QString diagramId() const;
  void setDiagramId(const QString &diagramId);
  QString diagramType() const;
  qreal zoom() const;
  QRectF diagramContentBounds(qreal margin = 0.0) const;
  void configureExportViewport(const QRectF &contentBounds, qreal zoom);
  bool filterActive() const;
  int visibleNodeCount() const;
  int totalNodeCount() const;
  int selectedNodeCount() const;
  int selectedContainerCount() const;
  int selectedContainerInternalConnectorCount() const;
  int selectedContainerChildPresentationCount() const;
  int selectedConnectorCount() const;
  int selectedNoteCount() const;
  int selectedSequenceParticipantCount() const;
  int selectedSequenceMessageCount() const;
  QString selectedSequenceMessageId() const;
  int selectedSequenceActivationCount() const;
  int selectedSequenceCombinedFragmentCount() const;
  bool noteSelected() const;
  bool canReattachSelectedConnectorEnds() const;
  bool canReattachSelectedConnectorEndsToFacingSides() const;
  bool canShiftSelectedConnectorEnds() const;
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
  bool typeIconsVisible() const;
  void setTypeIconsVisible(bool visible);
  QVariantMap typeIconSources() const;
  void setTypeIconSources(const QVariantMap &sources);
  bool sourceEditorDoubleClickEnabled() const;
  void setSourceEditorDoubleClickEnabled(bool enabled);
  QString defaultConnectorRouting() const;
  void setDefaultConnectorRouting(const QString &routing);
  QString selectedConnectorRouting() const;
  int selectedHorizontalPortSnapPoints() const;
  int selectedVerticalPortSnapPoints() const;
  bool diagramAttributesVisible() const;
  bool diagramOperationsVisible() const;
  bool diagramTypeIconsVisible() const;
  QString selectedAttributesVisibility() const;
  QString selectedOperationsVisibility() const;
  QString diagramOperationSignatureMode() const;
  QString sequenceTargetOperationDisplayMode() const;
  bool sequenceMessageStereotypesVisible() const;
  QString selectedOperationSignatureMode() const;
  int incomingRelatedTypeCount() const;
  int outgoingRelatedTypeCount() const;
  bool canWrapSelectionInPackage() const;
  QString selectedStyleId() const;
  bool automaticLayoutPreviewActive() const;
  int automaticLayoutPreviewCount() const;
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
  Q_INVOKABLE void
  createConnectionDestination(const QString &type,
                              const QString &relationshipType = {});
  Q_INVOKABLE void cancelConnectionDestination();
  Q_INVOKABLE void addActivityNode(const QString &type, bool atViewportCenter);
  Q_INVOKABLE void editSelectedActivityItem();
  Q_INVOKABLE void deleteSelectedActivityItems();
  bool activitySelectionEditable() const;
  int selectedActivityItemCount() const;
  int selectedActivityNodeCount() const;
  Q_INVOKABLE void createElementAtViewportCenter(const QString &type);
  Q_INVOKABLE QString createNoteAtContextPosition();
  Q_INVOKABLE QString createNoteAtViewportCenter();
  Q_INVOKABLE QString addSequenceParticipantAtContextPosition();
  Q_INVOKABLE QString addSequenceParticipantAtViewportCenter();
  Q_INVOKABLE QVariantList sequenceExecutionContexts() const;
  Q_INVOKABLE QString saveSequenceExecutionContext(const QString &contextId,
                                                   const QString &name,
                                                   const QString &type);
  Q_INVOKABLE bool deleteSequenceExecutionContext(const QString &contextId);
  Q_INVOKABLE QVariantMap sequenceParticipantContextEditorData() const;
  Q_INVOKABLE bool
  setSelectedSequenceParticipantExecutionContext(const QString &contextId);
  Q_INVOKABLE bool createSequenceMessage(const QString &type,
                                         const QString &text,
                                         const QString &targetOperationId = {});
  Q_INVOKABLE void cancelPendingSequenceMessage();
  Q_INVOKABLE QString sequenceMessageEndpointSummary() const;
  Q_INVOKABLE bool sequenceReturnCreationAllowed() const;
  Q_INVOKABLE bool sequenceCreateCreationAllowed() const;
  Q_INVOKABLE QVariantList sequenceMessageTargetOperationChoices() const;
  Q_INVOKABLE QVariantMap sequenceMessageTargetOperationEditorData() const;
  Q_INVOKABLE bool
  setSelectedSequenceMessageTargetOperation(const QString &targetOperationId);
  Q_INVOKABLE QVariantMap sequenceActivationEditorData() const;
  Q_INVOKABLE bool
  createSequenceActivation(const QString &startMessageId,
                           const QString &endMessageId, const QString &label,
                           qreal startY, qreal endY,
                           const QString &executionContextId = {});
  Q_INVOKABLE bool updateSelectedSequenceActivation(
      const QString &startMessageId, const QString &endMessageId,
      const QString &label, qreal startY, qreal endY,
      const QString &executionContextId = {});
  Q_INVOKABLE QVariantMap sequenceCombinedFragmentEditorData() const;
  Q_INVOKABLE bool createSequenceCombinedFragment(const QString &type,
                                                  const QString &startMessageId,
                                                  const QString &endMessageId,
                                                  const QVariantList &operands);
  Q_INVOKABLE bool updateSelectedSequenceCombinedFragment(
      const QString &type, const QString &startMessageId,
      const QString &endMessageId, const QVariantList &operands);
  Q_INVOKABLE void editSelectedNote();
  Q_INVOKABLE bool startSelectedNoteAttachment();
  Q_INVOKABLE void removeSelectedNoteAttachments();
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
  Q_INVOKABLE void routeSelectedConnectorsAroundObstacles();
  Q_INVOKABLE void routeVisibleConnectorsAroundObstacles();
  Q_INVOKABLE void routeSelectedContainerConnectorsAroundObstacles();
  Q_INVOKABLE void optimizeSelectedConnectorEndsAndRoute();
  Q_INVOKABLE void optimizeVisibleConnectorEndsAndRoute();
  Q_INVOKABLE void optimizeSelectedContainerConnectorEndsAndRoute(
      bool collapseTargetEndpointsByRelationshipType);
  Q_INVOKABLE int connectorOptimizationScopeCount(const QString &scope) const;
  Q_INVOKABLE void optimizeConnectors(const QString &scope,
                                      const QVariantMap &options);
  Q_INVOKABLE void reattachSelectedConnectorEnds(const QString &side);
  Q_INVOKABLE void reattachSelectedConnectorEndsToFacingSides();
  Q_INVOKABLE void shiftSelectedConnectorEnds(const QString &direction);
  Q_INVOKABLE void setSelectedPortSnapPoints(int horizontalPointCount,
                                             int verticalPointCount);
  Q_INVOKABLE void setDiagramCompartmentVisible(const QString &compartment,
                                                bool visible);
  Q_INVOKABLE void setDiagramOperationSignatureMode(const QString &mode);
  Q_INVOKABLE void setSequenceTargetOperationDisplayMode(const QString &mode);
  Q_INVOKABLE void setSequenceMessageStereotypesVisible(bool visible);
  Q_INVOKABLE void setSelectedCompartmentVisibility(const QString &compartment,
                                                    const QString &visibility);
  Q_INVOKABLE void setSelectedOperationSignatureMode(const QString &mode);
  Q_INVOKABLE void cycleSelectedOperationSignatureMode();
  Q_INVOKABLE void addRelatedTypes(const QString &direction);
  Q_INVOKABLE void fitSelectionToContent();
  Q_INVOKABLE void wrapSelectionInPackage();
  Q_INVOKABLE void assignStyleToSelection(const QString &styleId);
  Q_INVOKABLE void arrangeSelection(const QString &operation);
  Q_INVOKABLE bool previewAutomaticLayout(const QString &scope,
                                          const QString &direction);
  Q_INVOKABLE bool
  previewAutomaticLayoutWithOptions(const QString &scope,
                                    const QVariantMap &options);
  Q_INVOKABLE void applyAutomaticLayoutPreview();
  Q_INVOKABLE void applyAutomaticLayoutPreviewWithConnectorHandling(
      const QString &connectorHandling,
      const QVariantMap &connectorOptimizationOptions);
  Q_INVOKABLE void cancelAutomaticLayoutPreview();
  Q_INVOKABLE void nudgeSelection(qreal deltaX, qreal deltaY);
  Q_INVOKABLE void removeSelectedPresentations();
  Q_INVOKABLE QString
  transferSelectedPresentations(const QString &targetDiagramId, bool move,
                                const QString &newDiagramName = {});
  Q_INVOKABLE void deleteSelectedConnector();
  Q_INVOKABLE void deleteSelectedSequenceItems();
  Q_INVOKABLE void editSelectedSequenceItem();
  Q_INVOKABLE void clearCanvasSelection();
  Q_INVOKABLE void refreshTheme();
  Q_INVOKABLE bool beginToolboxRelationship(const QString &relationshipType,
                                            const QString &sourceNodeId,
                                            qreal sourceSceneX,
                                            qreal sourceSceneY,
                                            qreal pressViewX, qreal pressViewY);
  Q_INVOKABLE void updateToolboxRelationship(qreal viewX, qreal viewY,
                                             bool suppressSnapping = false);
  Q_INVOKABLE void finishToolboxRelationship(qreal viewX, qreal viewY,
                                             bool suppressSnapping = false);

signals:
  void projectChanged();
  void workspaceChanged();
  void diagramIdChanged();
  void viewportChanged();
  void diagramFilterChanged();
  void canvasSelectionChanged();
  void automaticLayoutPreviewChanged();
  void defaultDistributionGapChanged();
  void snapToGridEnabledChanged();
  void alignmentGuidesEnabledChanged();
  void gridSpacingChanged();
  void diagramItemSizingModeChanged();
  void typeIconsVisibleChanged();
  void typeIconSourcesChanged();
  void sourceEditorDoubleClickEnabledChanged();
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
  void connectionDestinationRequested(qreal x, qreal y,
                                      const QString &relationshipType);
  void editRequested(const QString &objectId, const QString &field, int index,
                     const QString &text, qreal x, qreal y, qreal width,
                     qreal height, qreal fontPixelSize, bool fontBold);
  void stereotypeEditRequested(const QString &objectId,
                               const QString &objectKind, qreal x, qreal y,
                               qreal width, qreal height);
  void sourceNavigationRequested(const QString &objectId, int operationIndex);
  void noteEditRequested(const QString &noteId, const QString &text, qreal x,
                         qreal y, qreal width, qreal height,
                         qreal fontPixelSize);
  // The canvas owns gesture geometry; QML owns the message-details dialog.
  // Keeping that boundary explicit avoids introducing a QQuickItem per
  // lifeline merely to launch an editor.
  void sequenceMessageCreationRequested();

protected:
  QSGNode *updatePaintNode(QSGNode *oldNode,
                           UpdatePaintNodeData *updatePaintNodeData) override;
  void geometryChange(const QRectF &newGeometry,
                      const QRectF &oldGeometry) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseUngrabEvent() override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void hoverMoveEvent(QHoverEvent *event) override;
  void hoverLeaveEvent(QHoverEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  struct ConnectorRoutingOptions {
    qreal endpointClearance = 12.0;
    qreal obstacleClearance = 12.0;
    int maximumAddedSnapPoints = 8;
  };

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
    CreateConnector,
    CreateNoteAttachment,
    MoveSequenceParticipant,
    MoveSequenceMessage,
    ResizeSequenceActivationStart,
    ResizeSequenceActivationEnd,
    PendingSequenceMessage,
    CreateSequenceMessage,
    MoveActivityNode,
    CreateActivityFlow,
    LassoActivity
  };
  enum class ResizeHandle { None, TopLeft, TopRight, BottomLeft, BottomRight };
  enum class SequenceActivationResizeEdge { None, Start, End };
  struct ConnectorEndpoints {
    QPointF source;
    QPointF target;
    QVector<QPointF> bendPoints;
    ConnectorRouting routing = ConnectorRouting::Straight;
    ConnectorSide sourceSide = ConnectorSide::Automatic;
    ConnectorSide targetSide = ConnectorSide::Automatic;
    bool valid = false;
  };
  struct PendingConnectionDestination {
    QString sourceId;
    QPointF scenePoint;
    QString relationshipType;
    ConnectorAnchor sourceAnchor;
    ConnectorRouting routing = ConnectorRouting::Straight;
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
  bool isActivityDiagram() const;
  ui::ActivityDiagramLayout activityLayout() const;
  void activityMousePress(QMouseEvent &event);
  void activityMouseMove(QMouseEvent &event);
  void activityMouseRelease(QMouseEvent &event);
  void selectActivityItemAt(const QPointF &scenePoint);
  void selectAllActivityItems();
  void pruneActivitySelection();
  void updateActivityLassoSelection(const QPointF &scenePoint);
  bool handleActivityKey(QKeyEvent &event);
  QRectF activityNodeGeometry(const ActivityNode &node) const;
  QList<ui::DiagramNodeGeometry>
  activityNodeGeometries(bool selectionOnly) const;
  bool previewActivityLayout(const QString &scope,
                             const ui::AutomaticLayoutOptions &options);
  void commitActivityPositions(const QHash<QString, QRectF> &geometries,
                               const QString &description);
  void refreshActivityCanvas();
  void cancelActivityGesture(bool restoreLassoSelection = true);
  QPointF snapActivityPoint(QPointF point) const;
  bool nodePassesFilter(const NodePresentation &node) const;
  bool connectorPassesFilter(const ConnectorPresentation &connector) const;
  bool connectorPassesFilter(const ConnectorPresentation &connector,
                             const ui::DiagramClipLayout &clipLayout) const;
  bool pruneFilteredSelection();
  QRectF nodeGeometry(const NodePresentation &node) const;
  QRectF containerGeometry(const ContainerPresentation &container) const;
  QRectF noteGeometry(const NotePresentation &note) const;
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
  const ui::SequenceMessageLayout *
  hitSequenceMessageAt(const ui::SequenceDiagramLayout &layout,
                       const QPointF &scenePoint) const;
  const NodePresentation *hitNode(const QPointF &scenePoint) const;
  const ContainerPresentation *hitContainer(const QPointF &scenePoint) const;
  const ContainerPresentation *
  hitContainer(const QPointF &scenePoint,
               const QSet<QString> &excludedPresentationIds) const;
  const ConnectorPresentation *hitConnector(const QPointF &scenePoint) const;
  const NotePresentation *hitNote(const QPointF &scenePoint) const;
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
  bool startConnectorGesture(const QString &relationshipType,
                             bool allowNodeBody = false);
  void requestConnectionDestination(PendingConnectionDestination destination);
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
  void updateNoteAttachmentGesture(const QPointF &scenePoint);
  void commitNoteAttachmentGesture(const QPointF &scenePoint);
  void cancelNoteAttachmentGesture();
  QString relationshipTypeForGestureKey(const QKeyEvent &event) const;
  void commitGeometryPreview();
  void updateLassoSelection(const QPointF &scenePoint);
  void finishLassoSelection();
  void cancelLassoSelection();
  void resetLassoState();
  void selectAllInContext();
  void synchronizeProjectSelection();
  QStringList selectedConnectorIdsInDiagramOrder() const;
  QStringList visibleConnectorIdsInDiagramOrder() const;
  QStringList
  visibleConnectorIdsInsideContainer(const QString &containerId) const;
  void routeConnectorsAroundObstacles(
      const QStringList &connectorIds,
      const ConnectorRoutingOptions &options = ConnectorRoutingOptions{});
  QStringList connectorIdsForOptimizationScope(const QString &scope) const;
  void optimizeConnectorEndsAndRoute(
      const QStringList &connectorIds, const QString &endpointMode,
      bool collapseTargetEndpointsByRelationshipType = false,
      bool preserveManualEndpoints = false,
      const ConnectorRoutingOptions &options = ConnectorRoutingOptions{});
  static ConnectorRoutingOptions
  connectorRoutingOptions(const QVariantMap &options);
  ui::OrthogonalObstacleRoutingRequest obstacleRoutingRequest(
      const NodePresentation &sourceNode, const NodePresentation &targetNode,
      const QPointF &source, const QPointF &target, ConnectorSide sourceSide,
      ConnectorSide targetSide, const QVector<QVector<QPointF>> &occupiedRoutes,
      const ConnectorRoutingOptions &options) const;
  QString selectedCompartmentVisibility(bool attributes) const;
  void captureSelectedGeometry();
  void collectContainerSubtreeGeometry(const Diagram &diagram,
                                       const ContainerPresentation &container,
                                       QHash<QString, QRectF> &geometries,
                                       QSet<QString> &visitedContainers) const;
  void clearContainerSelection();
  void normalizeActiveConnector();
  void selectOnlyContainer(const QString &containerId);
  void createElementAt(const QString &type, const QPointF &sceneCenter);
  void selectNode(const QString &nodeId, bool toggle);
  void selectConnector(const QString &connectorId, bool toggle);
  void selectNote(const QString &noteId, bool toggle);
  void selectSequenceParticipant(const QString &participantId, bool toggle);
  void selectSequenceMessage(const QString &messageId, bool toggle);
  void selectSequenceActivation(const QString &activationId, bool toggle);
  void selectSequenceCombinedFragment(const QString &fragmentId, bool toggle);
  SequenceActivationResizeEdge
  sequenceActivationResizeEdgeAt(const QRectF &activationRect,
                                 const QPointF &scenePoint) const;
  const SequenceMessage *nearestSequenceActivationBoundaryMessage(
      const Diagram &diagram, const SequenceActivation &activation, qreal y,
      qreal minimumY, qreal maximumY, qreal tolerance) const;
  void updateSequenceActivationResize(const QPointF &scenePoint,
                                      bool suppressSnapping);
  void commitSequenceActivationResize();
  void cancelSequenceActivationResize();
  void clearSequenceSelection();
  void requestNoteEdit(const NotePresentation &note);

  ProjectController *m_project = nullptr;
  WorkspaceController *m_workspace = nullptr;
  QString m_diagramId;
  qreal m_zoom = 1.0;
  QPointF m_pan = {30, 30};
  bool m_exportMode = false;
  QSet<QString> m_selectedNodes;
  // QSet provides cheap membership checks for rendering; this list preserves
  // click order so relationship source and target are deterministic.
  QStringList m_selectedNodeOrder;
  QSet<QString> m_selectedContainers;
  // A container is "active" only when it was selected on its own. Multi-item
  // selection still highlights containers but must not expose single-item
  // editing commands or redefine the Ctrl+A scope.
  QString m_selectedContainer;
  // Connector membership is independent from the active connector. Precise
  // endpoint, bend-point, and annotation editing remains active-only, while
  // selection-wide operations such as routing use the complete set.
  QSet<QString> m_selectedConnectors;
  QString m_selectedConnector;
  QSet<QString> m_selectedNotes;
  QString m_selectedNote;
  QSet<QString> m_selectedSequenceParticipants;
  // QSet enables efficient render-time membership checks, while this list
  // deliberately preserves selection order for directed sequence messages.
  QStringList m_selectedSequenceParticipantOrder;
  QString m_selectedSequenceParticipant;
  QSet<QString> m_selectedSequenceMessages;
  QString m_selectedSequenceMessage;
  QSet<QString> m_selectedSequenceActivations;
  QString m_selectedSequenceActivation;
  QSet<QString> m_selectedSequenceCombinedFragments;
  QString m_selectedSequenceCombinedFragment;
  QStringList m_selectedActivityItems;
  QStringList m_activityLassoBase;
  QString m_activityGestureNode;
  bool m_activityGestureMoved = false;
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
  QSet<QString> m_lassoBaseConnectors;
  QString m_lassoBaseConnector;
  QSet<QString> m_lassoBaseNotes;
  QString m_lassoBaseNote;
  // Empty-body presses remain potential lassos until the pointer crosses the
  // drag threshold. On a click-release this ID becomes the active container.
  QString m_lassoClickContainer;
  int m_lassoBaseBendPoint = -1;
  Qt::KeyboardModifiers m_lassoModifiers = Qt::NoModifier;
  bool m_lassoActive = false;
  QString m_interactionNode;
  QString m_interactionSequenceItem;
  qreal m_sequenceOriginalCoordinate = 0.0;
  qreal m_sequencePreviewCoordinate = 0.0;
  qreal m_sequenceActivationOriginalStartY = 0.0;
  qreal m_sequenceActivationOriginalEndY = 0.0;
  qreal m_sequenceActivationPreviewStartY = 0.0;
  qreal m_sequenceActivationPreviewEndY = 0.0;
  QString m_sequenceGestureSourceParticipant;
  QString m_sequenceGestureTargetParticipant;
  QString m_sequenceGestureSourceActivation;
  // A message-kind key pressed during the drag selects quick creation. Empty
  // retains the full details dialog on release.
  QString m_sequenceGestureMessageType;
  QPointF m_sequenceGesturePointer;
  qreal m_sequenceGestureY = 0.0;
  // These survive after mouse release while the modal message editor is open.
  // A negative Y means the older two-selected-participants command path.
  QString m_pendingSequenceMessageSource;
  QString m_pendingSequenceMessageTarget;
  QString m_pendingSequenceMessageSourceActivation;
  qreal m_pendingSequenceMessageY = -1.0;
  ResizeHandle m_resizeHandle = ResizeHandle::None;
  QHash<QString, QRectF> m_originalGeometry;
  QHash<QString, QRectF> m_previewGeometry;
  bool m_automaticLayoutPreviewActive = false;
  int m_automaticLayoutPreviewCount = 0;
  QStringList m_automaticLayoutConnectorIds;
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
  QPointF m_toolboxRelationshipPressView;
  bool m_toolboxRelationshipDragged = false;
  bool m_toolboxRelationshipArmed = false;
  std::optional<PendingConnectionDestination> m_connectionDestination;
  QString m_noteAttachmentSourceNote;
  QString m_noteAttachmentTargetPresentation;
  QPointF m_noteAttachmentTargetPoint;
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
  bool m_typeIconsVisible = false;
  QVariantMap m_typeIconSources;
  bool m_sourceEditorDoubleClickEnabled = false;
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
  QPointF m_connectorToolboxSceneAnchor;
  bool m_presentationToolboxCandidate = false;
  QString m_presentationToolboxPresentationId;
  QString m_presentationToolboxKind;
  QPointF m_presentationToolboxViewAnchor;
  QVector<QLineF> m_alignmentGuides;
  quint64 m_themeRevision = 0;
};

} // namespace yauml
