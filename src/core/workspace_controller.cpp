#include "core/workspace_controller.h"

#include "core/project_controller.h"
#include "ui/ui_theme.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDrag>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSettings>
#include <algorithm>

namespace yauml {
namespace {

constexpr int kDefaultDetachedWidth = 900;
constexpr int kDefaultDetachedHeight = 650;
constexpr int kWheelGestureQuietPeriodMs = 250;
constexpr auto kDiagramMimeType = "application/x-yauml-diagram-id";

QRect defaultDetachedGeometry(int x = 120, int y = 120) {
  return {x, y, kDefaultDetachedWidth, kDefaultDetachedHeight};
}

QRect ensureVisibleGeometry(QRect geometry) {
  if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
    return geometry;
  const auto screens = QGuiApplication::screens();
  if (screens.isEmpty())
    return geometry;
  for (const auto *screen : screens)
    if (screen->availableGeometry().intersects(geometry))
      return geometry;

  const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
  geometry.setSize(
      geometry.size().boundedTo(available.size()).expandedTo({480, 320}));
  geometry.moveCenter(available.center());
  return geometry;
}

} // namespace

WorkspaceController::WorkspaceController(ProjectController *project,
                                         bool persistenceEnabled,
                                         QObject *parent)
    : QAbstractListModel(parent), m_project(project),
      m_workspaceProjectId(project->data().id),
      m_persistenceEnabled(persistenceEnabled) {
  m_persistenceTimer.setSingleShot(true);
  m_persistenceTimer.setInterval(350);
  connect(&m_persistenceTimer, &QTimer::timeout, this,
          &WorkspaceController::persistWorkspace);
  m_hosts.insert(mainHostId(), projectDiagramIds());
  if (!projectDiagramIds().isEmpty())
    m_activeDiagramId = projectDiagramIds().first();
  connect(project, &ProjectController::diagramsChanged, this,
          &WorkspaceController::reconcile);
  if (m_persistenceEnabled) {
    restoreUiSettings();
    restoreWorkspace();
  }
}

WorkspaceController::~WorkspaceController() {
  if (m_persistenceEnabled)
    persistWorkspace();
}

void WorkspaceController::noteProjectTreeWheelInput() {
  m_projectTreeWheelInput.restart();
}

bool WorkspaceController::consumeDiagramWheelSuppression() {
  if (!m_projectTreeWheelInput.isValid() ||
      m_projectTreeWheelInput.elapsed() > kWheelGestureQuietPeriodMs)
    return false;

  // Restart for every discarded event: suppression ends only after the
  // physical wheel/touchpad gesture has actually gone quiet.
  m_projectTreeWheelInput.restart();
  return true;
}

int WorkspaceController::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_detachedOrder.size();
}

QVariant WorkspaceController::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= m_detachedOrder.size() || role != WindowHostIdRole)
    return {};
  return m_detachedOrder.at(index.row());
}

QHash<int, QByteArray> WorkspaceController::roleNames() const {
  return {{WindowHostIdRole, QByteArrayLiteral("windowHostId")}};
}

QString WorkspaceController::mainHostId() const {
  return QStringLiteral("main");
}

QVariantList WorkspaceController::detachedHostIds() const {
  QVariantList result;
  for (const auto &id : m_detachedOrder)
    result.append(id);
  return result;
}

QAbstractItemModel *WorkspaceController::detachedHostModel() { return this; }

QString WorkspaceController::activeDiagramId() const {
  return m_activeDiagramId;
}

void WorkspaceController::setActiveDiagramId(const QString &diagramId) {
  if (diagramId == m_activeDiagramId ||
      !projectDiagramIds().contains(diagramId))
    return;
  m_activeDiagramId = diagramId;
  emit activeDiagramIdChanged();
  schedulePersistence();
}

int WorkspaceController::revision() const { return m_revision; }

bool WorkspaceController::projectTreeVisible() const {
  return m_projectTreeVisible;
}

void WorkspaceController::setProjectTreeVisible(bool visible) {
  if (m_projectTreeVisible == visible)
    return;
  m_projectTreeVisible = visible;
  emit uiSettingsChanged();
  schedulePersistence();
}

bool WorkspaceController::propertiesVisible() const {
  return m_propertiesVisible;
}

void WorkspaceController::setPropertiesVisible(bool visible) {
  if (m_propertiesVisible == visible)
    return;
  m_propertiesVisible = visible;
  emit uiSettingsChanged();
  schedulePersistence();
}

int WorkspaceController::projectTreeWidth() const { return m_projectTreeWidth; }
int WorkspaceController::propertiesWidth() const { return m_propertiesWidth; }
int WorkspaceController::mainWindowX() const {
  return m_mainWindowGeometry.x();
}
int WorkspaceController::mainWindowY() const {
  return m_mainWindowGeometry.y();
}
int WorkspaceController::mainWindowWidth() const {
  return m_mainWindowGeometry.width();
}
int WorkspaceController::mainWindowHeight() const {
  return m_mainWindowGeometry.height();
}

QVariantList
WorkspaceController::diagramIdsForHost(const QString &hostId) const {
  QVariantList result;
  for (const auto &id : m_hosts.value(hostId))
    result.append(id);
  return result;
}

QString WorkspaceController::hostForDiagram(const QString &diagramId) const {
  for (auto it = m_hosts.cbegin(); it != m_hosts.cend(); ++it)
    if (it.value().contains(diagramId))
      return it.key();
  return {};
}

QString WorkspaceController::detachDiagram(const QString &diagramId, int x,
                                           int y) {
  if (!projectDiagramIds().contains(diagramId))
    return {};
  const QString current = hostForDiagram(diagramId);
  if (current != mainHostId() && m_hosts.value(current).size() == 1) {
    QRect geometry = m_geometries.value(current, defaultDetachedGeometry());
    geometry.moveTopLeft(QPoint(x, y));
    m_geometries[current] = geometry;
    emit hostRelocationRequested(current, x, y);
    return current;
  }
  removeFromCurrentHost(diagramId);
  const QString hostId = newId();
  m_hosts.insert(hostId, {diagramId});
  const int row = m_detachedOrder.size();
  beginInsertRows(QModelIndex(), row, row);
  m_detachedOrder.append(hostId);
  m_geometries.insert(hostId, defaultDetachedGeometry(x, y));
  endInsertRows();
  pruneEmptyHosts();
  setActiveDiagramId(diagramId);
  changed();
  return hostId;
}

QString WorkspaceController::detachDiagramAtCursor(const QString &diagramId) {
  // Position the floating window so its tab strip appears under the pointer,
  // matching the behavior users expect from native dock/tab systems.
  constexpr int kHorizontalTabOffset = 75;
  constexpr int kVerticalTabOffset = 18;
  const QPoint cursor = QCursor::pos();
  return detachDiagram(diagramId, cursor.x() - kHorizontalTabOffset,
                       cursor.y() - kVerticalTabOffset);
}

void WorkspaceController::startDiagramDrag(const QString &diagramId) {
  if (!projectDiagramIds().contains(diagramId))
    return;

  auto *mimeData = new QMimeData;
  mimeData->setData(kDiagramMimeType, diagramId.toUtf8());

  QDrag drag(this);
  drag.setMimeData(mimeData);

  // Give the drag a compact tab-like ghost and explicitly replace Qt's
  // forbidden cursor for the Ignore action. Ignore means "detach here" in this
  // application, so presenting it as an invalid operation is misleading.
  QPixmap ghost(190, 34);
  ghost.fill(Qt::transparent);
  {
    const auto &palette = ui::uiPalette();
    QPainter painter(&ghost);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(palette.dragGhostBorder);
    painter.setBrush(palette.dragGhostFill);
    painter.drawRoundedRect(ghost.rect().adjusted(0, 0, -1, -1), 5, 5);
    painter.setPen(palette.dragGhostText);
    painter.drawText(ghost.rect().adjusted(10, 0, -10, 0),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     m_project->diagramName(diagramId));
  }
  drag.setPixmap(ghost);
  drag.setHotSpot({75, 17});

  QPixmap detachCursor(24, 24);
  detachCursor.fill(Qt::transparent);
  {
    const auto &palette = ui::uiPalette();
    QPainter painter(&detachCursor);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette.accent, 2));
    painter.setBrush(palette.dragGhostFill);
    painter.drawRoundedRect(QRect(2, 2, 19, 19), 4, 4);
    painter.drawLine(8, 16, 17, 7);
    painter.drawLine(11, 7, 17, 7);
    painter.drawLine(17, 7, 17, 13);
  }
  drag.setDragCursor(detachCursor, Qt::IgnoreAction);

  const Qt::DropAction result = drag.exec(Qt::MoveAction, Qt::MoveAction);
  if (result == Qt::IgnoreAction &&
      QGuiApplication::topLevelAt(QCursor::pos()) == nullptr)
    detachDiagramAtCursor(diagramId);
}

void WorkspaceController::moveDiagram(const QString &diagramId,
                                      const QString &targetHostId, int index) {
  if (!projectDiagramIds().contains(diagramId) ||
      !m_hosts.contains(targetHostId))
    return;
  const QString current = hostForDiagram(diagramId);
  QStringList target = m_hosts.value(targetHostId);
  if (current == targetHostId) {
    target.removeAll(diagramId);
  } else {
    removeFromCurrentHost(diagramId);
    target = m_hosts.value(targetHostId);
  }
  if (index < 0 || index > target.size())
    index = target.size();
  target.insert(index, diagramId);
  m_hosts[targetHostId] = target;
  pruneEmptyHosts();
  setActiveDiagramId(diagramId);
  changed();
}

void WorkspaceController::returnDiagramToMain(const QString &diagramId) {
  moveDiagram(diagramId, mainHostId());
}

void WorkspaceController::closeHost(const QString &hostId) {
  if (hostId == mainHostId() || !m_hosts.contains(hostId))
    return;
  const QStringList diagrams = m_hosts.take(hostId);
  auto main = m_hosts.value(mainHostId());
  for (const auto &diagram : diagrams)
    if (!main.contains(diagram))
      main.append(diagram);
  m_hosts[mainHostId()] = main;
  const int row = m_detachedOrder.indexOf(hostId);
  if (row >= 0) {
    beginRemoveRows(QModelIndex(), row, row);
    m_detachedOrder.removeAt(row);
    endRemoveRows();
  }
  m_geometries.remove(hostId);
  changed();
}

void WorkspaceController::closeAllDetachedHosts() {
  if (m_detachedOrder.isEmpty())
    return;

  auto main = m_hosts.value(mainHostId());
  for (const auto &hostId : std::as_const(m_detachedOrder)) {
    for (const auto &diagramId : m_hosts.value(hostId))
      if (!main.contains(diagramId))
        main.append(diagramId);
  }
  beginResetModel();
  m_hosts.clear();
  m_hosts.insert(mainHostId(), main);
  m_detachedOrder.clear();
  m_geometries.clear();
  endResetModel();
  changed();
}

int WorkspaceController::hostX(const QString &hostId) const {
  return m_geometries.value(hostId, defaultDetachedGeometry()).x();
}

int WorkspaceController::hostY(const QString &hostId) const {
  return m_geometries.value(hostId, defaultDetachedGeometry()).y();
}

int WorkspaceController::hostWidth(const QString &hostId) const {
  return m_geometries.value(hostId, defaultDetachedGeometry()).width();
}

int WorkspaceController::hostHeight(const QString &hostId) const {
  return m_geometries.value(hostId, defaultDetachedGeometry()).height();
}

void WorkspaceController::updateHostGeometry(const QString &hostId, int x,
                                             int y, int width, int height) {
  if (!m_geometries.contains(hostId))
    return;
  m_geometries[hostId] = QRect(x, y, qMax(1, width), qMax(1, height));
  schedulePersistence();
}

void WorkspaceController::updatePanelWidths(int projectTreeWidth,
                                            int propertiesWidth) {
  const int left = qMax(170, projectTreeWidth);
  const int right = qMax(220, propertiesWidth);
  if (m_projectTreeWidth == left && m_propertiesWidth == right)
    return;
  m_projectTreeWidth = left;
  m_propertiesWidth = right;
  schedulePersistence();
}

void WorkspaceController::updateMainWindowGeometry(int x, int y, int width,
                                                   int height) {
  const QRect geometry(x, y, qMax(900, width), qMax(600, height));
  if (m_mainWindowGeometry == geometry)
    return;
  m_mainWindowGeometry = geometry;
  schedulePersistence();
}

QStringList WorkspaceController::projectDiagramIds() const {
  QStringList ids;
  for (const auto &diagram : m_project->data().diagrams)
    ids.append(diagram.id);
  return ids;
}

void WorkspaceController::reconcile() {
  if (m_workspaceProjectId != m_project->data().id) {
    persistWorkspace();
    m_persistenceTimer.stop();
    m_workspaceProjectId = m_project->data().id;
    if (m_persistenceEnabled) {
      restoreWorkspace();
    } else {
      const QStringList valid = projectDiagramIds();
      beginResetModel();
      m_hosts.clear();
      m_hosts.insert(mainHostId(), valid);
      m_detachedOrder.clear();
      m_geometries.clear();
      endResetModel();
      m_activeDiagramId = valid.isEmpty() ? QString() : valid.first();
      ++m_revision;
      emit hostsChanged();
      emit activeDiagramIdChanged();
    }
    return;
  }

  // ProjectController emits diagramsChanged at its completed-command boundary,
  // including commands that only modify the contents of a diagram. Preserve
  // the QML tab model in that common case: rebuilding an unchanged QVariantList
  // destroys the StackLayout delegates and can make an area fall back to its
  // first tab.
  const QHash<QString, QStringList> previousHosts = m_hosts;
  const QStringList previousDetachedOrder = m_detachedOrder;
  const QStringList valid = projectDiagramIds();
  for (auto it = m_hosts.begin(); it != m_hosts.end(); ++it) {
    it.value().removeIf([&](const QString &id) { return !valid.contains(id); });
  }
  QStringList hosted;
  for (const auto &items : std::as_const(m_hosts))
    hosted.append(items);
  auto main = m_hosts.value(mainHostId());
  for (const auto &id : valid)
    if (!hosted.contains(id))
      main.append(id);
  m_hosts[mainHostId()] = main;
  pruneEmptyHosts();
  if (!valid.contains(m_activeDiagramId)) {
    m_activeDiagramId = valid.isEmpty() ? QString() : valid.first();
    emit activeDiagramIdChanged();
  }
  if (m_hosts != previousHosts || m_detachedOrder != previousDetachedOrder)
    changed();
}

void WorkspaceController::removeFromCurrentHost(const QString &diagramId) {
  const QString hostId = hostForDiagram(diagramId);
  if (hostId.isEmpty())
    return;
  auto list = m_hosts.value(hostId);
  list.removeAll(diagramId);
  m_hosts[hostId] = list;
}

void WorkspaceController::pruneEmptyHosts() {
  for (int i = m_detachedOrder.size() - 1; i >= 0; --i) {
    const QString id = m_detachedOrder.at(i);
    if (m_hosts.value(id).isEmpty()) {
      beginRemoveRows(QModelIndex(), i, i);
      m_hosts.remove(id);
      m_geometries.remove(id);
      m_detachedOrder.removeAt(i);
      endRemoveRows();
    }
  }
}

void WorkspaceController::changed() {
  ++m_revision;
  emit hostsChanged();
  schedulePersistence();
}

void WorkspaceController::schedulePersistence() {
  if (m_persistenceEnabled && !m_restoring)
    m_persistenceTimer.start();
}

void WorkspaceController::persistWorkspace() {
  if (!m_persistenceEnabled || m_workspaceProjectId.isEmpty() || m_restoring)
    return;

  QSettings settings;
  settings.beginGroup(QStringLiteral("workspace/ui"));
  settings.setValue(QStringLiteral("projectTreeVisible"), m_projectTreeVisible);
  settings.setValue(QStringLiteral("propertiesVisible"), m_propertiesVisible);
  settings.setValue(QStringLiteral("projectTreeWidth"), m_projectTreeWidth);
  settings.setValue(QStringLiteral("propertiesWidth"), m_propertiesWidth);
  settings.setValue(QStringLiteral("mainWindowGeometry"), m_mainWindowGeometry);
  settings.endGroup();
  settings.beginGroup(
      QStringLiteral("workspace/projects/%1").arg(m_workspaceProjectId));
  settings.setValue(QStringLiteral("mainDiagrams"),
                    m_hosts.value(mainHostId()));
  settings.setValue(QStringLiteral("activeDiagram"), m_activeDiagramId);
  settings.beginWriteArray(QStringLiteral("detachedHosts"));
  for (int index = 0; index < m_detachedOrder.size(); ++index) {
    const QString &hostId = m_detachedOrder.at(index);
    settings.setArrayIndex(index);
    settings.setValue(QStringLiteral("diagrams"), m_hosts.value(hostId));
    settings.setValue(QStringLiteral("geometry"), m_geometries.value(hostId));
  }
  settings.endArray();
  settings.endGroup();
  settings.sync();
}

void WorkspaceController::restoreWorkspace() {
  if (!m_persistenceEnabled || m_workspaceProjectId.isEmpty())
    return;

  const QStringList valid = projectDiagramIds();
  QSet<QString> restored;
  QHash<QString, QStringList> hosts;
  QStringList detachedOrder;
  QHash<QString, QRect> geometries;

  QSettings settings;
  settings.beginGroup(
      QStringLiteral("workspace/projects/%1").arg(m_workspaceProjectId));
  QStringList main;
  for (const auto &diagramId :
       settings.value(QStringLiteral("mainDiagrams")).toStringList()) {
    if (valid.contains(diagramId) && !restored.contains(diagramId)) {
      main.append(diagramId);
      restored.insert(diagramId);
    }
  }

  const int detachedCount =
      settings.beginReadArray(QStringLiteral("detachedHosts"));
  for (int index = 0; index < detachedCount; ++index) {
    settings.setArrayIndex(index);
    QStringList diagrams;
    for (const auto &diagramId :
         settings.value(QStringLiteral("diagrams")).toStringList()) {
      if (valid.contains(diagramId) && !restored.contains(diagramId)) {
        diagrams.append(diagramId);
        restored.insert(diagramId);
      }
    }
    if (diagrams.isEmpty())
      continue;
    const QString hostId = newId();
    detachedOrder.append(hostId);
    hosts.insert(hostId, diagrams);
    geometries.insert(
        hostId, ensureVisibleGeometry(settings
                                          .value(QStringLiteral("geometry"),
                                                 defaultDetachedGeometry())
                                          .toRect()));
  }
  settings.endArray();
  for (const auto &diagramId : valid)
    if (!restored.contains(diagramId))
      main.append(diagramId);
  hosts.insert(mainHostId(), main);

  QString active = settings.value(QStringLiteral("activeDiagram")).toString();
  settings.endGroup();
  if (!valid.contains(active))
    active = valid.isEmpty() ? QString() : valid.first();

  m_restoring = true;
  beginResetModel();
  m_hosts = std::move(hosts);
  m_detachedOrder = std::move(detachedOrder);
  m_geometries = std::move(geometries);
  endResetModel();
  const bool activeChanged = m_activeDiagramId != active;
  m_activeDiagramId = active;
  ++m_revision;
  emit hostsChanged();
  if (activeChanged)
    emit activeDiagramIdChanged();
  m_restoring = false;
}

void WorkspaceController::restoreUiSettings() {
  QSettings settings;
  settings.beginGroup(QStringLiteral("workspace/ui"));
  m_projectTreeVisible =
      settings.value(QStringLiteral("projectTreeVisible"), true).toBool();
  m_propertiesVisible =
      settings.value(QStringLiteral("propertiesVisible"), true).toBool();
  m_projectTreeWidth = qMax(
      170, settings.value(QStringLiteral("projectTreeWidth"), 260).toInt());
  m_propertiesWidth =
      qMax(220, settings.value(QStringLiteral("propertiesWidth"), 310).toInt());
  m_mainWindowGeometry = ensureVisibleGeometry(
      settings.value(QStringLiteral("mainWindowGeometry"), m_mainWindowGeometry)
          .toRect());
  settings.endGroup();
}

} // namespace yauml
