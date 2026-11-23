#Requires -Version 7.4
<#
Drives the built program for verification, so checks are the same every time
instead of hand-made scripts. Evidence (PNG screenshots and logs) goes to
out/verify/, which is not committed.

  pwsh -File scripts/control-app.ps1 doctor
  pwsh -File scripts/control-app.ps1 screenshot -Name night -AppArgs '--time','22'
  pwsh -File scripts/control-app.ps1 smoke                 # every scenario below
  pwsh -File scripts/control-app.ps1 smoke -Only cameras   # the scenarios named cameras-*
  pwsh -File scripts/control-app.ps1 start -AppArgs '--camera','fly'
  pwsh -File scripts/control-app.ps1 stop
  pwsh -File scripts/control-app.ps1 clean

The program's own exit codes (src/AppControl.h): 0 passed, 1 a check failed,
2 wrong command line. A scenario states which code it expects, so the error
paths are checked as well as the happy ones.
#>
param(
    [Parameter(Position = 0, Mandatory)]
    [ValidateSet('doctor', 'screenshot', 'smoke', 'start', 'stop', 'clean', 'list')]
    [string] $Command,
    [string] $Name = 'shot',
    [string[]] $AppArgs = @(),
    [string] $Only = '',
    [ValidateSet('Debug', 'Release')]
    [string] $Config = 'Release',
    # The program to drive; found under out/ when not given.
    [string] $Exe = ''
)
. "$PSScriptRoot/common.ps1"

$VerifyDir = Join-Path $ProjectRoot 'out/verify'
$PidFile = Join-Path $VerifyDir 'app.pid'

# name, arguments, the exit code expected. Screenshots are named after the scenario.
$Scenarios = @(
    @{ Name = 'doctor'; Args = @('--doctor'); Expect = 0 }
    @{ Name = 'tour-train'; Args = @('--camera', 'train'); Expect = 0; Shot = $true }
    @{ Name = 'cameras-world'; Args = @('--camera', 'world'); Expect = 0; Shot = $true }
    @{ Name = 'cameras-chase'; Args = @('--camera', 'chase'); Expect = 0; Shot = $true }
    @{ Name = 'cameras-chase-crystal-tunnel'; Args = @('--camera', 'chase', '--u', '18'); Expect = 0; Shot = $true }
    @{ Name = 'cameras-chase-mine-tunnel'; Args = @('--camera', 'chase', '--u', '38'); Expect = 0; Shot = $true }
    @{ Name = 'cameras-chase-bridge'; Args = @('--camera', 'chase', '--u', '48'); Expect = 0; Shot = $true }
    @{ Name = 'cameras-top'; Args = @('--camera', 'top'); Expect = 0; Shot = $true }
    @{ Name = 'cameras-fly'; Args = @('--camera', 'fly'); Expect = 0; Shot = $true }
    @{ Name = 'light-off'; Args = @('--light', 'off', '--camera', 'world'); Expect = 0; Shot = $true }
    @{ Name = 'light-night'; Args = @('--time', '22', '--camera', 'world'); Expect = 0; Shot = $true }
    @{ Name = 'light-rain'; Args = @('--rain', 'on', '--frames', '20'); Expect = 0; Shot = $true }
    @{ Name = 'light-clouds-dusk'; Args = @('--time', '18.5', '--camera', 'world'); Expect = 0; Shot = $true }
    @{ Name = 'light-clouds-night'; Args = @('--time', '21', '--camera', 'world'); Expect = 0; Shot = $true }
    @{ Name = 'light-clouds-rain-night'; Args = @('--time', '21', '--rain', 'on', '--frames', '20', '--camera', 'world'); Expect = 0; Shot = $true }
    @{ Name = 'world-flat'; Args = @('--scenery', 'off', '--camera', 'world'); Expect = 0; Shot = $true }
    @{ Name = 'world-underwater'; Args = @('--u', '10', '--camera', 'train'); Expect = 0; Shot = $true }
    @{ Name = 'track-sample'; Args = @('--track', '{sample-track}', '--scenery', 'off', '--camera', 'world'); Expect = 0; Shot = $true }
    @{ Name = 'track-bad-count'; Args = @('--track', '{bad-track}'); Expect = 1; Shot = $true }
    @{ Name = 'track-missing'; Args = @('--track', '{missing-track}'); Expect = 1; Shot = $true }
    @{ Name = 'pack-missing'; Args = @('--pack', '{missing-track}'); Expect = 1; Shot = $true }
    @{ Name = 'usage-unknown'; Args = @('--frobnicate'); Expect = 2 }
    @{ Name = 'usage-bad-camera'; Args = @('--camera', 'side'); Expect = 2 }
)

function Find-Program {
    if ($Exe) {
        if (!(Test-Path -LiteralPath $Exe -PathType Leaf)) { throw "No program at $Exe" }
        return (Resolve-Path -LiteralPath $Exe).Path
    }
    $name = if ($IsWindows) { 'RollerCoasters.exe' } else { 'RollerCoasters' }
    $candidates = @(
        "out/build/windows/$Config/$name", "out/build/clang/$Config/$name",
        "out/build/vs/$Config/$name", "out/package/$Config/$name", "out/package/clang/$Config/$name") |
        ForEach-Object { Join-Path $ProjectRoot $_ } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Sort-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc } -Descending
    if (!$candidates) {
        throw "No $Config build of RollerCoasters under out/. Build it with: pwsh -File scripts/check.ps1 -Stage build -Config $Config"
    }
    return @($candidates)[0]
}

# Track files for the scenarios, written fresh into out/verify/fixtures.
function Initialize-Fixtures {
    $fixtures = Join-Path $VerifyDir 'fixtures'
    New-Item -ItemType Directory -Force -Path $fixtures | Out-Null
    $sample = Join-Path $fixtures 'sample.txt'
    Set-Content -LiteralPath $sample -Value "6`n-40 5 -40`n40 5 -40`n60 15 0`n40 5 40`n-40 5 40`n-60 10 0" -NoNewline
    $bad = Join-Path $fixtures 'bad-count.txt'
    Set-Content -LiteralPath $bad -Value "3`n0 0 0`n1 0 0`n2 0 0" -NoNewline
    return @{
        'sample-track'  = $sample
        'bad-track'     = $bad
        'missing-track' = (Join-Path $fixtures 'does-not-exist.txt')
    }
}

function Expand-Arguments([string[]] $arguments, [hashtable] $fixtures) {
    return @($arguments | ForEach-Object {
            if ($_ -match '^\{(.+)\}$') { $fixtures[$Matches[1]] } else { $_ }
        })
}

# Runs the program once; returns the exit code and keeps its output in a log.
function Invoke-Program([string] $program, [string[]] $arguments, [string] $logName) {
    New-Item -ItemType Directory -Force -Path $VerifyDir | Out-Null
    $log = Join-Path $VerifyDir "$logName.log"
    Write-Host "+ $program $($arguments -join ' ')"
    & $program @arguments *> $log
    $code = $LASTEXITCODE
    Get-Content -LiteralPath $log | ForEach-Object { Write-Host "  $_" }
    return $code
}

function Invoke-Scenario([string] $program, [hashtable] $scenario, [hashtable] $fixtures) {
    $arguments = Expand-Arguments $scenario.Args $fixtures
    $png = $null
    if ($scenario.ContainsKey('Shot') -and $scenario.Shot) {
        $png = Join-Path $VerifyDir "$($scenario.Name).png"
        Remove-Item -LiteralPath $png -ErrorAction SilentlyContinue
        $arguments = @('--screenshot', $png) + $arguments
    }
    $code = Invoke-Program $program $arguments $scenario.Name
    $ok = $code -eq $scenario.Expect
    # a passing screenshot must have left its picture; a failing one must not
    # have left a picture that could be mistaken for evidence
    if ($ok -and $png) {
        $exists = Test-Path -LiteralPath $png
        $ok = if ($scenario.Expect -eq 0) { $exists } else { !$exists }
    }
    return [pscustomobject]@{
        Scenario = $scenario.Name
        Expected = $scenario.Expect
        Exit     = $code
        Result   = if ($ok) { 'PASS' } else { 'FAIL' }
        Evidence = if ($png -and (Test-Path -LiteralPath $png)) { [IO.Path]::GetRelativePath($ProjectRoot, $png) } else { '' }
    }
}

function Stop-Started {
    if (Test-Path -LiteralPath $PidFile) {
        $id = [int] (Get-Content -LiteralPath $PidFile -Raw)
        Get-Process -Id $id -ErrorAction SilentlyContinue | Stop-Process -Force
        Remove-Item -LiteralPath $PidFile
        Write-Host "Stopped RollerCoasters (process $id)"
    }
}

switch ($Command) {
    'list' {
        $Scenarios | ForEach-Object { '{0,-20} exit {1}  {2}' -f $_.Name, $_.Expect, ($_.Args -join ' ') }
    }
    'doctor' {
        exit (Invoke-Program (Find-Program) (@('--doctor') + $AppArgs) 'doctor')
    }
    'screenshot' {
        $png = Join-Path $VerifyDir "$Name.png"
        $code = Invoke-Program (Find-Program) (@('--screenshot', $png) + $AppArgs) $Name
        if ($code -eq 0) { Write-Host "Evidence: $([IO.Path]::GetRelativePath($ProjectRoot, $png))" }
        exit $code
    }
    'smoke' {
        $program = Find-Program
        $fixtures = Initialize-Fixtures
        $selected = @($Scenarios | Where-Object { !$Only -or $_.Name -eq $Only -or $_.Name -like "$Only-*" })
        if (!$selected) { throw "No scenario matches '$Only'; see: pwsh -File scripts/control-app.ps1 list" }
        $results = @($selected | ForEach-Object { Invoke-Scenario $program $_ $fixtures })
        $table = $results | Format-Table -AutoSize | Out-String -Width 200
        Write-Host $table
        $summary = Join-Path $VerifyDir 'smoke.txt'
        @("program: $program", "date: $((Get-Date).ToString('u'))", $table) | Set-Content -LiteralPath $summary
        Write-Host "Summary: $([IO.Path]::GetRelativePath($ProjectRoot, $summary))"
        if ($results | Where-Object Result -eq 'FAIL') { exit 1 }
        exit 0
    }
    'start' {
        Stop-Started
        New-Item -ItemType Directory -Force -Path $VerifyDir | Out-Null
        $process = Start-Process -FilePath (Find-Program) -ArgumentList $AppArgs -PassThru
        Set-Content -LiteralPath $PidFile -Value $process.Id
        Write-Host "Started RollerCoasters (process $($process.Id)); stop it with: pwsh -File scripts/control-app.ps1 stop"
    }
    'stop' { Stop-Started }
    'clean' {
        Stop-Started
        if (Test-Path -LiteralPath $VerifyDir) { Remove-Item -LiteralPath $VerifyDir -Recurse -Force }
        Write-Host 'Removed out/verify'
    }
}
