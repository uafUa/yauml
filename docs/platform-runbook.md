# Platform runbook

This runbook contains commands and external prerequisites needed to reproduce a
yauml development or distribution workflow on another machine. It intentionally
contains no credentials, private keys, or machine-specific secret values.

## Supported and verification platforms

- **Windows 11 x64** with MSVC 2022 is the development, test, package, and
  release platform.
- **GitHub Actions Windows Server 2022** is the authoritative clean build.
- **WSL 2 / Linux** is currently required only for repository portability
  checks. Native Linux application packaging is not a release gate and remains
  future work.

## Values obtained outside Git

Set these for the local installation rather than committing a machine path:

| Value | Source | Required for |
| --- | --- | --- |
| Qt kit root, e.g. `C:\Qt\6.11.1\msvc2022_64` | Qt online installer | Configure, build, launch |
| LLVM root containing `bin\libclang.dll` | LLVM installer or Visual Studio LLVM component | C++ import and accepted build |
| `QIFW_ROOT` containing `bin\binarycreator.exe`, IFW 4.11+ | Qt Installer Framework | Installer/update repository |
| Git remote credentials | User's Git credential manager or SSH agent | Fetch/push/release operations |
| GitHub Pages source = **GitHub Actions** | Repository settings | Stable update-site deployment |

No Apple team ID, mobile device ID, provisioning profile, or LAN endpoint is
used by the current Windows product. CI uses GitHub's short-lived repository
token and requires no custom build secret. Windows binaries are currently
unsigned. A future signing setup will require a certificate and its unlock
secret supplied by the CI secret store; neither may be committed.

## Windows configure, build, test, and launch

Run in PowerShell 7 from the repository root:

```powershell
$YaumlQtRoot = 'C:\Qt\6.11.1\msvc2022_64'
$env:LLVM_ROOT = 'C:\Program Files\LLVM'

if (-not (Test-Path -LiteralPath "$YaumlQtRoot\bin\qmake.exe")) {
  throw "Qt MSVC kit was not found at $YaumlQtRoot"
}
if (-not (Test-Path -LiteralPath "$env:LLVM_ROOT\bin\libclang.dll")) {
  throw "libclang.dll was not found below LLVM_ROOT"
}

cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="$YaumlQtRoot" `
  -DYAUML_REQUIRE_LIBCLANG=ON
cmake --build build-release --config Release --parallel
ctest --test-dir build-release -C Release --output-on-failure

.\build-release\Release\yauml.exe
.\build-release\Release\yauml-cli.exe validate .\examples\sample.yauml
```

Success means CMake reports a usable Qt kit and libclang, all 11 current CTest
entries pass, the desktop process opens without a console window, and the CLI
validation exits with code 0.

If `sample.yauml` is not the current example path, use an existing project
directory listed by `Get-ChildItem examples`; do not invent or edit fixture
data merely to satisfy the command.

## Portable package and installer

After the complete Release test pass:

```powershell
$YaumlVersion = '0.13.0'
$env:QIFW_ROOT = 'C:\Qt\Tools\QtInstallerFramework\4.11'

.\tools\package_windows.ps1 `
  -BuildDirectory build-release `
  -Configuration Release `
  -Version $YaumlVersion `
  -RequireProjectVersionMatch `
  -Verify

.\tools\build_windows_installer.ps1 `
  -PortableArchive ".\out\packages\yauml-$YaumlVersion-windows-x64.zip" `
  -Version $YaumlVersion `
  -UpdateSiteDirectory .\out\update-site `
  -ReleaseNotesUrl "https://github.com/uafUa/yauml/releases/tag/v$YaumlVersion" `
  -Verify
```

Expected outputs are the portable ZIP, installer EXE, SHA-256 files, and an IFW
update repository below `out/`. Verification exercises portable runtime files,
install, replacement install, application validation, Windows uninstall
registration, and removal.

To test the portable build without installation:

```powershell
$PackageName = "yauml-$YaumlVersion-windows-x64"
$Package = ".\out\packages\$PackageName.zip"
$Scratch = Join-Path $env:TEMP "yauml-portable-$YaumlVersion"
New-Item -ItemType Directory -Force -Path $Scratch | Out-Null
7z x $Package "-o$Scratch" -y
& (Join-Path $Scratch "$PackageName\yauml.exe")
```

To install, launch the generated installer interactively. After installation,
start **yauml** from the Start menu; use Windows Control Panel > Programs or the
installed `maintenancetool.exe` to remove/update it.

## Common failures

| Symptom | Likely cause and correction |
| --- | --- |
| CMake cannot find Qt6 | `CMAKE_PREFIX_PATH` is not the MSVC Qt kit root, or Qt architecture does not match x64 MSVC |
| `YAUML_REQUIRE_LIBCLANG` fails | Install LLVM/libclang and set `LLVM_ROOT`; do not disable the requirement for acceptance |
| `Qt6Gui.dll` is missing | An undeployed build/test executable was started outside its configured environment; run through CTest or package with `windeployqt` |
| No Qt platform plugin can initialize | Runtime/plugin paths are incomplete; verify through the packaging script rather than copying an EXE alone |
| IFW 4.11 required/found older version | Point `QIFW_ROOT` at the 4.11+ installation used by `binarycreator.exe` |
| Visual regression differs only on CI | Download expected/actual/difference artifacts and inspect them before changing tolerance or baseline |
| Windows unknown-publisher warning | Expected while releases are unsigned; do not work around it by committing a certificate |

## Windows Git and EOL verification

Run before submission:

```powershell
git status --short
git diff --check
git diff --cached --check
git ls-files --eol
```

The index should report LF for text. `.bat` and `.cmd` may materialize as CRLF;
binary files should report `attr/-text`. A clean-worktree renormalization audit
may use `git add --renormalize .`, but inspect the staged stat and never mix a
repository-wide normalization with functional changes.

## WSL/Linux repository verification

From WSL 2, use a native clone for the strongest cross-OS check. A mounted
Windows worktree is acceptable only for comparing the same local dirty state:

```bash
cd /mnt/d/DatWork/uaf/2026/yauml
git status --short
git diff --check
git diff --cached --check
git ls-files --eol
```

The status and `diff --check` outcome must agree with Windows. Different
worktree EOL columns are acceptable only where `.gitattributes` explicitly
requires them; index EOL and canonical hashes must agree.

After the handover documentation has been explicitly submitted and pushed,
verify remote availability with a disposable WSL clone:

```bash
scratch="$(mktemp -d)"
git clone --depth 1 https://github.com/uafUa/yauml.git "$scratch/yauml"
cd "$scratch/yauml"
test -f AGENTS.md
test -f docs/development-context.md
test -f docs/current-handover.md
git ls-files --error-unmatch AGENTS.md \
  docs/development-context.md docs/current-handover.md
git diff --check
```

This fresh-clone check cannot pass before the files are committed and pushed;
local untracked documentation must never be described as portable.
