"""Orient manufacturing panels without scaling; preserve assembled STEP files."""
from pathlib import Path
import argparse,copy,csv,hashlib,json,math,shutil,struct,subprocess,sys

IDENTITY=((1,0,0),(0,1,0),(0,0,1))
XP=((1,0,0),(0,0,-1),(0,1,0));XM=((1,0,0),(0,0,1),(0,-1,0))
YP=((0,0,1),(0,1,0),(-1,0,0));YM=((0,0,-1),(0,1,0),(1,0,0));FLIP=((1,0,0),(0,-1,0),(0,0,-1))


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rotate(v,r):return tuple(sum(row[j]*v[j] for j in range(3)) for row in r)


def orient(source,target,r):
    data=bytearray(source.read_bytes());count=struct.unpack_from('<I',data,80)[0];assert count>0 and len(data)==84+50*count
    low=[math.inf]*3;high=[-math.inf]*3
    for i in range(count):
        for j in [12,24,36]:
            p=rotate(struct.unpack_from('<3f',data,84+i*50+j),r);assert all(math.isfinite(v) for v in p)
            for k in range(3):low[k]=min(low[k],p[k]);high[k]=max(high[k],p[k])
    move=[-(low[i]+high[i])/2 for i in [0,1]]+[-low[2]];error=0
    inverse=tuple(tuple(r[j][i] for j in range(3)) for i in range(3))
    for i in range(count):
        start=84+i*50;normal=rotate(struct.unpack_from('<3f',data,start),r);struct.pack_into('<3f',data,start,*normal)
        for j in [12,24,36]:
            original=struct.unpack_from('<3f',data,start+j);p=rotate(original,r);struct.pack_into('<3f',data,start+j,*(p[k]+move[k] for k in range(3)))
            stored=struct.unpack_from('<3f',data,start+j);restored=rotate(tuple(stored[k]-move[k] for k in range(3)),inverse)
            error=max(error,max(abs(restored[k]-original[k]) for k in range(3)))
    assert error<=5e-5;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    return dict(rotation=r,translationMm=move,bounds=[low[i]+move[i] for i in range(3)]+[high[i]+move[i] for i in range(3)],maximumVertexRoundTripErrorMm=error)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('exports',type=Path);ap.add_argument('design',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
    r=json.loads((a.exports/'print-parts.json').read_text(encoding='utf-8'));d=json.loads(a.design.read_text(encoding='utf-8'));mesh=json.loads((a.exports/'stl-validation.json').read_text(encoding='utf-8'))
    assert not r['failures'] and not r['checkOnly'] and not r['previewOnly'] and not mesh['failures'] and mesh['parts']==r['visibleParts']>0
    a.output.mkdir(parents=True,exist_ok=False);kit=d['kit'];roles={v:k for k,v in kit['panels'].items()};films=set(kit['sideGlazing']);result=copy.deepcopy(r);rows=[]
    reference_tokens=['指掛け枠','縦長手掛け枠','雨樋','ヒンジ','締結金具','貫通扉取手','横手掛け','ステップ','シャッター線','電球']
    for index,p in enumerate(result['parts'],1):
        role=roles.get(p['id'],'detail');b=p['bounds'];rotation=IDENTITY;instruction='形状に応じてスライサーで支持と姿勢を確認'
        if role.startswith('driver'):rotation=XM;instruction='外側を上。内側の裾絞りと受け座に必要な支持を付ける'
        elif role.startswith('assistant'):rotation=XP;instruction='外側を上。内側の裾絞りと受け座に必要な支持を付ける'
        elif role=='front':rotation=YP;instruction='前面を上。裏側と回込部を支持。窓の縁は支持痕を避ける'
        elif role=='rear':rotation=YM;instruction='外側を上。裏側に必要な支持を付ける'
        elif role.startswith('floor'):rotation=FLIP;instruction='床板上面を下。床下機器とスカートが上へ向く'
        elif role.startswith('roof'):instruction='屋根外側を上。内側を支持し、長手の反りを試作で確認'
        elif p['id'] in films:
            rotation=XP if b[1]+b[4]>0 else XM;role='glazing';instruction='段付き一体透明材。透明レジン等で試作。平板PETの一枚物ではない'
        reference=role=='detail' and any(t in p['name'] for t in reference_tokens)
        folder='reference-only' if reference else 'print';stem=f'P{index:03}-{p["id"][:8]}'
        stl=Path(folder)/'STL'/(stem+'.stl');step=Path(folder)/'STEP'/(stem+'.step');(a.output/step).parent.mkdir(parents=True,exist_ok=True)
        transform=orient(a.exports/p['stl'],a.output/stl,rotation);shutil.copy2(a.exports/p['step'],a.output/step)
        p.update(assemblyBounds=b,bounds=transform['bounds'],printTransform=transform,stl=stl.as_posix(),step=step.as_posix(),
            stlSha256=sha(a.output/stl),stepSha256=sha(a.output/step),role=role,referenceOnly=reference)
        size=[b[i+3]-b[i] for i in range(3)]
        if kit['compact'] and not reference:assert max(size)<125
        rows.append(dict(number=index,id=p['id'],name=p['name'],role=role,referenceOnly=reference,
            xMm=size[0],yMm=size[1],zMm=size[2],stl=stl.as_posix(),step=step.as_posix(),instruction='金属線・薄板・塗装・実物灯具などの参照。自動的に印刷対象としない' if reference else instruction))
    result.update(coordinateSystems=dict(stl='Per-part orthogonal rotation and translation to bed; scale exactly 1.0, mm',step='Original assembled CAD world coordinates'),
        sourceReportSha256=sha(a.exports/'print-parts.json'),sourceDesignSha256=sha(a.design),supportGeometryGenerated=False,
        slicerValidated=False,physicalPrintingTested=False)
    (a.output/'print-parts.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    with (a.output/'parts.csv').open('w',encoding='utf-8',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
    checker=Path(__file__).resolve().parents[1]/'series115_print/check_stl.py';subprocess.run([sys.executable,str(checker),str(a.output)],check=True)
    summary=dict(parts=len(rows),printParts=sum(not q['referenceOnly'] for q in rows),referenceParts=sum(q['referenceOnly'] for q in rows),compact=kit['compact'],
        maximumNativeExtentMm=max(max(q[k] for k in ['xMm','yMm','zMm']) for q in rows),
        maximumPrintExtentMm=max(max(q[k] for k in ['xMm','yMm','zMm']) for q in rows if not q['referenceOnly']),
        maximumVertexRoundTripErrorMm=max(p['printTransform']['maximumVertexRoundTripErrorMm'] for p in result['parts']),
        partReportSha256=sha(a.output/'print-parts.json'),stlValidationSha256=sha(a.output/'stl-validation.json'),
        physicalPrintingTested=False,runningTested=False)
    (a.output/'package-proof.json').write_text(json.dumps(summary,indent=2),encoding='utf-8');print(json.dumps(summary),flush=True)


if __name__=='__main__':main()
