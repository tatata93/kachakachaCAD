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

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo.FileName = $resolvedCommand.Source
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true

    $process.StartInfo.Environment.Clear()
    $cleanEnvironment = Get-CleanProcessEnvironment
    foreach ($key in $cleanEnvironment.Keys) {
        $process.StartInfo.Environment[$key] = $cleanEnvironment[$key]
    }

    foreach ($argument in $Arguments) {
        $process.StartInfo.ArgumentList.Add($argument)
    }

    $null = $process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()

    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()

    if ($stdout -ne "") {
        Write-Host $stdout.TrimEnd()
    }

    if ($stderr -ne "") {
        Write-Error $stderr.TrimEnd()
    }

    if ($process.ExitCode -ne 0) {
        throw "$Command failed with exit code $($process.ExitCode)"
    }
}
