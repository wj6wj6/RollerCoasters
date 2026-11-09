#Requires -Version 7.4
param([switch] $WithClang)
. "$PSScriptRoot/common.ps1"
# The progress bar makes large downloads much slower in non-interactive runs.
$ProgressPreference = 'SilentlyContinue'
# Use the Windows bsdtar explicitly: a GNU tar earlier on PATH treats "D:\..." as a remote host.
$Tar = Join-Path $env:SystemRoot 'System32/tar.exe'
$DownloadDirectory = Join-Path $ProjectRoot 'out/downloads'

function Write-Step {
    param([string] $Message)
    Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] $Message"
}

function Get-VerifiedArchive {
    param([hashtable] $Spec)
    New-Item -ItemType Directory -Force -Path $DownloadDirectory | Out-Null
    $archive = Join-Path $DownloadDirectory ([uri]::UnescapeDataString(([uri] $Spec.url).Segments[-1]))
    if (!(Test-Path -LiteralPath $archive)) {
        Write-Step "Downloading $($Spec.url)"
        Invoke-WebRequest -Uri $Spec.url -OutFile $archive
    }
    Write-Step "Verifying SHA256 of $(Split-Path -Leaf $archive)"
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Spec.sha256) {
        throw "SHA256 mismatch for $archive. Remove this download and retry."
    }
    return $archive
}

# Returns a tar archive that bsdtar can read. Older Windows bsdtar builds lack xz support,
# so decompress once with 7-Zip there instead of letting every tar pass fail or stall.
function Get-ReadableTar {
    param([string] $Archive)
    $tarVersion = & $Tar --version | Out-String
    Write-Step "Using $($tarVersion.Trim())"
    if ($tarVersion -match 'liblzma') { return $Archive }
    $sevenZip = @(Get-Command 7z.exe -ErrorAction SilentlyContinue) +
        @(Get-Item -LiteralPath "$env:ProgramFiles/7-Zip/7z.exe" -ErrorAction SilentlyContinue) |
        Select-Object -First 1
    if (!$sevenZip) { throw "This Windows tar cannot read .xz ($($tarVersion.Trim())). Install 7-Zip and retry." }
    $sevenZipPath = if ($sevenZip -is [Management.Automation.CommandInfo]) { $sevenZip.Source } else { $sevenZip.FullName }
    $plainTar = $Archive -replace '\.xz$', ''
    if (!(Test-Path -LiteralPath $plainTar)) {
        Write-Step 'Decompressing LLVM archive with 7-Zip'
        Invoke-Native $sevenZipPath @('x', '-y', "-o$DownloadDirectory", $Archive)
    }
    return $plainTar
}

function Install-ZipTool {
    param([string] $Name)
    $archive = Get-VerifiedArchive $ToolVersions[$Name]
    $destination = "$ProjectRoot/out/tools/$Name"
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Write-Step "Extracting $Name"
    Invoke-Native $Tar @('-xf', $archive, '-C', $destination)
}

# clang-format and clang-cl come from the same LLVM archive: read and extract it in one pass.
function Install-LlvmTools {
    param([string[]] $Names)
    $archive = Get-ReadableTar (Get-VerifiedArchive $ToolVersions[$Names[0]])
    Write-Step 'Listing LLVM archive'
    $entries = @(& $Tar -tf $archive)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read LLVM archive' }
    $selection = @($entries | Where-Object { $_ -match '/bin/clang-format\.exe$' })
    if ($selection.Count -ne 1) { throw 'Expected exactly one clang-format.exe in LLVM archive' }
    $strip = $selection[0].Split('/').Count - 2
    if ('clang-cl' -in $Names) {
        # Preserve compiler aliases, native DLLs and Clang's builtin headers.
        $selection += @($entries | Where-Object {
            $_ -match '/bin/(clang(?:-[0-9]+)?|clang-cl)\.exe$' -or
            $_ -match '/bin/[^/]+\.dll$' -or
            $_ -match '/lib/clang/[^/]+/include(/|$)'
        })
    }
    $destination = "$ProjectRoot/out/tools/llvm"
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $selectionFile = Join-Path $DownloadDirectory 'llvm-files.txt'
    $selection | Set-Content -LiteralPath $selectionFile -Encoding utf8NoBOM
    Write-Step "Extracting $($Names -join ', ')"
    Invoke-Native $Tar @('-xf', $archive, '-C', $destination, "--strip-components=$strip", '-T', $selectionFile)
}

try {
    $names = @('cmake', 'clang-format')
    if ($WithClang) { $names += @('ninja', 'clang-cl') }
    $missing = @($names | Where-Object {
        if (Resolve-NativeTool $_ -Optional) {
            Write-Step "Using installed $_ $($ToolVersions[$_].version)"
            return $false
        }
        return $true
    })
    foreach ($name in $missing | Where-Object { $_ -in @('cmake', 'ninja') }) { Install-ZipTool $name }
    $llvm = @($missing | Where-Object { $_ -in @('clang-format', 'clang-cl') })
    if ($llvm) { Install-LlvmTools $llvm }
    foreach ($name in $missing) { $null = Resolve-NativeTool $name }
    Write-Step 'Native tools ready'
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
