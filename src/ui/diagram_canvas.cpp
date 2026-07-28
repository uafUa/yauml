#include "ui/diagram_canvas.h"

#include "core/application_settings.h"
#include "core/connector_port_layout.h"
#include "core/diagram_filter.h"
#include "core/presentation_layout.h"
#include "core/project_controller.h"
#include "core/project_style.h"
#include "core/stereotype_catalog.h"
#include "ui/connector_routing.h"
#include "ui/diagram_arrangement.h"
#include "ui/diagram_clipping.h"
#include "ui/diagram_snapping.h"
#include "ui/relationship_style.h"
#include "ui/text_occlusion.h"
#include "ui/triangle_batch.h"
#include "ui/ui_theme.h"

#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextureMaterial>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace uuml {
namespace {

constexpr qreal kHeaderHeight = presentation_layout::kNodeHeaderHeight;
constexpr qreal kLineHeight = presentation_layout::kNodeLineHeight;
constexpr qreal kPadding = presentation_layout::kNodeTextPadding;
constexpr qreal kSnapToleranceViewPixels = 8.0;
constexpr qreal kMinimumNodeWidth = presentation_layout::kMinimumNodeWidth;
constexpr qreal kMinimumNodeHeight = presentation_layout::kMinimumNodeHeight;
constexpr qreal kContainerHeaderHeight =
    presentation_layout::kContainerHeaderHeight;
constexpr qreal kMinimumContainerWidth =
    presentation_layout::kMinimumContainerWidth;
constexpr qreal kMinimumContainerHeight =
    presentation_layout::kMinimumContainerHeight;
constexpr int kTextAtlasSize = 2048;
constexpr int kTextAtlasPadding = 2;
constexpr qreal kPortSnapToleranceViewPixels = 10.0;

struct RenderElementStyle {
  bool customized = false;
  QColor fill;
  QColor headerFill;
  QColor border;
  QColor primaryText;
  QColor secondaryText;
  QColor divider;
};

struct RenderNode {
  QRectF rect;
  ElementType type = ElementType::Class;
  QString name;
  QString stereotype;
  QStringList attributes;
  QStringList operations;
  QStringList enumLiterals;
  RenderElementStyle style;
  bool selected = false;
  bool selectionReference = false;
  QRectF clipRect;
  bool hasClip = false;
};

struct RenderContainer {
  QRectF rect;
  QString name;
  QString stereotype;
  qreal titleWidth = 0.0;
  bool package = false;
  RenderElementStyle style;
  bool selected = false;
  int depth = 0;
  QRectF clipRect;
  bool hasClip = false;
  ui::ContainerOverflowEdges overflowEdges;
};

struct RenderConnector {
  QVector<QPointF> points;
  QVector<int> bendPointRouteIndices;
  QString name;
  QString stereotype;
  RelationshipEnd sourceEnd;
  RelationshipEnd targetEnd;
  QHash<QString, ConnectorAnnotationPlacement> annotationPlacements;
  RelationshipType type = RelationshipType::Dependency;
  bool selected = false;
  int selectedBendPoint = -1;
  bool preview = false;
};

struct RenderPortSnapPoint {
  QPointF position;
  bool active = false;
  QRectF clipRect;
  bool hasClip = false;
};

struct SceneSnapshot {
  QVector<RenderContainer> containers;
  QVector<RenderNode> nodes;
  QVector<RenderConnector> connectors;
  QVector<RenderPortSnapPoint> portSnapPoints;
  QVector<QLineF> alignmentGuides;
  QRectF lassoRect;
  bool lassoVisible = false;
};

qreal nodeHeaderHeight(const RenderNode &node) {
  return kHeaderHeight + (node.stereotype.isEmpty() ? 0.0 : kLineHeight);
}

struct RenderText {
  QRectF target;
  QString text;
  QFont font;
  QColor color;
  Qt::Alignment alignment;
  QVector<QRectF> visibleFragments;
};

int detailLevel(qreal zoom) {
  if (zoom < 0.28)
    return 0;
  if (zoom < 0.42)
    return 1;
  return 2;
}

qreal textRasterScale(qreal zoom, qreal devicePixelRatio) {
  const qreal requested = zoom * devicePixelRatio;
  return std::clamp(std::ceil(requested * 2.0) / 2.0, 1.0, 8.0);
}

qreal distanceToSegment(const QPointF &point, const QPointF &start,
                        const QPointF &end) {
  const QPointF delta = end - start;
  const qreal lengthSquared = QPointF::dotProduct(delta, delta);
  if (lengthSquared <= 0.001)
    return QLineF(point, start).length();
  const qreal t = std::clamp(
      QPointF::dotProduct(point - start, delta) / lengthSquared, 0.0, 1.0);
  return QLineF(point, start + delta * t).length();
}

struct PolylineSample {
  QPointF position;
  QPointF tangent;
};

std::optional<PolylineSample> samplePolyline(const QVector<QPointF> &points,
                                             qreal distanceFromStart) {
  if (points.size() < 2)
    return std::nullopt;

  qreal totalLength = 0.0;
  for (qsizetype index = 1; index < points.size(); ++index)
    totalLength += QLineF(points.at(index - 1), points.at(index)).length();
  if (qFuzzyIsNull(totalLength))
    return std::nullopt;

  const qreal requested = std::clamp(distanceFromStart, 0.0, totalLength);
  qreal traversed = 0.0;
  for (qsizetype index = 1; index < points.size(); ++index) {
    const QPointF start = points.at(index - 1);
    const QPointF end = points.at(index);
    const qreal segmentLength = QLineF(start, end).length();
    if (segmentLength <= 0.001)
      continue;
    if (traversed + segmentLength >= requested || index + 1 == points.size()) {
      const qreal fraction =
          std::clamp((requested - traversed) / segmentLength, 0.0, 1.0);
      return PolylineSample{start + (end - start) * fraction,
                            (end - start) / segmentLength};
    }
    traversed += segmentLength;
  }
  return std::nullopt;
}

struct PolylineProjection {
  QPointF position;
  QPointF tangent;
  qreal normalizedDistance = 0.0;
};

std::optional<PolylineProjection>
projectOntoPolyline(const QVector<QPointF> &points, const QPointF &point) {
  qreal totalLength = 0.0;
  for (qsizetype index = 1; index < points.size(); ++index)
    totalLength += QLineF(points.at(index - 1), points.at(index)).length();
  if (points.size() < 2 || qFuzzyIsNull(totalLength))
    return std::nullopt;

  qreal bestDistance = std::numeric_limits<qreal>::max();
  qreal bestRouteDistance = 0.0;
  QPointF bestPosition;
  QPointF bestTangent;
  qreal traversed = 0.0;
  for (qsizetype index = 1; index < points.size(); ++index) {
    const QPointF start = points.at(index - 1);
    const QPointF end = points.at(index);
    const QPointF delta = end - start;
    const qreal segmentLength = QLineF(start, end).length();
    if (segmentLength <= 0.001)
      continue;
    const qreal fraction =
        std::clamp(QPointF::dotProduct(point - start, delta) /
                       (segmentLength * segmentLength),
                   0.0, 1.0);
    const QPointF projected = start + delta * fraction;
    const qreal distance = QLineF(point, projected).length();
    if (distance < bestDistance) {
      bestDistance = distance;
      bestPosition = projected;
      bestTangent = delta / segmentLength;
      bestRouteDistance = traversed + segmentLength * fraction;
    }
    traversed += segmentLength;
  }
  if (bestDistance == std::numeric_limits<qreal>::max())
    return std::nullopt;
  return PolylineProjection{bestPosition, bestTangent,
                            bestRouteDistance / totalLength};
}

qreal polylineLength(const QVector<QPointF> &points) {
  qreal result = 0.0;
  for (qsizetype index = 1; index < points.size(); ++index)
    result += QLineF(points.at(index - 1), points.at(index)).length();
  return result;
}

QRectF annotationTextRect(const QPointF &center, const QString &text,
                          const QFont &font, qreal height = 20.0) {
  const QFontMetricsF metrics(font);
  const qreal width = std::max(24.0, metrics.horizontalAdvance(text) + 10.0);
  return {center.x() - width / 2.0, center.y() - height / 2.0, width, height};
}

struct ConnectorAnnotationLayout {
  QRectF name;
  QRectF stereotype;
  QRectF sourceRole;
  QRectF sourceMultiplicity;
  QRectF targetRole;
  QRectF targetMultiplicity;
};

QRectF routeRelativeAnnotationRect(
    const QVector<QPointF> &points, const QString &text, const QFont &font,
    qreal defaultRoutePosition, qreal defaultTangentOffset,
    qreal defaultNormalOffset,
    const std::optional<ConnectorAnnotationPlacement> &manualPlacement,
    qreal height = 20.0) {
  const qreal length = polylineLength(points);
  if (text.isEmpty() || qFuzzyIsNull(length))
    return {};
  const qreal routePosition =
      manualPlacement ? manualPlacement->routePosition : defaultRoutePosition;
  const auto sample = samplePolyline(points, routePosition * length);
  if (!sample)
    return {};
  const QPointF normal(-sample->tangent.y(), sample->tangent.x());
  const qreal tangentOffset =
      manualPlacement ? manualPlacement->tangentOffset : defaultTangentOffset;
  const qreal normalOffset =
      manualPlacement ? manualPlacement->normalOffset : defaultNormalOffset;
  return annotationTextRect(sample->position + sample->tangent * tangentOffset +
                                normal * normalOffset,
                            text, font, height);
}

std::optional<ConnectorAnnotationPlacement> manualAnnotationPlacement(
    const QHash<QString, ConnectorAnnotationPlacement> &placements,
    const QString &key) {
  const auto placement = placements.constFind(key);
  return placement == placements.cend()
             ? std::nullopt
             : std::optional<ConnectorAnnotationPlacement>(*placement);
}

// Automatic and manual labels share one route-relative representation. The
// defaults keep endpoint metadata separated while manual overrides survive
// straight/orthogonal changes and arbitrary bend-point edits.
ConnectorAnnotationLayout connectorAnnotationLayout(
    const QVector<QPointF> &points, const QString &name,
    const QString &stereotype, const RelationshipEnd &sourceEnd,
    const RelationshipEnd &targetEnd,
    const QHash<QString, ConnectorAnnotationPlacement> &annotationPlacements,
    const QFont &font) {
  ConnectorAnnotationLayout layout;
  if (points.size() < 2)
    return layout;

  const qreal length = polylineLength(points);
  if (qFuzzyIsNull(length))
    return layout;
  layout.name = routeRelativeAnnotationRect(
      points, name, font, 0.5, 0.0, stereotype.isEmpty() ? 0.0 : 11.0,
      manualAnnotationPlacement(annotationPlacements, QStringLiteral("name")),
      24.0);
  layout.stereotype = routeRelativeAnnotationRect(
      points, stereotype, font, 0.5, 0.0, name.isEmpty() ? 0.0 : -13.0,
      manualAnnotationPlacement(annotationPlacements,
                                QStringLiteral("stereotype")));

  const qreal endpointRoutePosition = std::min(38.0, length * 0.25) / length;
  constexpr qreal kNormalOffset = 14.0;
  layout.sourceRole = routeRelativeAnnotationRect(
      points, sourceEnd.role, font, endpointRoutePosition, 0.0, kNormalOffset,
      manualAnnotationPlacement(annotationPlacements,
                                QStringLiteral("sourceRole")));
  layout.sourceMultiplicity = routeRelativeAnnotationRect(
      points, sourceEnd.multiplicity, font, endpointRoutePosition, 0.0,
      -kNormalOffset,
      manualAnnotationPlacement(annotationPlacements,
                                QStringLiteral("sourceMultiplicity")));
  layout.targetRole = routeRelativeAnnotationRect(
      points, targetEnd.role, font, 1.0 - endpointRoutePosition, 0.0,
      kNormalOffset,
      manualAnnotationPlacement(annotationPlacements,
                                QStringLiteral("targetRole")));
  layout.targetMultiplicity = routeRelativeAnnotationRect(
      points, targetEnd.multiplicity, font, 1.0 - endpointRoutePosition, 0.0,
      -kNormalOffset,
      manualAnnotationPlacement(annotationPlacements,
                                QStringLiteral("targetMultiplicity")));
  return layout;
}

QPointF edgePointToward(const QRectF &rect, const QPointF &target) {
  const QPointF center = rect.center();
  const QPointF direction = target - center;
  if (qFuzzyIsNull(direction.x()) && qFuzzyIsNull(direction.y()))
    return center;

  const qreal horizontalScale =
      qFuzzyIsNull(direction.x())
          ? std::numeric_limits<qreal>::max()
          : (rect.width() / 2.0) / std::abs(direction.x());
  const qreal verticalScale =
      qFuzzyIsNull(direction.y())
          ? std::numeric_limits<qreal>::max()
          : (rect.height() / 2.0) / std::abs(direction.y());
  return center + direction * std::min(horizontalScale, verticalScale);
}

QPointF connectorAnchorPoint(const QRectF &rect, const ConnectorAnchor &anchor,
                             const QPointF &automaticTarget) {
  const qreal offset = std::clamp(anchor.offset, 0.0, 1.0);
  switch (anchor.side) {
  case ConnectorSide::Automatic:
    return edgePointToward(rect, automaticTarget);
  case ConnectorSide::Top:
    return {rect.left() + rect.width() * offset, rect.top()};
  case ConnectorSide::Right:
    return {rect.right(), rect.top() + rect.height() * offset};
  case ConnectorSide::Bottom:
    return {rect.left() + rect.width() * offset, rect.bottom()};
  case ConnectorSide::Left:
    return {rect.left(), rect.top() + rect.height() * offset};
  }
  return edgePointToward(rect, automaticTarget);
}

ConnectorAnchor anchorAtPerimeterPoint(const QRectF &rect,
                                       const QPointF &point) {
  ConnectorAnchor anchor;
  qreal nearestDistance = std::abs(point.y() - rect.top());
  anchor.side = ConnectorSide::Top;
  anchor.offset = (point.x() - rect.left()) / rect.width();

  const auto consider = [&](qreal distance, ConnectorSide side, qreal offset) {
    if (distance < nearestDistance) {
      nearestDistance = distance;
      anchor.side = side;
      anchor.offset = offset;
    }
  };
  consider(std::abs(point.x() - rect.right()), ConnectorSide::Right,
           (point.y() - rect.top()) / rect.height());
  consider(std::abs(point.y() - rect.bottom()), ConnectorSide::Bottom,
           (point.x() - rect.left()) / rect.width());
  consider(std::abs(point.x() - rect.left()), ConnectorSide::Left,
           (point.y() - rect.top()) / rect.height());
  anchor.offset = std::clamp(anchor.offset, 0.0, 1.0);
  return anchor;
}

ConnectorAnchor anchorOnPerimeterSide(const QRectF &rect, const QPointF &point,
                                      ConnectorSide side) {
  ConnectorAnchor anchor;
  anchor.side = side;
  switch (side) {
  case ConnectorSide::Top:
  case ConnectorSide::Bottom:
    anchor.offset = (point.x() - rect.left()) / rect.width();
    break;
  case ConnectorSide::Left:
  case ConnectorSide::Right:
    anchor.offset = (point.y() - rect.top()) / rect.height();
    break;
  case ConnectorSide::Automatic:
    return anchorAtPerimeterPoint(rect, point);
  }
  anchor.offset = std::clamp(anchor.offset, 0.0, 1.0);
  return anchor;
}

qreal distanceToPerimeterSide(const QRectF &rect, const QPointF &point,
                              ConnectorSide side) {
  switch (side) {
  case ConnectorSide::Top:
    return std::abs(point.y() - rect.top());
  case ConnectorSide::Right:
    return std::abs(point.x() - rect.right());
  case ConnectorSide::Bottom:
    return std::abs(point.y() - rect.bottom());
  case ConnectorSide::Left:
    return std::abs(point.x() - rect.left());
  case ConnectorSide::Automatic:
    return std::numeric_limits<qreal>::max();
  }
  return std::numeric_limits<qreal>::max();
}

ConnectorAnchor snappedAnchorAtPerimeterPoint(const NodePresentation &node,
                                              const QRectF &rect,
                                              const QPointF &point,
                                              qreal tolerance,
                                              bool *snapped = nullptr) {
  ConnectorAnchor anchor = anchorAtPerimeterPoint(rect, point);
  const bool horizontalSide =
      anchor.side == ConnectorSide::Top || anchor.side == ConnectorSide::Bottom;
  const qreal sideLength = horizontalSide ? rect.width() : rect.height();
  anchor.offset = connector_ports::snapOffset(
      anchor.offset, sideLength,
      connector_ports::snapPointCountForSide(node, anchor.side), tolerance,
      snapped);
  return anchor;
}

void appendPortSnapPoints(SceneSnapshot &snapshot, const NodePresentation &node,
                          const QRectF &rect,
                          const std::optional<ConnectorAnchor> &activeAnchor,
                          const QRectF &clipRect = {}, bool hasClip = false) {
  constexpr ConnectorSide sides[] = {ConnectorSide::Top, ConnectorSide::Right,
                                     ConnectorSide::Bottom,
                                     ConnectorSide::Left};
  for (const ConnectorSide side : sides) {
    const int count = connector_ports::snapPointCountForSide(node, side);
    for (const qreal offset : connector_ports::snapOffsets(count)) {
      const ConnectorAnchor anchor{side, offset};
      const bool active = activeAnchor && activeAnchor->side == side &&
                          std::abs(activeAnchor->offset - offset) < 0.000001;
      snapshot.portSnapPoints.append(
          {connectorAnchorPoint(rect, anchor, rect.center()), active, clipRect,
           hasClip});
    }
  }
}

bool nearRectanglePerimeter(const QRectF &rect, const QPointF &point,
                            qreal tolerance) {
  if (!rect.adjusted(-tolerance, -tolerance, tolerance, tolerance)
           .contains(point))
    return false;
  return std::min({std::abs(point.x() - rect.left()),
                   std::abs(point.x() - rect.right()),
                   std::abs(point.y() - rect.top()),
                   std::abs(point.y() - rect.bottom())}) <= tolerance;
}

const QColor &elementColor(ElementType type, const ui::UiPalette &palette) {
  switch (type) {
  case ElementType::Package:
    return palette.packageFill;
  case ElementType::Class:
    return palette.classFill;
  case ElementType::Struct:
    return palette.structFill;
  case ElementType::Enumeration:
    return palette.enumerationFill;
  }
  return palette.surface;
}

RenderElementStyle renderElementStyle(const DiagramStyle *style) {
  if (!style)
    return {};
  return {true,
          QColor(style->fill),
          QColor(style->headerFill),
          QColor(style->border),
          QColor(style->primaryText),
          QColor(style->secondaryText),
          QColor(style->divider)};
}

void appendVertex(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                  const QPointF &point, const QColor &color) {
  QSGGeometry::ColoredPoint2D vertex;
  vertex.set(static_cast<float>(point.x()), static_cast<float>(point.y()),
             static_cast<uchar>(color.red()), static_cast<uchar>(color.green()),
             static_cast<uchar>(color.blue()),
             static_cast<uchar>(color.alpha()));
  vertices.append(vertex);
}

void appendTriangle(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                    const QPointF &a, const QPointF &b, const QPointF &c,
                    const QColor &color) {
  appendVertex(vertices, a, color);
  appendVertex(vertices, b, color);
  appendVertex(vertices, c, color);
}

void appendRect(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                const QRectF &rect, const QColor &color) {
  if (rect.isEmpty())
    return;
  appendTriangle(vertices, rect.topLeft(), rect.bottomLeft(), rect.topRight(),
                 color);
  appendTriangle(vertices, rect.topRight(), rect.bottomLeft(),
                 rect.bottomRight(), color);
}

void appendClippedRect(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                       const QRectF &rect, const QColor &color,
                       const QRectF &clipRect, bool hasClip) {
  appendRect(vertices, hasClip ? rect.intersected(clipRect) : rect, color);
}

void appendLine(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                const QPointF &start, const QPointF &end, qreal width,
                const QColor &color) {
  const QPointF delta = end - start;
  const qreal length = std::hypot(delta.x(), delta.y());
  if (length <= 0.001)
    return;
  const QPointF normal(-delta.y() / length * width / 2.0,
                       delta.x() / length * width / 2.0);
  appendTriangle(vertices, start + normal, start - normal, end + normal, color);
  appendTriangle(vertices, end + normal, start - normal, end - normal, color);
}

void appendClippedAxisLine(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                           const QPointF &start, const QPointF &end,
                           qreal width, const QColor &color,
                           const QRectF &clipRect, bool hasClip) {
  if (!hasClip) {
    appendLine(vertices, start, end, width, color);
    return;
  }

  const qreal halfWidth = width / 2.0;
  QRectF stroke;
  if (qFuzzyCompare(start.y(), end.y())) {
    stroke = QRectF(std::min(start.x(), end.x()), start.y() - halfWidth,
                    std::abs(end.x() - start.x()), width);
  } else if (qFuzzyCompare(start.x(), end.x())) {
    stroke = QRectF(start.x() - halfWidth, std::min(start.y(), end.y()), width,
                    std::abs(end.y() - start.y()));
  } else {
    // Container and node chrome only uses axis-aligned segments. Retain the
    // ordinary line as a defensive fallback if that invariant changes.
    appendLine(vertices, start, end, width, color);
    return;
  }
  appendRect(vertices, stroke.intersected(clipRect), color);
}

void appendAntialiasedLine(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                           const QPointF &start, const QPointF &end,
                           qreal width, qreal featherWidth,
                           const QColor &color) {
  const QPointF delta = end - start;
  const qreal length = std::hypot(delta.x(), delta.y());
  if (length <= 0.001)
    return;

  const QPointF unitNormal(-delta.y() / length, delta.x() / length);
  const qreal innerHalfWidth = std::max(0.0, (width - featherWidth) / 2.0);
  const qreal outerHalfWidth = innerHalfWidth + featherWidth;
  const QPointF innerNormal = unitNormal * innerHalfWidth;
  const QPointF outerNormal = unitNormal * outerHalfWidth;
  const QColor transparent(Qt::transparent);

  // The two outer bands interpolate from opaque to transparent over one view
  // pixel. This preserves batching while providing backend-independent edge
  // coverage for thin diagonal connector geometry.
  appendVertex(vertices, start + outerNormal, transparent);
  appendVertex(vertices, start + innerNormal, color);
  appendVertex(vertices, end + outerNormal, transparent);
  appendVertex(vertices, end + outerNormal, transparent);
  appendVertex(vertices, start + innerNormal, color);
  appendVertex(vertices, end + innerNormal, color);

  appendVertex(vertices, start + innerNormal, color);
  appendVertex(vertices, start - innerNormal, color);
  appendVertex(vertices, end + innerNormal, color);
  appendVertex(vertices, end + innerNormal, color);
  appendVertex(vertices, start - innerNormal, color);
  appendVertex(vertices, end - innerNormal, color);

  appendVertex(vertices, start - innerNormal, color);
  appendVertex(vertices, start - outerNormal, transparent);
  appendVertex(vertices, end - innerNormal, color);
  appendVertex(vertices, end - innerNormal, color);
  appendVertex(vertices, start - outerNormal, transparent);
  appendVertex(vertices, end - outerNormal, transparent);
}

void appendDashedLine(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                      const QPointF &start, const QPointF &end, qreal width,
                      qreal featherWidth, const QColor &color, qreal zoom) {
  const QPointF delta = end - start;
  const qreal length = std::hypot(delta.x(), delta.y());
  if (length <= 0.001)
    return;
  const QPointF unit = delta / length;
  constexpr qreal kMaximumDashCount = 64.0;
  const qreal period = std::max(13.0 / zoom, length / kMaximumDashCount);
  const qreal dash = period * (8.0 / 13.0);
  const qreal gap = period - dash;
  for (qreal offset = 0.0; offset < length; offset += dash + gap) {
    const qreal segmentEnd = std::min(offset + dash, length);
    appendAntialiasedLine(vertices, start + unit * offset,
                          start + unit * segmentEnd, width, featherWidth,
                          color);
  }
}

void appendAntialiasedTriangle(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                               const QPointF &a, const QPointF &b,
                               const QPointF &c, qreal featherWidth,
                               const QColor &color) {
  appendTriangle(vertices, a, b, c, color);
  const QPointF center = (a + b + c) / 3.0;
  const auto expanded = [&](const QPointF &point) {
    const QPointF direction = point - center;
    const qreal length = std::hypot(direction.x(), direction.y());
    return length > 0.001 ? point + direction / length * featherWidth : point;
  };
  const QPointF outerA = expanded(a);
  const QPointF outerB = expanded(b);
  const QPointF outerC = expanded(c);
  const QColor transparent(Qt::transparent);
  const auto appendFeather = [&](const QPointF &first, const QPointF &second,
                                 const QPointF &outerFirst,
                                 const QPointF &outerSecond) {
    appendVertex(vertices, first, color);
    appendVertex(vertices, outerFirst, transparent);
    appendVertex(vertices, second, color);
    appendVertex(vertices, second, color);
    appendVertex(vertices, outerFirst, transparent);
    appendVertex(vertices, outerSecond, transparent);
  };
  appendFeather(a, b, outerA, outerB);
  appendFeather(b, c, outerB, outerC);
  appendFeather(c, a, outerC, outerA);
}

void appendRelationshipDecoration(
    QVector<QSGGeometry::ColoredPoint2D> &vertices,
    ui::RelationshipDecoration decoration, const QPointF &tip,
    const QPointF &adjacentRoutePoint, qreal zoom, const QColor &color,
    const QColor &background) {
  if (decoration == ui::RelationshipDecoration::None)
    return;
  const QPointF direction = adjacentRoutePoint - tip;
  const qreal length = std::hypot(direction.x(), direction.y());
  if (length <= 1.0)
    return;

  const QPointF unit = direction / length;
  const QPointF normal(-unit.y(), unit.x());
  const qreal feather = 1.0 / zoom;
  if (decoration == ui::RelationshipDecoration::OpenArrow ||
      decoration == ui::RelationshipDecoration::HollowTriangle) {
    const QPointF first = tip + unit * (12.0 / zoom) + normal * (5.0 / zoom);
    const QPointF second = tip + unit * (12.0 / zoom) - normal * (5.0 / zoom);
    if (decoration == ui::RelationshipDecoration::HollowTriangle) {
      appendAntialiasedTriangle(vertices, tip, first, second, feather,
                                background);
      appendAntialiasedLine(vertices, first, second, 1.5 / zoom, feather,
                            color);
    }
    appendAntialiasedLine(vertices, tip, first, 1.5 / zoom, feather, color);
    appendAntialiasedLine(vertices, tip, second, 1.5 / zoom, feather, color);
    return;
  }

  if (decoration == ui::RelationshipDecoration::CirclePlus) {
    constexpr int kSegments = 20;
    constexpr qreal kTwoPi = 6.28318530717958647692;
    const qreal radius = 6.0 / zoom;
    const QPointF center = tip + unit * (7.0 / zoom);
    QPointF previous = center + QPointF(std::cos(0.0), std::sin(0.0)) * radius;
    for (int index = 1; index <= kSegments; ++index) {
      const qreal angle = kTwoPi * index / kSegments;
      const QPointF current =
          center + QPointF(std::cos(angle), std::sin(angle)) * radius;
      appendTriangle(vertices, center, previous, current, background);
      appendAntialiasedLine(vertices, previous, current, 1.5 / zoom, feather,
                            color);
      previous = current;
    }
    const qreal arm = 3.25 / zoom;
    appendAntialiasedLine(vertices, center - unit * arm, center + unit * arm,
                          1.5 / zoom, feather, color);
    appendAntialiasedLine(vertices, center - normal * arm,
                          center + normal * arm, 1.5 / zoom, feather, color);
    return;
  }

  const QPointF first = tip + unit * (8.0 / zoom) + normal * (5.5 / zoom);
  const QPointF far = tip + unit * (16.0 / zoom);
  const QPointF second = tip + unit * (8.0 / zoom) - normal * (5.5 / zoom);
  const QColor fill = decoration == ui::RelationshipDecoration::FilledDiamond
                          ? color
                          : background;
  appendTriangle(vertices, tip, first, second, fill);
  appendTriangle(vertices, first, far, second, fill);
  appendAntialiasedLine(vertices, tip, first, 1.5 / zoom, feather, color);
  appendAntialiasedLine(vertices, first, far, 1.5 / zoom, feather, color);
  appendAntialiasedLine(vertices, far, second, 1.5 / zoom, feather, color);
  appendAntialiasedLine(vertices, second, tip, 1.5 / zoom, feather, color);
}

void appendBorder(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                  const QRectF &rect, qreal width, const QColor &color) {
  appendRect(vertices, {rect.left(), rect.top(), rect.width(), width}, color);
  appendRect(vertices,
             {rect.left(), rect.bottom() - width, rect.width(), width}, color);
  appendRect(vertices, {rect.left(), rect.top(), width, rect.height()}, color);
  appendRect(vertices, {rect.right() - width, rect.top(), width, rect.height()},
             color);
}

void appendClippedBorder(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                         const QRectF &rect, qreal width, const QColor &color,
                         const QRectF &clipRect, bool hasClip) {
  appendClippedRect(vertices, {rect.left(), rect.top(), rect.width(), width},
                    color, clipRect, hasClip);
  appendClippedRect(vertices,
                    {rect.left(), rect.bottom() - width, rect.width(), width},
                    color, clipRect, hasClip);
  appendClippedRect(vertices, {rect.left(), rect.top(), width, rect.height()},
                    color, clipRect, hasClip);
  appendClippedRect(vertices,
                    {rect.right() - width, rect.top(), width, rect.height()},
                    color, clipRect, hasClip);
}

void appendContainerOutline(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                            const RenderContainer &container, qreal zoom,
                            const ui::UiPalette &palette) {
  const QColor border = container.selected           ? palette.accent
                        : container.style.customized ? container.style.border
                                                     : palette.containerBorder;
  const qreal borderWidth = (container.selected ? 3.0 : 1.2) / zoom;
  if (container.package) {
    const qreal tabHeight = 24.0;
    const qreal tabWidth =
        std::clamp(container.titleWidth, 90.0, container.rect.width());
    const QPointF topLeft = container.rect.topLeft();
    const QPointF tabTopRight(container.rect.left() + tabWidth,
                              container.rect.top());
    const QPointF tabBottomRight(container.rect.left() + tabWidth,
                                 container.rect.top() + tabHeight);
    const QPointF bodyTopRight(container.rect.right(),
                               container.rect.top() + tabHeight);
    appendClippedAxisLine(vertices, topLeft,
                          QPointF(tabTopRight.x(), topLeft.y()), borderWidth,
                          border, container.clipRect, container.hasClip);
    appendClippedAxisLine(vertices, tabTopRight, tabBottomRight, borderWidth,
                          border, container.clipRect, container.hasClip);
    appendClippedAxisLine(vertices, tabBottomRight, bodyTopRight, borderWidth,
                          border, container.clipRect, container.hasClip);
    appendClippedAxisLine(vertices, bodyTopRight, container.rect.bottomRight(),
                          borderWidth, border, container.clipRect,
                          container.hasClip);
    appendClippedAxisLine(vertices, container.rect.bottomRight(),
                          container.rect.bottomLeft(), borderWidth, border,
                          container.clipRect, container.hasClip);
    appendClippedAxisLine(vertices, container.rect.bottomLeft(), topLeft,
                          borderWidth, border, container.clipRect,
                          container.hasClip);
  } else {
    appendClippedBorder(vertices, container.rect, borderWidth, border,
                        container.clipRect, container.hasClip);
    appendClippedAxisLine(
        vertices,
        QPointF(container.rect.left(),
                container.rect.top() + kContainerHeaderHeight),
        QPointF(container.rect.right(),
                container.rect.top() + kContainerHeaderHeight),
        1.0 / zoom, border, container.clipRect, container.hasClip);
  }

  if (container.selected) {
    const qreal handle = 9.0 / zoom;
    const std::array<QRectF, 4> handles = {
        QRectF(container.rect.left(), container.rect.top(), handle, handle),
        QRectF(container.rect.right() - handle, container.rect.top(), handle,
               handle),
        QRectF(container.rect.left(), container.rect.bottom() - handle, handle,
               handle),
        QRectF(container.rect.right() - handle,
               container.rect.bottom() - handle, handle, handle)};
    for (const QRectF &handleRect : handles)
      appendClippedRect(vertices, handleRect, palette.accent,
                        container.clipRect, container.hasClip);
  }
}

QSGGeometryNode *
createColoredNode(const QVector<QSGGeometry::ColoredPoint2D> &vertices) {
  auto *geometry = new QSGGeometry(
      QSGGeometry::defaultAttributes_ColoredPoint2D(), vertices.size());
  geometry->setDrawingMode(QSGGeometry::DrawTriangles);
  std::copy(vertices.cbegin(), vertices.cend(),
            geometry->vertexDataAsColoredPoint2D());

  auto *material = new QSGVertexColorMaterial;
  material->setFlag(QSGMaterial::Blending, true);

  auto *node = new QSGGeometryNode;
  node->setGeometry(geometry);
  node->setFlag(QSGNode::OwnsGeometry);
  node->setMaterial(material);
  node->setFlag(QSGNode::OwnsMaterial);
  return node;
}

QSGNode *
createColoredNodeBatches(const QVector<QSGGeometry::ColoredPoint2D> &vertices) {
  auto *group = new QSGNode;
  for (const auto &batch : ui::triangleBatches(vertices.size())) {
    auto *geometry =
        new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(),
                        static_cast<int>(batch.vertexCount));
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    std::copy_n(vertices.cbegin() + batch.firstVertex, batch.vertexCount,
                geometry->vertexDataAsColoredPoint2D());

    auto *material = new QSGVertexColorMaterial;
    material->setFlag(QSGMaterial::Blending, true);
    auto *node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    group->appendChildNode(node);
  }
  return group;
}

qreal adaptiveGridStep(qreal zoom, qreal baseStep) {
  constexpr qreal kMinimumViewSpacing = 16.0;
  qreal step = baseStep;
  while (step * zoom < kMinimumViewSpacing)
    step *= 2.0;
  return step;
}

qreal snapToPhysicalPixelCenter(qreal position, qreal devicePixelRatio) {
  return (std::floor(position * devicePixelRatio) + 0.5) / devicePixelRatio;
}

QSGGeometryNode *buildGridGeometry(const QSizeF &viewportSize,
                                   const QPointF &pan, qreal zoom,
                                   qreal devicePixelRatio, qreal baseStep) {
  QVector<QSGGeometry::ColoredPoint2D> vertices;
  if (viewportSize.isEmpty() || zoom <= 0.0 || devicePixelRatio <= 0.0)
    return createColoredNode(vertices);

  const qreal sceneStep = adaptiveGridStep(zoom, baseStep);
  const qreal left = -pan.x() / zoom;
  const qreal right = (viewportSize.width() - pan.x()) / zoom;
  const qreal top = -pan.y() / zoom;
  const qreal bottom = (viewportSize.height() - pan.y()) / zoom;
  const qint64 firstColumn = static_cast<qint64>(std::ceil(left / sceneStep));
  const qint64 lastColumn = static_cast<qint64>(std::floor(right / sceneStep));
  const qint64 firstRow = static_cast<qint64>(std::ceil(top / sceneStep));
  const qint64 lastRow = static_cast<qint64>(std::floor(bottom / sceneStep));

  const qreal pixelWidth = 1.0 / devicePixelRatio;
  const qreal halfPixel = pixelWidth / 2.0;
  const QColor &color = ui::uiPalette().canvasGrid;
  vertices.reserve(static_cast<qsizetype>(
      std::max<qint64>(0, lastColumn - firstColumn + 1) * 6 +
      std::max<qint64>(0, lastRow - firstRow + 1) * 6));

  for (qint64 column = firstColumn; column <= lastColumn; ++column) {
    const qreal viewX = column * sceneStep * zoom + pan.x();
    const qreal x = snapToPhysicalPixelCenter(viewX, devicePixelRatio);
    appendRect(vertices,
               {x - halfPixel, 0.0, pixelWidth, viewportSize.height()}, color);
  }
  for (qint64 row = firstRow; row <= lastRow; ++row) {
    const qreal viewY = row * sceneStep * zoom + pan.y();
    const qreal y = snapToPhysicalPixelCenter(viewY, devicePixelRatio);
    appendRect(vertices, {0.0, y - halfPixel, viewportSize.width(), pixelWidth},
               color);
  }
  return createColoredNode(vertices);
}

QString textSignature(const RenderText &entry) {
  return entry.text + QChar(0x1f) + entry.font.toString() + QChar(0x1f) +
         entry.color.name(QColor::HexArgb) + QChar(0x1f) +
         QString::number(static_cast<int>(entry.alignment)) + QChar(0x1f) +
         QString::number(qCeil(entry.target.width())) + u'x' +
         QString::number(qCeil(entry.target.height()));
}

QString textLayoutSignature(const RenderText &entry) {
  QString result = textSignature(entry);
  for (const QRectF &fragment : entry.visibleFragments) {
    // Store fragment coordinates relative to the text target. Absolute scene
    // movement can then use the cheap atlas relayout path.
    result += QChar(0x1e) +
              QString::number(fragment.left() - entry.target.left(), 'g', 12) +
              u',' +
              QString::number(fragment.top() - entry.target.top(), 'g', 12) +
              u',' + QString::number(fragment.width(), 'g', 12) + u',' +
              QString::number(fragment.height(), 'g', 12);
  }
  return result;
}

struct AtlasPlacement {
  int entryIndex = -1;
  QRectF source;
  QRectF relativeTarget;
};

struct AtlasPaintCommand {
  QRect rect;
  int entryIndex = -1;
};

struct AtlasPageData {
  QImage image = QImage(kTextAtlasSize, kTextAtlasSize,
                        QImage::Format_RGBA8888_Premultiplied);
  QVector<AtlasPlacement> placements;
  QVector<AtlasPaintCommand> paintCommands;
  int cursorX = kTextAtlasPadding;
  int cursorY = kTextAtlasPadding;
  int rowHeight = 0;

  AtlasPageData() { image.fill(Qt::transparent); }
};

class OwnedTextureMaterial final : public QSGTextureMaterial {
public:
  explicit OwnedTextureMaterial(QSGTexture *texture) : m_texture(texture) {
    setTexture(texture);
    setFiltering(QSGTexture::Linear);
    setFlag(QSGMaterial::Blending, true);
  }

  ~OwnedTextureMaterial() override { delete m_texture; }

private:
  QSGTexture *m_texture = nullptr;
};

class TextAtlasPageNode final : public QSGGeometryNode {
public:
  TextAtlasPageNode(QSGTexture *texture, QVector<AtlasPlacement> placements,
                    const QVector<RenderText> &entries)
      : m_placements(std::move(placements)) {
    auto *geometry =
        new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(),
                        m_placements.size() * 6);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    setGeometry(geometry);
    setFlag(QSGNode::OwnsGeometry);
    setMaterial(new OwnedTextureMaterial(texture));
    setFlag(QSGNode::OwnsMaterial);
    updateTargets(entries);
  }

  void updateTargets(const QVector<RenderText> &entries) {
    auto *vertices = geometry()->vertexDataAsTexturedPoint2D();
    int offset = 0;
    for (const auto &placement : m_placements) {
      const QRectF entryTarget = entries.at(placement.entryIndex).target;
      const QRectF target(entryTarget.left() + placement.relativeTarget.left(),
                          entryTarget.top() + placement.relativeTarget.top(),
                          placement.relativeTarget.width(),
                          placement.relativeTarget.height());
      const QRectF source = placement.source;
      const auto set = [&](int index, qreal x, qreal y, qreal u, qreal v) {
        vertices[offset + index].set(
            static_cast<float>(x), static_cast<float>(y), static_cast<float>(u),
            static_cast<float>(v));
      };
      set(0, target.left(), target.top(), source.left(), source.top());
      set(1, target.left(), target.bottom(), source.left(), source.bottom());
      set(2, target.right(), target.top(), source.right(), source.top());
      set(3, target.right(), target.top(), source.right(), source.top());
      set(4, target.left(), target.bottom(), source.left(), source.bottom());
      set(5, target.right(), target.bottom(), source.right(), source.bottom());
      offset += 6;
    }
    markDirty(QSGNode::DirtyGeometry);
  }

private:
  QVector<AtlasPlacement> m_placements;
};

class TextAtlasGroup final : public QSGNode {
public:
  TextAtlasGroup(const QVector<RenderText> &entries, QQuickWindow *window,
                 qreal rasterScale)
      : m_rasterScale(rasterScale) {
    m_signatures.reserve(entries.size());
    for (const auto &entry : entries)
      m_signatures.append(textLayoutSignature(entry));

    QVector<AtlasPageData> pages;
    QHash<QString, QPair<int, QRectF>> sources;
    for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
      const auto &entry = entries.at(entryIndex);
      // Occlusion changes only the visible geometry. Reuse the same rasterized
      // text when content and styling match, even when its fragments differ.
      const QString contentSignature = textSignature(entry);
      auto source = sources.constFind(contentSignature);
      if (source == sources.cend()) {
        const int width = qMax(1, qCeil(entry.target.width() * m_rasterScale));
        const int height =
            qMax(1, qCeil(entry.target.height() * m_rasterScale));
        if (pages.isEmpty())
          pages.append(AtlasPageData());
        AtlasPageData *page = &pages.last();
        if (page->cursorX + width + kTextAtlasPadding > kTextAtlasSize) {
          page->cursorX = kTextAtlasPadding;
          page->cursorY += page->rowHeight + kTextAtlasPadding;
          page->rowHeight = 0;
        }
        if (page->cursorY + height + kTextAtlasPadding > kTextAtlasSize) {
          pages.append(AtlasPageData());
          page = &pages.last();
        }
        const QRect pixelRect(page->cursorX, page->cursorY, width, height);
        const QRectF normalized(
            static_cast<qreal>(pixelRect.x()) / kTextAtlasSize,
            static_cast<qreal>(pixelRect.y()) / kTextAtlasSize,
            static_cast<qreal>(pixelRect.width()) / kTextAtlasSize,
            static_cast<qreal>(pixelRect.height()) / kTextAtlasSize);
        const int pageIndex = pages.size() - 1;
        sources.insert(contentSignature, qMakePair(pageIndex, normalized));
        pages[pageIndex].paintCommands.append({pixelRect, entryIndex});
        page->cursorX += width + kTextAtlasPadding;
        page->rowHeight = qMax(page->rowHeight, height);
        source = sources.constFind(contentSignature);
      }
      for (const QRectF &fragment : entry.visibleFragments) {
        const qreal leftRatio =
            (fragment.left() - entry.target.left()) / entry.target.width();
        const qreal topRatio =
            (fragment.top() - entry.target.top()) / entry.target.height();
        const qreal widthRatio = fragment.width() / entry.target.width();
        const qreal heightRatio = fragment.height() / entry.target.height();
        const QRectF visibleSource(
            source->second.left() + source->second.width() * leftRatio,
            source->second.top() + source->second.height() * topRatio,
            source->second.width() * widthRatio,
            source->second.height() * heightRatio);
        const QRectF relativeTarget(fragment.left() - entry.target.left(),
                                    fragment.top() - entry.target.top(),
                                    fragment.width(), fragment.height());
        pages[source->first].placements.append(
            {entryIndex, visibleSource, relativeTarget});
      }
    }

    for (auto &page : pages) {
      QPainter painter(&page.image);
      painter.setRenderHint(QPainter::TextAntialiasing, true);
      for (const auto &command : page.paintCommands) {
        const auto &entry = entries.at(command.entryIndex);
        painter.save();
        painter.translate(command.rect.topLeft());
        painter.scale(m_rasterScale, m_rasterScale);
        painter.setFont(entry.font);
        painter.setPen(entry.color);
        painter.drawText(
            QRectF(0, 0, entry.target.width(), entry.target.height()),
            entry.alignment, entry.text);
        painter.restore();
      }
      painter.end();
      auto *texture = window->createTextureFromImage(page.image);
      auto *pageNode =
          new TextAtlasPageNode(texture, std::move(page.placements), entries);
      m_pages.append(pageNode);
      appendChildNode(pageNode);
    }
  }

  bool relayout(const QVector<RenderText> &entries, qreal rasterScale) {
    if (!qFuzzyCompare(rasterScale, m_rasterScale))
      return false;
    if (entries.size() != m_signatures.size())
      return false;
    for (int i = 0; i < entries.size(); ++i)
      if (textLayoutSignature(entries.at(i)) != m_signatures.at(i))
        return false;
    for (auto *page : std::as_const(m_pages))
      page->updateTargets(entries);
    return true;
  }

private:
  qreal m_rasterScale = 1.0;
  QStringList m_signatures;
  QVector<TextAtlasPageNode *> m_pages;
};

class DiagramSceneRoot final : public QSGNode {
public:
  DiagramSceneRoot() {
    m_transform = new QSGTransformNode;
    appendChildNode(m_transform);
  }

  void updateBackground(const QSizeF &size, quint64 themeRevision) {
    if (size == m_backgroundSize && themeRevision == m_backgroundThemeRevision)
      return;
    m_backgroundSize = size;
    m_backgroundThemeRevision = themeRevision;
    if (m_background) {
      removeChildNode(m_background);
      delete m_background;
    }
    QVector<QSGGeometry::ColoredPoint2D> vertices;
    appendRect(vertices, QRectF(QPointF(), size),
               ui::uiPalette().windowBackground);
    m_background = createColoredNode(vertices);
    insertChildNodeBefore(m_background, m_grid ? static_cast<QSGNode *>(m_grid)
                                               : m_transform);
  }

  void updateGrid(const QSizeF &viewportSize, const QPointF &pan, qreal zoom,
                  qreal devicePixelRatio, qreal baseStep,
                  quint64 themeRevision) {
    if (viewportSize == m_gridViewportSize && pan == m_gridPan &&
        qFuzzyCompare(zoom, m_gridZoom) &&
        qFuzzyCompare(devicePixelRatio, m_gridDevicePixelRatio) &&
        qFuzzyCompare(baseStep, m_gridBaseStep) &&
        themeRevision == m_gridThemeRevision)
      return;
    m_gridViewportSize = viewportSize;
    m_gridPan = pan;
    m_gridZoom = zoom;
    m_gridDevicePixelRatio = devicePixelRatio;
    m_gridBaseStep = baseStep;
    m_gridThemeRevision = themeRevision;
    if (m_grid) {
      removeChildNode(m_grid);
      delete m_grid;
    }
    m_grid =
        buildGridGeometry(viewportSize, pan, zoom, devicePixelRatio, baseStep);
    insertChildNodeBefore(m_grid, m_transform);
  }

  void updateTransform(const QPointF &pan, qreal zoom) {
    QMatrix4x4 matrix;
    matrix.translate(static_cast<float>(pan.x()), static_cast<float>(pan.y()));
    matrix.scale(static_cast<float>(zoom), static_cast<float>(zoom));
    m_transform->setMatrix(matrix);
  }

  void replaceGeometry(QSGNode *geometry) {
    if (m_geometry) {
      m_transform->removeChildNode(m_geometry);
      delete m_geometry;
    }
    m_geometry = geometry;
    if (m_text)
      m_transform->insertChildNodeBefore(m_geometry, m_text);
    else
      m_transform->appendChildNode(m_geometry);
  }

  void updateText(const QVector<RenderText> &entries, QQuickWindow *window,
                  qreal rasterScale) {
    if (m_text && m_text->relayout(entries, rasterScale))
      return;
    if (m_text) {
      m_transform->removeChildNode(m_text);
      delete m_text;
      m_text = nullptr;
    }
    if (!entries.isEmpty()) {
      m_text = new TextAtlasGroup(entries, window, rasterScale);
      m_transform->appendChildNode(m_text);
    }
  }

  int renderedDetail = -1;
  quint64 renderedThemeRevision = 0;
  qreal renderedTextScale = 0.0;
  QRectF textCoverage;

private:
  QSizeF m_backgroundSize;
  quint64 m_backgroundThemeRevision = 0;
  QSGGeometryNode *m_background = nullptr;
  QSizeF m_gridViewportSize;
  QPointF m_gridPan;
  qreal m_gridZoom = 0.0;
  qreal m_gridDevicePixelRatio = 0.0;
  qreal m_gridBaseStep = 0.0;
  quint64 m_gridThemeRevision = 0;
  QSGGeometryNode *m_grid = nullptr;
  QSGTransformNode *m_transform = nullptr;
  QSGNode *m_geometry = nullptr;
  TextAtlasGroup *m_text = nullptr;
};

QSGNode *buildSceneGeometry(const SceneSnapshot &snapshot, qreal zoom,
                            int detail) {
  const auto &palette = ui::uiPalette();
  QVector<QSGGeometry::ColoredPoint2D> vertices;
  vertices.reserve(
      snapshot.containers.size() * 32 + snapshot.nodes.size() * 48 +
      snapshot.connectors.size() * 96 + snapshot.portSnapPoints.size() * 12);

  for (const auto &container : snapshot.containers) {
    const QColor fill = container.style.customized ? container.style.fill
                                                   : palette.containerFill;
    const QColor headerFill = container.style.customized
                                  ? container.style.headerFill
                                  : palette.containerHeaderFill;
    if (container.package) {
      const qreal tabHeight = 24.0;
      const qreal tabWidth =
          std::clamp(container.titleWidth, 90.0, container.rect.width());
      const QRectF tab(container.rect.left(), container.rect.top(), tabWidth,
                       tabHeight);
      const QRectF body(container.rect.left(), container.rect.top() + tabHeight,
                        container.rect.width(),
                        container.rect.height() - tabHeight);
      appendClippedRect(vertices, body, fill, container.clipRect,
                        container.hasClip);
      appendClippedRect(vertices, tab, headerFill, container.clipRect,
                        container.hasClip);
    } else {
      appendClippedRect(vertices, container.rect, fill, container.clipRect,
                        container.hasClip);
      appendClippedRect(vertices,
                        {container.rect.left(), container.rect.top(),
                         container.rect.width(), kContainerHeaderHeight},
                        headerFill, container.clipRect, container.hasClip);
    }
  }

  // Container fills are diagram backgrounds for interaction purposes. Draw
  // the translucent selection area above them so a lasso started inside a
  // frame remains as visible as one started on the root canvas.
  if (snapshot.lassoVisible)
    appendRect(vertices, snapshot.lassoRect, palette.selectionOverlay);

  for (const auto &connector : snapshot.connectors) {
    if (connector.points.size() < 2)
      continue;
    const QColor &color = connector.selected || connector.preview
                              ? palette.accent
                              : palette.connector;
    const qreal lineWidth =
        (connector.selected || connector.preview ? 3.0 : 1.5) / zoom;
    const ui::RelationshipVisualStyle style =
        ui::relationshipVisualStyle(connector.type);
    for (qsizetype index = 1; index < connector.points.size(); ++index) {
      const QPointF start = connector.points.at(index - 1);
      const QPointF end = connector.points.at(index);
      if (style.line == ui::RelationshipLineStyle::Dashed)
        appendDashedLine(vertices, start, end, lineWidth, 1.0 / zoom, color,
                         zoom);
      else
        appendAntialiasedLine(vertices, start, end, lineWidth, 1.0 / zoom,
                              color);
    }
    if (detail > 0) {
      appendRelationshipDecoration(
          vertices, style.source, connector.points.constFirst(),
          connector.points.at(1), zoom, color, palette.surface);
      appendRelationshipDecoration(
          vertices, style.target, connector.points.constLast(),
          connector.points.at(connector.points.size() - 2), zoom, color,
          palette.surface);
    }
  }

  for (const auto &node : snapshot.nodes) {
    const QColor fill = node.style.customized
                            ? node.style.fill
                            : elementColor(node.type, palette);
    const QColor headerFill =
        node.style.customized ? node.style.headerFill : fill.darker(104);
    const QColor border = node.selected           ? palette.accent
                          : node.style.customized ? node.style.border
                                                  : palette.nodeBorder;
    const QColor divider =
        node.style.customized ? node.style.divider : palette.compartmentLine;
    const qreal headerHeight = nodeHeaderHeight(node);
    appendClippedRect(vertices, node.rect, fill, node.clipRect, node.hasClip);
    appendClippedRect(
        vertices,
        {node.rect.left(), node.rect.top(), node.rect.width(), headerHeight},
        headerFill, node.clipRect, node.hasClip);
    const qreal borderWidth =
        node.selectionReference ? 4.5 : (node.selected ? 3.0 : 1.2);
    appendClippedBorder(vertices, node.rect, borderWidth / zoom, border,
                        node.clipRect, node.hasClip);
    if (detail > 0) {
      appendClippedAxisLine(
          vertices, QPointF(node.rect.left(), node.rect.top() + headerHeight),
          QPointF(node.rect.right(), node.rect.top() + headerHeight),
          1.0 / zoom, divider, node.clipRect, node.hasClip);
    }
    if (detail == 2 && node.type != ElementType::Enumeration &&
        !node.attributes.isEmpty() && !node.operations.isEmpty()) {
      const qreal y =
          node.rect.top() + headerHeight + node.attributes.size() * kLineHeight;
      if (y <= node.rect.bottom())
        appendClippedAxisLine(vertices, QPointF(node.rect.left(), y),
                              QPointF(node.rect.right(), y), 1.0 / zoom,
                              divider, node.clipRect, node.hasClip);
    }
    if (node.selected) {
      const qreal handle = (node.selectionReference ? 11.0 : 9.0) / zoom;
      const std::array<QRectF, 4> handles = {
          QRectF(node.rect.left(), node.rect.top(), handle, handle),
          QRectF(node.rect.right() - handle, node.rect.top(), handle, handle),
          QRectF(node.rect.left(), node.rect.bottom() - handle, handle, handle),
          QRectF(node.rect.right() - handle, node.rect.bottom() - handle,
                 handle, handle)};
      for (const QRectF &handleRect : handles)
        appendClippedRect(vertices, handleRect, palette.accent, node.clipRect,
                          node.hasClip);
    }
  }

  // Child geometry is clipped just inside the frame, and this final outline
  // pass guarantees that selected/thicker borders remain crisp at every zoom.
  for (const auto &container : snapshot.containers)
    appendContainerOutline(vertices, container, zoom, palette);

  // A short warning-colored segment on an edge signals that direct children
  // continue beyond that side of the container's visible content area.
  for (const auto &container : snapshot.containers) {
    if (!container.overflowEdges)
      continue;
    const qreal headerHeight =
        container.package ? 24.0 : kContainerHeaderHeight;
    const QRectF contentRect(
        container.rect.left(), container.rect.top() + headerHeight,
        container.rect.width(), container.rect.height() - headerHeight);
    const qreal markerLength = 28.0 / zoom;
    const qreal markerThickness = 4.0 / zoom;
    const auto appendMarker = [&](const QRectF &marker) {
      appendClippedRect(vertices, marker, palette.warningBorder,
                        container.clipRect, container.hasClip);
    };
    if (container.overflowEdges.testFlag(ui::ContainerOverflowEdge::Left))
      appendMarker({contentRect.left(),
                    contentRect.center().y() - markerLength / 2.0,
                    markerThickness, markerLength});
    if (container.overflowEdges.testFlag(ui::ContainerOverflowEdge::Right))
      appendMarker({contentRect.right() - markerThickness,
                    contentRect.center().y() - markerLength / 2.0,
                    markerThickness, markerLength});
    if (container.overflowEdges.testFlag(ui::ContainerOverflowEdge::Top))
      appendMarker({contentRect.center().x() - markerLength / 2.0,
                    contentRect.top(), markerLength, markerThickness});
    if (container.overflowEdges.testFlag(ui::ContainerOverflowEdge::Bottom))
      appendMarker({contentRect.center().x() - markerLength / 2.0,
                    contentRect.bottom() - markerThickness, markerLength,
                    markerThickness});
  }

  // Port targets are transient interaction aids. A hollow square is available
  // for snapping; a filled square is the point currently holding the end.
  const qreal snapOuterSize = 8.0 / zoom;
  const qreal snapInnerSize = 4.0 / zoom;
  for (const auto &point : snapshot.portSnapPoints) {
    appendClippedRect(vertices,
                      {point.position.x() - snapOuterSize / 2.0,
                       point.position.y() - snapOuterSize / 2.0, snapOuterSize,
                       snapOuterSize},
                      palette.accent, point.clipRect, point.hasClip);
    appendClippedRect(vertices,
                      {point.position.x() - snapInnerSize / 2.0,
                       point.position.y() - snapInnerSize / 2.0, snapInnerSize,
                       snapInnerSize},
                      point.active ? palette.activeHandleFill : palette.surface,
                      point.clipRect, point.hasClip);
  }
  // Selection handles must be drawn after element fills. Endpoint handles sit
  // on element edges, so drawing them with the connector body would hide half
  // of each handle behind its node and make dragging unnecessarily difficult.
  constexpr qreal kOuterHandleSize = 11.0;
  constexpr qreal kInnerHandleSize = 7.0;
  const qreal outerHandleSize = kOuterHandleSize / zoom;
  const qreal innerHandleSize = kInnerHandleSize / zoom;
  const auto appendHandle = [&](const QPointF &center, bool active) {
    appendRect(vertices,
               {center.x() - outerHandleSize / 2.0,
                center.y() - outerHandleSize / 2.0, outerHandleSize,
                outerHandleSize},
               palette.accent);
    appendRect(vertices,
               {center.x() - innerHandleSize / 2.0,
                center.y() - innerHandleSize / 2.0, innerHandleSize,
                innerHandleSize},
               active ? palette.activeHandleFill : palette.surface);
  };
  for (const auto &connector : snapshot.connectors) {
    if (!connector.selected || connector.points.size() < 2)
      continue;
    appendHandle(connector.points.constFirst(), false);
    for (int bendPoint = 0; bendPoint < connector.bendPointRouteIndices.size();
         ++bendPoint) {
      const int routeIndex = connector.bendPointRouteIndices.at(bendPoint);
      if (routeIndex > 0 && routeIndex + 1 < connector.points.size())
        appendHandle(connector.points.at(routeIndex),
                     bendPoint == connector.selectedBendPoint);
    }
    appendHandle(connector.points.constLast(), false);
  }
  for (const auto &guide : snapshot.alignmentGuides) {
    appendDashedLine(vertices, guide.p1(), guide.p2(), 1.5 / zoom, 1.0 / zoom,
                     palette.alignmentGuide, zoom);
  }
  if (snapshot.lassoVisible)
    appendBorder(vertices, snapshot.lassoRect, 1.5 / zoom, palette.accent);
  return createColoredNodeBatches(vertices);
}

QVector<RenderText> buildTextEntries(const SceneSnapshot &snapshot, int detail,
                                     const QRectF &coverage) {
  const auto &palette = ui::uiPalette();
  QVector<RenderText> entries;
  if (detail == 0)
    return entries;

  const QFont base = QGuiApplication::font();
  QFont header = base;
  header.setBold(true);
  QFont stereotypeFont = base;
  if (stereotypeFont.pointSizeF() > 0.0)
    stereotypeFont.setPointSizeF(
        std::max(7.0, stereotypeFont.pointSizeF() - 2.0));
  const auto appendVisibleText =
      [&](const QRectF &target, const QRectF &ownClip, const QString &text,
          const QFont &font, const QColor &color, Qt::Alignment alignment,
          const QList<QRectF> &occluders) {
        const QRectF initiallyVisible = target.intersected(ownClip);
        const auto fragments =
            ui::visibleRectangleFragments(initiallyVisible, occluders);
        if (!fragments.isEmpty())
          entries.append({target, text, font, color, alignment, fragments});
      };

  QList<QRectF> allNodeRects;
  allNodeRects.reserve(snapshot.nodes.size());
  for (const auto &node : snapshot.nodes) {
    const QRectF visibleRect =
        node.hasClip ? node.rect.intersected(node.clipRect) : node.rect;
    if (!visibleRect.isEmpty())
      allNodeRects.append(visibleRect);
  }

  for (qsizetype containerIndex = 0;
       containerIndex < snapshot.containers.size(); ++containerIndex) {
    const auto &container = snapshot.containers.at(containerIndex);
    if (coverage.isValid() && !coverage.intersects(container.rect))
      continue;
    const qreal titleWidth =
        container.package
            ? std::clamp(container.titleWidth, 90.0, container.rect.width()) -
                  2.0 * kPadding
            : container.rect.width() - 2 * kPadding;
    const bool splitPackageTitle =
        container.package && !container.stereotype.isEmpty();
    const qreal titleHeight = container.package ? 24.0 : kContainerHeaderHeight;
    QRectF clip = container.rect.adjusted(kPadding, 0, -kPadding, 0);
    if (container.hasClip)
      clip = clip.intersected(container.clipRect);
    const QRectF stereotypeTarget(container.rect.left() + kPadding,
                                  container.rect.top(), titleWidth,
                                  splitPackageTitle ? 11.0 : 0.0);
    const QRectF target(container.rect.left() + kPadding,
                        container.rect.top() + (splitPackageTitle ? 10.0 : 0.0),
                        titleWidth,
                        splitPackageTitle ? titleHeight - 10.0 : titleHeight);
    QList<QRectF> occluders = allNodeRects;
    for (qsizetype later = containerIndex + 1;
         later < snapshot.containers.size(); ++later) {
      const auto &laterContainer = snapshot.containers.at(later);
      const QRectF laterVisible =
          laterContainer.hasClip
              ? laterContainer.rect.intersected(laterContainer.clipRect)
              : laterContainer.rect;
      if (laterVisible.intersects(target))
        occluders.append(laterVisible);
    }
    const QColor titleColor = container.style.customized
                                  ? container.style.primaryText
                                  : palette.containerTitleText;
    if (splitPackageTitle)
      appendVisibleText(stereotypeTarget, clip, container.stereotype,
                        stereotypeFont, titleColor, Qt::AlignCenter, occluders);
    appendVisibleText(target, clip, container.name, header, titleColor,
                      Qt::AlignVCenter | Qt::AlignLeft, occluders);
  }

  for (const auto &connector : snapshot.connectors) {
    if (detail != 2 || connector.points.size() < 2)
      continue;
    const ConnectorAnnotationLayout layout = connectorAnnotationLayout(
        connector.points, connector.name, connector.stereotype,
        connector.sourceEnd, connector.targetEnd,
        connector.annotationPlacements, base);
    const auto appendAnnotation = [&](const QRectF &target,
                                      const QString &text) {
      if (!target.isEmpty() &&
          (!coverage.isValid() || coverage.intersects(target)))
        appendVisibleText(target, target, text, base, palette.bodyText,
                          Qt::AlignCenter, allNodeRects);
    };
    appendAnnotation(layout.name, connector.name);
    appendAnnotation(layout.stereotype, connector.stereotype);
    appendAnnotation(layout.sourceRole, connector.sourceEnd.role);
    appendAnnotation(layout.sourceMultiplicity,
                     connector.sourceEnd.multiplicity);
    appendAnnotation(layout.targetRole, connector.targetEnd.role);
    appendAnnotation(layout.targetMultiplicity,
                     connector.targetEnd.multiplicity);
  }

  for (qsizetype nodeIndex = 0; nodeIndex < snapshot.nodes.size();
       ++nodeIndex) {
    const auto &node = snapshot.nodes.at(nodeIndex);
    if (coverage.isValid() && !coverage.intersects(node.rect))
      continue;
    QList<QRectF> laterNodeRects;
    for (qsizetype later = nodeIndex + 1; later < snapshot.nodes.size();
         ++later) {
      const auto &laterNode = snapshot.nodes.at(later);
      const QRectF laterVisible =
          laterNode.hasClip ? laterNode.rect.intersected(laterNode.clipRect)
                            : laterNode.rect;
      if (laterVisible.intersects(node.rect))
        laterNodeRects.append(laterVisible);
    }
    QRectF nodeTextClip = node.rect.adjusted(kPadding, 0, -kPadding, 0);
    if (node.hasClip)
      nodeTextClip = nodeTextClip.intersected(node.clipRect);
    const qreal headerHeight = nodeHeaderHeight(node);
    const QRectF stereotypeTarget(node.rect.left() + kPadding, node.rect.top(),
                                  node.rect.width() - 2 * kPadding,
                                  node.stereotype.isEmpty() ? 0.0
                                                            : kLineHeight);
    const QRectF headerTarget(
        node.rect.left() + kPadding,
        node.rect.top() + (node.stereotype.isEmpty() ? 0.0 : kLineHeight),
        node.rect.width() - 2 * kPadding, kHeaderHeight);
    const QColor primaryText =
        node.style.customized ? node.style.primaryText : palette.nodeTitleText;
    const QColor secondaryText =
        node.style.customized ? node.style.secondaryText : palette.bodyText;
    if (!node.stereotype.isEmpty())
      appendVisibleText(stereotypeTarget, nodeTextClip, node.stereotype, base,
                        secondaryText, Qt::AlignCenter, laterNodeRects);
    appendVisibleText(headerTarget, nodeTextClip, node.name, header,
                      primaryText, Qt::AlignCenter, laterNodeRects);
    if (detail != 2)
      continue;
    int line = 0;
    const auto addLines = [&](const QStringList &values, int &lineNumber) {
      for (const auto &text : values) {
        const QRectF target(node.rect.left() + kPadding,
                            node.rect.top() + headerHeight +
                                lineNumber * kLineHeight,
                            node.rect.width() - 2 * kPadding, kLineHeight);
        appendVisibleText(target, nodeTextClip, text, base, secondaryText,
                          Qt::AlignVCenter | Qt::AlignLeft, laterNodeRects);
        ++lineNumber;
      }
    };
    if (node.type == ElementType::Enumeration) {
      addLines(node.enumLiterals, line);
    } else {
      addLines(node.attributes, line);
      addLines(node.operations, line);
    }
  }
  return entries;
}

} // namespace

DiagramCanvas::DiagramCanvas(QQuickItem *parent)
    : QQuickItem(parent),
      m_defaultDistributionGap(ApplicationSettings::kDefaultDistributionGap),
      m_snapToGridEnabled(ApplicationSettings::kDefaultSnapToGridEnabled),
      m_alignmentGuidesEnabled(
          ApplicationSettings::kDefaultAlignmentGuidesEnabled),
      m_gridSpacing(ApplicationSettings::kDefaultGridSpacing),
      m_defaultConnectorRouting(ApplicationSettings::kDefaultConnectorRouting),
      m_relationshipGestureKeys(
          ApplicationSettings::defaultRelationshipGestureKeys()) {
  setFlag(QQuickItem::ItemHasContents, true);
  setClip(true);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);
}

ProjectController *DiagramCanvas::project() const { return m_project; }

void DiagramCanvas::setProject(ProjectController *project) {
  if (m_project == project)
    return;
  clearRelationshipToolboxCandidate();
  clearArrangementToolboxCandidate(true);
  clearConnectorToolboxCandidate(true);
  clearPresentationToolboxCandidate(true);
  if (m_project)
    disconnect(m_project, nullptr, this, nullptr);
  m_project = project;
  if (m_project) {
    connect(m_project, &ProjectController::stateChanged, this, [this] {
      clearRelationshipToolboxCandidate();
      refreshArrangementToolboxAnchor();
      refreshConnectorToolboxAnchor();
      refreshPresentationToolboxAnchor();
      m_sceneDirty = true;
      m_textDirty = true;
      const bool selectionPruned = pruneFilteredSelection();
      if (selectionPruned)
        synchronizeProjectSelection();
      if (selectionPruned || !m_selectedNodes.isEmpty() ||
          !m_selectedConnector.isEmpty() || !m_selectedContainers.isEmpty())
        emit canvasSelectionChanged();
      emit diagramFilterChanged();
      update();
    });
  }
  m_sceneDirty = true;
  m_textDirty = true;
  emit projectChanged();
  update();
}

QString DiagramCanvas::diagramId() const { return m_diagramId; }

void DiagramCanvas::setDiagramId(const QString &diagramId) {
  if (m_diagramId == diagramId)
    return;
  m_diagramId = diagramId;
  clearCanvasSelection();
  m_sceneDirty = true;
  m_textDirty = true;
  emit diagramIdChanged();
  emit diagramFilterChanged();
  update();
}

qreal DiagramCanvas::zoom() const { return m_zoom; }
bool DiagramCanvas::filterActive() const {
  const auto *currentDiagram = diagram();
  return currentDiagram && diagram_filter::isActive(currentDiagram->filter);
}

int DiagramCanvas::visibleNodeCount() const {
  const auto *currentDiagram = diagram();
  if (!currentDiagram)
    return 0;
  return static_cast<int>(std::count_if(
      currentDiagram->nodes.cbegin(), currentDiagram->nodes.cend(),
      [this](const NodePresentation &node) { return nodePassesFilter(node); }));
}

int DiagramCanvas::totalNodeCount() const {
  const auto *currentDiagram = diagram();
  return currentDiagram ? static_cast<int>(currentDiagram->nodes.size()) : 0;
}

int DiagramCanvas::selectedNodeCount() const { return m_selectedNodes.size(); }
int DiagramCanvas::selectedContainerCount() const {
  return m_selectedContainers.size();
}
bool DiagramCanvas::connectorSelected() const {
  return !m_selectedConnector.isEmpty();
}

bool DiagramCanvas::containerSelected() const {
  return !m_selectedContainers.isEmpty();
}

bool DiagramCanvas::bendPointSelected() const {
  if (m_selectedBendPoint < 0)
    return false;
  const auto *d = diagram();
  const auto *connector = d ? findConnector(*d, m_selectedConnector) : nullptr;
  return connector && m_selectedBendPoint < connector->bendPoints.size();
}

bool DiagramCanvas::selectedConnectorHasBendPoints() const {
  const auto *d = diagram();
  const auto *connector = d ? findConnector(*d, m_selectedConnector) : nullptr;
  return connector && !connector->bendPoints.isEmpty();
}

bool DiagramCanvas::selectedConnectorHasManualAnnotationPositions() const {
  const auto *d = diagram();
  const auto *connector = d ? findConnector(*d, m_selectedConnector) : nullptr;
  return connector && !connector->annotationPlacements.isEmpty();
}

bool DiagramCanvas::contextAnnotationHasManualPosition() const {
  const auto *d = diagram();
  const auto *connector = d ? findConnector(*d, m_selectedConnector) : nullptr;
  return connector && !m_contextAnnotationKey.isEmpty() &&
         connector->annotationPlacements.contains(m_contextAnnotationKey);
}

QString DiagramCanvas::selectedConnectorRouting() const {
  const auto *d = diagram();
  const auto *connector = d ? findConnector(*d, m_selectedConnector) : nullptr;
  return connector ? toString(connector->routing) : QString{};
}

int DiagramCanvas::selectedHorizontalPortSnapPoints() const {
  if (m_selectedNodes.size() != 1)
    return connector_ports::kDefaultSnapPointCount;
  const auto *currentDiagram = diagram();
  const auto *node =
      currentDiagram ? findNode(*currentDiagram, *m_selectedNodes.constBegin())
                     : nullptr;
  return node ? node->horizontalPortSnapPoints
              : connector_ports::kDefaultSnapPointCount;
}

int DiagramCanvas::selectedVerticalPortSnapPoints() const {
  if (m_selectedNodes.size() != 1)
    return connector_ports::kDefaultSnapPointCount;
  const auto *currentDiagram = diagram();
  const auto *node =
      currentDiagram ? findNode(*currentDiagram, *m_selectedNodes.constBegin())
                     : nullptr;
  return node ? node->verticalPortSnapPoints
              : connector_ports::kDefaultSnapPointCount;
}

bool DiagramCanvas::diagramAttributesVisible() const {
  const auto *currentDiagram = diagram();
  return !currentDiagram || currentDiagram->showAttributes;
}

bool DiagramCanvas::diagramOperationsVisible() const {
  const auto *currentDiagram = diagram();
  return !currentDiagram || currentDiagram->showOperations;
}

QString DiagramCanvas::selectedAttributesVisibility() const {
  return selectedCompartmentVisibility(true);
}

QString DiagramCanvas::selectedOperationsVisibility() const {
  return selectedCompartmentVisibility(false);
}

QString
DiagramCanvas::selectedCompartmentVisibility(bool attributes) const {
  if (m_selectedNodeOrder.isEmpty())
    return {};
  const auto *currentDiagram = diagram();
  if (!currentDiagram)
    return {};

  QString commonVisibility;
  for (const QString &nodeId : m_selectedNodeOrder) {
    const auto *node = findNode(*currentDiagram, nodeId);
    if (!node)
      return {};
    const std::optional<bool> value =
        attributes ? node->showAttributes : node->showOperations;
    QString visibility = QStringLiteral("inherit");
    if (value)
      visibility = *value ? QStringLiteral("show") : QStringLiteral("hide");
    if (commonVisibility.isEmpty())
      commonVisibility = visibility;
    else if (commonVisibility != visibility)
      return QStringLiteral("mixed");
  }
  return commonVisibility;
}

int DiagramCanvas::incomingRelatedTypeCount() const {
  return m_project && m_selectedNodeOrder.size() == 1
             ? m_project->relatedElementCountForDiagram(
                   m_diagramId, m_selectedNodeOrder.constFirst(),
                   QStringLiteral("incoming"))
             : 0;
}

int DiagramCanvas::outgoingRelatedTypeCount() const {
  return m_project && m_selectedNodeOrder.size() == 1
             ? m_project->relatedElementCountForDiagram(
                   m_diagramId, m_selectedNodeOrder.constFirst(),
                   QStringLiteral("outgoing"))
             : 0;
}

bool DiagramCanvas::canWrapSelectionInPackage() const {
  return m_project && m_selectedNodeOrder.size() == 1 &&
         m_project->canWrapPresentationInPackage(m_diagramId,
                                                 m_selectedNodeOrder.first());
}

QString DiagramCanvas::selectedStyleId() const {
  if (!m_project)
    return {};
  QString commonStyle;
  bool first = true;
  QStringList presentationIds = m_selectedNodeOrder;
  presentationIds.append(m_selectedContainers.values());
  for (const QString &presentationId : presentationIds) {
    const QString style =
        m_project->explicitStyleIdForPresentation(m_diagramId, presentationId);
    if (first) {
      commonStyle = style;
      first = false;
    } else if (style != commonStyle) {
      return {};
    }
  }
  return commonStyle;
}

QVariantMap DiagramCanvas::relationshipGestureKeys() const {
  return m_relationshipGestureKeys;
}

void DiagramCanvas::setRelationshipGestureKeys(const QVariantMap &keys) {
  const QVariantMap defaults =
      ApplicationSettings::defaultRelationshipGestureKeys();
  QVariantMap normalized;
  QSet<QString> assigned;
  for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
    const QString key = keys.value(it.key()).toString().trimmed().toUpper();
    if (key.size() != 1 || !key.front().isLetterOrNumber() ||
        assigned.contains(key))
      return;
    normalized.insert(it.key(), key);
    assigned.insert(key);
  }
  if (m_relationshipGestureKeys == normalized)
    return;
  m_relationshipGestureKeys = std::move(normalized);
  emit relationshipGestureKeysChanged();
}

bool DiagramCanvas::relationshipToolboxCandidate() const {
  return m_relationshipToolboxCandidate;
}

QString DiagramCanvas::relationshipToolboxNodeId() const {
  return m_relationshipToolboxNodeId;
}

QString DiagramCanvas::relationshipToolboxEdge() const {
  return m_relationshipToolboxEdge;
}

QPointF DiagramCanvas::relationshipToolboxViewAnchor() const {
  return m_relationshipToolboxViewAnchor;
}

QPointF DiagramCanvas::relationshipToolboxSceneAnchor() const {
  return m_relationshipToolboxSceneAnchor;
}

bool DiagramCanvas::arrangementToolboxCandidate() const {
  return m_arrangementToolboxCandidate;
}

QString DiagramCanvas::arrangementToolboxNodeId() const {
  return m_arrangementToolboxNodeId;
}

QPointF DiagramCanvas::arrangementToolboxViewAnchor() const {
  return m_arrangementToolboxViewAnchor;
}

bool DiagramCanvas::connectorToolboxCandidate() const {
  return m_connectorToolboxCandidate;
}

QString DiagramCanvas::connectorToolboxConnectorId() const {
  return m_connectorToolboxConnectorId;
}

QPointF DiagramCanvas::connectorToolboxViewAnchor() const {
  return m_connectorToolboxViewAnchor;
}

bool DiagramCanvas::presentationToolboxCandidate() const {
  return m_presentationToolboxCandidate;
}

QString DiagramCanvas::presentationToolboxPresentationId() const {
  return m_presentationToolboxPresentationId;
}

QString DiagramCanvas::presentationToolboxKind() const {
  return m_presentationToolboxKind;
}

QPointF DiagramCanvas::presentationToolboxViewAnchor() const {
  return m_presentationToolboxViewAnchor;
}

QString DiagramCanvas::connectorInteractionPrompt() const {
  if (!m_connectorGestureType.isEmpty())
    return QStringLiteral("Drag to a target element; release to create %1")
        .arg(m_connectorGestureType);
  if (m_endpointDragActive) {
    const bool source = m_interaction == Interaction::MoveSourcePort;
    return source
               ? QStringLiteral(
                     "Drag the source end; release over an element to attach")
               : QStringLiteral(
                     "Drag the target end; release over an element to attach");
  }
  return {};
}

int DiagramCanvas::defaultDistributionGap() const {
  return m_defaultDistributionGap;
}

void DiagramCanvas::setDefaultDistributionGap(int gap) {
  const int validGap =
      std::clamp(gap, ApplicationSettings::kMinimumDistributionGap,
                 ApplicationSettings::kMaximumDistributionGap);
  if (m_defaultDistributionGap == validGap)
    return;
  m_defaultDistributionGap = validGap;
  emit defaultDistributionGapChanged();
}

bool DiagramCanvas::snapToGridEnabled() const { return m_snapToGridEnabled; }

void DiagramCanvas::setSnapToGridEnabled(bool enabled) {
  if (m_snapToGridEnabled == enabled)
    return;
  m_snapToGridEnabled = enabled;
  emit snapToGridEnabledChanged();
}

bool DiagramCanvas::alignmentGuidesEnabled() const {
  return m_alignmentGuidesEnabled;
}

void DiagramCanvas::setAlignmentGuidesEnabled(bool enabled) {
  if (m_alignmentGuidesEnabled == enabled)
    return;
  m_alignmentGuidesEnabled = enabled;
  if (!enabled) {
    m_alignmentGuides.clear();
    m_sceneDirty = true;
    update();
  }
  emit alignmentGuidesEnabledChanged();
}

int DiagramCanvas::gridSpacing() const { return m_gridSpacing; }

void DiagramCanvas::setGridSpacing(int spacing) {
  const int validSpacing =
      std::clamp(spacing, ApplicationSettings::kMinimumGridSpacing,
                 ApplicationSettings::kMaximumGridSpacing);
  if (m_gridSpacing == validSpacing)
    return;
  m_gridSpacing = validSpacing;
  emit gridSpacingChanged();
  update();
}

QString DiagramCanvas::diagramItemSizingMode() const {
  return m_diagramItemSizingMode;
}

void DiagramCanvas::setDiagramItemSizingMode(const QString &mode) {
  const QString normalized = mode.trimmed().toLower() == QStringLiteral("fixed")
                                 ? QStringLiteral("fixed")
                                 : QStringLiteral("content");
  if (m_diagramItemSizingMode == normalized)
    return;
  m_diagramItemSizingMode = normalized;
  emit diagramItemSizingModeChanged();
}

QString DiagramCanvas::defaultConnectorRouting() const {
  return toString(m_defaultConnectorRouting);
}

void DiagramCanvas::setDefaultConnectorRouting(const QString &routing) {
  bool routingOk = false;
  const ConnectorRouting parsed =
      connectorRoutingFromString(routing, &routingOk);
  if (!routingOk || m_defaultConnectorRouting == parsed)
    return;
  m_defaultConnectorRouting = parsed;
  emit defaultConnectorRoutingChanged();
}

void DiagramCanvas::refreshTheme() {
  ++m_themeRevision;
  m_sceneDirty = true;
  m_textDirty = true;
  update();
}

const Diagram *DiagramCanvas::diagram() const {
  return m_project ? findDiagram(m_project->data(), m_diagramId) : nullptr;
}

bool DiagramCanvas::nodePassesFilter(const NodePresentation &node) const {
  const auto *currentDiagram = diagram();
  if (!currentDiagram || !m_project)
    return false;
  const auto *element = findElement(m_project->data(), node.elementId);
  return element && diagram_filter::matchesElement(m_project->data(), *element,
                                                   currentDiagram->filter);
}

bool DiagramCanvas::connectorPassesFilter(
    const ConnectorPresentation &connector) const {
  const auto *source = endpointNode(connector, true);
  const auto *target = endpointNode(connector, false);
  return source && target && nodePassesFilter(*source) &&
         nodePassesFilter(*target);
}

bool DiagramCanvas::pruneFilteredSelection() {
  bool changed = false;
  const auto *currentDiagram = diagram();
  if (!currentDiagram) {
    changed = !m_selectedNodes.isEmpty() || !m_selectedConnector.isEmpty();
    m_selectedNodes.clear();
    m_selectedNodeOrder.clear();
    m_selectedConnector.clear();
    return changed;
  }
  for (auto node = m_selectedNodes.begin(); node != m_selectedNodes.end();) {
    const auto *presentation = findNode(*currentDiagram, *node);
    if (presentation && nodePassesFilter(*presentation)) {
      ++node;
      continue;
    }
    m_selectedNodeOrder.removeAll(*node);
    node = m_selectedNodes.erase(node);
    changed = true;
  }
  if (!m_selectedConnector.isEmpty()) {
    const auto *connector = findConnector(*currentDiagram, m_selectedConnector);
    if (!connector || !connectorPassesFilter(*connector)) {
      m_selectedConnector.clear();
      m_selectedBendPoint = -1;
      changed = true;
    }
  }
  return changed;
}

QRectF DiagramCanvas::nodeGeometry(const NodePresentation &node) const {
  return m_previewGeometry.value(node.id, node.geometry);
}

QRectF
DiagramCanvas::containerGeometry(const ContainerPresentation &container) const {
  return m_previewGeometry.value(container.id, container.geometry);
}

QRectF DiagramCanvas::containerChildViewport(
    const ContainerPresentation &container) const {
  const auto *currentDiagram = diagram();
  return currentDiagram
             ? ui::DiagramClipLayout(*currentDiagram, m_previewGeometry)
                   .childViewport(container)
             : QRectF{};
}

QRectF DiagramCanvas::presentationClipRect(const QString &presentationId,
                                           bool *hasClip) const {
  const auto *currentDiagram = diagram();
  if (!currentDiagram) {
    *hasClip = false;
    return {};
  }
  const ui::PresentationClip clip =
      ui::DiagramClipLayout(*currentDiagram, m_previewGeometry)
          .clipFor(presentationId);
  *hasClip = clip.active;
  return clip.rect;
}

QRectF DiagramCanvas::visibleNodeGeometry(const NodePresentation &node) const {
  bool hasClip = false;
  const QRectF clip = presentationClipRect(node.id, &hasClip);
  return hasClip ? nodeGeometry(node).intersected(clip) : nodeGeometry(node);
}

QRectF DiagramCanvas::visibleContainerGeometry(
    const ContainerPresentation &container) const {
  bool hasClip = false;
  const QRectF clip = presentationClipRect(container.id, &hasClip);
  return hasClip ? containerGeometry(container).intersected(clip)
                 : containerGeometry(container);
}

DiagramCanvas::ResizeHandle
DiagramCanvas::resizeHandleAt(const QRectF &geometry,
                              const QPointF &scenePoint) const {
  // Match the visible corner square closely. A larger invisible hit region
  // would consume ordinary header clicks and relationship-edge gestures.
  const qreal size = 7.0 / m_zoom;
  if (QRectF(geometry.left(), geometry.top(), size, size)
          .contains(scenePoint))
    return ResizeHandle::TopLeft;
  if (QRectF(geometry.right() - size, geometry.top(), size, size)
          .contains(scenePoint))
    return ResizeHandle::TopRight;
  if (QRectF(geometry.left(), geometry.bottom() - size, size, size)
          .contains(scenePoint))
    return ResizeHandle::BottomLeft;
  if (QRectF(geometry.right() - size, geometry.bottom() - size, size, size)
          .contains(scenePoint))
    return ResizeHandle::BottomRight;
  return ResizeHandle::None;
}

Qt::Edges DiagramCanvas::resizeEdges(ResizeHandle handle) {
  switch (handle) {
  case ResizeHandle::TopLeft:
    return Qt::TopEdge | Qt::LeftEdge;
  case ResizeHandle::TopRight:
    return Qt::TopEdge | Qt::RightEdge;
  case ResizeHandle::BottomLeft:
    return Qt::BottomEdge | Qt::LeftEdge;
  case ResizeHandle::BottomRight:
    return Qt::BottomEdge | Qt::RightEdge;
  case ResizeHandle::None:
    return {};
  }
  return {};
}

QPointF DiagramCanvas::toScene(const QPointF &point) const {
  return (point - m_pan) / m_zoom;
}

QPointF DiagramCanvas::toView(const QPointF &point) const {
  return point * m_zoom + m_pan;
}

QRectF DiagramCanvas::toView(const QRectF &rect) const {
  return {toView(rect.topLeft()), rect.size() * m_zoom};
}

QRectF DiagramCanvas::textLineRect(const QRectF &nodeRect, int line,
                                   qreal headerHeight) const {
  return {nodeRect.left() + kPadding,
          nodeRect.top() + headerHeight + line * kLineHeight,
          nodeRect.width() - 2 * kPadding, kLineHeight};
}

DiagramCanvas::ConnectorEndpoints DiagramCanvas::connectorEndpoints(
    const ConnectorPresentation &connector) const {
  const auto *d = diagram();
  if (!d || !m_project)
    return {};

  const auto *sourceNode = endpointNode(connector, true);
  const auto *targetNode = endpointNode(connector, false);
  if (!sourceNode || !targetNode)
    return {};
  QRectF sourceRect = nodeGeometry(*sourceNode);
  QRectF targetRect = nodeGeometry(*targetNode);
  if (!sourceRect.isValid() || !targetRect.isValid())
    return {};

  ConnectorAnchor sourceAnchor = connector.sourceAnchor;
  ConnectorAnchor targetAnchor = connector.targetAnchor;
  std::optional<QPointF> detachedSource;
  std::optional<QPointF> detachedTarget;
  if (connector.id == m_selectedConnector && m_endpointDragActive) {
    const auto *previewNode = m_endpointDragTargetNode.isEmpty()
                                  ? nullptr
                                  : findNode(*d, m_endpointDragTargetNode);
    if (m_interaction == Interaction::MoveSourcePort) {
      sourceAnchor = m_endpointDragAnchor;
      if (previewNode)
        sourceRect = nodeGeometry(*previewNode);
      else
        detachedSource = m_endpointDragPoint;
    } else if (m_interaction == Interaction::MoveTargetPort) {
      targetAnchor = m_endpointDragAnchor;
      if (previewNode)
        targetRect = nodeGeometry(*previewNode);
      else
        detachedTarget = m_endpointDragPoint;
    }
  }
  QVector<QPointF> bendPoints;
  const auto &presentationBends =
      connector.id == m_selectedConnector && m_bendPointPreviewActive
          ? m_bendPointPreview
          : connector.bendPoints;
  bendPoints.reserve(presentationBends.size());
  for (const auto &bendPoint : presentationBends)
    bendPoints.append(bendPoint.position);

  const QPointF sourceTarget =
      bendPoints.isEmpty() ? detachedTarget.value_or(targetRect.center())
                           : bendPoints.first();
  const QPointF targetTarget =
      bendPoints.isEmpty() ? detachedSource.value_or(sourceRect.center())
                           : bendPoints.last();
  ConnectorEndpoints endpoints;
  endpoints.source = detachedSource.value_or(
      connectorAnchorPoint(sourceRect, sourceAnchor, sourceTarget));
  endpoints.target = detachedTarget.value_or(
      connectorAnchorPoint(targetRect, targetAnchor, targetTarget));
  endpoints.bendPoints = std::move(bendPoints);
  endpoints.routing = connector.routing;
  endpoints.sourceSide =
      detachedSource ? ConnectorSide::Automatic : sourceAnchor.side;
  endpoints.targetSide =
      detachedTarget ? ConnectorSide::Automatic : targetAnchor.side;
  endpoints.valid = true;
  return endpoints;
}

ui::ConnectorRoute
DiagramCanvas::connectorRoute(const ConnectorPresentation &connector) const {
  const ConnectorEndpoints endpoints = connectorEndpoints(connector);
  if (!endpoints.valid)
    return {};
  return ui::buildConnectorRoute(endpoints.source, endpoints.bendPoints,
                                 endpoints.target, endpoints.routing,
                                 endpoints.sourceSide, endpoints.targetSide);
}

const NodePresentation *
DiagramCanvas::endpointNode(const ConnectorPresentation &connector,
                            bool source) const {
  const auto *d = diagram();
  if (!d || !m_project)
    return nullptr;
  const auto *relationship =
      findRelationship(m_project->data(), connector.relationshipId);
  if (!relationship)
    return nullptr;
  const QString &elementId =
      source ? relationship->sourceId : relationship->targetId;
  for (const auto &node : d->nodes)
    if (node.elementId == elementId)
      return &node;
  return nullptr;
}

bool DiagramCanvas::hitSelectedPort(const QPointF &scenePoint,
                                    bool &source) const {
  const auto *d = diagram();
  if (!d || m_selectedConnector.isEmpty())
    return false;
  const auto *connector = findConnector(*d, m_selectedConnector);
  if (!connector)
    return false;
  const ConnectorEndpoints endpoints = connectorEndpoints(*connector);
  if (!endpoints.valid)
    return false;

  constexpr qreal kHandleHitRadius = 10.0;
  const qreal hitRadius = kHandleHitRadius / m_zoom;
  const qreal sourceDistance = QLineF(scenePoint, endpoints.source).length();
  const qreal targetDistance = QLineF(scenePoint, endpoints.target).length();
  if (sourceDistance > hitRadius && targetDistance > hitRadius)
    return false;
  source = sourceDistance <= targetDistance;
  return true;
}

int DiagramCanvas::hitBendPoint(const ConnectorPresentation &connector,
                                const QPointF &scenePoint) const {
  const auto &bendPoints =
      connector.id == m_selectedConnector && m_bendPointPreviewActive
          ? m_bendPointPreview
          : connector.bendPoints;
  constexpr qreal kHandleHitRadius = 10.0;
  const qreal hitRadius = kHandleHitRadius / m_zoom;
  int nearest = -1;
  qreal nearestDistance = hitRadius;
  for (int index = 0; index < bendPoints.size(); ++index) {
    const qreal distance =
        QLineF(scenePoint, bendPoints.at(index).position).length();
    if (distance <= nearestDistance) {
      nearest = index;
      nearestDistance = distance;
    }
  }
  return nearest;
}

int DiagramCanvas::nearestConnectorSegment(
    const ConnectorPresentation &connector, const QPointF &scenePoint) const {
  const ui::ConnectorRoute route = connectorRoute(connector);
  if (route.points.size() < 2)
    return -1;
  int nearestRouteSegment = -1;
  qreal nearestDistance = std::numeric_limits<qreal>::max();
  for (int index = 1; index < route.points.size(); ++index) {
    const qreal distance = distanceToSegment(
        scenePoint, route.points.at(index - 1), route.points.at(index));
    if (distance < nearestDistance) {
      nearestRouteSegment = index - 1;
      nearestDistance = distance;
    }
  }
  if (nearestRouteSegment < 0)
    return -1;
  return static_cast<int>(std::count_if(
      route.bendPointRouteIndices.cbegin(), route.bendPointRouteIndices.cend(),
      [nearestRouteSegment](int routeIndex) {
        return routeIndex <= nearestRouteSegment;
      }));
}

void DiagramCanvas::updateEndpointDrag(const QPointF &scenePoint,
                                       bool suppressSnapping) {
  const auto *d = diagram();
  if (!d || m_selectedConnector.isEmpty())
    return;
  const auto *connector = findConnector(*d, m_selectedConnector);
  if (!connector)
    return;

  m_endpointDragPoint = scenePoint;
  if (const auto *targetNode = hitNode(scenePoint)) {
    m_endpointDragTargetNode = targetNode->id;
    m_endpointDragAnchor =
        suppressSnapping
            ? anchorAtPerimeterPoint(nodeGeometry(*targetNode), scenePoint)
            : snappedAnchorAtPerimeterPoint(
                  *targetNode, nodeGeometry(*targetNode), scenePoint,
                  kPortSnapToleranceViewPixels / m_zoom,
                  &m_endpointDragSnapped);
    if (suppressSnapping)
      m_endpointDragSnapped = false;
  } else {
    // While detached, the endpoint follows the pointer. No model state changes
    // until release over a valid presentation.
    m_endpointDragTargetNode.clear();
    m_endpointDragAnchor = {ConnectorSide::Automatic, 0.5};
    m_endpointDragSnapped = false;
  }
  m_endpointDragActive = true;
  m_sceneDirty = true;
  m_textDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::commitEndpointDrag() {
  if (!m_project || !m_endpointDragActive || m_selectedConnector.isEmpty() ||
      m_endpointDragTargetNode.isEmpty()) {
    cancelEndpointDrag();
    return;
  }
  const bool source = m_interaction == Interaction::MoveSourcePort;
  m_project->reconnectRelationshipAtAnchor(m_diagramId, m_selectedConnector,
                                           m_endpointDragTargetNode, source,
                                           m_endpointDragAnchor);
  cancelEndpointDrag();
}

void DiagramCanvas::cancelEndpointDrag() {
  m_endpointDragTargetNode.clear();
  m_endpointDragPoint = {};
  m_endpointDragAnchor = {};
  m_endpointDragActive = false;
  m_endpointDragSnapped = false;
  if (m_interaction == Interaction::MoveSourcePort ||
      m_interaction == Interaction::MoveTargetPort)
    m_interaction = Interaction::None;
  m_sceneDirty = true;
  m_textDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::updateBendPointPreview(const QPointF &scenePoint) {
  if (m_selectedBendPoint < 0 ||
      m_selectedBendPoint >= m_bendPointPreview.size())
    return;
  m_bendPointPreview[m_selectedBendPoint].position = scenePoint;
  m_bendPointPreviewActive = true;
  m_sceneDirty = true;
  m_textDirty = true;
  update();
}

void DiagramCanvas::commitBendPointPreview() {
  if (!m_project || !m_bendPointPreviewActive ||
      m_selectedConnector.isEmpty() || m_selectedBendPoint < 0 ||
      m_selectedBendPoint >= m_bendPointPreview.size())
    return;
  const QPointF position = m_bendPointPreview.at(m_selectedBendPoint).position;
  m_project->moveConnectorBendPoint(m_diagramId, m_selectedConnector,
                                    m_selectedBendPoint, position.x(),
                                    position.y());
}

void DiagramCanvas::updateAnnotationPreview(const QPointF &scenePoint) {
  const auto *d = diagram();
  const auto *connector =
      d ? findConnector(*d, m_annotationDragConnector) : nullptr;
  if (!connector || m_annotationDragKey.isEmpty())
    return;
  const QPointF desiredCenter = scenePoint + m_annotationDragOffset;
  const ui::ConnectorRoute route = connectorRoute(*connector);
  const auto projection = projectOntoPolyline(route.points, desiredCenter);
  if (!projection)
    return;
  const QPointF normal(-projection->tangent.y(), projection->tangent.x());
  const QPointF offset = desiredCenter - projection->position;
  m_annotationPreview.routePosition = projection->normalizedDistance;
  m_annotationPreview.tangentOffset =
      QPointF::dotProduct(offset, projection->tangent);
  m_annotationPreview.normalOffset = QPointF::dotProduct(offset, normal);
  m_annotationPreviewActive = true;
  m_annotationDragMoved =
      m_annotationDragMoved ||
      QLineF(m_pressScene, scenePoint).length() >= 3.0 / m_zoom;
  m_sceneDirty = true;
  m_textDirty = true;
  update();
}

void DiagramCanvas::commitAnnotationPreview() {
  if (m_project && m_annotationPreviewActive && m_annotationDragMoved &&
      !m_annotationDragConnector.isEmpty() && !m_annotationDragKey.isEmpty()) {
    m_project->setConnectorAnnotationPlacement(
        m_diagramId, m_annotationDragConnector, m_annotationDragKey,
        m_annotationPreview.routePosition, m_annotationPreview.tangentOffset,
        m_annotationPreview.normalOffset);
  }
  cancelAnnotationPreview();
}

void DiagramCanvas::cancelAnnotationPreview() {
  m_annotationPreviewActive = false;
  m_annotationDragMoved = false;
  m_annotationDragConnector.clear();
  m_annotationDragKey.clear();
  if (m_interaction == Interaction::MoveAnnotation)
    m_interaction = Interaction::None;
  m_sceneDirty = true;
  m_textDirty = true;
  update();
}

QString
DiagramCanvas::relationshipTypeForGestureKey(const QKeyEvent &event) const {
  if (event.modifiers() != Qt::NoModifier)
    return {};
  QString key = event.text().trimmed().toUpper();
  if (key.isEmpty() && event.key() >= Qt::Key_0 && event.key() <= Qt::Key_Z)
    key = QChar(event.key()).toUpper();
  if (key.size() != 1)
    return {};
  for (auto it = m_relationshipGestureKeys.cbegin();
       it != m_relationshipGestureKeys.cend(); ++it) {
    if (it.value().toString() == key)
      return it.key();
  }
  return {};
}

bool DiagramCanvas::startConnectorGesture(const QString &relationshipType) {
  if (!m_leftButtonPressed || relationshipType.isEmpty() ||
      (m_interaction != Interaction::Move &&
       m_interaction != Interaction::Resize))
    return false;
  const auto *sourceNode = hitNode(m_pressScene);
  if (!sourceNode)
    return false;
  const QRectF sourceRect = nodeGeometry(*sourceNode);
  constexpr qreal kEdgeHitTolerance = 9.0;
  if (!nearRectanglePerimeter(sourceRect, m_pressScene,
                              kEdgeHitTolerance / m_zoom))
    return false;

  m_connectorGestureSourceNode = sourceNode->id;
  const bool suppressSnapping = m_pressModifiers.testFlag(Qt::AltModifier);
  m_connectorGestureSourceAnchor =
      suppressSnapping
          ? anchorAtPerimeterPoint(sourceRect, m_pressScene)
          : snappedAnchorAtPerimeterPoint(*sourceNode, sourceRect, m_pressScene,
                                          kPortSnapToleranceViewPixels / m_zoom,
                                          &m_connectorGestureSourceSnapped);
  if (suppressSnapping)
    m_connectorGestureSourceSnapped = false;
  m_connectorGestureType = relationshipType;
  m_interaction = Interaction::CreateConnector;
  m_originalGeometry.clear();
  m_previewGeometry.clear();
  m_alignmentGuides.clear();
  updateConnectorGesture(m_lastPointerScene, suppressSnapping);
  m_sceneDirty = true;
  emit canvasSelectionChanged();
  update();
  return true;
}

bool DiagramCanvas::beginToolboxRelationship(const QString &relationshipType,
                                             const QString &sourceNodeId,
                                             qreal sourceSceneX,
                                             qreal sourceSceneY) {
  bool typeValid = false;
  relationshipTypeFromString(relationshipType, &typeValid);
  const auto *currentDiagram = diagram();
  const auto *sourceNode =
      currentDiagram ? findNode(*currentDiagram, sourceNodeId) : nullptr;
  if (!m_project || !typeValid || m_interaction != Interaction::None ||
      !sourceNode || !m_selectedNodes.contains(sourceNodeId))
    return false;

  const QPointF sourcePoint(sourceSceneX, sourceSceneY);
  const QRectF sourceRect = nodeGeometry(*sourceNode);
  constexpr qreal kEdgeHitTolerance = 9.0;
  if (!nearRectanglePerimeter(sourceRect, sourcePoint,
                              kEdgeHitTolerance / m_zoom))
    return false;

  m_connectorGestureSourceNode = sourceNodeId;
  m_connectorGestureSourceAnchor = snappedAnchorAtPerimeterPoint(
      *sourceNode, sourceRect, sourcePoint,
      kPortSnapToleranceViewPixels / m_zoom, &m_connectorGestureSourceSnapped);
  m_connectorGestureType = relationshipType;
  m_interaction = Interaction::CreateConnector;
  m_originalGeometry.clear();
  m_previewGeometry.clear();
  m_alignmentGuides.clear();
  updateConnectorGesture(sourcePoint, false);
  clearRelationshipToolboxCandidate();
  m_sceneDirty = true;
  emit canvasSelectionChanged();
  update();
  return true;
}

void DiagramCanvas::updateToolboxRelationship(qreal viewX, qreal viewY,
                                              bool suppressSnapping) {
  if (m_interaction != Interaction::CreateConnector ||
      m_connectorGestureSourceNode.isEmpty())
    return;
  updateConnectorGesture(toScene({viewX, viewY}), suppressSnapping);
}

void DiagramCanvas::finishToolboxRelationship(qreal viewX, qreal viewY,
                                              bool suppressSnapping) {
  if (m_interaction != Interaction::CreateConnector ||
      m_connectorGestureSourceNode.isEmpty())
    return;
  commitConnectorGesture(toScene({viewX, viewY}), suppressSnapping);
}

void DiagramCanvas::updateRelationshipToolboxCandidate(
    const QPointF &viewPoint) {
  if (m_interaction != Interaction::None || m_selectedNodes.isEmpty()) {
    clearRelationshipToolboxCandidate();
    return;
  }

  const QPointF scenePoint = toScene(viewPoint);
  const auto *node = hitNode(scenePoint);
  constexpr qreal kEdgeHitToleranceViewPixels = 9.0;
  if (!node || !m_selectedNodes.contains(node->id) ||
      !nearRectanglePerimeter(nodeGeometry(*node), scenePoint,
                              kEdgeHitToleranceViewPixels / m_zoom)) {
    clearRelationshipToolboxCandidate();
    return;
  }

  const QRectF nodeRect = nodeGeometry(*node);
  // A corner starts as a resize target. Once an edge toolbox is already
  // visible, however, retain it while the pointer crosses a corner on the way
  // to the toolbox; clicks still begin the normal resize interaction.
  const bool retainingRelationshipToolbox =
      m_relationshipToolboxCandidate && m_relationshipToolboxNodeId == node->id;
  if (resizeHandleAt(nodeRect, scenePoint) != ResizeHandle::None &&
      !retainingRelationshipToolbox) {
    clearRelationshipToolboxCandidate();
    return;
  }

  ConnectorAnchor edgeAnchor = anchorAtPerimeterPoint(nodeRect, scenePoint);
  if (m_relationshipToolboxCandidate &&
      m_relationshipToolboxNodeId == node->id) {
    bool validPreviousSide = false;
    const ConnectorSide previousSide =
        connectorSideFromString(m_relationshipToolboxEdge, &validPreviousSide);
    // Once a side has produced a toolbox, retain it through the slightly wider
    // corner zone. Without this hysteresis, one-pixel diagonal movements can
    // alternate between (for example) Top and Right and relocate the toolbox
    // while the pointer is travelling toward it.
    constexpr qreal kEdgeRetentionToleranceViewPixels = 14.0;
    if (validPreviousSide &&
        distanceToPerimeterSide(nodeRect, scenePoint, previousSide) <=
            kEdgeRetentionToleranceViewPixels / m_zoom) {
      edgeAnchor = anchorOnPerimeterSide(nodeRect, scenePoint, previousSide);
    }
  }
  const QPointF sceneAnchor =
      connectorAnchorPoint(nodeRect, edgeAnchor, nodeRect.center());
  const QPointF viewAnchor = toView(sceneAnchor);
  const QString edge = toString(edgeAnchor.side);
  if (m_relationshipToolboxCandidate &&
      m_relationshipToolboxNodeId == node->id &&
      m_relationshipToolboxEdge == edge &&
      m_relationshipToolboxViewAnchor == viewAnchor &&
      m_relationshipToolboxSceneAnchor == sceneAnchor)
    return;

  m_relationshipToolboxCandidate = true;
  m_relationshipToolboxNodeId = node->id;
  m_relationshipToolboxEdge = edge;
  m_relationshipToolboxViewAnchor = viewAnchor;
  m_relationshipToolboxSceneAnchor = sceneAnchor;
  emit relationshipToolboxCandidateChanged();
}

void DiagramCanvas::clearRelationshipToolboxCandidate() {
  if (!m_relationshipToolboxCandidate && m_relationshipToolboxNodeId.isEmpty())
    return;
  m_relationshipToolboxCandidate = false;
  m_relationshipToolboxNodeId.clear();
  m_relationshipToolboxEdge.clear();
  m_relationshipToolboxViewAnchor = {};
  m_relationshipToolboxSceneAnchor = {};
  emit relationshipToolboxCandidateChanged();
}

void DiagramCanvas::updateArrangementToolboxCandidate(
    const QPointF &viewPoint) {
  if (m_interaction != Interaction::None || m_selectedNodes.size() < 2) {
    clearArrangementToolboxCandidate(m_selectedNodes.size() < 2);
    return;
  }

  const auto *node = hitNode(toScene(viewPoint));
  if (!node || !m_selectedNodes.contains(node->id)) {
    clearArrangementToolboxCandidate();
    return;
  }

  const bool becameCandidate = !m_arrangementToolboxCandidate;
  const bool targetChanged = m_arrangementToolboxNodeId != node->id;
  const QPointF previousAnchor = m_arrangementToolboxViewAnchor;
  m_arrangementToolboxCandidate = true;
  m_arrangementToolboxNodeId = node->id;
  refreshArrangementToolboxAnchor();
  if ((becameCandidate || targetChanged) && m_arrangementToolboxCandidate &&
      previousAnchor == m_arrangementToolboxViewAnchor)
    emit arrangementToolboxCandidateChanged();
}

void DiagramCanvas::refreshArrangementToolboxAnchor() {
  const auto *d = diagram();
  if (!d || m_selectedNodes.size() < 2 ||
      !m_selectedNodes.contains(m_arrangementToolboxNodeId)) {
    clearArrangementToolboxCandidate(true);
    return;
  }

  const auto *node = findNode(*d, m_arrangementToolboxNodeId);
  const QRectF visibleGeometry = node ? visibleNodeGeometry(*node) : QRectF{};
  if (visibleGeometry.isEmpty()) {
    clearArrangementToolboxCandidate(true);
    return;
  }

  const QRectF viewBounds = toView(visibleGeometry);
  const QPointF nextAnchor(viewBounds.center().x(), viewBounds.top());
  if (m_arrangementToolboxViewAnchor == nextAnchor)
    return;
  m_arrangementToolboxViewAnchor = nextAnchor;
  emit arrangementToolboxCandidateChanged();
}

void DiagramCanvas::clearArrangementToolboxCandidate(bool clearAnchor) {
  const bool changed =
      m_arrangementToolboxCandidate ||
      (clearAnchor && !m_arrangementToolboxViewAnchor.isNull());
  if (!changed)
    return;
  m_arrangementToolboxCandidate = false;
  if (clearAnchor) {
    m_arrangementToolboxNodeId.clear();
    m_arrangementToolboxViewAnchor = {};
  }
  emit arrangementToolboxCandidateChanged();
}

void DiagramCanvas::updateConnectorToolboxCandidate(const QPointF &viewPoint) {
  if (m_interaction != Interaction::None || m_selectedConnector.isEmpty()) {
    clearConnectorToolboxCandidate(m_selectedConnector.isEmpty());
    return;
  }

  const QPointF scenePoint = toScene(viewPoint);
  const auto *hoveredConnector = hitConnector(scenePoint);
  const AnnotationHit annotation = hitConnectorAnnotation(scenePoint);
  const bool overSelectedRoute =
      hoveredConnector && hoveredConnector->id == m_selectedConnector;
  const bool overSelectedAnnotation =
      !annotation.connectorId.isEmpty() &&
      annotation.connectorId == m_selectedConnector;
  if (!overSelectedRoute && !overSelectedAnnotation) {
    clearConnectorToolboxCandidate();
    return;
  }

  const bool becameCandidate = !m_connectorToolboxCandidate;
  const bool targetChanged =
      m_connectorToolboxConnectorId != m_selectedConnector;
  const QPointF previousAnchor = m_connectorToolboxViewAnchor;
  m_connectorToolboxCandidate = true;
  m_connectorToolboxConnectorId = m_selectedConnector;
  // Anchor at the point that attracted the user's attention. In particular,
  // a long relationship should not make the user travel back to its midpoint.
  m_connectorToolboxSceneAnchor = scenePoint;
  refreshConnectorToolboxAnchor();
  if ((becameCandidate || targetChanged) && m_connectorToolboxCandidate &&
      previousAnchor == m_connectorToolboxViewAnchor)
    emit connectorToolboxCandidateChanged();
}

void DiagramCanvas::refreshConnectorToolboxAnchor() {
  const auto *d = diagram();
  if (!d || m_connectorToolboxConnectorId.isEmpty() ||
      m_connectorToolboxConnectorId != m_selectedConnector) {
    clearConnectorToolboxCandidate(true);
    return;
  }

  const auto *connector = findConnector(*d, m_connectorToolboxConnectorId);
  if (!connector) {
    clearConnectorToolboxCandidate(true);
    return;
  }

  const QPointF nextAnchor = toView(m_connectorToolboxSceneAnchor);
  if (m_connectorToolboxViewAnchor == nextAnchor)
    return;
  m_connectorToolboxViewAnchor = nextAnchor;
  emit connectorToolboxCandidateChanged();
}

void DiagramCanvas::clearConnectorToolboxCandidate(bool clearAnchor) {
  const bool changed =
      m_connectorToolboxCandidate ||
      (clearAnchor && (!m_connectorToolboxConnectorId.isEmpty() ||
                       !m_connectorToolboxViewAnchor.isNull()));
  if (!changed)
    return;
  m_connectorToolboxCandidate = false;
  if (clearAnchor) {
    m_connectorToolboxConnectorId.clear();
    m_connectorToolboxViewAnchor = {};
    m_connectorToolboxSceneAnchor = {};
  }
  emit connectorToolboxCandidateChanged();
}

void DiagramCanvas::updatePresentationToolboxCandidate(
    const QPointF &viewPoint) {
  if (m_interaction != Interaction::None) {
    clearPresentationToolboxCandidate();
    return;
  }

  const QPointF scenePoint = toScene(viewPoint);
  QString candidateId;
  QString candidateKind;
  if (m_selectedNodes.size() == 1) {
    const auto *node = hitNode(scenePoint);
    if (node && m_selectedNodes.contains(node->id)) {
      const QRectF nodeRect = nodeGeometry(*node);
      if (resizeHandleAt(nodeRect, scenePoint) == ResizeHandle::None) {
        candidateId = node->id;
        candidateKind = QStringLiteral("node");
      }
    }
  } else if (!m_selectedContainer.isEmpty()) {
    const auto *diagramData = diagram();
    const auto *container =
        diagramData ? findContainer(*diagramData, m_selectedContainer)
                    : nullptr;
    if (container && !hitNode(scenePoint)) {
      const QRectF geometry = containerGeometry(*container);
      const QRectF visibleGeometry = visibleContainerGeometry(*container);
      const qreal headerHeight =
          container->subjectKind == QStringLiteral("package")
              ? 24.0
              : kContainerHeaderHeight;
      const QRectF visibleHeader = QRectF(geometry.left(), geometry.top(),
                                          geometry.width(), headerHeight)
                                       .intersected(visibleGeometry);
      if (visibleHeader.contains(scenePoint) &&
          resizeHandleAt(geometry, scenePoint) == ResizeHandle::None) {
        candidateId = container->id;
        candidateKind = QStringLiteral("container");
      }
    }
  }

  if (candidateId.isEmpty()) {
    clearPresentationToolboxCandidate();
    return;
  }

  const bool becameCandidate = !m_presentationToolboxCandidate;
  const bool targetChanged =
      m_presentationToolboxPresentationId != candidateId ||
      m_presentationToolboxKind != candidateKind;
  const QPointF previousAnchor = m_presentationToolboxViewAnchor;
  m_presentationToolboxCandidate = true;
  m_presentationToolboxPresentationId = candidateId;
  m_presentationToolboxKind = candidateKind;
  refreshPresentationToolboxAnchor();
  if ((becameCandidate || targetChanged) && m_presentationToolboxCandidate &&
      previousAnchor == m_presentationToolboxViewAnchor)
    emit presentationToolboxCandidateChanged();
}

void DiagramCanvas::refreshPresentationToolboxAnchor() {
  const auto *diagramData = diagram();
  QRectF visibleGeometry;
  if (diagramData && m_presentationToolboxKind == QStringLiteral("node") &&
      m_selectedNodes.size() == 1 &&
      m_selectedNodes.contains(m_presentationToolboxPresentationId)) {
    if (const auto *node =
            findNode(*diagramData, m_presentationToolboxPresentationId))
      visibleGeometry = visibleNodeGeometry(*node);
  } else if (diagramData &&
             m_presentationToolboxKind == QStringLiteral("container") &&
             m_selectedContainer == m_presentationToolboxPresentationId) {
    if (const auto *container =
            findContainer(*diagramData, m_presentationToolboxPresentationId))
      visibleGeometry = visibleContainerGeometry(*container);
  }

  if (visibleGeometry.isEmpty()) {
    clearPresentationToolboxCandidate(true);
    return;
  }
  const QRectF viewBounds = toView(visibleGeometry);
  const QPointF nextAnchor(viewBounds.center().x(), viewBounds.top());
  if (m_presentationToolboxViewAnchor == nextAnchor)
    return;
  m_presentationToolboxViewAnchor = nextAnchor;
  emit presentationToolboxCandidateChanged();
}

void DiagramCanvas::clearPresentationToolboxCandidate(bool clearAnchor) {
  const bool changed =
      m_presentationToolboxCandidate ||
      (clearAnchor && (!m_presentationToolboxPresentationId.isEmpty() ||
                       !m_presentationToolboxKind.isEmpty() ||
                       !m_presentationToolboxViewAnchor.isNull()));
  if (!changed)
    return;
  m_presentationToolboxCandidate = false;
  if (clearAnchor) {
    m_presentationToolboxPresentationId.clear();
    m_presentationToolboxKind.clear();
    m_presentationToolboxViewAnchor = {};
  }
  emit presentationToolboxCandidateChanged();
}

void DiagramCanvas::updateConnectorGesture(const QPointF &scenePoint,
                                           bool suppressSnapping) {
  m_connectorGestureTargetPoint = scenePoint;
  m_connectorGestureTargetNode.clear();
  m_connectorGestureTargetAnchor = {};
  m_connectorGestureTargetSnapped = false;
  if (const auto *targetNode = hitNode(scenePoint)) {
    m_connectorGestureTargetNode = targetNode->id;
    m_connectorGestureTargetAnchor =
        suppressSnapping
            ? anchorAtPerimeterPoint(nodeGeometry(*targetNode), scenePoint)
            : snappedAnchorAtPerimeterPoint(
                  *targetNode, nodeGeometry(*targetNode), scenePoint,
                  kPortSnapToleranceViewPixels / m_zoom,
                  &m_connectorGestureTargetSnapped);
  }
  m_sceneDirty = true;
  update();
}

void DiagramCanvas::commitConnectorGesture(const QPointF &scenePoint,
                                           bool suppressSnapping) {
  if (!m_project || m_connectorGestureSourceNode.isEmpty()) {
    cancelConnectorGesture();
    return;
  }
  updateConnectorGesture(scenePoint, suppressSnapping);
  const auto *d = diagram();
  const auto *targetNode =
      d ? findNode(*d, m_connectorGestureTargetNode) : nullptr;
  if (!targetNode) {
    cancelConnectorGesture();
    return;
  }

  const QString connectorId = m_project->createRelationshipAtAnchors(
      m_diagramId, m_connectorGestureSourceNode, targetNode->id,
      m_connectorGestureType, toString(m_defaultConnectorRouting),
      m_connectorGestureSourceAnchor, m_connectorGestureTargetAnchor);
  m_connectorGestureSourceNode.clear();
  m_connectorGestureTargetNode.clear();
  m_connectorGestureType.clear();
  m_connectorGestureSourceSnapped = false;
  m_connectorGestureTargetSnapped = false;
  m_interaction = Interaction::None;
  m_sceneDirty = true;
  if (!connectorId.isEmpty())
    selectConnector(connectorId, false);
  else {
    emit canvasSelectionChanged();
    update();
  }
}

void DiagramCanvas::cancelConnectorGesture() {
  m_connectorGestureSourceNode.clear();
  m_connectorGestureTargetNode.clear();
  m_connectorGestureType.clear();
  m_connectorGestureSourceSnapped = false;
  m_connectorGestureTargetSnapped = false;
  m_originalGeometry.clear();
  m_previewGeometry.clear();
  m_alignmentGuides.clear();
  m_interaction = Interaction::None;
  m_sceneDirty = true;
  emit canvasSelectionChanged();
  update();
}

QSGNode *
DiagramCanvas::updatePaintNode(QSGNode *oldNode,
                               UpdatePaintNodeData *updatePaintNodeData) {
  Q_UNUSED(updatePaintNodeData)
  auto *root = static_cast<DiagramSceneRoot *>(oldNode);
  if (!root)
    root = new DiagramSceneRoot;
  const bool themeChanged = root->renderedThemeRevision != m_themeRevision;
  root->updateBackground({width(), height()}, m_themeRevision);
  auto *quickWindow = window();
  const qreal devicePixelRatio =
      quickWindow ? quickWindow->devicePixelRatio() : 1.0;
  root->updateGrid({width(), height()}, m_pan, m_zoom, devicePixelRatio,
                   m_gridSpacing, m_themeRevision);
  root->updateTransform(m_pan, m_zoom);

  const int detail = detailLevel(m_zoom);
  const qreal rasterScale = textRasterScale(m_zoom, devicePixelRatio);
  const QRectF visibleScene =
      QRectF(toScene({0, 0}), toScene({width(), height()})).normalized();
  const bool detailChanged = root->renderedDetail != detail;
  const bool geometryDirty = m_sceneDirty || detailChanged || themeChanged;
  const bool scaleChanged =
      !qFuzzyCompare(rasterScale, root->renderedTextScale);
  const bool needsTextCoverage = detail > 0 && rasterScale > 1.0;
  const bool coverageExpired =
      needsTextCoverage && (!root->textCoverage.isValid() ||
                            !root->textCoverage.contains(visibleScene));
  const bool textDirty = m_textDirty || detailChanged || scaleChanged ||
                         coverageExpired || themeChanged;
  if (geometryDirty || textDirty) {
    SceneSnapshot snapshot;
    const auto *d = diagram();
    if (d && m_project) {
      const auto &projectData = m_project->data();
      QHash<QString, const ModelElement *> elements;
      elements.reserve(projectData.elements.size());
      for (const auto &element : projectData.elements)
        elements.insert(element.id, &element);

      QHash<QString, const Relationship *> relationships;
      relationships.reserve(projectData.relationships.size());
      for (const auto &relationship : projectData.relationships)
        relationships.insert(relationship.id, &relationship);

      QHash<QString, QString> containerOwnerByChild;
      QHash<QString, const ContainerPresentation *> containersById;
      containersById.reserve(d->containers.size());
      for (const auto &container : d->containers)
        containersById.insert(container.id, &container);
      for (const auto &container : d->containers) {
        for (const QString &childId : container.childPresentationIds)
          containerOwnerByChild.insert(childId, container.id);
      }
      QSet<QString> filteredPresentationIds;
      for (const auto &node : d->nodes)
        if (!nodePassesFilter(node))
          filteredPresentationIds.insert(node.id);
      const ui::DiagramClipLayout clipLayout(*d, m_previewGeometry,
                                             filteredPresentationIds);
      QSet<QString> detachedDragRoots;
      if (m_interaction == Interaction::Move && !m_previewGeometry.isEmpty()) {
        for (const QString &containerId : m_selectedContainers)
          if (m_originalGeometry.contains(containerId))
            detachedDragRoots.insert(containerId);
        for (const QString &nodeId : m_selectedNodeOrder)
          if (m_originalGeometry.contains(nodeId))
            detachedDragRoots.insert(nodeId);
      }
      const auto containerDepth = [&](const QString &containerId) {
        int depth = 0;
        QSet<QString> visited;
        QString current = containerId;
        while (containerOwnerByChild.contains(current) &&
               !visited.contains(current)) {
          visited.insert(current);
          current = containerOwnerByChild.value(current);
          ++depth;
        }
        return depth;
      };
      for (const auto &container : d->containers) {
        const ui::PresentationClip clip =
            clipLayout.clipFor(container.id, detachedDragRoots);
        const auto *containerElement =
            container.subjectKind == QStringLiteral("package")
                ? findElement(projectData, container.subjectId)
                : nullptr;
        snapshot.containers.append(
            {containerGeometry(container),
             presentation_layout::containerDisplayName(projectData, container),
             containerElement
                 ? stereotype_catalog::displayText(
                       projectData, containerElement->stereotypeIds)
                 : QString{},
             presentation_layout::containerTitleWidth(projectData, container),
             container.subjectKind == QStringLiteral("package"),
             renderElementStyle(project_style::effectiveStyleForContainer(
                 projectData, container)),
             m_selectedContainers.contains(container.id),
             containerDepth(container.id),
             clip.rect, clip.active, clipLayout.overflowEdges(container)});
      }
      std::stable_sort(
          snapshot.containers.begin(), snapshot.containers.end(),
          [](const RenderContainer &left, const RenderContainer &right) {
            if (left.depth != right.depth)
              return left.depth < right.depth;
            return left.rect.width() * left.rect.height() >
                   right.rect.width() * right.rect.height();
          });

      QHash<QString, QRectF> nodeRects;
      nodeRects.reserve(d->nodes.size());
      const auto containingPackageId = [&](const QString &presentationId) {
        QString currentId = containerOwnerByChild.value(presentationId);
        QSet<QString> visited;
        while (!currentId.isEmpty() && !visited.contains(currentId)) {
          visited.insert(currentId);
          const auto *owner = containersById.value(currentId);
          if (!owner)
            break;
          if (owner->subjectKind == QStringLiteral("package"))
            return owner->subjectId;
          currentId = containerOwnerByChild.value(currentId);
        }
        return QString{};
      };
      for (const auto &node : d->nodes) {
        if (!nodePassesFilter(node))
          continue;
        const QRectF rect = nodeGeometry(node);
        nodeRects.insert(node.elementId, rect);
        const auto element = elements.constFind(node.elementId);
        if (element == elements.cend())
          continue;
        const ui::PresentationClip clip =
            clipLayout.clipFor(node.id, detachedDragRoots);
        const bool showAttributes =
            node.showAttributes.value_or(d->showAttributes);
        const bool showOperations =
            node.showOperations.value_or(d->showOperations);
        snapshot.nodes.append(
            {rect, (*element)->type,
             presentation_layout::elementDisplayNameInPackage(
                 projectData, **element, containingPackageId(node.id)),
             stereotype_catalog::displayText(projectData,
                                             (*element)->stereotypeIds),
             showAttributes ? (*element)->attributes : QStringList{},
             showOperations ? (*element)->operations : QStringList{},
             (*element)->enumLiterals,
             renderElementStyle(
                 project_style::effectiveStyleForNode(projectData, node)),
             m_selectedNodes.contains(node.id),
             m_selectedNodes.size() > 1 &&
                 node.id == m_selectedNodeOrder.constLast(),
             clip.rect, clip.active});
      }
      for (const auto &connector : d->connectors) {
        const auto relationship =
            relationships.constFind(connector.relationshipId);
        if (relationship == relationships.cend())
          continue;
        const auto start = nodeRects.constFind((*relationship)->sourceId);
        const auto end = nodeRects.constFind((*relationship)->targetId);
        if (start == nodeRects.cend() || end == nodeRects.cend())
          continue;
        ui::ConnectorRoute route;
        if (connector.id == m_selectedConnector) {
          // Only the selected connector can have an interaction preview. The
          // focused calculation may resolve a provisional endpoint node;
          // unselected connectors retain the hash-based fast path below.
          const ConnectorEndpoints endpoints = connectorEndpoints(connector);
          if (!endpoints.valid)
            continue;
          route = ui::buildConnectorRoute(
              endpoints.source, endpoints.bendPoints, endpoints.target,
              endpoints.routing, endpoints.sourceSide, endpoints.targetSide);
        } else {
          QVector<QPointF> bendPoints;
          bendPoints.reserve(connector.bendPoints.size());
          for (const auto &bendPoint : connector.bendPoints)
            bendPoints.append(bendPoint.position);
          const QPointF sourceTarget =
              bendPoints.isEmpty() ? end->center() : bendPoints.first();
          const QPointF targetTarget =
              bendPoints.isEmpty() ? start->center() : bendPoints.last();
          const QPointF sourcePoint = connectorAnchorPoint(
              *start, connector.sourceAnchor, sourceTarget);
          const QPointF targetPoint =
              connectorAnchorPoint(*end, connector.targetAnchor, targetTarget);
          route = ui::buildConnectorRoute(
              sourcePoint, bendPoints, targetPoint, connector.routing,
              connector.sourceAnchor.side, connector.targetAnchor.side);
        }
        auto annotationPlacements = connector.annotationPlacements;
        if (m_annotationPreviewActive &&
            connector.id == m_annotationDragConnector)
          annotationPlacements.insert(m_annotationDragKey, m_annotationPreview);
        snapshot.connectors.append(
            {route.points, route.bendPointRouteIndices, (*relationship)->name,
             stereotype_catalog::displayText(projectData,
                                             (*relationship)->stereotypeIds),
             (*relationship)->sourceEnd, (*relationship)->targetEnd,
             std::move(annotationPlacements), (*relationship)->type,
             connector.id == m_selectedConnector,
             connector.id == m_selectedConnector ? m_selectedBendPoint : -1});
      }
      if (m_interaction == Interaction::CreateConnector &&
          !m_connectorGestureSourceNode.isEmpty()) {
        const auto sourceNode =
            std::find_if(d->nodes.cbegin(), d->nodes.cend(),
                         [this](const NodePresentation &candidate) {
                           return candidate.id == m_connectorGestureSourceNode;
                         });
        if (sourceNode != d->nodes.cend()) {
          const QRectF sourceRect = nodeGeometry(*sourceNode);
          QPointF targetPoint = m_connectorGestureTargetPoint;
          ConnectorSide targetSide = ConnectorSide::Automatic;
          const auto *targetNode = findNode(*d, m_connectorGestureTargetNode);
          if (targetNode) {
            const QRectF targetRect = nodeGeometry(*targetNode);
            targetSide = m_connectorGestureTargetAnchor.side;
            targetPoint =
                connectorAnchorPoint(targetRect, m_connectorGestureTargetAnchor,
                                     sourceRect.center());
          }
          const QPointF sourcePoint = connectorAnchorPoint(
              sourceRect, m_connectorGestureSourceAnchor, targetPoint);
          bool typeOk = false;
          const RelationshipType type =
              relationshipTypeFromString(m_connectorGestureType, &typeOk);
          if (typeOk) {
            const ui::ConnectorRoute route = ui::buildConnectorRoute(
                sourcePoint, {}, targetPoint, m_defaultConnectorRouting,
                m_connectorGestureSourceAnchor.side, targetSide);
            snapshot.connectors.append({route.points,
                                        route.bendPointRouteIndices,
                                        {},
                                        {},
                                        {},
                                        {},
                                        {},
                                        type,
                                        false,
                                        -1,
                                        true});
          }

          const ui::PresentationClip sourceClip =
              clipLayout.clipFor(sourceNode->id);
          appendPortSnapPoints(snapshot, *sourceNode, sourceRect,
                               m_connectorGestureSourceSnapped
                                   ? std::optional<ConnectorAnchor>(
                                         m_connectorGestureSourceAnchor)
                                   : std::nullopt,
                               sourceClip.rect, sourceClip.active);
          if (targetNode) {
            const ui::PresentationClip targetClip =
                clipLayout.clipFor(targetNode->id);
            appendPortSnapPoints(snapshot, *targetNode,
                                 nodeGeometry(*targetNode),
                                 m_connectorGestureTargetSnapped
                                     ? std::optional<ConnectorAnchor>(
                                           m_connectorGestureTargetAnchor)
                                     : std::nullopt,
                                 targetClip.rect, targetClip.active);
          }
        }
      }
      if (m_endpointDragActive && !m_endpointDragTargetNode.isEmpty()) {
        if (const auto *targetNode = findNode(*d, m_endpointDragTargetNode)) {
          const ui::PresentationClip targetClip =
              clipLayout.clipFor(targetNode->id);
          appendPortSnapPoints(
              snapshot, *targetNode, nodeGeometry(*targetNode),
              m_endpointDragSnapped
                  ? std::optional<ConnectorAnchor>(m_endpointDragAnchor)
                  : std::nullopt,
              targetClip.rect, targetClip.active);
        }
      }
    }
    snapshot.lassoRect = m_lassoRect.normalized();
    snapshot.lassoVisible = m_lassoActive && !snapshot.lassoRect.isEmpty();
    snapshot.alignmentGuides = m_alignmentGuides;
    if (geometryDirty)
      root->replaceGeometry(buildSceneGeometry(snapshot, m_zoom, detail));

    QRectF textCoverage;
    if (needsTextCoverage) {
      if (!scaleChanged && !coverageExpired && root->textCoverage.isValid()) {
        textCoverage = root->textCoverage;
      } else {
        textCoverage = visibleScene;
        textCoverage.adjust(-visibleScene.width(), -visibleScene.height(),
                            visibleScene.width(), visibleScene.height());
      }
    }
    if (textDirty && quickWindow) {
      root->updateText(buildTextEntries(snapshot, detail, textCoverage),
                       quickWindow, rasterScale);
      m_textDirty = false;
    }
    root->renderedDetail = detail;
    root->renderedThemeRevision = m_themeRevision;
    root->renderedTextScale = rasterScale;
    root->textCoverage = textCoverage;
    m_sceneDirty = false;
  }
  return root;
}

void DiagramCanvas::geometryChange(const QRectF &newGeometry,
                                   const QRectF &oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  clearRelationshipToolboxCandidate();
  clearArrangementToolboxCandidate(true);
  m_sceneDirty = true;
  update();
}

const NodePresentation *
DiagramCanvas::hitNode(const QPointF &scenePoint) const {
  const auto *d = diagram();
  if (!d)
    return nullptr;
  for (auto it = d->nodes.crbegin(); it != d->nodes.crend(); ++it)
    if (nodePassesFilter(*it) && visibleNodeGeometry(*it).contains(scenePoint))
      return &*it;
  return nullptr;
}

const ContainerPresentation *
DiagramCanvas::hitContainer(const QPointF &scenePoint) const {
  return hitContainer(scenePoint, {});
}

const ContainerPresentation *DiagramCanvas::hitContainer(
    const QPointF &scenePoint,
    const QSet<QString> &excludedPresentationIds) const {
  const auto *d = diagram();
  if (!d)
    return nullptr;
  const ContainerPresentation *best = nullptr;
  qreal bestArea = std::numeric_limits<qreal>::max();
  for (const auto &container : d->containers) {
    if (excludedPresentationIds.contains(container.id))
      continue;
    const QRectF geometry = visibleContainerGeometry(container);
    const qreal area = geometry.width() * geometry.height();
    if (!geometry.isEmpty() && geometry.contains(scenePoint) &&
        area < bestArea) {
      best = &container;
      bestArea = area;
    }
  }
  return best;
}

const ConnectorPresentation *
DiagramCanvas::hitConnector(const QPointF &scenePoint) const {
  const auto *d = diagram();
  if (!d)
    return nullptr;
  for (auto it = d->connectors.crbegin(); it != d->connectors.crend(); ++it) {
    if (!connectorPassesFilter(*it))
      continue;
    const ui::ConnectorRoute route = connectorRoute(*it);
    if (route.points.size() < 2)
      continue;
    for (qsizetype index = 1; index < route.points.size(); ++index) {
      if (distanceToSegment(scenePoint, route.points.at(index - 1),
                            route.points.at(index)) <= 7.0 / m_zoom)
        return &*it;
    }
  }
  return nullptr;
}

DiagramCanvas::AnnotationHit
DiagramCanvas::hitConnectorAnnotation(const QPointF &scenePoint) const {
  const auto *d = diagram();
  if (!d || !m_project)
    return {};
  // Connector text is clipped behind nodes by the renderer. Keep hit testing
  // consistent so an invisible annotation never steals a node interaction.
  if (hitNode(scenePoint))
    return {};
  const QFont font = QGuiApplication::font();
  for (auto connector = d->connectors.crbegin();
       connector != d->connectors.crend(); ++connector) {
    const auto *relationship =
        findRelationship(m_project->data(), connector->relationshipId);
    if (!relationship)
      continue;
    QHash<QString, ConnectorAnnotationPlacement> placements =
        connector->annotationPlacements;
    if (m_annotationPreviewActive && connector->id == m_annotationDragConnector)
      placements.insert(m_annotationDragKey, m_annotationPreview);
    const ui::ConnectorRoute route = connectorRoute(*connector);
    const ConnectorAnnotationLayout layout = connectorAnnotationLayout(
        route.points, relationship->name,
        stereotype_catalog::displayText(m_project->data(),
                                        relationship->stereotypeIds),
        relationship->sourceEnd, relationship->targetEnd, placements, font);
    const QList<QPair<QString, QRectF>> targets = {
        {QStringLiteral("name"), layout.name},
        {QStringLiteral("stereotype"), layout.stereotype},
        {QStringLiteral("sourceRole"), layout.sourceRole},
        {QStringLiteral("sourceMultiplicity"), layout.sourceMultiplicity},
        {QStringLiteral("targetRole"), layout.targetRole},
        {QStringLiteral("targetMultiplicity"), layout.targetMultiplicity}};
    for (const auto &[key, rect] : targets)
      if (!rect.isEmpty() && rect.contains(scenePoint))
        return {connector->id, relationship->id, key, rect};
  }
  return {};
}

DiagramCanvas::TextHit DiagramCanvas::hitText(const QPointF &scenePoint) const {
  const auto *d = diagram();
  if (!d || !m_project)
    return {};
  if (const auto *node = hitNode(scenePoint)) {
    const auto *element = findElement(m_project->data(), node->elementId);
    if (!element)
      return {};
    const QRectF rect = nodeGeometry(*node);
    const QString stereotype = stereotype_catalog::displayText(
        m_project->data(), element->stereotypeIds);
    const qreal headerHeight =
        kHeaderHeight + (stereotype.isEmpty() ? 0.0 : kLineHeight);
    if (!stereotype.isEmpty() &&
        QRectF(rect.left(), rect.top(), rect.width(), kLineHeight)
            .contains(scenePoint))
      return {element->id,
              QStringLiteral("stereotypes"),
              -1,
              stereotype,
              QRectF(rect.left() + 4, rect.top() + 2, rect.width() - 8,
                     kLineHeight - 4),
              false};
    if (QRectF(rect.left(),
               rect.top() + (stereotype.isEmpty() ? 0.0 : kLineHeight),
               rect.width(), kHeaderHeight)
            .contains(scenePoint))
      return {
          element->id,
          QStringLiteral("name"),
          -1,
          presentation_layout::elementDisplayNameInPackage(
              m_project->data(), *element,
              presentation_layout::containingPackageElementId(*d, node->id)),
          QRectF(rect.left() + 4,
                 rect.top() + (stereotype.isEmpty() ? 0.0 : kLineHeight) + 3,
                 rect.width() - 8, kHeaderHeight - 6),
          true};
    int line = 0;
    if (element->type == ElementType::Enumeration) {
      for (int i = 0; i < element->enumLiterals.size(); ++i, ++line)
        if (textLineRect(rect, line, headerHeight).contains(scenePoint))
          return {element->id, QStringLiteral("literal"), i,
                  element->enumLiterals.at(i),
                  textLineRect(rect, line, headerHeight)};
    } else {
      const bool showAttributes =
          node->showAttributes.value_or(d->showAttributes);
      const bool showOperations =
          node->showOperations.value_or(d->showOperations);
      if (showAttributes) {
        for (int i = 0; i < element->attributes.size(); ++i, ++line)
          if (textLineRect(rect, line, headerHeight).contains(scenePoint))
            return {element->id, QStringLiteral("attribute"), i,
                    element->attributes.at(i),
                    textLineRect(rect, line, headerHeight)};
      }
      if (showOperations) {
        for (int i = 0; i < element->operations.size(); ++i, ++line)
          if (textLineRect(rect, line, headerHeight).contains(scenePoint))
            return {element->id, QStringLiteral("operation"), i,
                    element->operations.at(i),
                    textLineRect(rect, line, headerHeight)};
      }
    }
  }
  if (const auto *container = hitContainer(scenePoint)) {
    const QRectF rect = containerGeometry(*container);
    const qreal headerHeight =
        container->subjectKind == QStringLiteral("package")
            ? 24.0
            : kContainerHeaderHeight;
    if (!QRectF(rect.left(), rect.top(), rect.width(), headerHeight)
             .contains(scenePoint))
      return {};
    if (container->subjectKind == QStringLiteral("folder")) {
      if (const auto *folder =
              findBrowserFolder(m_project->data(), container->subjectId))
        return {folder->id,
                QStringLiteral("name"),
                -1,
                folder->name,
                QRectF(rect.left() + 4, rect.top() + 3, rect.width() - 8,
                       headerHeight - 6),
                true};
    } else if (const auto *package =
                   findElement(m_project->data(), container->subjectId)) {
      const QString stereotype = stereotype_catalog::displayText(
          m_project->data(), package->stereotypeIds);
      if (!stereotype.isEmpty() &&
          QRectF(rect.left(), rect.top(), rect.width(), 11.0)
              .contains(scenePoint))
        return {package->id,
                QStringLiteral("stereotypes"),
                -1,
                stereotype,
                QRectF(rect.left() + 4, rect.top() + 1, rect.width() - 8, 10.0),
                false};
      return {
          package->id,
          QStringLiteral("name"),
          -1,
          package->name,
          QRectF(rect.left() + 4,
                 rect.top() + (stereotype.isEmpty() ? 3.0 : 10.0),
                 rect.width() - 8,
                 stereotype.isEmpty() ? headerHeight - 6 : headerHeight - 10),
          true};
    }
  }
  const QFont font = QGuiApplication::font();
  for (auto connector = d->connectors.crbegin();
       connector != d->connectors.crend(); ++connector) {
    const auto *relationship =
        findRelationship(m_project->data(), connector->relationshipId);
    if (!relationship)
      continue;
    const ui::ConnectorRoute route = connectorRoute(*connector);
    const ConnectorAnnotationLayout layout = connectorAnnotationLayout(
        route.points, relationship->name,
        stereotype_catalog::displayText(m_project->data(),
                                        relationship->stereotypeIds),
        relationship->sourceEnd, relationship->targetEnd,
        connector->annotationPlacements, font);
    if (layout.name.contains(scenePoint))
      return {relationship->id, QStringLiteral("name"), -1, relationship->name,
              layout.name};
    if (layout.stereotype.contains(scenePoint))
      return {relationship->id, QStringLiteral("stereotypes"), -1,
              stereotype_catalog::displayText(m_project->data(),
                                              relationship->stereotypeIds),
              layout.stereotype};
    if (layout.sourceRole.contains(scenePoint))
      return {relationship->id, QStringLiteral("sourceRole"), -1,
              relationship->sourceEnd.role, layout.sourceRole};
    if (layout.sourceMultiplicity.contains(scenePoint))
      return {relationship->id, QStringLiteral("sourceMultiplicity"), -1,
              relationship->sourceEnd.multiplicity, layout.sourceMultiplicity};
    if (layout.targetRole.contains(scenePoint))
      return {relationship->id, QStringLiteral("targetRole"), -1,
              relationship->targetEnd.role, layout.targetRole};
    if (layout.targetMultiplicity.contains(scenePoint))
      return {relationship->id, QStringLiteral("targetMultiplicity"), -1,
              relationship->targetEnd.multiplicity, layout.targetMultiplicity};
  }
  return {};
}

void DiagramCanvas::captureSelectedGeometry() {
  m_originalGeometry.clear();
  const auto *currentDiagram = diagram();
  if (!currentDiagram)
    return;

  // A selected frame moves as a subtree. Recording descendants here keeps a
  // mixed Ctrl+A selection visually intact without treating those descendants
  // as independently selected objects.
  QSet<QString> visitedContainers;
  const auto collectContainer =
      [&](const auto &self, const ContainerPresentation &container) -> void {
    if (visitedContainers.contains(container.id))
      return;
    visitedContainers.insert(container.id);
    m_originalGeometry.insert(container.id, containerGeometry(container));
    for (const QString &childId : container.childPresentationIds) {
      if (const auto *childNode = findNode(*currentDiagram, childId))
        m_originalGeometry.insert(childNode->id, nodeGeometry(*childNode));
      else if (const auto *childContainer =
                   findContainer(*currentDiagram, childId))
        self(self, *childContainer);
    }
  };

  for (const QString &containerId : m_selectedContainers)
    if (const auto *container = findContainer(*currentDiagram, containerId))
      collectContainer(collectContainer, *container);
  for (const QString &nodeId : m_selectedNodeOrder)
    if (!m_originalGeometry.contains(nodeId))
      if (const auto *node = findNode(*currentDiagram, nodeId))
        m_originalGeometry.insert(nodeId, nodeGeometry(*node));
}

void DiagramCanvas::mousePressEvent(QMouseEvent *event) {
  forceActiveFocus();
  emit contextToolboxesDismissRequested();
  clearRelationshipToolboxCandidate();
  clearArrangementToolboxCandidate(true);
  clearConnectorToolboxCandidate(true);
  clearPresentationToolboxCandidate(true);
  m_alignmentGuides.clear();
  m_pressView = event->position();
  m_pressScene = toScene(m_pressView);
  m_lastPointerScene = m_pressScene;
  m_pressModifiers = event->modifiers();
  m_resizeHandle = ResizeHandle::None;
  if (event->button() == Qt::MiddleButton) {
    m_interaction = Interaction::Pan;
    m_originalPan = m_pan;
    event->accept();
    return;
  }
  if (event->button() == Qt::RightButton) {
    m_contextScenePoint = m_pressScene;
    m_contextSegment = -1;
    m_contextAnnotationKey.clear();
    QString target = QStringLiteral("canvas");
    if (const auto *node = hitNode(m_pressScene)) {
      selectNode(node->id, false);
      if (m_project)
        m_project->selectObject(node->elementId, QStringLiteral("element"));
      target = QStringLiteral("element");
    } else if (const AnnotationHit annotation =
                   hitConnectorAnnotation(m_pressScene);
               !annotation.connectorId.isEmpty()) {
      const auto *connector = findConnector(*diagram(), annotation.connectorId);
      selectConnector(annotation.connectorId, false);
      m_contextAnnotationKey = annotation.key;
      m_selectedBendPoint =
          connector ? hitBendPoint(*connector, m_pressScene) : -1;
      m_contextSegment =
          connector ? nearestConnectorSegment(*connector, m_pressScene) : -1;
      if (m_project)
        m_project->selectObject(annotation.relationshipId,
                                QStringLiteral("relationship"));
      target = QStringLiteral("connector");
      emit canvasSelectionChanged();
    } else if (const auto *connector = hitConnector(m_pressScene)) {
      selectConnector(connector->id, false);
      m_selectedBendPoint = hitBendPoint(*connector, m_pressScene);
      m_contextSegment = nearestConnectorSegment(*connector, m_pressScene);
      if (m_project)
        m_project->selectObject(connector->relationshipId,
                                QStringLiteral("relationship"));
      target = QStringLiteral("connector");
      emit canvasSelectionChanged();
    } else if (const auto *container = hitContainer(m_pressScene)) {
      m_selectedNodes.clear();
      m_selectedNodeOrder.clear();
      m_selectedConnector.clear();
      m_selectedBendPoint = -1;
      selectOnlyContainer(container->id);
      if (m_project)
        m_project->clearSelection();
      target = QStringLiteral("container");
      m_sceneDirty = true;
      emit canvasSelectionChanged();
      update();
    } else {
      clearCanvasSelection();
      if (m_project)
        m_project->clearSelection();
    }
    emit contextMenuRequested(target, event->position().x(),
                              event->position().y());
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton) {
    event->ignore();
    return;
  }
  m_leftButtonPressed = true;

  if (const auto *d = diagram(); d && !m_selectedConnector.isEmpty()) {
    if (const auto *selected = findConnector(*d, m_selectedConnector)) {
      const int bendPoint = hitBendPoint(*selected, m_pressScene);
      if (bendPoint >= 0) {
        m_selectedBendPoint = bendPoint;
        m_bendPointPreview = selected->bendPoints;
        m_bendPointPreviewActive = true;
        m_interaction = Interaction::MoveBendPoint;
        emit canvasSelectionChanged();
        event->accept();
        return;
      }
    }
  }

  bool sourcePort = false;
  if (event->button() == Qt::LeftButton &&
      hitSelectedPort(m_pressScene, sourcePort)) {
    const auto *selected = findConnector(*diagram(), m_selectedConnector);
    const auto *currentEndpoint = endpointNode(*selected, sourcePort);
    if (!currentEndpoint) {
      event->accept();
      return;
    }
    m_interaction =
        sourcePort ? Interaction::MoveSourcePort : Interaction::MoveTargetPort;
    m_endpointDragAnchor =
        sourcePort ? selected->sourceAnchor : selected->targetAnchor;
    m_endpointDragTargetNode = currentEndpoint->id;
    const ConnectorEndpoints endpoints = connectorEndpoints(*selected);
    m_endpointDragPoint = sourcePort ? endpoints.source : endpoints.target;
    m_endpointDragActive = true;
    m_endpointDragSnapped = false;
    emit canvasSelectionChanged();
    event->accept();
    return;
  }

  if (const AnnotationHit annotation = hitConnectorAnnotation(m_pressScene);
      !annotation.connectorId.isEmpty()) {
    const auto *currentDiagram = diagram();
    const auto *connector =
        currentDiagram ? findConnector(*currentDiagram, annotation.connectorId)
                       : nullptr;
    if (connector) {
      selectConnector(annotation.connectorId, false);
      if (m_project)
        m_project->selectObject(annotation.relationshipId,
                                QStringLiteral("relationship"));
      m_contextAnnotationKey = annotation.key;
      m_annotationDragConnector = annotation.connectorId;
      m_annotationDragKey = annotation.key;
      m_annotationDragOffset = annotation.sceneRect.center() - m_pressScene;
      // Start from a clean placement so extension data from a previously
      // dragged annotation cannot leak into this one.
      m_annotationPreview = {};
      m_annotationPreviewActive = true;
      m_annotationDragMoved = false;
      const ui::ConnectorRoute route = connectorRoute(*connector);
      if (const auto projection = projectOntoPolyline(
              route.points, annotation.sceneRect.center())) {
        const QPointF normal(-projection->tangent.y(), projection->tangent.x());
        const QPointF offset =
            annotation.sceneRect.center() - projection->position;
        m_annotationPreview.routePosition = projection->normalizedDistance;
        m_annotationPreview.tangentOffset =
            QPointF::dotProduct(offset, projection->tangent);
        m_annotationPreview.normalOffset = QPointF::dotProduct(offset, normal);
        const auto existing =
            connector->annotationPlacements.constFind(annotation.key);
        if (existing != connector->annotationPlacements.cend())
          m_annotationPreview.extra = existing->extra;
      }
      m_interaction = Interaction::MoveAnnotation;
      emit canvasSelectionChanged();
      event->accept();
      return;
    }
  }

  const auto *node = hitNode(m_pressScene);
  const bool toggle = event->modifiers().testFlag(Qt::ControlModifier);
  const auto beginLasso = [&] {
    // Delay selection changes until the pointer crosses the drag threshold.
    // This keeps a plain click equivalent to a background click and lets
    // Ctrl/Shift-click on empty space preserve the existing selection.
    m_interaction = Interaction::Lasso;
    m_lassoOrigin = m_pressScene;
    m_lassoRect = QRectF(m_pressScene, m_pressScene);
    m_lassoBaseNodes = m_selectedNodes;
    m_lassoBaseNodeOrder = m_selectedNodeOrder;
    m_lassoBaseContainers = m_selectedContainers;
    m_lassoBaseContainer = m_selectedContainer;
    m_lassoBaseConnector = m_selectedConnector;
    m_lassoBaseBendPoint = m_selectedBendPoint;
    m_lassoModifiers = event->modifiers();
    m_lassoActive = false;
  };
  if (node) {
    selectNode(node->id, toggle);
    m_interactionNode = node->id;
    const QRectF rect = nodeGeometry(*node);
    m_resizeHandle = resizeHandleAt(rect, m_pressScene);
    m_interaction = m_resizeHandle != ResizeHandle::None ? Interaction::Resize
                                                         : Interaction::Move;
    captureSelectedGeometry();
    if (m_project)
      m_project->selectObject(node->elementId, QStringLiteral("element"));
  } else if (const auto *connector = hitConnector(m_pressScene)) {
    selectConnector(connector->id, toggle);
    m_selectedBendPoint = hitBendPoint(*connector, m_pressScene);
    if (m_project) {
      m_project->selectObject(connector->relationshipId,
                              QStringLiteral("relationship"));
    }
  } else if (const auto *container = hitContainer(m_pressScene)) {
    const QRectF rect = containerGeometry(*container);
    m_resizeHandle = resizeHandleAt(rect, m_pressScene);
    const qreal headerHeight =
        container->subjectKind == QStringLiteral("package")
            ? 24.0
            : kContainerHeaderHeight;
    const bool headerHit =
        QRectF(rect.left(), rect.top(), rect.width(), headerHeight)
            .contains(m_pressScene);
    if (!headerHit && m_resizeHandle == ResizeHandle::None) {
      // A container's body is diagram workspace: its child presentations are
      // still ordinary lasso targets. Restrict direct container movement to
      // the header so the frame does not consume interior drag gestures.
      beginLasso();
      event->accept();
      return;
    }

    const bool resize = m_resizeHandle != ResizeHandle::None;
    const bool preserveMultiSelection =
        !resize && m_selectedContainers.contains(container->id);
    if (!preserveMultiSelection) {
      m_selectedNodes.clear();
      m_selectedNodeOrder.clear();
      selectOnlyContainer(container->id);
    }
    m_selectedConnector.clear();
    m_selectedBendPoint = -1;
    m_interactionNode = container->id;
    m_interaction = resize ? Interaction::Resize : Interaction::Move;
    captureSelectedGeometry();
    if (m_project)
      m_project->clearSelection();
    m_sceneDirty = true;
    emit canvasSelectionChanged();
    update();
  } else {
    beginLasso();
  }
  event->accept();
}

void DiagramCanvas::mouseMoveEvent(QMouseEvent *event) {
  m_lastPointerScene = toScene(event->position());
  if (m_interaction == Interaction::Pan) {
    m_pan = m_originalPan + event->position() - m_pressView;
    emit viewportChanged();
    update();
    return;
  }
  if (m_interaction == Interaction::MoveSourcePort ||
      m_interaction == Interaction::MoveTargetPort) {
    updateEndpointDrag(toScene(event->position()),
                       event->modifiers().testFlag(Qt::AltModifier));
    return;
  }
  if (m_interaction == Interaction::MoveBendPoint) {
    updateBendPointPreview(toScene(event->position()));
    return;
  }
  if (m_interaction == Interaction::MoveAnnotation) {
    updateAnnotationPreview(toScene(event->position()));
    return;
  }
  if (m_interaction == Interaction::CreateConnector) {
    updateConnectorGesture(m_lastPointerScene,
                           event->modifiers().testFlag(Qt::AltModifier));
    return;
  }
  if (m_interaction == Interaction::Lasso) {
    constexpr qreal kDragThreshold = 4.0;
    if (!m_lassoActive &&
        QLineF(m_pressView, event->position()).length() < kDragThreshold)
      return;
    if (!m_lassoActive) {
      m_lassoActive = true;
      if (m_project)
        m_project->clearSelection();
    }
    updateLassoSelection(toScene(event->position()));
    return;
  }
  const QPointF delta = toScene(event->position()) - m_pressScene;
  if (m_interaction == Interaction::Move) {
    ui::DiagramSnapResult snapResult{delta, {}};
    const auto *d = diagram();
    const bool suppressSnapping = event->modifiers().testFlag(Qt::AltModifier);
    if (d && !suppressSnapping &&
        (m_snapToGridEnabled || m_alignmentGuidesEnabled)) {
      QList<ui::DiagramNodeGeometry> moving;
      QList<ui::DiagramNodeGeometry> stationary;
      moving.reserve(m_originalGeometry.size());
      stationary.reserve(d->nodes.size() + d->containers.size());
      for (auto original = m_originalGeometry.cbegin();
           original != m_originalGeometry.cend(); ++original)
        moving.append({original.key(), original.value()});
      for (const auto &node : d->nodes) {
        if (!m_originalGeometry.contains(node.id) && nodePassesFilter(node))
          stationary.append({node.id, node.geometry});
      }
      for (const auto &container : d->containers)
        if (!m_originalGeometry.contains(container.id))
          stationary.append({container.id, container.geometry});
      const ui::DiagramSnapOptions options{
          m_snapToGridEnabled, m_alignmentGuidesEnabled,
          static_cast<qreal>(m_gridSpacing), kSnapToleranceViewPixels / m_zoom};
      snapResult = ui::snapDiagramMove(moving, stationary, m_interactionNode,
                                       delta, options);
    }
    m_previewGeometry.clear();
    for (auto it = m_originalGeometry.cbegin(); it != m_originalGeometry.cend();
         ++it)
      m_previewGeometry.insert(it.key(),
                               it.value().translated(snapResult.delta));
    m_alignmentGuides = std::move(snapResult.guides);
    m_sceneDirty = true;
    m_textDirty = true;
    update();
  } else if (m_interaction == Interaction::Resize) {
    m_previewGeometry.clear();
    const QRectF original = m_originalGeometry.value(m_interactionNode);
    const auto *currentDiagram = diagram();
    const bool resizingContainer =
        currentDiagram && findContainer(*currentDiagram, m_interactionNode);
    const qreal minimumWidth =
        resizingContainer ? kMinimumContainerWidth : kMinimumNodeWidth;
    const qreal minimumHeight =
        resizingContainer ? kMinimumContainerHeight : kMinimumNodeHeight;
    QRectF resized = original;
    const Qt::Edges movingEdges = resizeEdges(m_resizeHandle);
    if (movingEdges.testFlag(Qt::LeftEdge))
      resized.setLeft(qMin(original.left() + delta.x(),
                           original.right() - minimumWidth));
    else if (movingEdges.testFlag(Qt::RightEdge))
      resized.setRight(qMax(original.right() + delta.x(),
                            original.left() + minimumWidth));
    if (movingEdges.testFlag(Qt::TopEdge))
      resized.setTop(qMin(original.top() + delta.y(),
                          original.bottom() - minimumHeight));
    else if (movingEdges.testFlag(Qt::BottomEdge))
      resized.setBottom(qMax(original.bottom() + delta.y(),
                             original.top() + minimumHeight));

    ui::DiagramResizeSnapResult snapResult{resized, {}};
    const auto *d = diagram();
    const bool suppressSnapping = event->modifiers().testFlag(Qt::AltModifier);
    if (d && !suppressSnapping &&
        (m_snapToGridEnabled || m_alignmentGuidesEnabled)) {
      QList<ui::DiagramNodeGeometry> stationary;
      stationary.reserve(d->nodes.size() + d->containers.size());
      for (const auto &node : d->nodes)
        if (node.id != m_interactionNode && nodePassesFilter(node) &&
            (!resizingContainer || !m_originalGeometry.contains(node.id)))
          stationary.append({node.id, node.geometry});
      for (const auto &container : d->containers)
        if (container.id != m_interactionNode &&
            (!resizingContainer || !m_originalGeometry.contains(container.id)))
          stationary.append({container.id, container.geometry});
      const ui::DiagramSnapOptions options{
          m_snapToGridEnabled, m_alignmentGuidesEnabled,
          static_cast<qreal>(m_gridSpacing), kSnapToleranceViewPixels / m_zoom};
      snapResult = ui::snapDiagramResize(
          resized, stationary, QSizeF(minimumWidth, minimumHeight), movingEdges,
          options);
    }
    m_previewGeometry.insert(m_interactionNode, snapResult.geometry);
    m_alignmentGuides = std::move(snapResult.guides);
    m_sceneDirty = true;
    m_textDirty = true;
    update();
  }
}

void DiagramCanvas::mouseReleaseEvent(QMouseEvent *event) {
  m_lastPointerScene = toScene(event->position());
  if (event->button() == Qt::LeftButton)
    m_leftButtonPressed = false;
  if (m_interaction == Interaction::CreateConnector) {
    commitConnectorGesture(m_lastPointerScene,
                           event->modifiers().testFlag(Qt::AltModifier));
    event->accept();
    return;
  }
  if (m_interaction == Interaction::Lasso) {
    if (m_lassoActive) {
      updateLassoSelection(toScene(event->position()));
      finishLassoSelection();
    } else {
      const bool preserve = m_lassoModifiers.testFlag(Qt::ControlModifier) ||
                            m_lassoModifiers.testFlag(Qt::ShiftModifier);
      if (!preserve) {
        clearCanvasSelection();
        if (m_project)
          m_project->clearSelection();
      }
      resetLassoState();
    }
    m_interaction = Interaction::None;
    event->accept();
    return;
  }
  if (m_interaction == Interaction::Move ||
      m_interaction == Interaction::Resize)
    commitGeometryPreview();
  else if (m_interaction == Interaction::MoveSourcePort ||
           m_interaction == Interaction::MoveTargetPort) {
    updateEndpointDrag(m_lastPointerScene,
                       event->modifiers().testFlag(Qt::AltModifier));
    commitEndpointDrag();
  } else if (m_interaction == Interaction::MoveBendPoint)
    commitBendPointPreview();
  else if (m_interaction == Interaction::MoveAnnotation)
    commitAnnotationPreview();
  m_interaction = Interaction::None;
  m_resizeHandle = ResizeHandle::None;
  m_originalGeometry.clear();
  m_previewGeometry.clear();
  m_alignmentGuides.clear();
  m_endpointDragActive = false;
  m_endpointDragTargetNode.clear();
  m_endpointDragSnapped = false;
  m_bendPointPreview.clear();
  m_bendPointPreviewActive = false;
  m_annotationPreviewActive = false;
  m_annotationDragConnector.clear();
  m_annotationDragKey.clear();
  m_annotationDragMoved = false;
  m_sceneDirty = true;
  // Preview geometry also drives the text atlas. Rebuild it after clearing the
  // preview so labels and frames return to the same committed position.
  m_textDirty = true;
  update();
}

void DiagramCanvas::mouseDoubleClickEvent(QMouseEvent *event) {
  const QPointF scenePoint = toScene(event->position());
  const TextHit hit = hitText(scenePoint);
  if (!hit.objectId.isEmpty()) {
    if (hit.field == QStringLiteral("stereotypes")) {
      const QString kind = findRelationship(m_project->data(), hit.objectId)
                               ? QStringLiteral("relationship")
                               : QStringLiteral("element");
      const QRectF view = toView(hit.sceneRect);
      emit stereotypeEditRequested(hit.objectId, kind, view.x(), view.y(),
                                   view.width(), view.height());
      event->accept();
      return;
    }
    const QRectF view = toView(hit.sceneRect);
    // The editor is a QML overlay in view coordinates, while canvas text is
    // defined in scene coordinates and enlarged by the canvas transform.
    const qreal fontPixelSize =
        qMax(1.0, QFontInfo(QGuiApplication::font()).pixelSize() * m_zoom);
    emit editRequested(hit.objectId, hit.field, hit.index, hit.text, view.x(),
                       view.y(), view.width(), view.height(), fontPixelSize,
                       hit.fontBold);
    event->accept();
    return;
  }
  if (const auto *connector = hitConnector(scenePoint)) {
    selectConnector(connector->id, false);
    m_contextScenePoint = scenePoint;
    m_contextSegment = nearestConnectorSegment(*connector, scenePoint);
    addBendPointAtContextPosition();
    event->accept();
  }
}

void DiagramCanvas::hoverMoveEvent(QHoverEvent *event) {
  m_lastPointerScene = toScene(event->position());
  ResizeHandle hoveredHandle = ResizeHandle::None;
  if (const auto *node = hitNode(m_lastPointerScene))
    hoveredHandle = resizeHandleAt(nodeGeometry(*node), m_lastPointerScene);
  else if (const auto *container = hitContainer(m_lastPointerScene))
    hoveredHandle =
        resizeHandleAt(containerGeometry(*container), m_lastPointerScene);

  if (hoveredHandle == ResizeHandle::TopLeft ||
      hoveredHandle == ResizeHandle::BottomRight)
    setCursor(Qt::SizeFDiagCursor);
  else if (hoveredHandle == ResizeHandle::TopRight ||
           hoveredHandle == ResizeHandle::BottomLeft)
    setCursor(Qt::SizeBDiagCursor);
  else
    unsetCursor();

  updateRelationshipToolboxCandidate(event->position());
  if (m_relationshipToolboxCandidate) {
    clearArrangementToolboxCandidate();
    clearConnectorToolboxCandidate();
    clearPresentationToolboxCandidate();
  } else {
    updateConnectorToolboxCandidate(event->position());
    if (m_connectorToolboxCandidate) {
      clearArrangementToolboxCandidate();
      clearPresentationToolboxCandidate();
    } else {
      updateArrangementToolboxCandidate(event->position());
      if (m_arrangementToolboxCandidate)
        clearPresentationToolboxCandidate();
      else
        updatePresentationToolboxCandidate(event->position());
    }
  }
  event->accept();
}

void DiagramCanvas::hoverLeaveEvent(QHoverEvent *event) {
  unsetCursor();
  clearRelationshipToolboxCandidate();
  clearArrangementToolboxCandidate();
  clearConnectorToolboxCandidate();
  clearPresentationToolboxCandidate();
  event->accept();
}

void DiagramCanvas::wheelEvent(QWheelEvent *event) {
  clearRelationshipToolboxCandidate();
  clearArrangementToolboxCandidate(true);
  clearConnectorToolboxCandidate(true);
  clearPresentationToolboxCandidate(true);
  const QPointF before = toScene(event->position());
  const qreal factor = event->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12;
  m_zoom = std::clamp(m_zoom * factor, 0.2, 4.0);
  m_pan = event->position() - before * m_zoom;
  m_sceneDirty = true;
  emit viewportChanged();
  update();
  event->accept();
}

void DiagramCanvas::commitGeometryPreview() {
  if (!m_project || m_previewGeometry.isEmpty())
    return;
  QVariantList values;
  for (auto it = m_previewGeometry.cbegin(); it != m_previewGeometry.cend();
       ++it) {
    QVariantMap map;
    map.insert(QStringLiteral("id"), it.key());
    map.insert(QStringLiteral("x"), it.value().x());
    map.insert(QStringLiteral("y"), it.value().y());
    map.insert(QStringLiteral("width"), it.value().width());
    map.insert(QStringLiteral("height"), it.value().height());
    values.append(map);
  }
  const bool moving = m_interaction == Interaction::Move;
  const bool includesContainers = !m_selectedContainers.isEmpty();
  const QString description =
      includesContainers
          ? (moving ? QStringLiteral("Move diagram selection")
                    : QStringLiteral("Resize diagram container"))
          : (moving ? QStringLiteral("Move diagram elements")
                    : QStringLiteral("Resize diagram element"));
  if (!moving) {
    m_project->updatePresentationGeometries(m_diagramId, values, description);
    return;
  }

  QStringList movedPresentationIds;
  QSet<QString> excludedDropTargets;
  for (const QString &containerId : m_selectedContainers) {
    if (m_originalGeometry.contains(containerId))
      movedPresentationIds.append(containerId);
  }
  if (!m_selectedContainers.isEmpty()) {
    // A moving container carries its complete subtree. None of those frames
    // can be a legal drop target because reparenting to one would create a
    // membership cycle.
    for (auto geometry = m_originalGeometry.cbegin();
         geometry != m_originalGeometry.cend(); ++geometry)
      excludedDropTargets.insert(geometry.key());
  }
  for (const QString &nodeId : m_selectedNodeOrder)
    if (m_originalGeometry.contains(nodeId))
      movedPresentationIds.append(nodeId);

  const auto *target = hitContainer(m_lastPointerScene, excludedDropTargets);
  const QString targetId = target ? target->id : QString{};
  if (!m_project->canMovePresentationsToContainer(
          m_diagramId, movedPresentationIds, targetId))
    return;
  m_project->movePresentationsToContainer(
      m_diagramId, values, movedPresentationIds, targetId, description);
}

void DiagramCanvas::updateLassoSelection(const QPointF &scenePoint) {
  const auto *d = diagram();
  if (!d)
    return;

  m_lassoRect = QRectF(m_lassoOrigin, scenePoint).normalized();
  QSet<QString> nextNodes;
  QStringList nextOrder;
  const bool toggle = m_lassoModifiers.testFlag(Qt::ControlModifier);
  const bool add = !toggle && m_lassoModifiers.testFlag(Qt::ShiftModifier);
  if (toggle || add) {
    nextNodes = m_lassoBaseNodes;
    nextOrder = m_lassoBaseNodeOrder;
  }

  for (const auto &node : d->nodes) {
    if (!nodePassesFilter(node))
      continue;
    if (!m_lassoRect.intersects(visibleNodeGeometry(node)))
      continue;
    if (toggle && nextNodes.contains(node.id)) {
      nextNodes.remove(node.id);
      nextOrder.removeAll(node.id);
    } else if (!nextNodes.contains(node.id)) {
      nextNodes.insert(node.id);
      nextOrder.append(node.id);
    }
  }

  const bool selectionChanged =
      nextNodes != m_selectedNodes || nextOrder != m_selectedNodeOrder ||
      !m_selectedContainers.isEmpty() || !m_selectedConnector.isEmpty();
  m_selectedNodes = std::move(nextNodes);
  m_selectedNodeOrder = std::move(nextOrder);
  clearContainerSelection();
  m_selectedConnector.clear();
  m_selectedBendPoint = -1;
  m_endpointDragActive = false;
  m_endpointDragTargetNode.clear();
  m_bendPointPreviewActive = false;
  m_sceneDirty = true;
  if (selectionChanged)
    emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::finishLassoSelection() {
  synchronizeProjectSelection();
  resetLassoState();
}

void DiagramCanvas::cancelLassoSelection() {
  m_selectedNodes = m_lassoBaseNodes;
  m_selectedNodeOrder = m_lassoBaseNodeOrder;
  m_selectedContainers = m_lassoBaseContainers;
  m_selectedContainer = m_lassoBaseContainer;
  m_selectedConnector = m_lassoBaseConnector;
  m_selectedBendPoint = m_lassoBaseBendPoint;
  synchronizeProjectSelection();
  resetLassoState();
  m_interaction = Interaction::None;
  m_sceneDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::resetLassoState() {
  m_lassoOrigin = {};
  m_lassoRect = {};
  m_lassoBaseNodes.clear();
  m_lassoBaseNodeOrder.clear();
  m_lassoBaseContainers.clear();
  m_lassoBaseContainer.clear();
  m_lassoBaseConnector.clear();
  m_lassoBaseBendPoint = -1;
  m_lassoModifiers = Qt::NoModifier;
  m_lassoActive = false;
  m_sceneDirty = true;
  update();
}

void DiagramCanvas::selectAllInContext() {
  const auto *d = diagram();
  if (!d)
    return;

  const auto ownerContainerId = [&](const QString &presentationId) {
    const auto owner = std::find_if(
        d->containers.cbegin(), d->containers.cend(),
        [&](const ContainerPresentation &container) {
          return container.childPresentationIds.contains(presentationId);
        });
    return owner == d->containers.cend() ? QString{} : owner->id;
  };

  QString scopeContainerId = m_selectedContainer;
  if (scopeContainerId.isEmpty()) {
    // A selected child makes its owner the natural selection scope. All
    // selected presentation kinds participate so a prior Ctrl+A remains in
    // the same one-level scope even when that level contains frames.
    QStringList selectedPresentationIds = m_selectedNodeOrder;
    selectedPresentationIds.append(m_selectedContainers.values());
    if (!selectedPresentationIds.isEmpty()) {
      scopeContainerId =
          ownerContainerId(selectedPresentationIds.constFirst());
      for (const QString &presentationId : selectedPresentationIds) {
        if (ownerContainerId(presentationId) != scopeContainerId) {
          // Mixed owners do not define a unique nested scope. Fall back to
          // diagram-level objects instead of choosing an arbitrary frame.
          scopeContainerId.clear();
          break;
        }
      }
    }
  }

  QSet<QString> scopedNodeIds;
  QSet<QString> scopedContainerIds;
  if (!scopeContainerId.isEmpty()) {
    // Selection follows one visible hierarchy level. Nested frames are
    // selected as objects, while their children remain outside this scope.
    if (const auto *container = findContainer(*d, scopeContainerId)) {
      for (const QString &childId : container->childPresentationIds) {
        if (const auto *node = findNode(*d, childId)) {
          if (nodePassesFilter(*node))
            scopedNodeIds.insert(childId);
        } else if (findContainer(*d, childId)) {
          scopedContainerIds.insert(childId);
        }
      }
    }
  } else {
    QSet<QString> containedPresentationIds;
    for (const auto &container : d->containers)
      for (const QString &childId : container.childPresentationIds)
        containedPresentationIds.insert(childId);
    for (const auto &node : d->nodes)
      if (!containedPresentationIds.contains(node.id) && nodePassesFilter(node))
        scopedNodeIds.insert(node.id);
    for (const auto &container : d->containers)
      if (!containedPresentationIds.contains(container.id))
        scopedContainerIds.insert(container.id);
  }

  // Keep an empty selected frame active; Ctrl+A in it is a harmless no-op.
  if (scopedNodeIds.isEmpty() && scopedContainerIds.isEmpty())
    return;

  clearRelationshipToolboxCandidate();
  clearArrangementToolboxCandidate(true);
  clearConnectorToolboxCandidate(true);
  clearPresentationToolboxCandidate(true);
  emit contextToolboxesDismissRequested();

  m_selectedNodes = scopedNodeIds;
  m_selectedNodeOrder.clear();
  m_selectedNodeOrder.reserve(scopedNodeIds.size());
  for (const auto &node : d->nodes)
    if (scopedNodeIds.contains(node.id))
      m_selectedNodeOrder.append(node.id);
  m_selectedContainers = std::move(scopedContainerIds);
  m_selectedContainer.clear();
  m_selectedConnector.clear();
  m_selectedBendPoint = -1;
  m_endpointDragTargetNode.clear();
  m_endpointDragActive = false;
  m_contextAnnotationKey.clear();
  synchronizeProjectSelection();
  m_sceneDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::synchronizeProjectSelection() {
  if (!m_project)
    return;
  if (!m_selectedConnector.isEmpty()) {
    const auto *d = diagram();
    const auto *connector =
        d ? findConnector(*d, m_selectedConnector) : nullptr;
    if (connector) {
      m_project->selectObject(connector->relationshipId,
                              QStringLiteral("relationship"));
      return;
    }
  }
  if (m_selectedNodes.size() == 1) {
    const auto *d = diagram();
    const QString nodeId = m_selectedNodeOrder.value(0);
    const auto *node = d ? findNode(*d, nodeId) : nullptr;
    if (node) {
      m_project->selectObject(node->elementId, QStringLiteral("element"));
      return;
    }
  }
  m_project->clearSelection();
}

void DiagramCanvas::fitToContent() {
  const auto *d = diagram();
  if (!d || (d->nodes.isEmpty() && d->containers.isEmpty()) || width() <= 0 ||
      height() <= 0)
    return;
  QRectF content;
  for (const auto &container : d->containers)
    content = content.isNull() ? container.geometry
                               : content.united(container.geometry);
  for (const auto &node : d->nodes)
    if (nodePassesFilter(node))
      content =
          content.isNull() ? node.geometry : content.united(node.geometry);
  for (const auto &connector : d->connectors) {
    if (!connectorPassesFilter(connector))
      continue;
    for (const auto &bendPoint : connector.bendPoints) {
      const QRectF bendBounds(bendPoint.position - QPointF(1.0, 1.0),
                              QSizeF(2.0, 2.0));
      content = content.united(bendBounds);
    }
  }
  content.adjust(-40, -40, 40, 40);
  m_zoom = std::clamp(
      qMin(width() / content.width(), height() / content.height()), 0.2, 2.0);
  m_pan = QPointF((width() - content.width() * m_zoom) / 2.0,
                  (height() - content.height() * m_zoom) / 2.0) -
          content.topLeft() * m_zoom;
  m_sceneDirty = true;
  emit viewportChanged();
  update();
}

QVariantMap DiagramCanvas::diagramFilter() const {
  return m_project ? m_project->diagramFilter(m_diagramId) : QVariantMap{};
}

void DiagramCanvas::setDiagramFilter(const QVariantMap &filter) {
  if (m_project)
    m_project->setDiagramFilter(m_diagramId, filter);
}

void DiagramCanvas::clearDiagramFilter() { setDiagramFilter({}); }

void DiagramCanvas::addElementsAt(const QStringList &elementIds, qreal x,
                                  qreal y) {
  if (!m_project || m_diagramId.isEmpty() || elementIds.isEmpty())
    return;
  forceActiveFocus();
  const QPointF scenePoint = toScene({x, y});
  m_project->addElementsToDiagram(m_diagramId, elementIds, scenePoint.x(),
                                  scenePoint.y(), m_diagramItemSizingMode);
}

void DiagramCanvas::addTreeItemsAt(const QStringList &elementIds,
                                   const QString &subjectsJson, qreal x,
                                   qreal y) {
  if (!m_project || m_diagramId.isEmpty())
    return;
  forceActiveFocus();
  const QPointF scenePoint = toScene({x, y});
  m_project->addTreeItemsToDiagram(m_diagramId, elementIds, subjectsJson,
                                   scenePoint.x(), scenePoint.y(),
                                   m_diagramItemSizingMode);
}

void DiagramCanvas::createElementAtContextPosition(const QString &type) {
  createElementAt(type, m_contextScenePoint);
}

void DiagramCanvas::createElementAtViewportCenter(const QString &type) {
  createElementAt(type, toScene({width() / 2.0, height() / 2.0}));
}

void DiagramCanvas::createElementAt(const QString &type,
                                    const QPointF &sceneCenter) {
  if (!m_project)
    return;
  const QString elementId = m_project->addElementCenteredAt(
      type, m_diagramId, sceneCenter.x(), sceneCenter.y());
  const auto *d = diagram();
  if (elementId.isEmpty() || !d)
    return;
  const auto node = std::find_if(d->nodes.cbegin(), d->nodes.cend(),
                                 [&](const NodePresentation &candidate) {
                                   return candidate.elementId == elementId;
                                 });
  if (node != d->nodes.cend()) {
    if (nodePassesFilter(*node))
      selectNode(node->id, false);
    else
      clearCanvasSelection();
  } else {
    const auto container = std::find_if(
        d->containers.cbegin(), d->containers.cend(),
        [&](const ContainerPresentation &candidate) {
          return candidate.subjectKind == QStringLiteral("package") &&
                 candidate.subjectId == elementId;
        });
    if (container != d->containers.cend()) {
      m_selectedNodes.clear();
      m_selectedNodeOrder.clear();
      m_selectedConnector.clear();
      selectOnlyContainer(container->id);
      emit canvasSelectionChanged();
      update();
    }
  }
}

void DiagramCanvas::createRelationship(const QString &type) {
  if (!m_project || m_selectedNodes.size() != 2)
    return;
  const QStringList ids = m_selectedNodeOrder;
  m_selectedConnector = m_project->createRelationshipWithRouting(
      m_diagramId, ids.at(0), ids.at(1), type,
      toString(m_defaultConnectorRouting));
  m_selectedBendPoint = -1;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::cancelConnectorInteraction() {
  if (m_interaction == Interaction::CreateConnector)
    cancelConnectorGesture();
  else if (m_interaction == Interaction::MoveSourcePort ||
           m_interaction == Interaction::MoveTargetPort)
    cancelEndpointDrag();
}

void DiagramCanvas::addBendPointAtContextPosition() {
  if (!m_project || m_selectedConnector.isEmpty() || m_contextSegment < 0)
    return;
  m_project->insertConnectorBendPoint(m_diagramId, m_selectedConnector,
                                      m_contextSegment, m_contextScenePoint.x(),
                                      m_contextScenePoint.y());
  m_selectedBendPoint = m_contextSegment;
  m_contextSegment = -1;
  m_sceneDirty = true;
  m_textDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::removeSelectedBendPoint() {
  if (!m_project || m_selectedConnector.isEmpty() || !bendPointSelected())
    return;
  m_project->removeConnectorBendPoint(m_diagramId, m_selectedConnector,
                                      m_selectedBendPoint);
  m_selectedBendPoint = -1;
  m_sceneDirty = true;
  m_textDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::clearSelectedConnectorBendPoints() {
  if (!m_project || m_selectedConnector.isEmpty())
    return;
  m_project->clearConnectorBendPoints(m_diagramId, m_selectedConnector);
  m_selectedBendPoint = -1;
  m_sceneDirty = true;
  m_textDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::resetContextAnnotationPosition() {
  if (!m_project || m_selectedConnector.isEmpty() ||
      m_contextAnnotationKey.isEmpty())
    return;
  m_project->resetConnectorAnnotationPlacement(m_diagramId, m_selectedConnector,
                                               m_contextAnnotationKey);
  m_sceneDirty = true;
  m_textDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::resetSelectedConnectorAnnotationPositions() {
  if (!m_project || m_selectedConnector.isEmpty())
    return;
  m_project->resetConnectorAnnotationPlacements(m_diagramId,
                                                m_selectedConnector);
  m_sceneDirty = true;
  m_textDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::editSelectedConnectorAnnotation(
    const QString &annotationField) {
  const auto *d = diagram();
  const auto *connector = d ? findConnector(*d, m_selectedConnector) : nullptr;
  const auto *relationship =
      connector && m_project
          ? findRelationship(m_project->data(), connector->relationshipId)
          : nullptr;
  if (!connector || !relationship)
    return;

  QString name = relationship->name;
  QString stereotype = stereotype_catalog::displayText(
      m_project->data(), relationship->stereotypeIds);
  RelationshipEnd sourceEnd = relationship->sourceEnd;
  RelationshipEnd targetEnd = relationship->targetEnd;
  QString currentText;

  // Empty annotations have no rendered hit rectangle. Supply a short sizing
  // placeholder only while calculating the editor target so every optional
  // field can still be created directly from the contextual toolbox.
  if (annotationField == QStringLiteral("name")) {
    currentText = name;
    if (name.isEmpty())
      name = QStringLiteral("relationship");
  } else if (annotationField == QStringLiteral("stereotypes")) {
    currentText = stereotype;
    if (stereotype.isEmpty())
      stereotype = QStringLiteral("«stereotype»");
  } else if (annotationField == QStringLiteral("sourceRole")) {
    currentText = sourceEnd.role;
    if (sourceEnd.role.isEmpty())
      sourceEnd.role = QStringLiteral("role");
  } else if (annotationField == QStringLiteral("sourceMultiplicity")) {
    currentText = sourceEnd.multiplicity;
    if (sourceEnd.multiplicity.isEmpty())
      sourceEnd.multiplicity = QStringLiteral("1..*");
  } else if (annotationField == QStringLiteral("targetRole")) {
    currentText = targetEnd.role;
    if (targetEnd.role.isEmpty())
      targetEnd.role = QStringLiteral("role");
  } else if (annotationField == QStringLiteral("targetMultiplicity")) {
    currentText = targetEnd.multiplicity;
    if (targetEnd.multiplicity.isEmpty())
      targetEnd.multiplicity = QStringLiteral("1..*");
  } else {
    return;
  }

  const ui::ConnectorRoute route = connectorRoute(*connector);
  const QFont font = QGuiApplication::font();
  const ConnectorAnnotationLayout layout = connectorAnnotationLayout(
      route.points, name, stereotype, sourceEnd, targetEnd,
      connector->annotationPlacements, font);
  const QHash<QString, QRectF> targets = {
      {QStringLiteral("name"), layout.name},
      {QStringLiteral("stereotypes"), layout.stereotype},
      {QStringLiteral("sourceRole"), layout.sourceRole},
      {QStringLiteral("sourceMultiplicity"), layout.sourceMultiplicity},
      {QStringLiteral("targetRole"), layout.targetRole},
      {QStringLiteral("targetMultiplicity"), layout.targetMultiplicity}};
  const QRectF sceneTarget = targets.value(annotationField);
  if (sceneTarget.isEmpty())
    return;
  const QRectF viewTarget = toView(sceneTarget);

  if (annotationField == QStringLiteral("stereotypes")) {
    emit stereotypeEditRequested(
        relationship->id, QStringLiteral("relationship"), viewTarget.x(),
        viewTarget.y(), viewTarget.width(), viewTarget.height());
    return;
  }

  const qreal fontPixelSize = qMax(1.0, QFontInfo(font).pixelSize() * m_zoom);
  emit editRequested(relationship->id, annotationField, -1, currentText,
                     viewTarget.x(), viewTarget.y(), viewTarget.width(),
                     viewTarget.height(), fontPixelSize, false);
}

void DiagramCanvas::editSelectedPresentationName() {
  const auto *diagramData = diagram();
  if (!diagramData || !m_project)
    return;

  QString objectId;
  QString currentName;
  QRectF sceneTarget;
  QRectF visibleGeometry;
  if (m_selectedNodes.size() == 1) {
    const auto *node = findNode(*diagramData, *m_selectedNodes.constBegin());
    const auto *element =
        node ? findElement(m_project->data(), node->elementId) : nullptr;
    if (!node || !element)
      return;
    objectId = element->id;
    currentName = presentation_layout::elementDisplayNameInPackage(
        m_project->data(), *element,
        presentation_layout::containingPackageElementId(*diagramData,
                                                        node->id));
    visibleGeometry = visibleNodeGeometry(*node);
    const QString stereotype = stereotype_catalog::displayText(
        m_project->data(), element->stereotypeIds);
    sceneTarget =
        QRectF(nodeGeometry(*node).left() + 4.0,
               nodeGeometry(*node).top() +
                   (stereotype.isEmpty() ? 0.0 : kLineHeight) + 3.0,
               nodeGeometry(*node).width() - 8.0, kHeaderHeight - 6.0);
  } else if (!m_selectedContainer.isEmpty()) {
    const auto *container = findContainer(*diagramData, m_selectedContainer);
    if (!container)
      return;
    visibleGeometry = visibleContainerGeometry(*container);
    const QRectF geometry = containerGeometry(*container);
    const qreal headerHeight =
        container->subjectKind == QStringLiteral("package")
            ? 24.0
            : kContainerHeaderHeight;
    qreal titleTop = geometry.top() + 3.0;
    qreal titleHeight = headerHeight - 6.0;
    if (container->subjectKind == QStringLiteral("folder")) {
      const auto *folder =
          findBrowserFolder(m_project->data(), container->subjectId);
      if (!folder)
        return;
      objectId = folder->id;
      currentName = folder->name;
    } else {
      const auto *package =
          findElement(m_project->data(), container->subjectId);
      if (!package)
        return;
      objectId = package->id;
      currentName = package->name;
      const QString stereotype = stereotype_catalog::displayText(
          m_project->data(), package->stereotypeIds);
      if (!stereotype.isEmpty()) {
        titleTop = geometry.top() + 10.0;
        titleHeight = headerHeight - 10.0;
      }
    }
    sceneTarget = QRectF(geometry.left() + 4.0, titleTop,
                         geometry.width() - 8.0, titleHeight);
  } else {
    return;
  }

  sceneTarget = sceneTarget.intersected(visibleGeometry);
  if (sceneTarget.isEmpty()) {
    // A clipped child can remain selected while its header is outside the
    // container viewport. Keep the command usable by placing the editor in the
    // first visible strip of that presentation.
    sceneTarget =
        QRectF(visibleGeometry.left() + 4.0, visibleGeometry.top() + 2.0,
               qMax(24.0, visibleGeometry.width() - 8.0),
               qMin(kHeaderHeight - 4.0, visibleGeometry.height() - 4.0));
  }
  if (sceneTarget.width() <= 0.0 || sceneTarget.height() <= 0.0)
    return;

  const QRectF viewTarget = toView(sceneTarget);
  const qreal fontPixelSize =
      qMax(1.0, QFontInfo(QGuiApplication::font()).pixelSize() * m_zoom);
  emit editRequested(objectId, QStringLiteral("name"), -1, currentName,
                     viewTarget.x(), viewTarget.y(), viewTarget.width(),
                     viewTarget.height(), fontPixelSize, true);
}

void DiagramCanvas::setSelectedConnectorRouting(const QString &routing) {
  if (!m_project || m_selectedConnector.isEmpty())
    return;
  m_project->setConnectorRouting(m_diagramId, m_selectedConnector, routing);
}

void DiagramCanvas::setSelectedPortSnapPoints(int horizontalPointCount,
                                              int verticalPointCount) {
  if (!m_project || m_selectedNodes.size() != 1)
    return;
  m_project->setNodePortSnapPoints(m_diagramId, *m_selectedNodes.constBegin(),
                                   horizontalPointCount, verticalPointCount);
}

void DiagramCanvas::setDiagramCompartmentVisible(const QString &compartment,
                                                 bool visible) {
  if (m_project)
    m_project->setDiagramCompartmentVisible(m_diagramId, compartment, visible);
}

void DiagramCanvas::setSelectedCompartmentVisibility(
    const QString &compartment, const QString &visibility) {
  if (!m_project || m_selectedNodeOrder.isEmpty())
    return;
  m_project->setNodesCompartmentVisibility(
      m_diagramId, m_selectedNodeOrder, compartment, visibility);
}

void DiagramCanvas::addRelatedTypes(const QString &direction) {
  if (!m_project || m_selectedNodeOrder.size() != 1)
    return;
  m_project->addRelatedElementsToDiagram(m_diagramId,
                                         m_selectedNodeOrder.constFirst(),
                                         direction, m_diagramItemSizingMode);
}

void DiagramCanvas::fitSelectionToContent() {
  const auto *d = diagram();
  if (!m_project || !d)
    return;

  QVariantList geometries;
  geometries.reserve(m_selectedNodeOrder.size() +
                     m_selectedContainers.size());
  const auto appendGeometry = [&](const QString &id, const QRectF &geometry) {
    QVariantMap value;
    value.insert(QStringLiteral("id"), id);
    value.insert(QStringLiteral("x"), geometry.x());
    value.insert(QStringLiteral("y"), geometry.y());
    value.insert(QStringLiteral("width"), geometry.width());
    value.insert(QStringLiteral("height"), geometry.height());
    geometries.append(value);
  };

  for (const QString &nodeId : m_selectedNodeOrder) {
    const auto *node = findNode(*d, nodeId);
    const auto *element =
        node ? findElement(m_project->data(), node->elementId) : nullptr;
    if (!node || !element)
      continue;
    QRectF fitted = node->geometry;
    const QString displayName =
        presentation_layout::elementDisplayNameInPackage(
            m_project->data(), *element,
            presentation_layout::containingPackageElementId(*d, node->id));
    fitted.setSize(presentation_layout::nodeContentSizeForDisplayName(
        m_project->data(), *element, displayName,
        node->showAttributes.value_or(d->showAttributes),
        node->showOperations.value_or(d->showOperations)));
    appendGeometry(nodeId, fitted);
  }
  for (const QString &containerId : m_selectedContainers) {
    if (const auto *container = findContainer(*d, containerId)) {
      appendGeometry(container->id,
                     presentation_layout::containerContentGeometry(
                         m_project->data(), *d, *container));
    }
  }

  if (geometries.isEmpty())
    return;
  const bool plural = geometries.size() > 1;
  m_project->updatePresentationGeometries(
      m_diagramId, geometries,
      plural ? QStringLiteral("Fit diagram elements to content")
             : QStringLiteral("Fit presentation to content"));
}

void DiagramCanvas::wrapSelectionInPackage() {
  if (!canWrapSelectionInPackage())
    return;
  m_project->wrapPresentationInPackage(m_diagramId,
                                       m_selectedNodeOrder.first());
}

void DiagramCanvas::assignStyleToSelection(const QString &styleId) {
  if (!m_project)
    return;
  QStringList presentationIds = m_selectedNodeOrder;
  presentationIds.append(m_selectedContainers.values());
  m_project->assignStyleToPresentations(m_diagramId, presentationIds, styleId);
}

void DiagramCanvas::arrangeSelection(const QString &operation) {
  if (!m_project || m_selectedNodes.size() < 2)
    return;
  const auto parsedOperation = ui::arrangementOperationFromKey(operation);
  const auto *d = diagram();
  if (!parsedOperation || !d)
    return;

  QList<ui::DiagramNodeGeometry> selected;
  selected.reserve(m_selectedNodeOrder.size());
  for (const QString &nodeId : m_selectedNodeOrder) {
    if (const auto *node = findNode(*d, nodeId))
      selected.append({nodeId, nodeGeometry(*node)});
  }
  const auto arranged = ui::arrangeDiagramNodes(selected, *parsedOperation,
                                                m_defaultDistributionGap);
  QVariantList geometries;
  geometries.reserve(arranged.size());
  for (const auto &node : arranged) {
    QVariantMap geometry;
    geometry.insert(QStringLiteral("id"), node.id);
    geometry.insert(QStringLiteral("x"), node.geometry.x());
    geometry.insert(QStringLiteral("y"), node.geometry.y());
    geometry.insert(QStringLiteral("width"), node.geometry.width());
    geometry.insert(QStringLiteral("height"), node.geometry.height());
    geometries.append(geometry);
  }
  m_project->updatePresentationGeometries(
      m_diagramId, geometries, ui::arrangementDescription(*parsedOperation));
}

void DiagramCanvas::nudgeSelection(qreal deltaX, qreal deltaY) {
  if (!m_project ||
      (m_selectedNodes.isEmpty() && m_selectedContainers.isEmpty()) ||
      (qFuzzyIsNull(deltaX) && qFuzzyIsNull(deltaY)))
    return;

  captureSelectedGeometry();
  QVariantList geometries;
  geometries.reserve(m_originalGeometry.size());
  for (auto item = m_originalGeometry.cbegin();
       item != m_originalGeometry.cend(); ++item) {
    const QRectF geometry = item.value().translated(deltaX, deltaY);
    QVariantMap value;
    value.insert(QStringLiteral("id"), item.key());
    value.insert(QStringLiteral("x"), geometry.x());
    value.insert(QStringLiteral("y"), geometry.y());
    value.insert(QStringLiteral("width"), geometry.width());
    value.insert(QStringLiteral("height"), geometry.height());
    geometries.append(value);
  }
  m_originalGeometry.clear();
  const qsizetype rootSelectionSize =
      m_selectedNodes.size() + m_selectedContainers.size();
  m_project->updatePresentationGeometries(
      m_diagramId, geometries,
      rootSelectionSize == 1 ? QStringLiteral("Nudge diagram element")
                             : QStringLiteral("Nudge diagram elements"));
}

void DiagramCanvas::removeSelectedPresentations() {
  if (!m_project ||
      (m_selectedNodes.isEmpty() && m_selectedContainers.isEmpty()))
    return;
  m_project->removeDiagramPresentations(m_diagramId, m_selectedNodes.values(),
                                        m_selectedContainers.values());
  clearCanvasSelection();
}

void DiagramCanvas::deleteSelectedConnector() {
  if (!m_project || m_selectedConnector.isEmpty())
    return;
  const auto *d = diagram();
  const auto *connector = d ? findConnector(*d, m_selectedConnector) : nullptr;
  if (!connector)
    return;
  const QString relationshipId = connector->relationshipId;
  m_project->deleteRelationship(relationshipId);
  clearCanvasSelection();
}

void DiagramCanvas::clearContainerSelection() {
  m_selectedContainers.clear();
  m_selectedContainer.clear();
}

void DiagramCanvas::selectOnlyContainer(const QString &containerId) {
  m_selectedContainers = {containerId};
  m_selectedContainer = containerId;
}

void DiagramCanvas::clearCanvasSelection() {
  const bool hadSelection =
      !m_selectedNodes.isEmpty() || !m_selectedContainers.isEmpty() ||
      !m_selectedConnector.isEmpty() ||
      !m_connectorGestureSourceNode.isEmpty() || m_endpointDragActive;
  resetLassoState();
  if (!hadSelection)
    return;
  clearRelationshipToolboxCandidate();
  clearArrangementToolboxCandidate(true);
  clearConnectorToolboxCandidate(true);
  clearPresentationToolboxCandidate(true);
  m_selectedNodes.clear();
  m_selectedNodeOrder.clear();
  clearContainerSelection();
  m_selectedConnector.clear();
  m_selectedBendPoint = -1;
  m_endpointDragTargetNode.clear();
  m_endpointDragActive = false;
  m_endpointDragSnapped = false;
  m_bendPointPreview.clear();
  m_bendPointPreviewActive = false;
  m_contextAnnotationKey.clear();
  m_annotationPreviewActive = false;
  m_annotationDragConnector.clear();
  m_annotationDragKey.clear();
  m_annotationDragMoved = false;
  m_connectorGestureSourceNode.clear();
  m_connectorGestureTargetNode.clear();
  m_connectorGestureType.clear();
  m_connectorGestureSourceSnapped = false;
  m_connectorGestureTargetSnapped = false;
  if (m_interaction == Interaction::CreateConnector ||
      m_interaction == Interaction::MoveSourcePort ||
      m_interaction == Interaction::MoveTargetPort ||
      m_interaction == Interaction::MoveAnnotation)
    m_interaction = Interaction::None;
  m_sceneDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::selectNode(const QString &nodeId, bool toggle) {
  clearRelationshipToolboxCandidate();
  clearArrangementToolboxCandidate(true);
  clearConnectorToolboxCandidate(true);
  clearPresentationToolboxCandidate(true);
  const bool preserveExistingSelection =
      !toggle && m_selectedNodes.contains(nodeId);
  if (!toggle && !preserveExistingSelection)
    clearContainerSelection();
  else
    m_selectedContainer.clear();
  if (toggle) {
    if (m_selectedNodes.contains(nodeId)) {
      m_selectedNodes.remove(nodeId);
      m_selectedNodeOrder.removeAll(nodeId);
    } else {
      m_selectedNodes.insert(nodeId);
      m_selectedNodeOrder.append(nodeId);
    }
  } else if (!m_selectedNodes.contains(nodeId)) {
    m_selectedNodes = {nodeId};
    m_selectedNodeOrder = {nodeId};
  } else {
    // A plain click inside an existing multi-selection promotes that node to
    // the reference position without discarding the rest of the selection.
    // Alignment and size matching use the last selected node as their key.
    m_selectedNodeOrder.removeAll(nodeId);
    m_selectedNodeOrder.append(nodeId);
  }
  if (!toggle) {
    m_selectedConnector.clear();
    m_selectedBendPoint = -1;
    m_endpointDragTargetNode.clear();
    m_endpointDragActive = false;
    m_contextAnnotationKey.clear();
  }
  m_sceneDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::selectConnector(const QString &connectorId,
                                    bool preserveNodes) {
  clearRelationshipToolboxCandidate();
  clearArrangementToolboxCandidate(true);
  clearConnectorToolboxCandidate(true);
  clearPresentationToolboxCandidate(true);
  clearContainerSelection();
  m_selectedConnector = connectorId;
  m_selectedBendPoint = -1;
  m_endpointDragTargetNode.clear();
  m_endpointDragActive = false;
  m_bendPointPreview.clear();
  m_bendPointPreviewActive = false;
  m_contextAnnotationKey.clear();
  if (!preserveNodes) {
    m_selectedNodes.clear();
    m_selectedNodeOrder.clear();
  }
  m_sceneDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape &&
      m_interaction == Interaction::MoveAnnotation) {
    cancelAnnotationPreview();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Escape &&
      m_interaction == Interaction::CreateConnector) {
    cancelConnectorGesture();
    event->accept();
    return;
  }
  if (event->matches(QKeySequence::SelectAll)) {
    selectAllInContext();
    event->accept();
    return;
  }
  const QString gestureType = relationshipTypeForGestureKey(*event);
  if (!gestureType.isEmpty() && startConnectorGesture(gestureType)) {
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Escape &&
      m_interaction == Interaction::MoveBendPoint) {
    m_interaction = Interaction::None;
    m_bendPointPreview.clear();
    m_bendPointPreviewActive = false;
    m_sceneDirty = true;
    m_textDirty = true;
    update();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Escape && m_interaction == Interaction::Lasso) {
    cancelLassoSelection();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Escape &&
      (m_interaction == Interaction::MoveSourcePort ||
       m_interaction == Interaction::MoveTargetPort)) {
    cancelEndpointDrag();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Delete &&
      (!m_selectedNodes.isEmpty() || !m_selectedContainers.isEmpty())) {
    removeSelectedPresentations();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Delete && bendPointSelected()) {
    removeSelectedBendPoint();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Delete && !m_selectedConnector.isEmpty() &&
      m_project) {
    deleteSelectedConnector();
    event->accept();
    return;
  }
  const bool arrowKey =
      event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
      event->key() == Qt::Key_Up || event->key() == Qt::Key_Down;
  Qt::KeyboardModifiers modifiers = event->modifiers();
  modifiers.setFlag(Qt::KeypadModifier, false);
  if (arrowKey &&
      (!m_selectedNodes.isEmpty() || !m_selectedContainers.isEmpty()) &&
      (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)) {
    const qreal distance = modifiers == Qt::ShiftModifier ? 10.0 : 1.0;
    QPointF delta;
    if (event->key() == Qt::Key_Left)
      delta.rx() = -distance;
    else if (event->key() == Qt::Key_Right)
      delta.rx() = distance;
    else if (event->key() == Qt::Key_Up)
      delta.ry() = -distance;
    else
      delta.ry() = distance;
    nudgeSelection(delta.x(), delta.y());
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Escape) {
    clearRelationshipToolboxCandidate();
    clearArrangementToolboxCandidate();
    clearConnectorToolboxCandidate();
    clearPresentationToolboxCandidate();
    emit contextToolboxesDismissRequested();
    event->accept();
    return;
  }
  QQuickItem::keyPressEvent(event);
}

} // namespace uuml
