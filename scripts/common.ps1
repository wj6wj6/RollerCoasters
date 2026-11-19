#Requires -Version 7.4
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ToolVersions = Get-Content -LiteralPath "$ProjectRoot/tools.json" -Raw | ConvertFrom-Json -AsHashtable

function Invoke-Native {
    param([string] $Command, [string[]] $Arguments = @())
    Write-Host "+ $Command $($Arguments -join ' ')"
    & $Command @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

function Resolve-NativeTool {
    param([string] $Name, [switch] $Optional)
    $spec = $ToolVersions[$Name]
    $localPath = Join-Path $ProjectRoot $spec.executable
    $candidates = @($localPath)
    # tools.json pins Windows downloads; the Linux CI job installs the same versions on PATH.
    $commandName = if ($IsWindows) { "$Name.exe" } else { $Name }
    $installed = @(Get-Command $commandName -All -CommandType Application -ErrorAction SilentlyContinue)
    if ($installed) { $candidates += $installed.Source }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $version = & $candidate --version 2>&1 | Out-String
            if ($LASTEXITCODE -eq 0 -and $version -match "(?<![\d.])$([regex]::Escape($spec.version))(?![\d.])") {
                return $candidate
            }
        }
    }
    if (!$Optional) {
        $setupOption = if ($Name -in @('clang-cl', 'ninja')) { ' -WithClang' } else { '' }
        throw "Missing $Name $($spec.version). Run pwsh -File scripts/setup-tools.ps1$setupOption"
    }
}

function Initialize-ClangEnvironment {
    $clang = Resolve-NativeTool 'clang-cl'
    $ninja = Resolve-NativeTool 'ninja'
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$installation) { throw 'Visual Studio C++ x64 tools and Windows SDK are required for clang-cl.' }
    $devCommand = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
    # Capture the development environment without printing environment variables.
    $environment = & $env:ComSpec /d /s /c "`"$devCommand`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the Visual Studio x64 environment.' }
    foreach ($line in $environment) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
        }
    }
    $env:PATH = (Split-Path -Parent $clang) + ';' + (Split-Path -Parent $ninja) + ';' + $env:PATH
}
