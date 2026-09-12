"""汇总真实运行证据。缺失项不会自动算作通过。"""
import hashlib
import json
from pathlib import Path
from statistics import median
import cv2
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"reports"
def read(p):return json.loads(p.read_text(encoding="utf-8-sig"))
def write(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding="utf-8")
def result(folder):
    p=ROOT/"runs"/folder/"case_results.json"
    return read(p) if p.exists() else {"status":"NOT_RUN"}
def event_list(folder):
    return [json.loads(x) for x in (ROOT/"runs"/folder/"events.jsonl").read_text(encoding="utf-8").splitlines()]
def compound(ids):return "PASS" if all(result(x)["status"]=="PASS" for x in ids) else "FAIL"
def main():
    OUT.mkdir(exist_ok=True)
    mapping={
    "E01-NATIVE":["E01-NATIVE-a1"],
    "P1-TEMPLATE":["image-synthetic-positive-a1","image-synthetic-negative-flat-a1","image-synthetic-negative-noise-a1"],
    "P1-WVD-BASE":["image-city-900-a1","image-wvd-negative-attention-a1","image-wvd-negative-title-a1"],
    "P1-COORD":["image-city-900-a1","image-city-1080-a1"],"P1-ASPECT":["image-aspect-reject-a1"],"P1-OCR":["image-ocr-a1"],
    "P2-NORMAL":["P2-NORMAL-f8b38b7f"],"P2-STOP-WAIT":["P2-STOP-WAIT-final"],"P2-STOP-CUSTOM":["P2-STOP-CUSTOM-final"],
    "P2-STOP-NESTED":["P2-STOP-NESTED-final"],"P2-FAIL":["P2-FAIL-7df3e22d"],"P2-EXCEPTION":["P2-EXCEPTION-905a8d47"],
    "P3-INTERRUPT":["P3-INTERRUPT-5d2f45fa"],"P3-RESTART":["P3-RESTART-30582ba8"],"P3-STALE":["P3-STALE-c11c680d"],
    "P4-CONTEXT":["P4-CONTEXT-final","P4-CLONE-STOP-final","P4-CONTEXT-96198c7a"],
    "P4-HISTORY":["P4-HISTORY-43a0a6ae"],"P4-LIFETIME":["P4-LIFETIME-c328fe0a"],"P4-PACK-ISOLATION":["P4-PACK-ISOLATION-a1"],
    "P5-CAPTURE":["P5-CAPTURE-system-a1","P5-CAPTURE-game-a1"],
    "P5-INPUT":["P5-INPUT-open","P5-INPUT-return"],
    "P5-RECONNECT":["P5-EMU-RESTART-game-ready","P5-RECONNECT-a1"],
    "P5-APP-RESTART":["P5-APP-RESTART-stop","P5-APP-RESTART-ready"],
    "P5-EMU-RESTART":["P5-EMU-RESTART-clash-start","P5-EMU-RESTART-vpn-enable","P5-EMU-RESTART-game-ready"],
    "P5-IPC":["P5-IPC-a1"]}
    cases=[{"case":k,"class":"L" if k=="P5-IPC" else "M","status":compound(v),"evidence":[f"runs/{x}" for x in v]} for k,v in mapping.items()]
    uni=ROOT/"中文 路径验证"
    uni_results=[read(uni/p/"case_results.json")["status"] for p in ["日志 回调-a2","日志 识别-a2","日志 模板"]]
    cases.extend([
        {"case":"E00-BASELINE","class":"M","status":"PASS" if read(ROOT/"private/baseline.json")["head"]=="6585f4075f5714ab522aa582993860c09af912c1" else "FAIL","evidence":["private/baseline.json","sdk/dependencies.json"]},
        {"case":"E01-UNICODE","class":"M","status":"PASS" if all(x=="PASS" for x in uni_results) else "FAIL","evidence":["中文 路径验证/日志 回调-a2","中文 路径验证/日志 识别-a2","中文 路径验证/日志 模板"],"correction":"process-local UTF-8 manifest; original failed commands preserved"},
        {"case":"E02-COLD","class":"M","status":"PASS" if read(ROOT/"runs/P5-EMU-RESTART/lifecycle.json")["boot_completed"] else "FAIL","evidence":["runs/E02-COLD","runs/E02-COLD-check","runs/P5-EMU-RESTART/lifecycle.json","runs/P5-EMU-RESTART-clash-start"],"correction":"manager-ready was earlier than ADB readiness; bounded boot-property polling validated on second normal shutdown/start"},
        {"case":"P1-NEXT","class":"L","status":"UNVERIFIED","reason":"没有与当前设备来源链一致的真实 NEXT/三角边缘目标完整正负样本；未故意触发战斗。原遮罩函数已提取，未实现/验证新遮罩分支。"},
        {"case":"P1-PAUSE","class":"L","status":"UNVERIFIED","reason":"没有完整 Pause 正例及角色/技能界面负例；合成 OCR 不等于游戏 Pause 通过。原可选 Tesseract 不可用，保留布局与反证分支待验。"},
        {"case":"CLEANUP","class":"M","status":read(OUT/"cleanup.json")["status"],"evidence":["reports/cleanup.json"]},
        {"case":"SPARK-RUNTIME","class":"OUT_OF_SCOPE","status":"OUT_OF_SCOPE","evidence":["reports/PORTABILITY_REVIEW.md"]}
    ])
    # 合成标签不是从匹配器反推：固定埋点位置独立验证。
    synthetic=result("image-synthetic-positive-a1")
    assert synthetic["device_x"]==374 and synthetic["device_y"]==437
    city=result("image-city-900-a1")
    assert 0<=city["device_x"]<=150 and 540<=city["device_y"]<=720
    assert all(result(n)["inputs"]==0 for n in ["image-synthetic-negative-flat-a1","image-synthetic-negative-noise-a1","image-wvd-negative-attention-a1","image-wvd-negative-title-a1"])
    captures=[]
    for name in ["P5-CAPTURE-system-a1","P5-CAPTURE-game-a1","P5-IPC-a1"]:
        res=result(name);decoded=[]
        for row in res["samples"]:
            path=ROOT/"runs"/name/(row["label"]+".png")
            image=cv2.imdecode(np.fromfile(str(path),dtype=np.uint8),cv2.IMREAD_COLOR) if path.exists() else None
            ok=image is not None and image.shape[:2]==(row["height"],row["width"]) and float(image.std())>1
            entry=dict(row,decoded_nonblank=ok,sha256=hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None)
            captures.append(dict(case_run=name,**entry))
            if row["label"].startswith("sample-"):decoded.append(entry)
        if not all(x["decoded_nonblank"] for x in decoded):
            for c in cases:
                if c["case"] in ("P5-IPC" if name=="P5-IPC-a1" else "P5-CAPTURE",):c["status"]="FAIL"
    write(OUT/"capture_samples.json",captures)
    summaries=[]
    for name in sorted(set(x["case_run"] for x in captures)):
        group=[x for x in captures if x["case_run"]==name and x["label"].startswith("sample-")]
        times=[x["elapsed_ms"] for x in group]
        summaries.append({"run":name,"attempts":len(group),"valid":sum(x["decoded_nonblank"] for x in group),"median_ms":median(times),"min_ms":min(times),"max_ms":max(times),"timing_boundary":"Maa post screencap -> wait -> cached decoded image; PNG encoding and disk write are outside measured interval"})
    write(OUT/"capture_summary.json",summaries)
    # 原算法和新适配器的坐标、分数逐项保留，包括低于阈值时的最佳分数。
    parity=[]
    for folder in ["synthetic-positive","synthetic-negative-flat","synthetic-negative-noise","city-900","city-1080","wvd-negative-attention","wvd-negative-title"]:
        legacy=read(ROOT/"fixtures"/folder/"legacy_result.json")
        events=event_list("image-"+folder+"-a1")
        native=[x["data"] for x in events if x["source"]=="legacy.native_match"]
        parity.append({"sample":folder,"legacy":legacy,"native":native})
    write(OUT/"parity_results.json",parity)
    index=[]
    labelled={
        "runs/discovery-native-screen/sample-0.png":("王城，左侧可见 Inn 标识，旅店正例","codex_visual_review"),
        "runs/game-cold-wait-2/sample-0.png":("游戏启动 Attention 免责声明，没有 Inn，负例","codex_visual_review"),
        "runs/game-cold-wait-5/sample-0.png":("游戏标题 Tap to Start，没有 Inn，负例","codex_visual_review"),
        "runs/P5-INPUT-open/after.png":("Android 设置：应用页；未修改开关","codex_visual_review"),
        "runs/P5-INPUT-return/after.png":("Android 设置：已返回网络和互联网页；未修改开关","codex_visual_review"),
        "runs/P5-APP-RESTART-ready/sample-0.png":("游戏应用重启后回到王城","codex_visual_review"),
        "runs/P5-EMU-RESTART-game-ready/sample-0.png":("模拟器重启、启用VPN后回到王城","codex_visual_review"),
        "runs/P5-IPC-a1/sample-0.png":("增强截图为王城；方向和颜色与ADB画面一致","codex_visual_review")
    }
    for rel,(label,level) in labelled.items():
        p=ROOT/rel;im=cv2.imdecode(np.fromfile(str(p),dtype=np.uint8),cv2.IMREAD_COLOR)
        index.append({"file":rel,"sha256":hashlib.sha256(p.read_bytes()).hexdigest(),"width":im.shape[1],"height":im.shape[0],"origin":"verified target instance","label":label,"label_source":level,"human_gold":False})
    write(OUT/"fixtures_index.json",index)
    cases.sort(key=lambda x:x["case"])
    write(OUT/"case_results.json",cases)
    counts={}
    for c in cases:counts[c["status"]]=counts.get(c["status"],0)+1
    decision="NO_GO" if any(c["class"]=="M" and c["status"]=="FAIL" for c in cases) else "BLOCKED" if any(c["class"]=="M" and c["status"]!="PASS" for c in cases) else "PASS_WITH_LIMITS"
    write(OUT/"decision.json",{"decision":decision,"fixed_cases":len(cases),"counts":counts,"mandatory":sum(c["class"]=="M" for c in cases),"mandatory_pass":sum(c["class"]=="M" and c["status"]=="PASS" for c in cases),"not_production_migration":True})
    manifest=[]
    for path in (ROOT/"probe").rglob("*"):
        if path.is_file():manifest.append({"file":path.relative_to(ROOT).as_posix(),"sha256":hashlib.sha256(path.read_bytes()).hexdigest()})
    exe=ROOT/"sdk/bin/wvd_maa_probe.exe";manifest.append({"file":"sdk/bin/wvd_maa_probe.exe","sha256":hashlib.sha256(exe.read_bytes()).hexdigest()})
    write(OUT/"artifact_hashes.json",manifest)
    print(json.dumps({"decision":decision,"counts":counts,"captures":summaries},ensure_ascii=False))
if __name__=="__main__":main()

