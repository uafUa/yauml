#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QRect>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

namespace uuml {

class ProjectController;

class WorkspaceController final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString mainHostId READ mainHostId CONSTANT)
  Q_PROPERTY(
      QVariantList detachedHostIds READ detachedHostIds NOTIFY hostsChanged)
  Q_PROPERTY(
      QAbstractItemModel *detachedHostModel READ detachedHostModel CONSTANT)
  Q_PROPERTY(QString activeDiagramId READ activeDiagramId WRITE
                 setActiveDiagramId NOTIFY activeDiagramIdChanged)
  Q_PROPERTY(int revision READ revision NOTIFY hostsChanged)
  Q_PROPERTY(bool projectTreeVisible READ projectTreeVisible WRITE
                 setProjectTreeVisible NOTIFY uiSettingsChanged)
  Q_PROPERTY(bool propertiesVisible READ propertiesVisible WRITE
                 setPropertiesVisible NOTIFY uiSettingsChanged)
  Q_PROPERTY(
      int projectTreeWidth READ projectTreeWidth NOTIFY uiSettingsChanged)
  Q_PROPERTY(int propertiesWidth READ propertiesWidth NOTIFY uiSettingsChanged)
  Q_PROPERTY(int mainWindowX READ mainWindowX NOTIFY uiSettingsChanged)
  Q_PROPERTY(int mainWindowY READ mainWindowY NOTIFY uiSettingsChanged)
  Q_PROPERTY(int mainWindowWidth READ mainWindowWidth NOTIFY uiSettingsChanged)
  Q_PROPERTY(
      int mainWindowHeight READ mainWindowHeight NOTIFY uiSettingsChanged)

public:
  enum DetachedHostRole { WindowHostIdRole = Qt::UserRole + 1 };

  explicit WorkspaceController(ProjectController *project,
                               bool persistenceEnabled = false,
                               QObject *parent = nullptr);
  ~WorkspaceController() override;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QString mainHostId() const;
  QVariantList detachedHostIds() const;
  QAbstractItemModel *detachedHostModel();
  QString activeDiagramId() const;
  void setActiveDiagramId(const QString &diagramId);
  int revision() const;
  bool projectTreeVisible() const;
  void setProjectTreeVisible(bool visible);
  bool propertiesVisible() const;
  void setPropertiesVisible(bool visible);
  int projectTreeWidth() const;
  int propertiesWidth() const;
  int mainWindowX() const;
  int mainWindowY() const;
  int mainWindowWidth() const;
  int mainWindowHeight() const;

  Q_INVOKABLE QVariantList diagramIdsForHost(const QString &hostId) const;
  Q_INVOKABLE QString hostForDiagram(const QString &diagramId) const;
  Q_INVOKABLE QString detachDiagram(const QString &diagramId, int x = 120,
                                    int y = 120);
  Q_INVOKABLE QString detachDiagramAtCursor(const QString &diagramId);
  Q_INVOKABLE void startDiagramDrag(const QString &diagramId);
  Q_INVOKABLE void moveDiagram(const QString &diagramId,
                               const QString &targetHostId, int index = -1);
  Q_INVOKABLE void returnDiagramToMain(const QString &diagramId);
  Q_INVOKABLE void closeHost(const QString &hostId);
  Q_INVOKABLE void closeAllDetachedHosts();
  Q_INVOKABLE int hostX(const QString &hostId) const;
  Q_INVOKABLE int hostY(const QString &hostId) const;
  Q_INVOKABLE int hostWidth(const QString &hostId) const;
  Q_INVOKABLE int hostHeight(const QString &hostId) const;
  Q_INVOKABLE void updateHostGeometry(const QString &hostId, int x, int y,
                                      int width, int height);
  Q_INVOKABLE void updatePanelWidths(int projectTreeWidth, int propertiesWidth);
  Q_INVOKABLE void updateMainWindowGeometry(int x, int y, int width,
                                            int height);

public slots:
  void reconcile();

signals:
  void hostsChanged();
  void activeDiagramIdChanged();
  void uiSettingsChanged();
  void hostRelocationRequested(const QString &hostId, int x, int y);

private:
  QStringList projectDiagramIds() const;
  void removeFromCurrentHost(const QString &diagramId);
  void pruneEmptyHosts();
  void changed();
  void schedulePersistence();
  void persistWorkspace();
  void restoreWorkspace();
  void restoreUiSettings();

  ProjectController *m_project;
  QHash<QString, QStringList> m_hosts;
  QStringList m_detachedOrder;
  QHash<QString, QRect> m_geometries;
  QString m_activeDiagramId;
  QString m_workspaceProjectId;
  QTimer m_persistenceTimer;
  bool m_persistenceEnabled = false;
  bool m_restoring = false;
  bool m_projectTreeVisible = true;
  bool m_propertiesVisible = true;
  int m_projectTreeWidth = 260;
  int m_propertiesWidth = 310;
  QRect m_mainWindowGeometry = {80, 80, 1440, 900};
  int m_revision = 0;
};

} // namespace uuml
