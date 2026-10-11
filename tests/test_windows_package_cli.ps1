# Exercise only owned fake package-manager commands, never host installations.
$ErrorActionPreference = 'Stop'
$sourceBinary = (Resolve-Path './cupidfetch.exe').Path
$compilerDirectory = Split-Path (Get-Command gcc -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$root = Join-Path $env:RUNNER_TEMP ('cupidfetch-packages-' + [guid]::NewGuid().ToString('N'))
$oldPath = $env:PATH
$oldExtensions = $env:PATHEXT
$oldConfig = $env:XDG_CONFIG_HOME
$oldLocation = Get-Location
function Assert-Count([string] $expected) {
    $output = & $binary --json
    if ($LASTEXITCODE -ne 0) { throw "Package CLI fixture failed to run (exit $LASTEXITCODE)" }
    $json = ($output -join "`n") | ConvertFrom-Json -AsHashtable
    if ($json.package_count -cne $expected) {
        throw "Unexpected package count: $($json.package_count); expected $expected"
    }
}
try {
    New-Item -ItemType Directory (Join-Path $root 'cupidfetch') -Force | Out-Null
    $runtimeDirectory = Join-Path $root 'runtime'
    New-Item -ItemType Directory $runtimeDirectory | Out-Null
    $binary = Join-Path $runtimeDirectory 'cupidfetch.exe'
    Copy-Item $sourceBinary $binary
    # PATH isolation must not hide DLLs supplied by the compiler used above.
    # Copy only its runtime DLLs beside the identical application binary.
    Get-ChildItem -LiteralPath $compilerDirectory -Filter '*.dll' -File |
        Copy-Item -Destination $runtimeDirectory
    Set-Content (Join-Path $root 'cupidfetch/cupidfetch.conf') 'modules = pkg' -Encoding ascii
    $env:XDG_CONFIG_HOME = $root
    $env:PATH = $root
    $env:PATHEXT = '.CMD'
    Set-Location $root
    & $binary --help | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Isolated application preflight failed (exit $LASTEXITCODE)" }
    $winget = @'
@echo off
if not "%*"=="list --disable-interactivity" exit /b 9
echo Name     Id          Version
echo ---------------------------
echo Example  Example.App 1.0
exit /b 0
'@
    $choco = @'
@echo off
if "%*"=="--version" goto version
if not "%*"=="list --limit-output --no-color" exit /b 9
echo example^|1.0
exit /b 0
:version
echo 2.3.0
exit /b 0
'@
    $scoop = @'
@echo off
if not "%*"=="export" exit /b 9
echo {"apps":[{"Name":"example","Version":"1.0"},{"Name":"global","Version":"2.0","Info":"Global install"}],"buckets":[{"Name":"main"}]}
exit /b 0
'@
    Set-Content winget.cmd $winget -Encoding ascii
    Set-Content choco.cmd $choco -Encoding ascii
    Set-Content scoop.cmd $scoop -Encoding ascii
    Assert-Count '1 (winget), 1 (choco), 2 (scoop)'
    Set-Content choco.cmd ($choco.Replace('2.3.0', '1.4.0').Replace('list --limit-output', 'list --local-only --limit-output')) -Encoding ascii
    Assert-Count '1 (winget), 1 (choco), 2 (scoop)'
    Set-Content choco.cmd "@echo off`r`necho invalid version`r`nexit /b 0" -Encoding ascii
    Assert-Count '1 (winget), 2 (scoop)'
    Set-Content winget.cmd "@echo off`r`necho Unexpected output`r`nexit /b 0" -Encoding ascii
    Assert-Count '2 (scoop)'
    Set-Content winget.cmd $winget -Encoding ascii
    Set-Content scoop.cmd ($scoop.Replace('exit /b 0', 'exit /b 7')) -Encoding ascii
    Assert-Count '1 (winget)'
    Write-Output 'Native Windows package CLI tests passed'
} finally {
    Set-Location $oldLocation
    $env:PATH = $oldPath
    $env:PATHEXT = $oldExtensions
    $env:XDG_CONFIG_HOME = $oldConfig
    Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}
