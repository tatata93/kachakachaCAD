"""Editable vehicle surface prototypes with explicitly estimated photo sections.
These are intermediate test fixtures, not certified full vehicle replicas.
"""
import argparse,json,math,zipfile
from pathlib import Path
from practical_fixtures import Fixture,bezier,xyz

def loft(f,name,sections):
    wires=[];chains=[]
    for i,curves in enumerate(sections):
        # Loft U follows the section and V the vehicle axis. Reverse section
        # traversal so U cross V points out of the vehicle shell.
        wire,chain=f.wire(f'{name} section {i+1}',[bezier(list(reversed(c))) for c in reversed(curves)])
        wires.append(wire);chains.append(chain)
    return f.add(name,'guide_surface','create_guide_surface',dict(chains=chains,roles=[2]*len(chains),method=2,offsetDistanceMm=0,lockSectionOrder=True),wires,key='surface')

def car_section(x,r,bottom,shoulder,top):
    # Rounded upper body and inward lower skirt, with consistent tangent directions.
    return [[(x,-.82*r,bottom),(x,-r,bottom+.25*(shoulder-bottom)),(x,-r,shoulder-.3*(shoulder-bottom)),(x,-r,shoulder)],
            [(x,-r,shoulder),(x,-r,top),(x,-.55*r,top),(x,0,top)],
            [(x,0,top),(x,.55*r,top),(x,r,top),(x,r,shoulder)],
            [(x,r,shoulder),(x,r,shoulder-.3*(shoulder-bottom)),(x,r,bottom+.25*(shoulder-bottom)),(x,.82*r,bottom)]]

def n700(template,out):
    # Envelope from Nippon Sharyo, scale 1:150: body 27100x3360x3600 mm.
    # Nose section positions/shape estimated from manufacturer photograph.
    # Keep this uncertainty explicit; windows, seams, bogies and end walls follow.
    stations=[(0,1.6,2.7,3.8,4.5),(3,3.1,2.0,4.1,5.0),(7,4.8,1.1,4.7,6.0),
              (13,6.8,.4,6.1,8.0),(21,8.5,0,8.4,11.5),(31,9.8,0,11.7,16.5),
              (42,10.7,0,15.0,21.2),(54,11.15,0,17.3,23.7),(64,11.2,0,17.8,24),
              (76,11.2,0,17.8,24),(100,11.2,0,17.8,24),(140,11.2,0,17.8,24),(27100/150,11.2,0,17.8,24)]
    f=Fixture(template,'N700-leading-car-body-1-150-prototype','Nippon Sharyo N700 body envelope 27100 x 3360 x 3600mm, scale1:150. Nose sections photo-estimated, not factory CAD. Body prototype only; windows/end walls/chassis still pending.')
    # Separate faces at the shoulder and crown, retaining common section vertices.
    sections=[car_section(*s) for s in stations]
    for strip in range(4):loft(f,f'N700 body strip {strip+1}',[[s[strip]] for s in sections])
    f.save(out/'n700-body-surfaces.kcd2')
    (out/'n700-sections.json').write_text(json.dumps(dict(source='https://www.n-sharyo.co.jp/business/tetsudo/pages/jrcn700.htm',scale=1/150,columns=['x','half_width','bottom_z','shoulder_z','roof_z'],sections_mm=stations,uncertainty='Photo-estimated stations; only envelope dimensions are published'),indent=2),encoding='utf-8')

def tank(template,out):
    # Taki1000 overall envelope is 13570 x2960 x3918mm (secondary reference).
    # Tank body sections are photo estimates. The lower middle bulge is retained;
    # a constant-radius capsule would miss that conspicuous shape feature.
    stations=[(-58,1.6,1.6,23),(-56,7.5,7.0,23),(-52,12.6,10.8,23),
              (-45,14.0,11.8,22.8),(-33,14.0,13.0,21.6),(-18,14.0,13.5,21.1),
              (0,14.0,13.5,21.1),(18,14.0,13.5,21.1),(33,14.0,13.0,21.6),
              (45,14.0,11.8,22.8),(52,12.6,10.8,23),(56,7.5,7.0,23),(58,1.6,1.6,23)]
    f=Fixture(template,'Taki1000-tank-body-1-100-prototype','JOT Taki1000 photo with secondary overall dimensions 13570x2960x3918mm; scale1:100. Eccentric elliptical tank sections photo-estimated. End plugs, chassis, bogies, ladders, platform and railings pending.')
    k=4/3*math.tan(math.pi/8)
    for q in range(4):
        angle=-q*math.pi/2;end=angle-math.pi/2;sections=[]
        for x,ry,rz,cz in stations:
            p=(x,ry*math.cos(angle),cz+rz*math.sin(angle));v=(0,-ry*math.sin(angle),rz*math.cos(angle))
            t=(x,ry*math.cos(end),cz+rz*math.sin(end));w=(0,-ry*math.sin(end),rz*math.cos(end))
            sections.append([[p,tuple(p[i]-k*v[i] for i in range(3)),tuple(t[i]+k*w[i] for i in range(3)),t]])
        loft(f,f'tank quadrant {q+1}',sections)
    f.save(out/'tank-body-surfaces.kcd2')
    (out/'tank-sections.json').write_text(json.dumps(dict(source='https://www.jot.co.jp/service/oil/',scale=.01,columns=['x','radius_y','radius_z','center_z'],sections_mm=stations,uncertainty='Photo-estimated sections, not a measured replica'),indent=2),encoding='utf-8')
    return f,stations

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--template',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    with zipfile.ZipFile(a.template) as z:template=json.loads(z.read('document.json'))
    out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
    n700(template,out);tank(template,out)
