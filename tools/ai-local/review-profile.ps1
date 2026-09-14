<#
review-profile.ps1 - decide how hard to look, from what actually changed.

The rule the owner set: risk is a property of the change, not of the phase it
belongs to. A large phase full of documentation is not dangerous. Three lines
inside a Document transaction are.

  QUICK      small UI fixes, documents, test edits, obvious small changes
             -> reasoning effort low
  NORMAL     ordinary feature work
             -> reasoning effort medium
  HIGH_RISK  transactions, ownership and lifetime, undo/redo, save and load,
             the document model, OCCT topology, the stored approximation
             structure, geometry algorithms
             -> reasoning effort high

A declaration may name a profile. The machine may raise it when the diff touches
something dangerous, and says why. It never lowers what a person asked for
without being told to (`force_profile`).
#>

Set-StrictMode -Version 1.0

# Paths whose contents are dangerous by nature. This is written by area, not by
# one particular tree: V1 still has its own model, persistence and geometry, and a
# careful three-line change there is exactly as dangerous as one in V2. Listing
# only src/next/ let a small change to src/core/kachakacha/model/ be called QUICK.
$script:HighRiskPaths = @(
    'src/next/kachakacha/document/',
    'src/next/kachakacha/io/',
    'src/next/kachakacha/geometry/',
    'src/next/kachakacha/fabrication/',
    'src/next/kachakacha/domain/',
    'src/next/kachakacha/modeling/',
    'src/next/kachakacha/exporters/',
    'src/next_occt/',
    'src/core/kachakacha/model/',
    'src/core/kachakacha/io/',
    'src/core/kachakacha/geometry/',
    'src/occt/'
)

# The same areas, wherever they live. A new tree tomorrow gets the same treatment
# without anyone having to remember to add it here.
$script:HighRiskAreas = @(
    '/document/', '/model/', '/io/', '/geometry/', '/fabrication/',
    '/domain/', '/modeling/', '/kernel/', '/exporters/'
)

# Names whose appearance in a diff means the same thing wherever they live.
$script:HighRiskTokens = @(
    'BeginCompound', 'EndCompound', 'AbortCompound', 'Transaction',
    'Undo', 'Redo', 'PushHistory', 'history_',
    'DocumentFile', 'Serialize', 'kcd2',
    'TopoDS_', 'BRep', 'Sewing', 'ShapeFix',
    'shared_ptr', 'unique_ptr', 'weak_ptr',
    'ApproxPart', 'manualBoundaries', 'bendRadius',
    'reinterpret_cast', 'const_cast', 'memcpy'
)

# Paths that carry no risk on their own.
$script:QuietPaths = @('docs/', 'tests_v2/', 'tools/qtstub/', 'samples/', '.github/')

function Test-QuietPath {
    param([string]$Path)
    if ($Path -like '*.md') { return $true }
    foreach ($quiet in $script:QuietPaths) {
        if ($Path -like ($quiet + '*')) { return $true }
    }
    return $false
}

function Get-RiskSignals {
    param(
        [AllowEmptyCollection()][string[]]$ChangedFiles,
        [string]$DiffText
    )
    $signals = @()
    foreach ($file in $ChangedFiles) {
        $normalised = ($file -replace '\\', '/')
        $matched = $false
        foreach ($risky in $script:HighRiskPaths) {
            if ($normalised -like ($risky + '*')) {
                $signals += ('path:' + $risky)
                $matched = $true
                break
            }
        }
        if ($matched) { continue }
        # Anything under src/ that lives in one of the dangerous areas counts,
        # whichever tree it belongs to.
        if ($normalised -like 'src/*') {
            foreach ($area in $script:HighRiskAreas) {
                if ($normalised -like ('*' + $area + '*')) {
                    $signals += ('area:' + $area.Trim('/'))
                    break
                }
            }
        }
    }
    if ($DiffText) {
        # Only added and removed lines count, and only inside code. A risky name
        # that merely sits in the surrounding context was not touched; a risky name
        # written in a document is prose about the danger, not the danger. A page
        # explaining what a transaction is does not become a transaction.
        # These names are product concepts, so they only mean anything inside the
        # product's own code. In a document they are prose about the danger, and in
        # this pipeline's own scripts the list of dangerous names is itself one of
        # the lines - which is how a documentation-only change was read as
        # HIGH_RISK twice today.
        $inProductCode = $false
        $inHunk = $false
        foreach ($line in ($DiffText -split "`r?`n")) {
            if ($line -like 'diff --git *') {
                $inProductCode = ($line -like '*src/*' -or $line -like '*tests_v2/*')
                $inHunk = $false
                continue
            }
            # Only inside a hunk is a leading + or - a changed line. Outside one,
            # "+++history_" is a file header; inside one it is an added line that
            # happens to start with a plus, and skipping it hid a real change.
            if ($line -like '@@*') { $inHunk = $true; continue }
            if (-not $inHunk) { continue }
            if (-not $inProductCode) { continue }
            if ($line.Length -lt 2) { continue }
            $first = $line.Substring(0, 1)
            if ($first -ne '+' -and $first -ne '-') { continue }
            foreach ($token in $script:HighRiskTokens) {
                if ($line -like ('*' + $token + '*')) { $signals += ('name:' + $token) }
            }
        }
    }
    $unique = @()
    $seen = @{}
    foreach ($signal in $signals) {
        if (-not $seen.ContainsKey($signal)) { $seen[$signal] = $true; $unique += $signal }
    }
    return $unique
}

function Get-ChangedLineCount {
    param([string]$DiffText)
    if (-not $DiffText) { return 0 }
    $count = 0
    $inHunk = $false
    foreach ($line in ($DiffText -split "`r?`n")) {
        if ($line -like 'diff --git *') { $inHunk = $false; continue }
        if ($line -like '@@*') { $inHunk = $true; continue }
        if (-not $inHunk) { continue }
        if ($line.Length -lt 1) { continue }
        $first = $line.Substring(0, 1)
        if ($first -ne '+' -and $first -ne '-') { continue }
        $count++
    }
    return $count
}

function ConvertTo-KnownProfile {
    param([string]$Name)
    switch (([string]$Name).ToUpperInvariant()) {
        'QUICK'      { return 'QUICK' }
        'LOW'        { return 'QUICK' }
        'NORMAL'     { return 'NORMAL' }
        'MEDIUM'     { return 'NORMAL' }
        'HIGH_RISK'  { return 'HIGH_RISK' }
        'HIGH'       { return 'HIGH_RISK' }
        'EXTRA_HIGH' { return 'HIGH_RISK' }
        default      { return '' }
    }
}

function Get-ProfileRank {
    param([string]$Profile)
    switch ($Profile) {
        'QUICK'     { return 1 }
        'NORMAL'    { return 2 }
        'HIGH_RISK' { return 3 }
        default     { return 0 }
    }
}

# The reviewer accepts a fixed set of lowercase names. An old manifest can still
# carry "HIGH", and passing that straight through makes the reviewer refuse the
# whole run with a 400. Anything unrecognised becomes medium rather than being
# handed on untouched.
function ConvertTo-KnownEffort {
    param([string]$Effort)
    switch (([string]$Effort).ToLowerInvariant()) {
        'none'    { return 'none' }
        'minimal' { return 'minimal' }
        'low'     { return 'low' }
        'medium'  { return 'medium' }
        'high'    { return 'high' }
        'xhigh'   { return 'xhigh' }
        'max'     { return 'max' }
        'quick'     { return 'low' }
        'normal'    { return 'medium' }
        'high_risk' { return 'high' }
        'extra_high' { return 'xhigh' }
        default   { return 'medium' }
    }
}

function Get-EffortForProfile {
    param([string]$Profile)
    switch ($Profile) {
        'QUICK'     { return 'low' }
        'HIGH_RISK' { return 'high' }
        default     { return 'medium' }
    }
}

# How long to wait before calling it a timeout. A deep read of a dangerous change
# deserves patience; a documentation fix does not.
function Get-TimeoutForProfile {
    param([string]$Profile)
    switch ($Profile) {
        'QUICK'     { return 600 }
        'HIGH_RISK' { return 2400 }
        default     { return 1200 }
    }
}

function Resolve-ReviewProfile {
    param(
        [string]$Declared,
        [AllowEmptyCollection()][string[]]$ChangedFiles,
        [string]$DiffText,
        [bool]$ForceDeclared = $false
    )
    $signals = @(Get-RiskSignals -ChangedFiles $ChangedFiles -DiffText $DiffText)
    $changedLines = Get-ChangedLineCount -DiffText $DiffText
    $allQuiet = $true
    foreach ($file in $ChangedFiles) {
        if (-not (Test-QuietPath -Path ($file -replace '\\', '/'))) { $allQuiet = $false; break }
    }

    $measured = 'NORMAL'
    $reason = 'ordinary feature work'
    if ($signals.Count -gt 0) {
        $measured = 'HIGH_RISK'
        $shown = $signals
        if ($shown.Count -gt 6) { $shown = $shown[0..5] }
        $reason = 'the change touches ' + ($shown -join ', ')
    } elseif ($allQuiet -and $ChangedFiles.Count -gt 0) {
        $measured = 'QUICK'
        $reason = 'only documents, tests and other quiet paths changed'
    } elseif ($changedLines -gt 0 -and $changedLines -le 200) {
        $measured = 'QUICK'
        $reason = "a small change ($changedLines changed lines) with nothing dangerous in it"
    }

    $declaredProfile = ConvertTo-KnownProfile -Name $Declared
    $chosen = $measured
    $note = $reason
    if ($declaredProfile) {
        if ($ForceDeclared) {
            $chosen = $declaredProfile
            $note = "the request insists on $declaredProfile; measured $measured ($reason)"
        } elseif ((Get-ProfileRank $declaredProfile) -gt (Get-ProfileRank $measured)) {
            $chosen = $declaredProfile
            $note = "the request asked for $declaredProfile; measured $measured ($reason)"
        } elseif ((Get-ProfileRank $measured) -gt (Get-ProfileRank $declaredProfile)) {
            $chosen = $measured
            $note = "raised from $declaredProfile to $measured because $reason"
        }
    }

    return [pscustomobject]@{
        profile          = $chosen
        declared         = $declaredProfile
        measured         = $measured
        reason           = $note
        effort           = (Get-EffortForProfile -Profile $chosen)
        timeout_seconds  = (Get-TimeoutForProfile -Profile $chosen)
        risk_signals     = $signals
        changed_lines    = $changedLines
    }
}
