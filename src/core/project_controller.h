#pragma once

#include "core/diagnostic_model.h"
#include "core/project_data.h"
#include "core/project_tree_model.h"

#include <QObject>
#include <QUndoStack>
#include <QUrl>
#include <memory>

namespace uuml {

class ProjectCommand;

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
  Q_INVOKABLE bool saveProject(const QUrl &url = {});
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();

  Q_INVOKABLE QString addElement(const QString &type,
                                 const QString &diagramId = {});
  Q_INVOKABLE QString addDiagram();
  Q_INVOKABLE void addSelectedToDiagram(const QString &diagramId);
  Q_INVOKABLE void removePresentations(const QString &diagramId,
                                       const QStringList &nodeIds);
  Q_INVOKABLE void deleteSelected();
  Q_INVOKABLE void selectObject(const QString &id, const QString &kind);
  Q_INVOKABLE void clearSelection();

  Q_INVOKABLE QString diagramName(const QString &diagramId) const;
  Q_INVOKABLE void renameDiagram(const QString &diagramId, const QString &name);
  Q_INVOKABLE void updateNodeGeometry(const QString &diagramId,
                                      const QString &nodeId, qreal x, qreal y,
                                      qreal width, qreal height);
  Q_INVOKABLE void updateNodeGeometries(const QString &diagramId,
                                        const QVariantList &geometries);
  Q_INVOKABLE QString createRelationship(const QString &diagramId,
                                         const QString &sourceNodeId,
                                         const QString &targetNodeId,
                                         const QString &type);
  Q_INVOKABLE void reconnectRelationship(const QString &diagramId,
                                         const QString &connectorId,
                                         const QString &nodeId,
                                         bool reconnectSource);
  Q_INVOKABLE void updateConnectorAnchor(const QString &diagramId,
                                         const QString &connectorId,
                                         bool source, const QString &side,
                                         qreal offset);
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
  void selectionChanged();
  void undoStateChanged();
  void dirtyChanged();

private:
  friend class ProjectCommand;
  void pushCommand(std::unique_ptr<ProjectCommand> command);
  void applyCommand(ProjectCommand &command, bool execute);
  void setDataDirect(const ProjectData &state);
  void logDiagnostics(const QList<Diagnostic> &items);
  QString normalizedLocalPath(const QUrl &url) const;
  ModelElement *selectedElement(ProjectData &project) const;
  const ModelElement *selectedElement() const;

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
