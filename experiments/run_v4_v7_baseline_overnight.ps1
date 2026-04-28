Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $repoRoot "build\src\Release\mtsp.exe"
$instanceDir = Join-Path $repoRoot "data\mtsp\generated_multifamily"
$resultsDir = Join-Path $repoRoot "data\results"

$resultsCsv = Join-Path $resultsDir "v4_v7_baseline_overnight_results.csv"
$summaryCsv = Join-Path $resultsDir "v4_v7_baseline_overnight_summary.csv"
$progressJson = Join-Path $resultsDir "v4_v7_baseline_overnight_progress.json"
$reportMd = Join-Path $resultsDir "v4_v7_baseline_overnight_report.md"

$solverBudgetMs = 150000
$wallTimeoutSec = 300
$totalBudgetSec = 8 * 60 * 60
$minStartWindowSec = $wallTimeoutSec + 20

$env:LKH3_WSL_BIN = Join-Path $repoRoot "baseline\LKH3\LKH"

$mainFamilies = @(
    "uniform",
    "mixed",
    "outliers",
    "high-m-stress",
    "clustered-center",
    "clustered-offset-depot"
)

$queueSpecs = @(
    @{ phase = "main"; node_count = 50000; salesman_count = 5; families = $mainFamilies },
    @{ phase = "main"; node_count = 50000; salesman_count = 7; families = $mainFamilies },
    @{ phase = "main"; node_count = 25000; salesman_count = 5; families = $mainFamilies },
    @{ phase = "main"; node_count = 25000; salesman_count = 7; families = $mainFamilies },
    @{ phase = "main"; node_count = 10000; salesman_count = 5; families = $mainFamilies },
    @{ phase = "overflow"; node_count = 10000; salesman_count = 7; families = $mainFamilies },
    @{ phase = "overflow"; node_count = 100000; salesman_count = 5; families = @("uniform", "mixed") }
)

$solvers = @(
    @{
        name = "lkh-wrapper-v4"
        args = @(
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
    },
    @{
        name = "lkh-wrapper-v7"
        args = @(
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
    },
    @{
        name = "lkh3-baseline"
        args = @(
            "--step", "lkh3-baseline",
            "--objective", "minsum",
            "--time-budget-ms", "$solverBudgetMs"
        )
    }
)

function Get-InstanceQueue {
    param(
        [string]$RootDir,
        [object[]]$Specs
    )

    $queue = New-Object System.Collections.Generic.List[object]
    $missing = New-Object System.Collections.Generic.List[string]

    foreach ($spec in $Specs) {
        foreach ($family in $spec.families) {
            $fileName = "{0}_n{1}_m{2}_r01.txt" -f $family, $spec.node_count, $spec.salesman_count
            $path = Join-Path $RootDir $fileName
            if (-not (Test-Path -LiteralPath $path)) {
                $missing.Add($path) | Out-Null
                continue
            }
            $queue.Add([pscustomobject]@{
                phase = [string]$spec.phase
                instance_family = [string]$family
                instance = $fileName
                path = (Resolve-Path -LiteralPath $path).Path
                node_count = [int]$spec.node_count
                salesman_count = [int]$spec.salesman_count
            }) | Out-Null
        }
    }

    return New-Object psobject -Property @{
        queue = $queue.ToArray()
        missing = $missing.ToArray()
    }
}

function Build-SummaryRows {
    param(
        [object[]]$Rows
    )

    $summary = New-Object System.Collections.Generic.List[object]
    $groups = $Rows | Group-Object instance
    foreach ($group in $groups) {
        $items = @($group.Group)
        $validItems = @($items | Where-Object { $_.valid -eq $true -and $null -ne $_.objective })
        $bestObjective = $null
        if ($validItems.Count -gt 0) {
            $bestObjective = ($validItems | Measure-Object -Property objective -Minimum).Minimum
        }

        foreach ($item in $items) {
            $gapPct = $null
            $winner = $false
            if ($null -ne $bestObjective -and $item.valid -eq $true -and $null -ne $item.objective) {
                $gapPct = [math]::Round((($item.objective - $bestObjective) / $bestObjective) * 100.0, 3)
                $winner = [math]::Abs($item.objective - $bestObjective) -lt 1e-9
            }

            $summary.Add([pscustomobject]@{
                phase = $item.phase
                instance_family = $item.instance_family
                instance = $item.instance
                node_count = $item.node_count
                salesman_count = $item.salesman_count
                solver = $item.solver
                valid = $item.valid
                status = $item.status
                objective = $item.objective
                gap_pct = $gapPct
                time_seconds = $item.time_seconds
                step_time_seconds = $item.step_time_seconds
                wall_seconds = $item.wall_seconds
                winner = $winner
            }) | Out-Null
        }
    }

    return @($summary.ToArray() | Sort-Object node_count, instance_family, solver)
}

function Build-ReportText {
    param(
        [object[]]$Rows,
        [object[]]$SummaryRows,
        [double]$ElapsedSeconds,
        [int]$QueuedRuns,
        [int]$CompletedRuns,
        [string]$StopReason
    )

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# Overnight v4 vs v7 vs baseline") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("Elapsed seconds: $([math]::Round($ElapsedSeconds, 3))") | Out-Null
    $lines.Add("Completed runs: $CompletedRuns / $QueuedRuns") | Out-Null
    $lines.Add("Stop reason: $StopReason") | Out-Null
    $lines.Add("") | Out-Null

    $winRows = @(
        $SummaryRows |
            Where-Object { $_ -and $null -ne $_.PSObject.Properties["winner"] -and $_.winner -eq $true }
    )
    $wins = @($winRows | Group-Object solver | Sort-Object Name)
    $lines.Add("## Win Counts") | Out-Null
    if ($wins.Count -eq 0) {
        $lines.Add("") | Out-Null
        $lines.Add("No completed winning runs yet.") | Out-Null
    } else {
        $lines.Add("") | Out-Null
        $lines.Add("| solver | wins |") | Out-Null
        $lines.Add("| --- | ---: |") | Out-Null
        foreach ($win in $wins) {
            $lines.Add("| $($win.Name) | $($win.Count) |") | Out-Null
        }
    }

    $lines.Add("") | Out-Null
    $lines.Add("## Aggregate Stats") | Out-Null
    $aggregateSource = @(
        $SummaryRows |
            Where-Object { $_ -and $null -ne $_.PSObject.Properties["solver"] }
    )
    $aggregate = @(
        $aggregateSource |
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
    $lines.Add("| solver | avg gap % | avg time s | max time s |") | Out-Null
    $lines.Add("| --- | ---: | ---: | ---: |") | Out-Null
    foreach ($item in $aggregate) {
        $lines.Add("| $($item.solver) | $($item.avg_gap_pct) | $($item.avg_time_s) | $($item.max_time_s) |") | Out-Null
    }

    $lines.Add("") | Out-Null
    $lines.Add("## Per Instance") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("| phase | family | instance | solver | valid | status | objective | gap % | time s |") | Out-Null
    $lines.Add("| --- | --- | --- | --- | --- | --- | ---: | ---: | ---: |") | Out-Null
    foreach ($item in ($SummaryRows | Sort-Object node_count, instance_family, solver)) {
        $objectiveText = if ($null -ne $item.objective) { [math]::Round([double]$item.objective, 6) } else { "" }
        $gapText = if ($null -ne $item.gap_pct) { [math]::Round([double]$item.gap_pct, 3) } else { "" }
        $timeText = if ($null -ne $item.time_seconds) { [math]::Round([double]$item.time_seconds, 3) } else { "" }
        $lines.Add("| $($item.phase) | $($item.instance_family) | $($item.instance) | $($item.solver) | $($item.valid) | $($item.status) | $objectiveText | $gapText | $timeText |") | Out-Null
    }

    return ($lines -join [Environment]::NewLine)
}

function Save-Artifacts {
    param(
        [object[]]$Rows,
        [string]$ResultsCsvPath,
        [string]$SummaryCsvPath,
        [string]$ProgressJsonPath,
        [string]$ReportPath,
        [double]$ElapsedSeconds,
        [int]$QueuedRuns,
        [int]$CompletedRuns,
        [string]$StopReason,
        [object]$QueueInfo,
        [object]$CurrentItem
    )

    $summaryRows = Build-SummaryRows -Rows $Rows

    $Rows |
        Sort-Object phase, node_count, instance_family, solver |
        Export-Csv -LiteralPath $ResultsCsvPath -NoTypeInformation -Encoding UTF8
    $summaryRows |
        Export-Csv -LiteralPath $SummaryCsvPath -NoTypeInformation -Encoding UTF8

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

    $progress = [pscustomobject]@{
        updated_utc = (Get-Date).ToUniversalTime().ToString("o")
        elapsed_seconds = [math]::Round($ElapsedSeconds, 3)
        queued_runs = $QueuedRuns
        completed_runs = $CompletedRuns
        stop_reason = $StopReason
        current_item = $CurrentItem
        missing_instances = @($QueueInfo.missing)
        wins = $wins
        result_files = [pscustomobject]@{
            results_csv = $ResultsCsvPath
            summary_csv = $SummaryCsvPath
            report_md = $ReportPath
        }
    }

    $progress |
        ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $ProgressJsonPath -Encoding UTF8

    $reportText = Build-ReportText -Rows $Rows -SummaryRows $summaryRows -ElapsedSeconds $ElapsedSeconds -QueuedRuns $QueuedRuns -CompletedRuns $CompletedRuns -StopReason $StopReason
    Set-Content -LiteralPath $ReportPath -Value $reportText -Encoding UTF8
}

function Run-SolverWithTimeout {
    param(
        [string]$ExePath,
        [object]$Instance,
        [object]$Solver,
        [int]$WallTimeoutSec
    )

    $stdoutPath = [System.IO.Path]::GetTempFileName()
    $stderrPath = [System.IO.Path]::GetTempFileName()
    $process = $null
    $killed = $false
    $launchError = $null
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $startUtc = (Get-Date).ToUniversalTime()

    try {
        $argumentList = @("--input-file", $Instance.instance) + $Solver.args
        $process = Start-Process -FilePath $ExePath `
                                 -ArgumentList $argumentList `
                                 -WorkingDirectory (Split-Path -Parent $Instance.path) `
                                 -RedirectStandardOutput $stdoutPath `
                                 -RedirectStandardError $stderrPath `
                                 -PassThru `
                                 -WindowStyle Hidden

        if (-not $process.WaitForExit($WallTimeoutSec * 1000)) {
            $killed = $true
            try {
                taskkill.exe /PID $process.Id /T /F | Out-Null
            } catch {
            }
            try {
                $process.WaitForExit()
            } catch {
            }
        } else {
            $process.WaitForExit()
        }
    } catch {
        $launchError = $_.Exception.Message
    } finally {
        $stopwatch.Stop()
    }

    $endUtc = (Get-Date).ToUniversalTime()
    $wallSeconds = [math]::Round($stopwatch.Elapsed.TotalSeconds, 6)
    $stdout = ""
    $stderr = ""
    if (Test-Path -LiteralPath $stdoutPath) {
        $stdout = [System.IO.File]::ReadAllText($stdoutPath)
    }
    if (Test-Path -LiteralPath $stderrPath) {
        $stderr = [System.IO.File]::ReadAllText($stderrPath)
    }
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    if ($null -ne $launchError) {
        return [pscustomobject]@{
            start_time_utc = $startUtc.ToString("o")
            end_time_utc = $endUtc.ToString("o")
            wall_seconds = $wallSeconds
            valid = $false
            status = "launcher_error"
            objective = $null
            time_seconds = $null
            step_time_seconds = $null
            message = $launchError
            killed = $false
            exit_code = $null
            artifacts_dir = ""
        }
    }

    if ($killed) {
        return [pscustomobject]@{
            start_time_utc = $startUtc.ToString("o")
            end_time_utc = $endUtc.ToString("o")
            wall_seconds = $wallSeconds
            valid = $false
            status = "wall_timeout"
            objective = $null
            time_seconds = $wallSeconds
            step_time_seconds = $null
            message = "Killed after exceeding the external wall-clock timeout."
            killed = $true
            exit_code = $null
            artifacts_dir = ""
        }
    }

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
            wall_seconds = $wallSeconds
            valid = $false
            status = "runner_error"
            objective = $null
            time_seconds = $wallSeconds
            step_time_seconds = $null
            message = $message
            killed = $false
            exit_code = $exitCode
            artifacts_dir = ""
        }
    }

    try {
        $json = $stdout | ConvertFrom-Json
    } catch {
        $parseMessage = if ([string]::IsNullOrWhiteSpace($stdout)) {
            "Solver produced no JSON output."
        } else {
            "Could not parse solver JSON output."
        }
        return [pscustomobject]@{
            start_time_utc = $startUtc.ToString("o")
            end_time_utc = $endUtc.ToString("o")
            wall_seconds = $wallSeconds
            valid = $false
            status = "json_parse_error"
            objective = $null
            time_seconds = $wallSeconds
            step_time_seconds = $null
            message = $parseMessage
            killed = $false
            exit_code = $exitCode
            artifacts_dir = ""
        }
    }

    $step = if ($json.steps.Count -gt 0) { $json.steps[0] } else { $null }
    $message = ""
    if ($json.PSObject.Properties.Name -contains "message") {
        $message = [string]$json.message
    }

    $artifactsDir = ""
    if ($null -ne $step -and $null -ne $step.metadata -and ($step.metadata.PSObject.Properties.Name -contains "artifacts_dir")) {
        $artifactsDir = [string]$step.metadata.artifacts_dir
    }

    return [pscustomobject]@{
        start_time_utc = $startUtc.ToString("o")
        end_time_utc = $endUtc.ToString("o")
        wall_seconds = $wallSeconds
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

$queueInfo = Get-InstanceQueue -RootDir $instanceDir -Specs $queueSpecs
$queue = @($queueInfo.queue)

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Could not find mtsp executable at $exePath"
}

$queuedRuns = $queue.Count * $solvers.Count
$rows = New-Object System.Collections.Generic.List[object]
$overall = [System.Diagnostics.Stopwatch]::StartNew()
$stopReason = "completed_queue"
$currentItem = $null
$completedRuns = 0

Save-Artifacts -Rows $rows.ToArray() `
               -ResultsCsvPath $resultsCsv `
               -SummaryCsvPath $summaryCsv `
               -ProgressJsonPath $progressJson `
               -ReportPath $reportMd `
               -ElapsedSeconds 0 `
               -QueuedRuns $queuedRuns `
               -CompletedRuns 0 `
               -StopReason "initializing" `
               -QueueInfo $queueInfo `
               -CurrentItem $null

:queueLoop foreach ($instance in $queue) {
    foreach ($solver in $solvers) {
        $elapsedSeconds = $overall.Elapsed.TotalSeconds
        $remainingSeconds = $totalBudgetSec - $elapsedSeconds
        if ($remainingSeconds -lt $minStartWindowSec) {
            $stopReason = "time_budget_reached"
            break queueLoop
        }

        $currentItem = [pscustomobject]@{
            phase = $instance.phase
            instance_family = $instance.instance_family
            instance = $instance.instance
            node_count = $instance.node_count
            salesman_count = $instance.salesman_count
            solver = $solver.name
            remaining_seconds = [math]::Round($remainingSeconds, 3)
        }

        Write-Host ("[{0}] {1} | {2} | n={3} m={4} | remaining ~{5}s" -f `
            (Get-Date).ToString("u"), `
            $instance.phase.ToUpperInvariant(), `
            $solver.name, `
            $instance.node_count, `
            $instance.salesman_count, `
            [math]::Round($remainingSeconds, 1))

        $solverResult = Run-SolverWithTimeout -ExePath $exePath -Instance $instance -Solver $solver -WallTimeoutSec $wallTimeoutSec
        $rows.Add([pscustomobject]@{
            phase = $instance.phase
            instance_family = $instance.instance_family
            instance = $instance.instance
            path = $instance.path
            node_count = $instance.node_count
            salesman_count = $instance.salesman_count
            solver = $solver.name
            solver_budget_ms = $solverBudgetMs
            wall_timeout_sec = $wallTimeoutSec
            start_time_utc = $solverResult.start_time_utc
            end_time_utc = $solverResult.end_time_utc
            wall_seconds = $solverResult.wall_seconds
            valid = $solverResult.valid
            status = $solverResult.status
            objective = $solverResult.objective
            time_seconds = $solverResult.time_seconds
            step_time_seconds = $solverResult.step_time_seconds
            message = $solverResult.message
            killed = $solverResult.killed
            exit_code = $solverResult.exit_code
            artifacts_dir = $solverResult.artifacts_dir
        }) | Out-Null

        $completedRuns += 1
        Save-Artifacts -Rows $rows.ToArray() `
                       -ResultsCsvPath $resultsCsv `
                       -SummaryCsvPath $summaryCsv `
                       -ProgressJsonPath $progressJson `
                       -ReportPath $reportMd `
                       -ElapsedSeconds $overall.Elapsed.TotalSeconds `
                       -QueuedRuns $queuedRuns `
                       -CompletedRuns $completedRuns `
                       -StopReason "running" `
                       -QueueInfo $queueInfo `
                       -CurrentItem $currentItem
    }
}

$overall.Stop()
if ($stopReason -eq "completed_queue") {
    $stopReason = "completed_queue"
}

Save-Artifacts -Rows $rows.ToArray() `
               -ResultsCsvPath $resultsCsv `
               -SummaryCsvPath $summaryCsv `
               -ProgressJsonPath $progressJson `
               -ReportPath $reportMd `
               -ElapsedSeconds $overall.Elapsed.TotalSeconds `
               -QueuedRuns $queuedRuns `
               -CompletedRuns $completedRuns `
               -StopReason $stopReason `
               -QueueInfo $queueInfo `
               -CurrentItem $currentItem

$summaryRows = Build-SummaryRows -Rows $rows.ToArray()
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
    stop_reason = $stopReason
    elapsed_seconds = [math]::Round($overall.Elapsed.TotalSeconds, 3)
    queued_runs = $queuedRuns
    completed_runs = $completedRuns
    results_csv = $resultsCsv
    summary_csv = $summaryCsv
    progress_json = $progressJson
    report_md = $reportMd
    wins = $wins
    missing_instances = @($queueInfo.missing)
} | ConvertTo-Json -Depth 8
