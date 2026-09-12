$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
$baseline=Get-Content -LiteralPath "$ValidationRoot/private/baseline.json" -Encoding UTF8 | ConvertFrom-Json
$changes=@()
foreach($file in $baseline.files){
 if(-not (Test-Path -LiteralPath $file.path -PathType Leaf)){$changes+=@{path=$file.path;reason='missing'};continue}
 $now=(Get-FileHash -LiteralPath $file.path -Algorithm SHA256).Hash
 if($now -ne $file.sha256){$changes+=@{path=$file.path;reason='hash_changed'}}
}
$git=@(& git -C $baseline.root status --porcelain)
$target=Get-Content -LiteralPath "$ValidationRoot/private/target.json" -Encoding UTF8 | ConvertFrom-Json
$r=Invoke-Evidence $target.adb @('-s',$target.serial,'shell','rm','-f','/data/local/tmp/wvdmaa-validation-ui.xml') 'CLEANUP' 30
$tempRemoved=$r.ExitCode -eq 0
$running=@(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -and $_.ExecutablePath.StartsWith($ValidationRoot,[StringComparison]::OrdinalIgnoreCase) -and $_.Name -eq 'wvd_maa_probe.exe'} | Select-Object ProcessId,Name)
$r=Invoke-Evidence $target.manager @('info','-v',[string]$target.index) 'CLEANUP' 30
$info=$r.Output | ConvertFrom-Json
$r=Invoke-Evidence $target.adb @('-s',$target.serial,'shell','dumpsys','connectivity') 'CLEANUP' 30
$vpn=$r.Output -match 'Transports: VPN'
$report=@{status=$(if($changes.Count -eq 0 -and $git.Count -eq 0 -and $running.Count -eq 0 -and $tempRemoved){'PASS'}else{'FAIL'});checked_files=$baseline.files.Count;changed_files=$changes;git_dirty=$git;probe_processes=$running;android_diagnostic_removed=$tempRemoved;target_running=$info.is_android_started;vpn_running=$vpn;user_state='游戏保留在王城；Clash/VPN 开启；不关闭用户原先使用的实例。';production_config='hash unchanged';artifacts_retained='独立验证目录，包括所有失败证据；未删除用户日志。'}
$report | ConvertTo-Json -Depth 7 | Set-Content "$ValidationRoot/reports/cleanup.json" -Encoding UTF8
$report | Select-Object status,checked_files,target_running,vpn_running,android_diagnostic_removed
