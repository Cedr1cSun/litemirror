#Requires -Version 5.1
<#
.SYNOPSIS
    Build LiteMirror from the command line.
.EXAMPLE
    .\build.ps1                 # Debug x64
    .\build.ps1 -Config Release
    .\build.ps1 -Rebuild
    .\build.ps1 -Test           # build, then run unit and integration tests
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Debug',
    [ValidateSet('x64')] [string] $Platform = 'x64',
    [switch] $Rebuild,
    [switch] $Test
)

$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; install Visual Studio 2022 or Build Tools" }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild.exe not found" }

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
& $msbuild "$PSScriptRoot\litemirror.sln" "-t:$target" "-p:Configuration=$Config" "-p:Platform=$Platform" -m -nologo -v:m
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    & "$PSScriptRoot\build\bin\$Config\litemirror-tests.exe"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & python "$PSScriptRoot\tests\integration\run.py" --config $Config
    exit $LASTEXITCODE
}
