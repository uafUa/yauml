#include "core/diagnostic_model.h"

namespace yauml {

DiagnosticModel::DiagnosticModel(QObject *parent)
    : QAbstractListModel(parent) {}

int DiagnosticModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_items.size();
}

QVariant DiagnosticModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
    return {};

  const auto &item = m_items.at(index.row());
  switch (role) {
  case SeverityRole:
    return toString(item.severity);
  case CategoryRole:
    return item.category;
  case MessageRole:
    return item.message;
  case ElementIdRole:
    return item.elementId;
  case TimestampRole:
    return item.timestamp;
  default:
    return {};
  }
}

QHash<int, QByteArray> DiagnosticModel::roleNames() const {
  return {{SeverityRole, "severity"},
          {CategoryRole, "category"},
          {MessageRole, "message"},
          {ElementIdRole, "elementId"},
          {TimestampRole, "timestamp"}};
}

void DiagnosticModel::add(const Diagnostic &diagnostic) {
  const int row = m_items.size();
  beginInsertRows({}, row, row);
  m_items.append(diagnostic);
  endInsertRows();
  emit countChanged();
  if (diagnostic.severity != DiagnosticSeverity::Info)
    emit attentionAdded();
  if (diagnostic.severity == DiagnosticSeverity::Error)
    emit errorAdded();
}

void DiagnosticModel::addInfo(const QString &category, const QString &message,
                              const QString &elementId) {
  add({DiagnosticSeverity::Info, category, message, elementId});
}

void DiagnosticModel::addWarning(const QString &category,
                                 const QString &message,
                                 const QString &elementId) {
  add({DiagnosticSeverity::Warning, category, message, elementId});
}

void DiagnosticModel::addError(const QString &category, const QString &message,
                               const QString &elementId) {
  add({DiagnosticSeverity::Error, category, message, elementId});
}

void DiagnosticModel::clear() {
  if (m_items.isEmpty())
    return;
  beginResetModel();
  m_items.clear();
  endResetModel();
  emit countChanged();
}

QString toString(DiagnosticSeverity severity) {
  switch (severity) {
  case DiagnosticSeverity::Info:
    return QStringLiteral("info");
  case DiagnosticSeverity::Warning:
    return QStringLiteral("warning");
  case DiagnosticSeverity::Error:
    return QStringLiteral("error");
  }
  return QStringLiteral("info");
}

} // namespace yauml
