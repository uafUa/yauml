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
workflow run for 14 days. Open the run's **Artifacts** section to download it.
These development packages use a name such as
`yauml-dev-1a2b3c4d-windows-x64.zip`.

## Tagged releases

An annotated tag matching `v*` builds and tests exactly like `main`, then
publishes the resulting ZIP and checksum as a permanent GitHub Release.
Release tags and `project(... VERSION ...)` in `CMakeLists.txt` must agree.
Pre-release suffixes are allowed, for example `v0.2.0-rc.1`.

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

When testing an undeployed Windows build tree manually, use `ctest` rather than
starting the test executables directly. CTest supplies the configured Qt kit's
runtime directory before launching each test.

## Repository settings

Once the first workflow run succeeds, protect `main` by requiring the
`Windows / build, test, and package` check before merging. The build job has
read-only repository access. Only the tag-only release job receives
`contents: write`, using GitHub's short-lived repository token; no custom
secrets are required.
