[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedHead,
    [string]$OutputJson = 'docs\p4b\p4b_acceptance.json',
    [string]$OutputMarkdown = 'docs\p4b\P4B_ACCEPTANCE.md'
)

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$expected = $ExpectedHead.ToLowerInvariant()

function Get-GitValue {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $value = ((& git -C $repoRoot @Arguments 2>$null) -join "`n").Trim().ToLowerInvariant()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($value)) {
        throw "git command failed: $($Arguments -join ' ')"
    }
    return $value
}

$status = @(& git -C $repoRoot status --porcelain --untracked-files=all 2>$null)
if ($LASTEXITCODE -ne 0) {
    throw 'could not inspect checkout status'
}
if ($status.Count -ne 0) {
    throw 'clean-checkout acceptance requires an empty worktree'
}

$head = Get-GitValue @('rev-parse', 'HEAD')
if ($head -ne $expected) {
    throw "clean-checkout HEAD mismatch: expected $expected, got $head"
}

$vsDevCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
)
$vsDevCmd = $vsDevCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($vsDevCmd)) {
    throw 'native Visual Studio developer environment is unavailable'
}

$acceptance = Join-Path $repoRoot 'tools\p4b\phase4b_acceptance.py'
$jsonPath = Join-Path $repoRoot $OutputJson
$markdownPath = Join-Path $repoRoot $OutputMarkdown
$command = 'call "' + $vsDevCmd + '" -arch=x64 && python -B "' + $acceptance +
    '" --expected-head ' + $expected + ' --output-json "' + $OutputJson +
    '" --output-markdown "' + $OutputMarkdown + '"'
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Phase-4B acceptance execution failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $jsonPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $markdownPath -PathType Leaf)) {
    throw 'acceptance execution did not generate both evidence files'
}

$validator = Join-Path $repoRoot 'tests\teacher\phase4b_acceptance_test.py'
& python -B $validator --report $jsonPath --markdown $markdownPath
if ($LASTEXITCODE -ne 0) {
    throw "generated Phase-4B evidence validation failed with exit code $LASTEXITCODE"
}

Write-Output "phase4b_clean_checkout_acceptance=PASS"
Write-Output "source_head=$head"
Write-Output "json=$OutputJson"
Write-Output "markdown=$OutputMarkdown"
