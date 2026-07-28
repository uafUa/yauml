#pragma once

#include "core/diagnostic_model.h"
#include "core/project_data.h"
#include "core/project_serializer.h"
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
  Q_PROPERTY(QString selectedSourceRole READ selectedSourceRole WRITE
                 setSelectedSourceRole NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedSourceMultiplicity READ selectedSourceMultiplicity
                 WRITE setSelectedSourceMultiplicity NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedTargetRole READ selectedTargetRole WRITE
                 setSelectedTargetRole NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedTargetMultiplicity READ selectedTargetMultiplicity
                 WRITE setSelectedTargetMultiplicity NOTIFY selectionChanged)
  Q_PROPERTY(QString selectedStereotypes READ selectedStereotypes NOTIFY
                 selectionChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
  Q_PROPERTY(QString undoText READ undoText NOTIFY undoStateChanged)
  Q_PROPERTY(QString redoText READ redoText NOTIFY undoStateChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
  Q_PROPERTY(QStringList externallyChangedProjectFiles READ
                 externallyChangedProjectFiles NOTIFY
                 externallyChangedProjectFilesChanged)
  Q_PROPERTY(QVariantList diagramStyles READ diagramStyles NOTIFY stateChanged)
  Q_PROPERTY(
      QVariantList stereotypeCatalog READ stereotypeCatalog NOTIFY stateChanged)

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
  QString selectedSourceRole() const;
  QString selectedSourceMultiplicity() const;
  QString selectedTargetRole() const;
  QString selectedTargetMultiplicity() const;
  QString selectedStereotypes() const;
  bool canUndo() const;
  bool canRedo() const;
  QString undoText() const;
  QString redoText() const;
  bool dirty() const;
  QStringList externallyChangedProjectFiles() const;
  QVariantList diagramStyles() const;
  QVariantList stereotypeCatalog() const;

  Q_INVOKABLE void
  newProject(const QString &name = QStringLiteral("New Project"));
  Q_INVOKABLE bool openProject(const QUrl &url);
  Q_INVOKABLE bool saveDestinationContainsProject(const QUrl &url) const;
  Q_INVOKABLE bool saveProject(const QUrl &url = {},
                               bool overwriteExisting = false);
  Q_INVOKABLE bool overwriteExternallyChangedProject();
  Q_INVOKABLE bool reloadProjectFromDisk();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE QVariantMap diagramStyle(const QString &styleId) const;
  Q_INVOKABLE QString saveDiagramStyle(const QString &styleId,
                                       const QString &name,
                                       const QVariantMap &colors);
  Q_INVOKABLE bool deleteDiagramStyle(const QString &styleId);
  Q_INVOKABLE int diagramStyleAssignmentCount(const QString &styleId) const;
  Q_INVOKABLE QString explicitStyleIdForBrowserSubject(
      const QString &kind, const QString &subjectId) const;
  Q_INVOKABLE void assignStyleToBrowserSubject(const QString &kind,
                                               const QString &subjectId,
                                               const QString &styleId);
  Q_INVOKABLE QString explicitStyleIdForPresentation(
      const QString &diagramId, const QString &presentationId) const;
  Q_INVOKABLE void
  assignStyleToPresentations(const QString &diagramId,
                             const QStringList &presentationIds,
                             const QString &styleId);
  Q_INVOKABLE QVariantMap
  stereotypeDefinition(const QString &stereotypeId) const;
  Q_INVOKABLE QString saveProjectStereotype(const QString &stereotypeId,
                                            const QString &name,
                                            const QStringList &applicableTo);
  Q_INVOKABLE bool deleteProjectStereotype(const QString &stereotypeId);
  Q_INVOKABLE int stereotypeAssignmentCount(const QString &stereotypeId) const;
  Q_INVOKABLE QVariantList
  applicableStereotypes(const QString &kind, const QString &subjectId) const;
  Q_INVOKABLE QStringList
  stereotypeIdsForObject(const QString &kind, const QString &subjectId) const;
  Q_INVOKABLE void assignStereotypes(const QString &kind,
                                     const QString &subjectId,
                                     const QStringList &stereotypeIds);

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
  Q_INVOKABLE bool canReorderBrowserItem(const QString &kind, const QString &id,
                                         int direction) const;
  Q_INVOKABLE bool reorderBrowserItem(const QString &kind, const QString &id,
                                      int direction);
  Q_INVOKABLE bool canReorderBrowserItemsAround(const QString &itemsJson,
                                                const QString &targetKind,
                                                const QString &targetId) const;
  Q_INVOKABLE bool reorderBrowserItemsAround(const QString &itemsJson,
                                             const QString &targetKind,
                                             const QString &targetId,
                                             bool before);
  Q_INVOKABLE bool moveBrowserItems(const QString &itemsJson,
                                    const QString &targetKind,
                                    const QString &targetId);
  Q_INVOKABLE QString browserMoveSemanticChangeSummary(
      const QString &itemsJson, const QString &targetKind,
      const QString &targetId) const;
  Q_INVOKABLE bool
  moveBrowserItemsWithSemanticReassignment(const QString &itemsJson,
                                           const QString &targetKind,
                                           const QString &targetId);
  // Compatibility aliases for integrations written before nested-type
  // reassignment joined the same policy and confirmation workflow.
  Q_INVOKABLE QString browserMovePackageChangeSummary(
      const QString &itemsJson, const QString &targetKind,
      const QString &targetId) const;
  Q_INVOKABLE bool
  moveBrowserItemsWithPackageReassignment(const QString &itemsJson,
                                          const QString &targetKind,
                                          const QString &targetId);
  Q_INVOKABLE void
  addSelectedToDiagram(const QString &diagramId,
                       const QString &sizingMode = QStringLiteral("content"));
  Q_INVOKABLE int
  addElementsToDiagram(const QString &diagramId, const QStringList &elementIds,
                       qreal x, qreal y,
                       const QString &sizingMode = QStringLiteral("content"));
  int relatedElementCountForDiagram(const QString &diagramId,
                                    const QString &nodeId,
                                    const QString &direction) const;
  int addRelatedElementsToDiagram(
      const QString &diagramId, const QString &nodeId, const QString &direction,
      const QString &sizingMode = QStringLiteral("content"));
  Q_INVOKABLE int
  addTreeItemsToDiagram(const QString &diagramId, const QStringList &elementIds,
                        const QString &subjectsJson, qreal x, qreal y,
                        const QString &sizingMode = QStringLiteral("content"));
  Q_INVOKABLE int addEmptyPackageToDiagram(const QString &diagramId,
                                           const QString &packageId);
  bool canWrapPresentationInPackage(const QString &diagramId,
                                    const QString &presentationId) const;
  bool wrapPresentationInPackage(const QString &diagramId,
                                 const QString &presentationId);
  Q_INVOKABLE void removePresentations(const QString &diagramId,
                                       const QStringList &nodeIds);
  Q_INVOKABLE void removeContainerPresentation(const QString &diagramId,
                                               const QString &containerId);
  void removeDiagramPresentations(const QString &diagramId,
                                  const QStringList &nodeIds,
                                  const QStringList &containerIds);
  Q_INVOKABLE void deleteSelected();
  Q_INVOKABLE void deleteDiagram(const QString &diagramId);
  Q_INVOKABLE void deleteRelationship(const QString &relationshipId);
  Q_INVOKABLE void deleteElement(const QString &elementId);
  Q_INVOKABLE void selectObject(const QString &id, const QString &kind);
  Q_INVOKABLE void clearSelection();

  Q_INVOKABLE QString diagramName(const QString &diagramId) const;
  Q_INVOKABLE void renameDiagram(const QString &diagramId, const QString &name);
  Q_INVOKABLE QVariantMap diagramFilter(const QString &diagramId) const;
  Q_INVOKABLE bool setDiagramFilter(const QString &diagramId,
                                    const QVariantMap &filter);
  Q_INVOKABLE void updateNodeGeometry(const QString &diagramId,
                                      const QString &nodeId, qreal x, qreal y,
                                      qreal width, qreal height);
  Q_INVOKABLE void updateNodeGeometries(const QString &diagramId,
                                        const QVariantList &geometries);
  Q_INVOKABLE void setNodePortSnapPoints(const QString &diagramId,
                                         const QString &nodeId,
                                         int horizontalPointCount,
                                         int verticalPointCount);
  Q_INVOKABLE void setDiagramCompartmentVisible(const QString &diagramId,
                                                const QString &compartment,
                                                bool visible);
  Q_INVOKABLE void setNodeCompartmentVisibility(const QString &diagramId,
                                                const QString &nodeId,
                                                const QString &compartment,
                                                const QString &visibility);
  Q_INVOKABLE void setNodesCompartmentVisibility(
      const QString &diagramId, const QStringList &nodeIds,
      const QString &compartment, const QString &visibility);
  void updatePresentationGeometries(const QString &diagramId,
                                    const QVariantList &geometries,
                                    const QString &description);
  // Commits a completed diagram drag as one command. An empty target ID moves
  // the presentations to the diagram root; resize and arrangement operations
  // use updatePresentationGeometries() and therefore leave membership intact.
  bool canMovePresentationsToContainer(const QString &diagramId,
                                       const QStringList &movedPresentationIds,
                                       const QString &targetContainerId) const;
  void movePresentationsToContainer(const QString &diagramId,
                                    const QVariantList &geometries,
                                    const QStringList &movedPresentationIds,
                                    const QString &targetContainerId,
                                    const QString &description);
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
  Q_INVOKABLE void setConnectorAnnotationPlacement(const QString &diagramId,
                                                   const QString &connectorId,
                                                   const QString &annotationKey,
                                                   qreal routePosition,
                                                   qreal tangentOffset,
                                                   qreal normalOffset);
  Q_INVOKABLE void
  resetConnectorAnnotationPlacement(const QString &diagramId,
                                    const QString &connectorId,
                                    const QString &annotationKey);
  Q_INVOKABLE void
  resetConnectorAnnotationPlacements(const QString &diagramId,
                                     const QString &connectorId);
  Q_INVOKABLE void editText(const QString &objectId, const QString &field,
                            int index, const QString &value);

public slots:
  void setSelectedName(const QString &name);
  void setSelectedAttributes(const QString &value);
  void setSelectedOperations(const QString &value);
  void setSelectedLiterals(const QString &value);
  void setSelectedSourceRole(const QString &value);
  void setSelectedSourceMultiplicity(const QString &value);
  void setSelectedTargetRole(const QString &value);
  void setSelectedTargetMultiplicity(const QString &value);

signals:
  void stateChanged();
  void diagramsChanged();
  void projectChanged();
  void projectOpened(const QString &projectPath);
  void selectionChanged();
  void undoStateChanged();
  void dirtyChanged();
  void externallyChangedProjectFilesChanged();
  void externalProjectChangeDetected();

private:
  bool moveBrowserItemsImpl(const QString &itemsJson, const QString &targetKind,
                            const QString &targetId, bool reassignPackage);
  friend class ProjectCommand;
  void pushCommand(std::unique_ptr<ProjectCommand> command);
  void applyCommand(ProjectCommand &command, bool execute);
  void setDataDirect(const ProjectData &state);
  void logDiagnostics(const QList<Diagnostic> &items);
  void setExternallyChangedProjectFiles(const QStringList &files,
                                        bool reportConflict);
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
                            const QString &description);
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
  ProjectFileRevision m_projectRevision;
  QStringList m_externallyChangedProjectFiles;
  QString m_selectedId;
  QString m_selectedKind;
  QUndoStack m_undoStack;
  DiagnosticModel m_diagnostics;
  ProjectTreeModel *m_treeModel;
  DiagramListModel *m_diagramModel;
};

} // namespace uuml
