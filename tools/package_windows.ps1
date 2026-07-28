[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",

    [string]$Version,

    [string]$OutputDirectory,

    [switch]$RequireProjectVersionMatch,

    [switch]$Verify
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot ".."))
Import-Module (Join-Path $PSScriptRoot "WindowsPackaging.psm1") -Force

function Find-QtRoot {
    param([string]$ResolvedBuildDirectory)

    if ($env:QT_ROOT_DIR) {
        $candidate = [System.IO.Path]::GetFullPath($env:QT_ROOT_DIR)
        if (Test-Path -LiteralPath (Join-Path $candidate "bin\windeployqt.exe")) {
            return $candidate
        }
    }

    $cachePath = Join-Path $ResolvedBuildDirectory "CMakeCache.txt"
    if (Test-Path -LiteralPath $cachePath) {
        $qtEntry = Select-String -LiteralPath $cachePath `
            -Pattern '^Qt6_DIR:PATH=(.+)$' | Select-Object -First 1
        if ($qtEntry) {
            $qtCmakeDirectory = $qtEntry.Matches[0].Groups[1].Value
            $candidate = [System.IO.Path]::GetFullPath(
                (Join-Path $qtCmakeDirectory "..\..\.."))
            if (Test-Path -LiteralPath `
                    (Join-Path $candidate "bin\windeployqt.exe")) {
                return $candidate
            }
        }
    }

    $deployCommand = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($deployCommand) {
        return Split-Path (Split-Path $deployCommand.Source -Parent) -Parent
    }

    throw "Could not find windeployqt. Set QT_ROOT_DIR or configure with Qt."
}

function Deploy-MsvcRuntime {
    param([string]$Destination)

    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} `
            "Microsoft Visual Studio\Installer\vswhere.exe")
    )
    $vswhereCommand = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($vswhereCommand) {
        $vswhereCandidates += $vswhereCommand.Source
    }
    $vswhereCandidates =
        $vswhereCandidates | Where-Object { Test-Path -LiteralPath $_ }
    $vswhere = $vswhereCandidates | Select-Object -First 1
    if (-not $vswhere) {
        throw "Could not find vswhere.exe to locate the MSVC runtime."
    }

    $visualStudioRoot = (& $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1)
    if (-not $visualStudioRoot) {
        throw "Could not find a Visual Studio installation with x64 C++ tools."
    }

    $redistRoot = Join-Path $visualStudioRoot "VC\Redist\MSVC"
    $redistVersion = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Where-Object { $_.Name -match '^\d+(\.\d+)+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1
    if (-not $redistVersion) {
        throw "Could not find a versioned MSVC redistributable directory."
    }

    $crtDirectory = Get-ChildItem `
        -LiteralPath (Join-Path $redistVersion.FullName "x64") -Directory |
        Where-Object { $_.Name -like "Microsoft.VC*.CRT" } |
        Select-Object -First 1
    if (-not $crtDirectory) {
        throw "Could not find the x64 MSVC CRT redistributable directory."
    }

    Copy-Item -Path (Join-Path $crtDirectory.FullName "*.dll") `
        -Destination $Destination
}

function Assert-PackageContents {
    param([string]$PackageDirectory)

    $requiredFiles = @(
        "yauml.exe",
        "libclang.dll",
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Qml.dll",
        "Qt6Quick.dll",
        "Qt6QuickControls2.dll",
        "msvcp140.dll",
        "vcruntime140.dll",
        "platforms\qwindows.dll"
    )
    foreach ($relativePath in $requiredFiles) {
        $fullPath = Join-Path $PackageDirectory $relativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            throw "Required package runtime file is missing: $relativePath"
        }
    }
}

$resolvedBuildDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot $BuildDirectory))
$projectVersion = Get-YaumlProjectVersion -RepositoryRoot $repositoryRoot
if (-not $Version) {
    $Version = $projectVersion
}

$versionBase = $Version.Split("-", 2)[0]
if ($RequireProjectVersionMatch -and $versionBase -ne $projectVersion) {
    throw "Package version '$Version' does not match CMake project version " +
        "'$projectVersion'. Update project(... VERSION ...) before tagging."
}
if ($Version -notmatch '^[0-9A-Za-z][0-9A-Za-z.+-]*$') {
    throw "Package version '$Version' is not safe for an artifact name."
}

if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot "out\packages"
}
$resolvedOutputDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot $OutputDirectory))
New-Item -ItemType Directory -Force -Path $resolvedOutputDirectory | Out-Null

$binaryDirectory = Join-Path $resolvedBuildDirectory $Configuration
$sourceExecutable = Join-Path $binaryDirectory "yauml.exe"
$sourceLibClang = Join-Path $binaryDirectory "libclang.dll"
if (-not (Test-Path -LiteralPath $sourceExecutable)) {
    throw "Application executable was not found at '$sourceExecutable'."
}
if (-not (Test-Path -LiteralPath $sourceLibClang)) {
    throw "libclang.dll was not found at '$sourceLibClang'. " +
        "Release packages must include C++ import support."
}

$packageBaseName = "yauml-$Version-windows-x64"
$archivePath = Join-Path $resolvedOutputDirectory "$packageBaseName.zip"
$checksumPath = "$archivePath.sha256"
$temporaryBase = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath())
$temporaryRoot = Join-Path $temporaryBase `
    ("yauml-package-" + [guid]::NewGuid().ToString("N"))
$stagingDirectory = Join-Path $temporaryRoot $packageBaseName

try {
    New-Item -ItemType Directory -Force -Path $stagingDirectory | Out-Null
    Copy-Item -LiteralPath $sourceExecutable -Destination $stagingDirectory
    Copy-Item -LiteralPath $sourceLibClang -Destination $stagingDirectory
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "README.md") `
        -Destination $stagingDirectory
    $noticesPath = Join-Path $repositoryRoot "THIRD_PARTY_NOTICES.md"
    if (Test-Path -LiteralPath $noticesPath) {
        Copy-Item -LiteralPath $noticesPath -Destination $stagingDirectory
    }

    $qtRoot = Find-QtRoot -ResolvedBuildDirectory $resolvedBuildDirectory
    $deployTool = Join-Path $qtRoot "bin\windeployqt.exe"
    & $deployTool `
        --release `
        --verbose 0 `
        --qmldir (Join-Path $repositoryRoot "qml") `
        --no-translations `
        --no-compiler-runtime `
        --dir $stagingDirectory `
        (Join-Path $stagingDirectory "yauml.exe")
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE."
    }
    Deploy-MsvcRuntime -Destination $stagingDirectory
    Assert-PackageContents -PackageDirectory $stagingDirectory

    if ($Verify) {
        $stagedExecutable = Join-Path $stagingDirectory "yauml.exe"
        $previousPath = $env:PATH
        $previousQpaPlatform = $env:QT_QPA_PLATFORM
        $previousQuickBackend = $env:QT_QUICK_BACKEND
        try {
            # Avoid finding Qt or compiler runtime DLLs through the developer
            # environment. The package must be self-contained apart from
            # Windows system components.
            $env:PATH = @(
                $stagingDirectory,
                $env:SystemRoot,
                (Join-Path $env:SystemRoot "System32"),
                (Join-Path $env:SystemRoot "System32\Wbem")
            ) -join [System.IO.Path]::PathSeparator

            & $stagedExecutable validate `
                (Join-Path $repositoryRoot "examples\sample.yauml")
            if ($LASTEXITCODE -ne 0) {
                throw "The packaged application's validation smoke test failed."
            }

            $env:QT_QPA_PLATFORM = "windows"
            $env:QT_QUICK_BACKEND = "software"
            & $stagedExecutable --smoke-test
            if ($LASTEXITCODE -ne 0) {
                throw "The packaged application's UI smoke test failed."
            }
        } finally {
            $env:PATH = $previousPath
            $env:QT_QPA_PLATFORM = $previousQpaPlatform
            $env:QT_QUICK_BACKEND = $previousQuickBackend
        }
    }

    Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $checksumPath -Force `
        -ErrorAction SilentlyContinue
    Compress-Archive -LiteralPath $stagingDirectory `
        -DestinationPath $archivePath -CompressionLevel Optimal

    $hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    "$($hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($archivePath))" |
        Set-Content -LiteralPath $checksumPath -Encoding ascii
} finally {
    $resolvedTemporaryRoot = [System.IO.Path]::GetFullPath($temporaryRoot)
    if ($resolvedTemporaryRoot.StartsWith(
            $temporaryBase,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTemporaryRoot)) {
        Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force
    }
}

Write-Host "Created $archivePath"
Write-Host "Created $checksumPath"
