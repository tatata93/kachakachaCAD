from pathlib import Path
import struct,json,collections,math,sys
base=Path(sys.argv[1]);report=json.loads((base/'print-parts.json').read_text(encoding='utf-8'));rows=[]
for part in report['parts']:
    path=base/part['stl'];data=path.read_bytes();n=struct.unpack_from('<I',data,80)[0];assert len(data)==84+n*50
    assert n>0
    edges=collections.Counter();orientation=collections.Counter();degenerate=0;volume=0
    for i in range(n):
        raw=struct.unpack_from('<12fH',data,84+i*50);points=[raw[j:j+3] for j in [3,6,9]]
        keys=[tuple(round(v/1e-5) for v in p) for p in points]
        if len(set(keys))<3:degenerate+=1;continue
        for a,b in zip(keys,keys[1:]+keys[:1]):
            edge=tuple(sorted([a,b]));edges[edge]+=1;orientation[edge]+=1 if a<b else -1
        a,b,c=points;volume+=(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6
    bad=sum(c!=2 for c in edges.values())
    rows.append(dict(id=part['id'],file=part['stl'],triangles=n,unpairedOrNonManifoldEdges=bad,
                     inconsistentOrientedEdges=sum(v!=0 for v in orientation.values()),
                     degenerateTriangles=degenerate,volumeMm3=volume,cadVolumeMm3=part['volumeMm3'],
                     relativeVolumeError=abs(volume-part['volumeMm3'])/part['volumeMm3']))
assert rows and len(rows)==report['visibleParts']
result=dict(parts=len(rows),failures=sum(bool(r['unpairedOrNonManifoldEdges'] or r['inconsistentOrientedEdges']
            or r['degenerateTriangles'] or r['volumeMm3']<=0 or r['relativeVolumeError']>.002) for r in rows),
            maximumRelativeVolumeError=.002,weldToleranceMm=1e-5,results=rows)
(base/'stl-validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('STL',len(rows),'failures',result['failures'], 'degenerate triangles',sum(r['degenerateTriangles'] for r in rows))
print([(r['file'],r['unpairedOrNonManifoldEdges']) for r in rows if r['unpairedOrNonManifoldEdges']])
raise SystemExit(bool(result['failures']))
