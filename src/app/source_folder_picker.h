#pragma once

#include <QObject>
#include <QStringList>

class QWindow;

namespace yauml::ui {

// Qt Quick's FolderDialog selects one directory. The Windows shell supports
// selecting several folders in one native picker, which is important when a
// project should synchronize only chosen source subtrees.
class SourceFolderPicker final : public QObject {
  Q_OBJECT

public:
  explicit SourceFolderPicker(QObject *parent = nullptr);

  Q_INVOKABLE void open(QObject *parentWindow,
                        const QStringList &initialFolders = {});

signals:
  void foldersSelected(const QStringList &folders);
  void fallbackRequested();
  void errorOccurred(const QString &message);
};

} // namespace yauml::ui
