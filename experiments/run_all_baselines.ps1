param(
    [string]$InstanceDir = "data/mtsp/generated_multifamily",
    [string]$InstanceGlob = "*.txt",
    [int]$TimeoutSec = 360,
    [int]$GraspIters = 50,
    [int]$GraspRcl = 3,
    [int]$Seed = 42,
    [switch]$SkipFilo2,
    [switch]$SkipLkh3Baseline
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($TimeoutSec -lt 300 -or $TimeoutSec -gt 400) {
    throw "TimeoutSec must be in [300..400]. Current value: $TimeoutSec"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$instanceRoot = Join-Path $repoRoot $InstanceDir
$resultsDir = Join-Path $repoRoot "data/results/manual_runs/all_baselines"
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$resultsCsv = Join-Path $resultsDir "all_baselines_$timestamp.csv"

$mtspBudgetSec = [Math]::Max(1, [Math]::Min(330, $TimeoutSec - 20))
$mtspBudgetMs = $mtspBudgetSec * 1000
$filo2TimeLimitSec = [Math]::Max(1, [Math]::Min($TimeoutSec - 10, 360))

New-Item -ItemType Directory -Path $resultsDir -Force | Out-Null

function Read-MtspHeader {
    param([string]$Path)
    $line = (Get-Content -LiteralPath $Path -TotalCount 1).Trim()
    if ([string]::IsNullOrWhiteSpace($line)) {
        throw "Empty instance file: $Path"
    }
    $parts = $line -split "\s+"
    if ($parts.Count -lt 2) {
        throw "Invalid mTSP header in $Path"
    }
    return [pscustomobject]@{
        n = [int]$parts[0]
        m = [int]$parts[1]
    }
}

function Invoke-WithTimeout {
    param(
        [string]$FilePath,
        [string[]]$ArgumentList,
        [string]$WorkingDirectory,
        [int]$TimeoutSeconds
    )

    $stdoutPath = [System.IO.Path]::GetTempFileName()
    $stderrPath = [System.IO.Path]::GetTempFileName()
    $process = $null
    $killed = $false
    $startUtc = (Get-Date).ToUniversalTime()
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

    try {
        $process = Start-Process -FilePath $FilePath `
                                 -ArgumentList $ArgumentList `
                                 -WorkingDirectory $WorkingDirectory `
                                 -RedirectStandardOutput $stdoutPath `
                                 -RedirectStandardError $stderrPath `
                                 -PassThru `
                                 -WindowStyle Hidden

        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
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
    } finally {
        $stopwatch.Stop()
    }

    $stdout = if (Test-Path -LiteralPath $stdoutPath) { [System.IO.File]::ReadAllText($stdoutPath) } else { "" }
    $stderr = if (Test-Path -LiteralPath $stderrPath) { [System.IO.File]::ReadAllText($stderrPath) } else { "" }
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    return [pscustomobject]@{
        start_time_utc = $startUtc.ToString("o")
        end_time_utc = (Get-Date).ToUniversalTime().ToString("o")
        wall_seconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 6)
        killed = $killed
        exit_code = if ($null -ne $process) { $process.ExitCode } else { $null }
        stdout = $stdout
        stderr = $stderr
    }
}

function Parse-RunMtspOutput {
    param([string]$Stdout, [string]$Stderr)

    $joined = @($Stdout, $Stderr) -join "`n"
    $pattern = "Status:\s*(?<status>\S+)\s*\|\s*Valid:\s*(?<valid>\S+)\s*\|\s*Objective:\s*(?<objective>[^|]+)\|\s*Time:\s*(?<time>[0-9.]+)\s*s"
    $match = [regex]::Match($joined, $pattern)
    if (-not $match.Success) {
        return [pscustomobject]@{
            parsed = $false
            status = "parse_failed"
            valid = $false
            objective = $null
            solve_time_seconds = $null
        }
    }

    $objectiveText = $match.Groups["objective"].Value.Trim()
    $objective = $null
    if ($objectiveText -ne "n/a") {
        $objective = [double]$objectiveText
    }

    return [pscustomobject]@{
        parsed = $true
        status = $match.Groups["status"].Value
        valid = ([string]$match.Groups["valid"].Value).ToLowerInvariant() -eq "true"
        objective = $objective
        solve_time_seconds = [double]$match.Groups["time"].Value
    }
}

function Get-Filo2ResultPath {
    param([string]$Stdout, [string]$Stderr)
    $joined = @($Stdout, $Stderr) -join "`n"
    $match = [regex]::Match($joined, "Saved result to (?<path>.+)")
    if (-not $match.Success) {
        return $null
    }
    return $match.Groups["path"].Value.Trim()
}

function Invoke-MtspSolver {
    param(
        [string]$InstancePath,
        [string]$SolverName,
        [string[]]$SolverArgs,
        [int]$TimeoutSeconds
    )

    $run = Invoke-WithTimeout -FilePath "python" `
                              -ArgumentList @("run_mtsp.py", "--input", $InstancePath) + $SolverArgs `
                              -WorkingDirectory $repoRoot `
                              -TimeoutSeconds $TimeoutSeconds

    $parsed = Parse-RunMtspOutput -Stdout $run.stdout -Stderr $run.stderr
    $status = if ($run.killed) { "wall_timeout" } elseif ($run.exit_code -ne 0) { "runner_error" } else { $parsed.status }

    return [pscustomobject]@{
        solver = $SolverName
        status = $status
        valid = if ($run.killed -or $run.exit_code -ne 0) { $false } else { $parsed.valid }
        objective = if ($run.killed -or $run.exit_code -ne 0) { $null } else { $parsed.objective }
        solve_time_seconds = if ($run.killed -or $run.exit_code -ne 0) { $null } else { $parsed.solve_time_seconds }
        wall_seconds = $run.wall_seconds
        killed = $run.killed
        exit_code = $run.exit_code
        extra = ""
        stdout_tail = if ($run.stdout.Length -gt 800) { $run.stdout.Substring($run.stdout.Length - 800) } else { $run.stdout }
        stderr_tail = if ($run.stderr.Length -gt 800) { $run.stderr.Substring($run.stderr.Length - 800) } else { $run.stderr }
    }
}

function Invoke-Filo2Solver {
    param(
        [string]$InstancePath,
        [int]$M,
        [int]$N,
        [int]$TimeoutSeconds
    )

    $args = @(
        "baseline/filo2/run_filo2_baseline.py",
        "--input", $InstancePath,
        "--m", "$M",
        "--time-limit", "$filo2TimeLimitSec",
        "--seed", "$Seed"
    )

    $run = Invoke-WithTimeout -FilePath "python" `
                              -ArgumentList $args `
                              -WorkingDirectory $repoRoot `
                              -TimeoutSeconds $TimeoutSeconds

    if ($run.killed) {
        return [pscustomobject]@{
            solver = "filo2-cvrp-adapter"
            status = "wall_timeout"
            valid = $false
            objective = $null
            solve_time_seconds = $null
            wall_seconds = $run.wall_seconds
            killed = $true
            exit_code = $run.exit_code
            extra = "timeout_before_result"
            stdout_tail = if ($run.stdout.Length -gt 800) { $run.stdout.Substring($run.stdout.Length - 800) } else { $run.stdout }
            stderr_tail = if ($run.stderr.Length -gt 800) { $run.stderr.Substring($run.stderr.Length - 800) } else { $run.stderr }
        }
    }

    if ($run.exit_code -ne 0) {
        return [pscustomobject]@{
            solver = "filo2-cvrp-adapter"
            status = "runner_error"
            valid = $false
            objective = $null
            solve_time_seconds = $null
            wall_seconds = $run.wall_seconds
            killed = $false
            exit_code = $run.exit_code
            extra = "nonzero_exit"
            stdout_tail = if ($run.stdout.Length -gt 800) { $run.stdout.Substring($run.stdout.Length - 800) } else { $run.stdout }
            stderr_tail = if ($run.stderr.Length -gt 800) { $run.stderr.Substring($run.stderr.Length - 800) } else { $run.stderr }
        }
    }

    $resultPath = Get-Filo2ResultPath -Stdout $run.stdout -Stderr $run.stderr
    if ([string]::IsNullOrWhiteSpace($resultPath) -or -not (Test-Path -LiteralPath $resultPath)) {
        return [pscustomobject]@{
            solver = "filo2-cvrp-adapter"
            status = "result_missing"
            valid = $false
            objective = $null
            solve_time_seconds = $null
            wall_seconds = $run.wall_seconds
            killed = $false
            exit_code = $run.exit_code
            extra = "result_json_not_found"
            stdout_tail = if ($run.stdout.Length -gt 800) { $run.stdout.Substring($run.stdout.Length - 800) } else { $run.stdout }
            stderr_tail = if ($run.stderr.Length -gt 800) { $run.stderr.Substring($run.stderr.Length - 800) } else { $run.stderr }
        }
    }

    $payload = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    $metrics = $payload.metrics
    $isValid = $false
    $objective = $null
    if ($null -ne $metrics) {
        $isValid = [bool]$metrics.valid_cover
        $objective = [double]$metrics.total_distance
    }

    $extraNote = "result=$resultPath"
    if ($N -ge 100000) {
        $extraNote = "$extraNote;high_n_watch=true;raw_solution_source=$($payload.solution_source)"
    }

    return [pscustomobject]@{
        solver = "filo2-cvrp-adapter"
        status = [string]$payload.status
        valid = $isValid
        objective = $objective
        solve_time_seconds = [double]$payload.elapsed_seconds
        wall_seconds = $run.wall_seconds
        killed = $false
        exit_code = $run.exit_code
        extra = $extraNote
        stdout_tail = if ($run.stdout.Length -gt 800) { $run.stdout.Substring($run.stdout.Length - 800) } else { $run.stdout }
        stderr_tail = if ($run.stderr.Length -gt 800) { $run.stderr.Substring($run.stderr.Length - 800) } else { $run.stderr }
    }
}

$instances = @(Get-ChildItem -Path $instanceRoot -Filter $InstanceGlob -File | Sort-Object Name)
if ($instances.Count -eq 0) {
    throw "No instances matched: $instanceRoot / $InstanceGlob"
}

Write-Host ("Running {0} instances | timeout={1}s | mtsp_budget={2}ms | filo2_limit={3}s" -f $instances.Count, $TimeoutSec, $mtspBudgetMs, $filo2TimeLimitSec)
Write-Host "Results CSV: $resultsCsv"

$rows = New-Object System.Collections.Generic.List[object]

foreach ($instance in $instances) {
    $header = Read-MtspHeader -Path $instance.FullName
    $n = $header.n
    $m = $header.m
    Write-Host ("[{0}] Instance {1} | n={2}, m={3}" -f (Get-Date).ToString("u"), $instance.Name, $n, $m)

    $solverPlans = @(
        [pscustomobject]@{ name = "2opt+greed"; args = @("--step", "2opt+greed"); enabled = $true; reason = "" },
        [pscustomobject]@{ name = "grasp"; args = @("--step", "grasp", "--iters", "$GraspIters", "--rcl", "$GraspRcl", "--seed", "$Seed"); enabled = $true; reason = "" },
        [pscustomobject]@{
            name = "lkh-wrapper-v12"
            args = @(
                "--step", "lkh-wrapper-v12",
                "--seed", "$Seed",
                "--time-budget-ms", "$mtspBudgetMs",
                "--reserve-budget-ms", "3000",
                "--local-candidate-count", "8",
                "--global-candidate-count", "14",
                "--popmusic-solutions", "12",
                "--popmusic-sample-size", "32",
                "--popmusic-window", "32",
                "--route-size-slack", "0.15",
                "--lookahead-weight", "0.35",
                "--depot-weight", "0.12"
            )
            enabled = $true
            reason = ""
        },
        [pscustomobject]@{
            name = "lkh3-baseline"
            args = @("--step", "lkh3-baseline", "--objective", "minsum", "--time-budget-ms", "$mtspBudgetMs")
            enabled = (-not $SkipLkh3Baseline) -and ($n -le 10000)
            reason = if ($SkipLkh3Baseline) { "disabled_by_flag" } elseif ($n -gt 10000) { "n_gt_10000" } else { "" }
        }
    )

    foreach ($plan in $solverPlans) {
        if (-not $plan.enabled) {
            $rows.Add([pscustomobject]@{
                timestamp_utc = (Get-Date).ToUniversalTime().ToString("o")
                instance = $instance.Name
                instance_path = $instance.FullName
                n = $n
                m = $m
                solver = $plan.name
                status = "skipped"
                valid = $false
                objective = $null
                solve_time_seconds = $null
                wall_seconds = 0
                killed = $false
                exit_code = $null
                extra = $plan.reason
                stdout_tail = ""
                stderr_tail = ""
            }) | Out-Null
            continue
        }

        Write-Host ("  -> {0}" -f $plan.name)
        $result = Invoke-MtspSolver -InstancePath $instance.FullName -SolverName $plan.name -SolverArgs $plan.args -TimeoutSeconds $TimeoutSec
        $rows.Add([pscustomobject]@{
            timestamp_utc = (Get-Date).ToUniversalTime().ToString("o")
            instance = $instance.Name
            instance_path = $instance.FullName
            n = $n
            m = $m
            solver = $result.solver
            status = $result.status
            valid = $result.valid
            objective = $result.objective
            solve_time_seconds = $result.solve_time_seconds
            wall_seconds = $result.wall_seconds
            killed = $result.killed
            exit_code = $result.exit_code
            extra = $result.extra
            stdout_tail = $result.stdout_tail
            stderr_tail = $result.stderr_tail
        }) | Out-Null
    }

    if (-not $SkipFilo2) {
        $filo2WatchTag = ""
        if ($n -ge 100000) {
            $filo2WatchTag = " [HIGH_N_WATCH]"
        }
        Write-Host ("  -> filo2-cvrp-adapter{0}" -f $filo2WatchTag)
        $filo2Result = Invoke-Filo2Solver -InstancePath $instance.FullName -M $m -N $n -TimeoutSeconds $TimeoutSec
        $rows.Add([pscustomobject]@{
            timestamp_utc = (Get-Date).ToUniversalTime().ToString("o")
            instance = $instance.Name
            instance_path = $instance.FullName
            n = $n
            m = $m
            solver = $filo2Result.solver
            status = $filo2Result.status
            valid = $filo2Result.valid
            objective = $filo2Result.objective
            solve_time_seconds = $filo2Result.solve_time_seconds
            wall_seconds = $filo2Result.wall_seconds
            killed = $filo2Result.killed
            exit_code = $filo2Result.exit_code
            extra = $filo2Result.extra
            stdout_tail = $filo2Result.stdout_tail
            stderr_tail = $filo2Result.stderr_tail
        }) | Out-Null
    } else {
        $rows.Add([pscustomobject]@{
            timestamp_utc = (Get-Date).ToUniversalTime().ToString("o")
            instance = $instance.Name
            instance_path = $instance.FullName
            n = $n
            m = $m
            solver = "filo2-cvrp-adapter"
            status = "skipped"
            valid = $false
            objective = $null
            solve_time_seconds = $null
            wall_seconds = 0
            killed = $false
            exit_code = $null
            extra = "disabled_by_flag"
            stdout_tail = ""
            stderr_tail = ""
        }) | Out-Null
    }

    $rows.ToArray() | Export-Csv -LiteralPath $resultsCsv -NoTypeInformation -Encoding UTF8
}

$summary = @(
    $rows.ToArray() |
        Group-Object solver |
        ForEach-Object {
            $solverRows = @($_.Group)
            $ok = @($solverRows | Where-Object { $_.status -eq "ok" -and $_.valid -eq $true -and $null -ne $_.objective })
            [pscustomobject]@{
                solver = $_.Name
                runs = $solverRows.Count
                ok_valid_runs = $ok.Count
                avg_objective = if ($ok.Count -gt 0) { [Math]::Round((($ok | Measure-Object -Property objective -Average).Average), 6) } else { $null }
                avg_wall_seconds = [Math]::Round((($solverRows | Measure-Object -Property wall_seconds -Average).Average), 6)
                timeouts = @($solverRows | Where-Object { $_.status -eq "wall_timeout" }).Count
                skipped = @($solverRows | Where-Object { $_.status -eq "skipped" }).Count
            }
        } |
        Sort-Object solver
)

$summaryCsv = [System.IO.Path]::ChangeExtension($resultsCsv, ".summary.csv")
$summary | Export-Csv -LiteralPath $summaryCsv -NoTypeInformation -Encoding UTF8

Write-Host ""
Write-Host "Done."
Write-Host "Results: $resultsCsv"
Write-Host "Summary: $summaryCsv"
