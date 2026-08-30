[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedHead,
    [string]$OutputDirectory,
    [string]$ArtifactDirectory,
    [string]$FinalizeFrom,
    [switch]$SkipHeavy,
    [string]$HeavyEvidenceFrom
)

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$expected = $ExpectedHead.ToLowerInvariant()

function Resolve-RepoPath {
    param(
        [Parameter(Mandatory = $false)]
        [string]$Value,
        [Parameter(Mandatory = $true)]
        [string]$DefaultRelative
    )
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $DefaultRelative))
    }
    if ([System.IO.Path]::IsPathRooted($Value)) {
        return [System.IO.Path]::GetFullPath($Value)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Value))
}

$outputDirectory = Resolve-RepoPath $OutputDirectory 'docs\p4a'
$artifactDirectory = Resolve-RepoPath $ArtifactDirectory 'artifacts\p4a\h-exec'

function Get-RepoRelativePath {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    $resolved = [System.IO.Path]::GetFullPath($PathValue)
    $root = $repoRoot.TrimEnd('\') + '\'
    if ($resolved.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $resolved.Substring($root.Length).Replace('\', '/')
    }
    return $resolved.Replace('\', '/')
}

function Get-GitValue {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $output = @(& git -C $repoRoot @Arguments 2>$null)
    $value = ($output -join "`n").Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($value)) {
        throw "git command failed: $($Arguments -join ' ')"
    }
    return $value
}

function Get-TextSha256 {
    param([Parameter(Mandatory = $true)][string]$Text)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Text)
        return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($PathValue)
        try {
            return ([System.BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
        } finally {
            $stream.Dispose()
        }
    } finally {
        $sha.Dispose()
    }
}

function Get-OutputCounts {
    param([Parameter(Mandatory = $true)][string]$Output)
    $ctest = [regex]::Matches($Output, '(\d+)% tests passed, (\d+) tests failed out of (\d+)')
    if ($ctest.Count -gt 0) {
        $match = $ctest[$ctest.Count - 1]
        return [ordered]@{
            kind = 'CTest'
            passed = [int]$match.Groups[3].Value - [int]$match.Groups[2].Value
            failed = [int]$match.Groups[2].Value
            total = [int]$match.Groups[3].Value
        }
    }
    $unittest = [regex]::Matches($Output, '(?m)^Ran\s+(\d+)\s+tests?(?:\s+in\s+[^\r\n]+)?\s*$')
    if ($unittest.Count -gt 0) {
        $total = [int]$unittest[$unittest.Count - 1].Groups[1].Value
        return [ordered]@{
            kind = 'unittest'
            passed = if ($Output -match '(?m)^OK(?:\s+\([^\r\n]+\))?\s*$') { $total } else { 0 }
            failed = if ($Output -match '(?m)^OK(?:\s+\([^\r\n]+\))?\s*$') { 0 } else { $total }
            total = $total
        }
    }
    return $null
}

$script:Runs = [ordered]@{}
$script:RunOutputs = @{}
$script:HeavyEvidence = $null
$script:ImportedHeavyReport = $null
$script:ExpectedCtestCounts = @{
    'focused-policy-ctest' = @{ kind = 'CTest'; passed = 12; failed = 0; total = 12 }
    'debug-normal-ctest' = @{ kind = 'CTest'; passed = 127; failed = 0; total = 127 }
    'release-normal-ctest' = @{ kind = 'CTest'; passed = 127; failed = 0; total = 127 }
    'heavy-replay-release' = @{ kind = 'CTest'; passed = 1; failed = 0; total = 1 }
}
$script:PythonSuiteLabels = @('repository-python', 'm3-python', 'm4-python')

function Get-RunValidationError {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [Parameter(Mandatory = $false)][object]$Counts
    )
    if ($ExitCode -ne 0) {
        return $null
    }
    if ($Command -ieq 'ctest') {
        if ($null -eq $Counts -or [int]$Counts.total -le 0) {
            return 'CTest exited 0 without a parseable non-empty test summary'
        }
        if ([int]$Counts.failed -ne 0) {
            return 'CTest reported failed tests despite exit code 0'
        }
        if ($script:ExpectedCtestCounts.ContainsKey($Label)) {
            $expected = $script:ExpectedCtestCounts[$Label]
            if ([string]$Counts.kind -ne [string]$expected.kind -or
                [int]$Counts.passed -ne [int]$expected.passed -or
                [int]$Counts.failed -ne [int]$expected.failed -or
                [int]$Counts.total -ne [int]$expected.total) {
                return "CTest count mismatch: expected $($expected.passed)/$($expected.total), observed $($Counts.passed)/$($Counts.total)"
            }
        }
    }
    if ($script:PythonSuiteLabels -contains $Label) {
        if ($null -eq $Counts -or
            [string]$Counts.kind -ne 'unittest' -or
            [int]$Counts.total -le 0 -or
            [int]$Counts.failed -ne 0) {
            return 'Python unittest exited 0 without a valid non-empty passing suite summary'
        }
    }
    return $null
}

function Invoke-Recorded {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$DisplayCommand
    )
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $lines = @()
    $exitCode = 1
    $workingDirectory = (Get-Location).Path
    try {
        Push-Location -LiteralPath $repoRoot
        try {
            $lines = @(& $Command @Arguments 2>&1 | ForEach-Object { $_.ToString() })
            $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { [int]$LASTEXITCODE }
        } finally {
            Pop-Location
        }
    } catch {
        $lines += $_.Exception.Message
        $exitCode = 1
        if ((Get-Location).Path -ne $workingDirectory) {
            Pop-Location
        }
    }
    $stopwatch.Stop()
    $output = [string]::Join("`n", [string[]]$lines)
    $outputLines = if ($output.Length -eq 0) { @() } else { $output -split "`n" }
    $counts = Get-OutputCounts $output
    $validationError = Get-RunValidationError $Label $Command $exitCode $counts
    $record = [ordered]@{
        label = $Label
        command = $DisplayCommand
        exit_code = $exitCode
        result = if ($exitCode -eq 0 -and $null -eq $validationError) { 'PASS' } else { 'FAIL' }
        elapsed_seconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
        output_summary = [ordered]@{
            sha256 = Get-TextSha256 $output
            utf8_bytes = [System.Text.UTF8Encoding]::new($false).GetByteCount($output)
            line_count = $outputLines.Count
            first_line = if ($outputLines.Count -gt 0) { $outputLines[0] } else { '' }
            last_line = if ($outputLines.Count -gt 0) { $outputLines[$outputLines.Count - 1] } else { '' }
        }
    }
    if ($null -ne $counts) {
        $record.test_counts = $counts
    }
    if ($null -ne $validationError) {
        $record.validation_error = $validationError
    }
    $script:Runs[$Label] = $record
    $script:RunOutputs[$Label] = $output
    return $record
}

function Invoke-Skipped {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$DisplayCommand,
        [Parameter(Mandatory = $true)][string]$Reason
    )
    $output = "SKIPPED: $Reason"
    $record = [ordered]@{
        label = $Label
        command = $DisplayCommand
        exit_code = $null
        result = 'SKIPPED'
        elapsed_seconds = [double]0
        output_summary = [ordered]@{
            sha256 = Get-TextSha256 $output
            utf8_bytes = [System.Text.UTF8Encoding]::new($false).GetByteCount($output)
            line_count = 1
            first_line = $output
            last_line = $output
        }
        validation_error = $Reason
    }
    $script:Runs[$Label] = $record
    $script:RunOutputs[$Label] = $output
    return $record
}

function Invoke-IdentitySourceScan {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $files = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'include\ygo'), (Join-Path $repoRoot 'src') -Recurse -File)
    $matches = @()
    foreach ($file in $files) {
        $matches += @(Select-String -LiteralPath $file.FullName -Pattern 'ocgforge\.test\.' -AllMatches -ErrorAction SilentlyContinue)
    }
    $stopwatch.Stop()
    $output = if ($matches.Count -eq 0) {
        'no ocgforge.test.* identity found under include/ygo or src'
    } else {
        ($matches | ForEach-Object { "$($_.Path):$($_.LineNumber):$($_.Line.Trim())" }) -join "`n"
    }
    $record = [ordered]@{
        label = 'production-identity-source-scan'
        command = 'source scan include/ygo and src for ocgforge.test.*'
        exit_code = if ($matches.Count -eq 0) { 0 } else { 1 }
        result = if ($matches.Count -eq 0) { 'PASS' } else { 'FAIL' }
        elapsed_seconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
        output_summary = [ordered]@{
            sha256 = Get-TextSha256 $output
            utf8_bytes = [System.Text.UTF8Encoding]::new($false).GetByteCount($output)
            line_count = if ($output.Length -eq 0) { 0 } else { ($output -split "`n").Count }
            first_line = $output
            last_line = $output
        }
    }
    $script:Runs[$record.label] = $record
    $script:RunOutputs[$record.label] = $output
    return $record
}

function Test-RunPass {
    param([Parameter(Mandatory = $true)][string]$Label)
    return $script:Runs.Contains($Label) -and $script:Runs[$Label].result -eq 'PASS'
}

function Test-AllRunsPass {
    param([Parameter(Mandatory = $true)][string[]]$Labels)
    if ($Labels.Count -eq 0) { return $false }
    foreach ($label in $Labels) {
        if (-not (Test-RunPass $label)) { return $false }
    }
    return $true
}

function Assert-ToolchainPrerequisites {
    $required = @(
        (Join-Path $repoRoot '.cache\toolchain\ninja\ninja.exe'),
        (Join-Path $repoRoot '.cache\toolchain\zig-x86_64-windows-0.14.1\zig.exe')
    )
    $missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
    if ($missing.Count -ne 0) {
        throw "P4A acceptance BLOCKED: missing required local toolchain input(s): $($missing -join ', ')"
    }
}

function New-Gate {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)][string]$Invariant,
        [Parameter(Mandatory = $true)][string]$Condition,
        [Parameter(Mandatory = $true)][string[]]$Evidence,
        [Parameter(Mandatory = $true)][string]$Status
    )
    return [ordered]@{
        gate = $Id
        invariant = $Invariant
        status = $Status
        exact_evidence = $Evidence
        pass_condition = $Condition
    }
}

function Assert-GateIds {
    param([Parameter(Mandatory = $true)][object[]]$Gates)
    $expectedIds = @(0..29 | ForEach-Object { 'P4A-G{0:D2}' -f $_ })
    $actualIds = @($Gates | ForEach-Object { $_.gate })
    if (($actualIds -join ',') -ne ($expectedIds -join ',')) {
        throw "acceptance gate matrix does not contain exactly P4A-G00 through P4A-G29"
    }
}

function Get-ArtifactRecords {
    if (-not (Test-Path -LiteralPath $artifactDirectory)) { return @() }
    $records = @()
    foreach ($file in @(Get-ChildItem -LiteralPath $artifactDirectory -Recurse -File | Sort-Object FullName)) {
        $records += [ordered]@{
            path = Get-RepoRelativePath $file.FullName
            sha256 = Get-FileSha256 $file.FullName
            bytes = $file.Length
        }
    }
    return $records
}

function Get-SemanticArtifactRecords {
    param([Parameter(Mandatory = $true)][object[]]$Artifacts)
    $records = @()
    foreach ($artifact in @($Artifacts)) {
        $role = $null
        if ($artifact.path -like '*/m4/full_game/full_fixed_deck_results.json') {
            $role = 'canonical-full-game'
        } elseif ($artifact.path -like '*/m4/lifecycle_stress.json') {
            $role = 'lifecycle-stress'
        }
        if ($null -ne $role) {
            $records += [ordered]@{
                role = $role
                sha256 = $artifact.sha256
                bytes = $artifact.bytes
            }
        }
    }

    if ($SkipHeavy -and $null -ne $script:ImportedHeavyReport) {
        $importedLifecycle = @($script:ImportedHeavyReport.semantic_fingerprint.semantic_artifacts | Where-Object {
                ($null -ne $_.role -and $_.role -eq 'lifecycle-stress') -or
                ($null -ne $_.path -and $_.path -like '*/m4/lifecycle_stress.json')
            })
        if ($importedLifecycle.Count -ne 1) {
            throw 'exact H_exec heavy evidence must contain exactly one lifecycle-stress artifact'
        }
        $lifecycle = $importedLifecycle[0]
        $records = @($records | Where-Object { $_.role -ne 'lifecycle-stress' })
        $records += [ordered]@{
            role = 'lifecycle-stress'
            sha256 = $lifecycle.sha256
            bytes = $lifecycle.bytes
        }
    }
    return @($records | Sort-Object role)
}

function Get-NormalRunLabels {
    return @(
        'debug-configure',
        'debug-build',
        'focused-policy-ctest',
        'debug-normal-ctest',
        'release-configure',
        'release-build',
        'release-normal-ctest',
        'repository-python',
        'm3-python',
        'm4-python',
        'public-fact-matrix',
        'rules-deck-identity',
        'policy-determinism',
        'policy-boundary',
        'rules-bundle-verification',
        'production-identity-source-scan',
        'm4-canonical-full-game',
        'm4-recommended-concurrency-soak'
    )
}

function Get-HeavyRunDescriptor {
    param(
        [Parameter(Mandatory = $false)][object]$Run,
        [Parameter(Mandatory = $true)][string]$RequiredText,
        [Parameter(Mandatory = $false)][object]$SemanticArtifact
    )
    if ($null -eq $Run) {
        return $null
    }
    $outputSha256 = ''
    if ($null -ne $Run.output_summary) {
        $outputSha256 = [string]$Run.output_summary.sha256
    }
    $containsRequiredText = $false
    $runLabel = [string]$Run.label
    if ($script:RunOutputs.ContainsKey($runLabel)) {
        $containsRequiredText = $script:RunOutputs[$runLabel] -match [regex]::Escape($RequiredText)
    }
    $counts = $null
    if ($null -ne $Run.test_counts) {
        $counts = [ordered]@{
            kind = [string]$Run.test_counts.kind
            passed = [int]$Run.test_counts.passed
            failed = [int]$Run.test_counts.failed
            total = [int]$Run.test_counts.total
        }
    }
    return [ordered]@{
        label = $runLabel
        result = [string]$Run.result
        exit_code = [int]$Run.exit_code
        output_sha256 = $outputSha256
        output_contains_required_text = [bool]$containsRequiredText
        test_counts = $counts
        semantic_artifact = if ($null -eq $SemanticArtifact) { $null } else {
            [ordered]@{
                role = [string]$SemanticArtifact.role
                sha256 = [string]$SemanticArtifact.sha256
                bytes = [int64]$SemanticArtifact.bytes
            }
        }
    }
}

function Test-HeavyEvidenceRunPass {
    param([Parameter(Mandatory = $false)][object]$Run)
    return $null -ne $Run -and
        [string]$Run.result -eq 'PASS' -and
        [int]$Run.exit_code -eq 0 -and
        [bool]$Run.output_contains_required_text -and
        ($Run.label -ne 'heavy-lifecycle-stress' -or $null -ne $Run.semantic_artifact)
}

function Get-LifecycleSemanticArtifact {
    param([Parameter(Mandatory = $true)][object[]]$Artifacts)
    $matches = @($Artifacts | Where-Object {
            $_.path -like '*/m4/lifecycle_stress.json'
        })
    if ($matches.Count -ne 1) {
        return $null
    }
    return [ordered]@{
        role = 'lifecycle-stress'
        sha256 = [string]$matches[0].sha256
        bytes = [int64]$matches[0].bytes
    }
}

function Get-ReportRunsByLabel {
    param(
        [Parameter(Mandatory = $true)][object]$Report,
        [Parameter(Mandatory = $true)][string]$Label
    )
    return @($Report.runs | Where-Object { $_.label -eq $Label })
}

function Get-TestCountsFingerprint {
    param([Parameter(Mandatory = $false)][object]$Counts)
    if ($null -eq $Counts) {
        return '<null>'
    }
    return ([string]$Counts.kind) + '|' +
        ([string]$Counts.passed) + '|' +
        ([string]$Counts.failed) + '|' +
        ([string]$Counts.total)
}

function Assert-ExpectedGateStatuses {
    param([Parameter(Mandatory = $true)][object]$Report)
    Assert-GateIds @($Report.gates)
    foreach ($number in 0..28) {
        $gateId = 'P4A-G{0:D2}' -f $number
        $gate = @($Report.gates | Where-Object { $_.gate -eq $gateId })[0]
        if ($gate.status -ne 'PASS') {
            throw "$gateId must be PASS in an exact acceptance report"
        }
    }
    $g29 = @($Report.gates | Where-Object { $_.gate -eq 'P4A-G29' })[0]
    if ($g29.status -ne 'NOT_RUN') {
        throw 'P4A-G29 must be NOT_RUN before finalization'
    }
}

function Assert-HeavyRunDescriptor {
    param(
        [Parameter(Mandatory = $true)][object]$Report,
        [Parameter(Mandatory = $false)][object]$Descriptor,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $runs = @(Get-ReportRunsByLabel $Report $Label)
    if ($runs.Count -ne 1) {
        throw "exact acceptance report must contain exactly one run labeled $Label"
    }
    if ($null -eq $Descriptor) {
        throw "heavy evidence descriptor is missing for $Label"
    }
    $run = $runs[0]
    if ([string]$Descriptor.label -ne $Label -or [string]$run.label -ne $Label) {
        throw "heavy evidence label mismatch for $Label"
    }
    if ([string]$Descriptor.result -ne 'PASS' -or [string]$run.result -ne 'PASS') {
        throw "heavy evidence run must be PASS for $Label"
    }
    if ([int]$Descriptor.exit_code -ne 0 -or [int]$run.exit_code -ne 0) {
        throw "heavy evidence run must have exit code 0 for $Label"
    }
    if ($null -eq $run.output_summary -or
        [string]::IsNullOrWhiteSpace([string]$run.output_summary.sha256) -or
        [string]$Descriptor.output_sha256 -ne [string]$run.output_summary.sha256) {
        throw "heavy evidence output SHA mismatch for $Label"
    }
    if ([bool]$Descriptor.output_contains_required_text -ne $true) {
        throw "heavy evidence required output text is absent for $Label"
    }
    if ((Get-TestCountsFingerprint $Descriptor.test_counts) -ne
        (Get-TestCountsFingerprint $run.test_counts)) {
        throw "heavy evidence test-count mismatch for $Label"
    }
    return $run
}

function Assert-LifecycleArtifactBinding {
    param(
        [Parameter(Mandatory = $true)][object]$Report,
        [Parameter(Mandatory = $true)][object]$LifecycleDescriptor
    )
    $artifactRecords = @($Report.artifacts | Where-Object {
            $_.path -like '*/m4/lifecycle_stress.json'
        })
    if ($artifactRecords.Count -ne 1) {
        throw 'exact acceptance report must contain exactly one lifecycle-stress artifact record'
    }
    $semanticRecords = @($Report.semantic_fingerprint.semantic_artifacts | Where-Object {
            ($null -ne $_.role -and $_.role -eq 'lifecycle-stress') -or
            ($null -ne $_.path -and $_.path -like '*/m4/lifecycle_stress.json')
        })
    if ($semanticRecords.Count -ne 1) {
        throw 'exact acceptance report must contain exactly one lifecycle-stress semantic artifact'
    }
    $descriptorArtifact = $LifecycleDescriptor.semantic_artifact
    if ($null -eq $descriptorArtifact -or
        [string]$descriptorArtifact.role -ne 'lifecycle-stress') {
        throw 'lifecycle heavy evidence is missing its semantic artifact binding'
    }
    $artifact = $artifactRecords[0]
    $semantic = $semanticRecords[0]
    foreach ($candidate in @($artifact, $semantic)) {
        if ([string]$candidate.sha256 -ne [string]$descriptorArtifact.sha256 -or
            [int64]$candidate.bytes -ne [int64]$descriptorArtifact.bytes) {
            throw 'lifecycle heavy evidence does not match its artifact record'
        }
    }
}

function Assert-HExecAcceptanceReport {
    param([Parameter(Mandatory = $true)][object]$Report)
    if ($Report.schema_version -ne 'ocgforge.phase4a_acceptance.v1' -or
        $Report.acceptance_profile -ne 'h-exec-fast-normal-plus-heavy' -or
        $Report.heavy_evidence_mode -ne 'executed-at-h-exec' -or
        $Report.status -ne 'INCOMPLETE' -or
        $Report.source_head.ToLowerInvariant() -ne $expected -or
        $Report.h_exec.ToLowerInvariant() -ne $expected) {
        throw 'heavy evidence report has the wrong exact H_exec profile or source binding'
    }
    Assert-ExpectedGateStatuses $Report
    if ($null -eq $Report.runtime_budget -or
        $Report.runtime_budget.heavy_inherited -ne $false) {
        throw 'H_exec runtime budget must state heavy_inherited=false'
    }
    foreach ($runtimeField in @('normal_seconds', 'heavy_seconds', 'measured_seconds', 'combined_acceptance_seconds')) {
        $runtimeValue = $Report.runtime_budget.$runtimeField
        if ($null -eq $runtimeValue -or [double]$runtimeValue -lt 0) {
            throw "H_exec runtime budget is missing a valid $runtimeField value"
        }
    }
    $heavyLabels = @('heavy-replay-release', 'heavy-lifecycle-stress')
    $heavyRuns = @($Report.runs | Where-Object { $_.label -in $heavyLabels })
    if ($heavyRuns.Count -ne 2) {
        throw 'H_exec report must contain exactly two Heavy runs'
    }
    $forbiddenOldHeavyLabels = @('debug-full-ctest', 'release-full-ctest', 'm4-lifecycle-stress')
    if (@($Report.runs | Where-Object { $_.label -in $forbiddenOldHeavyLabels }).Count -ne 0) {
        throw 'H_exec report contains an obsolete or duplicate Heavy run label'
    }
    if ($null -eq $Report.heavy_evidence -or
        $Report.heavy_evidence.source_head.ToLowerInvariant() -ne $expected) {
        throw 'H_exec report is missing exact heavy evidence provenance'
    }
    $replayRun = Assert-HeavyRunDescriptor $Report $Report.heavy_evidence.replay 'heavy-replay-release'
    $replayCounts = $Report.heavy_evidence.replay.test_counts
    if ($null -eq $replayCounts -or
        $replayCounts.kind -ne 'CTest' -or
        [int]$replayCounts.passed -ne 1 -or
        [int]$replayCounts.failed -ne 0 -or
        [int]$replayCounts.total -ne 1) {
        throw 'Heavy Replay evidence must be exactly CTest 1/1'
    }
    $null = $replayRun
    $null = Assert-HeavyRunDescriptor $Report $Report.heavy_evidence.lifecycle 'heavy-lifecycle-stress'
    Assert-LifecycleArtifactBinding $Report $Report.heavy_evidence.lifecycle
    return $Report.heavy_evidence
}

function Assert-CleanAcceptanceReport {
    param([Parameter(Mandatory = $true)][object]$Report)
    if ($Report.schema_version -ne 'ocgforge.phase4a_acceptance.v1' -or
        $Report.acceptance_profile -ne 'clean-checkout-fast-normal' -or
        $Report.heavy_evidence_mode -ne 'inherited-exact-h-exec' -or
        $Report.status -ne 'INCOMPLETE' -or
        $Report.source_head.ToLowerInvariant() -ne $expected -or
        $Report.h_exec.ToLowerInvariant() -ne $expected) {
        throw 'clean report has the wrong exact source or acceptance profile'
    }
    Assert-ExpectedGateStatuses $Report
    if ($null -eq $Report.runtime_budget -or
        $Report.runtime_budget.heavy_inherited -ne $true) {
        throw 'clean report runtime budget must state heavy_inherited=true'
    }
    $heavyLabels = @('heavy-replay-release', 'heavy-lifecycle-stress', 'debug-full-ctest', 'release-full-ctest', 'm4-lifecycle-stress')
    if (@($Report.runs | Where-Object { $_.label -in $heavyLabels }).Count -ne 0) {
        throw 'clean report must contain zero Heavy runs'
    }
    if ($null -eq $Report.heavy_evidence) {
        throw 'clean report is missing inherited heavy evidence'
    }
}

function New-HeavyEvidenceFromRuns {
    param(
        [Parameter(Mandatory = $true)][string]$SourceHead,
        [Parameter(Mandatory = $true)][object[]]$Artifacts
    )
    $lifecycleArtifact = Get-LifecycleSemanticArtifact $Artifacts
    return [ordered]@{
        source_head = $SourceHead
        replay = Get-HeavyRunDescriptor $script:Runs['heavy-replay-release'] 'trajectory_replay_admission_test'
        lifecycle = Get-HeavyRunDescriptor $script:Runs['heavy-lifecycle-stress'] 'test_result_then_exit_never_publishes_passed_under_repeated_scheduling' $lifecycleArtifact
    }
}

function Import-HeavyEvidence {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    if (-not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        throw "exact H_exec heavy evidence report is missing: $PathValue"
    }
    $report = Get-Content -LiteralPath $PathValue -Raw | ConvertFrom-Json
    $evidence = Assert-HExecAcceptanceReport $report
    $script:ImportedHeavyReport = $report
    return $evidence
}

function Get-SemanticFingerprint {
    param([Parameter(Mandatory = $true)][object[]]$Gates, [Parameter(Mandatory = $true)][object[]]$Artifacts)
    $gateResults = [ordered]@{}
    foreach ($gate in $Gates) {
        if ($gate.gate -ne 'P4A-G29') {
            $gateResults[$gate.gate] = $gate.status
        }
    }
    $normalLabels = @(Get-NormalRunLabels)
    $normalResults = [ordered]@{}
    $counts = [ordered]@{}
    foreach ($label in $normalLabels) {
        if ($script:Runs.Contains($label)) {
            $normalResults[$label] = $script:Runs[$label].result
        } else {
            $normalResults[$label] = 'NOT_RUN'
        }
        if ($script:Runs.Contains($label) -and $null -ne $script:Runs[$label].test_counts) {
            $counts[$label] = $script:Runs[$label].test_counts
        }
    }
    $semanticArtifacts = @(Get-SemanticArtifactRecords $Artifacts)
    return [ordered]@{
        gate_results = $gateResults
        normal_run_results = $normalResults
        normal_command_counts = $counts
        command_counts = $counts
        heavy_evidence = $script:HeavyEvidence
        semantic_artifacts = $semanticArtifacts
    }
}

function Get-RuntimeBudget {
    $normalSeconds = [double]0
    foreach ($label in @(Get-NormalRunLabels)) {
        if ($script:Runs.Contains($label)) {
            $normalSeconds += [double]$script:Runs[$label].elapsed_seconds
        }
    }
    $measuredHeavySeconds = [double]0
    foreach ($label in @('heavy-replay-release', 'heavy-lifecycle-stress')) {
        if ($script:Runs.Contains($label)) {
            $measuredHeavySeconds += [double]$script:Runs[$label].elapsed_seconds
        }
    }
    $heavySeconds = $measuredHeavySeconds
    $heavyInherited = $false
    if ($SkipHeavy -and $null -ne $script:ImportedHeavyReport) {
        $heavySeconds = [double]$script:ImportedHeavyReport.runtime_budget.heavy_seconds
        $heavyInherited = $true
    }
    return [ordered]@{
        normal_seconds = [math]::Round($normalSeconds, 3)
        heavy_seconds = [math]::Round($heavySeconds, 3)
        measured_seconds = [math]::Round($normalSeconds + $measuredHeavySeconds, 3)
        combined_acceptance_seconds = [math]::Round($normalSeconds + $heavySeconds, 3)
        heavy_inherited = $heavyInherited
    }
}

function Write-Reports {
    param([Parameter(Mandatory = $true)][object]$Report)
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    $jsonPath = Join-Path $outputDirectory 'p4a_acceptance.json'
    $json = $Report | ConvertTo-Json -Depth 30
    [System.IO.File]::WriteAllText(
        $jsonPath,
        $json + "`n",
        [System.Text.UTF8Encoding]::new($false))
    $renderReport = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('# Phase 4A Acceptance')
    $lines.Add('')
    $lines.Add('| Field | Value |')
    $lines.Add('|---|---|')
    $lines.Add("| Status | ``$($renderReport.status)`` |")
    $lines.Add("| Source HEAD | ``$($renderReport.source_head)`` |")
    $lines.Add("| H_exec | ``$($renderReport.h_exec)`` |")
    $lines.Add("| Acceptance profile | ``$($renderReport.acceptance_profile)`` |")
    $lines.Add("| Heavy evidence | ``$($renderReport.heavy_evidence_mode)`` |")
    $lines.Add("| Rules bundle | ``$($renderReport.environment.rules_bundle_id)`` |")
    $lines.Add('')
    $lines.Add('## Gate matrix')
    $lines.Add('')
    $lines.Add('| Gate | Status | Exact evidence |')
    $lines.Add('|---|---|---|')
    foreach ($gate in $renderReport.gates) {
        $evidence = ($gate.exact_evidence -join ', ').Replace('|', '\|')
        $lines.Add("| $($gate.gate) | ``$($gate.status)`` | $evidence |")
    }
    $lines.Add('')
    $lines.Add('## Command evidence')
    $lines.Add('')
    $lines.Add('| Label | Result | Exit | Counts | Output SHA-256 |')
    $lines.Add('|---|---|---:|---|---|')
    foreach ($run in $renderReport.runs) {
        $counts = if ($null -ne $run.test_counts) {
            "$($run.test_counts.passed)/$($run.test_counts.total)"
        } else { '-' }
        $lines.Add("| $($run.label) | ``$($run.result)`` | $($run.exit_code) | $counts | ``$($run.output_summary.sha256)`` |")
    }
    $lines.Add('')
    $lines.Add('## Generated artifact hashes')
    $lines.Add('')
    if ($renderReport.artifacts.Count -eq 0) {
        $lines.Add('No auxiliary artifacts were produced.')
    } else {
        $lines.Add('| Path | Bytes | SHA-256 |')
        $lines.Add('|---|---:|---|')
        foreach ($artifact in $renderReport.artifacts) {
            $lines.Add("| ``$($artifact.path)`` | $($artifact.bytes) | ``$($artifact.sha256)`` |")
        }
    }
    $lines.Add('')
    $lines.Add('## Runtime budget')
    $lines.Add('')
    $lines.Add('| Normal seconds | Heavy seconds | Measured seconds | Combined acceptance seconds | Heavy inherited |')
    $lines.Add('|---:|---:|---:|---:|---|')
    $lines.Add("| $($renderReport.runtime_budget.normal_seconds) | $($renderReport.runtime_budget.heavy_seconds) | $($renderReport.runtime_budget.measured_seconds) | $($renderReport.runtime_budget.combined_acceptance_seconds) | $($renderReport.runtime_budget.heavy_inherited) |")
    $lines.Add('')
    $lines.Add('## Scope and limitations')
    $lines.Add('')
    $lines.Add('- This report records executable evidence only; it does not implement TeacherCore or either StrategyProfile.')
    $lines.Add('- `BLOCKED` facts in the public-fact matrix remain blocked and are not replaced with private-state access.')
    if ($renderReport.status -ne 'PASS') {
        $lines.Add('- H_evidence is not complete until the clean-checkout reproduction changes P4A-G29 to `PASS`.')
    }
    [System.IO.File]::WriteAllText(
        (Join-Path $outputDirectory 'P4A_ACCEPTANCE.md'),
        ($lines -join "`n") + "`n",
        [System.Text.UTF8Encoding]::new($false))
}

$resolvedHead = Get-GitValue @('rev-parse', '--verify', "$expected`^{commit}")
if ($resolvedHead.ToLowerInvariant() -ne $expected) {
    throw "expected head does not resolve exactly: requested $expected, resolved $resolvedHead"
}
$currentHead = Get-GitValue @('rev-parse', '--verify', 'HEAD^{commit}')
if ($currentHead.ToLowerInvariant() -ne $expected) {
    throw "acceptance checkout is not exact H_exec: expected $expected, actual $currentHead"
}

if (-not [string]::IsNullOrWhiteSpace($FinalizeFrom)) {
    if ($SkipHeavy -or -not [string]::IsNullOrWhiteSpace($HeavyEvidenceFrom)) {
        throw '-SkipHeavy and -HeavyEvidenceFrom are only valid for a clean-checkout report'
    }
} elseif ($SkipHeavy) {
    if ([string]::IsNullOrWhiteSpace($HeavyEvidenceFrom)) {
        throw '-SkipHeavy requires -HeavyEvidenceFrom bound to the same H_exec'
    }
    $script:HeavyEvidence = Import-HeavyEvidence (Resolve-RepoPath $HeavyEvidenceFrom 'docs\p4a\p4a_acceptance.json')
} elseif (-not [string]::IsNullOrWhiteSpace($HeavyEvidenceFrom)) {
    throw '-HeavyEvidenceFrom requires -SkipHeavy'
}

if ([string]::IsNullOrWhiteSpace($FinalizeFrom)) {
    $statusOutput = @(& git -C $repoRoot status --porcelain 2>$null)
    if ($LASTEXITCODE -ne 0) {
        throw 'git command failed: status --porcelain'
    }
    $status = ($statusOutput -join "`n").Trim()
    if (-not [string]::IsNullOrWhiteSpace($status)) {
        throw 'H_exec acceptance must start from a clean worktree'
    }

    if (Test-Path -LiteralPath $artifactDirectory) {
        $existingArtifacts = @(Get-ChildItem -LiteralPath $artifactDirectory -Recurse -File)
        if ($existingArtifacts.Count -ne 0) {
            throw "acceptance artifact directory must be empty before execution: $artifactDirectory"
        }
    }
    Assert-ToolchainPrerequisites
    New-Item -ItemType Directory -Force -Path $artifactDirectory | Out-Null
    $focusedRegex = '^(trajectory_codec_test|trajectory_recorder_test|trajectory_shard_test|trajectory_restricted_evidence_test|trajectory_receipt_test|trajectory_dataset_manifest_test|public_action_identity_test|public_safe_state_test|policy_boundary_compile_test|policy_rng_test|random_legal_test|policy_runner_integration_test)$'
    $nonHeavyCTestExclusion = '^(trajectory_replay_admission_test|m4_failure_isolation_test)$'

    $null = Invoke-Recorded 'debug-configure' 'cmake' @('--preset', 'dev-windows-zig') 'cmake --preset dev-windows-zig'
    $null = Invoke-Recorded 'debug-build' 'cmake' @('--build', '--preset', 'dev-windows-zig', '--parallel') 'cmake --build --preset dev-windows-zig --parallel'
    $debugReady = Test-AllRunsPass @('debug-configure', 'debug-build')
    if ($debugReady) {
        $null = Invoke-Recorded 'focused-policy-ctest' 'ctest' @('--preset', 'dev-windows-zig', '-R', $focusedRegex, '--output-on-failure') "ctest --preset dev-windows-zig -R $focusedRegex --output-on-failure"
        $null = Invoke-Recorded 'debug-normal-ctest' 'ctest' @('--preset', 'dev-windows-zig', '-E', $nonHeavyCTestExclusion, '--output-on-failure') "ctest --preset dev-windows-zig -E $nonHeavyCTestExclusion --output-on-failure"
    } else {
        $debugSkipReason = 'debug configure/build did not both pass; dependent Debug CTest commands were not run'
        $null = Invoke-Skipped 'focused-policy-ctest' "ctest --preset dev-windows-zig -R $focusedRegex --output-on-failure" $debugSkipReason
        $null = Invoke-Skipped 'debug-normal-ctest' "ctest --preset dev-windows-zig -E $nonHeavyCTestExclusion --output-on-failure" $debugSkipReason
    }

    $null = Invoke-Recorded 'release-configure' 'cmake' @('--preset', 'release-windows-zig') 'cmake --preset release-windows-zig'
    $null = Invoke-Recorded 'release-build' 'cmake' @('--build', '--preset', 'release-windows-zig', '--parallel') 'cmake --build --preset release-windows-zig --parallel'
    $releaseReady = Test-AllRunsPass @('release-configure', 'release-build')
    if ($releaseReady) {
        $null = Invoke-Recorded 'release-normal-ctest' 'ctest' @('--preset', 'release-windows-zig', '-E', $nonHeavyCTestExclusion, '--output-on-failure') "ctest --preset release-windows-zig -E $nonHeavyCTestExclusion --output-on-failure"
    } else {
        $releaseSkipReason = 'release configure/build did not both pass; dependent Release CTest commands were not run'
        $null = Invoke-Skipped 'release-normal-ctest' "ctest --preset release-windows-zig -E $nonHeavyCTestExclusion --output-on-failure" $releaseSkipReason
    }

    if (-not $SkipHeavy) {
        if ($releaseReady) {
            $null = Invoke-Recorded 'heavy-replay-release' 'ctest' @('--preset', 'release-windows-zig', '-R', '^trajectory_replay_admission_test$', '--output-on-failure') 'ctest --preset release-windows-zig -R ^trajectory_replay_admission_test$ --output-on-failure'
        } else {
            $null = Invoke-Skipped 'heavy-replay-release' 'ctest --preset release-windows-zig -R ^trajectory_replay_admission_test$ --output-on-failure' $releaseSkipReason
        }
    }

    $null = Invoke-Recorded 'repository-python' 'python' @('-B', '-m', 'unittest', 'discover', '-s', 'tests/python', '-v') 'python -B -m unittest discover -s tests/python -v'
    $null = Invoke-Recorded 'm3-python' 'python' @('-B', '-m', 'unittest', 'discover', '-s', 'tests/m3', '-v') 'python -B -m unittest discover -s tests/m3 -v'

    $previousWorker = $env:YGO_M4_WORKER
    $env:YGO_M4_WORKER = 'build/windows-zig/ygo_m4_worker.exe'
    try {
        if ($debugReady) {
            $null = Invoke-Recorded 'm4-python' 'python' @('-B', 'tools/m4/run_fast_suite.py', '-v') 'YGO_M4_WORKER=build/windows-zig/ygo_m4_worker.exe python -B tools/m4/run_fast_suite.py -v'
        } else {
            $null = Invoke-Skipped 'm4-python' 'YGO_M4_WORKER=build/windows-zig/ygo_m4_worker.exe python -B tools/m4/run_fast_suite.py -v' 'debug configure/build did not both pass; dependent M4 worker tests were not run'
        }
    } finally {
        if ($null -eq $previousWorker) {
            Remove-Item Env:YGO_M4_WORKER -ErrorAction SilentlyContinue
        } else {
            $env:YGO_M4_WORKER = $previousWorker
        }
    }

    $null = Invoke-Recorded 'public-fact-matrix' 'python' @('-B', 'tests/policy/public_fact_matrix_test.py') 'python -B tests/policy/public_fact_matrix_test.py'
    $null = Invoke-Recorded 'rules-deck-identity' 'python' @('-B', 'tests/policy/rules_deck_identity_test.py') 'python -B tests/policy/rules_deck_identity_test.py'
    if ($debugReady) {
        $null = Invoke-Recorded 'policy-determinism' 'python' @('-B', 'tests/policy/policy_determinism_test.py') 'python -B tests/policy/policy_determinism_test.py'
    } else {
        $null = Invoke-Skipped 'policy-determinism' 'python -B tests/policy/policy_determinism_test.py' 'debug configure/build did not both pass; dependent policy probe was not run'
    }
    $null = Invoke-Recorded 'policy-boundary' 'python' @('-B', 'tests/policy/policy_boundary_test.py') 'python -B tests/policy/policy_boundary_test.py'
    $null = Invoke-Recorded 'rules-bundle-verification' 'python' @('-B', 'tools/verify_rules_bundle.py', '--lock', 'third_party/rules_bundle.lock.json', '--cache', '.cache/rules_bundle') 'python -B tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle'
    $null = Invoke-IdentitySourceScan

    $fullGameOutput = Get-RepoRelativePath (Join-Path $artifactDirectory 'm4\full_game')
    if ($releaseReady) {
        $null = Invoke-Recorded 'm4-canonical-full-game' 'python' @('-B', 'tests/m3/full_game/full_fixed_deck_test.py', '--probe', 'build/release-windows-zig/ygo_core_probe.exe', '--games', '16', '--max-steps', '2200', '--timeout', '300', '--output', $fullGameOutput) "python -B tests/m3/full_game/full_fixed_deck_test.py --probe build/release-windows-zig/ygo_core_probe.exe --games 16 --max-steps 2200 --timeout 300 --output $fullGameOutput"
    } else {
        $null = Invoke-Skipped 'm4-canonical-full-game' "python -B tests/m3/full_game/full_fixed_deck_test.py --probe build/release-windows-zig/ygo_core_probe.exe --games 16 --max-steps 2200 --timeout 300 --output $fullGameOutput" 'release configure/build did not both pass; dependent full-game probe was not run'
    }

    if (-not $SkipHeavy) {
        $lifecycleOutput = Get-RepoRelativePath (Join-Path $artifactDirectory 'm4\lifecycle_stress.json')
        $null = Invoke-Recorded 'heavy-lifecycle-stress' 'python' @('-B', 'tools/m4/record_lifecycle_stress_evidence.py', '--repetitions', '5', '--output', $lifecycleOutput) "python -B tools/m4/record_lifecycle_stress_evidence.py --repetitions 5 --output $lifecycleOutput"
    }

    $soakOutput = Get-RepoRelativePath (Join-Path $artifactDirectory 'm4\soak.json')
    if ($releaseReady) {
        $null = Invoke-Recorded 'm4-recommended-concurrency-soak' 'python' @('-B', '-m', 'tools.m4.benchmark', '--worker-executable', 'build/release-windows-zig/ygo_m4_worker.exe', '--games', '128', '--workers', '16', '--master-seed', '20260815', '--mode', 'throughput', '--warmup-games', '4', '--observation-mode', 'full', '--output', $soakOutput) "python -B -m tools.m4.benchmark --worker-executable build/release-windows-zig/ygo_m4_worker.exe --games 128 --workers 16 --master-seed 20260815 --mode throughput --warmup-games 4 --observation-mode full --output $soakOutput"
    } else {
        $null = Invoke-Skipped 'm4-recommended-concurrency-soak' "python -B -m tools.m4.benchmark --worker-executable build/release-windows-zig/ygo_m4_worker.exe --games 128 --workers 16 --master-seed 20260815 --mode throughput --warmup-games 4 --observation-mode full --output $soakOutput" 'release configure/build did not both pass; dependent concurrency soak was not run'
    }

    $lock = Get-Content -LiteralPath (Join-Path $repoRoot 'third_party\rules_bundle.lock.json') -Raw | ConvertFrom-Json
    $matchup = Get-Content -LiteralPath (Join-Path $repoRoot 'fixtures\decks\ocgforge.matchup.swordsoul_salamangreat.v1.json') -Raw | ConvertFrom-Json
    $environment = [ordered]@{
        rules_bundle_id = $lock.bundle_id
        matchup_id = $matchup.matchup_id
        deck_a_id = $matchup.deck_a_id
        deck_a_sha256 = $matchup.deck_a_sha256
        deck_b_id = $matchup.deck_b_id
        deck_b_sha256 = $matchup.deck_b_sha256
        main_deck_counts = $matchup.main_deck_count
        extra_deck_counts = $matchup.extra_deck_count
        core_patchset_id = $lock.rule_affecting_inputs.core.patchset.id
        core_patchset_sha256 = $lock.rule_affecting_inputs.core.patchset.sha256
    }
    $artifacts = @(Get-ArtifactRecords)
    if (-not $SkipHeavy) {
        $script:HeavyEvidence = New-HeavyEvidenceFromRuns $resolvedHead $artifacts
    }
    $heavyReplayPass = Test-HeavyEvidenceRunPass $script:HeavyEvidence.replay
    $heavyLifecyclePass = Test-HeavyEvidenceRunPass $script:HeavyEvidence.lifecycle
    $heavyReplayEvidence = if ($SkipHeavy) { "heavy-replay-release@$expected" } else { 'heavy-replay-release' }
    $heavyLifecycleEvidence = if ($SkipHeavy) { "heavy-lifecycle-stress@$expected" } else { 'heavy-lifecycle-stress' }
    $g03Pass = (Test-AllRunsPass @('release-configure', 'release-build', 'release-normal-ctest', 'm4-canonical-full-game', 'm4-recommended-concurrency-soak')) -and $heavyReplayPass -and $heavyLifecyclePass
    $g26Pass = $heavyReplayPass

    $gates = @(
        (New-Gate 'P4A-G00' 'Public-fact sufficiency matrix is complete for the planned Teacher scope' 'All frozen rows have exact sources, executable evidence, availability, and explicit BLOCKED reasons.' @('public-fact-matrix') $(if (Test-RunPass 'public-fact-matrix') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G01' 'Existing full CTest remains green' 'Debug configure, Debug build, and every non-heavy registered Debug CTest test pass; heavy tests are covered by their dedicated H_exec evidence.' @('debug-configure', 'debug-build', 'debug-normal-ctest') $(if (Test-AllRunsPass @('debug-configure', 'debug-build', 'debug-normal-ctest')) { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G02' 'Existing Python verification remains green' 'Repository, M3, and M4 Python suites all pass.' @('repository-python', 'm3-python', 'm4-python') $(if (Test-AllRunsPass @('repository-python', 'm3-python', 'm4-python')) { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G03' 'Required M4 acceptance remains green' 'Release configure, Release build, non-heavy Release CTest, Heavy Replay, lifecycle stress, fixed-game, and recommended-concurrency soak all pass; clean checkouts inherit only the exact H_exec heavy evidence.' @('release-configure', 'release-build', 'release-normal-ctest', $heavyReplayEvidence, $heavyLifecycleEvidence, 'm4-canonical-full-game', 'm4-recommended-concurrency-soak') $(if ($g03Pass) { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G04' 'Rules bundle and locked deck identities are unchanged' 'The frozen rules/deck identity test passes.' @('rules-deck-identity', 'rules-bundle-verification') $(if (Test-AllRunsPass @('rules-deck-identity', 'rules-bundle-verification')) { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G05' 'PublicSafeState strict typed decode succeeds' 'The safe-state regression test passes.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G06' 'Typed safe-state round-trip is byte-identical' 'The focused public-safe/action identity tests pass.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G07' 'Safe-state malformed inputs fail closed' 'The focused public-safe-state negative cases pass.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G08' 'Selector API has no private-state dependency' 'Policy boundary Python and compile tests pass.' @('focused-policy-ctest', 'policy-boundary') $(if (Test-AllRunsPass @('focused-policy-ctest', 'policy-boundary')) { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G09' 'Candidate domain is preserved exactly' 'The RandomLegal domain instrumentation passes.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G10' 'Empty RandomLegal domain fails closed' 'The RandomLegal empty-domain test passes.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G11' 'RNG initialization is canonical' 'Policy RNG and trajectory codec focused tests pass.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G12' 'SHA-256-counter golden vectors match' 'The policy RNG focused test passes.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G13' 'Cursor advancement and rejection semantics are exact' 'The policy RNG focused test passes.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G14' 'Bounded sampler is unbiased under forced rejection' 'The policy RNG focused test passes.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G15' 'Production provenance registrations are typed and complete' 'The RandomLegal provenance tests pass.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G16' 'RandomLegal with NONE RNG is rejected' 'The RandomLegal/codec negative tests pass.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G17' 'RandomLegal with deterministic sampling is rejected' 'The RandomLegal/codec negative tests pass.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G18' 'No production identity uses ocgforge.test.*' 'The production source scan finds no test identity.' @('production-identity-source-scan') $(if (Test-RunPass 'production-identity-source-scan') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G19' 'Accepted RandomLegal key is an existing public_action_key' 'The RandomLegal domain test passes.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G20' 'No candidate-zero or first-candidate fallback exists' 'RandomLegal empty/error and nonzero-domain tests pass.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G21' 'RandomLegal is deterministic across independent processes' 'The policy determinism probe passes against the configured build.' @('policy-determinism') $(if (Test-RunPass 'policy-determinism') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G22' 'Paired hidden worlds produce equal policy output' 'The paired-world RandomLegal test passes.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G23' 'Policy state resets and isolates episodes/participants' 'The policy determinism probe proves fresh cursor and interleaving isolation.' @('policy-determinism') $(if (Test-RunPass 'policy-determinism') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G24' 'Policy-origin StepRejected creates zero record and quarantine' 'The Runner integration test proves no retry and quarantine.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G25' 'RandomLegal accepted actions record trusted provenance' 'The Runner integration test proves exact accepted attribution.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G26' 'V2 semantic replay admission remains strict' 'The dedicated Release Heavy Replay command exits zero and runs trajectory_replay_admission_test; clean checkouts use the exact H_exec result without rerunning it.' @($heavyReplayEvidence) $(if ($g26Pass) { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G27' 'Candidate shard, restricted evidence, receipt, and DatasetManifest integrate' 'The Runner integration test proves all persistence artifacts.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G28' 'Public-fact matrix is complete and executable' 'The matrix validator passes at the frozen source head.' @('public-fact-matrix') $(if (Test-RunPass 'public-fact-matrix') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G29' 'Evidence is reproducible from a clean checkout' 'A separate exact-head report must match all local semantic gates, semantic fingerprints, and stable artifact hashes.' @('clean-checkout-reproduction') 'NOT_RUN')
    )
    Assert-GateIds $gates
    $sourceBase = Get-GitValue @('merge-base', $resolvedHead, 'origin/main')
    $report = [ordered]@{
        schema_version = 'ocgforge.phase4a_acceptance.v1'
        status = if (@($gates | Where-Object { $_.status -eq 'FAIL' }).Count -eq 0) { 'INCOMPLETE' } else { 'FAIL' }
        acceptance_profile = if ($SkipHeavy) { 'clean-checkout-fast-normal' } else { 'h-exec-fast-normal-plus-heavy' }
        heavy_evidence_mode = if ($SkipHeavy) { 'inherited-exact-h-exec' } else { 'executed-at-h-exec' }
        source_head = $resolvedHead
        source_base = $sourceBase
        h_exec = $resolvedHead
        environment = $environment
        gates = $gates
        runs = @($script:Runs.Values)
        artifacts = $artifacts
        heavy_evidence = $script:HeavyEvidence
        runtime_budget = Get-RuntimeBudget
        reproduction = [ordered]@{
            status = 'NOT_RUN'
            clean_checkout_report = $null
            semantic_gate_match = $null
            artifact_hash_match = $null
        }
        semantic_fingerprint = Get-SemanticFingerprint $gates $artifacts
        limitations = @(
            'G29 remains NOT_RUN until the exact-head clean-checkout reproduction is compared.',
            'TeacherCore and both StrategyProfiles are outside Phase 4A.'
        )
    }
    Write-Reports $report
    if ($report.status -eq 'FAIL') { exit 1 } else { exit 0 }
}

$existingReportPath = Join-Path $outputDirectory 'p4a_acceptance.json'
if (-not (Test-Path -LiteralPath $existingReportPath)) {
    throw "existing H_exec acceptance report is missing: $existingReportPath"
}
$referencePath = Resolve-RepoPath $FinalizeFrom 'artifacts\p4a\clean-checkout\p4a_acceptance.json'
if (-not (Test-Path -LiteralPath $referencePath)) {
    throw "clean-checkout acceptance report is missing: $referencePath"
}
$baseReport = Get-Content -LiteralPath $existingReportPath -Raw | ConvertFrom-Json
$referenceReport = Get-Content -LiteralPath $referencePath -Raw | ConvertFrom-Json
if ($baseReport.source_head.ToLowerInvariant() -ne $expected -or
    $referenceReport.source_head.ToLowerInvariant() -ne $expected) {
    throw 'clean-checkout comparison is not bound to the requested H_exec'
}

try {
    $null = Assert-HExecAcceptanceReport $baseReport
    $baseProfileValid = $true
    $baseProfileError = $null
} catch {
    $baseProfileValid = $false
    $baseProfileError = $_.Exception.Message
}
try {
    $null = Assert-CleanAcceptanceReport $referenceReport
    $cleanProfileValid = $true
    $cleanProfileError = $null
} catch {
    $cleanProfileValid = $false
    $cleanProfileError = $_.Exception.Message
}

$profileMatch = $baseProfileValid -and $cleanProfileValid
$heavyEvidenceMatch = $false
if ($profileMatch) {
    $baseHeavyEvidenceJson = $baseReport.heavy_evidence | ConvertTo-Json -Depth 30 -Compress
    $referenceHeavyEvidenceJson = $referenceReport.heavy_evidence | ConvertTo-Json -Depth 30 -Compress
    $heavyEvidenceMatch = $baseHeavyEvidenceJson -eq $referenceHeavyEvidenceJson
}

$baseGateMap = @{}
$referenceGateMap = @{}
$semanticGateMatch = $false
if ($profileMatch) {
    foreach ($gate in $baseReport.gates) { $baseGateMap[$gate.gate] = $gate.status }
    foreach ($gate in $referenceReport.gates) { $referenceGateMap[$gate.gate] = $gate.status }
    $semanticGateMatch = $true
    foreach ($gateId in $baseGateMap.Keys) {
        if ($gateId -eq 'P4A-G29') { continue }
        if ($baseGateMap[$gateId] -ne 'PASS' -or $referenceGateMap[$gateId] -ne $baseGateMap[$gateId]) {
            $semanticGateMatch = $false
        }
    }
}

$artifactHashMatch = $false
$semanticFingerprintMatch = $false
if ($profileMatch) {
    $baseArtifacts = @($baseReport.semantic_fingerprint.semantic_artifacts | ForEach-Object {
            if ($null -ne $_.role) { "$($_.role)|$($_.bytes)|$($_.sha256)" }
            else { "$($_.path)|$($_.bytes)|$($_.sha256)" }
        } | Sort-Object)
    $referenceArtifacts = @($referenceReport.semantic_fingerprint.semantic_artifacts | ForEach-Object {
            if ($null -ne $_.role) { "$($_.role)|$($_.bytes)|$($_.sha256)" }
            else { "$($_.path)|$($_.bytes)|$($_.sha256)" }
        } | Sort-Object)
    $artifactHashMatch = (($baseArtifacts -join "`n") -eq ($referenceArtifacts -join "`n"))
    $baseSemanticFingerprint = $baseReport.semantic_fingerprint | ConvertTo-Json -Depth 30 -Compress
    $referenceSemanticFingerprint = $referenceReport.semantic_fingerprint | ConvertTo-Json -Depth 30 -Compress
    $semanticFingerprintMatch = $baseSemanticFingerprint -eq $referenceSemanticFingerprint
}

$finalizationMatch = $profileMatch -and $heavyEvidenceMatch -and $semanticGateMatch -and $semanticFingerprintMatch -and $artifactHashMatch
$finalGate = New-Gate 'P4A-G29' 'Evidence is reproducible from a clean checkout' 'The exact H_exec and clean-checkout profiles, Heavy-run cardinalities, inherited Heavy evidence, semantic fingerprints, and stable artifact hashes all match.' @('clean-checkout-reproduction') $(if ($finalizationMatch) { 'PASS' } else { 'FAIL' })
$finalGates = @($baseReport.gates | Where-Object { $_.gate -ne 'P4A-G29' })
$finalGates += $finalGate
$baseReport.gates = $finalGates
$baseReport.status = if ($finalizationMatch -and (@($finalGates | Where-Object { $_.status -eq 'FAIL' }).Count -eq 0)) { 'PASS' } else { 'FAIL' }
$baseReport.reproduction = [ordered]@{
    status = if ($finalizationMatch) { 'PASS' } else { 'FAIL' }
    clean_checkout_report = Get-RepoRelativePath $referencePath
    clean_checkout_report_sha256 = Get-FileSha256 $referencePath
    profile_match = $profileMatch
    base_profile_valid = $baseProfileValid
    clean_profile_valid = $cleanProfileValid
    base_profile_error = $baseProfileError
    clean_profile_error = $cleanProfileError
    heavy_evidence_match = $heavyEvidenceMatch
    semantic_gate_match = $semanticGateMatch
    semantic_fingerprint_match = $semanticFingerprintMatch
    artifact_hash_match = $artifactHashMatch
    compared_head = $expected
}
$baseReport.limitations = @(
    'This report is bound to the immutable H_exec source head.',
    'TeacherCore and both StrategyProfiles are outside Phase 4A.'
)
Write-Reports $baseReport
if ($baseReport.status -eq 'PASS') { exit 0 } else { exit 1 }
