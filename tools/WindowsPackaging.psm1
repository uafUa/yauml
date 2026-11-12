Set-StrictMode -Version Latest

function Get-YaumlProjectVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepositoryRoot
    )

    $cmakePath = Join-Path $RepositoryRoot "CMakeLists.txt"
    $cmakeText = Get-Content -LiteralPath $cmakePath -Raw
    $match = [regex]::Match(
        $cmakeText,
        'project\s*\(\s*yauml\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
    if (-not $match.Success) {
        throw "Could not read the project version from '$cmakePath'."
    }

    return $match.Groups[1].Value
}

function Get-YaumlSequenceAuthoringFiles {
    # Keep the repository layout so the skill's relative links also work
    # from an installed copy.
    ".agents/skills/create-yauml-sequence-diagram/SKILL.md"
    ".agents/skills/create-yauml-sequence-diagram/agents/openai.yaml"
    "docs/sequence-diagram-import.md"
    "examples/sequence-import/standalone-demo.sequence.json5"
}

Export-ModuleMember -Function Get-YaumlProjectVersion, Get-YaumlSequenceAuthoringFiles
