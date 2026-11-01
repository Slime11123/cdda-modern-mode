param(
    [ValidateSet('Original', 'Probe', 'SelfTest')][string]$Mode = 'Original',
    [string]$UserDir = (Join-Path $PSScriptRoot '../../runtime/0i1/user'),
    [switch]$StartIn2D,
    [string]$RuntimeDir = ''
)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectRoot = Split-Path $sourceRoot -Parent
$runtime = if ($Mode -eq 'Original') { 'runtime/p0-0i1' } else { 'runtime/0i1' }
$runtimeRoot = if ($RuntimeDir) { $RuntimeDir } else { Join-Path $projectRoot $runtime }
$exe = Join-Path $runtimeRoot 'cataclysm-tiles.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    $exe = Join-Path $runtimeRoot 'cataclysm-tiles-worldgen.exe'
}
if (-not (Test-Path -LiteralPath $exe)) { throw "Build first: $exe" }
New-Item -ItemType Directory -Path "$UserDir/config", "$UserDir/gfx" -Force | Out-Null
$userRoot = (Resolve-Path $UserDir).Path
$options = Join-Path $userRoot 'config/options.json'
if (-not (Test-Path -LiteralPath $options)) {
    $defaults = @{
        USE_LANG='zh_CN'; FULLSCREEN='no'; RENDERER='opengl'; TILES='UNDEAD_PEOPLE_BASE'
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
    # This is the interactive game the user launched, rather than a background helper.
    $arguments = @('--basepath', ('"' + $sourceRoot.Replace('\','/') + '/"'),
                   '--userdir', ('"' + $userRoot.Replace('\','/') + '/"'))
    Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Normal
} finally {
    foreach ($name in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
}
