"""Independent STL rays against the photo-estimated editable tank sections.

Ideal input can contain individual overlapping details: this audit does not
certify their Boolean union. Normal-wall checks use the constant central body.
"""
import argparse,json,math
from pathlib import Path
import numpy as np
import trimesh
from PIL import Image,ImageDraw,ImageFont

p=argparse.ArgumentParser();p.add_argument('--notes',required=True);p.add_argument('--ideal',required=True)
p.add_argument('--approx');p.add_argument('--output',required=True);a=p.parse_args()
out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
stations=json.loads(Path(a.notes).read_text())['tank_stations_mm'];k=4/3*math.tan(math.pi/8)
references=[];directions=[];origins=[];central=[];normals=[]
for x,ry,rz,cz in stations:
    for q in range(4):
        start=-q*math.pi/2;end=start-math.pi/2
        point=lambda t:np.array([ry*np.cos(t),cz+rz*np.sin(t)])
        tangent=lambda t:np.array([-ry*np.sin(t),rz*np.cos(t)])
        c=np.array([point(start),point(start)-k*tangent(start),point(end)+k*tangent(end),point(end)])
        for t in np.linspace(.035,.965,24):
            value=(1-t)**3*c[0]+3*t*(1-t)**2*c[1]+3*t*t*(1-t)*c[2]+t**3*c[3]
            derivative=3*((1-t)**2*(c[1]-c[0])+2*t*(1-t)*(c[2]-c[1])+t*t*(c[3]-c[2]))
            n=np.array([-derivative[1],derivative[0]]);n/=np.linalg.norm(n)
            direction=value-[0,cz];direction/=np.linalg.norm(direction)
            references.append([x,*value]);directions.append([0,*direction]);origins.append([x,0,cz]);normals.append([0,*n]);central.append(abs(x)<=18)
references=np.array(references);directions=np.array(directions);origins=np.array(origins);normals=np.array(normals);central=np.array(central)
report={'reference':'Photo-estimated native cubic Bezier sections, not measured physical tank geometry.',
    'reference_samples':len(references),'nominal_wall_mm':.8,'models':{}}
sections={}
for name,file in [('ideal_individual_parts',a.ideal)]+([('approximation',a.approx)] if a.approx else []):
    mesh=trimesh.load_mesh(file,process=True);h,r,_=mesh.ray.intersects_location(origins,directions,multiple_hits=True)
    distance=np.sum((h-origins[r])*directions[r],axis=1)
    expected=np.linalg.norm(references-origins,axis=1);errors=[];missing=[]
    for i in range(len(references)):
        values=distance[r==i];candidates=values[np.abs(values-expected[i])<.5]
        if len(candidates):errors.append(float(candidates.max()-expected[i]))
        else:missing.append(i)
    wall=[];ambiguous=[];points=references[central];ns=normals[central];starts=points+ns*.35
    h,r,_=mesh.ray.intersects_location(starts,-ns,multiple_hits=True);distance=np.sum((starts[r]-h)*ns[r],axis=1)
    for i in range(len(points)):
        values=np.sort(distance[(r==i)&(distance>=0)&(distance<1.6)])
        if len(values):values=values[np.r_[True,np.diff(values)>1e-5]]
        if len(values)==2:wall.append(float(values[1]-values[0]))
        else:ambiguous.append(i)
    report['models'][name]={'file':str(Path(file).resolve()),'outer_radial_error_mm':{'max_abs':max(map(abs,errors)),
        'rms':float(np.sqrt(np.mean(np.square(errors)))),'min':min(errors),'max':max(errors)},'missing_reference_rays':missing,
        'central_source_normal_wall_mm':{'unambiguous_samples':len(wall),'min':min(wall) if wall else None,'max':max(wall) if wall else None,
            'ambiguous_samples':len(ambiguous),'scope':'Central constant section only; fittings can add intersections. Not a global minimum-wall certificate.'}}
    sections[name]=[trimesh.intersections.mesh_plane(mesh,[1,0,0],[x,0,0]) for x in (-52,0,52)]
im=Image.new('RGB',(1380,600),'#f3f6fa');draw=ImageDraw.Draw(im);font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',23)
draw.text((25,18),'Tank reference sections and STL reread',fill='#182b43',font=font)
colors={'ideal_individual_parts':'#1673b1','approximation':'#dc7422'}
for j,x in enumerate((-52,0,52)):
    cx=230+j*455;cy=450;scale=9
    draw.rectangle((cx-210,70,cx+210,510),fill='white',outline='#bcc7d2')
    draw.text((cx-180,85),f'X = {x} mm',fill='#182b43',font=font)
    for name,all_lines in sections.items():
        for line in all_lines[j]:draw.line([(cx+v[1]*scale,cy-(v[2]-5)*scale) for v in line],fill=colors[name],width=1)
    pts=references[np.isclose(references[:,0],x)]
    for v in pts:draw.ellipse((cx+v[1]*scale-1,cy-(v[2]-5)*scale-1,cx+v[1]*scale+1,cy-(v[2]-5)*scale+1),fill='#222222')
draw.text((25,535),'Black: native section samples. Blue: ideal parts. Orange: approximation.',fill='#182b43',font=font)
draw.text((25,565),'Fittings and underframe remain visible. Photo-estimated dimensions, not a factory drawing.',fill='#495969',font=font)
im.save(out/'sections.png');(out/'sections.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report['models'],indent=2))
