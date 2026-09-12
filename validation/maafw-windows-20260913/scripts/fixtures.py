"""隔离样本和未修改的 WVD 原函数对照；绝不导入生产模块。"""
import ast
import hashlib
import json
import logging
from pathlib import Path
import shutil
import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "upstream/wvd/src/script.py"
def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")

def extract():
    source = SOURCE.read_text(encoding="utf-8")
    tree = ast.parse(source)
    functions = [n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name in {"_check", "_check_bright_mask"}]
    namespace = {"cv2": cv2, "np": np, "logger": logging.getLogger("offline"), "_": lambda s: s, "brightMaskCache": {}, "SaveImage": lambda *a: None}
    manifest = []
    for node in functions:
        segment = ast.get_source_segment(source, node)
        # 编译原 AST，不重写识别计算、返回值或分支。
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(SOURCE), "exec"), namespace)
        path = ROOT / "probe/legacy_reference" / (node.name + ".py")
        path.write_text(segment + "\n", encoding="utf-8")
        manifest.append({"name": node.name, "line_start": node.lineno, "line_end": node.end_lineno,
                         "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                         "segment_sha256": hashlib.sha256(segment.encode()).hexdigest(),
                         "diagnostic_substitutions": ["logger", "SaveImage", "identity translation"],
                         "scope": "single valid ROI or no ROI; CutRoI multi-ROI not exercised"})
    write(ROOT / "probe/legacy_reference/manifest.json", manifest)
    return namespace

def image_write(path, array):
    path.parent.mkdir(parents=True, exist_ok=True)
    cv2.imencode(".png", array)[1].tofile(str(path))

def bundle(name, frame, template, expected, namespace, roi=None, click=False):
    dest = ROOT / "fixtures" / name
    (dest / "pipeline").mkdir(parents=True, exist_ok=True)
    (dest / "image").mkdir(exist_ok=True)
    shutil.copyfile(ROOT / "fixtures/offline/pipeline/probe.json", dest / "pipeline/probe.json")
    shutil.copyfile(ROOT / "fixtures/offline/default_pipeline.json", dest / "default_pipeline.json")
    shutil.copyfile(template, dest / "image/target.png")
    img = cv2.imdecode(np.fromfile(str(frame), dtype=np.uint8), cv2.IMREAD_COLOR)
    tpl = cv2.imdecode(np.fromfile(str(template), dtype=np.uint8), cv2.IMREAD_COLOR)
    base = cv2.resize(img, (900,1600)) if img.shape[:2] != (1600,900) else img
    pos, score = namespace["_check"](base, tpl, [roi] if roi else None)
    rec = {"template": "target.png", "method": 5, "threshold": 0.8, "order_by": "Score"}
    if roi: rec["roi"] = roi
    cfg = {"frame": str(frame), "recognition": rec, "expected_hit": expected, "click": click}
    if click:
        cfg["expected_device"] = [round(pos[0]*img.shape[1]/900), round(pos[1]*img.shape[0]/1600)]
    write(dest / "image_case.json", cfg)
    write(dest / "legacy_result.json", {"position": pos, "score": float(score), "expected_hit": expected, "threshold": 0.8, "source": "unmodified WVD _check", "frame_sha256": hashlib.sha256(frame.read_bytes()).hexdigest()})
    return {"name":name,"frame":str(frame),"sha256":hashlib.sha256(frame.read_bytes()).hexdigest(),
            "width":img.shape[1],"height":img.shape[0],"expected_hit":expected}

def main():
    ns=extract()
    frames=ROOT/"fixtures/generated"
    rng=np.random.default_rng(2813)
    tile=rng.integers(30,245,(35,49,3),dtype=np.uint8)
    target=frames/"template.png";image_write(target,tile)
    positive=np.full((1600,900,3),60,dtype=np.uint8);positive[420:455,350:399]=tile
    negative1=np.full_like(positive,60)
    negative2=rng.integers(30,245,positive.shape,dtype=np.uint8)
    records=[]
    for name,array,hit in [("synthetic-positive",positive,True),("synthetic-negative-flat",negative1,False),("synthetic-negative-noise",negative2,False)]:
        frame=frames/(name+".png");image_write(frame,array)
        records.append(bundle(name,frame,target,hit,ns,[100,200,600,700],click=hit))
    city=ROOT/"runs/discovery-native-screen/sample-0.png"
    inn=ROOT/"upstream/wvd/resources/images/Inn.png"
    records.append(bundle("city-900",city,inn,True,ns,click=True))
    city_image=cv2.imdecode(np.fromfile(str(city),dtype=np.uint8),cv2.IMREAD_COLOR)
    derived=frames/"city-1080x1920.png";image_write(derived,cv2.resize(city_image,(1080,1920)))
    records.append(bundle("city-1080",derived,inn,True,ns,click=True))
    bad=frames/"city-1080x2400.png";image_write(bad,cv2.resize(city_image,(1080,2400)))
    bundle("aspect-reject",bad,inn,False,ns)
    cfg_path=ROOT/"fixtures/aspect-reject/image_case.json"
    cfg=json.loads(cfg_path.read_text());cfg["reject_aspect"]=True;write(cfg_path,cfg)
    ocr=np.full_like(positive,255)
    cv2.putText(ocr,"Pause",(270,700),cv2.FONT_HERSHEY_SIMPLEX,2,(0,0,0),3,cv2.LINE_AA)
    op=frames/"ocr-pause.png";image_write(op,ocr)
    bundle("ocr",op,target,True,ns)
    cfg={"frame":str(op),"recognition":{},"expected_hit":True,"ocr":True,"expected_text":["Pause"],"roi":[230,600,420,140]}
    write(ROOT/"fixtures/ocr/image_case.json",cfg)
    shutil.copytree(ROOT/"fixtures/offline/model",ROOT/"fixtures/ocr/model",dirs_exist_ok=True)
    write(ROOT/"fixtures/fixtures_index.json",records)
if __name__=="__main__":
    main()

