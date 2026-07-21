#include "ui/text_occlusion.h"

#include <utility>

namespace uuml::ui {
namespace {

void appendIfVisible(QVector<QRectF> &fragments, const QRectF &candidate) {
  if (candidate.width() > 0.0 && candidate.height() > 0.0)
    fragments.append(candidate);
}

QVector<QRectF> subtractRectangle(const QRectF &source,
                                  const QRectF &occluder) {
  const QRectF intersection = source.intersected(occluder);
  if (intersection.isEmpty())
    return {source};

  QVector<QRectF> fragments;
  fragments.reserve(4);
  appendIfVisible(fragments, {source.left(), source.top(), source.width(),
                              intersection.top() - source.top()});
  appendIfVisible(fragments,
                  {source.left(), intersection.bottom(), source.width(),
                   source.bottom() - intersection.bottom()});
  appendIfVisible(fragments,
                  {source.left(), intersection.top(),
                   intersection.left() - source.left(), intersection.height()});
  appendIfVisible(fragments, {intersection.right(), intersection.top(),
                              source.right() - intersection.right(),
                              intersection.height()});
  return fragments;
}

} // namespace

QVector<QRectF>
visibleRectangleFragments(const QRectF &target,
                          const QList<QRectF> &opaqueOccluders) {
  if (target.isEmpty())
    return {};

  QVector<QRectF> visible{target};
  for (const QRectF &occluder : opaqueOccluders) {
    if (!occluder.intersects(target))
      continue;

    QVector<QRectF> next;
    for (const QRectF &fragment : std::as_const(visible))
      next.append(subtractRectangle(fragment, occluder));
    visible = std::move(next);
    if (visible.isEmpty())
      break;
  }
  return visible;
}

} // namespace uuml::ui
