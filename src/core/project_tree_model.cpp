#include "core/project_tree_model.h"

#include "core/project_controller.h"
#include "ui/ui_theme.h"

#include <QDrag>
#include <QGuiApplication>
#include <QItemSelection>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <algorithm>
#include <limits>

namespace uuml {

namespace {
constexpr auto kElementsMimeType = "application/x-uuml-element-ids";
constexpr auto kBrowserItemsMimeType = "application/x-uuml-browser-items";
constexpr auto kDiagramSubjectsMimeType = "application/x-uuml-diagram-subjects";
} // namespace

struct ProjectTreeModel::TreeNode {
  QString label;
  QString objectId;
  QString kind;
  QString objectType;
  QString qualifiedPath;
  TreeNode *parent = nullptr;
  std::vector<TreeNode *> children;
};

ProjectTreeModel::ProjectTreeModel(ProjectController *controller)
    : QAbstractItemModel(controller), m_controller(controller) {
  rebuildTree();
  connect(controller, &ProjectController::stateChanged, this,
          &ProjectTreeModel::reset);
}

ProjectTreeModel::~ProjectTreeModel() = default;

ProjectTreeModel::TreeNode *ProjectTreeModel::createNode() {
  m_nodes.push_back(std::make_unique<TreeNode>());
  return m_nodes.back().get();
}

QModelIndex ProjectTreeModel::indexForNode(const TreeNode *node) const {
  if (!node || !node->parent)
    return {};
  const auto &siblings = node->parent->children;
  const auto position = std::find(siblings.cbegin(), siblings.cend(), node);
  if (position == siblings.cend())
    return {};
  return createIndex(static_cast<int>(position - siblings.cbegin()), 0,
                     const_cast<TreeNode *>(node));
}

ProjectTreeModel::TreeNode *
ProjectTreeModel::nodeForIndex(const QModelIndex &item) const {
  return item.isValid() && item.model() == this
             ? static_cast<TreeNode *>(item.internalPointer())
             : nullptr;
}

void ProjectTreeModel::rebuildTree() {
  m_nodes.clear();
  m_elementNodes.clear();
  m_folderNodes.clear();
  m_namespaceNodes.clear();
  m_diagramNodes.clear();

  m_invisibleRoot = createNode();
  const auto attach = [](TreeNode *parent, TreeNode *child) {
    child->parent = parent;
    parent->children.push_back(child);
  };

  m_modelRoot = createNode();
  m_modelRoot->label = QStringLiteral("Model");
  m_modelRoot->objectId = QStringLiteral("model");
  m_modelRoot->kind = QStringLiteral("root");
  attach(m_invisibleRoot, m_modelRoot);

  m_diagramRoot = createNode();
  m_diagramRoot->label = QStringLiteral("Diagrams");
  m_diagramRoot->objectId = QStringLiteral("diagrams");
  m_diagramRoot->kind = QStringLiteral("root");
  attach(m_invisibleRoot, m_diagramRoot);

  QHash<QString, TreeNode *> elementsByQualifiedName;
  std::vector<TreeNode *> elementOrder;
  for (const auto &element : m_controller->data().elements) {
    TreeNode *node = createNode();
    node->label = element.name.section(QStringLiteral("::"), -1);
    if (node->label.isEmpty())
      node->label = element.name;
    node->objectId = element.id;
    node->kind = QStringLiteral("element");
    node->objectType = toString(element.type);
    node->qualifiedPath = element.name;
    m_elementNodes.insert(element.id, node);
    elementOrder.push_back(node);
    // Duplicate display names are legal user data. The first matching element
    // owns qualified descendants; later duplicates remain sibling leaves.
    if (!elementsByQualifiedName.contains(element.name))
      elementsByQualifiedName.insert(element.name, node);
  }

  QHash<TreeNode *, TreeNode *> desiredParents;
  std::vector<TreeNode *> namespaceOrder;
  const auto ensureNamespace = [&](const QString &qualifiedPath,
                                   const QString &label,
                                   TreeNode *parent) -> TreeNode * {
    if (TreeNode *existing = m_namespaceNodes.value(qualifiedPath, nullptr))
      return existing;
    TreeNode *node = createNode();
    node->label = label;
    node->objectId = qualifiedPath;
    node->kind = QStringLiteral("namespace");
    node->objectType = QStringLiteral("namespace");
    node->qualifiedPath = qualifiedPath;
    m_namespaceNodes.insert(qualifiedPath, node);
    desiredParents.insert(node, parent);
    namespaceOrder.push_back(node);
    return node;
  };

  const auto namespacePathNode = [&](const QString &path) -> TreeNode * {
    TreeNode *parent = m_modelRoot;
    QString qualifiedPath;
    const QStringList parts =
        path.split(QStringLiteral("::"), Qt::SkipEmptyParts);
    for (int index = 0; index < parts.size(); ++index) {
      if (!qualifiedPath.isEmpty())
        qualifiedPath += QStringLiteral("::");
      qualifiedPath += parts.at(index);
      if (TreeNode *existing = m_namespaceNodes.value(qualifiedPath, nullptr)) {
        parent = existing;
      } else if (index + 1 < parts.size()) {
        if (TreeNode *type =
                elementsByQualifiedName.value(qualifiedPath, nullptr))
          parent = type;
        else
          parent = ensureNamespace(qualifiedPath, parts.at(index), parent);
      } else {
        parent = ensureNamespace(qualifiedPath, parts.at(index), parent);
      }
    }
    return parent;
  };

  // Materialize namespace paths before attaching semantic elements so custom
  // folders can retain an otherwise-empty namespace as their stable parent.
  for (const auto &element : m_controller->data().elements) {
    TreeNode *parent = m_modelRoot;
    const QStringList parts =
        element.name.split(QStringLiteral("::"), Qt::SkipEmptyParts);
    QString qualifiedPath;
    for (int partIndex = 0; partIndex + 1 < parts.size(); ++partIndex) {
      if (!qualifiedPath.isEmpty())
        qualifiedPath += QStringLiteral("::");
      qualifiedPath += parts.at(partIndex);
      if (TreeNode *typeParent =
              elementsByQualifiedName.value(qualifiedPath, nullptr)) {
        parent = typeParent;
      } else {
        parent = ensureNamespace(qualifiedPath, parts.at(partIndex), parent);
      }
    }
  }

  std::vector<TreeNode *> folderOrder;
  for (const auto &folder : m_controller->data().browserFolders) {
    TreeNode *node = createNode();
    node->label = folder.name;
    node->objectId = folder.id;
    node->kind = QStringLiteral("folder");
    node->objectType = QStringLiteral("folder");
    m_folderNodes.insert(folder.id, node);
    folderOrder.push_back(node);
  }

  const auto explicitParentNode = [&](const BrowserParent &parent) {
    if (parent.kind == QStringLiteral("model"))
      return m_modelRoot;
    if (parent.kind == QStringLiteral("namespace"))
      return namespacePathNode(parent.id);
    if (parent.kind == QStringLiteral("element"))
      return m_elementNodes.value(parent.id, m_modelRoot);
    if (parent.kind == QStringLiteral("folder"))
      return m_folderNodes.value(parent.id, m_modelRoot);
    return m_modelRoot;
  };

  const auto semanticParentNode = [&](const ModelElement &element) {
    TreeNode *parent = m_modelRoot;
    const QStringList parts =
        element.name.split(QStringLiteral("::"), Qt::SkipEmptyParts);
    QString qualifiedPath;
    for (int partIndex = 0; partIndex + 1 < parts.size(); ++partIndex) {
      if (!qualifiedPath.isEmpty())
        qualifiedPath += QStringLiteral("::");
      qualifiedPath += parts.at(partIndex);
      if (TreeNode *typeParent =
              elementsByQualifiedName.value(qualifiedPath, nullptr))
        parent = typeParent;
      else
        parent = m_namespaceNodes.value(qualifiedPath, m_modelRoot);
    }
    return parent != m_modelRoot || element.packageId.isEmpty()
               ? parent
               : m_elementNodes.value(element.packageId, m_modelRoot);
  };

  for (const auto &folder : m_controller->data().browserFolders)
    desiredParents.insert(m_folderNodes.value(folder.id),
                          explicitParentNode(folder.parent));
  for (const auto &element : m_controller->data().elements) {
    TreeNode *parent = element.browserParent.kind.isEmpty()
                           ? semanticParentNode(element)
                           : explicitParentNode(element.browserParent);
    desiredParents.insert(m_elementNodes.value(element.id), parent);
  }

  // Validate the complete desired graph—including namespace nodes—before
  // assigning actual parent pointers. Malformed external files therefore
  // degrade to model-root placement rather than constructing a cyclic tree.
  const auto cycleSafeParent = [&](TreeNode *node) {
    TreeNode *parent = desiredParents.value(node, m_modelRoot);
    QSet<TreeNode *> visited{node};
    for (TreeNode *candidate = parent;
         candidate && candidate != m_modelRoot && candidate != m_invisibleRoot;
         candidate = desiredParents.value(candidate, m_modelRoot)) {
      if (visited.contains(candidate))
        return m_modelRoot;
      visited.insert(candidate);
    }
    return parent;
  };

  for (TreeNode *node : namespaceOrder)
    attach(cycleSafeParent(node), node);
  for (TreeNode *node : folderOrder)
    attach(cycleSafeParent(node), node);
  for (TreeNode *node : elementOrder)
    attach(cycleSafeParent(node), node);

  QHash<QString, int> browserOrderRank;
  for (const QString &key : m_controller->data().browserItemOrder)
    if (!browserOrderRank.contains(key))
      browserOrderRank.insert(key, browserOrderRank.size());
  const auto orderedKey = [](const TreeNode *node) {
    return node && (node->kind == QStringLiteral("element") ||
                    node->kind == QStringLiteral("folder") ||
                    node->kind == QStringLiteral("namespace"))
               ? node->kind + u':' + node->objectId
               : QString{};
  };
  const auto sortChildren = [&](const auto &self, TreeNode *parent) -> void {
    std::stable_sort(
        parent->children.begin(), parent->children.end(),
        [&](const TreeNode *left, const TreeNode *right) {
          const int unranked = std::numeric_limits<int>::max();
          return browserOrderRank.value(orderedKey(left), unranked) <
                 browserOrderRank.value(orderedKey(right), unranked);
        });
    for (TreeNode *child : parent->children)
      self(self, child);
  };
  sortChildren(sortChildren, m_modelRoot);

  for (const auto &diagram : m_controller->data().diagrams) {
    TreeNode *node = createNode();
    node->label = diagram.name;
    node->objectId = diagram.id;
    node->kind = QStringLiteral("diagram");
    node->objectType = QStringLiteral("class diagram");
    m_diagramNodes.insert(diagram.id, node);
    attach(m_diagramRoot, node);
  }
}

QModelIndex ProjectTreeModel::index(int row, int column,
                                    const QModelIndex &parentIndex) const {
  if (column != 0 || row < 0 || parentIndex.column() > 0)
    return {};
  TreeNode *parentNode =
      parentIndex.isValid() ? nodeForIndex(parentIndex) : m_invisibleRoot;
  if (!parentNode || row >= static_cast<int>(parentNode->children.size()))
    return {};
  return createIndex(row, column, parentNode->children.at(row));
}

QModelIndex ProjectTreeModel::parent(const QModelIndex &child) const {
  const TreeNode *node = nodeForIndex(child);
  if (!node || !node->parent || node->parent == m_invisibleRoot)
    return {};
  return indexForNode(node->parent);
}

int ProjectTreeModel::rowCount(const QModelIndex &parentIndex) const {
  if (parentIndex.column() > 0)
    return 0;
  const TreeNode *parentNode =
      parentIndex.isValid() ? nodeForIndex(parentIndex) : m_invisibleRoot;
  return parentNode ? static_cast<int>(parentNode->children.size()) : 0;
}

int ProjectTreeModel::columnCount(const QModelIndex &) const { return 1; }

QVariant ProjectTreeModel::data(const QModelIndex &item, int role) const {
  const TreeNode *node = nodeForIndex(item);
  if (!node)
    return {};
  if (role == Qt::DisplayRole)
    return node->label;
  if (role == IdRole)
    return node->objectId;
  if (role == KindRole)
    return node->kind;
  if (role == TypeRole)
    return node->objectType;
  return {};
}

QHash<int, QByteArray> ProjectTreeModel::roleNames() const {
  auto roles = QAbstractItemModel::roleNames();
  roles.insert(IdRole, "objectId");
  roles.insert(KindRole, "kind");
  roles.insert(TypeRole, "objectType");
  return roles;
}

QModelIndex ProjectTreeModel::indexForObject(const QString &objectId,
                                             const QString &kind) const {
  if (kind == QStringLiteral("element"))
    return indexForNode(m_elementNodes.value(objectId, nullptr));
  if (kind == QStringLiteral("folder"))
    return indexForNode(m_folderNodes.value(objectId, nullptr));
  if (kind == QStringLiteral("namespace"))
    return indexForNode(m_namespaceNodes.value(objectId, nullptr));
  if (kind == QStringLiteral("diagram"))
    return indexForNode(m_diagramNodes.value(objectId, nullptr));
  return {};
}

void ProjectTreeModel::collectElementIds(const TreeNode *node,
                                         QSet<QString> &ids) const {
  if (!node)
    return;
  if (node->kind == QStringLiteral("element"))
    ids.insert(node->objectId);
  for (const TreeNode *child : node->children)
    collectElementIds(child, ids);
}

QStringList
ProjectTreeModel::elementIdsForIndexes(const QModelIndexList &indexes) const {
  QSet<QString> selectedIds;
  for (const auto &item : indexes) {
    const TreeNode *node = nodeForIndex(item);
    if (!node || (node->kind != QStringLiteral("element") &&
                  node->kind != QStringLiteral("namespace") &&
                  node->kind != QStringLiteral("folder")))
      continue;
    collectElementIds(node, selectedIds);
  }

  // QItemSelectionModel does not promise selection-order iteration. Returning
  // project order makes the dropped grid deterministic and stable across runs.
  QStringList result;
  result.reserve(selectedIds.size());
  for (const auto &element : m_controller->data().elements)
    if (selectedIds.contains(element.id))
      result.append(element.id);
  return result;
}

QString ProjectTreeModel::browserItemsJsonForIndexes(
    const QModelIndexList &indexes) const {
  QSet<QString> seen;
  QJsonArray items;
  for (const QModelIndex &item : indexes) {
    const TreeNode *node = nodeForIndex(item);
    if (!node || (node->kind != QStringLiteral("element") &&
                  node->kind != QStringLiteral("folder") &&
                  node->kind != QStringLiteral("diagram")))
      continue;
    const QString key = node->kind + u':' + node->objectId;
    if (seen.contains(key))
      continue;
    seen.insert(key);
    QJsonObject object;
    object.insert(QStringLiteral("kind"), node->kind);
    object.insert(QStringLiteral("id"), node->objectId);
    object.insert(QStringLiteral("name"), node->label);
    items.append(object);
  }
  return QString::fromUtf8(
      QJsonDocument(items).toJson(QJsonDocument::Compact));
}

void ProjectTreeModel::selectFromPointer(QItemSelectionModel *selectionModel,
                                         const QModelIndex &item) {
  selectWithModifiers(selectionModel, item,
                      QGuiApplication::keyboardModifiers());
}

void ProjectTreeModel::selectWithModifiers(QItemSelectionModel *selectionModel,
                                           const QModelIndex &item,
                                           Qt::KeyboardModifiers modifiers) {
  if (!selectionModel || selectionModel->model() != this ||
      item.model() != this || !item.isValid())
    return;

  const QString kind = data(item, KindRole).toString();
  if (kind != QStringLiteral("element") &&
      kind != QStringLiteral("namespace") && kind != QStringLiteral("folder")) {
    selectionModel->clearSelection();
    selectionModel->setCurrentIndex(item, QItemSelectionModel::NoUpdate);
    m_selectionAnchor = {};
    return;
  }

  const bool extend = modifiers.testFlag(Qt::ShiftModifier);
  const bool toggle = modifiers.testFlag(Qt::ControlModifier);
  if (extend && m_selectionAnchor.isValid() &&
      m_selectionAnchor.parent() == item.parent()) {
    const int firstRow = std::min(m_selectionAnchor.row(), item.row());
    const int lastRow = std::max(m_selectionAnchor.row(), item.row());
    const QItemSelection range(index(firstRow, 0, item.parent()),
                               index(lastRow, 0, item.parent()));
    auto command = QItemSelectionModel::Select | QItemSelectionModel::Rows;
    if (!toggle)
      command |= QItemSelectionModel::Clear;
    selectionModel->select(range, command);
  } else if (toggle) {
    selectionModel->select(item, QItemSelectionModel::Toggle |
                                     QItemSelectionModel::Rows);
    m_selectionAnchor = item;
  } else {
    selectionModel->select(item, QItemSelectionModel::ClearAndSelect |
                                     QItemSelectionModel::Rows);
    m_selectionAnchor = item;
  }
  selectionModel->setCurrentIndex(item, QItemSelectionModel::NoUpdate);
}

void ProjectTreeModel::startTreeDrag(const QModelIndexList &indexes) {
  const QStringList elementIds = elementIdsForIndexes(indexes);
  QSet<QString> requested(elementIds.cbegin(), elementIds.cend());
  QStringList validIds;
  for (const auto &element : m_controller->data().elements) {
    if (requested.contains(element.id))
      validIds.append(element.id);
  }
  QSet<const TreeNode *> selectedNodes;
  for (const QModelIndex &item : indexes)
    if (const TreeNode *node = nodeForIndex(item))
      selectedNodes.insert(node);

  QJsonArray browserItems;
  for (const QModelIndex &item : indexes) {
    const TreeNode *node = nodeForIndex(item);
    if (!node || (node->kind != QStringLiteral("element") &&
                  node->kind != QStringLiteral("folder")))
      continue;
    bool selectedAncestor = false;
    for (const TreeNode *ancestor = node->parent; ancestor;
         ancestor = ancestor->parent) {
      if (selectedNodes.contains(ancestor) &&
          (ancestor->kind == QStringLiteral("element") ||
           ancestor->kind == QStringLiteral("folder"))) {
        selectedAncestor = true;
        break;
      }
    }
    if (selectedAncestor)
      continue;
    QJsonObject browserItem;
    browserItem.insert(QStringLiteral("kind"), node->kind);
    browserItem.insert(QStringLiteral("id"), node->objectId);
    browserItems.append(browserItem);
  }

  if (validIds.isEmpty() && browserItems.isEmpty())
    return;

  auto *mimeData = new QMimeData;
  if (!validIds.isEmpty())
    mimeData->setData(QLatin1String(kElementsMimeType),
                      QJsonDocument(QJsonArray::fromStringList(validIds))
                          .toJson(QJsonDocument::Compact));
  if (!browserItems.isEmpty())
    mimeData->setData(
        QLatin1String(kBrowserItemsMimeType),
        QJsonDocument(browserItems).toJson(QJsonDocument::Compact));
  if (!browserItems.isEmpty())
    mimeData->setData(
        QLatin1String(kDiagramSubjectsMimeType),
        QJsonDocument(browserItems).toJson(QJsonDocument::Compact));

  constexpr int kPreviewWidth = 240;
  constexpr int kPreviewHeight = 46;
  QPixmap preview(kPreviewWidth, kPreviewHeight);
  preview.fill(Qt::transparent);
  QPainter painter(&preview);
  painter.setRenderHint(QPainter::Antialiasing);
  const ui::UiPalette palette = ui::uiPalette();
  painter.setPen(palette.dragGhostBorder);
  painter.setBrush(palette.dragGhostFill);
  painter.drawRoundedRect(preview.rect().adjusted(1, 1, -1, -1), 5, 5);
  painter.setPen(palette.dragGhostText);
  const QString label =
      validIds.isEmpty() ? QStringLiteral("Move project-tree item")
      : validIds.size() == 1
          ? QStringLiteral("Add 1 element to diagram")
          : QStringLiteral("Add %1 elements to diagram").arg(validIds.size());
  painter.drawText(preview.rect().adjusted(12, 0, -8, 0),
                   Qt::AlignVCenter | Qt::AlignLeft, label);
  painter.end();

  QDrag drag(this);
  drag.setMimeData(mimeData);
  drag.setPixmap(preview);
  drag.setHotSpot({12, 12});
  drag.exec(Qt::CopyAction, Qt::CopyAction);
}

void ProjectTreeModel::reset() {
  m_selectionAnchor = {};
  beginResetModel();
  rebuildTree();
  endResetModel();
}

DiagramListModel::DiagramListModel(ProjectController *controller)
    : QAbstractListModel(controller), m_controller(controller) {
  connect(controller, &ProjectController::stateChanged, this,
          &DiagramListModel::reset);
}

int DiagramListModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_controller->data().diagrams.size();
}

QVariant DiagramListModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= m_controller->data().diagrams.size())
    return {};
  const auto &diagram = m_controller->data().diagrams.at(index.row());
  if (role == IdRole)
    return diagram.id;
  if (role == NameRole || role == Qt::DisplayRole)
    return diagram.name;
  return {};
}

QHash<int, QByteArray> DiagramListModel::roleNames() const {
  return {{IdRole, "diagramId"}, {NameRole, "name"}};
}

void DiagramListModel::reset() {
  beginResetModel();
  endResetModel();
}

} // namespace uuml
