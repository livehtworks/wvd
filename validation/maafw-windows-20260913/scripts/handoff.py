"""生成脱敏交接包，不包含真实配置、截图、SDK、模型或完整上游仓库。"""
import hashlib
import json
from pathlib import Path
import shutil
import zipfile
from collect import ROOT, read, write

def main():
    reports=ROOT/"reports"
    target=read(ROOT/"private/target.json")
    baseline=read(ROOT/"private/baseline.json")
    replacements=[
        (str(ROOT),"[VALIDATION_ROOT]"),(ROOT.as_posix(),"[VALIDATION_ROOT]"),
        (baseline["root"],"[WVD_ROOT]"),(baseline["root"].replace("\\","/"),"[WVD_ROOT]"),
        (str(Path.home()),"[USER_HOME]"),(Path.home().as_posix(),"[USER_HOME]"),
        (str(Path(target["manager"]).parents[1]),"[MUMU_ROOT]"),(Path(target["manager"]).parents[1].as_posix(),"[MUMU_ROOT]"),
        (target["serial"],"[TARGET_SERIAL]")
    ]
    def clean(value):
        if isinstance(value,dict):return {k:clean(v) for k,v in value.items()}
        if isinstance(value,list):return [clean(v) for v in value]
        if isinstance(value,str):
            for a,b in replacements:
                value=value.replace(a,b).replace(a.replace("/","\\"),b)
            return value
        return value
    commands=[]
    for line in (ROOT/"private/commands.jsonl").read_text(encoding="utf-8-sig").splitlines():
        row=json.loads(line)
        cmd=row["command"];instance=None;serial=None
        if "-s" in cmd:serial=cmd[cmd.index("-s")+1];instance=target["index"] if serial==target["serial"] else None
        if "-v" in cmd and cmd[cmd.index("-v")+1]==str(target["index"]):instance=target["index"]
        if "DEVICE" in cmd:instance=target["index"];serial=target["serial"]
        row["target_instance_id"]=instance;row["serial"]=serial
        commands.append(clean(row))
    with (reports/"commands.redacted.jsonl").open("w",encoding="utf-8") as f:
        for row in commands:f.write(json.dumps(row,ensure_ascii=False)+"\n")
    # 不回填猜测的线程ID或generation。原始事件留在本机，归一化字段未知则为null。
    with (reports/"events.redacted.jsonl").open("w",encoding="utf-8") as output:
        for path in sorted((ROOT/"runs").glob("*/events.jsonl")):
            envfile=path.parent/"environment.private.json"
            pid=read(envfile).get("pid") if envfile.exists() else None
            for line in path.read_text(encoding="utf-8").splitlines():
                row=json.loads(line);data=row.get("data",{});node=data.get("node_details",{})
                normalized={"case_run_id":path.parent.name,"seq":row.get("seq"),"utc":row.get("utc"),
                    "monotonic_ms":row.get("monotonic_us",0)/1000,"source":row.get("source"),"pid":pid,"tid":None,
                    "task_id":data.get("task_id"),"node_id":data.get("node_id",node.get("node_id")),
                    "reco_id":data.get("reco_id",node.get("reco_id")),"action_id":data.get("action_id",data.get("controller_action_id",node.get("action_id"))),
                    "generation":data.get("generation"),"business_terminal":data.get("business_terminal"),
                    "framework_status":data.get("framework_status",data.get("status")),"original_data":data}
                output.write(json.dumps(clean(normalized),ensure_ascii=False)+"\n")
    dependencies=read(ROOT/"sdk/dependencies.json")
    dependencies["ocr"]=read(ROOT/"sdk/ocr-dependencies.json")
    dependencies["nlohmann_json"]={"version":"3.11.3","url":"https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp","sha256":hashlib.sha256((ROOT/"probe/core/json.hpp").read_bytes()).hexdigest(),"license":"MIT (header retains notice)"}
    release=read(ROOT/"private/release.json")
    dependencies["linux_aarch64_asset"]=[{"name":a["name"],"url":a["browser_download_url"],"digest":a.get("digest")} for a in release["assets"] if a["name"].startswith("MAA-linux-aarch64-v5.13.0.")]
    downloaded=[ROOT/"sdk/native.zip",ROOT/"upstream/maafw.zip",ROOT/"probe/core/json.hpp",*list((ROOT/"fixtures/offline/model/ocr").glob("*"))]
    dependencies["new_download_bytes"]=sum(p.stat().st_size for p in downloaded if p.is_file())
    write(reports/"dependencies.lock.json",dependencies)
    stage=ROOT/"handoff"
    stage.mkdir(exist_ok=True)
    for directory in ["probe","scripts"]:
        for p in (ROOT/directory).rglob("*"):
            if not p.is_file() or "__pycache__" in p.parts:continue
            dest=stage/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
    for p in [ROOT/"README.md",ROOT/"run_validation.cmd"]:
        shutil.copyfile(p,stage/p.name)
    (stage/"reports").mkdir(exist_ok=True)
    for p in reports.iterdir():
        if not p.is_file():continue
        if p.name=="handoff.json":continue
        if p.suffix==".json":
            write(stage/"reports"/p.name,clean(read(p)))
        else:
            (stage/"reports"/p.name).write_text(clean(p.read_text(encoding="utf-8")),encoding="utf-8")
    licenses=stage/"licenses";licenses.mkdir(exist_ok=True)
    shutil.copyfile(ROOT/"upstream/wvd/LICENSE",licenses/"WVD-LICENSE")
    maa=next((ROOT/"upstream/maafw").iterdir())
    shutil.copyfile(maa/"LICENSE.md",licenses/"MaaFramework-LICENSE.md")
    (stage/"dependencies").mkdir(exist_ok=True)
    write(stage/"dependencies"/"lock.json",dependencies)
    examples={"index":"<确认的实例编号>","serial":"<确认的ADB地址>","adb":"<本机adb.exe绝对路径>","manager":"<本机MuMuManager.exe绝对路径>"}
    write(stage/"dependencies"/"target.example.json",examples)
    # 只交付算法埋点/合成文字图，不复制任何真实游戏整帧。
    for name in ["synthetic-positive","synthetic-negative-flat","synthetic-negative-noise","pack-a","pack-b","offline-runtime"]:
        source=ROOT/"fixtures"/name
        if not source.exists():continue
        for p in source.rglob("*"):
            if not p.is_file():continue
            dest=stage/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True)
            if p.suffix==".json":write(dest,clean(read(p)))
            else:shutil.copyfile(p,dest)
    generated=stage/"fixtures/generated";generated.mkdir(parents=True,exist_ok=True)
    for p in (ROOT/"fixtures/generated").glob("*.png"):
        if p.name.startswith("city-"):continue
        shutil.copyfile(p,generated/p.name)
    # 失败的原始内存采样是核心复现证据，不以图片/框架日志替代。
    memory=ROOT/"runs/P4-LIFETIME-c328fe0a/memory_samples.jsonl"
    shutil.copyfile(memory,stage/"reports"/"memory_samples.original.jsonl")
    # 离线异常与Unicode失败的原始stdout/stderr经路径脱敏后保留。
    for folder in ["E01-UNICODE-callback","E01-UNICODE-OCR","P4-LIFETIME-c328fe0a","P2-EXCEPTION-905a8d47"]:
        for p in (ROOT/"runs"/folder).glob("*.log"):
            dest=stage/"evidence"/folder/p.name;dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_text(clean(p.read_text(encoding="utf-8",errors="replace")),encoding="utf-8")
    # 校验只包含预定类型；扫描机器目录与生产配置，出错不创建分享包。
    for p in stage.rglob("*"):
        if not p.is_file():continue
        if p.suffix.lower() in {".exe",".dll",".onnx",".zip"}:raise RuntimeError(f"Forbidden handoff binary: {p.name}")
        if p.name=="config.json":raise RuntimeError("Production config forbidden")
        if p.suffix.lower() in {".md",".py",".ps1",".cmd",".cpp",".hpp",".json",".jsonl",".txt",".log",".manifest"}:
            text=p.read_text(encoding="utf-8",errors="replace")
            for needle in [str(ROOT),ROOT.as_posix(),str(Path.home()),str(Path(target["manager"]).parents[1]),Path(target["manager"]).parents[1].as_posix(),baseline["root"]]:
                if needle in text:raise RuntimeError(f"Unsanitized local path in {p.relative_to(stage)}")
    archive=ROOT/("VALIDATION_HANDOFF_"+ROOT.name+".zip")
    with zipfile.ZipFile(archive,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(stage.rglob("*")):
            if p.is_file():z.write(p,p.relative_to(stage).as_posix())
    write(reports/"handoff.json",{"file":archive.name,"bytes":archive.stat().st_size,"sha256":hashlib.sha256(archive.read_bytes()).hexdigest(),"privacy_check":"PASS","real_screenshots_included":False,"sdk_models_included":False})
    print(archive.name,archive.stat().st_size)
if __name__=="__main__":main()
