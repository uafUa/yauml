# Building and publishing releases

The repository's `Windows CI and Release` GitHub Actions workflow is the
authoritative Windows build. It uses the same MSVC Release configuration and
complete CTest suite as local acceptance testing.

## Continuous artifacts

Every pull request and every push to `main` performs a clean build with:

- Windows Server 2022 and MSVC 2022;
- Qt 6.10.3 for `win64_msvc2022_64`;
- the runner's LLVM/libclang installation, required rather than optional;
- all core, canvas, headless, import, UI smoke, and source-policy tests.

Successful runs upload a tested portable ZIP and its SHA-256 checksum to the
workflow run for 14 days, together with a tested offline Windows installer and
its checksum. Open the run's **Artifacts** section to download them. These
development packages use names such as:

- `yauml-dev-1a2b3c4d-windows-x64.zip`;
- `yauml-dev-1a2b3c4d-windows-x64-installer.exe`.

## Tagged releases

An annotated tag matching `v*` builds and tests exactly like `main`, then
publishes the resulting ZIP, installer, and checksums as a permanent GitHub
Release. Release tags and `project(... VERSION ...)` in `CMakeLists.txt` must
agree. Pre-release suffixes are allowed, for example `v0.2.0-rc.1`.

To publish version `0.2.0` after its version change is merged to `main`:

```powershell
git switch main
git pull --ff-only
git tag -a v0.2.0 -m "yauml 0.2.0"
git push origin v0.2.0
```

Do not move or reuse a published version tag. If a release build fails, fix
the cause on `main`, advance the project version, and publish a new tag.

## Local packaging

After a local MSVC Release build with libclang:

```powershell
.\tools\package_windows.ps1 `
  -BuildDirectory build-release `
  -Configuration Release `
  -Version 0.1.0 `
  -RequireProjectVersionMatch `
  -Verify
```

The script creates `out/packages/yauml-<version>-windows-x64.zip` plus a
checksum. It stages only runtime files in a temporary directory, invokes
`windeployqt`, requires `libclang.dll`, and runs validation and UI smoke tests
against the packaged executable before creating the archive.

To create the offline Windows installer from that verified archive:

```powershell
.\tools\build_windows_installer.ps1 `
  -PortableArchive .\out\packages\yauml-0.1.0-windows-x64.zip `
  -Version 0.1.0 `
  -Verify
```

This requires Qt Installer Framework 4.11 or newer. The script finds
`binarycreator.exe` through `QIFW_ROOT`, `PATH`, the GitHub Actions Qt tools
directory, or a standard `C:\Qt\Tools\QtInstallerFramework` installation. It
verifies the portable archive's checksum, builds an offline installer, and
then exercises the complete lifecycle in a temporary location: initial
installation, Windows uninstall registration, replacement by the same
installer, application validation, and removal.

The interactive installer installs to the 64-bit Windows applications
directory by default. The application component creates a Start-menu shortcut
and associates `.uuml` projects with uuml. A separately selectable component
controls the desktop shortcut.

Running a newer uuml installer over an existing registered installation asks
for confirmation, removes the old application files, and installs into the
same directory. Projects stored outside that directory and user preferences
are preserved. The Windows **Installed apps** entry opens the uninstaller
directly; package modification is intentionally not offered because the
offline distribution has no update repository.

The installer is currently unsigned, so Windows may show an unknown-publisher
warning until release code signing is introduced.

When testing an undeployed Windows build tree manually, use `ctest` rather than
starting the test executables directly. CTest supplies the configured Qt kit's
runtime directory before launching each test.

## Repository settings

Once the first workflow run succeeds, protect `main` by requiring the
`Windows / build, test, and package` check before merging. The build job has
read-only repository access. Only the tag-only release job receives
`contents: write`, using GitHub's short-lived repository token; no custom
secrets are required.
