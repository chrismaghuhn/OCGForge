[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedHead,
    [string]$RulesCache,
    [string]$ToolchainRoot,
    [string]$EvidenceOutput,
    [string]$WorktreeParent
)

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$expected = $ExpectedHead.ToLowerInvariant()

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,
        [Parameter(Mandatory = $false)]
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory
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

if ([string]::IsNullOrWhiteSpace($RulesCache)) {
    $RulesCache = Join-Path $repoRoot '.cache\rules_bundle'
}
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    $ToolchainRoot = $repoRoot
}
if ([string]::IsNullOrWhiteSpace($EvidenceOutput)) {
    $EvidenceOutput = Join-Path $repoRoot 'artifacts\episodic\v2\g32-clean-checkout'
}

$resolvedRulesCache = (Resolve-Path -LiteralPath $RulesCache).Path
$resolvedToolchainRoot = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$resolvedEvidenceOutput = [System.IO.Path]::GetFullPath($EvidenceOutput)
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
if ($resolvedExpected -ne $expected) {
    throw "expected head does not resolve exactly: requested $expected, resolved $resolvedExpected"
}

if ([string]::IsNullOrWhiteSpace($WorktreeParent)) {
    $worktreeParent = Join-Path (Split-Path -Parent $repoRoot) 'ocgforge-g32'
} else {
    $worktreeParent = [System.IO.Path]::GetFullPath($WorktreeParent)
}
New-Item -ItemType Directory -Force -Path $worktreeParent | Out-Null
$worktree = Join-Path $worktreeParent ("g32-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
$build = Join-Path $worktree 'build\g32-clean'
$runA = Join-Path $worktree 'artifacts\episodic\v2\g32-run-a'
$runB = Join-Path $worktree 'artifacts\episodic\v2\g32-run-b'

try {
    Invoke-Checked -Command 'git' -Arguments @('-C', $repoRoot, 'worktree', 'add', '--detach', $worktree, $expected) -WorkingDirectory $repoRoot
    $actualHead = (& git -C $worktree rev-parse HEAD).Trim().ToLowerInvariant()
    if ($actualHead -ne $expected) {
        throw "clean worktree HEAD mismatch: expected $expected, got $actualHead"
    }
    $status = (& git -C $worktree status --porcelain)
    if (-not [string]::IsNullOrWhiteSpace(($status -join ''))) {
        throw 'clean worktree is not clean before acceptance'
    }

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

    # The checked-in Zig fallback has a known CMake/Ninja archive-finalizer
    # mismatch. Repair only this generated file in the temporary worktree.
    $rulesFile = Join-Path $build 'CMakeFiles\rules.ninja'
    if (Test-Path -LiteralPath $rulesFile) {
        $rulesContent = Get-Content -LiteralPath $rulesFile -Raw
        $rulesContent = $rulesContent.Replace('zig-ar.cmd $TARGET_FILE &&', 'zig-ranlib.cmd $TARGET_FILE &&')
        [System.IO.File]::WriteAllText($rulesFile, $rulesContent)
    }
    Invoke-Checked -Command 'cmake' -Arguments @('--build', $build, '--parallel', '4') -WorkingDirectory $worktree

    foreach ($runDirectory in @($runA, $runB)) {
        New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
        $acceptanceArguments = @(
            'tools/episodic/acceptance.py',
            '--all',
            '--probe', (Join-Path $build 'ygo_episodic_probe.exe'),
            '--build-dir', $build,
            '--output', $runDirectory
        )
        # A non-zero result is expected when the manifest contains an honest
        # BLOCKED gate (for example unavailable native/hosted checks).
        Push-Location -LiteralPath $worktree
        try {
            & python @acceptanceArguments
            $acceptanceExitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        if ($acceptanceExitCode -gt 1) {
            throw "acceptance runner failed with execution exit code $acceptanceExitCode"
        }
    }

    $manifestA = Join-Path $runA 'episodic_acceptance_manifest.json'
    $manifestB = Join-Path $runB 'episodic_acceptance_manifest.json'
    if (-not (Test-Path -LiteralPath $manifestA) -or -not (Test-Path -LiteralPath $manifestB)) {
        throw 'clean-checkout acceptance did not produce both manifests'
    }
    $bytesA = [System.IO.File]::ReadAllBytes($manifestA)
    $bytesB = [System.IO.File]::ReadAllBytes($manifestB)
    if (-not [System.Linq.Enumerable]::SequenceEqual($bytesA, $bytesB)) {
        throw 'clean-checkout acceptance manifest changed between identical renders'
    }

    Invoke-Checked -Command 'git' -Arguments @('-C', $worktree, 'diff', '--check') -WorkingDirectory $worktree
    $afterEvidenceStatus = (& git -C $worktree status --porcelain)
    $cleanAfterEvidence = [string]::IsNullOrWhiteSpace(($afterEvidenceStatus -join ''))
    if (-not $cleanAfterEvidence) {
        throw 'clean worktree is not clean after acceptance evidence generation'
    }

    New-Item -ItemType Directory -Force -Path $resolvedEvidenceOutput | Out-Null
    Copy-Item -LiteralPath $manifestA -Destination (Join-Path $resolvedEvidenceOutput 'episodic_acceptance_manifest.json') -Force
    $summary = [ordered]@{
        schema = 'ocgforge.episodic_clean_checkout_acceptance.v1'
        result = 'PASS'
        expected_head = $expected
        actual_head = $actualHead
        clean_worktree = $true
        clean_worktree_after_evidence = $cleanAfterEvidence
        git_diff_check = $true
        identical_manifest_renders = $true
        worktree_path = $worktree
        rules_cache = $resolvedRulesCache
        native_msbuild = 'NOT_RUN'
        hosted_equivalence = 'NOT_RUN'
    }
    $summary | ConvertTo-Json -Compress | Set-Content -LiteralPath (Join-Path $resolvedEvidenceOutput 'g32_clean_checkout.json') -Encoding utf8
    Get-Content -LiteralPath (Join-Path $resolvedEvidenceOutput 'g32_clean_checkout.json')
} finally {
    if (Test-Path -LiteralPath $worktree) {
        Write-Host "clean checkout retained for inspection: $worktree"
    }
}
