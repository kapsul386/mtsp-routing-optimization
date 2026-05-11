# Large-scale-run launcher: sequentially executes 4 stages.
# Estimated total: ~6.2 hours.
# Logs each stage; if a stage fails, continues with next (--ContinueOnError style).
#
# Override LKH3_WSL_BIN beforehand if your binary is in another location:
#   $env:LKH3_WSL_BIN = "/mnt/c/path/to/LKH"
if (-not $env:LKH3_WSL_BIN) {
    $env:LKH3_WSL_BIN = "/mnt/c/Users/ddkup/coursework/external/LKH-3.0.7/LKH"
}
Set-Location $PSScriptRoot/../..

$startTime = Get-Date
Write-Host "=== LARGE-SCALE RUN START at $startTime ===" -ForegroundColor Cyan

$stages = @(
    @{ Name = "stage1_n25000_uniform";        Config = "experiments/configs/large_scale_n25000_config.json";        EstMin = 35  },
    @{ Name = "stage2_n50000_uniform";        Config = "experiments/configs/large_scale_n50000_config.json";        EstMin = 58  },
    @{ Name = "stage3_n100000_3families";     Config = "experiments/configs/large_scale_n100000_config.json";       EstMin = 245 },
    @{ Name = "stage4_highm_n10000";          Config = "experiments/configs/large_scale_highm_n10000_config.json";  EstMin = 35  }
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
Write-Host "=== LARGE-SCALE RUN COMPLETE at $endTime (total $([math]::Round($totalDuration, 1)) min) ===" -ForegroundColor Cyan
