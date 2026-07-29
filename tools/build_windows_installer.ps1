[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PortableArchive,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$OutputDirectory,

    [string]$IfwRoot,

    [string]$UpdateSiteDirectory,

    [string]$UpdateRepositoryUrl =
        "https://uafua.github.io/yauml/updates/windows/x64/stable",

    [string]$ReleaseNotesUrl =
        "https://github.com/uafUa/yauml/releases/latest",

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

function Assert-IfwVersion {
    param(
        [string]$InstallerBase,
        [version]$MinimumVersion
    )

    # Older IFW releases accept --version as a diagnostic even when they
    # report it as an unknown option, so parse the complete output.
    $versionOutput = (& $InstallerBase --version 2>&1 | Out-String)
    if ($versionOutput -notmatch
        'IFW Version:\s*(?<version>[0-9]+(?:\.[0-9]+){1,3})') {
        throw "Could not determine the Qt Installer Framework version."
    }
    $actualVersion = [version]$Matches.version
    if ($actualVersion -lt $MinimumVersion) {
        throw "Qt Installer Framework $MinimumVersion or newer is required; " +
            "found $actualVersion."
    }
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

function Get-YaumlUninstallEntries {
    param([string]$InstallationRoot)

    $normalizedRoot = [System.IO.Path]::GetFullPath(
        $InstallationRoot).TrimEnd(
            [System.IO.Path]::DirectorySeparatorChar,
            [System.IO.Path]::AltDirectorySeparatorChar)
    $registryRoots = @(
        "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall",
        "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall",
        "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"
    )

    foreach ($registryRoot in $registryRoots) {
        if (-not (Test-Path -LiteralPath $registryRoot)) {
            continue
        }
        foreach ($key in Get-ChildItem -LiteralPath $registryRoot) {
            $entry = Get-ItemProperty -LiteralPath $key.PSPath
            $installLocation =
                $entry.PSObject.Properties["InstallLocation"]
            if (-not $installLocation -or
                [string]::IsNullOrWhiteSpace($installLocation.Value)) {
                continue
            }
            $entryRoot = [System.IO.Path]::GetFullPath(
                $installLocation.Value).TrimEnd(
                    [System.IO.Path]::DirectorySeparatorChar,
                    [System.IO.Path]::AltDirectorySeparatorChar)
            if ($entryRoot.Equals(
                    $normalizedRoot,
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                [pscustomobject]@{
                    RegistryPath = $key.PSPath
                    Properties = $entry
                }
            }
        }
    }
}

function Assert-WindowsUninstallEntry {
    param([string]$InstallationRoot)

    $entries = @(Get-YaumlUninstallEntries -InstallationRoot $InstallationRoot)
    if ($entries.Count -ne 1) {
        throw "Expected one Windows uninstall entry for '$InstallationRoot', " +
            "but found $($entries.Count)."
    }
    $properties = $entries[0].Properties
    if ($properties.DisplayName -ne "yauml") {
        throw "The Windows uninstall entry has an unexpected display name."
    }
    if ([int]$properties.NoModify -ne 1) {
        throw "The Windows uninstall entry must disable unsupported Modify."
    }
    if ($properties.UninstallString -notmatch
        '(?i)maintenancetool\.exe"\s+--start-uninstaller') {
        throw "The Windows uninstall entry does not start uninstall mode."
    }
}

function Invoke-InstallerVerificationInstall {
    param(
        [string]$InstallerPath,
        [string]$InstallationRoot,
        [switch]$ReplaceExisting
    )

    & $InstallerPath `
        --root $InstallationRoot `
        --accept-messages `
        --accept-licenses `
        --confirm-command `
        install io.github.uafua.yauml `
        YaumlSkipShellIntegration=true
    if ($LASTEXITCODE -ne 0) {
        $action = $ReplaceExisting ? "replacement" : "installation"
        throw "The generated installer's headless $action failed."
    }
}

function Assert-SafeOutputDirectory {
    param([string]$Path)

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $relativePath = [System.IO.Path]::GetRelativePath(
        $repositoryRoot,
        $resolvedPath)
    if ([System.IO.Path]::IsPathRooted($relativePath) -or
        $relativePath -eq "." -or
        $relativePath -eq ".." -or
        $relativePath.StartsWith(
            "..$([System.IO.Path]::DirectorySeparatorChar)")) {
        throw "Update site output must be a directory inside the repository."
    }
    return $resolvedPath
}

if ($Version -notmatch '^[0-9A-Za-z][0-9A-Za-z.+-]*$') {
    throw "Installer version '$Version' is not safe for an artifact name."
}

$projectVersion = Get-YaumlProjectVersion -RepositoryRoot $repositoryRoot
$resolvedArchive = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot $PortableArchive))
if (-not (Test-Path -LiteralPath $resolvedArchive -PathType Leaf)) {
    throw "Portable package was not found at '$resolvedArchive'."
}
Assert-ArchiveChecksum -ArchivePath $resolvedArchive
if ($UpdateRepositoryUrl -notmatch '^https://') {
    throw "The update repository URL must use HTTPS."
}
if ($ReleaseNotesUrl -notmatch '^https://') {
    throw "The release notes URL must use HTTPS."
}

if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot "out\packages"
}
$resolvedOutputDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot $OutputDirectory))
New-Item -ItemType Directory -Force -Path $resolvedOutputDirectory | Out-Null

$ifwBinDirectory = Find-IfwBinDirectory -ExplicitRoot $IfwRoot
$binaryCreator = Join-Path $ifwBinDirectory "binarycreator.exe"
$installerBase = Join-Path $ifwBinDirectory "installerbase.exe"
$repositoryGenerator = Join-Path $ifwBinDirectory "repogen.exe"
Assert-IfwVersion -InstallerBase $installerBase -MinimumVersion "4.11"
if ($UpdateSiteDirectory -and
    -not (Test-Path -LiteralPath $repositoryGenerator -PathType Leaf)) {
    throw "The selected Qt Installer Framework does not contain repogen.exe."
}
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
$temporaryUpdateRepository = Join-Path $temporaryRoot "update-repository"
$temporaryUpdateSite = Join-Path $temporaryRoot "update-site"

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

    # IFW embeds the ICO into the installer/maintenance executable and uses the
    # PNG for its live window. Keep both sourced from the application branding
    # assets so packaged and portable builds cannot drift visually.
    $installerConfigDirectory =
        Join-Path $installerWorkspace "config"
    Copy-Item -LiteralPath (
        Join-Path $repositoryRoot "assets\yaml-icon.ico") `
        -Destination (
            Join-Path $installerConfigDirectory "yaml-icon.ico")
    Copy-Item -LiteralPath (
        Join-Path $repositoryRoot "assets\yaml-icon.png") `
        -Destination (
            Join-Path $installerConfigDirectory "yaml-icon.png")

    $templateValues = @{
        PRODUCT_VERSION = $projectVersion
        RELEASE_DATE = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd")
        UPDATE_REPOSITORY_URL = $UpdateRepositoryUrl.TrimEnd("/")
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

    if ($UpdateSiteDirectory) {
        & $repositoryGenerator `
            -p (Join-Path $installerWorkspace "packages") `
            $temporaryUpdateRepository
        if ($LASTEXITCODE -ne 0) {
            throw "repogen failed with exit code $LASTEXITCODE."
        }
        $updatesXml = Join-Path $temporaryUpdateRepository "Updates.xml"
        if (-not (Test-Path -LiteralPath $updatesXml -PathType Leaf)) {
            throw "repogen did not create Updates.xml."
        }

        $stableSite = Join-Path $temporaryUpdateSite `
            "updates\windows\x64\stable"
        New-Item -ItemType Directory -Force -Path $stableSite | Out-Null
        Get-ChildItem -LiteralPath $temporaryUpdateRepository |
            ForEach-Object {
                Copy-Item -LiteralPath $_.FullName `
                    -Destination $stableSite -Recurse
            }
        $manifest = [ordered]@{
            schemaVersion = 1
            channel = "stable"
            version = $projectVersion
            repositoryUrl = $UpdateRepositoryUrl.TrimEnd("/") + "/"
            releaseNotesUrl = $ReleaseNotesUrl
        }
        $manifest | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $stableSite "latest.json") `
                -Encoding utf8
        Set-Content -LiteralPath (Join-Path $temporaryUpdateSite ".nojekyll") `
            -Value "" -Encoding ascii

        $resolvedUpdateSite =
            Assert-SafeOutputDirectory -Path (
                Join-Path $repositoryRoot $UpdateSiteDirectory)
        if (Test-Path -LiteralPath $resolvedUpdateSite) {
            Remove-Item -LiteralPath $resolvedUpdateSite -Recurse -Force
        }
        New-Item -ItemType Directory -Force -Path $resolvedUpdateSite |
            Out-Null
        Get-ChildItem -LiteralPath $temporaryUpdateSite -Force |
            ForEach-Object {
                Copy-Item -LiteralPath $_.FullName `
                    -Destination $resolvedUpdateSite -Recurse -Force
            }
    }

    Remove-Item -LiteralPath $installerPath -Force `
        -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $checksumPath -Force `
        -ErrorAction SilentlyContinue
    & $binaryCreator `
        --hybrid `
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
        Invoke-InstallerVerificationInstall `
            -InstallerPath $installerPath `
            -InstallationRoot $verificationRoot
        Assert-WindowsUninstallEntry -InstallationRoot $verificationRoot

        $installedExecutable = Join-Path $verificationRoot "yauml.exe"
        & $installedExecutable validate `
            (Join-Path $repositoryRoot "examples\sample.yauml")
        if ($LASTEXITCODE -ne 0) {
            throw "The installed application's validation check failed."
        }

        # Also test installing the downloaded installer over an existing copy.
        # A marker distinguishes a real replacement from an unsafe file overlay.
        $upgradeMarker = Join-Path $verificationRoot `
            ".yauml-replace-existing"
        Set-Content -LiteralPath $upgradeMarker `
            -Value "must be removed during replacement" -Encoding ascii
        Invoke-InstallerVerificationInstall `
            -InstallerPath $installerPath `
            -InstallationRoot $verificationRoot `
            -ReplaceExisting
        if (Test-Path -LiteralPath $upgradeMarker) {
            throw "The installer did not replace the existing installation."
        }
        Assert-WindowsUninstallEntry -InstallationRoot $verificationRoot
        & $installedExecutable validate `
            (Join-Path $repositoryRoot "examples\sample.yauml")
        if ($LASTEXITCODE -ne 0) {
            throw "The replaced application's validation check failed."
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
            throw "The test installation still contains yauml.exe after purge."
        }
        if (@(Get-YaumlUninstallEntries `
                -InstallationRoot $verificationRoot).Count -ne 0) {
            throw "The uninstaller left its Windows registration behind."
        }
    }

    $hash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash
    "$($hash.ToLowerInvariant())  $installerName" |
        Set-Content -LiteralPath $checksumPath -Encoding ascii
} finally {
    $verificationMaintenanceTool =
        Join-Path $verificationRoot "maintenancetool.exe"
    if (Test-Path -LiteralPath $verificationMaintenanceTool -PathType Leaf) {
        & $verificationMaintenanceTool `
            --accept-messages `
            --confirm-command `
            purge
    }

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
if ($UpdateSiteDirectory) {
    Write-Host "Created update site $resolvedUpdateSite"
}
