"""Compare actual exported sections, retaining a regression rejection baseline."""
from pathlib import Path
import argparse,hashlib,json,struct
import numpy as np


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def mesh(path):
    data=path.read_bytes();count=struct.unpack_from('<I',data,80)[0]
    assert len(data)==84+50*count
    return np.ndarray((count,12),dtype='<f4',buffer=data,offset=84,strides=(50,4))[:,3:].reshape(-1,3,3).astype(float)


def front_section(triangles,y,z):
    # Intersect the X ray with STL facets; compare the same Y/Z across versions.
    a=triangles[:,0];b=triangles[:,1]-a;c=triangles[:,2]-a
    det=b[:,1]*c[:,2]-b[:,2]*c[:,1];ok=np.abs(det)>1e-10
    a,b,c,det=a[ok],b[ok],c[ok],det[ok]
    dy,dz=y-a[:,1],z-a[:,2]
    u=(dy*c[:,2]-dz*c[:,1])/det;v=(b[:,1]*dz-b[:,2]*dy)/det
    ok=(u>=-1e-8)&(v>=-1e-8)&(u+v<=1+1e-8)
    x=a[:,0]+u*b[:,0]+v*c[:,0];assert np.any(ok),'section misses mesh'
    return float(x[ok].min())


def sections(exports,design):
    read=lambda p:json.loads(p.read_text(encoding='utf-8'))
    report=read(exports/'print-parts.json');d=read(design)
    assert not report['failures']
    body=next(p for p in report['parts'] if '/01 車体' in p['groupPath'])
    skirt=next(p for p in report['parts'] if p['id']==d['keyParts']['integratedFloorApron'][0])
    bm,sm=mesh(exports/body['stl']),mesh(exports/skirt['stl'])
    rows=[]
    for y in [-14.,-12.7,-10.,-6.,0.,6.,10.,12.7,14.]:
        bx,sx=front_section(bm,y,14.),front_section(sm,y,7.)
        rows.append(dict(yMm=y,bodyZMm=14.,skirtZMm=7.,bodyFrontXMm=bx,skirtFrontXMm=sx,setbackMm=sx-bx))
    return dict(reportSha256=sha(exports/'print-parts.json'),designSha256=sha(design),
        meshSha256={p['stl']:sha(exports/p['stl']) for p in [body,skirt]},sections=rows)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('baseline',type=Path);ap.add_argument('workspace',type=Path)
    ap.add_argument('--run',default='05');a=ap.parse_args();w=a.workspace
    old=sections(a.baseline/'exports04',a.baseline/'candidate04/design.json')
    new=sections(w/('exports'+a.run),w/('candidate'+a.run)/'design.json')
    assert min(c['setbackMm'] for c in old['sections'])< -1.,'baseline must expose forward skirt'
    assert min(c['setbackMm'] for c in new['sections'])>.3,'skirt still projects ahead of lower body'
    for before,after in zip(old['sections'],new['sections']):
        assert abs(after['skirtFrontXMm']-before['skirtFrontXMm']-2.)<.001
        assert abs(after['bodyFrontXMm']-before['bodyFrontXMm'])<.001
    result=dict(passed=True,scope='Nine comparable STL ray sections; section planes are deliberately different heights.',
        baseline=old,candidate=new,minimumSetbackMm=min(c['setbackMm'] for c in new['sections']),
        limitations='Photo-estimated setback, not measured real-car mounting geometry; STL chord error applies.')
    (w/'appearance-sections.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    print('PASS actual section setback',result['minimumSetbackMm'])


if __name__=='__main__':main()
