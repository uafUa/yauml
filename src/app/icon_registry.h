#pragma once

#include <QHash>
#include <QObject>
#include <QUrl>
#include <QVector>

namespace yauml::ui {

// Loads the editable JSON5 catalog once and exposes resource URLs to QML.
// Missing assignments deliberately return an empty URL so a partially filled
// catalog keeps the corresponding controls text-only.
class IconRegistry final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int defaultSize READ defaultSize CONSTANT)

public:
  explicit IconRegistry(QObject *parent = nullptr);

  int defaultSize() const;
  bool isValid() const;
  QStringList errors() const;

  Q_INVOKABLE QUrl actionIcon(const QString &actionId) const;
  Q_INVOKABLE QUrl projectTreeIcon(const QString &kind,
                                   const QString &objectType,
                                   const QString &objectId, bool nested,
                                   bool expanded) const;

private:
  struct TreeIcon {
    QString kind;
    QString objectType;
    QString objectId;
    bool matchesNested = false;
    bool nested = false;
    QUrl collapsedUrl;
    QUrl expandedUrl;

    int specificity() const;
    bool matches(const QString &candidateKind,
                 const QString &candidateObjectType,
                 const QString &candidateObjectId, bool candidateNested) const;
  };

  QUrl resolveSvg(const QString &relativePath, const QString &entryId);
  void load();

  int m_defaultSize = 20;
  QString m_resourcePrefix = QStringLiteral("/icons");
  QHash<QString, QUrl> m_actionIcons;
  QVector<TreeIcon> m_treeIcons;
  QStringList m_errors;
};

} // namespace yauml::ui
