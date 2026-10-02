param(
    [ValidateSet('Start', 'Stop', 'Deploy')][string]$Action = 'Start',
    [string]$CandidateRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$DataRoot = (Join-Path $env:LOCALAPPDATA 'WvdNext'),
    [ValidateRange(1, 65535)][int]$Port = 17652,
    [ValidateRange(5, 600)][int]$WaitSeconds = 120,
    [switch]$OpenBrowser
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$CandidateRoot = [IO.Path]::GetFullPath($CandidateRoot).TrimEnd('\', '/')
$launchPath = Join-Path $CandidateRoot 'service-launch.json'
if (Test-Path -LiteralPath $launchPath -PathType Leaf) {
    $launch = Get-Content -LiteralPath $launchPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($launch.service -ne 'automationd') { throw 'INVALID_LAUNCH_SETTINGS' }
    if (-not $PSBoundParameters.ContainsKey('DataRoot')) { $DataRoot = $launch.data_root }
    if (-not $PSBoundParameters.ContainsKey('Port')) { $Port = [int]$launch.port }
    if ($Port -lt 1 -or $Port -gt 65535) { throw 'INVALID_LAUNCH_PORT' }
}
$DataRoot = [IO.Path]::GetFullPath($DataRoot).TrimEnd('\', '/')
$url = "http://127.0.0.1:$Port"

function Read-Service {
    try { return Invoke-RestMethod "$url/api/v1/service" -TimeoutSec 3 }
    catch { return $null }
}
function Assert-Owner($service) {
    if ($service.service -ne 'automationd' -or
        [IO.Path]::GetFullPath($service.data_root).TrimEnd('\', '/') -ine $DataRoot) {
        throw 'SERVICE_OWNER_MISMATCH: port belongs to another data directory; nothing was stopped.'
    }
    $process = Get-Process -Id $service.pid -ErrorAction Stop
    if ($process.Path -ine $service.executable) { throw 'SERVICE_PROCESS_IDENTITY_MISMATCH' }
    if ($process.StartTime.ToFileTimeUtc().ToString() -ne $service.process_start_filetime) {
        throw 'SERVICE_PROCESS_CREATION_MISMATCH'
    }
    # 提前打开进程句柄；等待的是这个实例本身，不会误认复用的 PID。
    $null = $process.Handle
    return $process
}
function Test-DataLocked {
    try {
        $probe = [IO.File]::Open((Join-Path $DataRoot 'service.lock'), 'Open', 'ReadWrite', 'None')
        $probe.Dispose()
        return $false
    } catch [IO.FileNotFoundException] { return $false }
      catch [IO.IOException] { return $true }
}
function Wait-PreviousCleanup {
    # UI退出已关监听时，数据锁可能还由清理中的实例持有。等待真实退出后才接管。
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    $reported = $false
    while (Test-DataLocked) {
        $observed = Read-Service
        if ($observed) { return $observed }
        if (-not $reported) { Write-Host 'WAITING for previous backend startup/cleanup'; $reported = $true }
        if ((Get-Date) -ge $deadline) { throw 'SERVICE_DATA_ROOT_STILL_OWNED: no second backend was launched.' }
        Start-Sleep -Milliseconds 250
    }
    return $null
}
function Stop-ServiceInstance($service) {
    $process = Assert-Owner $service
    if ($service.state -ne 'stopping') {
        $receipt = Invoke-RestMethod "$url/api/v1/service/shutdown" -Method Post -ContentType 'application/json' `
            -Body (@{ instance_id = $service.instance_id } | ConvertTo-Json -Compress) -TimeoutSec 15
        if ($receipt.instance_id -ne $service.instance_id -or $receipt.state -ne 'stopping') { throw 'INVALID_SHUTDOWN_RECEIPT' }
    }
    # 端口关闭早于设备/任务回收。必须等进程结束，不能只看到 HTTP 断线就启动新版。
    if (-not $process.WaitForExit($WaitSeconds * 1000)) {
        throw "SERVICE_EXIT_TIMEOUT: pid=$($service.pid); no second backend was launched."
    }
    Write-Host "STOPPED pid=$($service.pid) instance=$($service.instance_id)"
}

# 串行化工具的启动/部署；锁由 OS 随进程释放，不依赖陈旧 PID 文件。
$null = New-Item -ItemType Directory -Force -Path $DataRoot
$lockPath = Join-Path $DataRoot 'service-manager.lock'
$deadline = (Get-Date).AddSeconds($WaitSeconds)
$lock = $null
while (-not $lock) {
    try { $lock = [IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') }
    catch [IO.IOException] {
        if ((Get-Date) -ge $deadline) { throw 'SERVICE_MANAGER_BUSY' }
        Start-Sleep -Milliseconds 250
    }
}
try {
    if ($Action -ne 'Stop') {
        foreach ($item in @('automationd.exe', 'web/index.html', 'pack/manifest.json', 'data/quest.json')) {
            if (-not (Test-Path -LiteralPath (Join-Path $CandidateRoot $item) -PathType Leaf)) { throw "CANDIDATE_INCOMPLETE: $item" }
        }
    }
    $service = Read-Service
    if (-not $service) { $service = Wait-PreviousCleanup }
    if ($service) {
        $null = Assert-Owner $service
        if ($Action -eq 'Start' -and $service.state -ne 'stopping' -and
            $service.executable -ieq (Join-Path $CandidateRoot 'automationd.exe')) {
            if ($OpenBrowser) { Start-Process $url }
            $service | ConvertTo-Json -Compress
            return
        }
        Stop-ServiceInstance $service
    } elseif (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
        throw 'PORT_OCCUPIED_WITHOUT_SERVICE_IDENTITY: no unrelated process was stopped.'
    }
    if ($Action -eq 'Stop') { return }
    # 部署绑定只保存启动参数，不修改用户profile。打包的双击入口据此找到正式目录/端口。
    $settings = @{ service = 'automationd'; data_root = $DataRoot; port = $Port } | ConvertTo-Json
    $temporary = "$launchPath.$([Guid]::NewGuid().ToString('N')).tmp"
    [IO.File]::WriteAllText($temporary, $settings, [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $launchPath -Force
    $logs = Join-Path $DataRoot 'service-logs'
    $null = New-Item -ItemType Directory -Force -Path $logs
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $arguments = @('--launch', '--stdout-log', (Join-Path $logs "$stamp.out.log"),
        '--stderr-log', (Join-Path $logs "$stamp.err.log"),
        '--web-root', (Join-Path $CandidateRoot 'web'), '--data-root', $DataRoot,
        '--pack-root', (Join-Path $CandidateRoot 'pack'), '--quests', (Join-Path $CandidateRoot 'data/quest.json'),
        '--port', $Port, '--no-browser')
    # 原生启动器只传必要句柄并立即退出，后台不挂在临时Shell的生命周期上。
    $launched = & (Join-Path $CandidateRoot 'automationd.exe') @arguments 2> (Join-Path $logs "$stamp.launcher.err.log")
    if ($LASTEXITCODE -ne 0) { throw "SERVICE_LAUNCH_FAILED: logs=$logs\$stamp.launcher.err.log" }
    $receipt = $launched | ConvertFrom-Json
    $process = Get-Process -Id $receipt.pid -ErrorAction Stop
    $null = $process.Handle
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    do {
        if ($process.HasExited) { throw "SERVICE_START_FAILED: exit=$($process.ExitCode); logs=$logs\$stamp.err.log" }
        $service = Read-Service
        if ($service) {
            $null = Assert-Owner $service
            if ($service.pid -ne $process.Id) { throw 'SERVICE_START_IDENTITY_MISMATCH' }
            if ($OpenBrowser) { Start-Process $url }
            $service | ConvertTo-Json -Compress
            return
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    throw "SERVICE_START_TIMEOUT: pid=$($process.Id); logs=$logs"
} finally { $lock.Dispose() }
