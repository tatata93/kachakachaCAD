"""Independent STL sections against the editable can source, not a physical scan."""
import argparse,json,zipfile
from pathlib import Path
import numpy as np
import trimesh
from PIL import Image,ImageDraw,ImageFont
p=argparse.ArgumentParser();p.add_argument('--source',required=True);p.add_argument('--ideal',required=True);p.add_argument('--approx',required=True);p.add_argument('--output',required=True);a=p.parse_args()
out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(a.source) as z:doc=json.loads(z.read('document.json'))
wire=next(f for f in doc['features'] if f['type']=='create_wire' and f['displayName'].startswith('main shell'))
curves=[np.array([[v['x'],v['z']] for v in s['controlPoints']]) for s in wire['definition']['wire']['segments']]
def value(c,t):return (1-t)**3*c[0]+3*t*(1-t)**2*c[1]+3*t*t*(1-t)*c[2]+t**3*c[3]
def derivative(c,t):return 3*((1-t)**2*(c[1]-c[0])+2*t*(1-t)*(c[2]-c[1])+t*t*(c[3]-c[2]))
def source(z):
    c=next(c for c in curves if c[0,1]<=z<=c[-1,1]);lo,hi=0.,1.
    for _ in range(50):
        t=(lo+hi)/2
        if value(c,t)[1]<z:lo=t
        else:hi=t
    v=value(c,t);d=derivative(c,t);n=np.array([d[1],-d[0]]);n/=np.linalg.norm(n)
    return v[0],n
levels=np.r_[np.linspace(6,54,13),np.linspace(55,62.5,16)]
angles=np.linspace(0,2*np.pi,72,endpoint=False)+.013
origins=[];directions=[];refs=[];normals=[]
for z in levels:
    radius,n=source(z)
    for angle in angles:
        c,s=np.cos(angle),np.sin(angle);q=np.array([radius*c,radius*s,z]);normal=np.array([n[0]*c,n[0]*s,n[1]])
        origins.append([0,0,z]);directions.append([c,s,0]);refs.append(q);normals.append(normal)
origins=np.array(origins);directions=np.array(directions);refs=np.array(refs);normals=np.array(normals)
report={'reference':'Editable native CAD outer profile; published product diameter sets scale. Concealed shoulder is illustrative, not measured physical truth.',
    'source':a.source,'samples':len(refs),'z_range_mm':[float(levels[0]),float(levels[-1])],'nominal_wall_mm':.8,'models':{}}
profiles={}
for label,path in [('ideal',a.ideal),('approximation',a.approx)]:
    mesh=trimesh.load_mesh(path,process=True)
    hits,ray,_=mesh.ray.intersects_location(origins,directions,multiple_hits=True)
    radius=np.linalg.norm(hits[:,:2],axis=1);outer=np.full(len(refs),np.nan)
    for i in range(len(refs)):
        values=radius[ray==i]
        if len(values):outer[i]=values.max()
    error=outer-np.linalg.norm(refs[:,:2],axis=1)
    start=refs+normals*.12
    h,r,_=mesh.ray.intersects_location(start,-normals,multiple_hits=True)
    distance=np.sum((start[r]-h)*normals[r],axis=1);wall=[];missing=[]
    for i in range(len(refs)):
        v=np.sort(distance[(r==i)&(distance>=0)&(distance<2)])
        if len(v):v=v[np.r_[True,np.diff(v)>1e-5]]
        if len(v)==2:wall.append(float(v[1]-v[0]))
        else:missing.append({'sample':i,'crossings':v.tolist()})
    report['models'][label]={'file':str(Path(path).resolve()),'radial_missing':int(np.isnan(error).sum()),
        'outer_radial_error_mm':{'max_absolute':float(np.nanmax(np.abs(error))),'rms':float(np.sqrt(np.nanmean(error**2))),'min_signed':float(np.nanmin(error)),'max_signed':float(np.nanmax(error))},
        'source_normal_wall_mm':{'samples':len(wall),'min':min(wall),'max':max(wall),'rms_error':float(np.sqrt(np.mean((np.array(wall)-.8)**2)))},
        'ambiguous_normal_rays':missing}
    profiles[label]=outer.reshape(len(levels),len(angles))
(out/'sections.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
im=Image.new('RGB',(1400,800),'#f3f6fa');d=ImageDraw.Draw(im);font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',24);small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',18)
d.text((35,20),'Can STL roundtrip: measured sections and wall thickness',fill='#182b43',font=font)
for panel,index in enumerate((6,17,26)):
    z=levels[index];cx=235+panel*460;cy=310;scale=9.5;radius,_=source(z)
    d.rectangle((cx-200,80,cx+200,525),fill='white',outline='#bdcad8')
    d.text((cx-170,100),f'Horizontal section Z = {z:.2f} mm',fill='#182b43',font=small)
    d.ellipse((cx-radius*scale,cy-radius*scale,cx+radius*scale,cy+radius*scale),outline='#b8c0ca',width=4)
    for label,color in [('ideal','#1468b3'),('approximation','#da6d1b')]:
        points=[(cx+r*np.cos(t)*scale,cy-r*np.sin(t)*scale) for r,t in zip(profiles[label][index],angles)]
        d.line(points+[points[0]],fill=color,width=2)
for i,(label,facts) in enumerate(report['models'].items()):
    er=facts['outer_radial_error_mm'];w=facts['source_normal_wall_mm']
    d.text((40,560+70*i),f"{label}: outer max {er['max_absolute']:.6f} mm, RMS {er['rms']:.6f} mm",fill='#182b43',font=font)
    d.text((40,592+70*i),f"Wall {w['min']:.6f} to {w['max']:.6f} mm / {w['samples']} normal rays; ambiguous {len(facts['ambiguous_normal_rays'])}",fill='#182b43',font=small)
d.text((40,730),'Grey: native CAD profile. Blue: ideal STL. Orange: panel assembly STL. Not a scan-to-object accuracy claim.',fill='#46546d',font=small)
im.save(out/'sections.png');print(json.dumps(report['models'],indent=2))
