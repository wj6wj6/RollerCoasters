#Requires -Version 7.4
param([switch] $WithClang)
. "$PSScriptRoot/common.ps1"

try {
    $names = @('cmake', 'clang-format')
    if ($WithClang) { $names += @('ninja', 'clang-cl') }
    foreach ($name in $names) {
        if (Resolve-NativeTool $name -Optional) {
            Write-Host "Using installed $name $($ToolVersions[$name].version)"
            continue
        }
        $spec = $ToolVersions[$name]
        $downloadDirectory = Join-Path $ProjectRoot 'out/downloads'
        New-Item -ItemType Directory -Force -Path $downloadDirectory | Out-Null
        $archive = Join-Path $downloadDirectory ([uri]::UnescapeDataString(([uri] $spec.url).Segments[-1]))
        if (!(Test-Path -LiteralPath $archive)) {
            Invoke-WebRequest -Uri $spec.url -OutFile $archive
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $spec.sha256) {
            throw "SHA256 mismatch for $archive. Remove this download and retry."
        }
        if ($name -in @('cmake', 'ninja')) {
            Expand-Archive -LiteralPath $archive -DestinationPath "$ProjectRoot/out/tools/$name" -Force
        } else {
            $destination = "$ProjectRoot/out/tools/llvm"
            New-Item -ItemType Directory -Force -Path $destination | Out-Null
            $entries = @(& tar -tf $archive)
            if ($LASTEXITCODE -ne 0) { throw 'Cannot read LLVM archive' }
            $selection = @($entries | Where-Object { $_ -match '/bin/clang-format\.exe$' })
            if ($selection.Count -ne 1) { throw 'Expected exactly one clang-format.exe in LLVM archive' }
            $strip = $selection[0].Split('/').Count - 2
            if ($name -eq 'clang-cl') {
                # Preserve compiler aliases, native DLLs and Clang's builtin headers.
                $selection += @($entries | Where-Object {
                    $_ -match '/bin/(clang(?:-[0-9]+)?|clang-cl)\.exe$' -or
                    $_ -match '/bin/[^/]+\.dll$' -or
                    $_ -match '/lib/clang/[^/]+/include(/|$)'
                })
            }
            $selectionFile = Join-Path $downloadDirectory "llvm-$name-files.txt"
            $selection | Set-Content -LiteralPath $selectionFile -Encoding utf8NoBOM
            Invoke-Native tar @('-xf', $archive, '-C', $destination, "--strip-components=$strip", '-T', $selectionFile)
        }
        $null = Resolve-NativeTool $name
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
