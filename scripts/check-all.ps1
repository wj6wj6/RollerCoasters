#Requires -Version 7.4
# Runs every check that AGENTS.md asks for before a merge, in one go: format and
# repository policy, the shared MSVC /analyze pass, then build, CTest and the
# installed --version for MSVC and Clang in Debug and Release. The Windows
# workflow runs this same script, so a local pass is the same gate as CI, and
# it is the way to verify a PR when the Actions quota has run out.
# Writes a summary to out/check-summary.md for the PR description.
param(
    # Parallel build jobs passed to check.ps1; 0 uses every processor.
    [ValidateRange(0, 256)]
    [int] $Jobs = 1,
    [ValidateSet('msvc', 'clang')]
    [string[]] $Compiler = @('msvc', 'clang'),
    [ValidateSet('Debug', 'Release')]
    [string[]] $Config = @('Debug', 'Release')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$check = Join-Path $PSScriptRoot 'check.ps1'
$results = [Collections.Generic.List[object]]::new()

function Invoke-Check {
    param([string] $Name, [string[]] $Arguments)
    Write-Host "`n=== $Name ==="
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & pwsh -NoProfile -File $check @Arguments -Jobs $Jobs | Out-Host
    $passed = $LASTEXITCODE -eq 0
    $results.Add([pscustomobject] @{
            Check   = $Name
            Result  = if ($passed) { 'PASS' } else { 'FAIL' }
            Seconds = [int] $timer.Elapsed.TotalSeconds
        })
    return $passed
}

$null = Invoke-Check 'Format, repository policy, script syntax' @('-Stage', 'quick')
$null = Invoke-Check 'MSVC /analyze (Debug)' @('-Stage', 'lint', '-Config', 'Debug')
foreach ($compilerName in $Compiler) {
    foreach ($configName in $Config) {
        $label = "$compilerName $configName"
        $common = @('-Compiler', $compilerName, '-Config', $configName)
        # A failed build makes its tests and package meaningless; go on with the next pair.
        if (!(Invoke-Check "$label build" (@('-Stage', 'build') + $common))) { continue }
        if (!(Invoke-Check "$label CTest" (@('-Stage', 'test') + $common))) { continue }
        $null = Invoke-Check "$label install and --version" (@('-Stage', 'package') + $common)
    }
}

$commit = (& git -C $ProjectRoot rev-parse --short HEAD 2>$null) ?? 'unknown'
$dirty = if (& git -C $ProjectRoot status --porcelain 2>$null) { ' (uncommitted changes)' } else { '' }
$failed = @($results | Where-Object Result -EQ 'FAIL').Count
$lines = @(
    "### scripts/check-all.ps1: $(if ($failed) { "$failed FAILED" } else { 'all passed' })",
    '',
    "Commit ``$commit``$dirty, $([Environment]::OSVersion.VersionString), $((Get-Date).ToString('yyyy-MM-dd HH:mm'))",
    '',
    '| Check | Result | Seconds |',
    '| --- | --- | --- |'
) + @($results | ForEach-Object { "| $($_.Check) | $($_.Result) | $($_.Seconds) |" })
New-Item -ItemType Directory -Force -Path (Join-Path $ProjectRoot 'out') | Out-Null
$summary = Join-Path $ProjectRoot 'out/check-summary.md'
$lines | Set-Content -LiteralPath $summary -Encoding utf8NoBOM
if ($env:GITHUB_STEP_SUMMARY) { $lines | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding utf8NoBOM }
Write-Host "`n$($lines -join "`n")`n`nSummary for the PR: $summary"
exit [int] ($failed -gt 0)
