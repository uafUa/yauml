#include "ui/diagram_document_exporter.h"

#include "core/diagram_filter.h"
#include "core/model_operation.h"
#include "core/presentation_layout.h"
#include "core/project_style.h"
#include "core/stereotype_catalog.h"
#include "ui/connector_routing.h"
#include "ui/diagram_clipping.h"
#include "ui/relationship_style.h"
#include "ui/sequence_diagram_layout.h"
#include "ui/ui_theme.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLineF>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextDocument>
#include <QUrl>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace yauml::ui {
namespace {

constexpr qreal kHeaderHeight = presentation_layout::kNodeHeaderHeight;
constexpr qreal kLineHeight = presentation_layout::kNodeLineHeight;
constexpr qreal kPadding = presentation_layout::kNodeTextPadding;
constexpr qreal kTypeIconSize = 16.0;
constexpr qreal kTypeIconSpacing = 4.0;
constexpr qreal kPackageHeaderHeight = 24.0;
constexpr qreal kNoteFoldSize = 18.0;

struct ElementVisualStyle {
  QColor fill;
  QColor headerFill;
  QColor border;
  QColor primaryText;
  QColor secondaryText;
  QColor divider;
};

struct ExportContainer {
  QString id;
  QRectF rect;
  QString name;
  QString stereotype;
  qreal titleWidth = 0.0;
  bool package = false;
  ElementVisualStyle style;
  int depth = 0;
  PresentationClip clip;
  ContainerOverflowEdges overflowEdges;
};

struct ExportNode {
  QString id;
  QString elementId;
  QRectF rect;
  ElementType type = ElementType::Class;
  QString name;
  QString stereotype;
  QStringList attributes;
  QStringList operations;
  QStringList enumLiterals;
  QString iconDataUri;
  ElementVisualStyle style;
  PresentationClip clip;
};

struct ExportAnnotation {
  QRectF rect;
  QString text;
};

struct ExportConnector {
  QString id;
  QString sourcePresentationId;
  QString targetPresentationId;
  QVector<QPointF> points;
  RelationshipType type = RelationshipType::Dependency;
  QVector<ExportAnnotation> annotations;
};

struct ExportNote {
  QString id;
  QRectF rect;
  QString markdown;
};

struct ExportNoteAttachment {
  QString id;
  QPointF source;
  QPointF target;
};

struct ExportScene {
  QString diagramName;
  DiagramType type = DiagramType::Class;
  QRectF contentBounds;
  QVector<ExportContainer> containers;
  QVector<ExportNode> nodes;
  QVector<ExportConnector> connectors;
  QVector<ExportNote> notes;
  QVector<ExportNoteAttachment> noteAttachments;
  SequenceDiagramLayout sequence;
};

struct PolylineSample {
  QPointF position;
  QPointF tangent;
};

QString number(qreal value) {
  QString result = QString::number(value, 'f', 3);
  while (result.contains(u'.') && result.endsWith(u'0'))
    result.chop(1);
  if (result.endsWith(u'.'))
    result.chop(1);
  return result == QStringLiteral("-0") ? QStringLiteral("0") : result;
}

QString colorName(const QColor &color) {
  return color.isValid() ? color.name(QColor::HexRgb)
                         : uiPalette().connector.name(QColor::HexRgb);
}

void includeRect(QRectF &bounds, bool &hasBounds, const QRectF &rect) {
  if (!rect.isValid() || rect.isEmpty())
    return;
  bounds = hasBounds ? bounds.united(rect) : rect;
  hasBounds = true;
}

void includePoint(QRectF &bounds, bool &hasBounds, const QPointF &point) {
  includeRect(bounds, hasBounds,
              QRectF(point - QPointF(0.5, 0.5), QSizeF(1.0, 1.0)));
}

QPointF edgePointToward(const QRectF &rect, const QPointF &target) {
  const QPointF center = rect.center();
  const QPointF direction = target - center;
  if (qFuzzyIsNull(direction.x()) && qFuzzyIsNull(direction.y()))
    return center;
  const qreal xScale = qFuzzyIsNull(direction.x())
                           ? std::numeric_limits<qreal>::max()
                           : rect.width() / (2.0 * std::abs(direction.x()));
  const qreal yScale = qFuzzyIsNull(direction.y())
                           ? std::numeric_limits<qreal>::max()
                           : rect.height() / (2.0 * std::abs(direction.y()));
  return center + direction * std::min(xScale, yScale);
}

QPointF anchorPoint(const QRectF &rect, const ConnectorAnchor &anchor,
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

const QColor &defaultElementFill(ElementType type, const UiPalette &palette) {
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

ElementVisualStyle
elementStyle(const DiagramStyle *style,
             std::optional<ElementType> type = std::nullopt) {
  const UiPalette palette = uiPalette();
  if (!style) {
    const QColor fill =
        type ? defaultElementFill(*type, palette) : palette.containerFill;
    return {fill,
            type ? fill.darker(104) : palette.containerHeaderFill,
            type ? palette.nodeBorder : palette.containerBorder,
            type ? palette.nodeTitleText : palette.containerTitleText,
            palette.bodyText,
            type ? palette.compartmentLine : palette.containerBorder};
  }

  QString fillName = style->fill;
  if (type) {
    switch (*type) {
    case ElementType::Class:
      fillName = style->classFill;
      break;
    case ElementType::Struct:
      fillName = style->structFill;
      break;
    case ElementType::Enumeration:
      fillName = style->enumerationFill;
      break;
    case ElementType::Package:
      break;
    }
  }
  return {QColor(fillName),
          QColor(style->headerFill),
          QColor(style->border),
          QColor(style->primaryText),
          QColor(style->secondaryText),
          QColor(style->divider)};
}

qreal nodeHeaderHeight(const ExportNode &node) {
  return kHeaderHeight + (node.stereotype.isEmpty() ? 0.0 : kLineHeight);
}

qreal polylineLength(const QVector<QPointF> &points) {
  qreal result = 0.0;
  for (qsizetype index = 1; index < points.size(); ++index)
    result += QLineF(points.at(index - 1), points.at(index)).length();
  return result;
}

std::optional<PolylineSample> samplePolyline(const QVector<QPointF> &points,
                                             qreal distanceFromStart) {
  const qreal length = polylineLength(points);
  if (points.size() < 2 || qFuzzyIsNull(length))
    return std::nullopt;
  const qreal requested = std::clamp(distanceFromStart, 0.0, length);
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

QRectF annotationRect(const QVector<QPointF> &points, const QString &text,
                      const QFont &font, qreal routePosition,
                      qreal tangentOffset, qreal normalOffset,
                      qreal height = 20.0) {
  if (text.isEmpty())
    return {};
  const qreal length = polylineLength(points);
  const auto sample = samplePolyline(points, routePosition * length);
  if (!sample)
    return {};
  const QPointF normal(-sample->tangent.y(), sample->tangent.x());
  const QPointF center = sample->position + sample->tangent * tangentOffset +
                         normal * normalOffset;
  const qreal width =
      std::max(24.0, QFontMetricsF(font).horizontalAdvance(text) + 10.0);
  return {center.x() - width / 2.0, center.y() - height / 2.0, width, height};
}

ConnectorAnnotationPlacement annotationPlacement(
    const QHash<QString, ConnectorAnnotationPlacement> &placements,
    const QString &key, qreal routePosition, qreal tangentOffset,
    qreal normalOffset) {
  const auto found = placements.constFind(key);
  if (found != placements.cend())
    return *found;
  return {routePosition, tangentOffset, normalOffset, {}};
}

QVector<ExportAnnotation> connectorAnnotations(
    const QVector<QPointF> &points, const Relationship &relationship,
    const ConnectorPresentation &presentation, const ProjectData &project) {
  QVector<ExportAnnotation> result;
  if (points.size() < 2)
    return result;
  const QFont font = QGuiApplication::font();
  const QString stereotype =
      stereotype_catalog::displayText(project, relationship.stereotypeIds);
  const qreal length = polylineLength(points);
  if (qFuzzyIsNull(length))
    return result;

  const auto append = [&](const QString &key, const QString &text,
                          qreal routePosition, qreal tangentOffset,
                          qreal normalOffset, qreal height = 20.0) {
    if (text.isEmpty())
      return;
    const ConnectorAnnotationPlacement placement =
        annotationPlacement(presentation.annotationPlacements, key,
                            routePosition, tangentOffset, normalOffset);
    const QRectF rect =
        annotationRect(points, text, font, placement.routePosition,
                       placement.tangentOffset, placement.normalOffset, height);
    if (!rect.isEmpty())
      result.append({rect, text});
  };

  append(QStringLiteral("name"), relationship.name, 0.5, 0.0,
         stereotype.isEmpty() ? 0.0 : 11.0, 24.0);
  append(QStringLiteral("stereotype"), stereotype, 0.5, 0.0,
         relationship.name.isEmpty() ? 0.0 : -13.0);
  const qreal endpointPosition = std::min(38.0, length * 0.25) / length;
  constexpr qreal kNormalOffset = 14.0;
  append(QStringLiteral("sourceRole"), relationship.sourceEnd.role,
         endpointPosition, 0.0, kNormalOffset);
  append(QStringLiteral("sourceMultiplicity"),
         relationship.sourceEnd.multiplicity, endpointPosition, 0.0,
         -kNormalOffset);
  append(QStringLiteral("targetRole"), relationship.targetEnd.role,
         1.0 - endpointPosition, 0.0, kNormalOffset);
  append(QStringLiteral("targetMultiplicity"),
         relationship.targetEnd.multiplicity, 1.0 - endpointPosition, 0.0,
         -kNormalOffset);
  return result;
}

QString iconDataUri(const QString &source) {
  if (source.trimmed().isEmpty())
    return {};
  const QUrl url(source);
  QString path;
  if (url.scheme() == QStringLiteral("qrc"))
    path = QStringLiteral(":") + url.path();
  else if (url.isLocalFile())
    path = url.toLocalFile();
  else if (source.startsWith(QStringLiteral(":/")))
    path = source;
  else
    return {};

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  const QByteArray data = file.readAll();
  const QString suffix = QFileInfo(path).suffix().toLower();
  const QString mime = suffix == QStringLiteral("svg")
                           ? QStringLiteral("image/svg+xml")
                       : suffix == QStringLiteral("png")
                           ? QStringLiteral("image/png")
                           : QStringLiteral("application/octet-stream");
  return QStringLiteral("data:%1;base64,%2")
      .arg(mime, QString::fromLatin1(data.toBase64()));
}

QString plainMarkdown(const QString &markdown) {
  QTextDocument document;
  document.setMarkdown(markdown);
  return document.toPlainText();
}

ExportScene buildScene(const ProjectData &project, const Diagram &diagram,
                       const DiagramDocumentExportOptions &options) {
  ExportScene scene;
  scene.diagramName = diagram.name;
  scene.type = diagram.type;
  if (diagram.type == DiagramType::Sequence) {
    scene.sequence = buildSequenceDiagramLayout(project, diagram);
    scene.contentBounds = scene.sequence.contentBounds;
    bool hasBounds =
        scene.contentBounds.isValid() && !scene.contentBounds.isEmpty();
    for (const SequenceActivationLayout &activation :
         scene.sequence.activations) {
      QString label = activation.label;
      if (!activation.executionContextLabel.isEmpty())
        label = label.isEmpty() ? activation.executionContextLabel
                                : QStringLiteral("%1 — %2").arg(
                                      label, activation.executionContextLabel);
      if (!label.isEmpty())
        includeRect(scene.contentBounds, hasBounds,
                    {activation.rect.right() + 4.0, activation.rect.top(),
                     220.0, kLineHeight});
    }
    return scene;
  }

  QHash<QString, const ModelElement *> elements;
  elements.reserve(project.elements.size());
  for (const ModelElement &element : project.elements)
    elements.insert(element.id, &element);

  QHash<QString, const Relationship *> relationships;
  relationships.reserve(project.relationships.size());
  for (const Relationship &relationship : project.relationships)
    relationships.insert(relationship.id, &relationship);

  QHash<QString, const ContainerPresentation *> containersById;
  QHash<QString, QString> ownerByChild;
  for (const ContainerPresentation &container : diagram.containers) {
    containersById.insert(container.id, &container);
    for (const QString &childId : container.childPresentationIds)
      ownerByChild.insert(childId, container.id);
  }

  QSet<QString> filteredPresentationIds;
  for (const NodePresentation &node : diagram.nodes) {
    const ModelElement *element = elements.value(node.elementId);
    if (!element ||
        !diagram_filter::matchesElement(project, *element, diagram.filter))
      filteredPresentationIds.insert(node.id);
  }
  const DiagramClipLayout clipLayout(diagram, {}, filteredPresentationIds);

  const auto depthOf = [&](const QString &id) {
    int depth = 0;
    QString current = id;
    QSet<QString> visited;
    while (ownerByChild.contains(current) && !visited.contains(current)) {
      visited.insert(current);
      current = ownerByChild.value(current);
      ++depth;
    }
    return depth;
  };
  for (const ContainerPresentation &container : diagram.containers) {
    const ModelElement *package =
        container.subjectKind == QStringLiteral("package")
            ? findElement(project, container.subjectId)
            : nullptr;
    scene.containers.append(
        {container.id, container.geometry,
         presentation_layout::containerDisplayName(project, container),
         package
             ? stereotype_catalog::displayText(project, package->stereotypeIds)
             : QString{},
         presentation_layout::containerTitleWidth(project, container),
         container.subjectKind == QStringLiteral("package"),
         elementStyle(
             project_style::effectiveStyleForContainer(project, container)),
         depthOf(container.id), clipLayout.clipFor(container.id),
         clipLayout.overflowEdges(container)});
  }
  std::stable_sort(
      scene.containers.begin(), scene.containers.end(),
      [](const ExportContainer &left, const ExportContainer &right) {
        if (left.depth != right.depth)
          return left.depth < right.depth;
        return left.rect.width() * left.rect.height() >
               right.rect.width() * right.rect.height();
      });

  const auto containingPackageId = [&](const QString &presentationId) {
    QString current = ownerByChild.value(presentationId);
    QSet<QString> visited;
    while (!current.isEmpty() && !visited.contains(current)) {
      visited.insert(current);
      const ContainerPresentation *owner = containersById.value(current);
      if (!owner)
        break;
      if (owner->subjectKind == QStringLiteral("package"))
        return owner->subjectId;
      current = ownerByChild.value(current);
    }
    return QString{};
  };

  QHash<QString, QRectF> nodeRectByElement;
  QHash<QString, QString> presentationByElement;
  for (const NodePresentation &node : diagram.nodes) {
    if (filteredPresentationIds.contains(node.id))
      continue;
    const ModelElement *element = elements.value(node.elementId);
    if (!element)
      continue;
    const PresentationClip clip = clipLayout.clipFor(node.id);
    if (clip.active && node.geometry.intersected(clip.rect).isEmpty())
      continue;
    const bool showAttributes =
        node.showAttributes.value_or(diagram.showAttributes);
    const bool showOperations =
        node.showOperations.value_or(diagram.showOperations);
    const OperationSignatureMode signatureMode =
        node.operationSignatureMode.value_or(diagram.operationSignatureMode);
    const QString iconSource =
        options.typeIconsVisible
            ? options.typeIconSources.value(toString(element->type)).toString()
            : QString{};
    scene.nodes.append(
        {node.id, node.elementId, node.geometry, element->type,
         presentation_layout::elementDisplayNameInPackage(
             project, *element, containingPackageId(node.id)),
         stereotype_catalog::displayText(project, element->stereotypeIds),
         showAttributes ? element->attributes : QStringList{},
         showOperations
             ? modelOperationSignatures(element->operations, signatureMode)
             : QStringList{},
         element->enumLiterals, iconDataUri(iconSource),
         elementStyle(project_style::effectiveStyleForNode(project, node),
                      element->type),
         clip});
    // This deliberately matches the production canvas: when the same semantic
    // element has several presentations, its connector uses the last visible
    // presentation in diagram order.
    nodeRectByElement.insert(node.elementId, node.geometry);
    presentationByElement.insert(node.elementId, node.id);
  }

  for (const ConnectorPresentation &connector : diagram.connectors) {
    const Relationship *relationship =
        relationships.value(connector.relationshipId);
    if (!relationship || !nodeRectByElement.contains(relationship->sourceId) ||
        !nodeRectByElement.contains(relationship->targetId))
      continue;
    const QRectF sourceRect = nodeRectByElement.value(relationship->sourceId);
    const QRectF targetRect = nodeRectByElement.value(relationship->targetId);
    QVector<QPointF> bends;
    bends.reserve(connector.bendPoints.size());
    for (const ConnectorBendPoint &bend : connector.bendPoints)
      bends.append(bend.position);
    const QPointF sourceTarget =
        bends.isEmpty() ? targetRect.center() : bends.constFirst();
    const QPointF targetTarget =
        bends.isEmpty() ? sourceRect.center() : bends.constLast();
    const QPointF source =
        anchorPoint(sourceRect, connector.sourceAnchor, sourceTarget);
    const QPointF target =
        anchorPoint(targetRect, connector.targetAnchor, targetTarget);
    const ConnectorRoute route = buildConnectorRoute(
        source, bends, target, connector.routing, connector.sourceAnchor.side,
        connector.targetAnchor.side);
    scene.connectors.append(
        {connector.id, presentationByElement.value(relationship->sourceId),
         presentationByElement.value(relationship->targetId), route.points,
         relationship->type,
         connectorAnnotations(route.points, *relationship, connector,
                              project)});
  }

  for (const NotePresentation &note : diagram.notes)
    scene.notes.append({note.id, note.geometry, note.text});

  const auto presentationRect =
      [&](const QString &id) -> std::optional<QRectF> {
    if (const NodePresentation *node = findNode(diagram, id)) {
      if (filteredPresentationIds.contains(id))
        return std::nullopt;
      return node->geometry;
    }
    if (const ContainerPresentation *container = findContainer(diagram, id))
      return container->geometry;
    return std::nullopt;
  };
  for (const NoteAttachment &attachment : diagram.noteAttachments) {
    const NotePresentation *note = findNote(diagram, attachment.noteId);
    const std::optional<QRectF> target =
        presentationRect(attachment.targetPresentationId);
    if (!note || !target)
      continue;
    scene.noteAttachments.append(
        {attachment.id,
         anchorPoint(note->geometry, attachment.noteAnchor, target->center()),
         anchorPoint(*target, attachment.targetAnchor,
                     note->geometry.center())});
  }

  QRectF bounds;
  bool hasBounds = false;
  for (const ExportContainer &container : std::as_const(scene.containers)) {
    includeRect(bounds, hasBounds,
                container.clip.active
                    ? container.rect.intersected(container.clip.rect)
                    : container.rect);
  }
  for (const ExportNode &node : std::as_const(scene.nodes)) {
    includeRect(bounds, hasBounds,
                node.clip.active ? node.rect.intersected(node.clip.rect)
                                 : node.rect);
  }
  for (const ExportConnector &connector : std::as_const(scene.connectors)) {
    for (const QPointF &point : connector.points)
      includePoint(bounds, hasBounds, point);
    for (const ExportAnnotation &annotation : connector.annotations)
      includeRect(bounds, hasBounds, annotation.rect);
  }
  for (const ExportNote &note : std::as_const(scene.notes))
    includeRect(bounds, hasBounds, note.rect);
  for (const ExportNoteAttachment &attachment :
       std::as_const(scene.noteAttachments)) {
    includePoint(bounds, hasBounds, attachment.source);
    includePoint(bounds, hasBounds, attachment.target);
  }
  scene.contentBounds = hasBounds ? bounds : QRectF{};
  return scene;
}

QFont exportBaseFont() { return QGuiApplication::font(); }

qreal fontPixelSize(const QFont &font) {
  if (font.pixelSize() > 0)
    return font.pixelSize();
  if (font.pointSizeF() > 0.0)
    return font.pointSizeF() * 96.0 / 72.0;
  return 12.0;
}

QString elided(const QString &text, const QFont &font, qreal width) {
  if (width <= 0.0)
    return {};
  return QFontMetricsF(font).elidedText(text, Qt::ElideRight,
                                        std::floor(width));
}

void writeSvgRect(QXmlStreamWriter &xml, const QRectF &rect, const QColor &fill,
                  const QColor &stroke = Qt::transparent,
                  qreal strokeWidth = 0.0) {
  xml.writeStartElement(QStringLiteral("rect"));
  xml.writeAttribute(QStringLiteral("x"), number(rect.x()));
  xml.writeAttribute(QStringLiteral("y"), number(rect.y()));
  xml.writeAttribute(QStringLiteral("width"), number(rect.width()));
  xml.writeAttribute(QStringLiteral("height"), number(rect.height()));
  if (fill.alpha() == 0) {
    xml.writeAttribute(QStringLiteral("fill"), QStringLiteral("none"));
  } else {
    xml.writeAttribute(QStringLiteral("fill"), colorName(fill));
    if (fill.alpha() < 255)
      xml.writeAttribute(QStringLiteral("fill-opacity"), number(fill.alphaF()));
  }
  if (strokeWidth > 0.0 && stroke.alpha() > 0) {
    xml.writeAttribute(QStringLiteral("stroke"), colorName(stroke));
    xml.writeAttribute(QStringLiteral("stroke-width"), number(strokeWidth));
    if (stroke.alpha() < 255)
      xml.writeAttribute(QStringLiteral("stroke-opacity"),
                         number(stroke.alphaF()));
  }
  xml.writeEndElement();
}

void writeSvgLine(QXmlStreamWriter &xml, const QPointF &start,
                  const QPointF &end, const QColor &color, qreal width,
                  bool dashed = false) {
  xml.writeStartElement(QStringLiteral("line"));
  xml.writeAttribute(QStringLiteral("x1"), number(start.x()));
  xml.writeAttribute(QStringLiteral("y1"), number(start.y()));
  xml.writeAttribute(QStringLiteral("x2"), number(end.x()));
  xml.writeAttribute(QStringLiteral("y2"), number(end.y()));
  xml.writeAttribute(QStringLiteral("fill"), QStringLiteral("none"));
  xml.writeAttribute(QStringLiteral("stroke"), colorName(color));
  xml.writeAttribute(QStringLiteral("stroke-width"), number(width));
  if (dashed)
    xml.writeAttribute(QStringLiteral("stroke-dasharray"),
                       QStringLiteral("8 5"));
  xml.writeEndElement();
}

void writeSvgPolyline(QXmlStreamWriter &xml, const QVector<QPointF> &points,
                      const QColor &color, qreal width, bool dashed = false) {
  if (points.size() < 2)
    return;
  QStringList values;
  values.reserve(points.size());
  for (const QPointF &point : points)
    values.append(number(point.x()) + u',' + number(point.y()));
  xml.writeStartElement(QStringLiteral("polyline"));
  xml.writeAttribute(QStringLiteral("points"), values.join(u' '));
  xml.writeAttribute(QStringLiteral("fill"), QStringLiteral("none"));
  xml.writeAttribute(QStringLiteral("stroke"), colorName(color));
  xml.writeAttribute(QStringLiteral("stroke-width"), number(width));
  xml.writeAttribute(QStringLiteral("stroke-linejoin"),
                     QStringLiteral("miter"));
  if (dashed)
    xml.writeAttribute(QStringLiteral("stroke-dasharray"),
                       QStringLiteral("8 5"));
  xml.writeEndElement();
}

void writeSvgText(QXmlStreamWriter &xml, const QRectF &rect,
                  const QString &text, const QFont &font, const QColor &color,
                  Qt::Alignment alignment = Qt::AlignCenter,
                  bool italic = false) {
  if (text.isEmpty() || rect.isEmpty())
    return;
  const bool left = alignment.testFlag(Qt::AlignLeft);
  const bool right = alignment.testFlag(Qt::AlignRight);
  const qreal x = left ? rect.left() : right ? rect.right() : rect.center().x();
  xml.writeStartElement(QStringLiteral("text"));
  xml.writeAttribute(QStringLiteral("x"), number(x));
  xml.writeAttribute(QStringLiteral("y"), number(rect.center().y()));
  xml.writeAttribute(QStringLiteral("fill"), colorName(color));
  xml.writeAttribute(QStringLiteral("font-family"), font.family());
  xml.writeAttribute(QStringLiteral("font-size"), number(fontPixelSize(font)));
  if (font.bold())
    xml.writeAttribute(QStringLiteral("font-weight"), QStringLiteral("700"));
  if (italic || font.italic())
    xml.writeAttribute(QStringLiteral("font-style"), QStringLiteral("italic"));
  xml.writeAttribute(QStringLiteral("text-anchor"),
                     left    ? QStringLiteral("start")
                     : right ? QStringLiteral("end")
                             : QStringLiteral("middle"));
  xml.writeAttribute(QStringLiteral("dominant-baseline"),
                     QStringLiteral("central"));
  xml.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
  xml.writeCharacters(text);
  xml.writeEndElement();
}

void writeSvgDecoration(QXmlStreamWriter &xml,
                        RelationshipDecoration decoration, const QPointF &tip,
                        const QPointF &adjacent, const QColor &color,
                        const QColor &background) {
  if (decoration == RelationshipDecoration::None)
    return;
  const QPointF delta = adjacent - tip;
  const qreal length = std::hypot(delta.x(), delta.y());
  if (length <= 1.0)
    return;
  const QPointF unit = delta / length;
  const QPointF normal(-unit.y(), unit.x());
  if (decoration == RelationshipDecoration::OpenArrow ||
      decoration == RelationshipDecoration::HollowTriangle) {
    const QPointF first = tip + unit * 12.0 + normal * 5.0;
    const QPointF second = tip + unit * 12.0 - normal * 5.0;
    if (decoration == RelationshipDecoration::HollowTriangle) {
      xml.writeStartElement(QStringLiteral("polygon"));
      xml.writeAttribute(QStringLiteral("points"),
                         QStringLiteral("%1,%2 %3,%4 %5,%6")
                             .arg(number(tip.x()), number(tip.y()),
                                  number(first.x()), number(first.y()),
                                  number(second.x()), number(second.y())));
      xml.writeAttribute(QStringLiteral("fill"), colorName(background));
      xml.writeAttribute(QStringLiteral("stroke"), colorName(color));
      xml.writeAttribute(QStringLiteral("stroke-width"), QStringLiteral("1.5"));
      xml.writeEndElement();
    } else {
      writeSvgLine(xml, tip, first, color, 1.5);
      writeSvgLine(xml, tip, second, color, 1.5);
    }
    return;
  }
  if (decoration == RelationshipDecoration::CirclePlus) {
    const QPointF center = tip + unit * 7.0;
    xml.writeStartElement(QStringLiteral("circle"));
    xml.writeAttribute(QStringLiteral("cx"), number(center.x()));
    xml.writeAttribute(QStringLiteral("cy"), number(center.y()));
    xml.writeAttribute(QStringLiteral("r"), QStringLiteral("6"));
    xml.writeAttribute(QStringLiteral("fill"), colorName(background));
    xml.writeAttribute(QStringLiteral("stroke"), colorName(color));
    xml.writeAttribute(QStringLiteral("stroke-width"), QStringLiteral("1.5"));
    xml.writeEndElement();
    writeSvgLine(xml, center - unit * 3.25, center + unit * 3.25, color, 1.5);
    writeSvgLine(xml, center - normal * 3.25, center + normal * 3.25, color,
                 1.5);
    return;
  }
  const QPointF first = tip + unit * 8.0 + normal * 5.5;
  const QPointF far = tip + unit * 16.0;
  const QPointF second = tip + unit * 8.0 - normal * 5.5;
  xml.writeStartElement(QStringLiteral("polygon"));
  xml.writeAttribute(QStringLiteral("points"),
                     QStringLiteral("%1,%2 %3,%4 %5,%6 %7,%8")
                         .arg(number(tip.x()), number(tip.y()),
                              number(first.x()), number(first.y()),
                              number(far.x()), number(far.y()),
                              number(second.x()), number(second.y())));
  xml.writeAttribute(
      QStringLiteral("fill"),
      colorName(decoration == RelationshipDecoration::FilledDiamond
                    ? color
                    : background));
  xml.writeAttribute(QStringLiteral("stroke"), colorName(color));
  xml.writeAttribute(QStringLiteral("stroke-width"), QStringLiteral("1.5"));
  xml.writeEndElement();
}

void writeSvgSequenceArrow(QXmlStreamWriter &xml, const QPointF &tip,
                           const QPointF &adjacent, bool filled,
                           const QColor &color) {
  const QPointF delta = adjacent - tip;
  const qreal length = std::hypot(delta.x(), delta.y());
  if (length <= 1.0)
    return;
  const QPointF unit = delta / length;
  const QPointF normal(-unit.y(), unit.x());
  const QPointF first = tip + unit * 11.0 + normal * 5.0;
  const QPointF second = tip + unit * 11.0 - normal * 5.0;
  if (filled) {
    xml.writeStartElement(QStringLiteral("polygon"));
    xml.writeAttribute(QStringLiteral("points"),
                       QStringLiteral("%1,%2 %3,%4 %5,%6")
                           .arg(number(tip.x()), number(tip.y()),
                                number(first.x()), number(first.y()),
                                number(second.x()), number(second.y())));
    xml.writeAttribute(QStringLiteral("fill"), colorName(color));
    xml.writeEndElement();
  } else {
    writeSvgLine(xml, tip, first, color, 1.5);
    writeSvgLine(xml, tip, second, color, 1.5);
  }
}

void beginSvgClipGroup(QXmlStreamWriter &xml, const QString &clipId) {
  xml.writeStartElement(QStringLiteral("g"));
  xml.writeAttribute(QStringLiteral("clip-path"),
                     QStringLiteral("url(#%1)").arg(clipId));
}

void writeSvgContainerFillAndTitle(QXmlStreamWriter &xml,
                                   const ExportContainer &container,
                                   const QFont &base, const QFont &header,
                                   const QFont &small) {
  const QString clipId = QStringLiteral("clip-container-") + container.id;
  beginSvgClipGroup(xml, clipId);
  const qreal titleHeight = container.package
                                ? kPackageHeaderHeight
                                : presentation_layout::kContainerHeaderHeight;
  if (container.package) {
    const qreal tabWidth =
        std::clamp(container.titleWidth, 90.0, container.rect.width());
    writeSvgRect(xml,
                 {container.rect.left(), container.rect.top() + titleHeight,
                  container.rect.width(),
                  container.rect.height() - titleHeight},
                 container.style.fill);
    writeSvgRect(
        xml,
        {container.rect.left(), container.rect.top(), tabWidth, titleHeight},
        container.style.headerFill);
    const qreal textWidth = tabWidth - 2.0 * kPadding;
    if (!container.stereotype.isEmpty()) {
      writeSvgText(xml,
                   {container.rect.left() + kPadding, container.rect.top(),
                    textWidth, 11.0},
                   container.stereotype, small, container.style.primaryText);
      writeSvgText(xml,
                   {container.rect.left() + kPadding,
                    container.rect.top() + 10.0, textWidth, titleHeight - 10.0},
                   elided(container.name, header, textWidth), header,
                   container.style.primaryText,
                   Qt::AlignLeft | Qt::AlignVCenter);
    } else {
      writeSvgText(xml,
                   {container.rect.left() + kPadding, container.rect.top(),
                    textWidth, titleHeight},
                   elided(container.name, header, textWidth), header,
                   container.style.primaryText,
                   Qt::AlignLeft | Qt::AlignVCenter);
    }
  } else {
    writeSvgRect(xml, container.rect, container.style.fill);
    writeSvgRect(xml,
                 {container.rect.left(), container.rect.top(),
                  container.rect.width(), titleHeight},
                 container.style.headerFill);
    const QRectF title(container.rect.left() + kPadding, container.rect.top(),
                       container.rect.width() - 2.0 * kPadding, titleHeight);
    writeSvgText(xml, title, elided(container.name, header, title.width()),
                 header, container.style.primaryText,
                 Qt::AlignLeft | Qt::AlignVCenter);
  }
  Q_UNUSED(base)
  xml.writeEndElement();
}

void writeSvgContainerOutline(QXmlStreamWriter &xml,
                              const ExportContainer &container) {
  const QString clipId = QStringLiteral("clip-container-") + container.id;
  beginSvgClipGroup(xml, clipId);
  if (container.package) {
    const qreal tabWidth =
        std::clamp(container.titleWidth, 90.0, container.rect.width());
    const qreal left = container.rect.left();
    const qreal top = container.rect.top();
    const qreal right = container.rect.right();
    const qreal bottom = container.rect.bottom();
    const qreal tabRight = left + tabWidth;
    const qreal bodyTop = top + kPackageHeaderHeight;
    QVector<QPointF> outline{
        {left, bodyTop},  {left, top},     {tabRight, top}, {tabRight, bodyTop},
        {right, bodyTop}, {right, bottom}, {left, bottom},  {left, bodyTop}};
    writeSvgPolyline(xml, outline, container.style.border, 1.2);
  } else {
    writeSvgRect(xml, container.rect, Qt::transparent, container.style.border,
                 1.2);
    writeSvgLine(
        xml,
        {container.rect.left(),
         container.rect.top() + presentation_layout::kContainerHeaderHeight},
        {container.rect.right(),
         container.rect.top() + presentation_layout::kContainerHeaderHeight},
        container.style.divider, 1.0);
  }
  const QRectF content = container.rect;
  const qreal markerLength = 28.0;
  const qreal markerWidth = 4.0;
  const QColor warning = uiPalette().warningBorder;
  if (container.overflowEdges.testFlag(ContainerOverflowEdge::Left))
    writeSvgRect(xml,
                 {content.left(), content.center().y() - markerLength / 2.0,
                  markerWidth, markerLength},
                 warning);
  if (container.overflowEdges.testFlag(ContainerOverflowEdge::Right))
    writeSvgRect(xml,
                 {content.right() - markerWidth,
                  content.center().y() - markerLength / 2.0, markerWidth,
                  markerLength},
                 warning);
  if (container.overflowEdges.testFlag(ContainerOverflowEdge::Top))
    writeSvgRect(xml,
                 {content.center().x() - markerLength / 2.0, content.top(),
                  markerLength, markerWidth},
                 warning);
  if (container.overflowEdges.testFlag(ContainerOverflowEdge::Bottom))
    writeSvgRect(xml,
                 {content.center().x() - markerLength / 2.0,
                  content.bottom() - markerWidth, markerLength, markerWidth},
                 warning);
  xml.writeEndElement();
}

void writeSvgNode(QXmlStreamWriter &xml, const ExportNode &node,
                  const QFont &base, const QFont &header) {
  const QString clipId = QStringLiteral("clip-node-") + node.id;
  beginSvgClipGroup(xml, clipId);
  const qreal headerHeight = nodeHeaderHeight(node);
  writeSvgRect(xml, node.rect, node.style.fill, node.style.border, 1.2);
  writeSvgRect(
      xml, {node.rect.left(), node.rect.top(), node.rect.width(), headerHeight},
      node.style.headerFill);
  writeSvgLine(xml, {node.rect.left(), node.rect.top() + headerHeight},
               {node.rect.right(), node.rect.top() + headerHeight},
               node.style.divider, 1.0);
  if (node.type != ElementType::Enumeration && !node.attributes.isEmpty() &&
      !node.operations.isEmpty()) {
    const qreal dividerY =
        node.rect.top() + headerHeight + node.attributes.size() * kLineHeight;
    if (dividerY <= node.rect.bottom())
      writeSvgLine(xml, {node.rect.left(), dividerY},
                   {node.rect.right(), dividerY}, node.style.divider, 1.0);
  }

  const qreal headerTop =
      node.rect.top() + (node.stereotype.isEmpty() ? 0.0 : kLineHeight);
  if (!node.stereotype.isEmpty()) {
    writeSvgText(xml,
                 {node.rect.left() + kPadding, node.rect.top(),
                  node.rect.width() - 2.0 * kPadding, kLineHeight},
                 node.stereotype, base, node.style.secondaryText);
  }
  qreal titleLeft = node.rect.left() + kPadding;
  if (!node.iconDataUri.isEmpty()) {
    xml.writeStartElement(QStringLiteral("image"));
    xml.writeAttribute(QStringLiteral("x"), number(titleLeft));
    xml.writeAttribute(
        QStringLiteral("y"),
        number(headerTop + (kHeaderHeight - kTypeIconSize) / 2.0));
    xml.writeAttribute(QStringLiteral("width"), number(kTypeIconSize));
    xml.writeAttribute(QStringLiteral("height"), number(kTypeIconSize));
    xml.writeAttribute(QStringLiteral("href"), node.iconDataUri);
    xml.writeEndElement();
    titleLeft += kTypeIconSize + kTypeIconSpacing;
  }
  const QRectF title(titleLeft, headerTop,
                     node.rect.right() - kPadding - titleLeft, kHeaderHeight);
  writeSvgText(xml, title, elided(node.name, header, title.width()), header,
               node.style.primaryText);

  int line = 0;
  const auto writeRows = [&](const QStringList &rows) {
    for (const QString &row : rows) {
      const QRectF target(node.rect.left() + kPadding,
                          node.rect.top() + headerHeight + line * kLineHeight,
                          node.rect.width() - 2.0 * kPadding, kLineHeight);
      writeSvgText(xml, target, row, base, node.style.secondaryText,
                   Qt::AlignLeft | Qt::AlignVCenter);
      ++line;
    }
  };
  if (node.type == ElementType::Enumeration)
    writeRows(node.enumLiterals);
  else {
    writeRows(node.attributes);
    writeRows(node.operations);
  }
  xml.writeEndElement();
}

QStringList wrappedText(const QString &text, const QFont &font, qreal width,
                        int maximumLines) {
  QStringList result;
  const QFontMetricsF metrics(font);
  for (const QString &paragraph : text.split(u'\n')) {
    if (paragraph.isEmpty()) {
      result.append(QString{});
      continue;
    }
    QString current;
    for (const QString &word :
         paragraph.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
      const QString candidate =
          current.isEmpty() ? word : current + u' ' + word;
      if (!current.isEmpty() && metrics.horizontalAdvance(candidate) > width) {
        result.append(current);
        current = word;
      } else {
        current = candidate;
      }
      if (result.size() >= maximumLines)
        break;
    }
    if (!current.isEmpty() && result.size() < maximumLines)
      result.append(current);
    if (result.size() >= maximumLines)
      break;
  }
  if (result.size() == maximumLines &&
      metrics.horizontalAdvance(result.last()) > width)
    result.last() = metrics.elidedText(result.last(), Qt::ElideRight, width);
  return result;
}

void writeSvgNote(QXmlStreamWriter &xml, const ExportNote &note,
                  const QFont &font) {
  const UiPalette palette = uiPalette();
  const QRectF rect = note.rect;
  const QPointF foldBottom(rect.left() + kNoteFoldSize, rect.bottom());
  const QPointF foldLeft(rect.left(), rect.bottom() - kNoteFoldSize);
  xml.writeStartElement(QStringLiteral("polygon"));
  xml.writeAttribute(QStringLiteral("points"),
                     QStringLiteral("%1,%2 %3,%4 %5,%6 %7,%8 %9,%10")
                         .arg(number(rect.left()), number(rect.top()),
                              number(rect.right()), number(rect.top()),
                              number(rect.right()), number(rect.bottom()),
                              number(foldBottom.x()), number(foldBottom.y()),
                              number(foldLeft.x()), number(foldLeft.y())));
  xml.writeAttribute(QStringLiteral("fill"), colorName(palette.noteFill));
  xml.writeAttribute(QStringLiteral("stroke"), colorName(palette.noteBorder));
  xml.writeAttribute(QStringLiteral("stroke-width"), QStringLiteral("1.2"));
  xml.writeEndElement();
  xml.writeStartElement(QStringLiteral("polygon"));
  const QString foldPoints =
      QStringLiteral("%1,%2 %3,%4 %5,%6")
          .arg(number(foldLeft.x()), number(foldLeft.y()),
               number(foldBottom.x()), number(foldBottom.y()),
               number(rect.left() + kNoteFoldSize),
               number(rect.bottom() - kNoteFoldSize));
  xml.writeAttribute(QStringLiteral("points"), foldPoints);
  xml.writeAttribute(QStringLiteral("fill"), colorName(palette.noteFold));
  xml.writeEndElement();

  const QRectF textRect = rect.adjusted(12.0, 10.0, -12.0, -12.0);
  const int maximumLines =
      std::max(1, static_cast<int>(textRect.height() / kLineHeight));
  const QStringList lines = wrappedText(plainMarkdown(note.markdown), font,
                                        textRect.width(), maximumLines);
  for (qsizetype index = 0; index < lines.size(); ++index) {
    writeSvgText(xml,
                 {textRect.left(), textRect.top() + index * kLineHeight,
                  textRect.width(), kLineHeight},
                 lines.at(index), font, palette.noteText,
                 Qt::AlignLeft | Qt::AlignVCenter);
  }
}

void writeSvgSequence(QXmlStreamWriter &xml, const ExportScene &scene,
                      const QFont &base, const QFont &header,
                      const QFont &small) {
  const UiPalette palette = uiPalette();
  for (const SequenceCombinedFragmentLayout &fragment :
       scene.sequence.fragments) {
    writeSvgRect(xml, fragment.rect, Qt::transparent, palette.connector, 1.2);
    for (const SequenceCombinedFragmentOperandLayout &operand :
         fragment.operands) {
      if (operand.dividerY >= 0.0)
        writeSvgLine(xml, {fragment.rect.left(), operand.dividerY},
                     {fragment.rect.right(), operand.dividerY},
                     palette.connector, 1.2);
    }
    const QString firstGuard = fragment.operands.isEmpty()
                                   ? QString{}
                                   : fragment.operands.constFirst().guard;
    const QString title = firstGuard.isEmpty()
                              ? toString(fragment.type)
                              : QStringLiteral("%1 [%2]").arg(
                                    toString(fragment.type), firstGuard);
    writeSvgText(xml, fragment.headingRect, title, header, palette.bodyText,
                 Qt::AlignLeft | Qt::AlignVCenter);
    for (qsizetype index = 1; index < fragment.operands.size(); ++index) {
      const auto &operand = fragment.operands.at(index);
      if (operand.dividerY >= 0.0 && !operand.guard.isEmpty())
        writeSvgText(xml,
                     {fragment.rect.left() + 8.0, operand.dividerY + 1.0,
                      fragment.rect.width() - 16.0, kLineHeight},
                     QStringLiteral("[%1]").arg(operand.guard), base,
                     palette.bodyText, Qt::AlignLeft | Qt::AlignVCenter);
    }
  }

  for (const SequenceParticipantLayout &participant :
       scene.sequence.participants) {
    writeSvgLine(xml, {participant.lifelineX, participant.lifelineStartY},
                 {participant.lifelineX, participant.lifelineEndY},
                 palette.connector, 1.2, true);
  }
  for (const SequenceActivationLayout &activation :
       scene.sequence.activations) {
    writeSvgRect(xml, activation.rect, palette.surface, palette.connector, 1.2);
    QString label = activation.label;
    if (!activation.executionContextLabel.isEmpty())
      label = label.isEmpty() ? activation.executionContextLabel
                              : QStringLiteral("%1 — %2").arg(
                                    label, activation.executionContextLabel);
    if (!label.isEmpty())
      writeSvgText(xml,
                   {activation.rect.right() + 4.0, activation.rect.top(), 220.0,
                    kLineHeight},
                   label, small, palette.mutedText,
                   Qt::AlignLeft | Qt::AlignVCenter);
  }

  for (const SequenceMessageLayout &message : scene.sequence.messages) {
    if (message.points.size() < 2)
      continue;
    const QColor color =
        message.confidence == SequenceEvidenceConfidence::Inferred
            ? palette.mutedText
            : palette.connector;
    writeSvgPolyline(xml, message.points, color, 1.5,
                     message.type == SequenceMessageType::Return);
    writeSvgSequenceArrow(xml, message.points.constLast(),
                          message.points.at(message.points.size() - 2),
                          message.type == SequenceMessageType::Synchronous,
                          color);
    if (message.type == SequenceMessageType::Destroy) {
      const QPointF tip = message.points.constLast();
      writeSvgLine(xml, tip + QPointF(-7.0, -7.0), tip + QPointF(7.0, 7.0),
                   color, 1.8);
      writeSvgLine(xml, tip + QPointF(-7.0, 7.0), tip + QPointF(7.0, -7.0),
                   color, 1.8);
    }
    if (!message.text.isEmpty())
      writeSvgText(xml, message.labelRect,
                   elided(message.text, base, message.labelRect.width()), base,
                   message.confidence == SequenceEvidenceConfidence::Inferred
                       ? palette.mutedText
                       : palette.bodyText,
                   Qt::AlignCenter,
                   message.confidence == SequenceEvidenceConfidence::Inferred);
  }

  for (const SequenceParticipantLayout &participant :
       scene.sequence.participants) {
    writeSvgRect(xml, participant.headerRect, palette.classFill,
                 palette.nodeBorder, 1.2);
    const bool hasQualifier = !participant.qualifier.isEmpty();
    const bool hasContext = !participant.executionContextLabel.isEmpty();
    const qreal secondaryTop =
        participant.headerRect.bottom() -
        (static_cast<int>(hasQualifier) + static_cast<int>(hasContext)) *
            kLineHeight;
    const QRectF nameRect(
        participant.headerRect.left() + kPadding, participant.headerRect.top(),
        participant.headerRect.width() - 2.0 * kPadding,
        std::max(kLineHeight, secondaryTop - participant.headerRect.top()));
    writeSvgText(xml, nameRect,
                 elided(participant.name, header, nameRect.width()), header,
                 palette.nodeTitleText);
    if (hasQualifier)
      writeSvgText(xml,
                   {participant.headerRect.left() + kPadding, secondaryTop,
                    participant.headerRect.width() - 2.0 * kPadding,
                    kLineHeight},
                   elided(participant.qualifier, small,
                          participant.headerRect.width() - 2.0 * kPadding),
                   small, palette.mutedText);
    if (hasContext) {
      writeSvgLine(xml,
                   {participant.headerRect.left(),
                    participant.headerRect.bottom() - kLineHeight},
                   {participant.headerRect.right(),
                    participant.headerRect.bottom() - kLineHeight},
                   palette.compartmentLine, 1.0);
      writeSvgText(xml,
                   {participant.headerRect.left() + kPadding,
                    participant.headerRect.bottom() - kLineHeight,
                    participant.headerRect.width() - 2.0 * kPadding,
                    kLineHeight},
                   elided(participant.executionContextLabel, small,
                          participant.headerRect.width() - 2.0 * kPadding),
                   small, palette.mutedText);
    }
  }
}

bool writeSvg(const ExportScene &scene, const QString &path, qreal margin,
              QSize &documentSize, QString &error) {
  if (!scene.contentBounds.isValid() || scene.contentBounds.isEmpty()) {
    error = QStringLiteral("Cannot export an empty diagram.");
    return false;
  }
  const qreal safeMargin = std::max(0.0, margin);
  const QRectF documentBounds = scene.contentBounds.adjusted(
      -safeMargin, -safeMargin, safeMargin, safeMargin);
  documentSize = {std::max(1, qCeil(documentBounds.width())),
                  std::max(1, qCeil(documentBounds.height()))};
  const QPointF offset = -documentBounds.topLeft();

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    error = QStringLiteral("Could not open SVG export for writing: %1")
                .arg(file.errorString());
    return false;
  }
  QXmlStreamWriter xml(&file);
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  xml.writeStartElement(QStringLiteral("svg"));
  xml.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
  xml.writeAttribute(QStringLiteral("width"), number(documentSize.width()));
  xml.writeAttribute(QStringLiteral("height"), number(documentSize.height()));
  xml.writeAttribute(
      QStringLiteral("viewBox"),
      QStringLiteral("0 0 %1 %2")
          .arg(number(documentSize.width()), number(documentSize.height())));
  xml.writeAttribute(QStringLiteral("role"), QStringLiteral("img"));
  xml.writeAttribute(QStringLiteral("aria-label"), scene.diagramName);
  xml.writeTextElement(QStringLiteral("title"), scene.diagramName);
  xml.writeTextElement(
      QStringLiteral("desc"),
      QStringLiteral("Vector diagram exported by yauml (Yet Another UML)."));
  writeSvgRect(xml,
               {0.0, 0.0, static_cast<qreal>(documentSize.width()),
                static_cast<qreal>(documentSize.height())},
               uiPalette().surface);

  if (scene.type == DiagramType::Class) {
    xml.writeStartElement(QStringLiteral("defs"));
    for (const ExportContainer &container : scene.containers) {
      const QRectF visible =
          container.clip.active
              ? container.rect.intersected(container.clip.rect)
              : container.rect;
      xml.writeStartElement(QStringLiteral("clipPath"));
      xml.writeAttribute(QStringLiteral("id"),
                         QStringLiteral("clip-container-") + container.id);
      writeSvgRect(xml, visible, uiPalette().surface);
      xml.writeEndElement();
    }
    for (const ExportNode &node : scene.nodes) {
      const QRectF visible =
          node.clip.active ? node.rect.intersected(node.clip.rect) : node.rect;
      xml.writeStartElement(QStringLiteral("clipPath"));
      xml.writeAttribute(QStringLiteral("id"),
                         QStringLiteral("clip-node-") + node.id);
      writeSvgRect(xml, visible, uiPalette().surface);
      xml.writeEndElement();
    }
    xml.writeEndElement();
  }

  xml.writeStartElement(QStringLiteral("g"));
  xml.writeAttribute(QStringLiteral("transform"),
                     QStringLiteral("translate(%1 %2)")
                         .arg(number(offset.x()), number(offset.y())));
  const QFont base = exportBaseFont();
  QFont header = base;
  header.setBold(true);
  QFont small = base;
  if (small.pointSizeF() > 0.0)
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 2.0));
  else if (small.pixelSize() > 0)
    small.setPixelSize(std::max(9, small.pixelSize() - 2));

  if (scene.type == DiagramType::Sequence) {
    writeSvgSequence(xml, scene, base, header, small);
  } else {
    for (const ExportContainer &container : scene.containers)
      writeSvgContainerFillAndTitle(xml, container, base, header, small);

    for (const ExportNoteAttachment &attachment : scene.noteAttachments)
      writeSvgLine(xml, attachment.source, attachment.target,
                   uiPalette().noteAttachment, 1.4, true);

    for (const ExportConnector &connector : scene.connectors) {
      const RelationshipVisualStyle style =
          relationshipVisualStyle(connector.type);
      writeSvgPolyline(xml, connector.points, uiPalette().connector, 1.5,
                       style.line == RelationshipLineStyle::Dashed);
      if (connector.points.size() >= 2) {
        writeSvgDecoration(xml, style.source, connector.points.constFirst(),
                           connector.points.at(1), uiPalette().connector,
                           uiPalette().surface);
        writeSvgDecoration(xml, style.target, connector.points.constLast(),
                           connector.points.at(connector.points.size() - 2),
                           uiPalette().connector, uiPalette().surface);
      }
      for (const ExportAnnotation &annotation : connector.annotations)
        writeSvgText(xml, annotation.rect, annotation.text, base,
                     uiPalette().bodyText);
    }

    for (const ExportNode &node : scene.nodes)
      writeSvgNode(xml, node, base, header);
    for (const ExportNote &note : scene.notes)
      writeSvgNote(xml, note, base);
    for (const ExportContainer &container : scene.containers)
      writeSvgContainerOutline(xml, container);
  }
  xml.writeEndElement();
  xml.writeEndElement();
  xml.writeEndDocument();
  if (xml.hasError()) {
    error = QStringLiteral("Could not serialize the SVG document.");
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    error = QStringLiteral("Could not finish SVG export: %1")
                .arg(file.errorString());
    return false;
  }
  return true;
}

QString htmlLine(const QString &text) {
  return text.toHtmlEscaped().replace(u'\n', QStringLiteral("<br>"));
}

QString basicMarkdownHtml(const QString &markdown) {
  QString html = markdown.toHtmlEscaped();
  html.replace(QRegularExpression(QStringLiteral(R"(\*\*([^\n*]+)\*\*)")),
               QStringLiteral("<b>\\1</b>"));
  html.replace(QRegularExpression(QStringLiteral(R"(__([^\n_]+)__)")),
               QStringLiteral("<b>\\1</b>"));
  html.replace(QRegularExpression(QStringLiteral(R"(\*([^\n*]+)\*)")),
               QStringLiteral("<i>\\1</i>"));
  html.replace(QRegularExpression(QStringLiteral(R"(_([^\n_]+)_)")),
               QStringLiteral("<i>\\1</i>"));
  html.replace(QRegularExpression(QStringLiteral(R"(`([^\n`]+)`)")),
               QStringLiteral("<code>\\1</code>"));
  return html.replace(u'\n', QStringLiteral("<br>"));
}

QString drawioNodeTitleHtml(const ExportNode &node) {
  QString title;
  if (!node.stereotype.isEmpty())
    title += QStringLiteral("<div>%1</div>").arg(htmlLine(node.stereotype));
  title +=
      QStringLiteral("<div><b>%1%2</b></div>")
          .arg(node.iconDataUri.isEmpty()
                   ? QString{}
                   : QStringLiteral(
                         "<img src=\"%1\" width=\"16\" height=\"16\" "
                         "style=\"vertical-align:middle;margin-right:4px\">")
                         .arg(node.iconDataUri),
               htmlLine(node.name));
  return title;
}

QString drawioContainerHtml(const ExportContainer &container) {
  const QString title =
      container.stereotype.isEmpty()
          ? QStringLiteral("<b>%1</b>").arg(htmlLine(container.name))
          : QStringLiteral("<div>%1</div><b>%2</b>")
                .arg(htmlLine(container.stereotype), htmlLine(container.name));
  return QStringLiteral("<div style=\"text-align:left;padding:4px 8px\">%1"
                        "</div>")
      .arg(title);
}

QString drawioEdgeStyle(RelationshipType type, const QColor &color) {
  QString style =
      QStringLiteral(
          "edgeStyle=none;orthogonalLoop=1;jettySize=auto;html=1;rounded=0;"
          "strokeColor=%1;strokeWidth=1.5;startArrow=none;endArrow=none;")
          .arg(colorName(color));
  switch (type) {
  case RelationshipType::Dependency:
    style +=
        QStringLiteral("dashed=1;dashPattern=8 5;endArrow=open;endFill=0;");
    break;
  case RelationshipType::Generalization:
    style += QStringLiteral("endArrow=block;endFill=0;");
    break;
  case RelationshipType::Realization:
    style +=
        QStringLiteral("dashed=1;dashPattern=8 5;endArrow=block;endFill=0;");
    break;
  case RelationshipType::Association:
    style += QStringLiteral("endArrow=open;endFill=0;");
    break;
  case RelationshipType::Aggregation:
    style += QStringLiteral("startArrow=diamond;startFill=0;");
    break;
  case RelationshipType::Composition:
    style += QStringLiteral("startArrow=diamond;startFill=1;");
    break;
  case RelationshipType::Containment:
    style += QStringLiteral("startArrow=oval;startFill=0;");
    break;
  }
  return style;
}

QString drawioEndpointConstraint(const QString &prefix, const QRectF &rect,
                                 const QPointF &point) {
  if (!rect.isValid() || rect.isEmpty())
    return {};
  const qreal x =
      std::clamp((point.x() - rect.left()) / rect.width(), 0.0, 1.0);
  const qreal y =
      std::clamp((point.y() - rect.top()) / rect.height(), 0.0, 1.0);
  return QStringLiteral("%1X=%2;%1Y=%3;%1Perimeter=0;")
      .arg(prefix, number(x), number(y));
}

class DrawioWriter final {
public:
  DrawioWriter(QXmlStreamWriter &xml, QPointF offset)
      : m_xml(xml), m_offset(offset) {}

  QString nextId(const QString &prefix = QStringLiteral("cell")) {
    return prefix + QString::number(++m_nextId);
  }

  QString vertex(const QString &value, const QString &style,
                 const QRectF &sceneRect, const QString &yaumlId = {},
                 qreal alternateHeight = -1.0) {
    return writeVertex(value, style, sceneRect, QStringLiteral("1"), yaumlId,
                       true, alternateHeight);
  }

  QString childVertex(const QString &value, const QString &style,
                      const QRectF &parentRect, const QString &parent) {
    return writeVertex(value, style, parentRect, parent, {}, false, -1.0);
  }

  QString edge(const QString &style, const QVector<QPointF> &scenePoints,
               const QString &yaumlId = {}, const QString &source = {},
               const QString &target = {}) {
    if (scenePoints.size() < 2)
      return {};
    const QString id = nextId(QStringLiteral("edge"));
    m_xml.writeStartElement(QStringLiteral("mxCell"));
    m_xml.writeAttribute(QStringLiteral("id"), id);
    m_xml.writeAttribute(QStringLiteral("value"), QString{});
    m_xml.writeAttribute(QStringLiteral("style"), style);
    m_xml.writeAttribute(QStringLiteral("edge"), QStringLiteral("1"));
    m_xml.writeAttribute(QStringLiteral("parent"), QStringLiteral("1"));
    if (!yaumlId.isEmpty())
      m_xml.writeAttribute(QStringLiteral("yaumlId"), yaumlId);
    if (!source.isEmpty())
      m_xml.writeAttribute(QStringLiteral("source"), source);
    if (!target.isEmpty())
      m_xml.writeAttribute(QStringLiteral("target"), target);
    m_xml.writeStartElement(QStringLiteral("mxGeometry"));
    m_xml.writeAttribute(QStringLiteral("relative"), QStringLiteral("1"));
    m_xml.writeAttribute(QStringLiteral("as"), QStringLiteral("geometry"));
    writePoint(scenePoints.constFirst(), QStringLiteral("sourcePoint"));
    writePoint(scenePoints.constLast(), QStringLiteral("targetPoint"));
    if (scenePoints.size() > 2) {
      m_xml.writeStartElement(QStringLiteral("Array"));
      m_xml.writeAttribute(QStringLiteral("as"), QStringLiteral("points"));
      for (qsizetype index = 1; index + 1 < scenePoints.size(); ++index)
        writePoint(scenePoints.at(index));
      m_xml.writeEndElement();
    }
    m_xml.writeEndElement();
    m_xml.writeEndElement();
    return id;
  }

private:
  QString writeVertex(const QString &value, const QString &style,
                      const QRectF &rect, const QString &parent,
                      const QString &yaumlId, bool applyDocumentOffset,
                      qreal alternateHeight) {
    const QString id = nextId();
    m_xml.writeStartElement(QStringLiteral("mxCell"));
    m_xml.writeAttribute(QStringLiteral("id"), id);
    m_xml.writeAttribute(QStringLiteral("value"), value);
    m_xml.writeAttribute(QStringLiteral("style"), style);
    m_xml.writeAttribute(QStringLiteral("vertex"), QStringLiteral("1"));
    m_xml.writeAttribute(QStringLiteral("parent"), parent);
    if (!yaumlId.isEmpty())
      m_xml.writeAttribute(QStringLiteral("yaumlId"), yaumlId);
    m_xml.writeStartElement(QStringLiteral("mxGeometry"));
    const QRectF outputRect =
        applyDocumentOffset ? QRectF(rect.topLeft() + m_offset, rect.size())
                            : rect;
    m_xml.writeAttribute(QStringLiteral("x"), number(outputRect.x()));
    m_xml.writeAttribute(QStringLiteral("y"), number(outputRect.y()));
    m_xml.writeAttribute(QStringLiteral("width"), number(outputRect.width()));
    m_xml.writeAttribute(QStringLiteral("height"), number(outputRect.height()));
    m_xml.writeAttribute(QStringLiteral("as"), QStringLiteral("geometry"));
    if (alternateHeight > 0.0) {
      m_xml.writeStartElement(QStringLiteral("mxRectangle"));
      m_xml.writeAttribute(QStringLiteral("x"), number(outputRect.x()));
      m_xml.writeAttribute(QStringLiteral("y"), number(outputRect.y()));
      m_xml.writeAttribute(QStringLiteral("width"), number(outputRect.width()));
      m_xml.writeAttribute(QStringLiteral("height"), number(alternateHeight));
      m_xml.writeAttribute(QStringLiteral("as"),
                           QStringLiteral("alternateBounds"));
      m_xml.writeEndElement();
    }
    m_xml.writeEndElement();
    m_xml.writeEndElement();
    return id;
  }

  void writePoint(const QPointF &scenePoint, const QString &role = {}) {
    const QPointF point = scenePoint + m_offset;
    m_xml.writeStartElement(QStringLiteral("mxPoint"));
    m_xml.writeAttribute(QStringLiteral("x"), number(point.x()));
    m_xml.writeAttribute(QStringLiteral("y"), number(point.y()));
    if (!role.isEmpty())
      m_xml.writeAttribute(QStringLiteral("as"), role);
    m_xml.writeEndElement();
  }

  QXmlStreamWriter &m_xml;
  QPointF m_offset;
  int m_nextId = 1;
};

QString
transparentTextStyle(const QColor &color,
                     const QString &alignment = QStringLiteral("center")) {
  return QStringLiteral(
             "html=1;whiteSpace=wrap;overflow=hidden;fillColor=none;"
             "strokeColor=none;fontColor=%1;align=%2;verticalAlign=middle;")
      .arg(colorName(color), alignment);
}

QString drawioUmlMemberStyle(const ExportNode &node, bool topDivider) {
  QString style =
      QStringLiteral(
          "text;html=1;whiteSpace=wrap;overflow=hidden;fillColor=none;"
          "strokeColor=none;fontColor=%1;align=left;verticalAlign=middle;"
          "spacingLeft=%2;spacingRight=%2;rotatable=0;"
          "points=[[0,0.5],[1,0.5]];portConstraint=eastwest;")
          .arg(colorName(node.style.secondaryText), number(kPadding));
  if (topDivider) {
    // partialRectangle supplies the UML compartment separator without boxing
    // every member row. It is one of Draw.io's standard editable shapes.
    style +=
        QStringLiteral("shape=partialRectangle;top=1;left=0;right=0;bottom=0;"
                       "strokeColor=%1;strokeWidth=1;")
            .arg(colorName(node.style.divider));
  }
  return style;
}

QString writeDrawioUmlNode(DrawioWriter &writer, const ExportNode &node) {
  const qreal headerHeight = nodeHeaderHeight(node);
  const QString style =
      QStringLiteral(
          "swimlane;html=1;whiteSpace=wrap;overflow=hidden;fontStyle=1;"
          "align=center;verticalAlign=middle;childLayout=stackLayout;"
          "horizontal=1;horizontalStack=0;startSize=%1;resizeParent=1;"
          "resizeParentMax=0;resizeLast=0;collapsible=1;marginBottom=0;"
          "rounded=0;shadow=0;fillColor=%2;swimlaneFillColor=%3;"
          "strokeColor=%4;fontColor=%5;separatorColor=%6;")
          .arg(number(headerHeight), colorName(node.style.headerFill),
               colorName(node.style.fill), colorName(node.style.border),
               colorName(node.style.primaryText),
               colorName(node.style.divider));
  const QString parent = writer.vertex(drawioNodeTitleHtml(node), style,
                                       node.rect, node.id, headerHeight);

  struct MemberRow {
    QString text;
    bool topDivider = false;
  };
  QVector<MemberRow> rows;
  if (node.type == ElementType::Enumeration) {
    for (const QString &literal : node.enumLiterals)
      rows.append(MemberRow{literal, false});
  } else {
    for (const QString &attribute : node.attributes)
      rows.append(MemberRow{attribute, false});
    bool firstOperation = true;
    for (const QString &operation : node.operations) {
      rows.append(
          MemberRow{operation, firstOperation && !node.attributes.isEmpty()});
      firstOperation = false;
    }
  }

  qreal y = headerHeight;
  for (const MemberRow &row : rows) {
    const qreal remainingHeight = node.rect.height() - y;
    if (remainingHeight <= 0.0)
      break;
    const qreal rowHeight = std::min(kLineHeight, remainingHeight);
    writer.childVertex(htmlLine(row.text),
                       drawioUmlMemberStyle(node, row.topDivider),
                       {0.0, y, node.rect.width(), rowHeight}, parent);
    y += rowHeight;
  }
  return parent;
}

void writeDrawioSequence(DrawioWriter &writer, const ExportScene &scene) {
  const UiPalette palette = uiPalette();
  for (const SequenceCombinedFragmentLayout &fragment :
       scene.sequence.fragments) {
    writer.vertex(
        {},
        QStringLiteral("rounded=0;fillColor=none;strokeColor=%1;html=1;")
            .arg(colorName(palette.connector)),
        fragment.rect, fragment.id);
    const QString title =
        fragment.operands.isEmpty() || fragment.operands.first().guard.isEmpty()
            ? toString(fragment.type)
            : QStringLiteral("%1 [%2]").arg(toString(fragment.type),
                                            fragment.operands.first().guard);
    writer.vertex(
        htmlLine(title),
        transparentTextStyle(palette.bodyText, QStringLiteral("left")),
        fragment.headingRect);
    for (qsizetype index = 1; index < fragment.operands.size(); ++index) {
      const auto &operand = fragment.operands.at(index);
      if (operand.dividerY < 0.0)
        continue;
      writer.edge(QStringLiteral("edgeStyle=none;endArrow=none;strokeColor=%1;")
                      .arg(colorName(palette.connector)),
                  {{fragment.rect.left(), operand.dividerY},
                   {fragment.rect.right(), operand.dividerY}});
      if (!operand.guard.isEmpty())
        writer.vertex(
            htmlLine(QStringLiteral("[%1]").arg(operand.guard)),
            transparentTextStyle(palette.bodyText, QStringLiteral("left")),
            {fragment.rect.left() + 8.0, operand.dividerY + 1.0,
             fragment.rect.width() - 16.0, kLineHeight});
    }
  }
  for (const SequenceParticipantLayout &participant :
       scene.sequence.participants) {
    writer.edge(
        QStringLiteral("edgeStyle=none;endArrow=none;dashed=1;dashPattern=8 5;"
                       "strokeColor=%1;strokeWidth=1.2;")
            .arg(colorName(palette.connector)),
        {{participant.lifelineX, participant.lifelineStartY},
         {participant.lifelineX, participant.lifelineEndY}},
        participant.id + QStringLiteral(":lifeline"));
  }
  for (const SequenceActivationLayout &activation :
       scene.sequence.activations) {
    writer.vertex(
        {},
        QStringLiteral("rounded=0;fillColor=%1;strokeColor=%2;html=1;")
            .arg(colorName(palette.surface), colorName(palette.connector)),
        activation.rect, activation.id);
    QString label = activation.label;
    if (!activation.executionContextLabel.isEmpty())
      label = label.isEmpty() ? activation.executionContextLabel
                              : QStringLiteral("%1 — %2").arg(
                                    label, activation.executionContextLabel);
    if (!label.isEmpty())
      writer.vertex(
          htmlLine(label),
          transparentTextStyle(palette.mutedText, QStringLiteral("left")),
          {activation.rect.right() + 4.0, activation.rect.top(), 220.0,
           kLineHeight});
  }
  for (const SequenceMessageLayout &message : scene.sequence.messages) {
    QString style =
        QStringLiteral("edgeStyle=none;html=1;rounded=0;strokeColor=%1;"
                       "strokeWidth=1.5;endArrow=%2;endFill=%3;")
            .arg(colorName(message.confidence ==
                                   SequenceEvidenceConfidence::Inferred
                               ? palette.mutedText
                               : palette.connector),
                 message.type == SequenceMessageType::Synchronous
                     ? QStringLiteral("block")
                     : QStringLiteral("open"),
                 message.type == SequenceMessageType::Synchronous
                     ? QStringLiteral("1")
                     : QStringLiteral("0"));
    if (message.type == SequenceMessageType::Return)
      style += QStringLiteral("dashed=1;dashPattern=8 5;");
    writer.edge(style, message.points, message.id);
    if (!message.text.isEmpty())
      writer.vertex(
          htmlLine(message.text),
          transparentTextStyle(message.confidence ==
                                       SequenceEvidenceConfidence::Inferred
                                   ? palette.mutedText
                                   : palette.bodyText),
          message.labelRect);
    if (message.type == SequenceMessageType::Destroy &&
        !message.points.isEmpty()) {
      const QPointF tip = message.points.constLast();
      writer.vertex(QStringLiteral("×"),
                    transparentTextStyle(palette.connector),
                    {tip.x() - 10.0, tip.y() - 10.0, 20.0, 20.0});
    }
  }
  for (const SequenceParticipantLayout &participant :
       scene.sequence.participants) {
    QString value =
        QStringLiteral("<div><b>%1</b></div>").arg(htmlLine(participant.name));
    if (!participant.qualifier.isEmpty())
      value += QStringLiteral("<div style=\"color:%1\">%2</div>")
                   .arg(colorName(palette.mutedText),
                        htmlLine(participant.qualifier));
    if (!participant.executionContextLabel.isEmpty())
      value += QStringLiteral("<div style=\"border-top:1px solid %1;color:%2\">"
                              "%3</div>")
                   .arg(colorName(palette.compartmentLine),
                        colorName(palette.mutedText),
                        htmlLine(participant.executionContextLabel));
    writer.vertex(
        value,
        QStringLiteral(
            "html=1;whiteSpace=wrap;overflow=hidden;rounded=0;fillColor=%1;"
            "strokeColor=%2;fontColor=%3;align=center;verticalAlign=middle;")
            .arg(colorName(palette.classFill), colorName(palette.nodeBorder),
                 colorName(palette.nodeTitleText)),
        participant.headerRect, participant.id);
  }
}

bool writeDrawio(const ExportScene &scene, const QString &path, qreal margin,
                 QSize &documentSize, QString &error) {
  if (!scene.contentBounds.isValid() || scene.contentBounds.isEmpty()) {
    error = QStringLiteral("Cannot export an empty diagram.");
    return false;
  }
  const qreal safeMargin = std::max(0.0, margin);
  const QRectF documentBounds = scene.contentBounds.adjusted(
      -safeMargin, -safeMargin, safeMargin, safeMargin);
  documentSize = {std::max(1, qCeil(documentBounds.width())),
                  std::max(1, qCeil(documentBounds.height()))};
  const QPointF offset = -documentBounds.topLeft();

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    error = QStringLiteral("Could not open Draw.io export for writing: %1")
                .arg(file.errorString());
    return false;
  }
  QXmlStreamWriter xml(&file);
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  xml.writeStartElement(QStringLiteral("mxfile"));
  xml.writeAttribute(QStringLiteral("host"), QStringLiteral("yauml"));
  xml.writeAttribute(QStringLiteral("agent"),
                     QStringLiteral("yauml diagram exporter"));
  xml.writeAttribute(QStringLiteral("compressed"), QStringLiteral("false"));
  xml.writeStartElement(QStringLiteral("diagram"));
  xml.writeAttribute(QStringLiteral("id"), QStringLiteral("yauml-export"));
  xml.writeAttribute(QStringLiteral("name"), scene.diagramName);
  xml.writeStartElement(QStringLiteral("mxGraphModel"));
  xml.writeAttribute(QStringLiteral("dx"), number(documentSize.width()));
  xml.writeAttribute(QStringLiteral("dy"), number(documentSize.height()));
  xml.writeAttribute(QStringLiteral("grid"), QStringLiteral("1"));
  xml.writeAttribute(QStringLiteral("gridSize"), QStringLiteral("10"));
  xml.writeAttribute(QStringLiteral("page"), QStringLiteral("0"));
  xml.writeAttribute(QStringLiteral("background"),
                     colorName(uiPalette().surface));
  xml.writeStartElement(QStringLiteral("root"));
  xml.writeEmptyElement(QStringLiteral("mxCell"));
  xml.writeAttribute(QStringLiteral("id"), QStringLiteral("0"));
  xml.writeEmptyElement(QStringLiteral("mxCell"));
  xml.writeAttribute(QStringLiteral("id"), QStringLiteral("1"));
  xml.writeAttribute(QStringLiteral("parent"), QStringLiteral("0"));
  DrawioWriter writer(xml, offset);

  if (scene.type == DiagramType::Sequence) {
    writeDrawioSequence(writer, scene);
  } else {
    QHash<QString, QString> cellByPresentation;
    QHash<QString, QRectF> rectByPresentation;
    for (const ExportContainer &container : scene.containers) {
      QString style =
          QStringLiteral(
              "html=1;whiteSpace=wrap;overflow=hidden;rounded=0;fillColor=%1;"
              "strokeColor=%2;fontColor=%3;align=left;verticalAlign=top;")
              .arg(colorName(container.style.fill),
                   colorName(container.style.border),
                   colorName(container.style.primaryText));
      if (container.package)
        style += QStringLiteral("shape=folder;tabWidth=90;tabHeight=24;");
      const QString cell = writer.vertex(drawioContainerHtml(container), style,
                                         container.rect, container.id);
      cellByPresentation.insert(container.id, cell);
      rectByPresentation.insert(container.id, container.rect);
    }
    for (const ExportNode &node : scene.nodes) {
      const QString cell = writeDrawioUmlNode(writer, node);
      cellByPresentation.insert(node.id, cell);
      rectByPresentation.insert(node.id, node.rect);
    }
    for (const ExportNoteAttachment &attachment : scene.noteAttachments) {
      writer.edge(QStringLiteral(
                      "edgeStyle=none;endArrow=none;dashed=1;dashPattern=8 5;"
                      "strokeColor=%1;strokeWidth=1.4;")
                      .arg(colorName(uiPalette().noteAttachment)),
                  {attachment.source, attachment.target}, attachment.id);
    }
    for (const ExportConnector &connector : scene.connectors) {
      QString style = drawioEdgeStyle(connector.type, uiPalette().connector);
      if (connector.points.size() >= 2) {
        style += drawioEndpointConstraint(
            QStringLiteral("exit"),
            rectByPresentation.value(connector.sourcePresentationId),
            connector.points.constFirst());
        style += drawioEndpointConstraint(
            QStringLiteral("entry"),
            rectByPresentation.value(connector.targetPresentationId),
            connector.points.constLast());
      }
      writer.edge(style, connector.points, connector.id,
                  cellByPresentation.value(connector.sourcePresentationId),
                  cellByPresentation.value(connector.targetPresentationId));
      for (const ExportAnnotation &annotation : connector.annotations)
        writer.vertex(htmlLine(annotation.text),
                      transparentTextStyle(uiPalette().bodyText),
                      annotation.rect);
    }
    for (const ExportNote &note : scene.notes) {
      writer.vertex(
          basicMarkdownHtml(note.markdown),
          QStringLiteral(
              "shape=note;html=1;whiteSpace=wrap;overflow=hidden;fillColor=%1;"
              "strokeColor=%2;fontColor=%3;align=left;verticalAlign=top;"
              "spacing=10;")
              .arg(colorName(uiPalette().noteFill),
                   colorName(uiPalette().noteBorder),
                   colorName(uiPalette().noteText)),
          note.rect, note.id);
    }
  }

  xml.writeEndElement(); // root
  xml.writeEndElement(); // mxGraphModel
  xml.writeEndElement(); // diagram
  xml.writeEndElement(); // mxfile
  xml.writeEndDocument();
  if (xml.hasError()) {
    error = QStringLiteral("Could not serialize the Draw.io document.");
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    error = QStringLiteral("Could not finish Draw.io export: %1")
                .arg(file.errorString());
    return false;
  }
  return true;
}

} // namespace

DiagramDocumentExportResult
exportDiagramDocument(const ProjectData &project, const Diagram &diagram,
                      const QString &path, DiagramDocumentFormat format,
                      const DiagramDocumentExportOptions &options) {
  DiagramDocumentExportResult result;
  if (diagram.type == DiagramType::Activity) {
    result.error = QStringLiteral(
        "Activity diagrams currently support PNG and PDF export.");
    return result;
  }
  if (path.trimmed().isEmpty()) {
    result.error = QStringLiteral("A local export path is required.");
    return result;
  }
  const QFileInfo output(path);
  if (!output.absoluteDir().exists()) {
    result.error =
        QStringLiteral("The export destination folder does not exist: %1")
            .arg(output.absolutePath());
    return result;
  }

  const ExportScene scene = buildScene(project, diagram, options);
  result.success = format == DiagramDocumentFormat::Svg
                       ? writeSvg(scene, path, options.margin,
                                  result.documentSize, result.error)
                       : writeDrawio(scene, path, options.margin,
                                     result.documentSize, result.error);
  return result;
}

} // namespace yauml::ui
