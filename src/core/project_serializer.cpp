#include "core/project_serializer.h"

#include "core/json5.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace uuml {
namespace {

constexpr auto kManifestName = "manifest.json5";
constexpr auto kModelName = "model/model.json5";
constexpr auto kDiagramsName = "diagrams/diagrams.json5";
constexpr auto kRecoveryDirectory = ".uuml-recovery";
constexpr auto kRecoveryMarker = "pending";

Diagnostic error(const QString &category, const QString &message,
                 const QString &elementId = {}) {
  return {DiagnosticSeverity::Error, category, message, elementId};
}

Diagnostic warning(const QString &category, const QString &message,
                   const QString &elementId = {}) {
  return {DiagnosticSeverity::Warning, category, message, elementId};
}

QByteArray readFile(const QString &path, QString &readError) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    readError = file.errorString();
    return {};
  }
  return file.readAll();
}

bool writeFile(const QString &path, const QByteArray &bytes,
               QString &writeError) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    writeError = file.errorString();
    return false;
  }
  if (file.write(bytes) != bytes.size()) {
    writeError = file.errorString();
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    writeError = file.errorString();
    return false;
  }
  return true;
}

QJsonObject withoutKeys(QJsonObject object,
                        std::initializer_list<QString> keys) {
  for (const auto &key : keys)
    object.remove(key);
  return object;
}

QJsonArray stringArray(const QStringList &values) {
  QJsonArray array;
  for (const auto &value : values)
    array.append(value);
  return array;
}

QStringList readStringArray(const QJsonValue &value) {
  QStringList result;
  for (const auto &item : value.toArray()) {
    if (item.isString())
      result.append(item.toString());
  }
  return result;
}

QJsonObject anchorToJson(const ConnectorAnchor &anchor) {
  QJsonObject object = anchor.extra;
  object.insert(QStringLiteral("side"), toString(anchor.side));
  object.insert(QStringLiteral("offset"), anchor.offset);
  return object;
}

ConnectorAnchor readAnchor(const QJsonValue &value, const QString &name,
                           const QString &connectorId,
                           QList<Diagnostic> &diagnostics) {
  ConnectorAnchor anchor;
  if (value.isUndefined() || value.isNull())
    return anchor;
  if (!value.isObject()) {
    diagnostics.append(
        error(QStringLiteral("validation"),
              QStringLiteral("Connector %1 anchor must be an object").arg(name),
              connectorId));
    return anchor;
  }

  const QJsonObject object = value.toObject();
  bool sideOk = false;
  anchor.side = connectorSideFromString(
      object.value(QStringLiteral("side")).toString(), &sideOk);
  if (!sideOk)
    diagnostics.append(error(
        QStringLiteral("validation"),
        QStringLiteral("Connector %1 anchor has an unknown side").arg(name),
        connectorId));
  anchor.offset = object.value(QStringLiteral("offset")).toDouble(0.5);
  anchor.extra =
      withoutKeys(object, {QStringLiteral("side"), QStringLiteral("offset")});
  return anchor;
}

QJsonObject bendPointToJson(const ConnectorBendPoint &bendPoint) {
  QJsonObject object = bendPoint.extra;
  object.insert(QStringLiteral("x"), bendPoint.position.x());
  object.insert(QStringLiteral("y"), bendPoint.position.y());
  return object;
}

ConnectorBendPoint readBendPoint(const QJsonValue &value,
                                 const QString &connectorId,
                                 QList<Diagnostic> &diagnostics) {
  ConnectorBendPoint bendPoint;
  const qreal invalidCoordinate = std::numeric_limits<qreal>::quiet_NaN();
  if (!value.isObject()) {
    diagnostics.append(error(QStringLiteral("validation"),
                             QStringLiteral("Connector bend point must be an "
                                            "object"),
                             connectorId));
    bendPoint.position = {invalidCoordinate, invalidCoordinate};
    return bendPoint;
  }

  const QJsonObject object = value.toObject();
  bendPoint.position = {
      object.value(QStringLiteral("x")).toDouble(invalidCoordinate),
      object.value(QStringLiteral("y")).toDouble(invalidCoordinate)};
  bendPoint.extra =
      withoutKeys(object, {QStringLiteral("x"), QStringLiteral("y")});
  return bendPoint;
}

QJsonObject elementToJson(const ModelElement &element) {
  QJsonObject object = element.extra;
  object.insert(QStringLiteral("id"), element.id);
  object.insert(QStringLiteral("type"), toString(element.type));
  object.insert(QStringLiteral("name"), element.name);
  if (!element.packageId.isEmpty())
    object.insert(QStringLiteral("packageId"), element.packageId);
  else
    object.remove(QStringLiteral("packageId"));
  object.insert(QStringLiteral("attributes"), stringArray(element.attributes));
  object.insert(QStringLiteral("operations"), stringArray(element.operations));
  object.insert(QStringLiteral("enumLiterals"),
                stringArray(element.enumLiterals));
  return object;
}

QJsonObject relationshipToJson(const Relationship &relationship) {
  QJsonObject object = relationship.extra;
  object.insert(QStringLiteral("id"), relationship.id);
  object.insert(QStringLiteral("type"), toString(relationship.type));
  object.insert(QStringLiteral("name"), relationship.name);
  object.insert(QStringLiteral("sourceId"), relationship.sourceId);
  object.insert(QStringLiteral("targetId"), relationship.targetId);
  return object;
}

QJsonObject nodeToJson(const NodePresentation &node) {
  QJsonObject object = node.extra;
  object.insert(QStringLiteral("id"), node.id);
  object.insert(QStringLiteral("elementId"), node.elementId);
  QJsonObject geometry;
  geometry.insert(QStringLiteral("x"), node.geometry.x());
  geometry.insert(QStringLiteral("y"), node.geometry.y());
  geometry.insert(QStringLiteral("width"), node.geometry.width());
  geometry.insert(QStringLiteral("height"), node.geometry.height());
  object.insert(QStringLiteral("geometry"), geometry);
  return object;
}

QJsonObject connectorToJson(const ConnectorPresentation &connector) {
  QJsonObject object = connector.extra;
  object.insert(QStringLiteral("id"), connector.id);
  object.insert(QStringLiteral("relationshipId"), connector.relationshipId);
  if (connector.sourceAnchor.side != ConnectorSide::Automatic ||
      !connector.sourceAnchor.extra.isEmpty())
    object.insert(QStringLiteral("sourceAnchor"),
                  anchorToJson(connector.sourceAnchor));
  else
    object.remove(QStringLiteral("sourceAnchor"));
  if (connector.targetAnchor.side != ConnectorSide::Automatic ||
      !connector.targetAnchor.extra.isEmpty())
    object.insert(QStringLiteral("targetAnchor"),
                  anchorToJson(connector.targetAnchor));
  else
    object.remove(QStringLiteral("targetAnchor"));
  if (!connector.bendPoints.isEmpty()) {
    QJsonArray bendPoints;
    for (const auto &bendPoint : connector.bendPoints)
      bendPoints.append(bendPointToJson(bendPoint));
    object.insert(QStringLiteral("bendPoints"), bendPoints);
  } else {
    object.remove(QStringLiteral("bendPoints"));
  }
  return object;
}

QByteArray manifestBytes(const ProjectData &project) {
  QJsonObject object = project.manifestExtra;
  object.insert(QStringLiteral("schemaVersion"), project.schemaVersion);
  object.insert(QStringLiteral("id"), project.id);
  object.insert(QStringLiteral("name"), project.name);
  object.insert(QStringLiteral("model"), QString::fromLatin1(kModelName));
  object.insert(QStringLiteral("diagrams"), QString::fromLatin1(kDiagramsName));
  return Json5::serialize(QJsonDocument(object));
}

QByteArray modelBytes(const ProjectData &project) {
  QJsonObject object = project.modelExtra;
  QJsonArray elements;
  for (const auto &element : project.elements)
    elements.append(elementToJson(element));
  QJsonArray relationships;
  for (const auto &relationship : project.relationships)
    relationships.append(relationshipToJson(relationship));
  object.insert(QStringLiteral("elements"), elements);
  object.insert(QStringLiteral("relationships"), relationships);
  return Json5::serialize(QJsonDocument(object));
}

QByteArray diagramsBytes(const ProjectData &project) {
  QJsonObject root = project.diagramsExtra;
  QJsonArray diagrams;
  for (const auto &diagram : project.diagrams) {
    QJsonObject object = diagram.extra;
    object.insert(QStringLiteral("id"), diagram.id);
    object.insert(QStringLiteral("name"), diagram.name);
    QJsonArray nodes;
    for (const auto &node : diagram.nodes)
      nodes.append(nodeToJson(node));
    QJsonArray connectors;
    for (const auto &connector : diagram.connectors)
      connectors.append(connectorToJson(connector));
    object.insert(QStringLiteral("nodes"), nodes);
    object.insert(QStringLiteral("connectors"), connectors);
    diagrams.append(object);
  }
  root.insert(QStringLiteral("diagrams"), diagrams);
  return Json5::serialize(QJsonDocument(root));
}

bool recoverIfPending(const QString &root, QString &message) {
  const QDir project(root);
  const QString recovery =
      project.filePath(QString::fromLatin1(kRecoveryDirectory));
  const QString marker =
      QDir(recovery).filePath(QString::fromLatin1(kRecoveryMarker));
  if (!QFileInfo::exists(marker))
    return false;

  const QList<QPair<QString, QString>> files = {
      {QDir(recovery).filePath(QStringLiteral("manifest.json5")),
       project.filePath(QString::fromLatin1(kManifestName))},
      {QDir(recovery).filePath(QStringLiteral("model.json5")),
       project.filePath(QString::fromLatin1(kModelName))},
      {QDir(recovery).filePath(QStringLiteral("diagrams.json5")),
       project.filePath(QString::fromLatin1(kDiagramsName))}};

  for (const auto &[backup, target] : files) {
    QString ioError;
    const QByteArray bytes = readFile(backup, ioError);
    if (!ioError.isEmpty() || !writeFile(target, bytes, ioError)) {
      message =
          QStringLiteral("Recovery failed for %1: %2").arg(target, ioError);
      return false;
    }
  }
  QFile::remove(marker);
  QDir(recovery).removeRecursively();
  message = QStringLiteral(
      "Recovered the last valid project after an interrupted save");
  return true;
}

bool prepareRecovery(const QString &root, QString &message) {
  const QDir project(root);
  const QString manifest = project.filePath(QString::fromLatin1(kManifestName));
  const QString model = project.filePath(QString::fromLatin1(kModelName));
  const QString diagrams = project.filePath(QString::fromLatin1(kDiagramsName));
  if (!QFileInfo::exists(manifest) || !QFileInfo::exists(model) ||
      !QFileInfo::exists(diagrams))
    return true;

  const QString recovery =
      project.filePath(QString::fromLatin1(kRecoveryDirectory));
  QDir recoveryDir(recovery);
  if (recoveryDir.exists())
    recoveryDir.removeRecursively();
  QDir().mkpath(recovery);

  const QList<QPair<QString, QString>> files = {
      {manifest, QDir(recovery).filePath(QStringLiteral("manifest.json5"))},
      {model, QDir(recovery).filePath(QStringLiteral("model.json5"))},
      {diagrams, QDir(recovery).filePath(QStringLiteral("diagrams.json5"))}};
  for (const auto &[source, backup] : files) {
    QString ioError;
    const QByteArray bytes = readFile(source, ioError);
    if (!ioError.isEmpty() || !writeFile(backup, bytes, ioError)) {
      message =
          QStringLiteral("Could not prepare recovery data: %1").arg(ioError);
      return false;
    }
  }
  QString ioError;
  if (!writeFile(QDir(recovery).filePath(QString::fromLatin1(kRecoveryMarker)),
                 QByteArrayLiteral("pending\n"), ioError)) {
    message =
        QStringLiteral("Could not create recovery marker: %1").arg(ioError);
    return false;
  }
  return true;
}

void finishRecovery(const QString &root) {
  QDir(QDir(root).filePath(QString::fromLatin1(kRecoveryDirectory)))
      .removeRecursively();
}

Json5Result readJson5(const QString &path, QList<Diagnostic> &diagnostics) {
  QString ioError;
  const QByteArray bytes = readFile(path, ioError);
  if (!ioError.isEmpty()) {
    diagnostics.append(
        error(QStringLiteral("persistence"),
              QStringLiteral("Cannot read %1: %2").arg(path, ioError)));
    return {};
  }
  auto result = Json5::parse(bytes);
  if (!result) {
    diagnostics.append(
        error(QStringLiteral("persistence"),
              QStringLiteral("Cannot parse %1: %2").arg(path, result.error)));
  }
  return result;
}

} // namespace

QString ProjectSerializer::normalizeProjectPath(const QString &path) {
  QFileInfo info(path);
  if (info.fileName().compare(QString::fromLatin1(kManifestName),
                              Qt::CaseInsensitive) == 0)
    return info.absolutePath();
  return QDir::cleanPath(info.absoluteFilePath());
}

LoadOutcome ProjectSerializer::load(const QString &projectPath) {
  LoadOutcome outcome;
  const QString root = normalizeProjectPath(projectPath);
  QString recoveryMessage;
  if (QFileInfo::exists(
          QDir(root).filePath(QString::fromLatin1(kRecoveryDirectory) + u'/' +
                              QString::fromLatin1(kRecoveryMarker)))) {
    outcome.recovered = recoverIfPending(root, recoveryMessage);
    if (!outcome.recovered) {
      outcome.diagnostics.append(
          error(QStringLiteral("recovery"), recoveryMessage));
      return outcome;
    }
    outcome.diagnostics.append(
        warning(QStringLiteral("recovery"), recoveryMessage));
  }

  const QString manifestPath =
      QDir(root).filePath(QString::fromLatin1(kManifestName));
  const auto manifestResult = readJson5(manifestPath, outcome.diagnostics);
  if (!manifestResult || !manifestResult.document.isObject())
    return outcome;
  const QJsonObject manifest = manifestResult.document.object();

  const QString modelRelative = manifest.value(QStringLiteral("model"))
                                    .toString(QString::fromLatin1(kModelName));
  const QString diagramsRelative =
      manifest.value(QStringLiteral("diagrams"))
          .toString(QString::fromLatin1(kDiagramsName));
  const auto modelResult =
      readJson5(QDir(root).filePath(modelRelative), outcome.diagnostics);
  const auto diagramsResult =
      readJson5(QDir(root).filePath(diagramsRelative), outcome.diagnostics);
  if (!modelResult || !modelResult.document.isObject() || !diagramsResult ||
      !diagramsResult.document.isObject())
    return outcome;

  ProjectData project;
  project.schemaVersion =
      manifest.value(QStringLiteral("schemaVersion")).toInt();
  project.id = manifest.value(QStringLiteral("id")).toString();
  project.name = manifest.value(QStringLiteral("name")).toString();
  project.manifestExtra = withoutKeys(
      manifest, {QStringLiteral("schemaVersion"), QStringLiteral("id"),
                 QStringLiteral("name"), QStringLiteral("model"),
                 QStringLiteral("diagrams")});

  const QJsonObject model = modelResult.document.object();
  project.modelExtra = withoutKeys(
      model, {QStringLiteral("elements"), QStringLiteral("relationships")});
  for (const auto &value : model.value(QStringLiteral("elements")).toArray()) {
    const QJsonObject object = value.toObject();
    ModelElement element;
    element.id = object.value(QStringLiteral("id")).toString();
    bool typeOk = false;
    element.type = elementTypeFromString(
        object.value(QStringLiteral("type")).toString(), &typeOk);
    if (!typeOk)
      outcome.diagnostics.append(error(QStringLiteral("validation"),
                                       QStringLiteral("Unknown element type"),
                                       element.id));
    element.name = object.value(QStringLiteral("name")).toString();
    element.packageId = object.value(QStringLiteral("packageId")).toString();
    element.attributes =
        readStringArray(object.value(QStringLiteral("attributes")));
    element.operations =
        readStringArray(object.value(QStringLiteral("operations")));
    element.enumLiterals =
        readStringArray(object.value(QStringLiteral("enumLiterals")));
    element.extra = withoutKeys(
        object,
        {QStringLiteral("id"), QStringLiteral("type"), QStringLiteral("name"),
         QStringLiteral("packageId"), QStringLiteral("attributes"),
         QStringLiteral("operations"), QStringLiteral("enumLiterals")});
    project.elements.append(element);
  }

  for (const auto &value :
       model.value(QStringLiteral("relationships")).toArray()) {
    const QJsonObject object = value.toObject();
    Relationship relationship;
    relationship.id = object.value(QStringLiteral("id")).toString();
    bool typeOk = false;
    relationship.type = relationshipTypeFromString(
        object.value(QStringLiteral("type")).toString(), &typeOk);
    if (!typeOk)
      outcome.diagnostics.append(
          error(QStringLiteral("validation"),
                QStringLiteral("Unknown relationship type"), relationship.id));
    relationship.name = object.value(QStringLiteral("name")).toString();
    relationship.sourceId = object.value(QStringLiteral("sourceId")).toString();
    relationship.targetId = object.value(QStringLiteral("targetId")).toString();
    relationship.extra =
        withoutKeys(object, {QStringLiteral("id"), QStringLiteral("type"),
                             QStringLiteral("name"), QStringLiteral("sourceId"),
                             QStringLiteral("targetId")});
    project.relationships.append(relationship);
  }

  const QJsonObject diagramRoot = diagramsResult.document.object();
  project.diagramsExtra =
      withoutKeys(diagramRoot, {QStringLiteral("diagrams")});
  for (const auto &value :
       diagramRoot.value(QStringLiteral("diagrams")).toArray()) {
    const QJsonObject object = value.toObject();
    Diagram diagram;
    diagram.id = object.value(QStringLiteral("id")).toString();
    diagram.name = object.value(QStringLiteral("name")).toString();
    for (const auto &nodeValue :
         object.value(QStringLiteral("nodes")).toArray()) {
      const QJsonObject nodeObject = nodeValue.toObject();
      NodePresentation node;
      node.id = nodeObject.value(QStringLiteral("id")).toString();
      node.elementId = nodeObject.value(QStringLiteral("elementId")).toString();
      const QJsonObject geometry =
          nodeObject.value(QStringLiteral("geometry")).toObject();
      node.geometry = {geometry.value(QStringLiteral("x")).toDouble(),
                       geometry.value(QStringLiteral("y")).toDouble(),
                       geometry.value(QStringLiteral("width")).toDouble(),
                       geometry.value(QStringLiteral("height")).toDouble()};
      node.extra = withoutKeys(nodeObject, {QStringLiteral("id"),
                                            QStringLiteral("elementId"),
                                            QStringLiteral("geometry")});
      diagram.nodes.append(node);
    }
    for (const auto &connectorValue :
         object.value(QStringLiteral("connectors")).toArray()) {
      const QJsonObject connectorObject = connectorValue.toObject();
      ConnectorPresentation connector;
      connector.id = connectorObject.value(QStringLiteral("id")).toString();
      connector.relationshipId =
          connectorObject.value(QStringLiteral("relationshipId")).toString();
      connector.sourceAnchor = readAnchor(
          connectorObject.value(QStringLiteral("sourceAnchor")),
          QStringLiteral("source"), connector.id, outcome.diagnostics);
      connector.targetAnchor = readAnchor(
          connectorObject.value(QStringLiteral("targetAnchor")),
          QStringLiteral("target"), connector.id, outcome.diagnostics);
      const QJsonValue bendPointsValue =
          connectorObject.value(QStringLiteral("bendPoints"));
      if (!bendPointsValue.isUndefined() && !bendPointsValue.isArray()) {
        outcome.diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("Connector bendPoints must be an array"),
                  connector.id));
      } else {
        for (const auto &bendPointValue : bendPointsValue.toArray())
          connector.bendPoints.append(
              readBendPoint(bendPointValue, connector.id, outcome.diagnostics));
      }
      connector.extra = withoutKeys(
          connectorObject,
          {QStringLiteral("id"), QStringLiteral("relationshipId"),
           QStringLiteral("sourceAnchor"), QStringLiteral("targetAnchor"),
           QStringLiteral("bendPoints")});
      diagram.connectors.append(connector);
    }
    diagram.extra = withoutKeys(
        object, {QStringLiteral("id"), QStringLiteral("name"),
                 QStringLiteral("nodes"), QStringLiteral("connectors")});
    project.diagrams.append(diagram);
  }

  project.loadedFromCommentedJson5 = manifestResult.hadComments ||
                                     modelResult.hadComments ||
                                     diagramsResult.hadComments;
  outcome.diagnostics.append(validate(project));
  outcome.project = project;
  outcome.ok =
      std::none_of(outcome.diagnostics.cbegin(), outcome.diagnostics.cend(),
                   [](const Diagnostic &item) {
                     return item.severity == DiagnosticSeverity::Error;
                   });
  return outcome;
}

SaveOutcome ProjectSerializer::save(const QString &projectPath,
                                    const ProjectData &project) {
  SaveOutcome outcome;
  outcome.diagnostics = validate(project);
  if (std::any_of(outcome.diagnostics.cbegin(), outcome.diagnostics.cend(),
                  [](const Diagnostic &item) {
                    return item.severity == DiagnosticSeverity::Error;
                  }))
    return outcome;

  if (project.loadedFromCommentedJson5) {
    outcome.diagnostics.append(
        error(QStringLiteral("persistence"),
              QStringLiteral(
                  "Saving was refused because JSON5 comments would be lost. "
                  "Remove the comments or use a comment-preserving editor.")));
    return outcome;
  }

  const QString root = normalizeProjectPath(projectPath);
  QDir().mkpath(root);
  const QString pendingMarker =
      QDir(root).filePath(QString::fromLatin1(kRecoveryDirectory) + u'/' +
                          QString::fromLatin1(kRecoveryMarker));
  if (QFileInfo::exists(pendingMarker)) {
    QString recoveryMessage;
    if (!recoverIfPending(root, recoveryMessage)) {
      outcome.diagnostics.append(
          error(QStringLiteral("recovery"), recoveryMessage));
      return outcome;
    }
    outcome.diagnostics.append(
        warning(QStringLiteral("recovery"), recoveryMessage));
  }
  const QList<QPair<QString, QByteArray>> files = {
      {QDir(root).filePath(QString::fromLatin1(kManifestName)),
       manifestBytes(project)},
      {QDir(root).filePath(QString::fromLatin1(kModelName)),
       modelBytes(project)},
      {QDir(root).filePath(QString::fromLatin1(kDiagramsName)),
       diagramsBytes(project)}};

  bool allSame = true;
  for (const auto &[path, bytes] : files) {
    QString ioError;
    const QByteArray existing = readFile(path, ioError);
    if (!ioError.isEmpty() || existing != bytes) {
      allSame = false;
      break;
    }
  }
  if (allSame) {
    outcome.ok = true;
    outcome.unchanged = true;
    return outcome;
  }

  QString recoveryError;
  if (!prepareRecovery(root, recoveryError)) {
    outcome.diagnostics.append(
        error(QStringLiteral("recovery"), recoveryError));
    return outcome;
  }

  for (const auto &[path, bytes] : files) {
    QString ioError;
    if (!writeFile(path, bytes, ioError)) {
      outcome.diagnostics.append(
          error(QStringLiteral("persistence"),
                QStringLiteral("Cannot write %1: %2").arg(path, ioError)));
      return outcome;
    }
  }
  finishRecovery(root);
  outcome.ok = true;
  return outcome;
}

QList<Diagnostic> ProjectSerializer::validate(const ProjectData &project) {
  QList<Diagnostic> diagnostics;
  if (project.schemaVersion != 1)
    diagnostics.append(error(QStringLiteral("validation"),
                             QStringLiteral("Unsupported schema version %1")
                                 .arg(project.schemaVersion)));
  if (project.id.isEmpty())
    diagnostics.append(error(QStringLiteral("validation"),
                             QStringLiteral("Project ID is missing")));
  if (project.name.trimmed().isEmpty())
    diagnostics.append(error(QStringLiteral("validation"),
                             QStringLiteral("Project name is empty")));
  if (project.diagrams.isEmpty())
    diagnostics.append(
        error(QStringLiteral("validation"),
              QStringLiteral("The project has no class diagrams")));

  QSet<QString> ids;
  auto checkId = [&](const QString &id, const QString &kind) {
    if (id.isEmpty()) {
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("A %1 has no ID").arg(kind)));
    } else if (ids.contains(id)) {
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("Duplicate stable ID %1").arg(id),
                               id));
    } else {
      ids.insert(id);
    }
  };

  QSet<QString> elementIds;
  for (const auto &element : project.elements) {
    checkId(element.id, QStringLiteral("model element"));
    elementIds.insert(element.id);
    if (element.name.trimmed().isEmpty())
      diagnostics.append(error(
          QStringLiteral("validation"),
          QStringLiteral("A model element has an empty name"), element.id));
    if (!element.packageId.isEmpty() &&
        !findElement(project, element.packageId))
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("Package reference %1 is broken")
                                   .arg(element.packageId),
                               element.id));
  }

  QSet<QString> relationshipIds;
  for (const auto &relationship : project.relationships) {
    checkId(relationship.id, QStringLiteral("relationship"));
    relationshipIds.insert(relationship.id);
    if (!elementIds.contains(relationship.sourceId))
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("Relationship source is missing"),
                               relationship.id));
    if (!elementIds.contains(relationship.targetId))
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("Relationship target is missing"),
                               relationship.id));
  }

  for (const auto &diagram : project.diagrams) {
    checkId(diagram.id, QStringLiteral("diagram"));
    if (diagram.name.trimmed().isEmpty())
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("A diagram has an empty name"),
                               diagram.id));
    QSet<QString> presentedElements;
    for (const auto &node : diagram.nodes) {
      checkId(node.id, QStringLiteral("node presentation"));
      if (!elementIds.contains(node.elementId))
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("Node presentation references a missing element"),
            node.id));
      if (presentedElements.contains(node.elementId))
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("An element appears twice on the same diagram"),
            node.id));
      presentedElements.insert(node.elementId);
      if (node.geometry.width() <= 0 || node.geometry.height() <= 0)
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("Node presentation has invalid geometry"), node.id));
    }
    for (const auto &connector : diagram.connectors) {
      checkId(connector.id, QStringLiteral("connector presentation"));
      if (!relationshipIds.contains(connector.relationshipId))
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("Connector references a missing relationship"),
                  connector.id));
      const auto checkAnchor = [&](const ConnectorAnchor &anchor,
                                   const QString &name) {
        if (anchor.side != ConnectorSide::Automatic &&
            (anchor.offset < 0.0 || anchor.offset > 1.0))
          diagnostics.append(
              error(QStringLiteral("validation"),
                    QStringLiteral(
                        "Connector %1 anchor offset must be between 0 and 1")
                        .arg(name),
                    connector.id));
      };
      checkAnchor(connector.sourceAnchor, QStringLiteral("source"));
      checkAnchor(connector.targetAnchor, QStringLiteral("target"));
      for (const auto &bendPoint : connector.bendPoints) {
        if (!std::isfinite(bendPoint.position.x()) ||
            !std::isfinite(bendPoint.position.y()))
          diagnostics.append(
              error(QStringLiteral("validation"),
                    QStringLiteral("Connector bend point coordinates must be "
                                   "finite"),
                    connector.id));
      }
    }
  }
  return diagnostics;
}

} // namespace uuml
