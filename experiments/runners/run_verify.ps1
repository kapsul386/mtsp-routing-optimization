# Verification run with the canonical LKH-3 binary.
# Stage A: N=25K with LKH-3 + 4 v21 solvers ~ 70-90 min
# Stage B: N=50K v21-only (no LKH-3, would take too long) ~ 35-45 min
# Total: ~2 hours
#
# Set LKH3_WSL_BIN beforehand if your binary lives outside the default location:
#   $env:LKH3_WSL_BIN = "/mnt/c/path/to/LKH"
if (-not $env:LKH3_WSL_BIN) {
    $env:LKH3_WSL_BIN = "/mnt/c/Users/ddkup/coursework/external/LKH-3.0.7/LKH"
}
Set-Location $PSScriptRoot/../..

$startTime = Get-Date
Write-Host "=== VERIFY RUN START at $startTime ===" -ForegroundColor Cyan
Write-Host "    LKH3_WSL_BIN = $env:LKH3_WSL_BIN" -ForegroundColor Gray

$stages = @(
    @{ Name = "verify_stageA_n25000_with_LKH3";        Config = "experiments/configs/verify_n25000_config.json";           EstMin = 75 },
    @{ Name = "verify_stageB_n50000_v21_only";          Config = "experiments/configs/verify_n50000_v21_only_config.json"; EstMin = 40 }
)

foreach ($stage in $stages) {
    $name = $stage.Name
    $config = $stage.Config
    $stageStart = Get-Date
    Write-Host ""
    Write-Host "=== [$name] start at $stageStart (est ~$($stage.EstMin) min) ===" -ForegroundColor Yellow

    try {
        python experiments/runners/run_benchmarks.py --config $config 2>&1 | Tee-Object -FilePath "data/results/${name}_run.log"
        $stageEnd = Get-Date
        $duration = ($stageEnd - $stageStart).TotalMinutes
        Write-Host "=== [$name] done in $([math]::Round($duration, 1)) min ===" -ForegroundColor Green
    } catch {
        Write-Host "=== [$name] FAILED: $_ ===" -ForegroundColor Red
    }
}

$endTime = Get-Date
$totalDuration = ($endTime - $startTime).TotalMinutes
Write-Host ""
Write-Host "=== VERIFY RUN COMPLETE at $endTime (total $([math]::Round($totalDuration, 1)) min) ===" -ForegroundColor Cyan
