#Requires -Version 7.4
param([Parameter(Mandatory)][string] $MessageFile)
$ErrorActionPreference = 'Stop'
try {
    $lines = @(Get-Content -LiteralPath $MessageFile -Encoding utf8)
    $subject = if ($lines.Count) { $lines[0] } else { '' }
    $pattern = '^(feat|fix|docs|style|refactor|perf|test|build|ci|chore)(\([\w./-]+\))?!?: .+$'
    if ($subject -cnotmatch '^(Merge |Revert )' -and $subject -cnotmatch $pattern) {
        throw 'Use a Conventional Commit subject, e.g. ci: add Windows quality checks'
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
