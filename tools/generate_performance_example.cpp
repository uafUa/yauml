#include "core/model_operation.h"
#include "core/project_data.h"
#include "core/project_serializer.h"

#include <QCoreApplication>
#include <QTextStream>
#include <algorithm>

namespace {

QString deterministicId(quint64 value) {
  return QStringLiteral("019b0000-0000-7000-8000-%1")
      .arg(value, 12, 10, QLatin1Char('0'));
}

yauml::ProjectData createPerformanceProject(int elementCount) {
  constexpr int columns = 30;
  constexpr qreal horizontalSpacing = 250.0;
  constexpr qreal verticalSpacing = 160.0;

  yauml::ProjectData project;
  project.id = deterministicId(1);
  project.name =
      QStringLiteral("Performance Example — %1 classes").arg(elementCount);

  yauml::Diagram diagram;
  diagram.id = deterministicId(2);
  diagram.name =
      QStringLiteral("%1-node performance overview").arg(elementCount);

  for (int index = 0; index < elementCount; ++index) {
    yauml::ModelElement element;
    element.id = deterministicId(1000 + index);
    element.type = yauml::ElementType::Class;
    element.name =
        QStringLiteral("Component%1").arg(index + 1, 3, 10, QLatin1Char('0'));
    element.attributes = {QStringLiteral("+ value%1: int").arg(index + 1),
                          QStringLiteral("- state: State")};
    element.operations = {yauml::modelOperationFromSignature(
        QStringLiteral("+ update(input: Data): Result"),
        element.id + QStringLiteral(":operation:0"))};
    project.elements.append(element);

    yauml::NodePresentation node;
    node.id = deterministicId(10000 + index);
    node.elementId = element.id;
    const int column = index % columns;
    const int row = index / columns;
    node.geometry = QRectF(50.0 + column * horizontalSpacing,
                           50.0 + row * verticalSpacing, 205.0, 125.0);
    diagram.nodes.append(node);
  }

  quint64 relationshipNumber = 0;
  auto connect = [&](int sourceIndex, int targetIndex) {
    yauml::Relationship relationship;
    relationship.id = deterministicId(20000 + relationshipNumber);
    relationship.type = yauml::RelationshipType::Dependency;
    relationship.name = QStringLiteral("uses");
    relationship.sourceId = project.elements.at(sourceIndex).id;
    relationship.targetId = project.elements.at(targetIndex).id;
    project.relationships.append(relationship);

    yauml::ConnectorPresentation connector;
    connector.id = deterministicId(40000 + relationshipNumber);
    connector.relationshipId = relationship.id;
    diagram.connectors.append(connector);
    ++relationshipNumber;
  };

  for (int index = 0; index < elementCount; ++index) {
    const int column = index % columns;
    if (column + 1 < columns && index + 1 < elementCount)
      connect(index, index + 1);
    if (index + columns < elementCount)
      connect(index, index + columns);
  }

  project.diagrams.append(diagram);
  return project;
}

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  QTextStream out(stdout);
  QTextStream err(stderr);
  const QStringList arguments = application.arguments();
  if (arguments.size() < 2) {
    err << "Usage: yauml_generate_performance_example <project-directory> "
           "[element-count]\n";
    return 64;
  }

  bool countOk = true;
  int count = arguments.size() >= 3 ? arguments.at(2).toInt(&countOk) : 600;
  if (!countOk || count < 100 || count > 5000) {
    err << "Element count must be between 100 and 5000\n";
    return 64;
  }

  const auto project = createPerformanceProject(count);
  const auto outcome = yauml::ProjectSerializer::save(arguments.at(1), project);
  for (const auto &diagnostic : outcome.diagnostics) {
    QTextStream &stream =
        diagnostic.severity == yauml::DiagnosticSeverity::Error ? err : out;
    stream << yauml::toString(diagnostic.severity).toUpper() << " ["
           << diagnostic.category << "] " << diagnostic.message << '\n';
  }
  if (!outcome.ok)
    return 2;

  out << "Generated " << project.elements.size() << " nodes and "
      << project.relationships.size() << " connectors in " << arguments.at(1)
      << '\n';
  return 0;
}
