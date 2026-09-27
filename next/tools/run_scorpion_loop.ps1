param(
    [string]$BaseUrl = 'http://127.0.0.1:17652'
)

$ErrorActionPreference = 'Stop'
# 循环唯一所有者是应用后台。此入口只提交一次请求，不驻留、不重启任务、
# 不读写 STOP 文件；界面停止/服务退出都会取消后台的当前轮和后续轮。
$body = @{
    task_id = 'Scorpionesses'
    request_id = [guid]::NewGuid().ToString()
    resource_locale = 'zh-Hant'
    repeat = $true
} | ConvertTo-Json -Compress
$accepted = Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/v1/runs/start" -ContentType 'application/json' -Body $body -TimeoutSec 30
if (!$accepted.accepted) { throw 'START_NOT_ACCEPTED' }
Write-Output "启动请求已接收：$($accepted.request_id)"
Write-Output "开始、停止与循环状态：$BaseUrl"
