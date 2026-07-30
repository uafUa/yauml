#include "core/project_serializer.h"

#include "core/connector_port_layout.h"
#include "core/diagram_filter.h"
#include "core/json5.h"
#include "core/model_operation.h"
#include "core/project_schema.h"
#include "core/project_schema_version.h"
#include "core/project_serializer_test_support.h"
#include "core/stereotype_catalog.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace yauml {
namespace {

constexpr auto kManifestName = "manifest.json5";
constexpr auto kModelName = "model/model.json5";
constexpr auto kDiagramsName = "diagrams/diagrams.json5";
constexpr auto kRecoveryDirectory = ".yauml-recovery";
constexpr auto kPreviousRecoveryDirectory = "."
                                            "u"
                                            "uml-recovery";
constexpr auto kRecoveryMarker = "pending";
#ifdef Q_OS_WIN
constexpr auto kPathCaseSensitivity = Qt::CaseInsensitive;
#else
constexpr auto kPathCaseSensitivity = Qt::CaseSensitive;
#endif

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

QByteArray contentDigest(const QByteArray &bytes) {
  return QByteArrayLiteral("sha256:") +
         QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QByteArray currentFileDigest(const QString &path) {
  if (!QFileInfo::exists(path))
    return QByteArrayLiteral("missing");
  QString readError;
  const QByteArray bytes = readFile(path, readError);
  return readError.isEmpty() ? contentDigest(bytes)
                             : QByteArrayLiteral("unreadable");
}

QString revisionKey(const QString &root, const QString &path) {
  return QDir::fromNativeSeparators(
      QDir::cleanPath(QDir(root).relativeFilePath(path)));
}

bool sameProjectRoot(const QString &left, const QString &right) {
  return QDir::cleanPath(left).compare(QDir::cleanPath(right),
                                       kPathCaseSensitivity) == 0;
}

void addCurrentRevisionEntry(ProjectFileRevision &revision,
                             const QString &relativePath) {
  const QString normalized =
      QDir::fromNativeSeparators(QDir::cleanPath(relativePath));
  if (!revision.fileDigests.contains(normalized)) {
    revision.fileDigests.insert(
        normalized,
        currentFileDigest(QDir(revision.rootPath).filePath(normalized)));
  }
}

ProjectFileRevision
savedRevision(const QString &root,
              const QList<QPair<QString, QByteArray>> &files) {
  ProjectFileRevision revision;
  revision.rootPath = QDir::cleanPath(root);
  for (const auto &[path, bytes] : files)
    revision.fileDigests.insert(revisionKey(root, path), contentDigest(bytes));
  return revision;
}

QStringList externallyChangedFiles(
    const QString &root, const ProjectFileRevision &expectedRevision,
    const QMap<QString, QByteArray> &knownCurrentDigests = {}) {
  if (!expectedRevision.isValid() ||
      !sameProjectRoot(root, expectedRevision.rootPath))
    return {};

  QStringList changed;
  for (auto file = expectedRevision.fileDigests.cbegin();
       file != expectedRevision.fileDigests.cend(); ++file) {
    const auto known = knownCurrentDigests.constFind(file.key());
    const QByteArray current =
        known != knownCurrentDigests.cend()
            ? known.value()
            : currentFileDigest(QDir(root).filePath(file.key()));
    if (current != file.value())
      changed.append(file.key());
  }
  return changed;
}

bool writeFile(const QString &path, const QByteArray &bytes,
               test_support::ProjectWritePurpose purpose, QString &writeError) {
  using test_support::ProjectWriteBoundary;
  using test_support::ProjectWriteStage;

  const auto injectFault = [&](ProjectWriteStage stage) {
    if (!test_support::detail::shouldInjectProjectWriteFault(
            ProjectWriteBoundary{purpose, stage, path}))
      return false;
    writeError = QStringLiteral("Injected persistence fault for %1").arg(path);
    return true;
  };

  QDir().mkpath(QFileInfo(path).absolutePath());
  if (injectFault(ProjectWriteStage::Open))
    return false;
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    writeError = file.errorString();
    return false;
  }
  if (injectFault(ProjectWriteStage::Write)) {
    file.cancelWriting();
    return false;
  }
  if (file.write(bytes) != bytes.size()) {
    writeError = file.errorString();
    file.cancelWriting();
    return false;
  }
  if (injectFault(ProjectWriteStage::Commit)) {
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

QJsonObject filterToJson(const DiagramFilter &filter) {
  QJsonObject object = filter.extra;
  const auto writeArray = [&](const QString &key, const QStringList &values) {
    if (values.isEmpty())
      object.remove(key);
    else
      object.insert(key, stringArray(values));
  };
  const auto writeString = [&](const QString &key, const QString &value) {
    if (value.isEmpty())
      object.remove(key);
    else
      object.insert(key, value);
  };
  writeArray(QStringLiteral("excludedElementTypes"),
             filter.excludedElementTypes);
  writeArray(QStringLiteral("includedStereotypeIds"),
             filter.includedStereotypeIds);
  writeArray(QStringLiteral("excludedStereotypeIds"),
             filter.excludedStereotypeIds);
  writeString(QStringLiteral("namePattern"), filter.namePattern);
  writeString(QStringLiteral("memberPattern"), filter.memberPattern);
  if (filter.excludeNameMatches)
    object.insert(QStringLiteral("excludeNameMatches"), true);
  else
    object.remove(QStringLiteral("excludeNameMatches"));
  if (filter.excludeMemberMatches)
    object.insert(QStringLiteral("excludeMemberMatches"), true);
  else
    object.remove(QStringLiteral("excludeMemberMatches"));
  return object;
}

DiagramFilter readDiagramFilter(const QJsonValue &value,
                                const QString &diagramId,
                                QList<Diagnostic> &diagnostics) {
  DiagramFilter filter;
  if (value.isUndefined() || value.isNull())
    return filter;
  if (!value.isObject()) {
    diagnostics.append(error(QStringLiteral("validation"),
                             QStringLiteral("Diagram filter must be an object"),
                             diagramId));
    return filter;
  }

  const QJsonObject object = value.toObject();
  const auto readArray = [&](const QString &key) {
    const QJsonValue field = object.value(key);
    if (field.isUndefined())
      return QStringList{};
    if (!field.isArray()) {
      diagnostics.append(
          error(QStringLiteral("validation"),
                QStringLiteral("Diagram filter %1 must be an array").arg(key),
                diagramId));
      return QStringList{};
    }
    return readStringArray(field);
  };
  const auto readString = [&](const QString &key) {
    const QJsonValue field = object.value(key);
    if (field.isUndefined())
      return QString{};
    if (!field.isString()) {
      diagnostics.append(
          error(QStringLiteral("validation"),
                QStringLiteral("Diagram filter %1 must be text").arg(key),
                diagramId));
      return QString{};
    }
    return field.toString().trimmed();
  };
  const auto readBoolean = [&](const QString &key) {
    const QJsonValue field = object.value(key);
    if (field.isUndefined())
      return false;
    if (!field.isBool()) {
      diagnostics.append(
          error(QStringLiteral("validation"),
                QStringLiteral("Diagram filter %1 must be a boolean").arg(key),
                diagramId));
      return false;
    }
    return field.toBool();
  };

  filter.excludedElementTypes =
      readArray(QStringLiteral("excludedElementTypes"));
  filter.includedStereotypeIds =
      readArray(QStringLiteral("includedStereotypeIds"));
  filter.excludedStereotypeIds =
      readArray(QStringLiteral("excludedStereotypeIds"));
  filter.namePattern = readString(QStringLiteral("namePattern"));
  filter.memberPattern = readString(QStringLiteral("memberPattern"));
  filter.excludeNameMatches = readBoolean(QStringLiteral("excludeNameMatches"));
  filter.excludeMemberMatches =
      readBoolean(QStringLiteral("excludeMemberMatches"));
  filter.extra = withoutKeys(object, {QStringLiteral("excludedElementTypes"),
                                      QStringLiteral("includedStereotypeIds"),
                                      QStringLiteral("excludedStereotypeIds"),
                                      QStringLiteral("namePattern"),
                                      QStringLiteral("excludeNameMatches"),
                                      QStringLiteral("memberPattern"),
                                      QStringLiteral("excludeMemberMatches")});
  return filter;
}

int readPortSnapPointCount(const QJsonObject &object, const QString &key,
                           const QString &nodeId,
                           QList<Diagnostic> &diagnostics) {
  const QJsonValue value = object.value(key);
  if (value.isUndefined())
    return connector_ports::kDefaultSnapPointCount;

  const qreal numericValue = value.toDouble(0.0);
  const bool representable =
      std::isfinite(numericValue) && numericValue >= 0.0 &&
      numericValue <= connector_ports::kMaximumSnapPointCount;
  const int count = representable ? static_cast<int>(numericValue)
                                  : connector_ports::kDefaultSnapPointCount;
  const bool valid = value.isDouble() && representable &&
                     numericValue == count &&
                     connector_ports::isValidSnapPointCount(count);
  if (!valid) {
    diagnostics.append(
        error(QStringLiteral("validation"),
              QStringLiteral(
                  "Node presentation %1 must be an odd number between 1 and %2")
                  .arg(key)
                  .arg(connector_ports::kMaximumSnapPointCount),
              nodeId));
  }
  return valid ? count : connector_ports::kDefaultSnapPointCount;
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

QJsonObject browserParentToJson(const BrowserParent &parent) {
  QJsonObject object;
  object.insert(QStringLiteral("kind"), parent.kind);
  if (!parent.id.isEmpty())
    object.insert(QStringLiteral("id"), parent.id);
  return object;
}

BrowserParent readBrowserParent(const QJsonValue &value) {
  const QJsonObject object = value.toObject();
  return {object.value(QStringLiteral("kind")).toString(),
          object.value(QStringLiteral("id")).toString()};
}

QJsonObject diagramStyleToJson(const DiagramStyle &style) {
  QJsonObject object = style.extra;
  object.insert(QStringLiteral("id"), style.id);
  object.insert(QStringLiteral("name"), style.name);
  object.insert(QStringLiteral("fill"), style.fill);
  object.insert(QStringLiteral("headerFill"), style.headerFill);
  object.insert(QStringLiteral("border"), style.border);
  object.insert(QStringLiteral("primaryText"), style.primaryText);
  object.insert(QStringLiteral("secondaryText"), style.secondaryText);
  object.insert(QStringLiteral("divider"), style.divider);
  return object;
}

QJsonObject stereotypeDefinitionToJson(const StereotypeDefinition &definition) {
  QJsonObject object = definition.extra;
  object.insert(QStringLiteral("id"), definition.id);
  object.insert(QStringLiteral("name"), definition.name);
  object.insert(QStringLiteral("applicableTo"),
                stringArray(definition.applicableTo));
  return object;
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
  if (!element.enclosingTypeId.isEmpty())
    object.insert(QStringLiteral("enclosingTypeId"), element.enclosingTypeId);
  else
    object.remove(QStringLiteral("enclosingTypeId"));
  object.insert(QStringLiteral("attributes"), stringArray(element.attributes));
  object.insert(QStringLiteral("operations"),
                modelOperationsToJson(element.operations));
  object.insert(QStringLiteral("enumLiterals"),
                stringArray(element.enumLiterals));
  if (!element.browserParent.kind.isEmpty())
    object.insert(QStringLiteral("browserParent"),
                  browserParentToJson(element.browserParent));
  else
    object.remove(QStringLiteral("browserParent"));
  if (!element.styleId.isEmpty())
    object.insert(QStringLiteral("styleId"), element.styleId);
  else
    object.remove(QStringLiteral("styleId"));
  if (!element.stereotypeIds.isEmpty())
    object.insert(QStringLiteral("stereotypeIds"),
                  stringArray(element.stereotypeIds));
  else
    object.remove(QStringLiteral("stereotypeIds"));
  return object;
}

QJsonObject browserFolderToJson(const BrowserFolder &folder) {
  QJsonObject object = folder.extra;
  object.insert(QStringLiteral("id"), folder.id);
  object.insert(QStringLiteral("name"), folder.name);
  object.insert(QStringLiteral("parent"), browserParentToJson(folder.parent));
  if (!folder.styleId.isEmpty())
    object.insert(QStringLiteral("styleId"), folder.styleId);
  else
    object.remove(QStringLiteral("styleId"));
  return object;
}

QJsonObject relationshipEndToJson(const RelationshipEnd &end) {
  QJsonObject object = end.extra;
  if (!end.role.isEmpty())
    object.insert(QStringLiteral("role"), end.role);
  else
    object.remove(QStringLiteral("role"));
  if (!end.multiplicity.isEmpty())
    object.insert(QStringLiteral("multiplicity"), end.multiplicity);
  else
    object.remove(QStringLiteral("multiplicity"));
  return object;
}

RelationshipEnd relationshipEndFromJson(const QJsonValue &value,
                                        const QString &endName,
                                        const QString &relationshipId,
                                        QList<Diagnostic> &diagnostics) {
  RelationshipEnd end;
  if (value.isUndefined())
    return end;
  if (!value.isObject()) {
    diagnostics.append(
        error(QStringLiteral("validation"),
              QStringLiteral("Relationship %1 must be an object").arg(endName),
              relationshipId));
    return end;
  }
  const QJsonObject object = value.toObject();
  const auto readOptionalText = [&](const QString &key) {
    const QJsonValue text = object.value(key);
    if (!text.isUndefined() && !text.isString())
      diagnostics.append(
          error(QStringLiteral("validation"),
                QStringLiteral("Relationship %1 %2 must be a string")
                    .arg(endName, key),
                relationshipId));
    return text.toString();
  };
  end.role = readOptionalText(QStringLiteral("role"));
  end.multiplicity = readOptionalText(QStringLiteral("multiplicity"));
  end.extra = withoutKeys(
      object, {QStringLiteral("role"), QStringLiteral("multiplicity")});
  return end;
}

QJsonObject relationshipToJson(const Relationship &relationship) {
  QJsonObject object = relationship.extra;
  object.insert(QStringLiteral("id"), relationship.id);
  object.insert(QStringLiteral("type"), toString(relationship.type));
  object.insert(QStringLiteral("name"), relationship.name);
  object.insert(QStringLiteral("sourceId"), relationship.sourceId);
  object.insert(QStringLiteral("targetId"), relationship.targetId);
  const QJsonObject sourceEnd = relationshipEndToJson(relationship.sourceEnd);
  if (!sourceEnd.isEmpty())
    object.insert(QStringLiteral("sourceEnd"), sourceEnd);
  else
    object.remove(QStringLiteral("sourceEnd"));
  const QJsonObject targetEnd = relationshipEndToJson(relationship.targetEnd);
  if (!targetEnd.isEmpty())
    object.insert(QStringLiteral("targetEnd"), targetEnd);
  else
    object.remove(QStringLiteral("targetEnd"));
  if (!relationship.stereotypeIds.isEmpty())
    object.insert(QStringLiteral("stereotypeIds"),
                  stringArray(relationship.stereotypeIds));
  else
    object.remove(QStringLiteral("stereotypeIds"));
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
  if (node.horizontalPortSnapPoints != connector_ports::kDefaultSnapPointCount)
    object.insert(QStringLiteral("horizontalPortSnapPoints"),
                  node.horizontalPortSnapPoints);
  else
    object.remove(QStringLiteral("horizontalPortSnapPoints"));
  if (node.verticalPortSnapPoints != connector_ports::kDefaultSnapPointCount)
    object.insert(QStringLiteral("verticalPortSnapPoints"),
                  node.verticalPortSnapPoints);
  else
    object.remove(QStringLiteral("verticalPortSnapPoints"));
  if (node.showAttributes)
    object.insert(QStringLiteral("showAttributes"), *node.showAttributes);
  else
    object.remove(QStringLiteral("showAttributes"));
  if (node.showOperations)
    object.insert(QStringLiteral("showOperations"), *node.showOperations);
  else
    object.remove(QStringLiteral("showOperations"));
  if (node.operationSignatureMode) {
    object.insert(QStringLiteral("operationSignatureMode"),
                  toString(*node.operationSignatureMode));
  } else {
    object.remove(QStringLiteral("operationSignatureMode"));
  }
  if (!node.styleId.isEmpty())
    object.insert(QStringLiteral("styleId"), node.styleId);
  else
    object.remove(QStringLiteral("styleId"));
  return object;
}

QJsonObject containerToJson(const ContainerPresentation &container) {
  QJsonObject object = container.extra;
  object.insert(QStringLiteral("id"), container.id);
  object.insert(QStringLiteral("subjectKind"), container.subjectKind);
  object.insert(QStringLiteral("subjectId"), container.subjectId);
  QJsonObject geometry;
  geometry.insert(QStringLiteral("x"), container.geometry.x());
  geometry.insert(QStringLiteral("y"), container.geometry.y());
  geometry.insert(QStringLiteral("width"), container.geometry.width());
  geometry.insert(QStringLiteral("height"), container.geometry.height());
  object.insert(QStringLiteral("geometry"), geometry);
  object.insert(QStringLiteral("childPresentationIds"),
                stringArray(container.childPresentationIds));
  if (!container.styleId.isEmpty())
    object.insert(QStringLiteral("styleId"), container.styleId);
  else
    object.remove(QStringLiteral("styleId"));
  return object;
}

QJsonObject
annotationPlacementToJson(const ConnectorAnnotationPlacement &placement) {
  QJsonObject object = placement.extra;
  object.insert(QStringLiteral("routePosition"), placement.routePosition);
  if (!qFuzzyIsNull(placement.tangentOffset))
    object.insert(QStringLiteral("tangentOffset"), placement.tangentOffset);
  else
    object.remove(QStringLiteral("tangentOffset"));
  if (!qFuzzyIsNull(placement.normalOffset))
    object.insert(QStringLiteral("normalOffset"), placement.normalOffset);
  else
    object.remove(QStringLiteral("normalOffset"));
  return object;
}

ConnectorAnnotationPlacement
readAnnotationPlacement(const QJsonValue &value, const QString &connectorId,
                        const QString &annotationKey,
                        QList<Diagnostic> &diagnostics) {
  ConnectorAnnotationPlacement placement;
  const qreal invalidCoordinate = std::numeric_limits<qreal>::quiet_NaN();
  if (!value.isObject()) {
    diagnostics.append(error(
        QStringLiteral("validation"),
        QStringLiteral("Connector annotation placement %1 must be an object")
            .arg(annotationKey),
        connectorId));
    placement.routePosition = invalidCoordinate;
    return placement;
  }
  const QJsonObject object = value.toObject();
  placement.routePosition =
      object.value(QStringLiteral("routePosition")).toDouble(invalidCoordinate);
  placement.tangentOffset =
      object.value(QStringLiteral("tangentOffset")).toDouble(0.0);
  placement.normalOffset =
      object.value(QStringLiteral("normalOffset")).toDouble(0.0);
  placement.extra = withoutKeys(object, {QStringLiteral("routePosition"),
                                         QStringLiteral("tangentOffset"),
                                         QStringLiteral("normalOffset")});
  return placement;
}

bool relationshipAnnotationHasText(const Relationship &relationship,
                                   const QString &key) {
  if (key == QStringLiteral("name"))
    return !relationship.name.isEmpty();
  if (key == QStringLiteral("stereotype"))
    return !relationship.stereotypeIds.isEmpty();
  if (key == QStringLiteral("sourceRole"))
    return !relationship.sourceEnd.role.isEmpty();
  if (key == QStringLiteral("sourceMultiplicity"))
    return !relationship.sourceEnd.multiplicity.isEmpty();
  if (key == QStringLiteral("targetRole"))
    return !relationship.targetEnd.role.isEmpty();
  if (key == QStringLiteral("targetMultiplicity"))
    return !relationship.targetEnd.multiplicity.isEmpty();
  // Preserve extensions produced by newer versions even though this version
  // cannot determine whether their semantic annotation is visible.
  return true;
}

QJsonObject connectorToJson(const ConnectorPresentation &connector,
                            const Relationship *relationship) {
  QJsonObject object = connector.extra;
  object.insert(QStringLiteral("id"), connector.id);
  object.insert(QStringLiteral("relationshipId"), connector.relationshipId);
  if (connector.routing != ConnectorRouting::Straight)
    object.insert(QStringLiteral("routing"), toString(connector.routing));
  else
    object.remove(QStringLiteral("routing"));
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
  QJsonObject annotationPlacements;
  QStringList annotationKeys = connector.annotationPlacements.keys();
  std::sort(annotationKeys.begin(), annotationKeys.end());
  for (const QString &key : annotationKeys) {
    if (!relationship || relationshipAnnotationHasText(*relationship, key))
      annotationPlacements.insert(
          key,
          annotationPlacementToJson(connector.annotationPlacements.value(key)));
  }
  if (!annotationPlacements.isEmpty())
    object.insert(QStringLiteral("annotationPlacements"), annotationPlacements);
  else
    object.remove(QStringLiteral("annotationPlacements"));
  return object;
}

QByteArray manifestBytes(const ProjectData &project) {
  QJsonObject object = project.manifestExtra;
  object.insert(QStringLiteral("schemaVersion"), project.schemaVersion);
  object.insert(QStringLiteral("id"), project.id);
  object.insert(QStringLiteral("name"), project.name);
  object.insert(QStringLiteral("model"), QString::fromLatin1(kModelName));
  object.insert(QStringLiteral("diagrams"), QString::fromLatin1(kDiagramsName));
  QJsonObject cppImport = project.cppImport.extra;
  cppImport.remove(QStringLiteral("sourceRoot"));
  if (!project.cppImport.sourceRoots.isEmpty())
    cppImport.insert(QStringLiteral("sourceRoots"),
                     QJsonArray::fromStringList(project.cppImport.sourceRoots));
  else
    cppImport.remove(QStringLiteral("sourceRoots"));
  if (!cppImport.isEmpty())
    object.insert(QStringLiteral("cppImport"), cppImport);
  else
    object.remove(QStringLiteral("cppImport"));
  return Json5::serialize(QJsonDocument(object));
}

QByteArray modelBytes(const ProjectData &project) {
  QJsonObject object = project.modelExtra;
  QJsonArray styles;
  for (const auto &style : project.diagramStyles)
    styles.append(diagramStyleToJson(style));
  QJsonArray stereotypes;
  for (const auto &definition : project.stereotypeDefinitions)
    stereotypes.append(stereotypeDefinitionToJson(definition));
  QJsonArray elements;
  for (const auto &element : project.elements)
    elements.append(elementToJson(element));
  QJsonArray browserFolders;
  for (const auto &folder : project.browserFolders)
    browserFolders.append(browserFolderToJson(folder));
  QJsonArray relationships;
  for (const auto &relationship : project.relationships)
    relationships.append(relationshipToJson(relationship));
  if (!styles.isEmpty())
    object.insert(QStringLiteral("styles"), styles);
  else
    object.remove(QStringLiteral("styles"));
  // Schema 2 distinguishes an intentionally empty project catalog from a
  // schema-1 project that still needs the conventional UML defaults seeded.
  object.insert(QStringLiteral("stereotypes"), stereotypes);
  if (!project.namespaceStyleIds.isEmpty()) {
    QJsonObject namespaceStyles;
    QStringList paths = project.namespaceStyleIds.keys();
    std::sort(paths.begin(), paths.end());
    for (const QString &path : paths)
      namespaceStyles.insert(path, project.namespaceStyleIds.value(path));
    object.insert(QStringLiteral("namespaceStyles"), namespaceStyles);
  } else {
    object.remove(QStringLiteral("namespaceStyles"));
  }
  object.insert(QStringLiteral("elements"), elements);
  if (!browserFolders.isEmpty())
    object.insert(QStringLiteral("browserFolders"), browserFolders);
  else
    object.remove(QStringLiteral("browserFolders"));
  if (!project.browserItemOrder.isEmpty())
    object.insert(QStringLiteral("browserOrder"),
                  stringArray(project.browserItemOrder));
  else
    object.remove(QStringLiteral("browserOrder"));
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
    if (!diagram.showAttributes)
      object.insert(QStringLiteral("showAttributes"), false);
    else
      object.remove(QStringLiteral("showAttributes"));
    if (!diagram.showOperations)
      object.insert(QStringLiteral("showOperations"), false);
    else
      object.remove(QStringLiteral("showOperations"));
    if (diagram.operationSignatureMode != OperationSignatureMode::Full) {
      object.insert(QStringLiteral("operationSignatureMode"),
                    toString(diagram.operationSignatureMode));
    } else {
      object.remove(QStringLiteral("operationSignatureMode"));
    }
    const QJsonObject filter = filterToJson(diagram.filter);
    if (diagram_filter::isActive(diagram.filter) || !filter.isEmpty())
      object.insert(QStringLiteral("filter"), filter);
    else
      object.remove(QStringLiteral("filter"));
    QJsonArray containers;
    for (const auto &container : diagram.containers)
      containers.append(containerToJson(container));
    QJsonArray nodes;
    for (const auto &node : diagram.nodes)
      nodes.append(nodeToJson(node));
    QJsonArray connectors;
    for (const auto &connector : diagram.connectors)
      connectors.append(connectorToJson(
          connector, findRelationship(project, connector.relationshipId)));
    if (!containers.isEmpty())
      object.insert(QStringLiteral("containers"), containers);
    else
      object.remove(QStringLiteral("containers"));
    object.insert(QStringLiteral("nodes"), nodes);
    object.insert(QStringLiteral("connectors"), connectors);
    diagrams.append(object);
  }
  root.insert(QStringLiteral("diagrams"), diagrams);
  return Json5::serialize(QJsonDocument(root));
}

bool recoverIfPending(const QString &root, const QString &recoveryDirectory,
                      QString &message) {
  const QDir project(root);
  const QString recovery = project.filePath(recoveryDirectory);
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

  // Validate the complete recovery set before replacing any live file. This
  // prevents a missing or damaged later backup from creating a second partial
  // project while trying to recover the first one.
  QList<QByteArray> backupContents;
  backupContents.reserve(files.size());
  for (const auto &[backup, target] : files) {
    Q_UNUSED(target)
    QString ioError;
    const QByteArray bytes = readFile(backup, ioError);
    if (!ioError.isEmpty()) {
      message = QStringLiteral("Recovery backup cannot be read: %1: %2")
                    .arg(backup, ioError);
      return false;
    }
    const Json5Result parsed = Json5::parse(bytes);
    if (!parsed || !parsed.document.isObject()) {
      message =
          QStringLiteral("Recovery backup is invalid: %1: %2")
              .arg(backup, parsed ? QStringLiteral("expected a JSON5 object")
                                  : parsed.error);
      return false;
    }
    backupContents.append(bytes);
  }

  for (qsizetype index = 0; index < files.size(); ++index) {
    QString ioError;
    const QString &target = files.at(index).second;
    if (!writeFile(target, backupContents.at(index),
                   test_support::ProjectWritePurpose::RecoveryRestore,
                   ioError)) {
      // Keep the marker and complete backup set. A later launch can retry the
      // entire recovery after the filesystem problem has been resolved.
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
    if (!ioError.isEmpty() ||
        !writeFile(backup, bytes,
                   test_support::ProjectWritePurpose::RecoveryBackup,
                   ioError)) {
      message =
          QStringLiteral("Could not prepare recovery data: %1").arg(ioError);
      return false;
    }
  }
  QString ioError;
  if (!writeFile(QDir(recovery).filePath(QString::fromLatin1(kRecoveryMarker)),
                 QByteArrayLiteral("pending\n"),
                 test_support::ProjectWritePurpose::RecoveryMarker, ioError)) {
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

Json5Result readJson5(const QString &path, QList<Diagnostic> &diagnostics,
                      QByteArray *digest = nullptr) {
  QString ioError;
  const QByteArray bytes = readFile(path, ioError);
  if (!ioError.isEmpty()) {
    diagnostics.append(
        error(QStringLiteral("persistence"),
              QStringLiteral("Cannot read %1: %2").arg(path, ioError)));
    return {};
  }
  if (digest)
    *digest = contentDigest(bytes);
  auto result = Json5::parse(bytes);
  if (!result) {
    diagnostics.append(
        error(QStringLiteral("persistence"),
              QStringLiteral("Cannot parse %1: %2").arg(path, result.error)));
  }
  return result;
}

} // namespace

bool ProjectFileRevision::isValid() const {
  return !rootPath.isEmpty() && !fileDigests.isEmpty();
}

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
  const QStringList recoveryDirectories = {
      QString::fromLatin1(kRecoveryDirectory),
      QString::fromLatin1(kPreviousRecoveryDirectory)};
  for (const QString &recoveryDirectory : recoveryDirectories) {
    if (!QFileInfo::exists(QDir(root).filePath(
            recoveryDirectory + u'/' + QString::fromLatin1(kRecoveryMarker)))) {
      continue;
    }
    outcome.recovered =
        recoverIfPending(root, recoveryDirectory, recoveryMessage);
    if (!outcome.recovered) {
      outcome.diagnostics.append(
          error(QStringLiteral("recovery"), recoveryMessage));
      return outcome;
    }
    outcome.diagnostics.append(
        warning(QStringLiteral("recovery"), recoveryMessage));
    break;
  }

  const QString manifestPath =
      QDir(root).filePath(QString::fromLatin1(kManifestName));
  QByteArray manifestDigest;
  const auto manifestResult =
      readJson5(manifestPath, outcome.diagnostics, &manifestDigest);
  if (!manifestResult || !manifestResult.document.isObject())
    return outcome;
  const QJsonObject sourceManifest = manifestResult.document.object();

  const QString modelRelative = sourceManifest.value(QStringLiteral("model"))
                                    .toString(QString::fromLatin1(kModelName));
  const QString diagramsRelative =
      sourceManifest.value(QStringLiteral("diagrams"))
          .toString(QString::fromLatin1(kDiagramsName));
  const QString modelPath = QDir(root).filePath(modelRelative);
  const QString diagramsPath = QDir(root).filePath(diagramsRelative);
  QByteArray modelDigest;
  QByteArray diagramsDigest;
  const auto modelResult =
      readJson5(modelPath, outcome.diagnostics, &modelDigest);
  const auto diagramsResult =
      readJson5(diagramsPath, outcome.diagnostics, &diagramsDigest);
  if (!modelResult || !modelResult.document.isObject() || !diagramsResult ||
      !diagramsResult.document.isObject())
    return outcome;

  outcome.revision.rootPath = root;
  outcome.revision.fileDigests.insert(revisionKey(root, manifestPath),
                                      manifestDigest);
  outcome.revision.fileDigests.insert(revisionKey(root, modelPath),
                                      modelDigest);
  outcome.revision.fileDigests.insert(revisionKey(root, diagramsPath),
                                      diagramsDigest);
  // Saving always targets the canonical paths. Track them even when an older
  // or hand-authored manifest points elsewhere, so a newly appeared target is
  // not overwritten without detection.
  addCurrentRevisionEntry(outcome.revision, QString::fromLatin1(kManifestName));
  addCurrentRevisionEntry(outcome.revision, QString::fromLatin1(kModelName));
  addCurrentRevisionEntry(outcome.revision, QString::fromLatin1(kDiagramsName));

  auto migration = ProjectSchemaMigrator::migrate(
      {sourceManifest, modelResult.document.object(),
       diagramsResult.document.object()});
  outcome.diagnostics.append(migration.diagnostics);
  if (!migration.ok)
    return outcome;
  outcome.migrated = migration.migrated;

  const QJsonObject &manifest = migration.documents.manifest;
  ProjectData project;
  project.schemaVersion =
      manifest.value(QStringLiteral("schemaVersion")).toInt();
  project.id = manifest.value(QStringLiteral("id")).toString();
  project.name = manifest.value(QStringLiteral("name")).toString();
  const QJsonObject cppImport =
      manifest.value(QStringLiteral("cppImport")).toObject();
  for (const auto &value :
       cppImport.value(QStringLiteral("sourceRoots")).toArray()) {
    const QString sourceRoot = value.toString();
    if (!sourceRoot.isEmpty() &&
        !project.cppImport.sourceRoots.contains(sourceRoot))
      project.cppImport.sourceRoots.append(sourceRoot);
  }
  // Projects written before multi-root import used one sourceRoot string.
  if (project.cppImport.sourceRoots.isEmpty()) {
    const QString legacyRoot =
        cppImport.value(QStringLiteral("sourceRoot")).toString();
    if (!legacyRoot.isEmpty())
      project.cppImport.sourceRoots.append(legacyRoot);
  }
  project.cppImport.extra = withoutKeys(
      cppImport, {QStringLiteral("sourceRoot"), QStringLiteral("sourceRoots")});
  project.manifestExtra = withoutKeys(
      manifest, {QStringLiteral("schemaVersion"), QStringLiteral("id"),
                 QStringLiteral("name"), QStringLiteral("model"),
                 QStringLiteral("diagrams"), QStringLiteral("cppImport")});

  const QJsonObject &model = migration.documents.model;
  project.modelExtra = withoutKeys(
      model, {QStringLiteral("styles"), QStringLiteral("stereotypes"),
              QStringLiteral("namespaceStyles"), QStringLiteral("elements"),
              QStringLiteral("browserFolders"), QStringLiteral("browserOrder"),
              QStringLiteral("relationships")});
  for (const auto &value : model.value(QStringLiteral("styles")).toArray()) {
    const QJsonObject object = value.toObject();
    DiagramStyle style;
    style.id = object.value(QStringLiteral("id")).toString();
    style.name = object.value(QStringLiteral("name")).toString();
    style.fill = object.value(QStringLiteral("fill")).toString();
    style.headerFill = object.value(QStringLiteral("headerFill")).toString();
    style.border = object.value(QStringLiteral("border")).toString();
    style.primaryText = object.value(QStringLiteral("primaryText")).toString();
    style.secondaryText =
        object.value(QStringLiteral("secondaryText")).toString();
    style.divider = object.value(QStringLiteral("divider")).toString();
    style.extra = withoutKeys(
        object, {QStringLiteral("id"), QStringLiteral("name"),
                 QStringLiteral("fill"), QStringLiteral("headerFill"),
                 QStringLiteral("border"), QStringLiteral("primaryText"),
                 QStringLiteral("secondaryText"), QStringLiteral("divider")});
    project.diagramStyles.append(std::move(style));
  }
  const QJsonValue stereotypesValue =
      model.value(QStringLiteral("stereotypes"));
  if (!stereotypesValue.isUndefined() && !stereotypesValue.isArray())
    outcome.diagnostics.append(
        error(QStringLiteral("validation"),
              QStringLiteral("Project stereotypes must be an array")));
  for (const auto &value : stereotypesValue.toArray()) {
    if (!value.isObject()) {
      outcome.diagnostics.append(
          error(QStringLiteral("validation"),
                QStringLiteral("A project stereotype must be an object")));
      continue;
    }
    const QJsonObject object = value.toObject();
    StereotypeDefinition definition;
    definition.id = object.value(QStringLiteral("id")).toString();
    definition.name = object.value(QStringLiteral("name")).toString();
    const QJsonValue applicableTo =
        object.value(QStringLiteral("applicableTo"));
    if (!applicableTo.isArray())
      outcome.diagnostics.append(
          error(QStringLiteral("validation"),
                QStringLiteral("Stereotype applicableTo must be an array"),
                definition.id));
    definition.applicableTo = readStringArray(applicableTo);
    definition.extra =
        withoutKeys(object, {QStringLiteral("id"), QStringLiteral("name"),
                             QStringLiteral("applicableTo")});
    project.stereotypeDefinitions.append(std::move(definition));
  }
  const QJsonObject namespaceStyles =
      model.value(QStringLiteral("namespaceStyles")).toObject();
  for (auto style = namespaceStyles.begin(); style != namespaceStyles.end();
       ++style)
    project.namespaceStyleIds.insert(style.key(), style.value().toString());
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
    element.enclosingTypeId =
        object.value(QStringLiteral("enclosingTypeId")).toString();
    element.attributes =
        readStringArray(object.value(QStringLiteral("attributes")));
    QString operationsError;
    const auto operations = modelOperationsFromJson(
        object.value(QStringLiteral("operations")), &operationsError);
    if (operations) {
      element.operations = *operations;
    } else {
      outcome.diagnostics.append(
          error(QStringLiteral("validation"), operationsError, element.id));
    }
    element.enumLiterals =
        readStringArray(object.value(QStringLiteral("enumLiterals")));
    element.browserParent =
        readBrowserParent(object.value(QStringLiteral("browserParent")));
    element.styleId = object.value(QStringLiteral("styleId")).toString();
    const QJsonValue elementStereotypes =
        object.value(QStringLiteral("stereotypeIds"));
    if (!elementStereotypes.isUndefined() && !elementStereotypes.isArray())
      outcome.diagnostics.append(
          error(QStringLiteral("validation"),
                QStringLiteral("Element stereotypeIds must be an array"),
                element.id));
    element.stereotypeIds = readStringArray(elementStereotypes);
    element.extra = withoutKeys(
        object,
        {QStringLiteral("id"), QStringLiteral("type"), QStringLiteral("name"),
         QStringLiteral("packageId"), QStringLiteral("enclosingTypeId"),
         QStringLiteral("attributes"), QStringLiteral("operations"),
         QStringLiteral("enumLiterals"), QStringLiteral("browserParent"),
         QStringLiteral("styleId"), QStringLiteral("stereotypeIds")});
    project.elements.append(element);
  }

  for (const auto &value :
       model.value(QStringLiteral("browserFolders")).toArray()) {
    const QJsonObject object = value.toObject();
    BrowserFolder folder;
    folder.id = object.value(QStringLiteral("id")).toString();
    folder.name = object.value(QStringLiteral("name")).toString();
    folder.parent = readBrowserParent(object.value(QStringLiteral("parent")));
    folder.styleId = object.value(QStringLiteral("styleId")).toString();
    folder.extra = withoutKeys(
        object, {QStringLiteral("id"), QStringLiteral("name"),
                 QStringLiteral("parent"), QStringLiteral("styleId")});
    project.browserFolders.append(folder);
  }
  project.browserItemOrder =
      readStringArray(model.value(QStringLiteral("browserOrder")));

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
    relationship.sourceEnd = relationshipEndFromJson(
        object.value(QStringLiteral("sourceEnd")), QStringLiteral("sourceEnd"),
        relationship.id, outcome.diagnostics);
    relationship.targetEnd = relationshipEndFromJson(
        object.value(QStringLiteral("targetEnd")), QStringLiteral("targetEnd"),
        relationship.id, outcome.diagnostics);
    const QJsonValue relationshipStereotypes =
        object.value(QStringLiteral("stereotypeIds"));
    if (!relationshipStereotypes.isUndefined() &&
        !relationshipStereotypes.isArray())
      outcome.diagnostics.append(
          error(QStringLiteral("validation"),
                QStringLiteral("Relationship stereotypeIds must be an array"),
                relationship.id));
    relationship.stereotypeIds = readStringArray(relationshipStereotypes);
    relationship.extra = withoutKeys(
        object, {QStringLiteral("id"), QStringLiteral("type"),
                 QStringLiteral("name"), QStringLiteral("sourceId"),
                 QStringLiteral("targetId"), QStringLiteral("sourceEnd"),
                 QStringLiteral("targetEnd"), QStringLiteral("stereotypeIds")});
    project.relationships.append(relationship);
  }

  const QJsonObject &diagramRoot = migration.documents.diagrams;
  project.diagramsExtra =
      withoutKeys(diagramRoot, {QStringLiteral("diagrams")});
  for (const auto &value :
       diagramRoot.value(QStringLiteral("diagrams")).toArray()) {
    const QJsonObject object = value.toObject();
    Diagram diagram;
    diagram.id = object.value(QStringLiteral("id")).toString();
    diagram.name = object.value(QStringLiteral("name")).toString();
    const QJsonValue containersValue =
        object.value(QStringLiteral("containers"));
    if (!containersValue.isUndefined() && !containersValue.isArray())
      outcome.diagnostics.append(error(
          QStringLiteral("validation"),
          QStringLiteral("Diagram containers must be an array"), diagram.id));
    for (const auto &containerValue : containersValue.toArray()) {
      const QJsonObject containerObject = containerValue.toObject();
      ContainerPresentation container;
      container.id = containerObject.value(QStringLiteral("id")).toString();
      container.subjectKind =
          containerObject.value(QStringLiteral("subjectKind")).toString();
      container.subjectId =
          containerObject.value(QStringLiteral("subjectId")).toString();
      const QJsonObject geometry =
          containerObject.value(QStringLiteral("geometry")).toObject();
      container.geometry = {
          geometry.value(QStringLiteral("x")).toDouble(),
          geometry.value(QStringLiteral("y")).toDouble(),
          geometry.value(QStringLiteral("width")).toDouble(),
          geometry.value(QStringLiteral("height")).toDouble()};
      const QJsonValue childIdsValue =
          containerObject.value(QStringLiteral("childPresentationIds"));
      if (!childIdsValue.isUndefined() && !childIdsValue.isArray())
        outcome.diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("Container childPresentationIds must be an array"),
            container.id));
      container.childPresentationIds = readStringArray(childIdsValue);
      container.styleId =
          containerObject.value(QStringLiteral("styleId")).toString();
      container.extra = withoutKeys(
          containerObject,
          {QStringLiteral("id"), QStringLiteral("subjectKind"),
           QStringLiteral("subjectId"), QStringLiteral("geometry"),
           QStringLiteral("childPresentationIds"), QStringLiteral("styleId")});
      diagram.containers.append(std::move(container));
    }
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
      node.horizontalPortSnapPoints = readPortSnapPointCount(
          nodeObject, QStringLiteral("horizontalPortSnapPoints"), node.id,
          outcome.diagnostics);
      node.verticalPortSnapPoints = readPortSnapPointCount(
          nodeObject, QStringLiteral("verticalPortSnapPoints"), node.id,
          outcome.diagnostics);
      const auto readVisibilityOverride =
          [&](const QString &key) -> std::optional<bool> {
        const QJsonValue value = nodeObject.value(key);
        if (value.isUndefined())
          return std::nullopt;
        if (value.isBool())
          return value.toBool();
        outcome.diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("Node %1 must be a boolean").arg(key), node.id));
        return std::nullopt;
      };
      node.showAttributes =
          readVisibilityOverride(QStringLiteral("showAttributes"));
      node.showOperations =
          readVisibilityOverride(QStringLiteral("showOperations"));
      const QJsonValue signatureModeValue =
          nodeObject.value(QStringLiteral("operationSignatureMode"));
      if (!signatureModeValue.isUndefined()) {
        bool modeOk = false;
        const auto mode = operationSignatureModeFromString(
            signatureModeValue.toString(), &modeOk);
        if (modeOk) {
          node.operationSignatureMode = mode;
        } else {
          outcome.diagnostics.append(
              error(QStringLiteral("validation"),
                    QStringLiteral(
                        "Node operationSignatureMode has an unknown value"),
                    node.id));
        }
      }
      node.styleId = nodeObject.value(QStringLiteral("styleId")).toString();
      node.extra = withoutKeys(
          nodeObject,
          {QStringLiteral("id"), QStringLiteral("elementId"),
           QStringLiteral("geometry"),
           QStringLiteral("horizontalPortSnapPoints"),
           QStringLiteral("verticalPortSnapPoints"),
           QStringLiteral("showAttributes"), QStringLiteral("showOperations"),
           QStringLiteral("operationSignatureMode"),
           QStringLiteral("styleId")});
      diagram.nodes.append(node);
    }
    for (const auto &connectorValue :
         object.value(QStringLiteral("connectors")).toArray()) {
      const QJsonObject connectorObject = connectorValue.toObject();
      ConnectorPresentation connector;
      connector.id = connectorObject.value(QStringLiteral("id")).toString();
      connector.relationshipId =
          connectorObject.value(QStringLiteral("relationshipId")).toString();
      const QJsonValue routingValue =
          connectorObject.value(QStringLiteral("routing"));
      if (!routingValue.isUndefined()) {
        bool routingOk = false;
        connector.routing =
            connectorRoutingFromString(routingValue.toString(), &routingOk);
        if (!routingOk)
          outcome.diagnostics.append(
              error(QStringLiteral("validation"),
                    QStringLiteral("Connector has an unknown routing mode"),
                    connector.id));
      }
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
      const QJsonValue annotationPlacementsValue =
          connectorObject.value(QStringLiteral("annotationPlacements"));
      if (!annotationPlacementsValue.isUndefined() &&
          !annotationPlacementsValue.isObject()) {
        outcome.diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("Connector annotationPlacements must be an object"),
            connector.id));
      } else {
        const QJsonObject placements = annotationPlacementsValue.toObject();
        for (auto placement = placements.begin(); placement != placements.end();
             ++placement) {
          connector.annotationPlacements.insert(
              placement.key(),
              readAnnotationPlacement(placement.value(), connector.id,
                                      placement.key(), outcome.diagnostics));
        }
      }
      connector.extra = withoutKeys(
          connectorObject,
          {QStringLiteral("id"), QStringLiteral("relationshipId"),
           QStringLiteral("routing"), QStringLiteral("sourceAnchor"),
           QStringLiteral("targetAnchor"), QStringLiteral("bendPoints"),
           QStringLiteral("annotationPlacements")});
      diagram.connectors.append(connector);
    }
    const auto readDiagramVisibility = [&](const QString &key) {
      const QJsonValue value = object.value(key);
      if (value.isUndefined())
        return true;
      if (value.isBool())
        return value.toBool();
      outcome.diagnostics.append(error(
          QStringLiteral("validation"),
          QStringLiteral("Diagram %1 must be a boolean").arg(key), diagram.id));
      return true;
    };
    diagram.showAttributes =
        readDiagramVisibility(QStringLiteral("showAttributes"));
    diagram.showOperations =
        readDiagramVisibility(QStringLiteral("showOperations"));
    const QJsonValue signatureModeValue =
        object.value(QStringLiteral("operationSignatureMode"));
    if (!signatureModeValue.isUndefined()) {
      bool modeOk = false;
      diagram.operationSignatureMode = operationSignatureModeFromString(
          signatureModeValue.toString(), &modeOk);
      if (!modeOk) {
        outcome.diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral(
                      "Diagram operationSignatureMode has an unknown value"),
                  diagram.id));
        diagram.operationSignatureMode = OperationSignatureMode::Full;
      }
    }
    diagram.filter = readDiagramFilter(object.value(QStringLiteral("filter")),
                                       diagram.id, outcome.diagnostics);
    diagram.extra = withoutKeys(
        object,
        {QStringLiteral("id"), QStringLiteral("name"),
         QStringLiteral("showAttributes"), QStringLiteral("showOperations"),
         QStringLiteral("operationSignatureMode"), QStringLiteral("filter"),
         QStringLiteral("containers"), QStringLiteral("nodes"),
         QStringLiteral("connectors")});
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
                                    const ProjectData &project,
                                    const ProjectFileRevision &expectedRevision,
                                    bool overwriteExternalChanges) {
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
    if (!recoverIfPending(root, QString::fromLatin1(kRecoveryDirectory),
                          recoveryMessage)) {
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
  QMap<QString, QByteArray> currentTargetDigests;
  for (const auto &[path, bytes] : files) {
    QString ioError;
    const QByteArray existing = readFile(path, ioError);
    currentTargetDigests.insert(revisionKey(root, path),
                                ioError.isEmpty()
                                    ? contentDigest(existing)
                                    : (QFileInfo::exists(path)
                                           ? QByteArrayLiteral("unreadable")
                                           : QByteArrayLiteral("missing")));
    if (!ioError.isEmpty() || existing != bytes) {
      allSame = false;
    }
  }
  if (allSame) {
    outcome.ok = true;
    outcome.unchanged = true;
    outcome.revision = savedRevision(root, files);
    return outcome;
  }

  const auto refuseExternalChanges = [&](const QStringList &changedFiles) {
    if (changedFiles.isEmpty())
      return false;
    outcome.externallyChangedFiles = changedFiles;
    outcome.externalChangesDetected = true;
    outcome.diagnostics.append(
        warning(QStringLiteral("external-change"),
                QStringLiteral(
                    "Saving was stopped because project files changed outside "
                    "yauml: %1")
                    .arg(changedFiles.join(QStringLiteral(", ")))));
    return true;
  };
  if (!overwriteExternalChanges) {
    if (refuseExternalChanges(externallyChangedFiles(root, expectedRevision,
                                                     currentTargetDigests)))
      return outcome;
  }

  QString recoveryError;
  if (!prepareRecovery(root, recoveryError)) {
    outcome.diagnostics.append(
        error(QStringLiteral("recovery"), recoveryError));
    return outcome;
  }

  // Recheck after preparing recovery data. This closes the most likely race
  // window between the initial comparison and the first replacement while
  // retaining the latest valid external files if another writer intervened.
  if (!overwriteExternalChanges &&
      refuseExternalChanges(externallyChangedFiles(root, expectedRevision))) {
    finishRecovery(root);
    return outcome;
  }

  for (const auto &[path, bytes] : files) {
    QString ioError;
    if (!writeFile(path, bytes, test_support::ProjectWritePurpose::ProjectFile,
                   ioError)) {
      outcome.diagnostics.append(
          error(QStringLiteral("persistence"),
                QStringLiteral("Cannot write %1: %2").arg(path, ioError)));
      return outcome;
    }
  }
  finishRecovery(root);
  outcome.ok = true;
  outcome.revision = savedRevision(root, files);
  return outcome;
}

QList<Diagnostic> ProjectSerializer::validate(const ProjectData &project) {
  QList<Diagnostic> diagnostics;
  if (project.schemaVersion != kCurrentProjectSchemaVersion)
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
  QSet<QString> packageIds;
  QSet<QString> styleIds;
  QSet<QString> styleNames;
  for (const auto &style : project.diagramStyles) {
    checkId(style.id, QStringLiteral("diagram style"));
    styleIds.insert(style.id);
    const QString normalizedName = style.name.trimmed().toCaseFolded();
    if (normalizedName.isEmpty()) {
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("A diagram style has no name"),
                               style.id));
    } else if (styleNames.contains(normalizedName)) {
      diagnostics.append(error(
          QStringLiteral("validation"),
          QStringLiteral("Diagram style names must be unique"), style.id));
    } else {
      styleNames.insert(normalizedName);
    }
    const QList<QPair<QString, QString>> colors = {
        {QStringLiteral("fill"), style.fill},
        {QStringLiteral("headerFill"), style.headerFill},
        {QStringLiteral("border"), style.border},
        {QStringLiteral("primaryText"), style.primaryText},
        {QStringLiteral("secondaryText"), style.secondaryText},
        {QStringLiteral("divider"), style.divider}};
    for (const auto &[role, value] : colors)
      if (!QColor(value).isValid())
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("Diagram style %1 color is invalid").arg(role),
                  style.id));
  }
  const QSet<QString> validStereotypeApplicabilities = {
      QStringLiteral("package"), QStringLiteral("class"),
      QStringLiteral("struct"), QStringLiteral("enumeration"),
      stereotype_catalog::kRelationshipApplicability};
  QSet<QString> stereotypeNames;
  for (const auto &definition : project.stereotypeDefinitions) {
    checkId(definition.id, QStringLiteral("project stereotype"));
    const QString normalizedName = definition.name.trimmed().toCaseFolded();
    if (normalizedName.isEmpty()) {
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("A stereotype has no name"),
                               definition.id));
    } else if (stereotypeNames.contains(normalizedName)) {
      diagnostics.append(error(
          QStringLiteral("validation"),
          QStringLiteral("Stereotype names must be unique"), definition.id));
    } else {
      stereotypeNames.insert(normalizedName);
    }
    if (definition.applicableTo.isEmpty())
      diagnostics.append(error(
          QStringLiteral("validation"),
          QStringLiteral("A stereotype must apply to at least one subject"),
          definition.id));
    QSet<QString> seenApplicabilities;
    for (const QString &applicability : definition.applicableTo) {
      if (!validStereotypeApplicabilities.contains(applicability))
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("Unknown stereotype applicability %1")
                      .arg(applicability),
                  definition.id));
      if (seenApplicabilities.contains(applicability))
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("A stereotype repeats an applicability"),
                  definition.id));
      seenApplicabilities.insert(applicability);
    }
  }
  const auto validateStereotypeReferences =
      [&](const QStringList &stereotypeIds, const QString &applicability,
          const QString &subjectId) {
        QSet<QString> seen;
        for (const QString &stereotypeId : stereotypeIds) {
          if (seen.contains(stereotypeId)) {
            diagnostics.append(
                error(QStringLiteral("validation"),
                      QStringLiteral("A stereotype is assigned more than once"),
                      subjectId));
            continue;
          }
          seen.insert(stereotypeId);
          const auto *definition =
              stereotype_catalog::find(project, stereotypeId);
          if (!definition) {
            diagnostics.append(
                error(QStringLiteral("validation"),
                      QStringLiteral("Stereotype reference %1 is invalid")
                          .arg(stereotypeId),
                      subjectId));
          } else if (!stereotype_catalog::appliesTo(*definition,
                                                    applicability)) {
            diagnostics.append(
                error(QStringLiteral("validation"),
                      QStringLiteral("Stereotype %1 does not apply to %2")
                          .arg(definition->name, applicability),
                      subjectId));
          }
        }
      };
  const auto validateStyleReference = [&](const QString &styleId,
                                          const QString &subjectId) {
    if (!styleId.isEmpty() && !styleIds.contains(styleId))
      diagnostics.append(error(
          QStringLiteral("validation"),
          QStringLiteral("Diagram style reference %1 is invalid").arg(styleId),
          subjectId));
  };
  for (const auto &element : project.elements)
    if (element.type == ElementType::Package)
      packageIds.insert(element.id);
  for (const auto &element : project.elements) {
    checkId(element.id, QStringLiteral("model element"));
    elementIds.insert(element.id);
    if (element.name.trimmed().isEmpty())
      diagnostics.append(error(
          QStringLiteral("validation"),
          QStringLiteral("A model element has an empty name"), element.id));
    if (!element.packageId.isEmpty() && !packageIds.contains(element.packageId))
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("Package reference %1 is invalid")
                                   .arg(element.packageId),
                               element.id));
    validateStyleReference(element.styleId, element.id);
    validateStereotypeReferences(
        element.stereotypeIds,
        stereotype_catalog::applicabilityFor(element.type), element.id);
    for (const auto &operation : element.operations) {
      checkId(operation.id, QStringLiteral("operation"));
      if (operation.name.trimmed().isEmpty()) {
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("An operation has an empty name"), operation.id));
      }
      if (operation.sourceLine < 0 || operation.sourceColumn < 0) {
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("An operation source location is invalid"),
                  operation.id));
      }
      for (const auto &parameter : operation.parameters) {
        if (parameter.type.trimmed().isEmpty()) {
          diagnostics.append(
              error(QStringLiteral("validation"),
                    QStringLiteral("An operation parameter has an empty type"),
                    operation.id));
        }
      }
    }
    if (!element.enclosingTypeId.isEmpty()) {
      const auto *owner = findElement(project, element.enclosingTypeId);
      if (!owner || owner->id == element.id ||
          (owner->type != ElementType::Class &&
           owner->type != ElementType::Struct) ||
          element.type == ElementType::Package) {
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("Enclosing type reference %1 is invalid")
                      .arg(element.enclosingTypeId),
                  element.id));
      } else if (element.packageId != owner->packageId) {
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("A nested type must share its owner's package"),
            element.id));
      }
    }
  }

  for (const auto &element : project.elements) {
    QSet<QString> path{element.id};
    QString current = element.enclosingTypeId;
    while (!current.isEmpty()) {
      if (path.contains(current)) {
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("Nested type ownership contains a cycle"),
                  element.id));
        break;
      }
      path.insert(current);
      const auto *owner = findElement(project, current);
      if (!owner)
        break;
      current = owner->enclosingTypeId;
    }
  }

  QSet<QString> folderIds;
  for (const auto &folder : project.browserFolders) {
    checkId(folder.id, QStringLiteral("browser folder"));
    folderIds.insert(folder.id);
    if (folder.name.trimmed().isEmpty())
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("A browser folder has an empty "
                                              "name"),
                               folder.id));
    validateStyleReference(folder.styleId, folder.id);
  }

  for (auto assignment = project.namespaceStyleIds.cbegin();
       assignment != project.namespaceStyleIds.cend(); ++assignment) {
    if (assignment.key().trimmed().isEmpty())
      diagnostics.append(
          error(QStringLiteral("validation"),
                QStringLiteral("A namespace style assignment has no path")));
    validateStyleReference(assignment.value(), assignment.key());
  }

  const auto validateBrowserParent = [&](const BrowserParent &parent,
                                         const QString &subjectId,
                                         bool parentRequired) {
    if (parent.kind.isEmpty()) {
      if (parentRequired)
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("A browser folder has no parent"), subjectId));
      return;
    }
    if (parent.kind == QStringLiteral("model")) {
      if (!parent.id.isEmpty())
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("The model browser parent must not have an ID"),
            subjectId));
      return;
    }
    if (parent.kind == QStringLiteral("namespace")) {
      if (parent.id.trimmed().isEmpty())
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("A namespace browser parent has no path"),
                  subjectId));
      return;
    }
    if (parent.kind == QStringLiteral("element")) {
      if (!elementIds.contains(parent.id) || parent.id == subjectId)
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("Browser item %1 references invalid parent element "
                           "%2")
                .arg(subjectId, parent.id),
            subjectId));
      return;
    }
    if (parent.kind == QStringLiteral("folder")) {
      if (!folderIds.contains(parent.id) || parent.id == subjectId)
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("Browser item %1 references invalid parent folder "
                           "%2")
                .arg(subjectId, parent.id),
            subjectId));
      return;
    }
    diagnostics.append(error(
        QStringLiteral("validation"),
        QStringLiteral("Unknown browser parent kind: %1").arg(parent.kind),
        subjectId));
  };

  for (const auto &element : project.elements)
    validateBrowserParent(element.browserParent, element.id, false);
  for (const auto &folder : project.browserFolders)
    validateBrowserParent(folder.parent, folder.id, true);

  // Browser ownership is a one-parent graph spanning both semantic elements
  // and custom folders. Reject cycles before the tree model sees the data.
  QHash<QString, QString> elementKeyByQualifiedName;
  for (const auto &element : project.elements)
    if (!elementKeyByQualifiedName.contains(element.name))
      elementKeyByQualifiedName.insert(element.name,
                                       QStringLiteral("element:") + element.id);
  QHash<QString, QString> browserParentBySubject;
  const auto parentKey = [](const BrowserParent &parent) {
    if (parent.kind == QStringLiteral("element"))
      return QStringLiteral("element:") + parent.id;
    if (parent.kind == QStringLiteral("folder"))
      return QStringLiteral("folder:") + parent.id;
    if (parent.kind == QStringLiteral("namespace") && !parent.id.isEmpty())
      return QStringLiteral("namespace:") + parent.id;
    return QString{};
  };
  QSet<QString> namespacePaths;
  const auto rememberNamespacePath = [&](const QString &path) {
    QString current = path;
    while (!current.isEmpty()) {
      namespacePaths.insert(current);
      const int separator = current.lastIndexOf(QStringLiteral("::"));
      current = separator >= 0 ? current.left(separator) : QString{};
    }
  };
  for (const auto &element : project.elements)
    if (element.browserParent.kind == QStringLiteral("namespace"))
      rememberNamespacePath(element.browserParent.id);
  for (const auto &folder : project.browserFolders)
    if (folder.parent.kind == QStringLiteral("namespace"))
      rememberNamespacePath(folder.parent.id);

  for (const QString &namespacePath : namespacePaths) {
    const int separator = namespacePath.lastIndexOf(QStringLiteral("::"));
    if (separator < 0)
      continue;
    const QString parentPath = namespacePath.left(separator);
    const QString parent = elementKeyByQualifiedName.contains(parentPath)
                               ? elementKeyByQualifiedName.value(parentPath)
                               : QStringLiteral("namespace:") + parentPath;
    browserParentBySubject.insert(QStringLiteral("namespace:") + namespacePath,
                                  parent);
  }
  for (const auto &element : project.elements) {
    QString parent;
    if (!element.browserParent.kind.isEmpty()) {
      parent = parentKey(element.browserParent);
    } else if (!element.enclosingTypeId.isEmpty()) {
      parent = QStringLiteral("element:") + element.enclosingTypeId;
    } else {
      const QStringList parts =
          element.name.split(QStringLiteral("::"), Qt::SkipEmptyParts);
      QString qualifiedPath;
      for (int index = 0; index + 1 < parts.size(); ++index) {
        if (!qualifiedPath.isEmpty())
          qualifiedPath += QStringLiteral("::");
        qualifiedPath += parts.at(index);
        if (elementKeyByQualifiedName.contains(qualifiedPath))
          parent = elementKeyByQualifiedName.value(qualifiedPath);
        else
          parent = QStringLiteral("namespace:") + qualifiedPath;
      }
      if (parent.isEmpty() && !element.packageId.isEmpty())
        parent = QStringLiteral("element:") + element.packageId;
    }
    if (!parent.isEmpty())
      browserParentBySubject.insert(QStringLiteral("element:") + element.id,
                                    parent);
  }
  for (const auto &folder : project.browserFolders) {
    const QString parent = parentKey(folder.parent);
    if (!parent.isEmpty())
      browserParentBySubject.insert(QStringLiteral("folder:") + folder.id,
                                    parent);
  }

  QSet<QString> reportedCycles;
  for (auto subject = browserParentBySubject.cbegin();
       subject != browserParentBySubject.cend(); ++subject) {
    QSet<QString> path;
    QString current = subject.key();
    while (!current.isEmpty() && browserParentBySubject.contains(current)) {
      if (path.contains(current)) {
        if (!reportedCycles.contains(current)) {
          reportedCycles.insert(current);
          diagnostics.append(
              error(QStringLiteral("validation"),
                    QStringLiteral("Browser hierarchy contains a cycle"),
                    current.section(u':', 1)));
        }
        break;
      }
      path.insert(current);
      current = browserParentBySubject.value(current);
    }
  }

  QSet<QString> relationshipIds;
  for (const auto &relationship : project.relationships) {
    checkId(relationship.id, QStringLiteral("relationship"));
    relationshipIds.insert(relationship.id);
    if (!elementIds.contains(relationship.sourceId))
      diagnostics.append(error(
          QStringLiteral("validation"),
          QStringLiteral("Relationship %1 source references missing model "
                         "element %2")
              .arg(relationship.id, relationship.sourceId),
          relationship.id));
    if (!elementIds.contains(relationship.targetId))
      diagnostics.append(error(
          QStringLiteral("validation"),
          QStringLiteral("Relationship %1 target references missing model "
                         "element %2")
              .arg(relationship.id, relationship.targetId),
          relationship.id));
    validateStereotypeReferences(relationship.stereotypeIds,
                                 stereotype_catalog::kRelationshipApplicability,
                                 relationship.id);
  }

  for (const auto &diagram : project.diagrams) {
    checkId(diagram.id, QStringLiteral("diagram"));
    if (diagram.name.trimmed().isEmpty())
      diagnostics.append(error(QStringLiteral("validation"),
                               QStringLiteral("A diagram has an empty name"),
                               diagram.id));
    QSet<QString> presentationIds;
    QSet<QString> presentedContainerSubjects;
    for (const auto &container : diagram.containers) {
      checkId(container.id, QStringLiteral("container presentation"));
      presentationIds.insert(container.id);
      const bool validFolder =
          container.subjectKind == QStringLiteral("folder") &&
          folderIds.contains(container.subjectId);
      const bool validPackage =
          container.subjectKind == QStringLiteral("package") &&
          packageIds.contains(container.subjectId);
      if (!validFolder && !validPackage) {
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("Diagram \"%1\" container presentation %2 "
                                 "references missing or invalid %3 %4")
                      .arg(diagram.name, container.id, container.subjectKind,
                           container.subjectId),
                  container.id));
      }
      const QString subjectKey =
          container.subjectKind + u':' + container.subjectId;
      if (presentedContainerSubjects.contains(subjectKey)) {
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("A container subject appears twice on the "
                                 "same diagram"),
                  container.id));
      }
      presentedContainerSubjects.insert(subjectKey);
      if (container.geometry.width() <= 0 || container.geometry.height() <= 0) {
        diagnostics.append(
            error(QStringLiteral("validation"),
                  QStringLiteral("Container presentation has invalid geometry"),
                  container.id));
      }
      validateStyleReference(container.styleId, container.id);
    }
    QSet<QString> presentedElements;
    for (const auto &node : diagram.nodes) {
      checkId(node.id, QStringLiteral("node presentation"));
      presentationIds.insert(node.id);
      if (!elementIds.contains(node.elementId))
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("Diagram \"%1\" node presentation %2 references "
                           "missing element %3")
                .arg(diagram.name, node.id, node.elementId),
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
      if (!connector_ports::isValidSnapPointCount(
              node.horizontalPortSnapPoints) ||
          !connector_ports::isValidSnapPointCount(node.verticalPortSnapPoints))
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("Node presentation snap-point counts must be odd "
                           "numbers between 1 and %1")
                .arg(connector_ports::kMaximumSnapPointCount),
            node.id));
      validateStyleReference(node.styleId, node.id);
    }

    QHash<QString, QString> ownerByChild;
    for (const auto &container : diagram.containers) {
      QSet<QString> seenChildren;
      for (const QString &childId : container.childPresentationIds) {
        if (childId == container.id || !presentationIds.contains(childId)) {
          diagnostics.append(
              error(QStringLiteral("validation"),
                    QStringLiteral("Diagram \"%1\" container presentation %2 "
                                   "references missing or invalid child "
                                   "presentation %3")
                        .arg(diagram.name, container.id, childId),
                    container.id));
          continue;
        }
        if (seenChildren.contains(childId)) {
          diagnostics.append(error(
              QStringLiteral("validation"),
              QStringLiteral("Container lists a child presentation twice"),
              container.id));
          continue;
        }
        seenChildren.insert(childId);
        if (ownerByChild.contains(childId)) {
          diagnostics.append(
              error(QStringLiteral("validation"),
                    QStringLiteral("A presentation belongs to two containers"),
                    childId));
        } else {
          ownerByChild.insert(childId, container.id);
        }
      }
    }
    QSet<QString> reportedContainerCycles;
    for (const auto &container : diagram.containers) {
      QSet<QString> path;
      QString current = container.id;
      while (ownerByChild.contains(current)) {
        if (path.contains(current)) {
          if (!reportedContainerCycles.contains(current)) {
            reportedContainerCycles.insert(current);
            diagnostics.append(
                error(QStringLiteral("validation"),
                      QStringLiteral("Diagram containment contains a cycle"),
                      current));
          }
          break;
        }
        path.insert(current);
        current = ownerByChild.value(current);
      }
    }
    for (const auto &connector : diagram.connectors) {
      checkId(connector.id, QStringLiteral("connector presentation"));
      if (!relationshipIds.contains(connector.relationshipId))
        diagnostics.append(error(
            QStringLiteral("validation"),
            QStringLiteral("Diagram \"%1\" connector presentation %2 "
                           "references missing relationship %3")
                .arg(diagram.name, connector.id, connector.relationshipId),
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
      for (auto placement = connector.annotationPlacements.cbegin();
           placement != connector.annotationPlacements.cend(); ++placement) {
        const auto &value = placement.value();
        if (!std::isfinite(value.routePosition) || value.routePosition < 0.0 ||
            value.routePosition > 1.0 || !std::isfinite(value.tangentOffset) ||
            !std::isfinite(value.normalOffset)) {
          diagnostics.append(
              error(QStringLiteral("validation"),
                    QStringLiteral(
                        "Connector annotation placement %1 must contain finite "
                        "offsets and a route position between 0 and 1")
                        .arg(placement.key()),
                    connector.id));
        }
      }
    }
  }
  return diagnostics;
}

} // namespace yauml
