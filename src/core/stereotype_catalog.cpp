#include "core/stereotype_catalog.h"

#include <algorithm>

namespace uuml::stereotype_catalog {
namespace {

StereotypeDefinition common(QString id, QString name,
                            QStringList applicableTo) {
  StereotypeDefinition definition;
  definition.id = std::move(id);
  definition.name = std::move(name);
  definition.applicableTo = std::move(applicableTo);
  return definition;
}

} // namespace

const QList<StereotypeDefinition> &commonDefinitions() {
  static const QList<StereotypeDefinition> definitions = {
      common(QStringLiteral("uml.interface"), QStringLiteral("interface"),
             {QStringLiteral("class"), QStringLiteral("struct")}),
      common(QStringLiteral("uml.abstract"), QStringLiteral("abstract"),
             {QStringLiteral("class"), QStringLiteral("struct")}),
      common(QStringLiteral("uml.utility"), QStringLiteral("utility"),
             {QStringLiteral("class"), QStringLiteral("struct")}),
      common(QStringLiteral("uml.entity"), QStringLiteral("entity"),
             {QStringLiteral("class"), QStringLiteral("struct")}),
      common(QStringLiteral("uml.control"), QStringLiteral("control"),
             {QStringLiteral("class"), QStringLiteral("struct")}),
      common(QStringLiteral("uml.boundary"), QStringLiteral("boundary"),
             {QStringLiteral("class"), QStringLiteral("struct")}),
      common(QStringLiteral("uml.service"), QStringLiteral("service"),
             {QStringLiteral("class"), QStringLiteral("struct")}),
      common(QStringLiteral("uml.datatype"), QStringLiteral("dataType"),
             {QStringLiteral("class"), QStringLiteral("struct"),
              QStringLiteral("enumeration")}),
      common(QStringLiteral("uml.subsystem"), QStringLiteral("subsystem"),
             {QStringLiteral("package")}),
      common(QStringLiteral("uml.trace"), QStringLiteral("trace"),
             {kRelationshipApplicability}),
      common(QStringLiteral("uml.refine"), QStringLiteral("refine"),
             {kRelationshipApplicability}),
      common(QStringLiteral("uml.derive"), QStringLiteral("derive"),
             {kRelationshipApplicability})};
  return definitions;
}

bool isCommon(const QString &stereotypeId) {
  return std::any_of(commonDefinitions().cbegin(), commonDefinitions().cend(),
                     [&](const StereotypeDefinition &definition) {
                       return definition.id == stereotypeId;
                     });
}

const StereotypeDefinition *find(const ProjectData &project,
                                 const QString &stereotypeId) {
  const auto common =
      std::find_if(commonDefinitions().cbegin(), commonDefinitions().cend(),
                   [&](const StereotypeDefinition &definition) {
                     return definition.id == stereotypeId;
                   });
  if (common != commonDefinitions().cend())
    return &*common;
  return findStereotypeDefinition(project, stereotypeId);
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
