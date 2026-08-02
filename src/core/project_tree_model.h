#pragma once

#include <QAbstractItemModel>
#include <QAbstractListModel>
#include <QHash>
#include <QItemSelectionModel>
#include <QPersistentModelIndex>
#include <QRegularExpression>
#include <QSet>
#include <QVariant>
#include <memory>
#include <vector>

namespace yauml {

class ProjectController;

class ProjectTreeModel final : public QAbstractItemModel {
  Q_OBJECT
  Q_PROPERTY(QString filterPattern READ filterPattern WRITE setFilterPattern
                 NOTIFY filterPatternChanged)
  Q_PROPERTY(
      QStringList columns READ columns WRITE setColumns NOTIFY columnsChanged)
  Q_PROPERTY(bool relationshipsVisible READ relationshipsVisible WRITE
                 setRelationshipsVisible NOTIFY relationshipsVisibleChanged)
public:
  enum Role {
    IdRole = Qt::UserRole + 1,
    KindRole,
    TypeRole,
    NestedRole,
    NameRole,
    SourcePathRole,
    SourceDirectoryRole,
    SourceFileRole,
    StereotypesRole,
    QualifiedNameRole
  };
  Q_ENUM(Role)

  explicit ProjectTreeModel(ProjectController *controller);
  ~ProjectTreeModel() override;
  QModelIndex index(int row, int column,
                    const QModelIndex &parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &child) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;
  Q_INVOKABLE QModelIndex indexForObject(const QString &objectId,
                                         const QString &kind) const;
  Q_INVOKABLE QVariantList findMatches(const QString &pattern) const;
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
  QString filterPattern() const;
  void setFilterPattern(const QString &pattern);
  QStringList columns() const;
  void setColumns(const QStringList &columns);
  bool relationshipsVisible() const;
  void setRelationshipsVisible(bool visible);

public slots:
  void reset();

signals:
  void filterPatternChanged();
  void columnsChanged();
  void relationshipsVisibleChanged();

private:
  struct TreeNode;

  TreeNode *createNode();
  QModelIndex indexForNode(const TreeNode *node) const;
  TreeNode *nodeForIndex(const QModelIndex &index) const;
  void rebuildTree();
  void rebuildVisibleTree();
  bool updateVisibleChildren(TreeNode *node,
                             const QRegularExpression &expression,
                             bool filtering);
  bool nodeMatches(const TreeNode *node,
                   const QRegularExpression &expression) const;
  bool findNodeMatches(const TreeNode *node,
                       const QRegularExpression &expression,
                       const QRegularExpression &qualifiedPathExpression) const;
  void collectElementIds(const TreeNode *node, QSet<QString> &ids) const;

  ProjectController *m_controller;
  QString m_filterPattern;
  QStringList m_columns{QStringLiteral("name")};
  bool m_relationshipsVisible = true;
  QPersistentModelIndex m_selectionAnchor;
  std::vector<std::unique_ptr<TreeNode>> m_nodes;
  TreeNode *m_invisibleRoot = nullptr;
  TreeNode *m_modelRoot = nullptr;
  TreeNode *m_diagramRoot = nullptr;
  QHash<QString, TreeNode *> m_elementNodes;
  QHash<QString, TreeNode *> m_folderNodes;
  QHash<QString, TreeNode *> m_namespaceNodes;
  QHash<QString, TreeNode *> m_diagramNodes;
  QHash<QString, TreeNode *> m_relationshipNodes;
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

} // namespace yauml
