#include "core/stereotype_catalog.h"

#include <algorithm>

namespace uuml::stereotype_catalog {
namespace {

StereotypeDefinition defaultDefinition(QString id, QString name,
                                       QStringList applicableTo) {
  StereotypeDefinition definition;
  definition.id = std::move(id);
  definition.name = std::move(name);
  definition.applicableTo = std::move(applicableTo);
  return definition;
}

} // namespace

const QList<StereotypeDefinition> &sourceVisibilityDefinitions() {
  static const QStringList typeApplicability{QStringLiteral("class"),
                                             QStringLiteral("struct"),
                                             QStringLiteral("enumeration")};
  static const QList<StereotypeDefinition> definitions = {
      defaultDefinition(kLocalStereotypeId, QStringLiteral("local"),
                        typeApplicability),
      defaultDefinition(kPrivateStereotypeId, QStringLiteral("private"),
                        typeApplicability),
      defaultDefinition(kApiStereotypeId, QStringLiteral("api"),
                        typeApplicability)};
  return definitions;
}

const QList<StereotypeDefinition> &defaultDefinitions() {
  static const QList<StereotypeDefinition> definitions = [] {
    QList<StereotypeDefinition> result = {
        defaultDefinition(QStringLiteral("uml.interface"),
                          QStringLiteral("interface"),
                          {QStringLiteral("class"), QStringLiteral("struct")}),
        defaultDefinition(QStringLiteral("uml.abstract"),
                          QStringLiteral("abstract"),
                          {QStringLiteral("class"), QStringLiteral("struct")}),
        defaultDefinition(QStringLiteral("uml.utility"),
                          QStringLiteral("utility"),
                          {QStringLiteral("class"), QStringLiteral("struct")}),
        defaultDefinition(QStringLiteral("uml.entity"),
                          QStringLiteral("entity"),
                          {QStringLiteral("class"), QStringLiteral("struct")}),
        defaultDefinition(QStringLiteral("uml.control"),
                          QStringLiteral("control"),
                          {QStringLiteral("class"), QStringLiteral("struct")}),
        defaultDefinition(QStringLiteral("uml.boundary"),
                          QStringLiteral("boundary"),
                          {QStringLiteral("class"), QStringLiteral("struct")}),
        defaultDefinition(QStringLiteral("uml.service"),
                          QStringLiteral("service"),
                          {QStringLiteral("class"), QStringLiteral("struct")}),
        defaultDefinition(QStringLiteral("uml.datatype"),
                          QStringLiteral("dataType"),
                          {QStringLiteral("class"), QStringLiteral("struct"),
                           QStringLiteral("enumeration")})};
    result.append(sourceVisibilityDefinitions());
    result.append(
        {defaultDefinition(QStringLiteral("uml.subsystem"),
                           QStringLiteral("subsystem"),
                           {QStringLiteral("package")}),
         defaultDefinition(QStringLiteral("uml.trace"), QStringLiteral("trace"),
                           {kRelationshipApplicability}),
         defaultDefinition(QStringLiteral("uml.refine"),
                           QStringLiteral("refine"),
                           {kRelationshipApplicability}),
         defaultDefinition(QStringLiteral("uml.derive"),
                           QStringLiteral("derive"),
                           {kRelationshipApplicability})});
    return result;
  }();
  return definitions;
}

const StereotypeDefinition *find(const ProjectData &project,
                                 const QString &stereotypeId) {
  return findStereotypeDefinition(project, stereotypeId);
}

const StereotypeDefinition *
findByConventionalIdOrName(const ProjectData &project,
                           const QString &stereotypeId,
                           const QString &conventionalName) {
  if (const auto *definition = find(project, stereotypeId))
    return definition;
  const auto match =
      std::find_if(project.stereotypeDefinitions.cbegin(),
                   project.stereotypeDefinitions.cend(),
                   [&](const StereotypeDefinition &definition) {
                     return definition.name.compare(conventionalName,
                                                    Qt::CaseInsensitive) == 0;
                   });
  return match == project.stereotypeDefinitions.cend() ? nullptr : &*match;
}

QString applicabilityFor(ElementType type) { return toString(type); }

bool appliesTo(const StereotypeDefinition &definition,
               const QString &applicability) {
  return definition.applicableTo.contains(applicability);
}

QString displayName(const ProjectData &project, const QString &stereotypeId) {
  const auto *definition = find(project, stereotypeId);
  return definition ? definition->name : stereotypeId;
}

QString displayText(const ProjectData &project,
                    const QStringList &stereotypeIds) {
  QStringList names;
  names.reserve(stereotypeIds.size());
  for (const QString &id : stereotypeIds) {
    const QString name = displayName(project, id).trimmed();
    if (!name.isEmpty())
      names.append(name);
  }
  return names.isEmpty()
             ? QString{}
             : QStringLiteral("«%1»").arg(names.join(QStringLiteral(", ")));
}

} // namespace uuml::stereotype_catalog
