#Requires -Version 5.1
<#
.SYNOPSIS
    Build LiteMirror from the command line.
.EXAMPLE
    .\build.ps1                 # Debug x64
    .\build.ps1 -Config Release
    .\build.ps1 -Rebuild
    .\build.ps1 -Test           # build, then run unit and integration tests
    .\build.ps1 -Config Release -Package   # build, then make build\LiteMirror-<version>-setup.exe
.NOTES
    Needs Visual Studio 2022 Build Tools (C++) and the .NET SDK 8 for the WPF app;
    -Package also needs Inno Setup 6 (winget install JRSoftware.InnoSetup).
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Debug',
    [ValidateSet('x64')] [string] $Platform = 'x64',
    [switch] $Rebuild,
    [switch] $Test,
    [switch] $Package
)

$ErrorActionPreference = 'Stop'
if ($Package -and $Config -ne 'Release') { throw 'package a Release build: .\build.ps1 -Config Release -Package' }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; install Visual Studio 2022 or Build Tools" }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild.exe not found" }

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
& $msbuild "$PSScriptRoot\litemirror.sln" "-t:$target" "-p:Configuration=$Config" "-p:Platform=$Platform" -m -nologo -v:m
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# the WPF app: an SDK-style project, built with dotnet into the same output folder
$dotnet = (Get-Command dotnet -ErrorAction SilentlyContinue).Source
if (-not $dotnet) { $dotnet = "$env:ProgramFiles\dotnet\dotnet.exe" }
if (-not (Test-Path $dotnet)) { throw ".NET SDK not found; install it with: winget install Microsoft.DotNet.SDK.8" }
$appArgs = @("$PSScriptRoot\src\app\LiteMirror\LiteMirror.csproj", '-c', $Config, '-nologo', '-v:m')
if ($Rebuild) { $appArgs += '--no-incremental' }
& $dotnet build @appArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    & "$PSScriptRoot\build\bin\$Config\litemirror-tests.exe"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $dotnet test "$PSScriptRoot\tests\app\LiteMirror.Tests\LiteMirror.Tests.csproj" -c $Config -nologo -v:q
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & python "$PSScriptRoot\tests\integration\run.py" --config $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($Package) {
    $version = ([xml](Get-Content "$PSScriptRoot\src\app\LiteMirror\LiteMirror.csproj")).Project.PropertyGroup.Version | Where-Object { $_ } | Select-Object -First 1
    $iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    if (-not (Test-Path $iscc)) { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
    if (-not (Test-Path $iscc)) { throw "ISCC.exe not found; install it with: winget install JRSoftware.InnoSetup" }
    & $iscc "/DVersion=$version" "/DBin=$PSScriptRoot\build\bin\$Config" "$PSScriptRoot\installer\LiteMirror.iss"
    exit $LASTEXITCODE
}
