Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exePath = Join-Path $repoRoot "build\src\Release\mtsp.exe"
$resultsCsv = Join-Path $repoRoot "data\results\v4_v7_baseline_long_run_results.csv"
$summaryCsv = Join-Path $repoRoot "data\results\v4_v7_baseline_long_run_summary.csv"
$reportMd = Join-Path $repoRoot "data\results\v4_v7_baseline_long_run_report.md"
$progressJson = Join-Path $repoRoot "data\results\v4_v7_baseline_long_run_progress.json"

$solverBudgetMs = 150000
$wallTimeoutSec = 300
$env:LKH3_WSL_BIN = Join-Path $repoRoot "baseline\LKH3\LKH"

function Get-SolverArgs {
    param(
        [string]$SolverName
    )

    switch ($SolverName) {
        "lkh-wrapper-v4" {
            return @(
                "--step", "lkh-wrapper-v4",
                "--rounds", "8",
                "--seed", "42",
                "--candidate-count", "12",
                "--lookahead-weight", "0.35",
                "--depot-weight", "0.12",
                "--guided-cleanup-passes", "2",
                "--inter-route-batch", "3",
                "--time-budget-ms", "$solverBudgetMs"
            )
        }
        "lkh-wrapper-v7" {
            return @(
                "--step", "lkh-wrapper-v7",
                "--rounds", "4",
                "--seed", "42",
                "--local-candidate-count", "8",
                "--global-candidate-count", "14",
                "--lookahead-weight", "0.35",
                "--depot-weight", "0.12",
                "--guided-cleanup-passes", "2",
                "--inter-route-batch", "3",
                "--time-budget-ms", "$solverBudgetMs",
                "--reserve-budget-ms", "15000",
                "--exact-candidate-threshold", "512",
                "--popmusic-solutions", "12",
                "--popmusic-sample-size", "32",
                "--popmusic-window", "32",
                "--route-size-slack", "0.15",
                "--cluster-relocate-passes", "2"
            )
        }
        "lkh3-baseline" {
            return @(
                "--step", "lkh3-baseline",
                "--objective", "minsum",
                "--time-budget-ms", "$solverBudgetMs"
            )
        }
        default {
            throw "Unsupported solver: $SolverName"
        }
    }
}

function Parse-Bool {
    param(
        [object]$Value
    )

    if ($null -eq $Value) {
        return $false
    }
    return [string]$Value -eq "True"
}

function Parse-NullableDouble {
    param(
        [object]$Value
    )

    if ($null -eq $Value) {
        return $null
    }
    $text = [string]$Value
    if ([string]::IsNullOrWhiteSpace($text)) {
        return $null
    }
    return [double]$text
}

function Build-SummaryRows {
    param(
        [object[]]$Rows
    )

    $summary = New-Object System.Collections.Generic.List[object]
    $groups = $Rows | Group-Object instance
    foreach ($group in $groups) {
        $items = @($group.Group)
        $validItems = @(
            $items |
                Where-Object {
                    (Parse-Bool $_.valid) -and $null -ne (Parse-NullableDouble $_.objective)
                }
        )
        $bestObjective = $null
        if ($validItems.Count -gt 0) {
            $bestObjective = ($validItems | Measure-Object -Property objective -Minimum).Minimum
        }

        foreach ($item in $items) {
            $objective = Parse-NullableDouble $item.objective
            $timeSeconds = Parse-NullableDouble $item.time_seconds
            $stepTimeSeconds = Parse-NullableDouble $item.step_time_seconds
            $wallSeconds = Parse-NullableDouble $item.wall_seconds
            $gapPct = $null
            $winner = $false
            if ((Parse-Bool $item.valid) -and $null -ne $objective -and $null -ne $bestObjective) {
                $gapPct = [math]::Round((($objective - $bestObjective) / $bestObjective) * 100.0, 3)
                $winner = [math]::Abs($objective - $bestObjective) -lt 1e-9
            }

            $summary.Add([pscustomobject]@{
                phase = $item.phase
                instance_family = $item.instance_family
                instance = $item.instance
                node_count = [int]$item.node_count
                salesman_count = [int]$item.salesman_count
                solver = $item.solver
                valid = (Parse-Bool $item.valid)
                status = $item.status
                objective = $objective
                gap_pct = $gapPct
                time_seconds = $timeSeconds
                step_time_seconds = $stepTimeSeconds
                wall_seconds = $wallSeconds
                winner = $winner
            }) | Out-Null
        }
    }

    return @($summary.ToArray() | Sort-Object node_count, instance_family, solver)
}

function Build-ReportText {
    param(
        [object[]]$SummaryRows
    )

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# Long-run v4 vs v7 vs baseline") | Out-Null
    $lines.Add("") | Out-Null

    $winRows = @(
        $SummaryRows |
            Where-Object { $_ -and $null -ne $_.PSObject.Properties["winner"] -and $_.winner -eq $true }
    )
    $wins = @($winRows | Group-Object solver | Sort-Object Name)

    $lines.Add("## Win Counts") | Out-Null
    $lines.Add("") | Out-Null
    if ($wins.Count -eq 0) {
        $lines.Add("No completed winning runs yet.") | Out-Null
    } else {
        $lines.Add("| solver | wins |") | Out-Null
        $lines.Add("| --- | ---: |") | Out-Null
        foreach ($win in $wins) {
            $lines.Add("| $($win.Name) | $($win.Count) |") | Out-Null
        }
    }

    $aggregate = @(
        $SummaryRows |
            Where-Object { $_ -and $null -ne $_.PSObject.Properties["solver"] } |
            Group-Object solver |
            ForEach-Object {
                $items = @($_.Group | Where-Object { $_.valid -eq $true -and $null -ne $_.objective })
                [pscustomobject]@{
                    solver = $_.Name
                    avg_gap_pct = if ($items.Count -gt 0) {
                        [math]::Round((($items | Measure-Object -Property gap_pct -Average).Average), 3)
                    } else {
                        $null
                    }
                    avg_time_s = if ($items.Count -gt 0) {
                        [math]::Round((($items | Measure-Object -Property time_seconds -Average).Average), 3)
                    } else {
                        $null
                    }
                    max_time_s = if ($items.Count -gt 0) {
                        [math]::Round((($items | Measure-Object -Property time_seconds -Maximum).Maximum), 3)
                    } else {
                        $null
                    }
                }
            } |
            Sort-Object solver
    )

    $lines.Add("") | Out-Null
    $lines.Add("## Aggregate Stats") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("| solver | avg gap % | avg time s | max time s |") | Out-Null
    $lines.Add("| --- | ---: | ---: | ---: |") | Out-Null
    foreach ($item in $aggregate) {
        $lines.Add("| $($item.solver) | $($item.avg_gap_pct) | $($item.avg_time_s) | $($item.max_time_s) |") | Out-Null
    }

    return ($lines -join [Environment]::NewLine)
}

function Save-Files {
    param(
        [object[]]$Rows
    )

    $Rows | Export-Csv -LiteralPath $resultsCsv -NoTypeInformation -Encoding UTF8
    $summaryRows = Build-SummaryRows -Rows $Rows
    $summaryRows | Export-Csv -LiteralPath $summaryCsv -NoTypeInformation -Encoding UTF8
    Set-Content -LiteralPath $reportMd -Value (Build-ReportText -SummaryRows $summaryRows) -Encoding UTF8
}

function Run-SolverWithTimeout {
    param(
        [string]$ExePath,
        [string]$InstancePath,
        [string]$InstanceFile,
        [string[]]$SolverArgs,
        [int]$WallTimeoutSec
    )

    $stdoutPath = [System.IO.Path]::GetTempFileName()
    $stderrPath = [System.IO.Path]::GetTempFileName()
    $process = $null
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $startUtc = (Get-Date).ToUniversalTime()

    try {
        $argumentList = @("--input-file", $InstanceFile) + $SolverArgs
        $process = Start-Process -FilePath $ExePath `
                                 -ArgumentList $argumentList `
                                 -WorkingDirectory (Split-Path -Parent $InstancePath) `
                                 -RedirectStandardOutput $stdoutPath `
                                 -RedirectStandardError $stderrPath `
                                 -PassThru `
                                 -WindowStyle Hidden

        if (-not $process.WaitForExit($WallTimeoutSec * 1000)) {
            try {
                taskkill.exe /PID $process.Id /T /F | Out-Null
            } catch {
            }
            try {
                $process.WaitForExit()
            } catch {
            }

            $stopwatch.Stop()
            return [pscustomobject]@{
                start_time_utc = $startUtc.ToString("o")
                end_time_utc = (Get-Date).ToUniversalTime().ToString("o")
                wall_seconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 6)
                valid = $false
                status = "wall_timeout"
                objective = $null
                time_seconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 6)
                step_time_seconds = $null
                message = "Killed after exceeding the external wall-clock timeout."
                killed = $true
                exit_code = $null
                artifacts_dir = ""
            }
        }

        $process.WaitForExit()
    } finally {
        $stopwatch.Stop()
    }

    $endUtc = (Get-Date).ToUniversalTime()
    $stdout = if (Test-Path -LiteralPath $stdoutPath) { [System.IO.File]::ReadAllText($stdoutPath) } else { "" }
    $stderr = if (Test-Path -LiteralPath $stderrPath) { [System.IO.File]::ReadAllText($stderrPath) } else { "" }
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    $exitCode = $process.ExitCode
    if ($exitCode -ne 0) {
        $message = if ([string]::IsNullOrWhiteSpace($stderr)) {
            "mtsp.exe exited with code $exitCode."
        } else {
            $stderr.Trim()
        }
        return [pscustomobject]@{
            start_time_utc = $startUtc.ToString("o")
            end_time_utc = $endUtc.ToString("o")
            wall_seconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 6)
            valid = $false
            status = "runner_error"
            objective = $null
            time_seconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 6)
            step_time_seconds = $null
            message = $message
            killed = $false
            exit_code = $exitCode
            artifacts_dir = ""
        }
    }

    $json = $stdout | ConvertFrom-Json
    $step = if ($json.steps.Count -gt 0) { $json.steps[0] } else { $null }
    $message = if ($json.PSObject.Properties.Name -contains "message") { [string]$json.message } else { "" }
    $artifactsDir = ""
    if ($null -ne $step -and $null -ne $step.metadata -and ($step.metadata.PSObject.Properties.Name -contains "artifacts_dir")) {
        $artifactsDir = [string]$step.metadata.artifacts_dir
    }

    return [pscustomobject]@{
        start_time_utc = $startUtc.ToString("o")
        end_time_utc = $endUtc.ToString("o")
        wall_seconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 6)
        valid = [bool]$json.valid
        status = if ($null -ne $step) { [string]$step.status } else { [string]$json.status }
        objective = if ([bool]$json.valid) { [double]$json.objective } else { $null }
        time_seconds = [double]$json.time
        step_time_seconds = if ($null -ne $step) { [double]$step.time } else { $null }
        message = $message
        killed = $false
        exit_code = $exitCode
        artifacts_dir = $artifactsDir
    }
}

$rows = @(
    Import-Csv -LiteralPath $resultsCsv |
        Sort-Object phase, node_count, instance_family, solver
)

$repairTargets = @($rows | Where-Object { $_.status -eq "runner_error" })
$totalTargets = $repairTargets.Count
$completed = 0

foreach ($row in $repairTargets) {
    $completed += 1
    Write-Host ("[{0}] REPAIR {1}/{2} | {3} | {4}" -f `
        (Get-Date).ToString("u"), `
        $completed, `
        $totalTargets, `
        $row.solver, `
        $row.instance)

    $solverArgs = Get-SolverArgs -SolverName $row.solver
    $result = Run-SolverWithTimeout -ExePath $exePath `
                                    -InstancePath $row.path `
                                    -InstanceFile $row.instance `
                                    -SolverArgs $solverArgs `
                                    -WallTimeoutSec $wallTimeoutSec

    $row.start_time_utc = $result.start_time_utc
    $row.end_time_utc = $result.end_time_utc
    $row.wall_seconds = $result.wall_seconds
    $row.valid = $result.valid
    $row.status = $result.status
    $row.objective = $result.objective
    $row.time_seconds = $result.time_seconds
    $row.step_time_seconds = $result.step_time_seconds
    $row.message = $result.message
    $row.killed = $result.killed
    $row.exit_code = $result.exit_code
    $row.artifacts_dir = $result.artifacts_dir

    Save-Files -Rows $rows
}

$summaryRows = Build-SummaryRows -Rows $rows
$wins = @(
    $summaryRows |
        Where-Object { $_ -and $null -ne $_.PSObject.Properties["winner"] -and $_.winner -eq $true } |
        Group-Object solver |
        Sort-Object Name |
        ForEach-Object {
            [pscustomobject]@{
                solver = $_.Name
                wins = $_.Count
            }
        }
)

[pscustomobject]@{
    repaired_runs = $totalTargets
    results_csv = $resultsCsv
    summary_csv = $summaryCsv
    report_md = $reportMd
    progress_json = $progressJson
    wins = $wins
} | ConvertTo-Json -Depth 6
