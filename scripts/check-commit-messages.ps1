[CmdletBinding(DefaultParameterSetName = "MessageFile")]
param (
    [Parameter(Mandatory = $true, ParameterSetName = "MessageFile")]
    [string]$MessageFile,

    [Parameter(Mandatory = $true, ParameterSetName = "RevisionRange")]
    [string]$RevisionRange
)

$ErrorActionPreference = "Stop"
$subjectPattern = '^(feat|fix|refactor|docs|test|chore): .+$'
$mergeSubjectPattern = '^Merge (branch |remote-tracking branch |pull request #\d+ from )'

function Test-CommitSubject {
    param (
        [string]$Subject,
        [string]$Source
    )

    if ($Subject -cmatch $mergeSubjectPattern) {
        return $true
    }

    if ($Subject -cnotmatch $subjectPattern) {
        [Console]::Error.WriteLine(("Invalid commit subject in {0}: {1}" -f $Source, $Subject))
        [Console]::Error.WriteLine("Expected '<label>: <short description>'; labels: feat, fix, refactor, docs, test, chore.")
        return $false
    }

    return $true
}

if ($PSCmdlet.ParameterSetName -eq "MessageFile") {
    if (-not (Test-Path -LiteralPath $MessageFile -PathType Leaf)) {
        [Console]::Error.WriteLine("Commit message file not found: {0}" -f $MessageFile)
        exit 2
    }

    $subject = Get-Content -LiteralPath $MessageFile -TotalCount 1
    if ($null -eq $subject) {
        $subject = ""
    }

    if (-not (Test-CommitSubject -Subject $subject -Source $MessageFile)) {
        exit 1
    }
}
else {
    $subjects = & git log --no-merges --format=%s $RevisionRange
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    foreach ($subject in $subjects) {
        if (-not (Test-CommitSubject -Subject $subject -Source $RevisionRange)) {
            exit 1
        }
    }
}

exit 0
