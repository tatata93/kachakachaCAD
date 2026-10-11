"""Reread vase STL against the editable uniform spline, independent of OCCT.

Photo tracing uncertainty is separate from CAD/mesh deviation. Sampled normal
rays are not a global minimum-wall or exact self-intersection certificate.
"""
import argparse,json,zipfile
from pathlib import Path
import numpy as np
import trimesh
from scipy.interpolate import BSpline
from PIL import Image,ImageDraw,ImageFont

p=argparse.ArgumentParser();p.add_argument('--source',required=True)
p.add_argument('--ideal',required=True);p.add_argument('--approx',required=True);p.add_argument('--output',required=True);a=p.parse_args()
with zipfile.ZipFile(a.source) as z:doc=json.loads(z.read('document.json'))
f=next(f for f in doc['features'] if f['type']=='create_wire' and f['definition']['wire']['segments'][0]['type']=='cubic_b_spline')
points=np.array([[p['x'],p['z']] for p in f['definition']['wire']['segments'][0]['controlPoints']]);n=len(points)
curve=BSpline(np.arange(-3,n+1),points,3);derivative=curve.derivative()
# Avoid the separately modeled base and the open-mouth end cap in wall probes.
ts=np.linspace((n-3)*3/165,(n-3)*162/165,240);rz=curve(ts);d=derivative(ts)
# An odd, phase-shifted count avoids sampling only the 32 panel boundaries.
angles=(np.arange(37)+.37)*2*np.pi/37;directions=[];origins=[];references=[];normals=[]
for (r,z),(dr,dz) in zip(rz,d):
    for angle in angles:
        c,s=np.cos(angle),np.sin(angle);speed=np.hypot(dr,dz)
        directions.append([c,s,0]);origins.append([0,0,z]);references.append([r*c,r*s,z]);normals.append([dz*c/speed,dz*s/speed,-dr/speed])
origins=np.array(origins);directions=np.array(directions);references=np.array(references);normals=np.array(normals)
report={'source':str(Path(a.source).resolve()),'reference':'Editable spline fitted to a public-domain photograph trace, not a factory drawing.',
    'sampled_heights_mm':[3,162],'heights':240,'angles':37,'angular_phase_turns':.37/37,'nominal_wall_mm':.8,'models':{}}
sections={}
def ray_hits(mesh,origins,directions):
    # Keep candidate triangle arrays bounded on a 16 GB workstation. All rays
    # are retained; batching changes memory use, not the numerical test.
    locations=[];rays=[]
    for first in range(0,len(origins),32):
        h,r,_=mesh.ray.intersects_location(origins[first:first+32],directions[first:first+32],multiple_hits=True)
        locations.append(h);rays.append(r+first)
    return np.concatenate(locations),np.concatenate(rays)
for name,file in [('ideal',a.ideal),('approximation',a.approx)]:
    print('Ray comparison:',name,flush=True)
    mesh=trimesh.load_mesh(file,process=True);h,r=ray_hits(mesh,origins,directions)
    dist=np.sum((h-origins[r])*directions[r],axis=1);expected=np.linalg.norm(references-origins,axis=1)
    errors=[];missing=[]
    for i in range(len(origins)):
        values=dist[r==i];candidates=values[np.abs(values-expected[i])<.6]
        if len(candidates):errors.append(float(candidates.max()-expected[i]))
        else:missing.append(i)
    starts=references+normals*.5;h,r=ray_hits(mesh,starts,-normals)
    dist=np.sum((starts[r]-h)*normals[r],axis=1);wall=[];ambiguous=[]
    for i in range(len(starts)):
        values=np.sort(dist[(r==i)&(dist>=0)&(dist<2.0)])
        if len(values):values=values[np.r_[True,np.diff(values)>1e-5]]
        if len(values)==2:wall.append(float(values[1]-values[0]))
        else:ambiguous.append(i)
    report['models'][name]={'file':str(Path(file).resolve()),'radial_error_mm':{'max_abs':max(map(abs,errors)),'rms':float(np.sqrt(np.mean(np.square(errors)))),'min':min(errors),'max':max(errors)},
        'missing_rays':missing,'normal_wall_mm':{'samples':len(wall),'min':min(wall) if wall else None,'max':max(wall) if wall else None,'ambiguous':len(ambiguous)},
        'scope':'Sampled profile/normal-wall check. Run mesh topology and crossing audits separately.'}
    sections[name]=trimesh.intersections.mesh_plane(mesh,[0,1,0],[0,0,0])
im=Image.new('RGB',(1000,1040),'#f3f6fa');draw=ImageDraw.Draw(im);font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',23)
draw.text((25,20),'Vase: native spline and reread STL section',fill='#182b43',font=font)
draw.rectangle((220,70,780,930),fill='white',outline='#bcc7d2')
project=lambda x,z:(500+x*5,915-z*5)
for name,color in [('ideal','#1673b1'),('approximation','#dc7422')]:
    for line in sections[name]:draw.line([project(v[0],v[2]) for v in line],fill=color,width=1)
for r,z in curve(np.linspace(0,n-3,800)):
    x,y=project(r,z);draw.ellipse((x-1,y-1,x+1,y+1),fill='#222222')
draw.text((25,955),'Black: editable photo trace. Blue: ideal. Orange: approximation.',fill='#182b43',font=font)
draw.text((25,990),'Image silhouette uncertainty is separate from numerical CAD errors.',fill='#495969',font=font)
out=Path(a.output);out.mkdir(parents=True,exist_ok=True);im.save(out/'sections.png')
(out/'sections.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report['models'],indent=2))
