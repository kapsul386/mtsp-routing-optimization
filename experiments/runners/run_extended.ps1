# Extended-run launcher: 5 fast solvers (no lkh3-baseline, no lkh_v21_minmax).
# Sequential: N=50K (uniform) -> N=100K (3 families) -> high-m N=10K.
# Estimated total: ~4 hours.
#
# Override LKH3_WSL_BIN beforehand if your binary is in another location:
#   $env:LKH3_WSL_BIN = "/mnt/c/path/to/LKH"
if (-not $env:LKH3_WSL_BIN) {
    $env:LKH3_WSL_BIN = "/mnt/c/Users/ddkup/coursework/external/LKH-3.0.7/LKH"
}
Set-Location $PSScriptRoot/../..

$startTime = Get-Date
Write-Host "=== EXTENDED RUN START at $startTime ===" -ForegroundColor Cyan

$stages = @(
    @{ Name = "stage1_n50000_uniform";        Config = "experiments/configs/extended_n50000_config.json";        EstMin = 40  },
    @{ Name = "stage2_n100000_3families";     Config = "experiments/configs/extended_n100000_config.json";       EstMin = 175 },
    @{ Name = "stage3_highm_n10000";          Config = "experiments/configs/extended_highm_n10000_config.json";  EstMin = 30  }
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
Write-Host "=== EXTENDED RUN COMPLETE at $endTime (total $([math]::Round($totalDuration, 1)) min) ===" -ForegroundColor Cyan
