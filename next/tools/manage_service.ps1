param(
    [ValidateSet('Start', 'Stop', 'Deploy', 'Validate')][string]$Action = 'Start',
    [string]$CandidateRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$DataRoot = (Join-Path $env:LOCALAPPDATA 'WvdNext'),
    [ValidateRange(1, 65535)][int]$Port = 17652,
    [ValidateRange(5, 600)][int]$WaitSeconds = 120,
    [switch]$OpenBrowser
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$CandidateRoot = [IO.Path]::GetFullPath($CandidateRoot).TrimEnd('\', '/')
function Get-CandidateHash([string]$path) {
    # Python 子进程可能继承 PS7 的模块路径；标准 .NET 哈希不依赖 Get-FileHash 脚本模块。
    $stream = [IO.File]::OpenRead($path)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $hasher.Dispose(); $stream.Dispose() }
}
function Assert-PlainDirectory([string]$path) {
    $check = [IO.DirectoryInfo]::new($path)
    while ($check) {
        if (-not $check.Exists -or ($check.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "CANDIDATE_DIRECTORY_IDENTITY_INVALID: $($check.FullName)"
        }
        $check = $check.Parent
    }
}
function Assert-PlainFile([string]$path) {
    try { $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop }
    catch [System.Management.Automation.ItemNotFoundException] { return }
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "CANDIDATE_FILE_IDENTITY_INVALID: $path"
    }
}
function Assert-Candidate {
    $marker = Join-Path $CandidateRoot 'DELIVERY_STATUS.json'
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) { throw 'CANDIDATE_MARKER_MISSING' }
    $delivery = Get-Content -LiteralPath $marker -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($delivery.engine -ne 'wvd_native' -or $delivery.status -ne 'BUILT_NOT_GAME_ACCEPTED' -or
        $delivery.build_input.state -ne 'BUILT' -or -not $delivery.build_input.artifacts -or
        -not $delivery.source_commit -or -not $delivery.product_inputs_sha256 -or
        $delivery.product_inputs_sha256 -notmatch '^[0-9a-f]{64}$' -or
        $delivery.product_input_count -le 0 -or -not $delivery.files) { throw 'CANDIDATE_MARKER_INVALID' }
    if ($delivery.source_commit -ne $delivery.build_input.source.source_commit -or
        $delivery.product_inputs_sha256 -ne $delivery.build_input.source.product_inputs_sha256 -or
        $delivery.product_input_count -ne $delivery.build_input.source.product_input_count) { throw 'CANDIDATE_BUILD_IDENTITY_MISMATCH' }
    $members = @{}
    foreach ($row in $delivery.files) {
        $relative = [string]$row.path
        if (-not $relative -or $relative.Contains('\') -or $relative.Contains(':') -or
            $relative.Split('/') -contains '..' -or [IO.Path]::IsPathRooted($relative) -or $members.ContainsKey($relative)) {
            throw 'CANDIDATE_MEMBER_PATH_INVALID'
        }
        $path = Join-Path $CandidateRoot $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "CANDIDATE_INCOMPLETE: $relative" }
        if ((Get-CandidateHash $path) -ine $row.sha256) { throw "CANDIDATE_HASH_MISMATCH: $relative" }
        $members[$relative] = $row.sha256
    }
    foreach ($relative in @('automationd.exe','wvd-capture-host.exe','opencv_world4120.dll','onnxruntime.dll',
        'scrcpy-server-v3.3.4','web/index.html','pack/manifest.json','pack/parameters/semantic-assets.json',
        'data/quest.json','tools/manage_service.ps1')) {
        if (-not $members.ContainsKey($relative)) { throw "CANDIDATE_REQUIRED_MEMBER_UNRECORDED: $relative" }
    }
    if ($members['automationd.exe'] -ine $delivery.exe_sha256 -or $members['web/index.html'] -ine $delivery.web_index_sha256 -or
        $members['pack/manifest.json'] -ine $delivery.pack_manifest_sha256 -or
        $members['pack/parameters/semantic-assets.json'] -ine $delivery.resource_catalog_sha256) { throw 'CANDIDATE_IDENTITY_HASH_MISMATCH' }
    $manifest = Get-Content -LiteralPath (Join-Path $CandidateRoot 'pack/manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($artifact in $delivery.build_input.artifacts.PSObject.Properties) {
        $relative = [string]$artifact.Name
        if ($relative.StartsWith('build/Release/')) { $relative = $relative.Substring('build/Release/'.Length) }
        elseif ($relative.StartsWith('web/dist/')) { $relative = 'web/' + $relative.Substring('web/dist/'.Length) }
        else { throw 'CANDIDATE_BUILD_ARTIFACT_PATH_INVALID' }
        if (-not $members.ContainsKey($relative) -or $members[$relative] -ine $artifact.Value) { throw 'CANDIDATE_BUILD_ARTIFACT_MISMATCH' }
    }
    foreach ($relative in @('automationd.exe','wvd-capture-host.exe','opencv_world4120.dll','onnxruntime.dll','scrcpy-server-v3.3.4')) {
        if (-not $delivery.build_input.artifacts.PSObject.Properties['build/Release/' + $relative]) { throw 'CANDIDATE_BUILD_ARTIFACT_MISSING' }
    }
    if (-not $delivery.build_input.artifacts.PSObject.Properties['web/dist/index.html']) { throw 'CANDIDATE_BUILD_ARTIFACT_MISSING' }
    if ($manifest.revision -ne $delivery.pack_revision) { throw 'CANDIDATE_PACK_REVISION_MISMATCH' }
    $packMembers = @{}
    foreach ($row in $manifest.files) {
        $relative = 'pack/' + $row.path
        if ($packMembers.ContainsKey($relative) -or -not $members.ContainsKey($relative) -or $members[$relative] -ine $row.sha256) {
            throw "CANDIDATE_PACK_MEMBER_MISMATCH: $relative"
        }
        $packMembers[$relative] = $true
    }
    # 前端/资源多出来的文件同样改变发布身份，不只检查首页及模板叶子。
    foreach ($folder in @('web','pack')) {
        foreach ($file in Get-ChildItem -LiteralPath (Join-Path $CandidateRoot $folder) -File -Recurse) {
            $relative = $file.FullName.Substring($CandidateRoot.Length + 1).Replace('\','/')
            if (-not $members.ContainsKey($relative)) { throw "CANDIDATE_UNRECORDED_MEMBER: $relative" }
        }
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = [Diagnostics.ProcessStartInfo]::new((Join-Path $CandidateRoot 'automationd.exe'), '--version')
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    try {
        $null = $process.Start()
        if (-not $process.WaitForExit(10000)) { $process.Kill(); $process.WaitForExit(); throw 'CANDIDATE_VERSION_TIMEOUT' }
        if ($process.ExitCode -ne 0 -or -not $delivery.version_output -or
            $process.StandardOutput.ReadToEnd().Trim() -cne $delivery.version_output) { throw 'CANDIDATE_VERSION_FAILED' }
    } finally { $process.Dispose() }
}
# 预检在读取旧服务、申请数据锁和发送 shutdown 之前；Validate不写启动绑定。
if ($Action -ne 'Stop' -or (Test-Path -LiteralPath $CandidateRoot)) { Assert-PlainDirectory $CandidateRoot }
else { Assert-PlainDirectory (Split-Path -Parent $CandidateRoot) }
if ($Action -ne 'Stop') { Assert-Candidate }
if ($Action -eq 'Validate') { Write-Output 'CANDIDATE_VALIDATED_NOT_DEPLOYED'; return }
# The launch binding belongs to the candidate, not to an individual data-root.
# Hold this lock from its first read through successful service verification and
# atomic commit, including calls selecting two different data directories.
$bindingLock = $null
$bindingDeadline = (Get-Date).AddSeconds($WaitSeconds)
if ($Action -ne 'Stop' -or (Test-Path -LiteralPath $CandidateRoot)) { Assert-PlainDirectory $CandidateRoot }
$bindingRoot = Join-Path (Split-Path -Parent $CandidateRoot) ('.' + (Split-Path -Leaf $CandidateRoot) + '.transactions')
$null = New-Item -ItemType Directory -Force -Path $bindingRoot
Assert-PlainDirectory $bindingRoot
# Same stable writer lock as package_functional's directory transaction. The
# directory containing the binding cannot be swapped while Start owns it.
$bindingLockPath = Join-Path $bindingRoot 'writer.lock'
while (-not $bindingLock) {
    Assert-PlainFile $bindingLockPath
    try { $bindingLock = [IO.File]::Open($bindingLockPath, 'OpenOrCreate', 'ReadWrite', 'None') }
    catch [IO.IOException] {
        if ((Get-Date) -ge $bindingDeadline) { throw 'SERVICE_BINDING_BUSY: no backend was launched.' }
        Start-Sleep -Milliseconds 250
    }
}
try {
Assert-PlainFile $bindingLockPath
if ($Action -ne 'Stop' -and ((Test-Path -LiteralPath (Join-Path $bindingRoot 'transaction.json')) -or
    @(Get-ChildItem -LiteralPath $bindingRoot -Filter 'transaction.json.pending-*' -File | Select-Object -First 1).Count -or
    @(Get-ChildItem -LiteralPath $bindingRoot -Filter 'prepared-*' | Select-Object -First 1).Count)) {
    throw "CANDIDATE_TRANSACTION_PENDING: path=$bindingRoot; finish candidate recovery before service control."
}
if ($Action -ne 'Stop') { Assert-Candidate }
$launchPath = Join-Path $CandidateRoot 'service-launch.json'
Assert-PlainFile $launchPath
$bindingExisted = Test-Path -LiteralPath $launchPath -PathType Leaf
$bindingHash = if ($bindingExisted) { Get-CandidateHash $launchPath } else { $null }
$launch = $null
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
function Write-LaunchBindingBytes($stream, [byte[]]$bytes) {
    $stream.Write($bytes, 0, $bytes.Length)
}
function Flush-LaunchBindingBytes($stream) { $stream.Flush($true) }
function Publish-LaunchBindingBytes([string]$temporary, [string]$target, [bool]$replace) {
    if ($replace) { [IO.File]::Replace($temporary, $target, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $target) }
}
function Commit-LaunchBinding($service) {
    $null = Assert-Owner $service
    if ($service.state -ne 'running' -or
        $service.executable -ine (Join-Path $CandidateRoot 'automationd.exe')) {
        throw 'SERVICE_BINDING_UNVERIFIED: binding was not changed.'
    }
    Assert-PlainFile $launchPath
    $existsNow = Test-Path -LiteralPath $launchPath -PathType Leaf
    if ($existsNow -ne $bindingExisted -or
        ($existsNow -and (Get-CandidateHash $launchPath) -cne $bindingHash)) {
        throw "SERVICE_BINDING_CHANGED: process_started=true pid=$($service.pid) instance=$($service.instance_id); binding was not changed."
    }
    if ($launch -and [IO.Path]::GetFullPath($launch.data_root).TrimEnd('\', '/') -ieq $DataRoot -and
        [int]$launch.port -eq $Port) { return }
    $settings = @{ service = 'automationd'; data_root = $DataRoot; port = $Port } | ConvertTo-Json
    $temporary = "$launchPath.$([Guid]::NewGuid().ToString('N')).tmp"
    $created = $false
    $stream = $null
    try {
        $stream = [IO.File]::Open($temporary, 'CreateNew', 'Write', 'None')
        $created = $true
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($settings)
        Write-LaunchBindingBytes $stream $bytes
        Flush-LaunchBindingBytes $stream
        $stream.Dispose(); $stream = $null
        Publish-LaunchBindingBytes $temporary $launchPath $bindingExisted
    } catch {
        $primary = $_.Exception.Message
        if ($stream) { $stream.Dispose(); $stream = $null }
        $cleanup = ''
        if ($created -and (Test-Path -LiteralPath $temporary)) {
            try { Remove-Item -LiteralPath $temporary -ErrorAction Stop }
            catch { $cleanup = '; cleanup_pending=' + $temporary + '; cleanup_error=' + $_.Exception.Message }
        }
        throw "SERVICE_BINDING_COMMIT_FAILED: process_started=true pid=$($service.pid) instance=$($service.instance_id); primary_error=$primary$cleanup"
    } finally { if ($stream) { $stream.Dispose() } }
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
$launchAttempted = $false
$launchReceipt = $null
try {
    $service = Read-Service
    if (-not $service) { $service = Wait-PreviousCleanup }
    if ($service) {
        $null = Assert-Owner $service
        if ($Action -eq 'Start' -and $service.state -ne 'stopping' -and
            $service.executable -ieq (Join-Path $CandidateRoot 'automationd.exe')) {
            Commit-LaunchBinding $service
            if ($OpenBrowser) { Start-Process $url }
            $service | ConvertTo-Json -Compress
            return
        }
        Stop-ServiceInstance $service
    } elseif (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
        throw 'PORT_OCCUPIED_WITHOUT_SERVICE_IDENTITY: no unrelated process was stopped.'
    }
    if ($Action -eq 'Stop') { return }
    # The previous binding remains byte-for-byte intact until verification.
    $logs = Join-Path $DataRoot 'service-logs'
    $null = New-Item -ItemType Directory -Force -Path $logs
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $arguments = @('--launch', '--stdout-log', (Join-Path $logs "$stamp.out.log"),
        '--stderr-log', (Join-Path $logs "$stamp.err.log"),
        '--web-root', (Join-Path $CandidateRoot 'web'), '--data-root', $DataRoot,
        '--pack-root', (Join-Path $CandidateRoot 'pack'), '--quests', (Join-Path $CandidateRoot 'data/quest.json'),
        '--port', $Port, '--no-browser')
    # 原生启动器只传必要句柄并立即退出，后台不挂在临时Shell的生命周期上。
    $launchAttempted = $true
    $launched = & (Join-Path $CandidateRoot 'automationd.exe') @arguments 2> (Join-Path $logs "$stamp.launcher.err.log")
    if ($LASTEXITCODE -ne 0) { throw "SERVICE_LAUNCH_FAILED: logs=$logs\$stamp.launcher.err.log" }
    $receipt = $launched | ConvertFrom-Json
    $launchReceipt = $receipt
    $process = Get-Process -Id $receipt.pid -ErrorAction Stop
    $null = $process.Handle
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    do {
        if ($process.HasExited) { throw "SERVICE_START_FAILED: exit=$($process.ExitCode); logs=$logs\$stamp.err.log" }
        $service = Read-Service
        if ($service) {
            $null = Assert-Owner $service
            if ($service.pid -ne $process.Id) { throw 'SERVICE_START_IDENTITY_MISMATCH' }
            Commit-LaunchBinding $service
            if ($OpenBrowser) { Start-Process $url }
            $service | ConvertTo-Json -Compress
            return
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    throw "SERVICE_START_TIMEOUT: pid=$($process.Id); logs=$logs"
} catch {
    $started = if ($launchReceipt -and $launchReceipt.pid) { 'true' } elseif ($launchAttempted) { 'unconfirmed' } else { 'false' }
    $startedPid = if ($launchReceipt) { $launchReceipt.pid } else { 'unconfirmed' }
    throw "SERVICE_MANAGER_FAILED: process_started=$started pid=$startedPid; primary_error=$($_.Exception.Message)"
} finally { $lock.Dispose() }
} finally { $bindingLock.Dispose() }
