#include "core/source_editor_controller.h"

#include "core/application_settings.h"
#include "core/project_controller.h"
#include "core/project_data.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <utility>

namespace yauml {
namespace {

QString absoluteSourcePath(const QString &sourcePath,
                           const QString &projectPath) {
  const QString trimmed = sourcePath.trimmed();
  if (trimmed.isEmpty())
    return {};

  const QFileInfo sourceInfo(trimmed);
  if (sourceInfo.isAbsolute())
    return QDir::cleanPath(sourceInfo.absoluteFilePath());

  const QString basePath =
      projectPath.isEmpty() ? QDir::currentPath() : projectPath;
  return QDir(basePath).absoluteFilePath(trimmed);
}

QString quotedCommandArgument(const QString &argument) {
  if (!argument.contains(QRegularExpression(QStringLiteral("[\\s\"]"))))
    return argument;
  QString escaped = argument;
  escaped.replace(u'"', QStringLiteral("\\\""));
  return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

} // namespace

SourceEditorController::SourceEditorController(ProjectController *project,
                                               ApplicationSettings *settings,
                                               QObject *parent)
    : QObject(parent), m_project(project), m_settings(settings) {}

QVariantMap SourceEditorController::sourceLocation(const QString &objectKind,
                                                   const QString &objectId,
                                                   int operationIndex) const {
  const Location location =
      resolveLocation(objectKind, objectId, operationIndex);
  if (location.filePath.isEmpty())
    return {};
  return {{QStringLiteral("file"), location.filePath},
          {QStringLiteral("line"), location.line},
          {QStringLiteral("column"), location.column},
          {QStringLiteral("exists"), QFileInfo::exists(location.filePath)}};
}

bool SourceEditorController::canOpenObject(const QString &objectKind,
                                           const QString &objectId,
                                           int operationIndex) const {
  const Location location =
      resolveLocation(objectKind, objectId, operationIndex);
  return !location.filePath.isEmpty() && QFileInfo::exists(location.filePath);
}

bool SourceEditorController::openObject(const QString &objectKind,
                                        const QString &objectId,
                                        int operationIndex) {
  const Location location =
      resolveLocation(objectKind, objectId, operationIndex);
  if (location.filePath.isEmpty()) {
    warn(tr("No imported source location is available for this item."),
         objectId);
    return false;
  }
  if (!QFileInfo::exists(location.filePath)) {
    warn(tr("The source file no longer exists: %1").arg(location.filePath),
         objectId);
    return false;
  }
  return launch(location);
}

SourceEditorController::Location
SourceEditorController::resolveLocation(const QString &objectKind,
                                        const QString &objectId,
                                        int operationIndex) const {
  if (!m_project || objectId.isEmpty())
    return {};

  const auto fromBinding = [this](const QJsonObject &binding) {
    Location result;
    result.filePath =
        absoluteSourcePath(binding.value(QStringLiteral("file")).toString(),
                           m_project->projectPath());
    result.line = qMax(1, binding.value(QStringLiteral("line")).toInt(1));
    result.column = qMax(1, binding.value(QStringLiteral("column")).toInt(1));
    return result;
  };

  if (objectKind == QStringLiteral("element")) {
    const ModelElement *element = findElement(m_project->data(), objectId);
    if (!element)
      return {};
    if (operationIndex >= 0 && operationIndex < element->operations.size()) {
      const ModelOperation &operation = element->operations.at(operationIndex);
      if (!operation.sourceFile.trimmed().isEmpty()) {
        return {
            absoluteSourcePath(operation.sourceFile, m_project->projectPath()),
            qMax(1, operation.sourceLine), qMax(1, operation.sourceColumn)};
      }
    }
    return fromBinding(
        element->extra.value(QStringLiteral("sourceBinding")).toObject());
  }

  if (objectKind == QStringLiteral("relationship")) {
    const Relationship *relationship =
        findRelationship(m_project->data(), objectId);
    if (!relationship)
      return {};
    return fromBinding(
        relationship->extra.value(QStringLiteral("sourceBinding")).toObject());
  }

  return {};
}

QString
SourceEditorController::resolveExecutable(const QString &program) const {
  const QFileInfo configuredInfo(program);
  if (configuredInfo.isAbsolute() && configuredInfo.isFile())
    return configuredInfo.absoluteFilePath();

  QString resolved = QStandardPaths::findExecutable(program);
#ifdef Q_OS_WIN
  // The shell launcher installed on PATH is commonly code.cmd. Starting the
  // sibling GUI executable directly avoids a console flash and cmd quoting
  // rules when the standard VS Code installation layout is available.
  if (!resolved.isEmpty() &&
      QFileInfo(resolved).suffix().compare(QStringLiteral("cmd"),
                                           Qt::CaseInsensitive) == 0) {
    const QFileInfo guiCandidate(
        QDir(QFileInfo(resolved).absolutePath())
            .absoluteFilePath(QStringLiteral("../Code.exe")));
    if (guiCandidate.isFile())
      return guiCandidate.absoluteFilePath();
  }

  if (program.compare(QStringLiteral("code"), Qt::CaseInsensitive) == 0) {
    const QStringList candidates = {
        QDir(qEnvironmentVariable("LOCALAPPDATA"))
            .absoluteFilePath(
                QStringLiteral("Programs/Microsoft VS Code/Code.exe")),
        QDir(qEnvironmentVariable("ProgramFiles"))
            .absoluteFilePath(QStringLiteral("Microsoft VS Code/Code.exe")),
        QDir(qEnvironmentVariable("ProgramFiles(x86)"))
            .absoluteFilePath(QStringLiteral("Microsoft VS Code/Code.exe"))};
    for (const QString &candidate : candidates) {
      if (QFileInfo(candidate).isFile())
        return QDir::cleanPath(candidate);
    }
  }
#endif
  return resolved;
}

bool SourceEditorController::launch(const Location &location) {
  const QString configuredCommand =
      (m_settings ? m_settings->sourceEditorCommand() : QStringLiteral("code"))
          .trimmed();
  // A pasted executable path containing spaces is the common Windows setup.
  // Accept it without requiring shell-style quotes; only parse arguments when
  // the complete preference value is not itself an existing file.
  QStringList command = QFileInfo(configuredCommand).isFile()
                            ? QStringList{configuredCommand}
                            : QProcess::splitCommand(configuredCommand);
  if (command.isEmpty()) {
    warn(
        tr("The source editor command is empty. Configure it in Preferences."));
    return false;
  }

  const QString configuredProgram = command.takeFirst();
  const QString executable = resolveExecutable(configuredProgram);
  if (executable.isEmpty()) {
    warn(tr("Cannot find the source editor command “%1”. Configure VS Code in "
            "Preferences.")
             .arg(configuredProgram));
    return false;
  }

  command.append(QStringLiteral("--reuse-window"));
  command.append(QStringLiteral("--goto"));
  command.append(QStringLiteral("%1:%2:%3")
                     .arg(QDir::toNativeSeparators(location.filePath))
                     .arg(location.line)
                     .arg(location.column));

#ifdef Q_OS_WIN
  const QString suffix = QFileInfo(executable).suffix().toLower();
  if (suffix == QStringLiteral("cmd") || suffix == QStringLiteral("bat")) {
    QString commandLine = quotedCommandArgument(executable);
    for (const QString &argument : std::as_const(command))
      commandLine += u' ' + quotedCommandArgument(argument);
    if (QProcess::startDetached(QStringLiteral("cmd.exe"),
                                {QStringLiteral("/d"), QStringLiteral("/s"),
                                 QStringLiteral("/c"), commandLine}))
      return true;
  } else
#endif
  {
    if (QProcess::startDetached(executable, command))
      return true;
  }

  warn(tr("Could not start the source editor command “%1”.").arg(executable));
  return false;
}

void SourceEditorController::warn(const QString &message,
                                  const QString &objectId) const {
  if (m_project)
    m_project->diagnostics()->addWarning(QStringLiteral("source-editor"),
                                         message, objectId);
}

} // namespace yauml
