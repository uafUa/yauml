[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PortableArchive,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$OutputDirectory,

    [string]$IfwRoot,

    [switch]$Verify
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot ".."))
Import-Module (Join-Path $PSScriptRoot "WindowsPackaging.psm1") -Force

function Find-IfwBinDirectory {
    param([string]$ExplicitRoot)

    $candidates = [System.Collections.Generic.List[string]]::new()
    foreach ($root in @($ExplicitRoot, $env:QIFW_ROOT)) {
        if (-not [string]::IsNullOrWhiteSpace($root)) {
            $candidates.Add($root)
            $candidates.Add((Join-Path $root "bin"))
        }
    }

    $binaryCreatorCommand =
        Get-Command binarycreator.exe -ErrorAction SilentlyContinue
    if ($binaryCreatorCommand) {
        $candidates.Add((Split-Path $binaryCreatorCommand.Source -Parent))
    }

    if ($env:IQTA_TOOLS -and (Test-Path -LiteralPath $env:IQTA_TOOLS)) {
        Get-ChildItem -LiteralPath $env:IQTA_TOOLS -Recurse `
            -Filter binarycreator.exe -File -ErrorAction SilentlyContinue |
            ForEach-Object { $candidates.Add($_.DirectoryName) }
    }

    $localIfwRoot = "C:\Qt\Tools\QtInstallerFramework"
    if (Test-Path -LiteralPath $localIfwRoot) {
        Get-ChildItem -LiteralPath $localIfwRoot -Directory |
            Sort-Object {
                $parsedVersion = $null
                if ([version]::TryParse($_.Name, [ref]$parsedVersion)) {
                    return $parsedVersion
                }
                return [version]"0.0"
            } -Descending |
            ForEach-Object { $candidates.Add((Join-Path $_.FullName "bin")) }
    }

    foreach ($candidate in $candidates) {
        $resolvedCandidate = [System.IO.Path]::GetFullPath($candidate)
        $binaryCreator = Join-Path $resolvedCandidate "binarycreator.exe"
        $installerBase = Join-Path $resolvedCandidate "installerbase.exe"
        if ((Test-Path -LiteralPath $binaryCreator -PathType Leaf) -and
            (Test-Path -LiteralPath $installerBase -PathType Leaf)) {
            return $resolvedCandidate
        }
    }

    throw "Qt Installer Framework was not found. Install it or set QIFW_ROOT."
}

function Expand-InstallerTemplate {
    param(
        [string]$TemplatePath,
        [string]$DestinationPath,
        [hashtable]$Values
    )

    $content = Get-Content -LiteralPath $TemplatePath -Raw
    foreach ($entry in $Values.GetEnumerator()) {
        $content = $content.Replace(
            "{{$($entry.Key)}}",
            [string]$entry.Value)
    }
    if ($content -match '\{\{[A-Z0-9_]+\}\}') {
        throw "Unresolved value in installer template '$TemplatePath'."
    }

    $destinationParent = Split-Path $DestinationPath -Parent
    New-Item -ItemType Directory -Force -Path $destinationParent | Out-Null
    Set-Content -LiteralPath $DestinationPath -Value $content -Encoding utf8
}

function Assert-ArchiveChecksum {
    param([string]$ArchivePath)

    $checksumPath = "$ArchivePath.sha256"
    if (-not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) {
        throw "Portable package checksum was not found at '$checksumPath'."
    }

    $expected = (Get-Content -LiteralPath $checksumPath -Raw).
        Trim().Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)[0]
    $actual = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).
        Hash.ToLowerInvariant()
    if ($actual -ne $expected.ToLowerInvariant()) {
        throw "Portable package checksum does not match '$checksumPath'."
    }
}

if ($Version -notmatch '^[0-9A-Za-z][0-9A-Za-z.+-]*$') {
    throw "Installer version '$Version' is not safe for an artifact name."
}

$projectVersion = Get-UumlProjectVersion -RepositoryRoot $repositoryRoot
$resolvedArchive = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot $PortableArchive))
if (-not (Test-Path -LiteralPath $resolvedArchive -PathType Leaf)) {
    throw "Portable package was not found at '$resolvedArchive'."
}
Assert-ArchiveChecksum -ArchivePath $resolvedArchive

if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot "out\packages"
}
$resolvedOutputDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot $OutputDirectory))
New-Item -ItemType Directory -Force -Path $resolvedOutputDirectory | Out-Null

$ifwBinDirectory = Find-IfwBinDirectory -ExplicitRoot $IfwRoot
$binaryCreator = Join-Path $ifwBinDirectory "binarycreator.exe"
$installerBase = Join-Path $ifwBinDirectory "installerbase.exe"
$installerName = "yauml-$Version-windows-x64-installer.exe"
$installerPath = Join-Path $resolvedOutputDirectory $installerName
$checksumPath = "$installerPath.sha256"

$temporaryBase = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath())
$temporaryRoot = Join-Path $temporaryBase `
    ("yauml-installer-" + [guid]::NewGuid().ToString("N"))
$extractedDirectory = Join-Path $temporaryRoot "portable"
$installerWorkspace = Join-Path $temporaryRoot "ifw"
$verificationRoot = Join-Path $temporaryRoot "installed"

try {
    New-Item -ItemType Directory -Force -Path $extractedDirectory |
        Out-Null
    Expand-Archive -LiteralPath $resolvedArchive `
        -DestinationPath $extractedDirectory

    $archiveEntries = @(Get-ChildItem -LiteralPath $extractedDirectory)
    $expectedArchiveRoot = "yauml-$Version-windows-x64"
    if ($archiveEntries.Count -ne 1 -or
        -not $archiveEntries[0].PSIsContainer -or
        $archiveEntries[0].Name -ne $expectedArchiveRoot) {
        throw "Portable package must contain exactly '$expectedArchiveRoot'."
    }

    $sourceDefinition = Join-Path $repositoryRoot "installer\windows"
    Copy-Item -LiteralPath $sourceDefinition -Destination $installerWorkspace `
        -Recurse

    $templateValues = @{
        PRODUCT_VERSION = $projectVersion
        RELEASE_DATE = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd")
    }
    Get-ChildItem -LiteralPath $installerWorkspace -Recurse `
        -Filter "*.in" -File |
        ForEach-Object {
            $destination = $_.FullName.Substring(
                0,
                $_.FullName.Length - ".in".Length)
            Expand-InstallerTemplate -TemplatePath $_.FullName `
                -DestinationPath $destination -Values $templateValues
            Remove-Item -LiteralPath $_.FullName -Force
        }

    $applicationData = Join-Path $installerWorkspace `
        "packages\io.github.uafua.yauml\data"
    New-Item -ItemType Directory -Force -Path $applicationData | Out-Null
    Get-ChildItem -LiteralPath $archiveEntries[0].FullName |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $applicationData `
                -Recurse
        }

    Remove-Item -LiteralPath $installerPath -Force `
        -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $checksumPath -Force `
        -ErrorAction SilentlyContinue
    & $binaryCreator `
        --offline-only `
        --template $installerBase `
        --packages (Join-Path $installerWorkspace "packages") `
        --config (Join-Path $installerWorkspace "config\config.xml") `
        $installerPath
    if ($LASTEXITCODE -ne 0) {
        throw "binarycreator failed with exit code $LASTEXITCODE."
    }
    if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
        throw "Qt Installer Framework did not create '$installerPath'."
    }

    if ($Verify) {
        & $installerPath `
            --root $verificationRoot `
            --accept-messages `
            --accept-licenses `
            --confirm-command `
            install io.github.uafua.yauml `
            UumlSkipShellIntegration=true
        if ($LASTEXITCODE -ne 0) {
            throw "The generated installer's headless installation failed."
        }

        $installedExecutable = Join-Path $verificationRoot "uuml.exe"
        & $installedExecutable validate `
            (Join-Path $repositoryRoot "examples\sample.uuml")
        if ($LASTEXITCODE -ne 0) {
            throw "The installed application's validation check failed."
        }

        $maintenanceTool = Join-Path $verificationRoot "maintenancetool.exe"
        & $maintenanceTool `
            --accept-messages `
            --confirm-command `
            purge
        if ($LASTEXITCODE -ne 0) {
            throw "The generated uninstaller failed to remove the test install."
        }
        if (Test-Path -LiteralPath $installedExecutable) {
            throw "The test installation still contains uuml.exe after purge."
        }
    }

    $hash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash
    "$($hash.ToLowerInvariant())  $installerName" |
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

Write-Host "Created $installerPath"
Write-Host "Created $checksumPath"
