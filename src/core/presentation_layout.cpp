#include "core/presentation_layout.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace uuml::presentation_layout {
namespace {

QStringList bodyLines(const ModelElement &element) {
  if (element.type == ElementType::Enumeration)
    return element.enumLiterals;
  QStringList lines = element.attributes;
  lines.append(element.operations);
  return lines;
}

qreal fallbackTextWidth(const QString &text, bool bold) {
  // Headless validation and app-less unit tests have no font database. This
  // conservative approximation keeps their geometry deterministic; the GUI
  // path below uses the exact application font.
  qreal width = 0.0;
  for (const QChar character : text) {
    if (character.isSpace())
      width += 4.0;
    else if (QStringLiteral("ilI|!.,:'`").contains(character))
      width += 4.5;
    else if (QStringLiteral("MW@#%&").contains(character))
      width += 10.0;
    else if (character.isUpper())
      width += 8.0;
    else
      width += 7.0;
  }
  return width * (bold ? 1.06 : 1.0);
}

qreal applicationTextWidth(const QString &text, bool bold) {
  if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
    QFont font = QGuiApplication::font();
    font.setBold(bold);
    return QFontMetricsF(font).horizontalAdvance(text);
  }
  return fallbackTextWidth(text, bold);
}

QRectF presentationGeometry(const Diagram &diagram,
                            const QString &presentationId) {
  if (const auto *node = findNode(diagram, presentationId))
    return node->geometry;
  if (const auto *container = findContainer(diagram, presentationId))
    return container->geometry;
  return {};
}

QRectF childPresentationBounds(
    const Diagram &diagram, const ContainerPresentation &container,
    bool *hasContent) {
  QRectF bounds;
  *hasContent = false;
  for (const QString &childId : container.childPresentationIds) {
    const QRectF childGeometry = presentationGeometry(diagram, childId);
    if (!childGeometry.isValid())
      continue;
    bounds = *hasContent ? bounds.united(childGeometry) : childGeometry;
    *hasContent = true;
  }
  return bounds;
}

QString fullyQualifiedElementName(const ProjectData &project,
                                  const ModelElement &element,
                                  QSet<QString> &visited) {
  if (element.packageId.isEmpty() || visited.contains(element.id))
    return element.name;
  visited.insert(element.id);

  const auto *package = findElement(project, element.packageId);
  if (!package)
    return element.name;
  const QString packageName =
      fullyQualifiedElementName(project, *package, visited);
  if (packageName.isEmpty())
    return element.name;

  const QStringList packageParts =
      packageName.split(QStringLiteral("::"), Qt::SkipEmptyParts);
  const QStringList elementParts =
      element.name.split(QStringLiteral("::"), Qt::SkipEmptyParts);
  int overlap = std::min(packageParts.size(), elementParts.size());
  while (overlap > 0) {
    bool matches = true;
    for (int index = 0; index < overlap; ++index) {
      if (packageParts.at(packageParts.size() - overlap + index) !=
          elementParts.at(index)) {
        matches = false;
        break;
      }
    }
    if (matches)
      break;
    --overlap;
  }

  QStringList qualifiedParts = packageParts;
  qualifiedParts.append(elementParts.mid(overlap));
  return qualifiedParts.join(QStringLiteral("::"));
}

} // namespace

QSizeF nodeContentSize(const ModelElement &element) {
  qreal widestLine = applicationTextWidth(element.name, true);
  const QStringList lines = bodyLines(element);
  for (const QString &line : lines)
    widestLine = std::max(widestLine, applicationTextWidth(line, false));

  const qreal width =
      std::max(kMinimumNodeWidth,
               std::ceil(widestLine + 2.0 * kNodeTextPadding + 8.0));
  const qreal height = std::max(
      kMinimumNodeHeight,
      std::ceil(kNodeHeaderHeight + lines.size() * kNodeLineHeight +
                kNodeBottomPadding));
  return {width, height};
}

QSizeF nodePlacementSize(const ModelElement &element,
                         const QString &sizingMode) {
  return sizingMode.trimmed().compare(QStringLiteral("fixed"),
                                      Qt::CaseInsensitive) == 0
             ? QSizeF(kFixedNodeWidth, kFixedNodeHeight)
             : nodeContentSize(element);
}

QString fullyQualifiedElementName(const ProjectData &project,
                                  const ModelElement &element) {
  QSet<QString> visited;
  return fullyQualifiedElementName(project, element, visited);
}

QString elementDisplayNameInPackage(const ProjectData &project,
                                    const ModelElement &element,
                                    const QString &packageElementId) {
  if (packageElementId.isEmpty())
    return element.name;
  const auto *package = findElement(project, packageElementId);
  if (!package)
    return element.name;

  const QString packageName = fullyQualifiedElementName(project, *package);
  const QString qualifiedName = fullyQualifiedElementName(project, element);
  const QString prefix = packageName + QStringLiteral("::");
  return qualifiedName.startsWith(prefix) ? qualifiedName.mid(prefix.size())
                                          : element.name;
}

QString containerDisplayName(const ProjectData &project,
                             const ContainerPresentation &container) {
  if (container.subjectKind == QStringLiteral("folder")) {
    const auto *folder = findBrowserFolder(project, container.subjectId);
    return folder ? folder->name : QString{};
  }
  if (container.subjectKind == QStringLiteral("package")) {
    const auto *package = findElement(project, container.subjectId);
    return package ? fullyQualifiedElementName(project, *package) : QString{};
  }
  return {};
}

qreal containerTitleWidth(const ProjectData &project,
                          const ContainerPresentation &container) {
  return std::ceil(applicationTextWidth(
                       containerDisplayName(project, container), true) +
                   2.0 * kNodeTextPadding + 8.0);
}

QRectF containerContentGeometry(const ProjectData &project,
                                const Diagram &diagram,
                                const ContainerPresentation &container) {
  bool hasContent = false;
  const QRectF contentBounds =
      childPresentationBounds(diagram, container, &hasContent);

  const qreal minimumWidth =
      std::max(kMinimumContainerWidth, containerTitleWidth(project, container));
  if (!hasContent)
    return {container.geometry.topLeft(),
            QSizeF(minimumWidth, kMinimumContainerHeight)};

  QRectF fitted =
      contentBounds.adjusted(-kContainerHorizontalPadding,
                             -kContainerTopPadding,
                             kContainerHorizontalPadding,
                             kContainerBottomPadding);
  if (fitted.width() < minimumWidth)
    fitted.setWidth(minimumWidth);
  if (fitted.height() < kMinimumContainerHeight)
    fitted.setHeight(kMinimumContainerHeight);
  return fitted;
}

} // namespace uuml::presentation_layout
