param(
    [Parameter(Mandatory = $true)][string]$ToolchainRoot,
    [ValidateSet('ON', 'OFF')][string]$Feature = 'OFF',
    [string]$BuildDir = '',
    [string]$RuntimeDir = (Join-Path $PSScriptRoot '../../runtime/0i1'),
    [int]$Jobs = 12,
    [switch]$SkipBuild,
    [switch]$TestsOnly
)

$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$toolRoot = (Resolve-Path $ToolchainRoot).Path
$oldPath = $env:PATH
$oldMsystem = $env:MSYSTEM
if (-not $BuildDir) {
    $BuildDir = Join-Path $PSScriptRoot $(if ($TestsOnly) { '../../build/0i1-tests' } else { '../../build/0i1' })
}
try {
    $env:PATH = "$toolRoot/bin;$(Split-Path $toolRoot -Parent)/usr/bin;$oldPath"
    $env:MSYSTEM = 'UCRT64'
    $testOption = if ($TestsOnly) { 'ON' } else { 'OFF' }
    $buildTarget = if ($TestsOnly) { 'cata_test-tiles' } else { 'cataclysm-tiles' }
    if (-not $SkipBuild) {
        $git = (Get-Command git.exe).Source.Replace('\', '/')
        $fontInclude = "$toolRoot/include/freetype2".Replace('\', '/')
        & "$toolRoot/bin/cmake.exe" -S $sourceRoot -B $BuildDir -G Ninja `
            '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG -Wno-error -I$fontInclude" `
            '-DTILES=ON' '-DCURSES=OFF' '-DSOUND=ON' '-DLOCALIZE=ON' '-DLANGUAGES=zh_CN' `
            '-DDYNAMIC_LINKING=ON' "-DTESTS=$testOption" '-DBACKTRACE=OFF' '-DUSE_HOME_DIR=OFF' `
            '-DCATA_CCACHE=OFF' "-DGIT_BINARY=$git" "-DENABLE_FP3D=$Feature"
        if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
        & "$toolRoot/bin/cmake.exe" --build $BuildDir --target $buildTarget --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw 'Game compilation failed.' }
    }
    if ($TestsOnly) {
        $testExe = Join-Path $BuildDir 'tests/cata_test-tiles.exe'
        if (-not (Test-Path -LiteralPath $testExe)) { throw "Build tests first: $testExe" }
        Write-Output "Tests ready: $testExe"
        return
    }
    New-Item -ItemType Directory -Path $RuntimeDir -Force | Out-Null
    $exe = Join-Path $RuntimeDir 'cataclysm-tiles.exe'
    Copy-Item -LiteralPath (Join-Path $BuildDir 'src/cataclysm-tiles.exe') -Destination $exe
    & "$toolRoot/bin/strip.exe" --strip-unneeded $exe
    if ($LASTEXITCODE -ne 0) { throw 'Executable packaging failed.' }
    $queue = [Collections.Generic.Queue[string]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $queue.Enqueue($exe)
    while ($queue.Count -gt 0) {
        $binary = $queue.Dequeue()
        $imports = & "$toolRoot/bin/objdump.exe" -p $binary
        if ($LASTEXITCODE -ne 0) { throw "Cannot read imports: $binary" }
        foreach ($line in $imports) {
            if ($line -match 'DLL Name:\s*(\S+)') {
                $dll = $Matches[1]
                $candidate = Join-Path "$toolRoot/bin" $dll
                if ($seen.Add($dll) -and (Test-Path -LiteralPath $candidate)) {
                    $destination = Join-Path $RuntimeDir $dll
                    Copy-Item -LiteralPath $candidate -Destination $destination
                    $queue.Enqueue($destination)
                }
            }
        }
    }
    Write-Output "Ready: $exe"
    Write-Output "Data: --basepath $sourceRoot/ --userdir <separate-test-directory>/"
} finally {
    $env:PATH = $oldPath
    $env:MSYSTEM = $oldMsystem
}
