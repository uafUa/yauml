#pragma once

#include "core/project_controller.h"

#include <QHash>
#include <QQuickItem>
#include <QSet>
#include <QStringList>

namespace uuml {

struct ConnectorPresentation;
struct Diagram;
struct NodePresentation;

class DiagramCanvas : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(ProjectController *project READ project WRITE setProject NOTIFY
                 projectChanged)
  Q_PROPERTY(QString diagramId READ diagramId WRITE setDiagramId NOTIFY
                 diagramIdChanged)
  Q_PROPERTY(qreal zoom READ zoom NOTIFY viewportChanged)
  Q_PROPERTY(int selectedNodeCount READ selectedNodeCount NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool connectorSelected READ connectorSelected NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool bendPointSelected READ bendPointSelected NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(bool selectedConnectorHasBendPoints READ
                 selectedConnectorHasBendPoints NOTIFY canvasSelectionChanged)
  Q_PROPERTY(QString reconnectPrompt READ reconnectPrompt NOTIFY
                 canvasSelectionChanged)
  Q_PROPERTY(int defaultDistributionGap READ defaultDistributionGap WRITE
                 setDefaultDistributionGap NOTIFY defaultDistributionGapChanged)

public:
  explicit DiagramCanvas(QQuickItem *parent = nullptr);

  ProjectController *project() const;
  void setProject(ProjectController *project);
  QString diagramId() const;
  void setDiagramId(const QString &diagramId);
  qreal zoom() const;
  int selectedNodeCount() const;
  bool connectorSelected() const;
  bool bendPointSelected() const;
  bool selectedConnectorHasBendPoints() const;
  QString reconnectPrompt() const;
  int defaultDistributionGap() const;
  void setDefaultDistributionGap(int gap);

  Q_INVOKABLE void fitToContent();
  Q_INVOKABLE void createElementAtContextPosition(const QString &type);
  Q_INVOKABLE void createElementAtViewportCenter(const QString &type);
  Q_INVOKABLE void createRelationship(const QString &type);
  Q_INVOKABLE void reconnectSource();
  Q_INVOKABLE void reconnectTarget();
  Q_INVOKABLE void cancelReconnect();
  Q_INVOKABLE void addBendPointAtContextPosition();
  Q_INVOKABLE void removeSelectedBendPoint();
  Q_INVOKABLE void clearSelectedConnectorBendPoints();
  Q_INVOKABLE void arrangeSelection(const QString &operation);
  Q_INVOKABLE void nudgeSelection(qreal deltaX, qreal deltaY);
  Q_INVOKABLE void removeSelectedPresentations();
  Q_INVOKABLE void deleteSelectedConnector();
  Q_INVOKABLE void clearCanvasSelection();
  Q_INVOKABLE void refreshTheme();

signals:
  void projectChanged();
  void diagramIdChanged();
  void viewportChanged();
  void canvasSelectionChanged();
  void defaultDistributionGapChanged();
  void contextMenuRequested(const QString &target, qreal x, qreal y);
  void editRequested(const QString &objectId, const QString &field, int index,
                     const QString &text, qreal x, qreal y, qreal width,
                     qreal height, qreal fontPixelSize, bool fontBold);

protected:
  QSGNode *updatePaintNode(QSGNode *oldNode,
                           UpdatePaintNodeData *updatePaintNodeData) override;
  void geometryChange(const QRectF &newGeometry,
                      const QRectF &oldGeometry) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
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
    MoveBendPoint
  };
  enum class ReconnectEndpoint { None, Source, Target };
  struct ConnectorEndpoints {
    QPointF source;
    QPointF target;
    QVector<QPointF> bendPoints;
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

  const Diagram *diagram() const;
  QRectF nodeGeometry(const NodePresentation &node) const;
  QPointF toScene(const QPointF &point) const;
  QPointF toView(const QPointF &point) const;
  QRectF toView(const QRectF &rect) const;
  const NodePresentation *hitNode(const QPointF &scenePoint) const;
  const ConnectorPresentation *hitConnector(const QPointF &scenePoint) const;
  TextHit hitText(const QPointF &scenePoint) const;
  QRectF textLineRect(const QRectF &nodeRect, int line) const;
  ConnectorEndpoints
  connectorEndpoints(const ConnectorPresentation &connector) const;
  QRectF endpointNodeRect(const ConnectorPresentation &connector,
                          bool source) const;
  bool hitSelectedPort(const QPointF &scenePoint, bool &source) const;
  int hitBendPoint(const ConnectorPresentation &connector,
                   const QPointF &scenePoint) const;
  int nearestConnectorSegment(const ConnectorPresentation &connector,
                              const QPointF &scenePoint) const;
  void updatePortPreview(const QPointF &scenePoint);
  void commitPortPreview();
  void updateBendPointPreview(const QPointF &scenePoint);
  void commitBendPointPreview();
  void commitGeometryPreview();
  void updateLassoSelection(const QPointF &scenePoint);
  void finishLassoSelection();
  void cancelLassoSelection();
  void resetLassoState();
  void synchronizeProjectSelection();
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
  QString m_selectedConnector;
  ReconnectEndpoint m_reconnectEndpoint = ReconnectEndpoint::None;
  Interaction m_interaction = Interaction::None;
  QPointF m_pressView;
  QPointF m_pressScene;
  QPointF m_originalPan;
  QPointF m_lassoOrigin;
  QPointF m_contextScenePoint;
  QRectF m_lassoRect;
  QSet<QString> m_lassoBaseNodes;
  QStringList m_lassoBaseNodeOrder;
  QString m_lassoBaseConnector;
  int m_lassoBaseBendPoint = -1;
  ReconnectEndpoint m_lassoBaseReconnectEndpoint = ReconnectEndpoint::None;
  Qt::KeyboardModifiers m_lassoModifiers = Qt::NoModifier;
  bool m_lassoActive = false;
  QString m_interactionNode;
  QHash<QString, QRectF> m_originalGeometry;
  QHash<QString, QRectF> m_previewGeometry;
  ConnectorAnchor m_portPreview;
  bool m_portPreviewActive = false;
  QList<ConnectorBendPoint> m_bendPointPreview;
  int m_selectedBendPoint = -1;
  int m_contextSegment = -1;
  bool m_bendPointPreviewActive = false;
  bool m_sceneDirty = true;
  // Selection-only changes rebuild colored geometry but can retain the
  // expensive text atlases. Model and geometry changes set both dirty flags.
  bool m_textDirty = true;
  int m_defaultDistributionGap;
  quint64 m_themeRevision = 0;
};

} // namespace uuml
