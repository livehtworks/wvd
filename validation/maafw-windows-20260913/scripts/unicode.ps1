$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
$dir="$ValidationRoot/中文 路径验证"
New-Item -ItemType Directory -Force "$dir/程序","$dir/图像 模板模型" | Out-Null
Get-ChildItem -LiteralPath "$ValidationRoot/sdk/bin" -File -Filter '*.dll' | Copy-Item -Destination "$dir/程序"
Copy-Item -LiteralPath "$ValidationRoot/sdk/bin/plugins" -Destination "$dir/程序" -Recurse -Force
Copy-Item -LiteralPath "$ValidationRoot/sdk/bin/wvd_maa_probe.exe" -Destination "$dir/程序"
Copy-Item -Path "$ValidationRoot/fixtures/ocr/*" -Destination "$dir/图像 模板模型" -Recurse -Force
Copy-Item -LiteralPath "$ValidationRoot/fixtures/generated/ocr-pause.png" -Destination "$dir/图像 模板模型/原始 画面.png"
$cfg=Get-Content -LiteralPath "$dir/图像 模板模型/image_case.json" -Encoding UTF8 | ConvertFrom-Json
$cfg.frame="$dir/图像 模板模型/原始 画面.png"
$cfg | ConvertTo-Json -Depth 8 | Set-Content "$dir/图像 模板模型/image_case.json" -Encoding UTF8
$r=Invoke-Evidence "$dir/程序/wvd_maa_probe.exe" @('E01-UNICODE',"$dir/图像 模板模型","$dir/日志 回调-a2") 'E01-UNICODE-callback-a2' 30
"Callback exit=$($r.ExitCode)"
$r=Invoke-Evidence "$dir/程序/wvd_maa_probe.exe" @('IMAGE',"$dir/图像 模板模型","$dir/日志 识别-a2") 'E01-UNICODE-OCR-a2' 30
"OCR exit=$($r.ExitCode)"
New-Item -ItemType Directory -Force "$dir/模板 对照" | Out-Null
Copy-Item -Path "$ValidationRoot/fixtures/synthetic-positive/*" -Destination "$dir/模板 对照" -Recurse -Force
Copy-Item -LiteralPath "$ValidationRoot/fixtures/generated/synthetic-positive.png" -Destination "$dir/模板 对照/原始 画面.png"
$cfg=Get-Content -LiteralPath "$dir/模板 对照/image_case.json" -Encoding UTF8 | ConvertFrom-Json
$cfg.frame="$dir/模板 对照/原始 画面.png"
$cfg | ConvertTo-Json -Depth 8 | Set-Content "$dir/模板 对照/image_case.json" -Encoding UTF8
$r=Invoke-Evidence "$dir/程序/wvd_maa_probe.exe" @('IMAGE',"$dir/模板 对照","$dir/日志 模板") 'E01-UNICODE-template' 30
"Template exit=$($r.ExitCode)"
