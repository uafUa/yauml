#pragma once

#include <QAbstractItemModel>
#include <QAbstractListModel>
#include <QHash>
#include <QItemSelectionModel>
#include <QPersistentModelIndex>
#include <QSet>
#include <memory>
#include <vector>

namespace uuml {

class ProjectController;

class ProjectTreeModel final : public QAbstractItemModel {
  Q_OBJECT
public:
  enum Role { IdRole = Qt::UserRole + 1, KindRole, TypeRole, NestedRole };
  Q_ENUM(Role)

  explicit ProjectTreeModel(ProjectController *controller);
  ~ProjectTreeModel() override;
  QModelIndex index(int row, int column,
                    const QModelIndex &parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &child) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  Q_INVOKABLE QModelIndex indexForObject(const QString &objectId,
                                         const QString &kind) const;
  Q_INVOKABLE QStringList
  elementIdsForIndexes(const QModelIndexList &indexes) const;
  Q_INVOKABLE QString
  browserItemsJsonForIndexes(const QModelIndexList &indexes) const;
  Q_INVOKABLE void selectFromPointer(QItemSelectionModel *selectionModel,
                                     const QModelIndex &item);
  void selectWithModifiers(QItemSelectionModel *selectionModel,
                           const QModelIndex &item,
                           Qt::KeyboardModifiers modifiers);
  Q_INVOKABLE void startTreeDrag(const QModelIndexList &indexes);

public slots:
  void reset();

private:
  struct TreeNode;

  TreeNode *createNode();
  QModelIndex indexForNode(const TreeNode *node) const;
  TreeNode *nodeForIndex(const QModelIndex &index) const;
  void rebuildTree();
  void collectElementIds(const TreeNode *node, QSet<QString> &ids) const;

  ProjectController *m_controller;
  QPersistentModelIndex m_selectionAnchor;
  std::vector<std::unique_ptr<TreeNode>> m_nodes;
  TreeNode *m_invisibleRoot = nullptr;
  TreeNode *m_modelRoot = nullptr;
  TreeNode *m_diagramRoot = nullptr;
  QHash<QString, TreeNode *> m_elementNodes;
  QHash<QString, TreeNode *> m_folderNodes;
  QHash<QString, TreeNode *> m_namespaceNodes;
  QHash<QString, TreeNode *> m_diagramNodes;
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
