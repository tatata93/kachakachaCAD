"""Editable Taki1000 reference study, not a measured manufacturing replica.

The main shell has four matched lofts; small fittings remain exact solids.
Dimensional source and estimated details are recorded beside the native file.
"""
import argparse,json,math,zipfile
from pathlib import Path
from practical_vehicle_fixtures import tank
from practical_parts import box,extrude,circle,wheelsets,poly
from practical_fixtures import bezier

def rod(f,name,a,b,r=.4):
    d=tuple(q-p for p,q in zip(a,b));length=math.sqrt(sum(v*v for v in d))
    normal=tuple(v/length for v in d)
    xdir=(1,0,0) if abs(normal[0])<.9 else (0,1,0)
    return extrude(f,name,circle(a,r,normal,xdir),normal,length)

def end_cap(f,x,r,z):
    k=4/3*math.tan(math.pi/8);curves=[]
    for q in range(4):
        a=q*math.pi/2;b=a+math.pi/2
        p=(x,r*math.cos(a),z+r*math.sin(a));t=(x,r*math.cos(b),z+r*math.sin(b))
        v=(0,-r*math.sin(a),r*math.cos(a));w=(0,-r*math.sin(b),r*math.cos(b))
        curves.append(bezier([p,tuple(p[i]+k*v[i] for i in range(3)),tuple(t[i]-k*w[i] for i in range(3)),t]))
    extrude(f,f'tank end closure {x}',curves,(-1 if x>0 else 1,0,0),1.0)

def build(template,out):
    f,stations=tank(template,out)
    f.d['metadata']['title']='Taki1000 complete reference study 1:100'
    f.d['metadata']['description']=('JOT Taki1000 reference photograph, https://www.jot.co.jp/service/oil/; '
        'secondary overall dimensions13570x2960x3918mm, scale1:100. Main eccentric tank is a photo-estimated '
        'four-loft shell. End closures, frame, bogies, wheels, manhole, walkways, railings, ladder and discharge '
        'fittings are editable native solids. Detail dimensions are estimates, thin rails enlarged to0.8mm '
        'diameter for inspection. No lettering, exact brake piping or working mechanisms. Main shell is '
        'approximated as sheet panels; fittings retained exact. No measured-replica accuracy claim.')
    for x in (-58,58):end_cap(f,x,1.6,23)
    box(f,'tank center underframe',-64,-4,7.8,64,4,10.5)
    for y in (-10,8.8):box(f,'tank longitudinal frame '+str(y),-64,y,8.8,64,y+1.2,11)
    for x in (-46.85,46.85):
        box(f,'tank bogie bolster '+str(x),x-12,-9.2,5.6,x+12,9.2,9.5)
        # Saddle projects into the shell, so the printed assembly has a real joint.
        extrude(f,'tank saddle '+str(x),poly([(x-3,-10,10),(x-3,10,10),(x-3,8.5,16),(x-3,-8.5,16)]),(1,0,0),6)
    wheelsets(f,'tank',[-57.35,-36.35,36.35,57.35],radius=4.05,halftrack=8.5,wheelwidth=2.1)
    for x in (-64,58):
        box(f,'tank end platform '+str(x),x,-14.8,10.3,x+6,14.8,11.1)
        for dx in (0,6):
            for y in (-13.8,13.8):rod(f,'platform upright',(x+dx,y,10.7),(x+dx,y,21.6))
            rod(f,'platform transverse handrail',(x+dx,-13.8,21.6),(x+dx,13.8,21.6))
        for y in (-13.8,13.8):rod(f,'platform longitudinal handrail',(x,y,21.6),(x+6,y,21.6))
    for x in (-67.85,64):box(f,'tank coupler '+str(x),x,-1.8,7.5,x+3.85,1.8,10.5)
    # Loading hatch and narrow catwalk, supported rather than floating above tank.
    extrude(f,'tank loading neck',circle((0,0,33.9),3.2),(0,0,1),2.9)
    extrude(f,'tank manhole lid',circle((0,0,36.5),3.6),(0,0,1),.7)
    for y in (-6.6,4.8):box(f,'tank catwalk '+str(y),-30,y,34.6,30,y+1.8,35.4)
    for x in (-28,0,28):
        for y in (-5.7,5.7):
            rod(f,'catwalk support',(x,y,31.5),(x,y,35.0),.55)
            rod(f,'catwalk upright',(x,y,35.0),(x,y,38.78))
    for y in (-5.7,5.7):rod(f,'catwalk upper handrail',(-28,y,38.78),(28,y,38.78))
    for x in (-28,28):rod(f,'catwalk end handrail',(x,-5.7,38.78),(x,5.7,38.78))
    # Side ladder corresponds to the conspicuous ladder at the left of the photo.
    for x in (-31,-25):
        rod(f,'ladder stile',(x,-14.1,10.5),(x,-14.1,35.3))
        rod(f,'ladder top return',(x,-14.1,35.3),(x,-5.7,35.3))
        rod(f,'ladder lower bracket',(x,-14.1,10.5),(x,-9.3,10.5))
    for z in (12,15,18,21,24,27,30,33):rod(f,'ladder rung',(-31,-14.1,z),(-25,-14.1,z),.35)
    rod(f,'bottom discharge neck',(0,0,7.7),(0,0,6.0),1.5)
    rod(f,'bottom discharge pipe',(0,-7,6.0),(0,7,6.0),.8)
    f.save(out/'tank-complete-source.kcd2')
    (out/'tank-design-notes.json').write_text(json.dumps(dict(reference='https://www.jot.co.jp/service/oil/',
        scale=.01,overall_mm=[135.7,29.6,39.18],tank_stations_mm=stations,
        assumptions='All detailed section, fitting, chassis and ladder dimensions are photo estimates. Rail diameter enlarged to0.8mm; rung0.7mm. Bogie centers93.7mm, estimated wheelbase21mm.',
        status='Unvalidated source fixture: requires rebuild, thickness, joints, export and slicer checks.'),indent=2),encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--template',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    with zipfile.ZipFile(a.template) as z:template=json.loads(z.read('document.json'))
    out=Path(a.output);out.mkdir(parents=True,exist_ok=True);build(template,out)
