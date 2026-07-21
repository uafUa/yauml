#include "ui/diagram_canvas.h"

#include "core/project_controller.h"
#include "ui/text_occlusion.h"
#include "ui/triangle_batch.h"

#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
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

namespace uuml {
namespace {

constexpr qreal kHeaderHeight = 30.0;
constexpr qreal kLineHeight = 21.0;
constexpr qreal kPadding = 8.0;
constexpr int kTextAtlasSize = 2048;
constexpr int kTextAtlasPadding = 2;

struct RenderNode {
  QRectF rect;
  ElementType type = ElementType::Class;
  QString name;
  QStringList attributes;
  QStringList operations;
  QStringList enumLiterals;
  bool selected = false;
};

struct RenderConnector {
  QPointF start;
  QPointF end;
  QString name;
  RelationshipType type = RelationshipType::Dependency;
  bool selected = false;
};

struct SceneSnapshot {
  QVector<RenderNode> nodes;
  QVector<RenderConnector> connectors;
};

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

QColor elementColor(ElementType type) {
  switch (type) {
  case ElementType::Package:
    return QColor(QStringLiteral("#fff1c2"));
  case ElementType::Class:
    return QColor(QStringLiteral("#f8fbff"));
  case ElementType::Struct:
    return QColor(QStringLiteral("#eefaf1"));
  case ElementType::Enumeration:
    return QColor(QStringLiteral("#f7efff"));
  }
  return Qt::white;
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

void appendBorder(QVector<QSGGeometry::ColoredPoint2D> &vertices,
                  const QRectF &rect, qreal width, const QColor &color) {
  appendRect(vertices, {rect.left(), rect.top(), rect.width(), width}, color);
  appendRect(vertices,
             {rect.left(), rect.bottom() - width, rect.width(), width}, color);
  appendRect(vertices, {rect.left(), rect.top(), width, rect.height()}, color);
  appendRect(vertices, {rect.right() - width, rect.top(), width, rect.height()},
             color);
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

qreal adaptiveGridStep(qreal zoom) {
  constexpr qreal kBaseStep = 20.0;
  constexpr qreal kMinimumViewSpacing = 16.0;
  qreal step = kBaseStep;
  while (step * zoom < kMinimumViewSpacing)
    step *= 2.0;
  return step;
}

qreal snapToPhysicalPixelCenter(qreal position, qreal devicePixelRatio) {
  return (std::floor(position * devicePixelRatio) + 0.5) / devicePixelRatio;
}

QSGGeometryNode *buildGridGeometry(const QSizeF &viewportSize,
                                   const QPointF &pan, qreal zoom,
                                   qreal devicePixelRatio) {
  QVector<QSGGeometry::ColoredPoint2D> vertices;
  if (viewportSize.isEmpty() || zoom <= 0.0 || devicePixelRatio <= 0.0)
    return createColoredNode(vertices);

  const qreal sceneStep = adaptiveGridStep(zoom);
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
  const QColor color(QStringLiteral("#dce2e8"));
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

  void updateBackground(const QSizeF &size) {
    if (size == m_backgroundSize)
      return;
    m_backgroundSize = size;
    if (m_background) {
      removeChildNode(m_background);
      delete m_background;
    }
    QVector<QSGGeometry::ColoredPoint2D> vertices;
    appendRect(vertices, QRectF(QPointF(), size),
               QColor(QStringLiteral("#f4f6f8")));
    m_background = createColoredNode(vertices);
    insertChildNodeBefore(m_background, m_grid ? static_cast<QSGNode *>(m_grid)
                                               : m_transform);
  }

  void updateGrid(const QSizeF &viewportSize, const QPointF &pan, qreal zoom,
                  qreal devicePixelRatio) {
    if (viewportSize == m_gridViewportSize && pan == m_gridPan &&
        qFuzzyCompare(zoom, m_gridZoom) &&
        qFuzzyCompare(devicePixelRatio, m_gridDevicePixelRatio))
      return;
    m_gridViewportSize = viewportSize;
    m_gridPan = pan;
    m_gridZoom = zoom;
    m_gridDevicePixelRatio = devicePixelRatio;
    if (m_grid) {
      removeChildNode(m_grid);
      delete m_grid;
    }
    m_grid = buildGridGeometry(viewportSize, pan, zoom, devicePixelRatio);
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
  qreal renderedTextScale = 0.0;
  QRectF textCoverage;

private:
  QSizeF m_backgroundSize;
  QSGGeometryNode *m_background = nullptr;
  QSizeF m_gridViewportSize;
  QPointF m_gridPan;
  qreal m_gridZoom = 0.0;
  qreal m_gridDevicePixelRatio = 0.0;
  QSGGeometryNode *m_grid = nullptr;
  QSGTransformNode *m_transform = nullptr;
  QSGNode *m_geometry = nullptr;
  TextAtlasGroup *m_text = nullptr;
};

QSGNode *buildSceneGeometry(const SceneSnapshot &snapshot, qreal zoom,
                            int detail) {
  QVector<QSGGeometry::ColoredPoint2D> vertices;
  vertices.reserve(snapshot.nodes.size() * 48 +
                   snapshot.connectors.size() * 64);

  for (const auto &connector : snapshot.connectors) {
    const QColor color(connector.selected ? QStringLiteral("#1769d2")
                                          : QStringLiteral("#52606d"));
    const qreal lineWidth = (connector.selected ? 3.0 : 1.5) / zoom;
    if (connector.type == RelationshipType::Dependency)
      appendDashedLine(vertices, connector.start, connector.end, lineWidth,
                       1.0 / zoom, color, zoom);
    else
      appendAntialiasedLine(vertices, connector.start, connector.end, lineWidth,
                            1.0 / zoom, color);
    if (detail > 0 && connector.type != RelationshipType::Association) {
      const QPointF direction = connector.start - connector.end;
      const qreal length = std::hypot(direction.x(), direction.y());
      if (length > 1) {
        const QPointF unit = direction / length;
        const QPointF normal(-unit.y(), unit.x());
        const QPointF first =
            connector.end + unit * (12.0 / zoom) + normal * (5.0 / zoom);
        const QPointF second =
            connector.end + unit * (12.0 / zoom) - normal * (5.0 / zoom);
        if (connector.type == RelationshipType::Generalization) {
          appendAntialiasedTriangle(vertices, connector.end, first, second,
                                    1.0 / zoom, Qt::white);
          appendAntialiasedLine(vertices, connector.end, first, 1.5 / zoom,
                                1.0 / zoom, color);
          appendAntialiasedLine(vertices, connector.end, second, 1.5 / zoom,
                                1.0 / zoom, color);
          appendAntialiasedLine(vertices, first, second, 1.5 / zoom, 1.0 / zoom,
                                color);
        } else {
          // Dependencies use the UML open arrowhead.
          appendAntialiasedLine(vertices, connector.end, first, 1.5 / zoom,
                                1.0 / zoom, color);
          appendAntialiasedLine(vertices, connector.end, second, 1.5 / zoom,
                                1.0 / zoom, color);
        }
      }
    }
    if (connector.selected) {
      constexpr qreal kOuterHandleSize = 11.0;
      constexpr qreal kInnerHandleSize = 7.0;
      const qreal outerSize = kOuterHandleSize / zoom;
      const qreal innerSize = kInnerHandleSize / zoom;
      const auto appendHandle = [&](const QPointF &center) {
        appendRect(vertices,
                   {center.x() - outerSize / 2.0, center.y() - outerSize / 2.0,
                    outerSize, outerSize},
                   QColor(QStringLiteral("#1769d2")));
        appendRect(vertices,
                   {center.x() - innerSize / 2.0, center.y() - innerSize / 2.0,
                    innerSize, innerSize},
                   Qt::white);
      };
      appendHandle(connector.start);
      appendHandle(connector.end);
    }
  }

  for (const auto &node : snapshot.nodes) {
    const QColor border(node.selected ? QStringLiteral("#1769d2")
                                      : QStringLiteral("#3f4b56"));
    appendRect(vertices, node.rect, elementColor(node.type));
    appendRect(
        vertices,
        {node.rect.left(), node.rect.top(), node.rect.width(), kHeaderHeight},
        elementColor(node.type).darker(104));
    appendBorder(vertices, node.rect, (node.selected ? 3.0 : 1.2) / zoom,
                 border);
    if (detail > 0) {
      appendLine(vertices,
                 QPointF(node.rect.left(), node.rect.top() + kHeaderHeight),
                 QPointF(node.rect.right(), node.rect.top() + kHeaderHeight),
                 1.0 / zoom, QColor(QStringLiteral("#65727e")));
    }
    if (detail == 2 && node.type != ElementType::Enumeration &&
        !node.attributes.isEmpty() && !node.operations.isEmpty()) {
      const qreal y = node.rect.top() + kHeaderHeight +
                      node.attributes.size() * kLineHeight;
      if (y <= node.rect.bottom())
        appendLine(vertices, QPointF(node.rect.left(), y),
                   QPointF(node.rect.right(), y), 1.0 / zoom,
                   QColor(QStringLiteral("#c5ccd3")));
    }
    if (node.selected) {
      const qreal handle = 9.0 / zoom;
      appendRect(vertices,
                 {node.rect.right() - handle, node.rect.bottom() - handle,
                  handle, handle},
                 QColor(QStringLiteral("#1769d2")));
    }
  }
  return createColoredNodeBatches(vertices);
}

QVector<RenderText> buildTextEntries(const SceneSnapshot &snapshot, int detail,
                                     const QRectF &coverage) {
  QVector<RenderText> entries;
  if (detail == 0)
    return entries;

  const QFont base = QGuiApplication::font();
  QFont header = base;
  header.setBold(true);
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
  for (const auto &node : snapshot.nodes)
    allNodeRects.append(node.rect);

  for (const auto &connector : snapshot.connectors) {
    if (detail != 2 || connector.name.isEmpty())
      continue;
    const QPointF middle = (connector.start + connector.end) / 2.0;
    const QRectF target(middle.x() - 70, middle.y() - 12, 140, 24);
    if (!coverage.isValid() || coverage.intersects(target))
      appendVisibleText(target, target, connector.name, base,
                        QColor(QStringLiteral("#263238")), Qt::AlignCenter,
                        allNodeRects);
  }

  for (qsizetype nodeIndex = 0; nodeIndex < snapshot.nodes.size();
       ++nodeIndex) {
    const auto &node = snapshot.nodes.at(nodeIndex);
    if (coverage.isValid() && !coverage.intersects(node.rect))
      continue;
    QList<QRectF> laterNodeRects;
    for (qsizetype later = nodeIndex + 1; later < snapshot.nodes.size();
         ++later) {
      if (snapshot.nodes.at(later).rect.intersects(node.rect))
        laterNodeRects.append(snapshot.nodes.at(later).rect);
    }
    const QRectF nodeTextClip = node.rect.adjusted(kPadding, 0, -kPadding, 0);
    const QRectF headerTarget(node.rect.left() + kPadding, node.rect.top(),
                              node.rect.width() - 2 * kPadding, kHeaderHeight);
    appendVisibleText(headerTarget, nodeTextClip, node.name, header,
                      QColor(QStringLiteral("#18212a")), Qt::AlignCenter,
                      laterNodeRects);
    if (detail != 2)
      continue;
    int line = 0;
    const auto addLines = [&](const QStringList &values, int &lineNumber) {
      for (const auto &text : values) {
        const QRectF target(node.rect.left() + kPadding,
                            node.rect.top() + kHeaderHeight +
                                lineNumber * kLineHeight,
                            node.rect.width() - 2 * kPadding, kLineHeight);
        appendVisibleText(target, nodeTextClip, text, base,
                          QColor(QStringLiteral("#263238")),
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

DiagramCanvas::DiagramCanvas(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(QQuickItem::ItemHasContents, true);
  setClip(true);
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptHoverEvents(true);
}

ProjectController *DiagramCanvas::project() const { return m_project; }

void DiagramCanvas::setProject(ProjectController *project) {
  if (m_project == project)
    return;
  if (m_project)
    disconnect(m_project, nullptr, this, nullptr);
  m_project = project;
  if (m_project) {
    connect(m_project, &ProjectController::stateChanged, this, [this] {
      m_sceneDirty = true;
      update();
    });
  }
  m_sceneDirty = true;
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
  emit diagramIdChanged();
  update();
}

qreal DiagramCanvas::zoom() const { return m_zoom; }
int DiagramCanvas::selectedNodeCount() const { return m_selectedNodes.size(); }
bool DiagramCanvas::connectorSelected() const {
  return !m_selectedConnector.isEmpty();
}

QString DiagramCanvas::reconnectPrompt() const {
  if (m_reconnectEndpoint == ReconnectEndpoint::Source)
    return QStringLiteral("Click the new source node");
  if (m_reconnectEndpoint == ReconnectEndpoint::Target)
    return QStringLiteral("Click the new target node");
  return {};
}

const Diagram *DiagramCanvas::diagram() const {
  return m_project ? findDiagram(m_project->data(), m_diagramId) : nullptr;
}

QRectF DiagramCanvas::nodeGeometry(const NodePresentation &node) const {
  return m_previewGeometry.value(node.id, node.geometry);
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

QRectF DiagramCanvas::textLineRect(const QRectF &nodeRect, int line) const {
  return {nodeRect.left() + kPadding,
          nodeRect.top() + kHeaderHeight + line * kLineHeight,
          nodeRect.width() - 2 * kPadding, kLineHeight};
}

DiagramCanvas::ConnectorEndpoints DiagramCanvas::connectorEndpoints(
    const ConnectorPresentation &connector) const {
  const auto *d = diagram();
  if (!d || !m_project)
    return {};
  const auto *relationship =
      findRelationship(m_project->data(), connector.relationshipId);
  if (!relationship)
    return {};

  QRectF sourceRect;
  QRectF targetRect;
  for (const auto &node : d->nodes) {
    if (node.elementId == relationship->sourceId)
      sourceRect = nodeGeometry(node);
    if (node.elementId == relationship->targetId)
      targetRect = nodeGeometry(node);
  }
  if (!sourceRect.isValid() || !targetRect.isValid())
    return {};

  ConnectorAnchor sourceAnchor = connector.sourceAnchor;
  ConnectorAnchor targetAnchor = connector.targetAnchor;
  if (connector.id == m_selectedConnector && m_portPreviewActive) {
    if (m_interaction == Interaction::MoveSourcePort)
      sourceAnchor = m_portPreview;
    else if (m_interaction == Interaction::MoveTargetPort)
      targetAnchor = m_portPreview;
  }
  return {connectorAnchorPoint(sourceRect, sourceAnchor, targetRect.center()),
          connectorAnchorPoint(targetRect, targetAnchor, sourceRect.center()),
          true};
}

QRectF DiagramCanvas::endpointNodeRect(const ConnectorPresentation &connector,
                                       bool source) const {
  const auto *d = diagram();
  if (!d || !m_project)
    return {};
  const auto *relationship =
      findRelationship(m_project->data(), connector.relationshipId);
  if (!relationship)
    return {};
  const QString &elementId =
      source ? relationship->sourceId : relationship->targetId;
  for (const auto &node : d->nodes)
    if (node.elementId == elementId)
      return nodeGeometry(node);
  return {};
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

void DiagramCanvas::updatePortPreview(const QPointF &scenePoint) {
  const auto *d = diagram();
  if (!d || m_selectedConnector.isEmpty())
    return;
  const auto *connector = findConnector(*d, m_selectedConnector);
  if (!connector)
    return;
  const bool source = m_interaction == Interaction::MoveSourcePort;
  const QRectF rect = endpointNodeRect(*connector, source);
  if (!rect.isValid())
    return;
  m_portPreview = anchorAtPerimeterPoint(rect, scenePoint);
  m_portPreviewActive = true;
  m_sceneDirty = true;
  update();
}

void DiagramCanvas::commitPortPreview() {
  if (!m_project || !m_portPreviewActive || m_selectedConnector.isEmpty())
    return;
  const bool source = m_interaction == Interaction::MoveSourcePort;
  m_project->updateConnectorAnchor(m_diagramId, m_selectedConnector, source,
                                   toString(m_portPreview.side),
                                   m_portPreview.offset);
}

QSGNode *
DiagramCanvas::updatePaintNode(QSGNode *oldNode,
                               UpdatePaintNodeData *updatePaintNodeData) {
  Q_UNUSED(updatePaintNodeData)
  auto *root = static_cast<DiagramSceneRoot *>(oldNode);
  if (!root)
    root = new DiagramSceneRoot;
  root->updateBackground({width(), height()});
  auto *quickWindow = window();
  const qreal devicePixelRatio =
      quickWindow ? quickWindow->devicePixelRatio() : 1.0;
  root->updateGrid({width(), height()}, m_pan, m_zoom, devicePixelRatio);
  root->updateTransform(m_pan, m_zoom);

  const int detail = detailLevel(m_zoom);
  const qreal rasterScale = textRasterScale(m_zoom, devicePixelRatio);
  const QRectF visibleScene =
      QRectF(toScene({0, 0}), toScene({width(), height()})).normalized();
  const bool geometryDirty = m_sceneDirty || root->renderedDetail != detail;
  const bool scaleChanged =
      !qFuzzyCompare(rasterScale, root->renderedTextScale);
  const bool needsTextCoverage = detail > 0 && rasterScale > 1.0;
  const bool coverageExpired =
      needsTextCoverage && (!root->textCoverage.isValid() ||
                            !root->textCoverage.contains(visibleScene));
  const bool textDirty = geometryDirty || scaleChanged || coverageExpired;
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

      QHash<QString, QRectF> nodeRects;
      nodeRects.reserve(d->nodes.size());
      for (const auto &node : d->nodes) {
        const QRectF rect = nodeGeometry(node);
        nodeRects.insert(node.elementId, rect);
        const auto element = elements.constFind(node.elementId);
        if (element == elements.cend())
          continue;
        snapshot.nodes.append({rect, (*element)->type, (*element)->name,
                               (*element)->attributes, (*element)->operations,
                               (*element)->enumLiterals,
                               m_selectedNodes.contains(node.id)});
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
        ConnectorAnchor sourceAnchor = connector.sourceAnchor;
        ConnectorAnchor targetAnchor = connector.targetAnchor;
        if (connector.id == m_selectedConnector && m_portPreviewActive) {
          if (m_interaction == Interaction::MoveSourcePort)
            sourceAnchor = m_portPreview;
          else if (m_interaction == Interaction::MoveTargetPort)
            targetAnchor = m_portPreview;
        }
        snapshot.connectors.append(
            {connectorAnchorPoint(*start, sourceAnchor, end->center()),
             connectorAnchorPoint(*end, targetAnchor, start->center()),
             (*relationship)->name, (*relationship)->type,
             connector.id == m_selectedConnector});
      }
    }
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
    if (textDirty && quickWindow)
      root->updateText(buildTextEntries(snapshot, detail, textCoverage),
                       quickWindow, rasterScale);
    root->renderedDetail = detail;
    root->renderedTextScale = rasterScale;
    root->textCoverage = textCoverage;
    m_sceneDirty = false;
  }
  return root;
}

void DiagramCanvas::geometryChange(const QRectF &newGeometry,
                                   const QRectF &oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  m_sceneDirty = true;
  update();
}

const NodePresentation *
DiagramCanvas::hitNode(const QPointF &scenePoint) const {
  const auto *d = diagram();
  if (!d)
    return nullptr;
  for (auto it = d->nodes.crbegin(); it != d->nodes.crend(); ++it)
    if (nodeGeometry(*it).contains(scenePoint))
      return &*it;
  return nullptr;
}

const ConnectorPresentation *
DiagramCanvas::hitConnector(const QPointF &scenePoint) const {
  const auto *d = diagram();
  if (!d)
    return nullptr;
  for (auto it = d->connectors.crbegin(); it != d->connectors.crend(); ++it) {
    const ConnectorEndpoints endpoints = connectorEndpoints(*it);
    if (endpoints.valid && distanceToSegment(scenePoint, endpoints.source,
                                             endpoints.target) <= 7.0 / m_zoom)
      return &*it;
  }
  return nullptr;
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
    if (QRectF(rect.left(), rect.top(), rect.width(), kHeaderHeight)
            .contains(scenePoint))
      return {element->id,
              QStringLiteral("name"),
              -1,
              element->name,
              QRectF(rect.left() + 4, rect.top() + 3, rect.width() - 8,
                     kHeaderHeight - 6),
              true};
    int line = 0;
    if (element->type == ElementType::Enumeration) {
      for (int i = 0; i < element->enumLiterals.size(); ++i, ++line)
        if (textLineRect(rect, line).contains(scenePoint))
          return {element->id, QStringLiteral("literal"), i,
                  element->enumLiterals.at(i), textLineRect(rect, line)};
    } else {
      for (int i = 0; i < element->attributes.size(); ++i, ++line)
        if (textLineRect(rect, line).contains(scenePoint))
          return {element->id, QStringLiteral("attribute"), i,
                  element->attributes.at(i), textLineRect(rect, line)};
      for (int i = 0; i < element->operations.size(); ++i, ++line)
        if (textLineRect(rect, line).contains(scenePoint))
          return {element->id, QStringLiteral("operation"), i,
                  element->operations.at(i), textLineRect(rect, line)};
    }
  }
  if (const auto *connector = hitConnector(scenePoint)) {
    if (const auto *relationship =
            findRelationship(m_project->data(), connector->relationshipId)) {
      const ConnectorEndpoints endpoints = connectorEndpoints(*connector);
      if (!endpoints.valid)
        return {};
      const QPointF middle = (endpoints.source + endpoints.target) / 2.0;
      const QRectF label(middle.x() - 70, middle.y() - 12, 140, 24);
      if (label.contains(scenePoint))
        return {relationship->id, QStringLiteral("name"), -1,
                relationship->name, label};
    }
  }
  return {};
}

void DiagramCanvas::mousePressEvent(QMouseEvent *event) {
  forceActiveFocus();
  m_pressView = event->position();
  m_pressScene = toScene(m_pressView);
  if (event->button() == Qt::MiddleButton) {
    m_interaction = Interaction::Pan;
    m_originalPan = m_pan;
    event->accept();
    return;
  }

  bool sourcePort = false;
  if (event->button() == Qt::LeftButton &&
      hitSelectedPort(m_pressScene, sourcePort)) {
    const auto *selected = findConnector(*diagram(), m_selectedConnector);
    m_interaction =
        sourcePort ? Interaction::MoveSourcePort : Interaction::MoveTargetPort;
    m_portPreview =
        sourcePort ? selected->sourceAnchor : selected->targetAnchor;
    m_portPreviewActive = true;
    event->accept();
    return;
  }

  const auto *node = hitNode(m_pressScene);
  const bool toggle = event->modifiers().testFlag(Qt::ControlModifier);
  if (event->button() == Qt::LeftButton && node &&
      m_reconnectEndpoint != ReconnectEndpoint::None && m_project &&
      !m_selectedConnector.isEmpty()) {
    m_project->reconnectRelationship(m_diagramId, m_selectedConnector, node->id,
                                     m_reconnectEndpoint ==
                                         ReconnectEndpoint::Source);
    m_reconnectEndpoint = ReconnectEndpoint::None;
    m_selectedNodes.clear();
    m_selectedNodeOrder.clear();
    emit canvasSelectionChanged();
    event->accept();
    return;
  }
  if (node) {
    selectNode(node->id, toggle);
    m_interactionNode = node->id;
    const QRectF rect = nodeGeometry(*node);
    const QRectF resizeHandle(rect.right() - 14 / m_zoom,
                              rect.bottom() - 14 / m_zoom, 14 / m_zoom,
                              14 / m_zoom);
    m_interaction = resizeHandle.contains(m_pressScene) ? Interaction::Resize
                                                        : Interaction::Move;
    m_originalGeometry.clear();
    for (const auto &candidate : diagram()->nodes)
      if (m_selectedNodes.contains(candidate.id))
        m_originalGeometry.insert(candidate.id, nodeGeometry(candidate));
    if (m_project)
      m_project->selectObject(node->elementId, QStringLiteral("element"));
  } else if (const auto *connector = hitConnector(m_pressScene)) {
    selectConnector(connector->id, toggle);
    if (m_project) {
      m_project->selectObject(connector->relationshipId,
                              QStringLiteral("relationship"));
    }
  } else {
    clearCanvasSelection();
    if (m_project)
      m_project->clearSelection();
  }
  event->accept();
}

void DiagramCanvas::mouseMoveEvent(QMouseEvent *event) {
  if (m_interaction == Interaction::Pan) {
    m_pan = m_originalPan + event->position() - m_pressView;
    emit viewportChanged();
    update();
    return;
  }
  if (m_interaction == Interaction::MoveSourcePort ||
      m_interaction == Interaction::MoveTargetPort) {
    updatePortPreview(toScene(event->position()));
    return;
  }
  const QPointF delta = toScene(event->position()) - m_pressScene;
  if (m_interaction == Interaction::Move) {
    m_previewGeometry.clear();
    for (auto it = m_originalGeometry.cbegin(); it != m_originalGeometry.cend();
         ++it)
      m_previewGeometry.insert(it.key(), it.value().translated(delta));
    m_sceneDirty = true;
    update();
  } else if (m_interaction == Interaction::Resize) {
    m_previewGeometry.clear();
    const QRectF original = m_originalGeometry.value(m_interactionNode);
    QRectF resized = original;
    resized.setWidth(qMax(120.0, original.width() + delta.x()));
    resized.setHeight(qMax(60.0, original.height() + delta.y()));
    m_previewGeometry.insert(m_interactionNode, resized);
    m_sceneDirty = true;
    update();
  }
}

void DiagramCanvas::mouseReleaseEvent(QMouseEvent *event) {
  Q_UNUSED(event)
  if (m_interaction == Interaction::Move ||
      m_interaction == Interaction::Resize)
    commitGeometryPreview();
  else if (m_interaction == Interaction::MoveSourcePort ||
           m_interaction == Interaction::MoveTargetPort)
    commitPortPreview();
  m_interaction = Interaction::None;
  m_originalGeometry.clear();
  m_previewGeometry.clear();
  m_portPreviewActive = false;
  m_sceneDirty = true;
  update();
}

void DiagramCanvas::mouseDoubleClickEvent(QMouseEvent *event) {
  const TextHit hit = hitText(toScene(event->position()));
  if (hit.objectId.isEmpty())
    return;
  const QRectF view = toView(hit.sceneRect);
  // The editor is a QML overlay in view coordinates, while canvas text is
  // defined in scene coordinates and enlarged by the canvas transform.
  const qreal fontPixelSize =
      qMax(1.0, QFontInfo(QGuiApplication::font()).pixelSize() * m_zoom);
  emit editRequested(hit.objectId, hit.field, hit.index, hit.text, view.x(),
                     view.y(), view.width(), view.height(), fontPixelSize,
                     hit.fontBold);
  event->accept();
}

void DiagramCanvas::wheelEvent(QWheelEvent *event) {
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
  m_project->updateNodeGeometries(m_diagramId, values);
}

void DiagramCanvas::fitToContent() {
  const auto *d = diagram();
  if (!d || d->nodes.isEmpty() || width() <= 0 || height() <= 0)
    return;
  QRectF content;
  for (const auto &node : d->nodes)
    content = content.isNull() ? node.geometry : content.united(node.geometry);
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

void DiagramCanvas::createRelationship(const QString &type) {
  if (!m_project || m_selectedNodes.size() != 2)
    return;
  const QStringList ids = m_selectedNodeOrder;
  m_selectedConnector =
      m_project->createRelationship(m_diagramId, ids.at(0), ids.at(1), type);
  m_reconnectEndpoint = ReconnectEndpoint::None;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::reconnectSource() {
  if (m_selectedConnector.isEmpty())
    return;
  m_reconnectEndpoint = ReconnectEndpoint::Source;
  m_selectedNodes.clear();
  m_selectedNodeOrder.clear();
  emit canvasSelectionChanged();
}

void DiagramCanvas::reconnectTarget() {
  if (m_selectedConnector.isEmpty())
    return;
  m_reconnectEndpoint = ReconnectEndpoint::Target;
  m_selectedNodes.clear();
  m_selectedNodeOrder.clear();
  emit canvasSelectionChanged();
}

void DiagramCanvas::cancelReconnect() {
  if (m_reconnectEndpoint == ReconnectEndpoint::None)
    return;
  m_reconnectEndpoint = ReconnectEndpoint::None;
  emit canvasSelectionChanged();
}

void DiagramCanvas::removeSelectedPresentations() {
  if (!m_project || m_selectedNodes.isEmpty())
    return;
  m_project->removePresentations(m_diagramId, m_selectedNodes.values());
  clearCanvasSelection();
}

void DiagramCanvas::clearCanvasSelection() {
  if (m_selectedNodes.isEmpty() && m_selectedConnector.isEmpty())
    return;
  m_selectedNodes.clear();
  m_selectedNodeOrder.clear();
  m_selectedConnector.clear();
  m_reconnectEndpoint = ReconnectEndpoint::None;
  m_portPreviewActive = false;
  m_sceneDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::selectNode(const QString &nodeId, bool toggle) {
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
  }
  if (!toggle) {
    m_selectedConnector.clear();
    m_reconnectEndpoint = ReconnectEndpoint::None;
  }
  m_sceneDirty = true;
  emit canvasSelectionChanged();
  update();
}

void DiagramCanvas::selectConnector(const QString &connectorId,
                                    bool preserveNodes) {
  m_selectedConnector = connectorId;
  m_reconnectEndpoint = ReconnectEndpoint::None;
  m_portPreviewActive = false;
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
      m_reconnectEndpoint != ReconnectEndpoint::None) {
    cancelReconnect();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Delete && !m_selectedNodes.isEmpty()) {
    removeSelectedPresentations();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Delete && !m_selectedConnector.isEmpty() &&
      m_project) {
    m_project->deleteSelected();
    clearCanvasSelection();
    event->accept();
    return;
  }
  QQuickItem::keyPressEvent(event);
}

} // namespace uuml
