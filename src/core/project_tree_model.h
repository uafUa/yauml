#pragma once

#include <QAbstractItemModel>
#include <QAbstractListModel>

namespace uuml {

class ProjectController;

class ProjectTreeModel final : public QAbstractItemModel {
  Q_OBJECT
public:
  enum Role { IdRole = Qt::UserRole + 1, KindRole, TypeRole };
  Q_ENUM(Role)

  explicit ProjectTreeModel(ProjectController *controller);
  QModelIndex index(int row, int column,
                    const QModelIndex &parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &child) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  Q_INVOKABLE QModelIndex indexForObject(const QString &objectId,
                                         const QString &kind) const;

public slots:
  void reset();

private:
  ProjectController *m_controller;
};

class DiagramListModel final : public QAbstractListModel {
  Q_OBJECT
public:
  enum Role { IdRole = Qt::UserRole + 1, NameRole };
  Q_ENUM(Role)

  explicit DiagramListModel(ProjectController *controller);
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

public slots:
  void reset();

private:
  ProjectController *m_controller;
};

} // namespace uuml
