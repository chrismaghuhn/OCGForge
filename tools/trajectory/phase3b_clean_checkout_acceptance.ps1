[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedHead,
    [string]$RulesCache,
    [string]$ToolchainRoot,
    [string]$EvidenceOutput,
    [string]$WorktreeParent,
    [switch]$SkipFullCTest
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$expected = $ExpectedHead.ToLowerInvariant()

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)] [string]$Command,
        [Parameter(Mandatory = $false)] [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)] [string]$WorkingDirectory
    )

    Push-Location -LiteralPath $WorkingDirectory
    try {
        & $Command @Arguments
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($exitCode -ne 0) {
        throw "$Command failed with exit code $exitCode"
    }
}

function Invoke-Captured {
    param(
        [Parameter(Mandatory = $true)] [string]$Command,
        [Parameter(Mandatory = $false)] [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)] [string]$WorkingDirectory,
        [Parameter(Mandatory = $true)] [string]$OutputPath
    )

    Push-Location -LiteralPath $WorkingDirectory
    try {
        $output = @(& $Command @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    $output | Set-Content -LiteralPath $OutputPath -Encoding utf8
    if ($exitCode -ne 0) {
        throw "$Command failed with exit code $exitCode"
    }
    return $output
}

if ([string]::IsNullOrWhiteSpace($RulesCache)) {
    $RulesCache = Join-Path $repoRoot '.cache\rules_bundle'
}
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    $ToolchainRoot = $repoRoot
}
if ([string]::IsNullOrWhiteSpace($EvidenceOutput)) {
    $EvidenceOutput = Join-Path $repoRoot 'artifacts\trajectory\phase3b-clean-checkout'
}
if ([string]::IsNullOrWhiteSpace($WorktreeParent)) {
    $WorktreeParent = Join-Path (Split-Path -Parent $repoRoot) 'ocgforge-phase3b-clean'
}

$resolvedRulesCache = (Resolve-Path -LiteralPath $RulesCache).Path
$resolvedToolchainRoot = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$resolvedEvidenceOutput = [System.IO.Path]::GetFullPath($EvidenceOutput)
$resolvedWorktreeParent = [System.IO.Path]::GetFullPath($WorktreeParent)
$ninja = Join-Path $resolvedToolchainRoot '.cache\toolchain\ninja\ninja.exe'
$cc = Join-Path $resolvedToolchainRoot 'tools\zig-cc.cmd'
$cxx = Join-Path $resolvedToolchainRoot 'tools\zig-cxx.cmd'
$ar = Join-Path $resolvedToolchainRoot 'tools\zig-ar.cmd'
$ranlib = Join-Path $resolvedToolchainRoot 'tools\zig-ranlib.cmd'

foreach ($requiredPath in @($resolvedRulesCache, $ninja, $cc, $cxx, $ar, $ranlib)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "required clean-checkout dependency is missing: $requiredPath"
    }
}

$resolvedExpected = (& git -C $repoRoot rev-parse --verify "$expected`^{commit}").Trim().ToLowerInvariant()
if ($LASTEXITCODE -ne 0 -or $resolvedExpected -ne $expected) {
    throw "expected head does not resolve exactly: requested $expected, resolved $resolvedExpected"
}

New-Item -ItemType Directory -Force -Path $resolvedWorktreeParent | Out-Null
$worktree = Join-Path $resolvedWorktreeParent ("phase3b-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
$build = Join-Path $worktree 'build\phase3b-clean'
$evidenceRoot = Join-Path $worktree 'artifacts\trajectory\phase3b'
$runA = Join-Path $evidenceRoot 'determinism-run-a.txt'
$runB = Join-Path $evidenceRoot 'determinism-run-b.txt'
$artifactRunA = Join-Path $evidenceRoot 'artifact-run-a'
$artifactRunB = Join-Path $evidenceRoot 'artifact-run-b'

try {
    Invoke-Checked -Command 'git' -Arguments @('-C', $repoRoot, 'worktree', 'add', '--detach', $worktree, $expected) -WorkingDirectory $repoRoot
    $actualHead = (& git -C $worktree rev-parse HEAD).Trim().ToLowerInvariant()
    if ($actualHead -ne $expected) {
        throw "clean worktree HEAD mismatch: expected $expected, got $actualHead"
    }
    $before = (& git -C $worktree status --porcelain)
    if (-not [string]::IsNullOrWhiteSpace(($before -join ''))) {
        throw 'clean worktree is not clean before acceptance'
    }

    Invoke-Checked -Command 'python' -Arguments @(
        'tools/verify_rules_bundle.py',
        '--lock', 'third_party/rules_bundle.lock.json',
        '--cache', $resolvedRulesCache
    ) -WorkingDirectory $worktree

    $configureArguments = @(
        '-S', $worktree,
        '-B', $build,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Debug',
        "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_C_COMPILER=$cc",
        "-DCMAKE_CXX_COMPILER=$cxx",
        "-DCMAKE_AR=$ar",
        "-DCMAKE_RANLIB=$ranlib",
        '-DM0_AUTO_FETCH_RULES=OFF',
        "-DM0_RULES_CACHE=$resolvedRulesCache"
    )
    Invoke-Checked -Command 'cmake' -Arguments $configureArguments -WorkingDirectory $worktree

    # The repository's Zig fallback may emit an archive-finalizer command that
    # is incompatible with the wrapper. This edits generated build metadata
    # only inside the temporary clean worktree.
    $rulesFile = Join-Path $build 'CMakeFiles\rules.ninja'
    if (Test-Path -LiteralPath $rulesFile) {
        $rulesContent = Get-Content -LiteralPath $rulesFile -Raw
        $rulesContent = $rulesContent.Replace('zig-ar.cmd $TARGET_FILE &&', 'zig-ranlib.cmd $TARGET_FILE &&')
        [System.IO.File]::WriteAllText($rulesFile, $rulesContent)
    }
    Invoke-Checked -Command 'cmake' -Arguments @('--build', $build, '--parallel', '4') -WorkingDirectory $worktree

    Invoke-Checked -Command 'python' -Arguments @('-m', 'unittest', 'discover', '-s', 'tests/python', '-v') -WorkingDirectory $worktree
    Invoke-Checked -Command 'python' -Arguments @('tests/protocol/decision_coverage_test.py') -WorkingDirectory $worktree
    Invoke-Checked -Command 'python' -Arguments @('tests/observation/observation_coverage_test.py') -WorkingDirectory $worktree
    Invoke-Checked -Command 'python' -Arguments @('tests/episodic/episode_driver_ownership_guard.py') -WorkingDirectory $worktree
    Invoke-Checked -Command 'python' -Arguments @('-B', '-m', 'unittest', 'tests.m4.test_benchmark_integrity', 'tests.m4.test_failure_isolation', 'tests.m4.test_job_generation', 'tests.m4.test_process_metrics', 'tests.m4.test_worker_protocol', '-v') -WorkingDirectory $worktree
    Invoke-Checked -Command 'python' -Arguments @(
        'tests/determinism/m1_engine_determinism_test.py',
        '--probe', (Join-Path $build 'm1_engine_conformance_test.exe')
    ) -WorkingDirectory $worktree
    Invoke-Checked -Command 'python' -Arguments @(
        'tests/determinism/determinism_test.py',
        '--probe', (Join-Path $build 'ygo_core_probe.exe')
    ) -WorkingDirectory $worktree

    $trajectoryRegex = '^(trajectory_codec_test|trajectory_recorder_test|trajectory_shard_test|trajectory_restricted_evidence_test|trajectory_replay_admission_test|trajectory_receipt_test|trajectory_dataset_manifest_test|trajectory_artifact_determinism_test|trajectory_privacy_test)$'
    Invoke-Checked -Command 'ctest' -Arguments @('--test-dir', $build, '--tests-regex', $trajectoryRegex, '--output-on-failure') -WorkingDirectory $worktree
    if (-not $SkipFullCTest) {
        Invoke-Checked -Command 'ctest' -Arguments @('--test-dir', $build, '--output-on-failure') -WorkingDirectory $worktree
    }

    New-Item -ItemType Directory -Force -Path $evidenceRoot | Out-Null
    $probe = Join-Path $build 'trajectory_artifact_determinism_test.exe'
    $outputA = Invoke-Captured -Command $probe -Arguments @('--output-dir', $artifactRunA) -WorkingDirectory $worktree -OutputPath $runA
    $outputB = Invoke-Captured -Command $probe -Arguments @('--output-dir', $artifactRunB) -WorkingDirectory $worktree -OutputPath $runB
    $bytesA = [System.IO.File]::ReadAllBytes($runA)
    $bytesB = [System.IO.File]::ReadAllBytes($runB)
    if (-not [System.Linq.Enumerable]::SequenceEqual($bytesA, $bytesB)) {
        throw 'trajectory artifact determinism output differs between independent processes'
    }
    foreach ($artifactName in @(
        'episode_envelope.bin',
        'candidate_shard.bin',
        'restricted_evidence.bin',
        'admission_receipt.bin',
        'dataset_manifest.bin'
    )) {
        $artifactA = Join-Path $artifactRunA $artifactName
        $artifactB = Join-Path $artifactRunB $artifactName
        if (-not (Test-Path -LiteralPath $artifactA) -or
            -not (Test-Path -LiteralPath $artifactB)) {
            throw "determinism probe omitted canonical artifact: $artifactName"
        }
        $artifactBytesA = [System.IO.File]::ReadAllBytes($artifactA)
        $artifactBytesB = [System.IO.File]::ReadAllBytes($artifactB)
        if (-not [System.Linq.Enumerable]::SequenceEqual($artifactBytesA, $artifactBytesB)) {
            throw "canonical artifact differs between independent processes: $artifactName"
        }
    }

    $values = [ordered]@{}
    foreach ($line in $outputA) {
        if ($line -match '^(?<key>[a-z0-9_]+)=(?<value>.*)$') {
            $values[$Matches.key] = $Matches.value
        }
    }
    foreach ($requiredKey in @(
        'candidate_shard_artifact_sha256',
        'restricted_evidence_artifact_sha256',
        'admission_receipt_id',
        'dataset_semantic_id',
        'canonical_bytes_sha256'
    )) {
        if (-not $values.Contains($requiredKey)) {
            throw "determinism probe omitted required output: $requiredKey"
        }
    }

    Invoke-Checked -Command 'git' -Arguments @('-C', $worktree, 'diff', '--check') -WorkingDirectory $worktree
    $after = (& git -C $worktree status --porcelain)
    $cleanAfterEvidence = [string]::IsNullOrWhiteSpace(($after -join ''))
    if (-not $cleanAfterEvidence) {
        throw 'clean worktree is not clean after acceptance evidence generation'
    }

    New-Item -ItemType Directory -Force -Path $resolvedEvidenceOutput | Out-Null
    Copy-Item -LiteralPath $runA -Destination (Join-Path $resolvedEvidenceOutput 'determinism-run-a.txt') -Force
    Copy-Item -LiteralPath $runB -Destination (Join-Path $resolvedEvidenceOutput 'determinism-run-b.txt') -Force
    $summary = [ordered]@{
        schema = 'ocgforge.phase3b_clean_checkout_acceptance.v1'
        result = if ($SkipFullCTest) { 'NOT_RUN' } else { 'PASS' }
        expected_head = $expected
        actual_head = $actualHead
        clean_worktree_before_evidence = $true
        clean_worktree_after_evidence = $cleanAfterEvidence
        git_diff_check = $true
        trajectory_artifact_process_outputs_identical = $true
        trajectory_canonical_artifact_bytes_identical = $true
        full_ctest = if ($SkipFullCTest) { 'NOT_RUN' } else { 'PASS' }
        artifact_fixture = $values
    }
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $resolvedEvidenceOutput 'phase3b_clean_checkout.json') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $resolvedEvidenceOutput 'phase3b_clean_checkout.json')
} finally {
    if (Test-Path -LiteralPath $worktree) {
        Write-Host "clean checkout retained for inspection: $worktree"
    }
}
