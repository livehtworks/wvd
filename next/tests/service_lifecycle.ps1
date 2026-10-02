param([Parameter(Mandatory)][string]$CandidateRoot, [int]$Port = 17764)
$ErrorActionPreference = 'Stop'
$sourceCandidate = (Resolve-Path $CandidateRoot).Path
# 专属空目录，不读取正式 profile，不连设备、不启动游戏任务。
$root = Join-Path $env:TEMP ('wvd-service-lifecycle-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
$CandidateRoot = Join-Path $root 'candidate'
$null = New-Item -ItemType Directory -Path $CandidateRoot
Get-ChildItem -LiteralPath $sourceCandidate -File | Where-Object { $_.Extension -in '.exe', '.dll' } |
    Copy-Item -Destination $CandidateRoot
foreach ($directory in @('web', 'pack')) {
    $null = New-Item -ItemType Junction -Path (Join-Path $CandidateRoot $directory) -Target (Join-Path $sourceCandidate $directory)
}
$null = New-Item -ItemType Directory -Path "$CandidateRoot/tools", "$CandidateRoot/data"
Copy-Item -LiteralPath "$sourceCandidate/tools/manage_service.ps1" -Destination "$CandidateRoot/tools"
Copy-Item -LiteralPath "$sourceCandidate/data/quest.json" -Destination "$CandidateRoot/data"
$manager = Join-Path $CandidateRoot 'tools/manage_service.ps1'
$url = "http://127.0.0.1:$Port"
function Require($condition, $message) { if (-not $condition) { throw $message } }
try {
    & $manager -Action Start -CandidateRoot $CandidateRoot -DataRoot $root -Port $Port
    $first = Invoke-RestMethod "$url/api/v1/service"
    $profile = Invoke-RestMethod "$url/api/v1/profile"
    & $manager -Action Start -CandidateRoot $CandidateRoot -DataRoot $root -Port $Port
    $same = Invoke-RestMethod "$url/api/v1/service"
    Require ($same.instance_id -eq $first.instance_id) 'Repeated Start spawned another instance'
    $rejected = $false
    try { Invoke-RestMethod "$url/api/v1/service/shutdown" -Method Post -ContentType 'application/json' -Body '{"instance_id":"stale-page"}' > $null }
    catch { $rejected = [int]$_.Exception.Response.StatusCode -eq 409 }
    Require $rejected 'Stale shutdown was not rejected'
    $arguments = @('--web-root', "`"$CandidateRoot\web`"", '--data-root', "`"$root`"",
        '--pack-root', "`"$CandidateRoot\pack`"", '--quests', "`"$CandidateRoot\data\quest.json`"",
        '--port', ($Port + 1), '--no-browser')
    $duplicate = Start-Process -FilePath "$CandidateRoot\automationd.exe" -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput "$root/duplicate.out.log" -RedirectStandardError "$root/duplicate.err.log"
    $null = $duplicate.Handle
    Require ($duplicate.WaitForExit(15000)) 'Duplicate failed to exit'
    Require ($duplicate.ExitCode -eq 1) 'Duplicate did not reject shared data ownership'
    Require ((Get-Content -Raw "$root/duplicate.err.log") -match 'SERVICE_DATA_ROOT_LOCKED') 'Missing ownership rejection'
    & $manager -Action Deploy -CandidateRoot $CandidateRoot -DataRoot $root -Port $Port
    $replacement = Invoke-RestMethod "$url/api/v1/service"
    Require ($replacement.instance_id -ne $first.instance_id) 'Deploy did not replace instance'
    Require (-not (Get-Process -Id $first.pid -ErrorAction SilentlyContinue)) 'Old backend remained alive'
    $after = Invoke-RestMethod "$url/api/v1/profile"
    Require ($profile.revision -eq $after.revision) 'Deployment modified profile'
    $receipt = Invoke-RestMethod "$url/api/v1/service/shutdown" -Method Post -ContentType 'application/json' `
        -Body (@{instance_id=$replacement.instance_id} | ConvertTo-Json -Compress)
    Require ($receipt.state -eq 'stopping') 'Missing UI-style shutdown receipt'
    # 使用绑定后的默认参数立即启动，覆盖退出清理窗口与双击入口的目录/端口选择。
    & $manager -Action Start -CandidateRoot $CandidateRoot
    $restart = Invoke-RestMethod "$url/api/v1/service"
    Require (-not (Get-Process -Id $replacement.pid -ErrorAction SilentlyContinue)) 'Stop left backend alive'
    Require ($restart.instance_id -ne $replacement.instance_id) 'Restart reused stopping instance'
    & $manager -Action Stop -CandidateRoot $CandidateRoot
    Require (-not (Get-Process -Id $restart.pid -ErrorAction SilentlyContinue)) 'Bound Stop left backend alive'
    Write-Host "PASS: repeated start, stale identity, data lock, deploy, preserved profile, immediate restart, bound launch/exit. Evidence: $root"
} finally {
    & $manager -Action Stop -CandidateRoot $CandidateRoot -DataRoot $root -Port $Port
}
