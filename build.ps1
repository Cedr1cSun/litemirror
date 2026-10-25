#Requires -Version 5.1
<#
.SYNOPSIS
    Build LiteMirror from the command line.
.EXAMPLE
    .\build.ps1                 # Debug x64
    .\build.ps1 -Config Release
    .\build.ps1 -Rebuild
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Debug',
    [ValidateSet('x64')] [string] $Platform = 'x64',
    [switch] $Rebuild
)

$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; install Visual Studio 2022 or Build Tools" }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild.exe not found" }

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
& $msbuild "$PSScriptRoot\litemirror.sln" "-t:$target" "-p:Configuration=$Config" "-p:Platform=$Platform" -m -nologo -v:m
exit $LASTEXITCODE
