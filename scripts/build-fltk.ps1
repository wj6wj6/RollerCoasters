#Requires -Version 7.4
# Rebuilds the bundled FLTK (include/FL and lib/{Debug,Release}/fltk*.lib) from the
# official source release with the same settings as the committed copy: static
# libraries, /MD and /MDd runtimes, bundled zlib/libpng/libjpeg, /utf-8, and
# debug information inside the Debug .lib files (/Z7), so Visual Studio shows
# FLTK frames in call stacks. See docs/adr/0007-fltk-1-3-11.md.
#
#   pwsh -File scripts/build-fltk.ps1
#   pwsh -File scripts/check.ps1 -Stage vendor-lock
param(
    # Visual Studio generator; the linker that builds RollerCoasters must be at
    # least as new as the compiler used here.
    [string] $Generator = 'Visual Studio 17 2022'
)
. "$PSScriptRoot/common.ps1"
$ProgressPreference = 'SilentlyContinue'

$Version = '1.3.11'
$Url = "https://github.com/fltk/fltk/releases/download/release-$Version/fltk-$Version-source.tar.gz"
$Sha256 = '92805ABC84505E3E7E27AEC775AB0754ECB4182FE2D8FF2A9D568CCDCB2811AC'
$Work = Join-Path $ProjectRoot "out/fltk-$Version"
$Tar = Join-Path $env:SystemRoot 'System32/tar.exe'

New-Item -ItemType Directory -Force -Path $Work | Out-Null
$archive = Join-Path $Work "fltk-$Version-source.tar.gz"
if (!(Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri $Url -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Sha256) {
    throw "SHA256 mismatch for $archive. Remove it and retry."
}
$source = Join-Path $Work "fltk-$Version"
if (!(Test-Path -LiteralPath $source)) {
    Invoke-Native $Tar @('-xzf', $archive, '-C', $Work)
}

$build = Join-Path $Work 'build'
$cmake = (Get-Command cmake -ErrorAction Stop).Source
Invoke-Native $cmake @(
    '-S', $source, '-B', $build, '-G', $Generator, '-A', 'x64',
    '-DOPTION_BUILD_SHARED_LIBS=OFF', '-DFLTK_MSVC_RUNTIME_DLL=ON',
    '-DOPTION_USE_SYSTEM_ZLIB=OFF', '-DOPTION_USE_SYSTEM_LIBJPEG=OFF', '-DOPTION_USE_SYSTEM_LIBPNG=OFF',
    '-DOPTION_USE_GL=ON', '-DOPTION_CAIRO=OFF',
    '-DFLTK_BUILD_TEST=OFF', '-DFLTK_BUILD_EXAMPLES=OFF',
    '-DOPTION_BUILD_HTML_DOCUMENTATION=OFF', '-DOPTION_BUILD_PDF_DOCUMENTATION=OFF',
    # FLTK's sources are UTF-8; without /utf-8 a Chinese (950) code page misreads them.
    '-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /utf-8',
    '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /utf-8',
    '-DCMAKE_C_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1',
    '-DCMAKE_CXX_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1')
foreach ($config in 'Debug', 'Release') {
    Invoke-Native $cmake @('--build', $build, '--config', $config, '--parallel',
        '--target', 'fltk', 'fltk_forms', 'fltk_images', 'fltk_gl')
    Copy-Item -Path (Join-Path $build "lib/$config/*.lib") -Destination (Join-Path $ProjectRoot "lib/$config") -Force
}

$headers = Join-Path $ProjectRoot 'include/FL'
Remove-Item -LiteralPath $headers -Recurse -Force
Copy-Item -LiteralPath (Join-Path $source 'FL') -Destination $headers -Recurse
# The one generated header; keep LF like the rest of include/FL.
$abi = (Get-Content -LiteralPath (Join-Path $build 'FL/abi-version.h') -Raw).Replace("`r`n", "`n")
[IO.File]::WriteAllText((Join-Path $headers 'abi-version.h'), $abi)
Write-Host "FLTK $Version copied into include/FL and lib/. Run: pwsh -File scripts/check.ps1 -Stage vendor-lock"
