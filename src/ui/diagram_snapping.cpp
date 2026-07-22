#include "ui/diagram_snapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>

namespace uuml::ui {
namespace {

constexpr qreal kComparisonEpsilon = 0.001;
constexpr qreal kGuidePadding = 12.0;

struct AxisFeature {
  qreal position = 0.0;
  QRectF geometry;
};

struct AxisCandidate {
  qreal adjustment = 0.0;
  qreal distance = std::numeric_limits<qreal>::max();
  qreal guidePosition = 0.0;
  QRectF movingGeometry;
  QRectF stationaryGeometry;
  bool alignment = false;
  bool valid = false;
};

std::array<qreal, 3> xFeatures(const QRectF &geometry) {
  return {geometry.left(), geometry.center().x(), geometry.right()};
}

std::array<qreal, 3> yFeatures(const QRectF &geometry) {
  return {geometry.top(), geometry.center().y(), geometry.bottom()};
}

void considerCandidate(AxisCandidate &best, qreal adjustment,
                       qreal guidePosition, const QRectF &moving,
                       const QRectF &stationary, bool alignment,
                       qreal tolerance) {
  const qreal distance = std::abs(adjustment);
  if (distance > tolerance + kComparisonEpsilon)
    return;
  const bool strictlyCloser = distance < best.distance - kComparisonEpsilon;
  const bool sameDistance =
      std::abs(distance - best.distance) <= kComparisonEpsilon;
  // Alignment wins an exact tie with the grid because it communicates a
  // stronger relationship and provides a visible guide.
  if (best.valid && !strictlyCloser &&
      !(sameDistance && alignment && !best.alignment))
    return;
  best.adjustment = adjustment;
  best.distance = distance;
  best.guidePosition = guidePosition;
  best.movingGeometry = moving;
  best.stationaryGeometry = stationary;
  best.alignment = alignment;
  best.valid = true;
}

AxisCandidate alignmentCandidate(const QList<DiagramNodeGeometry> &moving,
                                 const QList<DiagramNodeGeometry> &stationary,
                                 const QPointF &requestedDelta,
                                 Qt::Orientation orientation, qreal tolerance) {
  QVector<AxisFeature> stationaryFeatures;
  stationaryFeatures.reserve(stationary.size() * 3);
  for (const auto &node : stationary) {
    const auto features = orientation == Qt::Horizontal
                              ? xFeatures(node.geometry)
                              : yFeatures(node.geometry);
    for (const qreal position : features)
      stationaryFeatures.append({position, node.geometry});
  }
  std::sort(stationaryFeatures.begin(), stationaryFeatures.end(),
            [](const AxisFeature &left, const AxisFeature &right) {
              if (left.position != right.position)
                return left.position < right.position;
              if (left.geometry.top() != right.geometry.top())
                return left.geometry.top() < right.geometry.top();
              return left.geometry.left() < right.geometry.left();
            });

  AxisCandidate best;
  for (const auto &node : moving) {
    const QRectF translated = node.geometry.translated(requestedDelta);
    const auto features = orientation == Qt::Horizontal ? xFeatures(translated)
                                                        : yFeatures(translated);
    for (const qreal movingPosition : features) {
      const auto nearest = std::lower_bound(
          stationaryFeatures.cbegin(), stationaryFeatures.cend(),
          movingPosition, [](const AxisFeature &feature, qreal position) {
            return feature.position < position;
          });
      const auto consider = [&](const AxisFeature &feature) {
        considerCandidate(best, feature.position - movingPosition,
                          feature.position, node.geometry, feature.geometry,
                          true, tolerance);
      };
      if (nearest != stationaryFeatures.cend())
        consider(*nearest);
      if (nearest != stationaryFeatures.cbegin())
        consider(*std::prev(nearest));
    }
  }
  return best;
}

const DiagramNodeGeometry *
findPrimaryNode(const QList<DiagramNodeGeometry> &moving,
                const QString &primaryNodeId) {
  const auto found = std::find_if(moving.cbegin(), moving.cend(),
                                  [&](const DiagramNodeGeometry &node) {
                                    return node.id == primaryNodeId;
                                  });
  return found == moving.cend() ? nullptr : &*found;
}

void considerGridCandidate(AxisCandidate &best, qreal position, qreal spacing,
                           qreal tolerance) {
  if (spacing <= 0.0)
    return;
  const qreal target = std::round(position / spacing) * spacing;
  considerCandidate(best, target - position, target, {}, {}, false, tolerance);
}

AxisCandidate
resizeAlignmentCandidate(const QRectF &requestedGeometry,
                         const QList<DiagramNodeGeometry> &stationary,
                         Qt::Orientation orientation, qreal minimumEdge,
                         qreal tolerance) {
  const qreal draggedEdge = orientation == Qt::Horizontal
                                ? requestedGeometry.right()
                                : requestedGeometry.bottom();
  AxisCandidate best;
  for (const auto &node : stationary) {
    const auto features = orientation == Qt::Horizontal
                              ? xFeatures(node.geometry)
                              : yFeatures(node.geometry);
    for (const qreal position : features) {
      if (position < minimumEdge - kComparisonEpsilon)
        continue;
      considerCandidate(best, position - draggedEdge, position,
                        requestedGeometry, node.geometry, true, tolerance);
    }
  }
  return best;
}

void considerResizeGridCandidate(AxisCandidate &best, qreal position,
                                 qreal minimumEdge, qreal spacing,
                                 qreal tolerance) {
  if (spacing <= 0.0)
    return;
  const qreal target = std::round(position / spacing) * spacing;
  if (target < minimumEdge - kComparisonEpsilon)
    return;
  considerCandidate(best, target - position, target, {}, {}, false, tolerance);
}

} // namespace

DiagramSnapResult snapDiagramMove(const QList<DiagramNodeGeometry> &moving,
                                  const QList<DiagramNodeGeometry> &stationary,
                                  const QString &primaryNodeId,
                                  const QPointF &requestedDelta,
                                  const DiagramSnapOptions &options) {
  DiagramSnapResult result{requestedDelta, {}};
  if (moving.isEmpty() || options.tolerance < 0.0)
    return result;

  AxisCandidate xCandidate;
  AxisCandidate yCandidate;
  if (options.snapToAlignment && !stationary.isEmpty()) {
    xCandidate = alignmentCandidate(moving, stationary, requestedDelta,
                                    Qt::Horizontal, options.tolerance);
    yCandidate = alignmentCandidate(moving, stationary, requestedDelta,
                                    Qt::Vertical, options.tolerance);
  }

  if (options.snapToGrid) {
    if (const auto *primary = findPrimaryNode(moving, primaryNodeId)) {
      considerGridCandidate(xCandidate,
                            primary->geometry.left() + requestedDelta.x(),
                            options.gridSpacing, options.tolerance);
      considerGridCandidate(yCandidate,
                            primary->geometry.top() + requestedDelta.y(),
                            options.gridSpacing, options.tolerance);
    }
  }

  if (xCandidate.valid)
    result.delta.rx() += xCandidate.adjustment;
  if (yCandidate.valid)
    result.delta.ry() += yCandidate.adjustment;

  if (xCandidate.valid && xCandidate.alignment) {
    const QRectF moved = xCandidate.movingGeometry.translated(result.delta);
    const qreal top =
        std::min(moved.top(), xCandidate.stationaryGeometry.top()) -
        kGuidePadding;
    const qreal bottom =
        std::max(moved.bottom(), xCandidate.stationaryGeometry.bottom()) +
        kGuidePadding;
    result.guides.append(QLineF(xCandidate.guidePosition, top,
                                xCandidate.guidePosition, bottom));
  }
  if (yCandidate.valid && yCandidate.alignment) {
    const QRectF moved = yCandidate.movingGeometry.translated(result.delta);
    const qreal left =
        std::min(moved.left(), yCandidate.stationaryGeometry.left()) -
        kGuidePadding;
    const qreal right =
        std::max(moved.right(), yCandidate.stationaryGeometry.right()) +
        kGuidePadding;
    result.guides.append(QLineF(left, yCandidate.guidePosition, right,
                                yCandidate.guidePosition));
  }
  return result;
}

DiagramResizeSnapResult
snapDiagramBottomRightResize(const QRectF &requestedGeometry,
                             const QList<DiagramNodeGeometry> &stationary,
                             const QSizeF &minimumSize,
                             const DiagramSnapOptions &options) {
  DiagramResizeSnapResult result{requestedGeometry, {}};
  if (options.tolerance < 0.0)
    return result;

  const qreal minimumRight = requestedGeometry.left() + minimumSize.width();
  const qreal minimumBottom = requestedGeometry.top() + minimumSize.height();
  AxisCandidate xCandidate;
  AxisCandidate yCandidate;
  if (options.snapToAlignment && !stationary.isEmpty()) {
    xCandidate =
        resizeAlignmentCandidate(requestedGeometry, stationary, Qt::Horizontal,
                                 minimumRight, options.tolerance);
    yCandidate =
        resizeAlignmentCandidate(requestedGeometry, stationary, Qt::Vertical,
                                 minimumBottom, options.tolerance);
  }

  if (options.snapToGrid) {
    considerResizeGridCandidate(xCandidate, requestedGeometry.right(),
                                minimumRight, options.gridSpacing,
                                options.tolerance);
    considerResizeGridCandidate(yCandidate, requestedGeometry.bottom(),
                                minimumBottom, options.gridSpacing,
                                options.tolerance);
  }

  if (xCandidate.valid)
    result.geometry.setRight(requestedGeometry.right() + xCandidate.adjustment);
  if (yCandidate.valid)
    result.geometry.setBottom(requestedGeometry.bottom() +
                              yCandidate.adjustment);

  if (xCandidate.valid && xCandidate.alignment) {
    const qreal top =
        std::min(result.geometry.top(), xCandidate.stationaryGeometry.top()) -
        kGuidePadding;
    const qreal bottom = std::max(result.geometry.bottom(),
                                  xCandidate.stationaryGeometry.bottom()) +
                         kGuidePadding;
    result.guides.append(QLineF(xCandidate.guidePosition, top,
                                xCandidate.guidePosition, bottom));
  }
  if (yCandidate.valid && yCandidate.alignment) {
    const qreal left =
        std::min(result.geometry.left(), yCandidate.stationaryGeometry.left()) -
        kGuidePadding;
    const qreal right = std::max(result.geometry.right(),
                                 yCandidate.stationaryGeometry.right()) +
                        kGuidePadding;
    result.guides.append(QLineF(left, yCandidate.guidePosition, right,
                                yCandidate.guidePosition));
  }
  return result;
}

} // namespace uuml::ui
