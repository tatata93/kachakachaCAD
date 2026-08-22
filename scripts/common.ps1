function Repair-PathEnvironment {
    $processEnvironment = [System.Environment]::GetEnvironmentVariables("Process")
    $pathValues = @()

    foreach ($key in $processEnvironment.Keys) {
        if ([string]::Equals([string]$key, "Path", [System.StringComparison]::OrdinalIgnoreCase)) {
            $value = [System.Environment]::GetEnvironmentVariable([string]$key, "Process")
            if ($null -ne $value -and $value -ne "") {
                $pathValues += $value
            }
        }
    }

    if ($pathValues.Count -eq 0) {
        return
    }

    $mergedEntries = New-Object System.Collections.Generic.List[string]
    $seenEntries = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($pathValue in $pathValues) {
        foreach ($entry in ($pathValue -split ";")) {
            if ($entry -eq "") {
                continue
            }

            if ($seenEntries.Add($entry)) {
                $mergedEntries.Add($entry)
            }
        }
    }

    foreach ($key in @($processEnvironment.Keys)) {
        if ([string]::Equals([string]$key, "Path", [System.StringComparison]::OrdinalIgnoreCase)) {
            [System.Environment]::SetEnvironmentVariable([string]$key, $null, "Process")
        }
    }

    [System.Environment]::SetEnvironmentVariable("Path", [string]::Join(";", $mergedEntries), "Process")
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Process")
}

function Get-CleanProcessEnvironment {
    $processEnvironment = [System.Environment]::GetEnvironmentVariables("Process")
    $clean = [ordered]@{}
    $seen = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
    $pathValues = @()

    foreach ($key in $processEnvironment.Keys) {
        $name = [string]$key
        $value = [System.Environment]::GetEnvironmentVariable($name, "Process")

        if ([string]::Equals($name, "Path", [System.StringComparison]::OrdinalIgnoreCase)) {
            if ($null -ne $value -and $value -ne "") {
                $pathValues += $value
            }
            continue
        }

        if ($seen.Add($name)) {
            $clean[$name] = $value
        }
    }

    $pathEntries = New-Object System.Collections.Generic.List[string]
    $seenPathEntries = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($pathValue in $pathValues) {
        foreach ($entry in ($pathValue -split ";")) {
            if ($entry -eq "") {
                continue
            }

            if ($seenPathEntries.Add($entry)) {
                $pathEntries.Add($entry)
            }
        }
    }

    $clean["Path"] = [string]::Join(";", $pathEntries)
    return $clean
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [string[]]$Arguments = @()
    )

    $resolvedCommand = Get-Command $Command -ErrorAction SilentlyContinue
    if ($null -eq $resolvedCommand) {
        throw "Command not found: $Command"
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $resolvedCommand.Source
    $startInfo.UseShellExecute = $false
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }

    $cleanEnvironment = Get-CleanProcessEnvironment
    $startInfo.Environment.Clear()
    foreach ($entry in $cleanEnvironment.GetEnumerator()) {
        $startInfo.Environment[[string]$entry.Key] = [string]$entry.Value
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    if ($exitCode -ne 0) {
        throw "$Command failed with exit code $exitCode"
    }
}
