#Requires -Version 7.4
param(
    [ValidateSet('all', 'quick', 'format', 'lint', 'build', 'test', 'package', 'vendor-lock')]
    [string] $Stage = 'all',
    [ValidateSet('Debug', 'Release')]
    [string] $Config = 'Debug',
    [ValidateSet('msvc', 'clang')]
    [string] $Compiler = 'msvc',
    # Parallel build jobs; 0 uses every processor (CI). One job keeps a laptop responsive.
    [ValidateRange(0, 256)]
    [int] $Jobs = 1,
    [switch] $Fix
)
. "$PSScriptRoot/common.ps1"

function Get-VendorHashes {
    $result = [ordered]@{}
    foreach ($directory in @('include', 'lib')) {
        $digest = [System.Security.Cryptography.IncrementalHash]::CreateHash('SHA256')
        try {
            [string[]] $paths = Get-ChildItem -LiteralPath "$ProjectRoot/$directory" -File -Recurse -Force |
                ForEach-Object { [IO.Path]::GetRelativePath($ProjectRoot, $_.FullName).Replace('\', '/') }
            [Array]::Sort($paths, [StringComparer]::Ordinal)
            foreach ($path in $paths) {
                $content = [IO.File]::ReadAllBytes((Join-Path $ProjectRoot $path))
                if ([IO.Path]::GetExtension($path) -ine '.lib') {
                    $text = [Text.Encoding]::Latin1.GetString($content).Replace("`r`n", "`n")
                    $content = [Text.Encoding]::Latin1.GetBytes($text)
                }
                $digest.AppendData([Text.Encoding]::UTF8.GetBytes($path + [char] 0))
                $digest.AppendData([Security.Cryptography.SHA256]::HashData($content))
            }
            $result[$directory] = [Convert]::ToHexString($digest.GetHashAndReset()).ToLowerInvariant()
        } finally { $digest.Dispose() }
    }
    return $result
}

function Test-Policy {
    $expected = Get-Content -LiteralPath vendor-lock.json -Raw | ConvertFrom-Json -AsHashtable
    $actual = Get-VendorHashes
    foreach ($directory in @('include', 'lib')) {
        if ($actual[$directory] -ne $expected[$directory]) {
            throw 'Vendored dependencies changed; review and regenerate vendor-lock.json.'
        }
    }
    $paths = (& git ls-files --cached --others --exclude-standard -z | Out-String).Split([char] 0)
    if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed' }
    foreach ($path in $paths) {
        $path = $path.TrimEnd("`r", "`n")
        if (!$path -or $path -match '^(include|lib)/') { continue }
        if ($path -match '^(build[^/]*|out|data|backups|logs)/' -or
            $path -match '(^|/)\.env($|\.(?!example$))' -or
            $path -match '\.(exe|obj|pdb|key|pem|pfx)$') {
            throw "Generated or private repository file: $path"
        }
    }
    foreach ($script in Get-ChildItem -LiteralPath scripts -Filter *.ps1 -File) {
        $parseErrors = $null
        $null = [Management.Automation.Language.Parser]::ParseFile($script.FullName, [ref] $null, [ref] $parseErrors)
        if ($parseErrors.Count) { throw "Invalid PowerShell syntax in $($script.Name): $parseErrors" }
    }
    Test-Consistency
    Write-Host 'PASS: dependency fingerprints, repository policy and PowerShell syntax'
}

# Mistakes that once reached review or CI, kept out by a check instead of a rule
# in AGENTS.md: the Linux build globs src/, so only the Windows list can miss a file.
function Test-Consistency {
    $problems = [Collections.Generic.List[string]]::new()
    $windowsList = Get-Content -LiteralPath CMakeLists.txt -Raw
    $linuxList = Get-Content -LiteralPath tests/linux/CMakeLists.txt -Raw
    $testDocs = Get-Content -LiteralPath docs/development.md -Raw
    $featureMap = Get-Content -LiteralPath .agents/skills/verify-app/references/features/README.md -Raw

    foreach ($file in Get-ChildItem -Path src/*.cpp, src/Utilities/*.cpp -File) {
        $path = [IO.Path]::GetRelativePath($ProjectRoot, $file.FullName).Replace('\', '/')
        if (!$windowsList.Contains($path)) { $problems.Add("$path is not in the Windows target in CMakeLists.txt") }
    }
    foreach ($file in Get-ChildItem -Path tests/*.cpp -File) {
        $name = $file.BaseName
        if (!$windowsList.Contains("tests/$name.cpp")) { $problems.Add("tests/$name.cpp is not in CMakeLists.txt") }
        if (!$linuxList.Contains($name)) { $problems.Add("$name is not in tests/linux/CMakeLists.txt") }
        if (!$testDocs.Contains("``$name``")) { $problems.Add("$name is not described under 測試範圍 in docs/development.md") }
    }
    $modules = Get-ChildItem -Path src/* -File -Include *.cpp, *.h, *.H | ForEach-Object BaseName | Sort-Object -Unique
    foreach ($module in $modules) {
        if (!$featureMap.Contains($module)) {
            $problems.Add("src/$module.* has no row in .agents/skills/verify-app/references/features/README.md")
        }
    }
    # One paragraph per line is fine; a whole changelog on one line is not.
    $docs = @('README.md', 'AGENTS.md') + @(Get-ChildItem -Path docs, .agents -Recurse -File -Filter *.md |
            ForEach-Object { [IO.Path]::GetRelativePath($ProjectRoot, $_.FullName) })
    foreach ($doc in $docs) {
        $number = 0
        foreach ($line in Get-Content -LiteralPath $doc) {
            $number++
            if ($line.Length -gt 800) { $problems.Add("${doc}:$number is $($line.Length) characters; split it (limit 800)") }
        }
    }
    if ($problems.Count) { throw "Repository consistency:`n  " + ($problems -join "`n  ") }
}

function Invoke-Formatting {
    $formatter = Resolve-NativeTool 'clang-format'
    $sources = @(Get-ChildItem -Path src, tests -Recurse -File |
        Where-Object { $_.Extension -in @('.cpp', '.h') } |
        ForEach-Object { [IO.Path]::GetRelativePath($ProjectRoot, $_.FullName) } | Sort-Object)
    [string[]] $flags = if ($Fix) { @('-i') } else { @('--dry-run', '--Werror') }
    Invoke-Native $formatter ($flags + $sources)
}

function Invoke-Build {
    param([string] $Preset)
    Invoke-Native $CMake @('--preset', $Preset)
    $parallel = if ($Jobs -gt 0) { $Jobs } else { [Environment]::ProcessorCount }
    $buildArguments = @('--build', "out/build/$Preset", '--config', $Config, '--parallel', "$parallel")
    if ($Preset -ne 'clang') { $buildArguments += @('--', '/nodeReuse:false') }
    Invoke-Native $CMake $buildArguments
}

Push-Location $ProjectRoot
try {
    if ($Fix -and $Stage -ne 'format') { throw '-Fix requires -Stage format' }
    if ($Stage -eq 'vendor-lock') {
        Get-VendorHashes | ConvertTo-Json | Set-Content -LiteralPath vendor-lock.json -Encoding utf8NoBOM
        exit 0
    }
    if ($Stage -in @('all', 'quick', 'format')) { Invoke-Formatting }
    if ($Stage -in @('all', 'quick', 'lint')) { Test-Policy }
    if ($Stage -in @('all', 'lint', 'build', 'test', 'package')) { $CMake = Resolve-NativeTool 'cmake' }
    $buildPreset = if ($Compiler -eq 'clang') { 'clang' } else { 'windows' }
    $packageDirectory = if ($Compiler -eq 'clang') { "out/package/clang/$Config" } else { "out/package/$Config" }
    if ($Compiler -eq 'clang' -and $Stage -in @('all', 'build')) { Initialize-ClangEnvironment }
    # Static analysis remains a shared MSVC gate for both compiler builds.
    if ($Stage -in @('all', 'lint')) { Invoke-Build 'analyze' }
    if ($Stage -in @('all', 'build')) { Invoke-Build $buildPreset }
    if ($Stage -in @('all', 'test')) {
        $ctest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
        Invoke-Native $ctest @('--test-dir', "out/build/$buildPreset", '-C', $Config,
            '--output-on-failure', '--no-tests=error', '--output-junit', "results-$Config.xml")
    }
    if ($Stage -in @('all', 'package')) {
        Invoke-Native $CMake @('--install', "out/build/$buildPreset", '--config', $Config,
            '--prefix', $packageDirectory)
        Invoke-Native "$ProjectRoot/$packageDirectory/RollerCoasters.exe" @('--version')
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally { Pop-Location }
