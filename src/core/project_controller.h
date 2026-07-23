#pragma once

#include "core/diagnostic_model.h"
#include "core/project_data.h"
#include "core/project_tree_model.h"

#include <QObject>
#include <QUndoStack>
#include <QUrl>
#include <memory>
#include <optional>

namespace uuml {

class ProjectCommand;
struct CppImportPreview;

class ProjectController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(ProjectTreeModel *treeModel READ treeModel CONSTANT)
  Q_PROPERTY(DiagramListModel *diagramModel READ diagramModel CONSTANT)
  Q_PROPERTY(DiagnosticModel *diagnostics READ diagnostics CONSTANT)
  Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
  Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
  Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedKind READ selectedKind NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedType READ selectedType NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedName READ selectedName WRITE setSelectedName NOTIFY
                 selectionChanged)
  Q_PROPERTY(QString selectedAttributes READ selectedAttributes WRITE
                 setSelectedAttributes NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedOperations READ selectedOperations WRITE
                 setSelectedOperations NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedLiterals READ selectedLiterals WRITE
                 setSelectedLiterals NOTIFY selectionChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
  Q_PROPERTY(QString undoText READ undoText NOTIFY undoStateChanged)
  Q_PROPERTY(QString redoText READ redoText NOTIFY undoStateChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
  explicit ProjectController(QObject *parent = nullptr);
  ~ProjectController() override;

  ProjectTreeModel *treeModel() const;
  DiagramListModel *diagramModel() const;
  DiagnosticModel *diagnostics();
  const ProjectData &data() const;

  QString projectName() const;
  QString projectPath() const;
  QString selectedId() const;
  QString selectedKind() const;
  QString selectedType() const;
  QString selectedName() const;
  QString selectedAttributes() const;
  QString selectedOperations() const;
  QString selectedLiterals() const;
  bool canUndo() const;
  bool canRedo() const;
  QString undoText() const;
  QString redoText() const;
  bool dirty() const;

  Q_INVOKABLE void
  newProject(const QString &name = QStringLiteral("New Project"));
  Q_INVOKABLE bool openProject(const QUrl &url);
  Q_INVOKABLE bool saveDestinationContainsProject(const QUrl &url) const;
  Q_INVOKABLE bool saveProject(const QUrl &url = {},
                               bool overwriteExisting = false);
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();

  // Applies a previously discovered C++ plan as one compact undo command.
  // Callers re-plan immediately before invoking this method so user edits made
  // while a preview was open are never overwritten by stale state.
  int applyCppImportPlan(const CppImportPreview &preview);

  Q_INVOKABLE QString addElement(const QString &type,
                                 const QString &diagramId = {});
  Q_INVOKABLE QString addElementAt(const QString &type,
                                   const QString &diagramId, qreal x, qreal y);
  QString addElementCenteredAt(const QString &type, const QString &diagramId,
                               qreal centerX, qreal centerY);
  Q_INVOKABLE QString addDiagram();
  Q_INVOKABLE QString addBrowserFolder(const QString &parentKind,
                                       const QString &parentId,
                                       const QString &name);
  Q_INVOKABLE void renameBrowserFolder(const QString &folderId,
                                       const QString &name);
  Q_INVOKABLE void deleteBrowserFolder(const QString &folderId);
  Q_INVOKABLE void deleteBrowserItems(const QString &itemsJson);
  Q_INVOKABLE bool canReorderBrowserItem(const QString &kind,
                                         const QString &id,
                                         int direction) const;
  Q_INVOKABLE bool reorderBrowserItem(const QString &kind, const QString &id,
                                      int direction);
  Q_INVOKABLE bool canReorderBrowserItemsAround(
      const QString &itemsJson, const QString &targetKind,
      const QString &targetId) const;
  Q_INVOKABLE bool reorderBrowserItemsAround(
      const QString &itemsJson, const QString &targetKind,
      const QString &targetId, bool before);
  Q_INVOKABLE bool moveBrowserItems(const QString &itemsJson,
                                    const QString &targetKind,
                                    const QString &targetId);
  Q_INVOKABLE QString
  browserMovePackageChangeSummary(const QString &itemsJson,
                                  const QString &targetKind,
                                  const QString &targetId) const;
  Q_INVOKABLE bool
  moveBrowserItemsWithPackageReassignment(const QString &itemsJson,
                                          const QString &targetKind,
                                          const QString &targetId);
  Q_INVOKABLE void
  addSelectedToDiagram(const QString &diagramId,
                       const QString &sizingMode = QStringLiteral("content"));
  Q_INVOKABLE int addElementsToDiagram(const QString &diagramId,
                                       const QStringList &elementIds, qreal x,
                                       qreal y,
                                       const QString &sizingMode =
                                           QStringLiteral("content"));
  Q_INVOKABLE int addTreeItemsToDiagram(const QString &diagramId,
                                        const QStringList &elementIds,
                                        const QString &subjectsJson, qreal x,
                                        qreal y,
                                        const QString &sizingMode =
                                            QStringLiteral("content"));
  Q_INVOKABLE void removePresentations(const QString &diagramId,
                                       const QStringList &nodeIds);
  Q_INVOKABLE void removeContainerPresentation(const QString &diagramId,
                                               const QString &containerId);
  Q_INVOKABLE void deleteSelected();
  Q_INVOKABLE void deleteDiagram(const QString &diagramId);
  Q_INVOKABLE void deleteRelationship(const QString &relationshipId);
  Q_INVOKABLE void deleteElement(const QString &elementId);
  Q_INVOKABLE void selectObject(const QString &id, const QString &kind);
  Q_INVOKABLE void clearSelection();

  Q_INVOKABLE QString diagramName(const QString &diagramId) const;
  Q_INVOKABLE void renameDiagram(const QString &diagramId, const QString &name);
  Q_INVOKABLE void updateNodeGeometry(const QString &diagramId,
                                      const QString &nodeId, qreal x, qreal y,
                                      qreal width, qreal height);
  Q_INVOKABLE void updateNodeGeometries(const QString &diagramId,
                                        const QVariantList &geometries);
  Q_INVOKABLE void setNodePortSnapPoints(const QString &diagramId,
                                         const QString &nodeId,
                                         int horizontalPointCount,
                                         int verticalPointCount);
  void updatePresentationGeometries(const QString &diagramId,
                                    const QVariantList &geometries,
                                    const QString &description);
  // Commits a completed diagram drag as one command. An empty target ID moves
  // the presentations to the diagram root; resize and arrangement operations
  // use updatePresentationGeometries() and therefore leave membership intact.
  void movePresentationsToContainer(const QString &diagramId,
                                    const QVariantList &geometries,
                                    const QStringList &movedPresentationIds,
                                    const QString &targetContainerId,
                                    const QString &description,
                                    bool reassignPackage = false);
  QString presentationMovePackageChangeSummary(
      const QString &diagramId, const QStringList &movedPresentationIds,
      const QString &targetContainerId) const;
  Q_INVOKABLE QString createRelationship(const QString &diagramId,
                                         const QString &sourceNodeId,
                                         const QString &targetNodeId,
                                         const QString &type);
  Q_INVOKABLE QString createRelationshipWithRouting(const QString &diagramId,
                                                    const QString &sourceNodeId,
                                                    const QString &targetNodeId,
                                                    const QString &type,
                                                    const QString &routing);
  QString createRelationshipAtAnchors(
      const QString &diagramId, const QString &sourceNodeId,
      const QString &targetNodeId, const QString &type, const QString &routing,
      ConnectorAnchor sourceAnchor, ConnectorAnchor targetAnchor);
  Q_INVOKABLE void reconnectRelationship(const QString &diagramId,
                                         const QString &connectorId,
                                         const QString &nodeId,
                                         bool reconnectSource);
  void reconnectRelationshipAtAnchor(const QString &diagramId,
                                     const QString &connectorId,
                                     const QString &nodeId,
                                     bool reconnectSource,
                                     ConnectorAnchor anchor);
  Q_INVOKABLE void updateConnectorAnchor(const QString &diagramId,
                                         const QString &connectorId,
                                         bool source, const QString &side,
                                         qreal offset);
  Q_INVOKABLE void setConnectorRouting(const QString &diagramId,
                                       const QString &connectorId,
                                       const QString &routing);
  Q_INVOKABLE void insertConnectorBendPoint(const QString &diagramId,
                                            const QString &connectorId,
                                            int index, qreal x, qreal y);
  Q_INVOKABLE void moveConnectorBendPoint(const QString &diagramId,
                                          const QString &connectorId, int index,
                                          qreal x, qreal y);
  Q_INVOKABLE void removeConnectorBendPoint(const QString &diagramId,
                                            const QString &connectorId,
                                            int index);
  Q_INVOKABLE void clearConnectorBendPoints(const QString &diagramId,
                                            const QString &connectorId);
  Q_INVOKABLE void editText(const QString &objectId, const QString &field,
                            int index, const QString &value);

public slots:
  void setSelectedName(const QString &name);
  void setSelectedAttributes(const QString &value);
  void setSelectedOperations(const QString &value);
  void setSelectedLiterals(const QString &value);

signals:
  void stateChanged();
  void diagramsChanged();
  void projectChanged();
  void projectOpened(const QString &projectPath);
  void selectionChanged();
  void undoStateChanged();
  void dirtyChanged();

private:
  bool moveBrowserItemsImpl(const QString &itemsJson,
                            const QString &targetKind,
                            const QString &targetId,
                            bool reassignPackage);
  friend class ProjectCommand;
  void pushCommand(std::unique_ptr<ProjectCommand> command);
  void applyCommand(ProjectCommand &command, bool execute);
  void setDataDirect(const ProjectData &state);
  void logDiagnostics(const QList<Diagnostic> &items);
  QString normalizedLocalPath(const QUrl &url) const;
  ModelElement *selectedElement(ProjectData &project) const;
  const ModelElement *selectedElement() const;
  QString createRelationshipImpl(const QString &diagramId,
                                 const QString &sourceNodeId,
                                 const QString &targetNodeId,
                                 const QString &type, ConnectorRouting routing);
  QString addElementAtImpl(const QString &type, const QString &diagramId,
                           qreal x, qreal y, bool coordinatesAreCenter);
  void
  commitPresentationChanges(const QString &diagramId,
                            const QVariantList &geometries,
                            const QStringList &movedPresentationIds,
                            const std::optional<QString> &targetContainerId,
                            const QString &description,
                            bool reassignPackage = false);
  QString createRelationshipImpl(const QString &diagramId,
                                 const QString &sourceNodeId,
                                 const QString &targetNodeId,
                                 const QString &type, ConnectorRouting routing,
                                 std::optional<ConnectorAnchor> sourceAnchor,
                                 std::optional<ConnectorAnchor> targetAnchor);
  void updateConnectorBendPoints(const QString &diagramId,
                                 const QString &connectorId,
                                 QList<ConnectorBendPoint> bendPoints,
                                 const QString &description);

  ProjectData m_data;
  QString m_projectPath;
  QString m_selectedId;
  QString m_selectedKind;
  QUndoStack m_undoStack;
  DiagnosticModel m_diagnostics;
  ProjectTreeModel *m_treeModel;
  DiagramListModel *m_diagramModel;
};

} // namespace uuml
