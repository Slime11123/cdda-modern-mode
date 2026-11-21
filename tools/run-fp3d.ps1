param(
    [ValidateSet('Original', 'Probe', 'SelfTest')][string]$Mode = 'Probe',
    [string]$UserDir = '',
    [switch]$StartIn2D,
    [string]$RuntimeDir = '',
    [ValidatePattern('^cataclysm-tiles(?:-[a-z0-9-]+)?\.exe$')][string]$ExecutableName = 'cataclysm-tiles.exe'
)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectRoot = Split-Path $sourceRoot -Parent
$runtime = if ($Mode -eq 'Original') { 'runtime/p0-0i1' } else { 'runtime/p6-sdl3' }
$runtimeRoot = if ($RuntimeDir) { $RuntimeDir } else { Join-Path $projectRoot $runtime }
$exe = if (-not $RuntimeDir -and $Mode -ne 'Original' -and $ExecutableName -eq 'cataclysm-tiles.exe') {
    Join-Path $projectRoot 'build/p7-sdl3-gpu-only/src/cataclysm-tiles.exe'
} else {
    Join-Path $runtimeRoot $ExecutableName
}
if (-not $UserDir) {
    $UserDir = Join-Path $runtimeRoot 'user'
}
if ($Mode -eq 'Original' -and $ExecutableName -eq 'cataclysm-tiles.exe' -and -not (Test-Path -LiteralPath $exe)) {
    $exe = Join-Path $runtimeRoot 'cataclysm-tiles-worldgen.exe'
}
if (-not (Test-Path -LiteralPath $exe)) { throw "Build first: $exe" }
New-Item -ItemType Directory -Path "$UserDir/config", "$UserDir/gfx" -Force | Out-Null
$userRoot = (Resolve-Path $UserDir).Path
$options = Join-Path $userRoot 'config/options.json'
if (-not (Test-Path -LiteralPath $options)) {
    $defaults = @{
        USE_LANG='zh_CN'; FULLSCREEN='no'; RENDERER='gpu'; TILES='UNDEAD_PEOPLE_BASE'
        FONT_WIDTH='12'; FONT_HEIGHT='24'; FONT_SIZE='20'; FONT_BLENDING='true'
        TERMINAL_X='100'; TERMINAL_Y='32'
    }
    @($defaults.GetEnumerator() | ForEach-Object { @{name=$_.Key;value=$_.Value} }) |
        ConvertTo-Json | Set-Content -LiteralPath $options -Encoding utf8
}
$fonts = Join-Path $userRoot 'config/fonts.json'
if (-not (Test-Path -LiteralPath $fonts)) {
    $fallback = $sourceRoot.Replace('\','/') + '/data/font/unifont.ttf'
    $uiFonts = @('C:/Windows/Fonts/consola.ttf','C:/Windows/Fonts/msyh.ttc') |
        Where-Object { Test-Path -LiteralPath $_ }
    $uiFonts = @($uiFonts) + @($fallback)
    @{typeface=$uiFonts;gui_typeface=$uiFonts;
      map_typeface=@($fallback);overmap_typeface=@($fallback)} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $fonts -Encoding utf8
}
$tiles = Join-Path $projectRoot 'cdda-windows-with-graphics-and-sounds-x64-2026-09-19-2324/gfx/UltimateCataclysm'
if ((Test-Path -LiteralPath $tiles) -and -not (Test-Path -LiteralPath "$userRoot/gfx/UltimateCataclysm")) {
    Copy-Item -LiteralPath $tiles -Destination "$userRoot/gfx" -Recurse
}
$undead = Join-Path $projectRoot 'assets/tilesets/MSX++UnDeadPeopleEdition'
if ((Test-Path -LiteralPath $undead) -and -not (Test-Path -LiteralPath "$userRoot/gfx/MSX++UnDeadPeopleEdition")) {
    Copy-Item -LiteralPath $undead -Destination "$userRoot/gfx" -Recurse
}
$previous = @{}
foreach ($name in @('CDDA_FP3D','CDDA_FP3D_SELFTEST','CDDA_FP3D_START_ACTIVE')) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:CDDA_FP3D = if ($Mode -eq 'Original') { '0' } else { '1' }
    $env:CDDA_FP3D_SELFTEST = if ($Mode -eq 'SelfTest') { '1' } else { '0' }
    $env:CDDA_FP3D_START_ACTIVE = if ($Mode -eq 'Probe' -and -not $StartIn2D) { '1' } else { '0' }
    $previousPath = $env:PATH
    $env:PATH = "$runtimeRoot;$previousPath"
    $dataRoot = if ($Mode -eq 'Original') { $sourceRoot } else { Join-Path $projectRoot 'runtime/0i1' }
    $arguments = @('--basepath', ('"' + $dataRoot.Replace('\','/') + '/"'),
                   '--userdir', ('"' + $userRoot.Replace('\','/') + '/"'))
    Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $runtimeRoot -WindowStyle Normal
} finally {
    if ($null -ne $previousPath) { $env:PATH = $previousPath }
    foreach ($name in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
}
