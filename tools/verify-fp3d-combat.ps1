param(
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [string]$TestExecutable = '../build/0i1-tests/tests/cata_test-tiles.exe',
    [string]$ReportDir = '../qa/0i1/modern-combat',
    [string]$TestFilter = '[.fp3d_combat_compare]'
)
$ErrorActionPreference = 'Stop'
$taskOldPath = $env:PATH
$taskOldMode = $env:CDDA_FP3D_COMBAT_MODE
$taskOldReport = $env:CDDA_FP3D_COMBAT_REPORT
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $taskExecutable = (Resolve-Path -LiteralPath $TestExecutable).Path
    $taskRunDir = Join-Path $ReportDir (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
    New-Item -ItemType Directory -Path $taskRunDir -Force | Out-Null
    $taskRunDir = (Resolve-Path -LiteralPath $taskRunDir).Path
    $env:PATH = (Join-Path $ToolchainRoot 'bin') + ';' + $env:PATH
    foreach ($taskName in @('native-1', 'native-2', 'modern')) {
        $env:CDDA_FP3D_COMBAT_MODE = if ($taskName -eq 'modern') { 'modern' } else { 'native' }
        $env:CDDA_FP3D_COMBAT_REPORT = Join-Path $taskRunDir "$taskName.json"
        $taskUserDir = Join-Path $taskRunDir "$taskName-user"
        $taskLog = Join-Path $taskRunDir "$taskName.log"
        & $taskExecutable $TestFilter --rng-seed 424242 "--user-dir=$taskUserDir/" *> $taskLog
        if ($LASTEXITCODE -ne 0) { throw "Native comparison failed: $taskLog" }
    }
    $taskBaseline = (Get-FileHash -LiteralPath (Join-Path $taskRunDir 'native-1.json') -Algorithm SHA256).Hash
    foreach ($taskName in @('native-2', 'modern')) {
        $taskHash = (Get-FileHash -LiteralPath (Join-Path $taskRunDir "$taskName.json") -Algorithm SHA256).Hash
        if ($taskHash -ne $taskBaseline) { throw "Native/modern results differ: $taskName; inspect $taskRunDir" }
    }
    $taskResults = Get-Content -LiteralPath (Join-Path $taskRunDir 'modern.json') -Raw | ConvertFrom-Json
    $taskResults | Select-Object scenario, distance, displacement, peak_velocity, fuel_used, damage, elapsed_seconds, weapon, seed, attack_moves, target_damage, counterattack_moves, stamina, pain, shots, reload_moves, reloaded, ammo_remaining, reserve, vehicle, yaw, key, player_moves, velocity, cruise_velocity, turn_degrees, gasoline, battery |
        Export-Csv -LiteralPath (Join-Path $taskRunDir 'summary.csv') -NoTypeInformation -Encoding UTF8
    "PASS: $($taskResults.Count) scenarios; native baseline reproducible; modern results identical (SHA256 $taskBaseline)." |
        Tee-Object -FilePath (Join-Path $taskRunDir 'result.txt')
    "Reports: $taskRunDir"
} finally {
    $env:PATH = $taskOldPath
    $env:CDDA_FP3D_COMBAT_MODE = $taskOldMode
    $env:CDDA_FP3D_COMBAT_REPORT = $taskOldReport
    Pop-Location
}
