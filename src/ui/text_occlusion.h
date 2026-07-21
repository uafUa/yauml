#pragma once

#include <QList>
#include <QRectF>
#include <QVector>

namespace uuml::ui {

// Returns the axis-aligned fragments of `target` that remain visible after
// later-painted opaque rectangles cover it. The fragments never overlap,
// which lets the scene graph clip atlas-backed text without stencil nodes or
// one draw call per diagram element.
QVector<QRectF> visibleRectangleFragments(const QRectF &target,
                                          const QList<QRectF> &opaqueOccluders);

} // namespace uuml::ui
