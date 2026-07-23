#pragma once

#include "core/project_data.h"

#include <QFlags>
#include <QHash>
#include <QRectF>
#include <QSet>

namespace uuml::ui {

// Keep child content clear of the container outline. The outline itself is
// rendered in view-pixel units, so it is also redrawn above child geometry;
// this scene-space inset keeps clipping and hit-testing consistently inside
// the frame at every zoom level.
inline constexpr qreal kContainerChildViewportInset = 2.0;

enum class ContainerOverflowEdge : quint8 {
  Left = 1 << 0,
  Right = 1 << 1,
  Top = 1 << 2,
  Bottom = 1 << 3
};
Q_DECLARE_FLAGS(ContainerOverflowEdges, ContainerOverflowEdge)

struct PresentationClip {
  QRectF rect;
  bool active = false;
};

// Resolves hierarchical presentation clipping against persisted geometry or
// an interaction preview. The indexes are built once so rendering remains
// linear in the number of presentations even for large diagrams.
class DiagramClipLayout final {
public:
  explicit DiagramClipLayout(
      const Diagram &diagram,
      const QHash<QString, QRectF> &geometryOverrides = {});

  QRectF geometryFor(const QString &presentationId) const;
  QRectF childViewport(const ContainerPresentation &container) const;
  PresentationClip
  clipFor(const QString &presentationId,
          const QSet<QString> &detachedSubtreeRoots = {}) const;
  ContainerOverflowEdges
  overflowEdges(const ContainerPresentation &container) const;

private:
  const Diagram &m_diagram;
  QHash<QString, QRectF> m_geometryOverrides;
  QHash<QString, const ContainerPresentation *> m_containersById;
  QHash<QString, QString> m_ownerByChild;
};

} // namespace uuml::ui

Q_DECLARE_OPERATORS_FOR_FLAGS(uuml::ui::ContainerOverflowEdges)
