param([Parameter(Mandatory=$true)][string]$RepoRoot)
. "$PSScriptRoot/common.ps1"
$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
$baseline = [ordered]@{ root=$repo; head=(& git -C $repo rev-parse HEAD); dirty=@(& git -C $repo status --porcelain); remotes=@(& git -C $repo remote -v); files=@() }
$paths = @(& git -C $repo ls-files) + @('config.json','dist/wvd/config.json','dist/wvd/wvd.exe')
if (Test-Path "$repo/mod") { $paths += Get-ChildItem "$repo/mod" -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($repo,$_.FullName) } }
foreach ($p in ($paths | Sort-Object -Unique)) {
    $full = Join-Path $repo $p
    if (Test-Path -LiteralPath $full -PathType Leaf) {
        $baseline.files += @{path=$full;sha256=(Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash}
    }
}
$baseline | ConvertTo-Json -Depth 6 | Set-Content "$ValidationRoot/private/baseline.json" -Encoding UTF8
$r = Invoke-Evidence git @('-C',$repo,'archive','--format=zip','-o',"$ValidationRoot/upstream/wvd.zip",'6585f4075f5714ab522aa582993860c09af912c1')
if ($r.ExitCode) { throw $r.Error }
Expand-Archive "$ValidationRoot/upstream/wvd.zip" "$ValidationRoot/upstream/wvd" -Force
$tag = Invoke-RestMethod 'https://api.github.com/repos/MaaXYZ/MaaFramework/git/ref/tags/v5.13.0'
if ($tag.object.sha -ne '2bcfa85c66a2eac6ca3e5937f175495275ee0643') { throw 'Tag SHA mismatch' }
$release = Invoke-RestMethod 'https://api.github.com/repos/MaaXYZ/MaaFramework/releases/tags/v5.13.0'
$release | ConvertTo-Json -Depth 12 | Set-Content "$ValidationRoot/private/release.json" -Encoding UTF8
$asset = $release.assets | Where-Object name -eq 'MAA-win-x86_64-v5.13.0.zip'
if (-not $asset -or $asset.size -gt 500MB) { throw 'SDK asset unavailable or over budget' }
$r = Invoke-Evidence curl.exe @('-L','--fail','--retry','1','--max-time','180','-o',"$ValidationRoot/sdk/native.zip",$asset.browser_download_url) 'download' 190
if ($r.ExitCode) { throw $r.Error }
$digest = (Get-FileHash "$ValidationRoot/sdk/native.zip" -Algorithm SHA256).Hash.ToLowerInvariant()
if ("sha256:$digest" -ne $asset.digest) { throw 'SDK SHA256 mismatch' }
Expand-Archive "$ValidationRoot/sdk/native.zip" "$ValidationRoot/sdk" -Force
$url = 'https://api.github.com/repos/MaaXYZ/MaaFramework/zipball/2bcfa85c66a2eac6ca3e5937f175495275ee0643'
$r = Invoke-Evidence curl.exe @('-L','--fail','--retry','1','--max-time','180','-o',"$ValidationRoot/upstream/maafw.zip",$url) 'download' 190
if ($r.ExitCode) { throw $r.Error }
Expand-Archive "$ValidationRoot/upstream/maafw.zip" "$ValidationRoot/upstream/maafw" -Force
@{sdk_url=$asset.browser_download_url;sdk_sha256=$digest;maafw_sha=$tag.object.sha;wvd_sha=$baseline.head;source_url=$url;source_sha256=(Get-FileHash "$ValidationRoot/upstream/maafw.zip").Hash} | ConvertTo-Json | Set-Content "$ValidationRoot/sdk/dependencies.json" -Encoding UTF8
Write-Output 'Fixed sources and verified native SDK prepared.'
