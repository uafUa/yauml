#include "ui/diagram_clipping.h"

#include "core/presentation_layout.h"

#include <QSet>

namespace yauml::ui {

DiagramClipLayout::DiagramClipLayout(
    const Diagram &diagram, const QHash<QString, QRectF> &geometryOverrides,
    const QSet<QString> &excludedPresentationIds)
    : m_diagram(diagram), m_geometryOverrides(geometryOverrides),
      m_excludedPresentationIds(excludedPresentationIds) {
  m_containersById.reserve(diagram.containers.size());
  for (const auto &container : diagram.containers) {
    m_containersById.insert(container.id, &container);
    for (const QString &childId : container.childPresentationIds)
      m_ownerByChild.insert(childId, container.id);
  }
}

QRectF DiagramClipLayout::geometryFor(const QString &presentationId) const {
  const auto override = m_geometryOverrides.constFind(presentationId);
  if (override != m_geometryOverrides.cend())
    return *override;
  if (const auto *node = findNode(m_diagram, presentationId))
    return node->geometry;
  if (const auto *container = findContainer(m_diagram, presentationId))
    return container->geometry;
  return {};
}

QRectF
DiagramClipLayout::childViewport(const ContainerPresentation &container) const {
  const QRectF geometry = geometryFor(container.id);
  const qreal headerHeight = container.subjectKind == QStringLiteral("package")
                                 ? 24.0
                                 : presentation_layout::kContainerHeaderHeight;
  const QRectF body(geometry.left(), geometry.top() + headerHeight,
                    geometry.width(), geometry.height() - headerHeight);
  return body.adjusted(
      kContainerChildViewportInset, kContainerChildViewportInset,
      -kContainerChildViewportInset, -kContainerChildViewportInset);
}

PresentationClip
DiagramClipLayout::clipFor(const QString &presentationId,
                           const QSet<QString> &detachedSubtreeRoots) const {
  PresentationClip result;
  QSet<QString> visited;
  QString currentId = presentationId;
  QString ownerId = m_ownerByChild.value(currentId);
  while (!ownerId.isEmpty() && !visited.contains(ownerId)) {
    // During a drag, the moved subtree is visually detached before its
    // persisted owner is changed on drop. Descendants remain clipped by the
    // moving root, but the root and its contents can leave the old container.
    if (detachedSubtreeRoots.contains(currentId))
      break;
    visited.insert(ownerId);
    const auto *owner = m_containersById.value(ownerId);
    if (!owner)
      break;
    const QRectF ownerViewport = childViewport(*owner);
    result.rect =
        result.active ? result.rect.intersected(ownerViewport) : ownerViewport;
    result.active = true;
    currentId = ownerId;
    ownerId = m_ownerByChild.value(ownerId);
  }
  return result;
}

ContainerOverflowEdges
DiagramClipLayout::overflowEdges(const ContainerPresentation &container) const {
  ContainerOverflowEdges result;
  const QRectF viewport = childViewport(container);
  constexpr qreal kOverflowTolerance = 0.01;
  for (const QString &childId : container.childPresentationIds) {
    if (m_excludedPresentationIds.contains(childId))
      continue;
    const QRectF childGeometry = geometryFor(childId);
    if (!childGeometry.isValid())
      continue;
    if (childGeometry.left() < viewport.left() - kOverflowTolerance)
      result.setFlag(ContainerOverflowEdge::Left);
    if (childGeometry.right() > viewport.right() + kOverflowTolerance)
      result.setFlag(ContainerOverflowEdge::Right);
    if (childGeometry.top() < viewport.top() - kOverflowTolerance)
      result.setFlag(ContainerOverflowEdge::Top);
    if (childGeometry.bottom() > viewport.bottom() + kOverflowTolerance)
      result.setFlag(ContainerOverflowEdge::Bottom);
  }
  return result;
}

} // namespace yauml::ui
