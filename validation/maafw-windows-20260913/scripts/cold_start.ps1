param([string]$Case='E02-COLD')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
$target=Get-Content -LiteralPath "$ValidationRoot/private/target.json" -Encoding UTF8 | ConvertFrom-Json
$manager=$target.manager
$adb=$target.adb
$serial=$target.serial
$index=[string]$target.index
function Info {
 $r=Invoke-Evidence $manager @('info','-v',$index) $Case 30
 if($r.ExitCode -ne 0){throw 'Manager info failed'}
 return ($r.Output | ConvertFrom-Json)
}
$before=Info
if($before.index -ne $index -or "$($before.adb_host_ip):$($before.adb_port)" -ne $serial){throw 'Target identity mismatch'}
$before | ConvertTo-Json -Depth 6 | Set-Content "$ValidationRoot/private/$Case-before.json" -Encoding UTF8
if($before.is_process_started){
 $r=Invoke-Evidence $manager @('control','-v',$index,'shutdown') $Case 30
 if($r.ExitCode -ne 0){throw 'Normal shutdown failed; no force kill allowed'}
 $stopped=$false
 for($i=0;$i -lt 12;$i++){
  Start-Sleep -Seconds 5
  $info=Info
  if(-not $info.is_process_started){$stopped=$true;break}
 }
 if(-not $stopped){throw 'Shutdown not confirmed'}
}
$closed=Info
if($closed.is_process_started){throw 'Cold precondition failed'}
$r=Invoke-Evidence $manager @('control','-v',$index,'launch') $Case 30
if($r.ExitCode -ne 0){throw 'Launch failed'}
$ready=$false
for($i=0;$i -lt 36;$i++){
 Start-Sleep -Seconds 5
 $info=Info
 if($info.is_android_started -and $info.is_process_started){
  $connection=Invoke-Evidence $adb @('connect',$serial) $Case 30
  $boot=Invoke-Evidence $adb @('-s',$serial,'shell','getprop','sys.boot_completed') $Case 30
  if($boot.ExitCode -eq 0 -and $boot.Output.Trim() -eq '1'){$ready=$true;break}
 }
}
if(-not $ready){throw 'Android startup exceeded 180s'}
if($info.index -ne $index -or "$($info.adb_host_ip):$($info.adb_port)" -ne $serial){throw 'Restarted identity mismatch'}
$r=Invoke-Evidence $adb @('connect',$serial) $Case 30
if($r.ExitCode -ne 0){throw 'ADB connect failed'}
$r=Invoke-Evidence $adb @('-s',$serial,'shell','getprop','sys.boot_completed') $Case 30
if($r.Output.Trim() -ne '1'){throw 'Boot not completed'}
@{initially_running=[bool]$before.is_process_started;normally_closed=$true;boot_completed=$true;old_pid=$before.pid;new_pid=$info.pid;serial=$serial;index=$index} | ConvertTo-Json | Set-Content "$ValidationRoot/runs/$Case/lifecycle.json" -Encoding UTF8
Write-Output "Cold start confirmed: target index $index, old pid $($before.pid), new pid $($info.pid)"
