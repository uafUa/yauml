#pragma once

#include <QAbstractListModel>
#include <QDateTime>

namespace uuml {

enum class DiagnosticSeverity { Info, Warning, Error };

struct Diagnostic {
  DiagnosticSeverity severity = DiagnosticSeverity::Info;
  QString category;
  QString message;
  QString elementId;
  QDateTime timestamp = QDateTime::currentDateTime();
};

class DiagnosticModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
  enum Role {
    SeverityRole = Qt::UserRole + 1,
    CategoryRole,
    MessageRole,
    ElementIdRole,
    TimestampRole
  };
  Q_ENUM(Role)

  explicit DiagnosticModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void add(const Diagnostic &diagnostic);
  void addInfo(const QString &category, const QString &message,
               const QString &elementId = {});
  void addWarning(const QString &category, const QString &message,
                  const QString &elementId = {});
  void addError(const QString &category, const QString &message,
                const QString &elementId = {});
  Q_INVOKABLE void clear();

signals:
  void countChanged();
  void errorAdded();

private:
  QList<Diagnostic> m_items;
};

QString toString(DiagnosticSeverity severity);

} // namespace uuml
