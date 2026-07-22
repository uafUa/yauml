#include "core/project_tree_model.h"

#include "core/project_controller.h"
#include "ui/ui_theme.h"

#include <QDrag>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>

namespace uuml {

namespace {
constexpr quintptr kModelRoot = 1;
constexpr quintptr kDiagramRoot = 2;
constexpr quintptr kElementBase = 1000;
constexpr quintptr kDiagramBase = 2000;
constexpr auto kElementsMimeType = "application/x-uuml-element-ids";
} // namespace

ProjectTreeModel::ProjectTreeModel(ProjectController *controller)
    : QAbstractItemModel(controller), m_controller(controller) {
  connect(controller, &ProjectController::stateChanged, this,
          &ProjectTreeModel::reset);
}

QModelIndex ProjectTreeModel::index(int row, int column,
                                    const QModelIndex &parentIndex) const {
  if (column != 0 || row < 0)
    return {};
  if (!parentIndex.isValid()) {
    if (row == 0)
      return createIndex(row, column, kModelRoot);
    if (row == 1)
      return createIndex(row, column, kDiagramRoot);
    return {};
  }
  if (parentIndex.internalId() == kModelRoot &&
      row < m_controller->data().elements.size())
    return createIndex(row, column, kElementBase + static_cast<quintptr>(row));
  if (parentIndex.internalId() == kDiagramRoot &&
      row < m_controller->data().diagrams.size())
    return createIndex(row, column, kDiagramBase + static_cast<quintptr>(row));
  return {};
}

QModelIndex ProjectTreeModel::parent(const QModelIndex &child) const {
  if (!child.isValid())
    return {};
  if (child.internalId() >= kElementBase && child.internalId() < kDiagramBase)
    return createIndex(0, 0, kModelRoot);
  if (child.internalId() >= kDiagramBase)
    return createIndex(1, 0, kDiagramRoot);
  return {};
}

int ProjectTreeModel::rowCount(const QModelIndex &parentIndex) const {
  if (!parentIndex.isValid())
    return 2;
  if (parentIndex.internalId() == kModelRoot)
    return m_controller->data().elements.size();
  if (parentIndex.internalId() == kDiagramRoot)
    return m_controller->data().diagrams.size();
  return 0;
}

int ProjectTreeModel::columnCount(const QModelIndex &) const { return 1; }

QVariant ProjectTreeModel::data(const QModelIndex &item, int role) const {
  if (!item.isValid())
    return {};
  const quintptr id = item.internalId();
  if (id == kModelRoot) {
    if (role == Qt::DisplayRole)
      return QStringLiteral("Model");
    if (role == KindRole)
      return QStringLiteral("root");
    return {};
  }
  if (id == kDiagramRoot) {
    if (role == Qt::DisplayRole)
      return QStringLiteral("Diagrams");
    if (role == KindRole)
      return QStringLiteral("root");
    return {};
  }
  if (id >= kElementBase && id < kDiagramBase) {
    const int row = static_cast<int>(id - kElementBase);
    if (row >= m_controller->data().elements.size())
      return {};
    const auto &element = m_controller->data().elements.at(row);
    if (role == Qt::DisplayRole)
      return element.name;
    if (role == IdRole)
      return element.id;
    if (role == KindRole)
      return QStringLiteral("element");
    if (role == TypeRole)
      return toString(element.type);
  } else if (id >= kDiagramBase) {
    const int row = static_cast<int>(id - kDiagramBase);
    if (row >= m_controller->data().diagrams.size())
      return {};
    const auto &diagram = m_controller->data().diagrams.at(row);
    if (role == Qt::DisplayRole)
      return diagram.name;
    if (role == IdRole)
      return diagram.id;
    if (role == KindRole)
      return QStringLiteral("diagram");
    if (role == TypeRole)
      return QStringLiteral("class diagram");
  }
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
  if (kind == QStringLiteral("element")) {
    const auto &elements = m_controller->data().elements;
    for (int row = 0; row < elements.size(); ++row)
      if (elements.at(row).id == objectId)
        return index(row, 0, index(0, 0));
  } else if (kind == QStringLiteral("diagram")) {
    const auto &diagrams = m_controller->data().diagrams;
    for (int row = 0; row < diagrams.size(); ++row)
      if (diagrams.at(row).id == objectId)
        return index(row, 0, index(1, 0));
  }
  return {};
}

QStringList
ProjectTreeModel::elementIdsForIndexes(const QModelIndexList &indexes) const {
  QSet<QString> selectedIds;
  for (const auto &item : indexes) {
    if (item.model() != this ||
        data(item, KindRole).toString() != QStringLiteral("element"))
      continue;
    const QString id = data(item, IdRole).toString();
    if (!id.isEmpty())
      selectedIds.insert(id);
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

void ProjectTreeModel::startElementDrag(const QStringList &elementIds) {
  QSet<QString> requested(elementIds.cbegin(), elementIds.cend());
  QStringList validIds;
  for (const auto &element : m_controller->data().elements) {
    if (requested.contains(element.id))
      validIds.append(element.id);
  }
  if (validIds.isEmpty())
    return;

  auto *mimeData = new QMimeData;
  mimeData->setData(QLatin1String(kElementsMimeType),
                    QJsonDocument(QJsonArray::fromStringList(validIds))
                        .toJson(QJsonDocument::Compact));

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
      validIds.size() == 1
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
  beginResetModel();
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
