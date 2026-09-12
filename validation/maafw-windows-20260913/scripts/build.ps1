$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Existing MSVC toolchain unavailable'}
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$r=Invoke-Evidence $cmake @('-S',"$ValidationRoot/probe",'-B',"$ValidationRoot/build",'-G','Visual Studio 17 2022','-A','x64',"-DMAA_SDK=$ValidationRoot/sdk") 'E01-build' 120
if($r.ExitCode -ne 0){Write-Output $r.Error;Write-Output $r.Output;exit $r.ExitCode}
$r=Invoke-Evidence $cmake @('--build',"$ValidationRoot/build",'--config','Release') 'E01-build' 180
Write-Output $r.Output
if($r.ExitCode -ne 0){Write-Output $r.Error;exit $r.ExitCode}
Copy-Item -LiteralPath "$ValidationRoot/build/Release/wvd_maa_probe.exe" -Destination "$ValidationRoot/sdk/bin/wvd_maa_probe.exe"

