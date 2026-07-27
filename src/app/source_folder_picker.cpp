#include "app/source_folder_picker.h"

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QWindow>

#ifdef Q_OS_WIN
#include <ShObjIdl.h>
#include <Windows.h>
#include <wrl/client.h>
#endif

namespace uuml::ui {

SourceFolderPicker::SourceFolderPicker(QObject *parent) : QObject(parent) {}

void SourceFolderPicker::open(QObject *parentWindow,
                              const QStringList &initialFolders) {
#ifdef Q_OS_WIN
  using Microsoft::WRL::ComPtr;

  const HRESULT initialization =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninitialize = initialization == S_OK || initialization == S_FALSE;
  if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE) {
    emit errorOccurred(
        QStringLiteral("Windows could not initialize the source-folder "
                       "picker (error 0x%1)")
            .arg(static_cast<quint32>(initialization), 8, 16,
                 QLatin1Char('0')));
    return;
  }

  const auto finishCom = qScopeGuard([uninitialize] {
    if (uninitialize)
      CoUninitialize();
  });

  ComPtr<IFileOpenDialog> dialog;
  HRESULT result =
      CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&dialog));
  if (FAILED(result)) {
    emit errorOccurred(
        QStringLiteral("Windows could not create the source-folder picker "
                       "(error 0x%1)")
            .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0')));
    return;
  }

  FILEOPENDIALOGOPTIONS options{};
  result = dialog->GetOptions(&options);
  if (SUCCEEDED(result)) {
    result =
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_ALLOWMULTISELECT |
                           FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  }
  if (FAILED(result)) {
    emit errorOccurred(
        QStringLiteral("Windows could not configure multi-folder selection "
                       "(error 0x%1)")
            .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0')));
    return;
  }
  dialog->SetTitle(L"Choose C++ source directories");

  if (!initialFolders.isEmpty()) {
    const QString initialPath =
        QFileInfo(initialFolders.first()).absoluteFilePath();
    ComPtr<IShellItem> initialFolder;
    if (SUCCEEDED(SHCreateItemFromParsingName(
            reinterpret_cast<const wchar_t *>(initialPath.utf16()), nullptr,
            IID_PPV_ARGS(&initialFolder)))) {
      dialog->SetFolder(initialFolder.Get());
    }
  }

  HWND owner = nullptr;
  if (auto *window = qobject_cast<QWindow *>(parentWindow))
    owner = reinterpret_cast<HWND>(window->winId());
  result = dialog->Show(owner);
  if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    return;
  if (FAILED(result)) {
    emit errorOccurred(
        QStringLiteral("Windows could not show the source-folder picker "
                       "(error 0x%1)")
            .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0')));
    return;
  }

  ComPtr<IShellItemArray> items;
  result = dialog->GetResults(&items);
  if (FAILED(result)) {
    emit errorOccurred(
        QStringLiteral("Windows could not read the selected source folders "
                       "(error 0x%1)")
            .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0')));
    return;
  }

  DWORD count = 0;
  items->GetCount(&count);
  QStringList selectedFolders;
  for (DWORD index = 0; index < count; ++index) {
    ComPtr<IShellItem> item;
    if (FAILED(items->GetItemAt(index, &item)))
      continue;
    PWSTR nativePath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &nativePath)) ||
        !nativePath) {
      continue;
    }
    const QString folder = QDir::cleanPath(QString::fromWCharArray(nativePath));
    CoTaskMemFree(nativePath);
    if (!folder.isEmpty() &&
        !selectedFolders.contains(folder, Qt::CaseInsensitive)) {
      selectedFolders.append(folder);
    }
  }
  if (!selectedFolders.isEmpty())
    emit foldersSelected(selectedFolders);
#else
  Q_UNUSED(parentWindow)
  Q_UNUSED(initialFolders)
  emit fallbackRequested();
#endif
}

} // namespace uuml::ui
