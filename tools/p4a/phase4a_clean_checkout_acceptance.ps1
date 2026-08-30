[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedHead,
    [string]$OutputDirectory,
    [string]$ArtifactDirectory,
    [string]$FinalizeFrom
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
    $unittest = [regex]::Matches($Output, '(?m)^Ran\s+(\d+)\s+tests?\s*$')
    if ($unittest.Count -gt 0) {
        $total = [int]$unittest[$unittest.Count - 1].Groups[1].Value
        return [ordered]@{
            kind = 'unittest'
            passed = if ($Output -match '(?m)^OK\s*$') { $total } else { 0 }
            failed = if ($Output -match '(?m)^OK\s*$') { 0 } else { $total }
            total = $total
        }
    }
    return $null
}

$script:Runs = [ordered]@{}
$script:RunOutputs = @{}

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
    $record = [ordered]@{
        label = $Label
        command = $DisplayCommand
        exit_code = $exitCode
        result = if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' }
        elapsed_seconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
        output_summary = [ordered]@{
            sha256 = Get-TextSha256 $output
            utf8_bytes = [System.Text.UTF8Encoding]::new($false).GetByteCount($output)
            line_count = $outputLines.Count
            first_line = if ($outputLines.Count -gt 0) { $outputLines[0] } else { '' }
            last_line = if ($outputLines.Count -gt 0) { $outputLines[$outputLines.Count - 1] } else { '' }
        }
    }
    $counts = Get-OutputCounts $output
    if ($null -ne $counts) {
        $record.test_counts = $counts
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

function Get-SemanticFingerprint {
    param([Parameter(Mandatory = $true)][object[]]$Gates, [Parameter(Mandatory = $true)][object[]]$Artifacts)
    $gateResults = [ordered]@{}
    foreach ($gate in $Gates) {
        if ($gate.gate -ne 'P4A-G29') {
            $gateResults[$gate.gate] = $gate.status
        }
    }
    $counts = [ordered]@{}
    foreach ($label in @('focused-policy-ctest', 'debug-full-ctest', 'release-full-ctest',
                         'repository-python', 'm3-python', 'm4-python')) {
        if ($script:Runs.Contains($label) -and $null -ne $script:Runs[$label].test_counts) {
            $counts[$label] = $script:Runs[$label].test_counts
        }
    }
    $semanticArtifacts = @($Artifacts | Where-Object {
            $_.path -like '*/m4/full_game/full_fixed_deck_results.json' -or
            $_.path -like '*/m4/lifecycle_stress.json'
        } | ForEach-Object {
            [ordered]@{ path = $_.path; sha256 = $_.sha256; bytes = $_.bytes }
        })
    return [ordered]@{
        gate_results = $gateResults
        command_counts = $counts
        semantic_artifacts = $semanticArtifacts
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

if ([string]::IsNullOrWhiteSpace($FinalizeFrom)) {
    $statusOutput = @(& git -C $repoRoot status --porcelain 2>$null)
    if ($LASTEXITCODE -ne 0) {
        throw 'git command failed: status --porcelain'
    }
    $status = ($statusOutput -join "`n").Trim()
    if (-not [string]::IsNullOrWhiteSpace($status)) {
        throw 'H_exec acceptance must start from a clean worktree'
    }

    New-Item -ItemType Directory -Force -Path $artifactDirectory | Out-Null
    $focusedRegex = '^(trajectory_codec_test|trajectory_recorder_test|trajectory_shard_test|trajectory_restricted_evidence_test|trajectory_receipt_test|trajectory_dataset_manifest_test|public_action_identity_test|public_safe_state_test|policy_boundary_compile_test|policy_rng_test|random_legal_test|policy_runner_integration_test)$'

    $null = Invoke-Recorded 'debug-configure' 'cmake' @('--preset', 'dev-windows-zig') 'cmake --preset dev-windows-zig'
    $null = Invoke-Recorded 'debug-build' 'cmake' @('--build', '--preset', 'dev-windows-zig', '--parallel') 'cmake --build --preset dev-windows-zig --parallel'
    $null = Invoke-Recorded 'focused-policy-ctest' 'ctest' @('--preset', 'dev-windows-zig', '-R', $focusedRegex, '--output-on-failure') "ctest --preset dev-windows-zig -R $focusedRegex --output-on-failure"
    $null = Invoke-Recorded 'debug-full-ctest' 'ctest' @('--preset', 'dev-windows-zig', '--output-on-failure') 'ctest --preset dev-windows-zig --output-on-failure'

    $null = Invoke-Recorded 'release-configure' 'cmake' @('--preset', 'release-windows-zig') 'cmake --preset release-windows-zig'
    $null = Invoke-Recorded 'release-build' 'cmake' @('--build', '--preset', 'release-windows-zig', '--parallel') 'cmake --build --preset release-windows-zig --parallel'
    $null = Invoke-Recorded 'release-full-ctest' 'ctest' @('--preset', 'release-windows-zig', '--output-on-failure') 'ctest --preset release-windows-zig --output-on-failure'

    $null = Invoke-Recorded 'repository-python' 'python' @('-B', '-m', 'unittest', 'discover', '-s', 'tests/python', '-v') 'python -B -m unittest discover -s tests/python -v'
    $null = Invoke-Recorded 'm3-python' 'python' @('-B', '-m', 'unittest', 'discover', '-s', 'tests/m3', '-v') 'python -B -m unittest discover -s tests/m3 -v'

    $previousWorker = $env:YGO_M4_WORKER
    $env:YGO_M4_WORKER = 'build/windows-zig/ygo_m4_worker.exe'
    try {
        $null = Invoke-Recorded 'm4-python' 'python' @('-B', '-m', 'unittest', 'discover', '-s', 'tests/m4', '-v') 'YGO_M4_WORKER=build/windows-zig/ygo_m4_worker.exe python -B -m unittest discover -s tests/m4 -v'
    } finally {
        if ($null -eq $previousWorker) {
            Remove-Item Env:YGO_M4_WORKER -ErrorAction SilentlyContinue
        } else {
            $env:YGO_M4_WORKER = $previousWorker
        }
    }

    $null = Invoke-Recorded 'public-fact-matrix' 'python' @('-B', 'tests/policy/public_fact_matrix_test.py') 'python -B tests/policy/public_fact_matrix_test.py'
    $null = Invoke-Recorded 'rules-deck-identity' 'python' @('-B', 'tests/policy/rules_deck_identity_test.py') 'python -B tests/policy/rules_deck_identity_test.py'
    $null = Invoke-Recorded 'policy-determinism' 'python' @('-B', 'tests/policy/policy_determinism_test.py') 'python -B tests/policy/policy_determinism_test.py'
    $null = Invoke-Recorded 'policy-boundary' 'python' @('-B', 'tests/policy/policy_boundary_test.py') 'python -B tests/policy/policy_boundary_test.py'
    $null = Invoke-Recorded 'rules-bundle-verification' 'python' @('-B', 'tools/verify_rules_bundle.py', '--lock', 'third_party/rules_bundle.lock.json', '--cache', '.cache/rules_bundle') 'python -B tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle'
    $null = Invoke-IdentitySourceScan

    $fullGameOutput = Get-RepoRelativePath (Join-Path $artifactDirectory 'm4\full_game')
    $null = Invoke-Recorded 'm4-canonical-full-game' 'python' @('-B', 'tests/m3/full_game/full_fixed_deck_test.py', '--probe', 'build/release-windows-zig/ygo_core_probe.exe', '--games', '16', '--max-steps', '2200', '--timeout', '300', '--output', $fullGameOutput) "python -B tests/m3/full_game/full_fixed_deck_test.py --probe build/release-windows-zig/ygo_core_probe.exe --games 16 --max-steps 2200 --timeout 300 --output $fullGameOutput"

    $lifecycleOutput = Get-RepoRelativePath (Join-Path $artifactDirectory 'm4\lifecycle_stress.json')
    $null = Invoke-Recorded 'm4-lifecycle-stress' 'python' @('-B', 'tools/m4/record_lifecycle_stress_evidence.py', '--repetitions', '5', '--internal-repetitions', '100', '--output', $lifecycleOutput) "python -B tools/m4/record_lifecycle_stress_evidence.py --repetitions 5 --internal-repetitions 100 --output $lifecycleOutput"

    $soakOutput = Get-RepoRelativePath (Join-Path $artifactDirectory 'm4\soak.json')
    $null = Invoke-Recorded 'm4-recommended-concurrency-soak' 'python' @('-B', '-m', 'tools.m4.benchmark', '--worker-executable', 'build/release-windows-zig/ygo_m4_worker.exe', '--games', '128', '--workers', '16', '--master-seed', '20260815', '--mode', 'throughput', '--warmup-games', '4', '--observation-mode', 'full', '--output', $soakOutput) "python -B -m tools.m4.benchmark --worker-executable build/release-windows-zig/ygo_m4_worker.exe --games 128 --workers 16 --master-seed 20260815 --mode throughput --warmup-games 4 --observation-mode full --output $soakOutput"

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

    $gates = @(
        (New-Gate 'P4A-G00' 'Public-fact sufficiency matrix is complete for the planned Teacher scope' 'All frozen rows have exact sources, executable evidence, availability, and explicit BLOCKED reasons.' @('public-fact-matrix') $(if (Test-RunPass 'public-fact-matrix') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G01' 'Existing full CTest remains green' 'Debug configure, Debug build, and all currently registered Debug CTest tests pass.' @('debug-configure', 'debug-build', 'debug-full-ctest') $(if (Test-AllRunsPass @('debug-configure', 'debug-build', 'debug-full-ctest')) { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G02' 'Existing Python verification remains green' 'Repository, M3, and M4 Python suites all pass.' @('repository-python', 'm3-python', 'm4-python') $(if (Test-AllRunsPass @('repository-python', 'm3-python', 'm4-python')) { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G03' 'Required M4 acceptance remains green' 'Release configure, Release build, Release CTest, fixed-game, lifecycle, and recommended-concurrency soak all pass.' @('release-configure', 'release-build', 'release-full-ctest', 'm4-canonical-full-game', 'm4-lifecycle-stress', 'm4-recommended-concurrency-soak') $(if (Test-AllRunsPass @('release-configure', 'release-build', 'release-full-ctest', 'm4-canonical-full-game', 'm4-lifecycle-stress', 'm4-recommended-concurrency-soak')) { 'PASS' } else { 'FAIL' })),
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
        (New-Gate 'P4A-G26' 'V2 semantic replay admission remains strict' 'Both full CTest runs pass and both outputs include trajectory_replay_admission_test.' @('debug-full-ctest', 'release-full-ctest') $(if ((Test-RunPass 'debug-full-ctest') -and (Test-RunPass 'release-full-ctest') -and ($script:RunOutputs['debug-full-ctest'] -match 'trajectory_replay_admission_test') -and ($script:RunOutputs['release-full-ctest'] -match 'trajectory_replay_admission_test')) { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G27' 'Candidate shard, restricted evidence, receipt, and DatasetManifest integrate' 'The Runner integration test proves all persistence artifacts.' @('focused-policy-ctest') $(if (Test-RunPass 'focused-policy-ctest') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G28' 'Public-fact matrix is complete and executable' 'The matrix validator passes at the frozen source head.' @('public-fact-matrix') $(if (Test-RunPass 'public-fact-matrix') { 'PASS' } else { 'FAIL' })),
        (New-Gate 'P4A-G29' 'Evidence is reproducible from a clean checkout' 'A separate exact-head report must match all local semantic gates, semantic fingerprints, and stable artifact hashes.' @('clean-checkout-reproduction') 'NOT_RUN')
    )
    Assert-GateIds $gates
    $artifacts = @(Get-ArtifactRecords)
    $sourceBase = Get-GitValue @('merge-base', $resolvedHead, 'origin/main')
    $report = [ordered]@{
        schema_version = 'ocgforge.phase4a_acceptance.v1'
        status = if (@($gates | Where-Object { $_.status -eq 'FAIL' }).Count -eq 0) { 'INCOMPLETE' } else { 'FAIL' }
        source_head = $resolvedHead
        source_base = $sourceBase
        h_exec = $resolvedHead
        environment = $environment
        gates = $gates
        runs = @($script:Runs.Values)
        artifacts = $artifacts
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

$baseGateMap = @{}
$referenceGateMap = @{}
foreach ($gate in $baseReport.gates) { $baseGateMap[$gate.gate] = $gate.status }
foreach ($gate in $referenceReport.gates) { $referenceGateMap[$gate.gate] = $gate.status }
Assert-GateIds $baseReport.gates
Assert-GateIds $referenceReport.gates
$semanticGateMatch = $true
foreach ($gateId in $baseGateMap.Keys) {
    if ($gateId -eq 'P4A-G29') { continue }
    if ($baseGateMap[$gateId] -ne 'PASS' -or $referenceGateMap[$gateId] -ne $baseGateMap[$gateId]) {
        $semanticGateMatch = $false
    }
}
$baseArtifacts = @($baseReport.semantic_fingerprint.semantic_artifacts | ForEach-Object { "$($_.path)|$($_.bytes)|$($_.sha256)" } | Sort-Object)
$referenceArtifacts = @($referenceReport.semantic_fingerprint.semantic_artifacts | ForEach-Object { "$($_.path)|$($_.bytes)|$($_.sha256)" } | Sort-Object)
$artifactHashMatch = (($baseArtifacts -join "`n") -eq ($referenceArtifacts -join "`n"))
$baseSemanticFingerprint = $baseReport.semantic_fingerprint | ConvertTo-Json -Depth 30 -Compress
$referenceSemanticFingerprint = $referenceReport.semantic_fingerprint | ConvertTo-Json -Depth 30 -Compress
$semanticFingerprintMatch = $baseSemanticFingerprint -eq $referenceSemanticFingerprint

$finalGate = New-Gate 'P4A-G29' 'Evidence is reproducible from a clean checkout' 'The exact H_exec clean-checkout report matches all local semantic gate results, semantic fingerprints, and stable artifact hashes.' @('clean-checkout-reproduction') $(if ($semanticGateMatch -and $semanticFingerprintMatch -and $artifactHashMatch) { 'PASS' } else { 'FAIL' })
$finalGates = @($baseReport.gates | Where-Object { $_.gate -ne 'P4A-G29' })
$finalGates += $finalGate
$baseReport.gates = $finalGates
$baseReport.status = if ($semanticGateMatch -and $semanticFingerprintMatch -and $artifactHashMatch -and (@($finalGates | Where-Object { $_.status -eq 'FAIL' }).Count -eq 0)) { 'PASS' } else { 'FAIL' }
$baseReport.reproduction = [ordered]@{
    status = if ($semanticGateMatch -and $semanticFingerprintMatch -and $artifactHashMatch) { 'PASS' } else { 'FAIL' }
    clean_checkout_report = Get-RepoRelativePath $referencePath
    clean_checkout_report_sha256 = Get-FileSha256 $referencePath
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
