param([switch]$NoPause,[string]$Case='P2-NORMAL')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
if($Case -eq 'DEVICE'){throw 'Real device execution is deliberately excluded from the default launcher.'}
$id="$Case-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))"
$bundle="$ValidationRoot/fixtures/offline-runtime"
if(-not (Test-Path -LiteralPath "$bundle/pipeline/probe.json")){
 New-Item -ItemType Directory -Force "$bundle/pipeline" | Out-Null
 Copy-Item -LiteralPath "$ValidationRoot/fixtures/offline/pipeline/probe.json" -Destination "$bundle/pipeline/probe.json"
 Copy-Item -LiteralPath "$ValidationRoot/fixtures/offline/default_pipeline.json" -Destination "$bundle/default_pipeline.json"
}
$r=Invoke-Evidence "$ValidationRoot/sdk/bin/wvd_maa_probe.exe" @($Case,$bundle,"$ValidationRoot/runs/$id") $id $(if($Case -eq 'P4-LIFETIME'){300}else{30})
Write-Output $r.Output
Write-Output $r.Error
exit $r.ExitCode
