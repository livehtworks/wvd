$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
$commit='dabcd4681ac990dc4361de26416d986abd80e4aa'
$dir="$ValidationRoot/fixtures/offline/model/ocr"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$records=@()
foreach($file in @('det.onnx','rec.onnx','keys.txt')){
 $url="https://raw.githubusercontent.com/MaaXYZ/MaaCommonAssets/$commit/OCR/ppocr_v3/en_us/$file"
 $r=Invoke-Evidence curl.exe @('-L','--fail','--retry','1','--max-time','120',$url,'-o',"$dir/$file") 'download-models' 150
 if($r.ExitCode -ne 0){throw "Model download failed: $file"}
 $records+=@{file=$file;url=$url;sha256=(Get-FileHash -LiteralPath "$dir/$file" -Algorithm SHA256).Hash;bytes=(Get-Item -LiteralPath "$dir/$file").Length}
}
@{repository='MaaXYZ/MaaCommonAssets';commit=$commit;model='ppocr_v3/en_us';files=$records} | ConvertTo-Json -Depth 6 | Set-Content "$ValidationRoot/sdk/ocr-dependencies.json" -Encoding UTF8
