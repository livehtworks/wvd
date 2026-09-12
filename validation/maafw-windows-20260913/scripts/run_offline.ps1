$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
$cases=@('P2-NORMAL','P2-STOP-WAIT','P2-STOP-CUSTOM','P2-STOP-NESTED','P2-FAIL','P2-EXCEPTION','P3-INTERRUPT','P3-RESTART','P3-STALE','P4-CONTEXT','P4-CLONE-STOP','P4-HISTORY','P4-LIFETIME')
$failed=$false
foreach($case in $cases){
 $id="$case-$([guid]::NewGuid().ToString('N').Substring(0,8))"
 $timeout=30
 if($case -eq 'P4-LIFETIME'){$timeout=300}
 $r=Invoke-Evidence "$ValidationRoot/sdk/bin/wvd_maa_probe.exe" @($case,"$ValidationRoot/fixtures/offline","$ValidationRoot/runs/$id") $id $timeout
 [pscustomobject]@{Case=$case;Exit=$r.ExitCode;Timeout=$r.TimedOut;Run=$id} | ConvertTo-Json -Compress
 if($r.ExitCode -ne 0){$failed=$true}
 if($r.TimedOut){Write-Warning "Watchdog: $case. No real inputs permitted."}
}
if($failed){exit 1}
