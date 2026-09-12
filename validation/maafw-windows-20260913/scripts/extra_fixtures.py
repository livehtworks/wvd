import json
import shutil
import numpy as np
from fixtures import ROOT, extract, bundle, image_write, write

ns=extract()
inn=ROOT/'upstream/wvd/resources/images/Inn.png'
bundle('wvd-negative-attention',ROOT/'runs/game-cold-wait-2/sample-0.png',inn,False,ns)
bundle('wvd-negative-title',ROOT/'runs/game-cold-wait-5/sample-0.png',inn,False,ns)
rng=np.random.default_rng(50913)
frame=np.full((1600,900,3),45,np.uint8)
a=rng.integers(15,240,(31,41,3),np.uint8)
b=rng.integers(15,240,(31,41,3),np.uint8)
frame[300:331,200:241]=a
frame[850:881,650:691]=b
fp=ROOT/'fixtures/generated/pack.png';image_write(fp,frame)
for label,tile,pos in [('pack-a',a,[220,315]),('pack-b',b,[670,865])]:
 tp=ROOT/'fixtures/generated'/f'{label}.png';image_write(tp,tile)
 bundle(label,fp,tp,True,ns,click=True)
 cfg=json.loads((ROOT/'fixtures'/label/'image_case.json').read_text())
 cfg['expected_device']=pos
 write(ROOT/'fixtures'/label/'image_case.json',cfg)
