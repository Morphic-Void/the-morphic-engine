param(
    [string] $RepositoryRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$resolvedRoot = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$records = & git -C $resolvedRoot ls-files --eol
if ($LASTEXITCODE -ne 0) {
    throw "Unable to query tracked-file line endings beneath '$resolvedRoot'."
}

$failures = @()
foreach ($record in $records) {
    if ($record -notmatch '^i/(\S*)\s+w/(\S*)\s+attr/(.*?)\t(.+)$') {
        continue
    }

    $indexEnding = $Matches[1]
    $workingEnding = $Matches[2]
    $attributes = $Matches[3]
    $relativePath = $Matches[4]

    if (($indexEnding.Length -eq 0) -and ($workingEnding.Length -eq 0)) {
        continue
    }
    if ($attributes -match '(^|\s)-text($|\s)') {
        continue
    }

    $expectedEnding = $null
    if ($attributes -match '(^|\s)eol=lf($|\s)') {
        $expectedEnding = "lf"
    }
    elseif ($attributes -match '(^|\s)eol=crlf($|\s)') {
        $expectedEnding = "crlf"
    }
    else {
        continue
    }

    if ($indexEnding -notin @("lf", "none")) {
        $failures += "$relativePath`: index is $indexEnding; expected normalized LF"
    }
    if ($workingEnding -notin @($expectedEnding, "none")) {
        $failures += "$relativePath`: working tree is $workingEnding; expected $expectedEnding"
    }

    $absolutePath = Join-Path $resolvedRoot $relativePath
    if (Test-Path -LiteralPath $absolutePath -PathType Leaf) {
        $bytes = [IO.File]::ReadAllBytes($absolutePath)
        if (($bytes.Length -ne 0) -and ($bytes[$bytes.Length - 1] -ne 10)) {
            $failures += "$relativePath`: missing final newline"
        }
    }
}

if ($failures.Count -ne 0) {
    foreach ($failure in $failures) {
        Write-Error $failure -ErrorAction Continue
    }
    exit 1
}

Write-Host "Line endings: tracked text matches .gitattributes."
exit 0
