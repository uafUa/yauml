#include "core/project_style.h"

#include <QSet>

namespace yauml::project_style {
namespace {

struct Subject {
  QString kind;
  QString id;

  bool isEmpty() const { return kind.isEmpty() || id.isEmpty(); }
  QString key() const { return kind + u':' + id; }
};

Subject browserParentSubject(const BrowserParent &parent) {
  return {parent.kind, parent.id};
}

Subject effectiveParent(const ProjectData &project, const Subject &subject) {
  if (subject.kind == QStringLiteral("folder")) {
    const auto *folder = findBrowserFolder(project, subject.id);
    return folder ? browserParentSubject(folder->parent) : Subject{};
  }

  if (subject.kind == QStringLiteral("namespace")) {
    const int separator = subject.id.lastIndexOf(QStringLiteral("::"));
    if (separator < 0)
      return {};
    const QString parentPath = subject.id.left(separator);
    for (const auto &element : project.elements)
      if (element.name == parentPath)
        return {QStringLiteral("element"), element.id};
    return {QStringLiteral("namespace"), parentPath};
  }

  if (subject.kind != QStringLiteral("element") &&
      subject.kind != QStringLiteral("package"))
    return {};
  const auto *element = findElement(project, subject.id);
  if (!element)
    return {};
  if (!element->browserParent.kind.isEmpty())
    return browserParentSubject(element->browserParent);
  if (!element->enclosingTypeId.isEmpty())
    return {QStringLiteral("element"), element->enclosingTypeId};
  if (!element->packageId.isEmpty())
    return {QStringLiteral("element"), element->packageId};

  // Backward-compatible fallback for older imported projects whose qualified
  // names predate materialized package elements.
  const int separator = element->name.lastIndexOf(QStringLiteral("::"));
  if (separator < 0)
    return {};
  const QString parentPath = element->name.left(separator);
  for (const auto &candidate : project.elements)
    if (candidate.id != element->id && candidate.name == parentPath)
      return {QStringLiteral("element"), candidate.id};
  return {QStringLiteral("namespace"), parentPath};
}

} // namespace

QString explicitStyleId(const ProjectData &project, const QString &kind,
                        const QString &id) {
  if (kind == QStringLiteral("element") || kind == QStringLiteral("package")) {
    const auto *element = findElement(project, id);
    return element ? element->styleId : QString{};
  }
  if (kind == QStringLiteral("folder")) {
    const auto *folder = findBrowserFolder(project, id);
    return folder ? folder->styleId : QString{};
  }
  if (kind == QStringLiteral("namespace"))
    return project.namespaceStyleIds.value(id);
  return {};
}

const DiagramStyle *effectiveStyleForSubject(const ProjectData &project,
                                             const QString &kind,
                                             const QString &id) {
  QSet<QString> visited;
  Subject current{kind, id};
  while (!current.isEmpty() && !visited.contains(current.key())) {
    visited.insert(current.key());
    const QString styleId = explicitStyleId(project, current.kind, current.id);
    if (const auto *style = findDiagramStyle(project, styleId))
      return style;
    current = effectiveParent(project, current);
  }
  return nullptr;
}

const DiagramStyle *effectiveStyleForNode(const ProjectData &project,
                                          const NodePresentation &node) {
  if (const auto *style = findDiagramStyle(project, node.styleId))
    return style;
  return effectiveStyleForSubject(project, QStringLiteral("element"),
                                  node.elementId);
}

const DiagramStyle *
effectiveStyleForContainer(const ProjectData &project,
                           const ContainerPresentation &container) {
  if (const auto *style = findDiagramStyle(project, container.styleId))
    return style;
  return effectiveStyleForSubject(project, container.subjectKind,
                                  container.subjectId);
}

} // namespace yauml::project_style
