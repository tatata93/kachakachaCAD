"""N700 leading car, editable 1:150 reference study. Detail dimensions are estimates."""
import argparse,json,zipfile
from pathlib import Path
from practical_fixtures import Fixture
from practical_vehicle_fixtures import loft
from practical_parts import plane,poly,reverse,rounded_rect,box,extrude,circle,wheelsets,line3

def section(x,r,bottom,shoulder,top):
    belt=bottom+.34*(shoulder-bottom)
    def line(a,b):return [a,tuple((2*a[i]+b[i])/3 for i in range(3)),tuple((a[i]+2*b[i])/3 for i in range(3)),b]
    return [[(x,-.82*r,bottom),(x,-r,bottom),(x,-r,.7*belt+.3*bottom),(x,-r,belt)],
        line((x,-r,belt),(x,-r,shoulder)),
        [(x,-r,shoulder),(x,-r,top),(x,-.55*r,top),(x,0,top)],
        [(x,0,top),(x,.55*r,top),(x,r,top),(x,r,shoulder)],
        line((x,r,shoulder),(x,r,belt)),
        [(x,r,belt),(x,r,.7*belt+.3*bottom),(x,r,bottom),(x,.82*r,bottom)]]

def subcurve(p,a,b):
    def split(p,t):
        levels=[p]
        while len(levels[-1])>1:
            levels.append([tuple((1-t)*u[i]+t*v[i] for i in range(3)) for u,v in zip(levels[-1],levels[-1][1:])])
        return [s[0] for s in levels],[s[-1] for s in reversed(levels)]
    left,_=split(p,b);_,part=split(left,a/b);return part

def build(template,out):
    length=27100/150;r=11.2;bottom=5.4;shoulder=17.8;top=24.;belt=bottom+.34*(shoulder-bottom)
    stations=[(0,1.6,3.0,3.8,4.5),(3,3.1,3.0,4.1,5.0),(7,4.8,3.1,4.7,6.0),
        (13,6.8,3.3,6.1,8.0),(21,8.5,3.6,8.4,11.5),(31,9.8,4.0,11.7,16.5),
        (42,10.7,4.6,15.0,21.2),(54,11.15,5.2,17.3,23.7),(64,r,bottom,shoulder,top)]
    f=Fixture(template,'N700-leading-car-1-150',
        'Nippon Sharyo N700 reference photo and published27100x3360x3600mm envelope; 1:150. '
        'Duckbill sections, windows, cab glazing, doors, fairings and running gear are photo estimates, not factory CAD. '
        'Rail datum assumed z0; local profile uses maxz24. Cab glazing is raised representational relief; window apertures in straight side sheets are real openings. '
        'No source photograph is redistributed. Small details enlarged where stated in design notes.')
    nose=[section(*s) for s in stations]
    reference_guides=[]
    for strip in range(6):reference_guides.append(loft(f,f'N700 nose strip {strip+1}',[[s[strip]] for s in nose]))
    from practical_fixtures import bezier
    from practical_complete_fixtures import closed_wire
    profiles=[closed_wire(f,f'N700 closed nose section{i+1}',[bezier(p) for p in s]+[line3(s[-1][-1],s[0][0])]) for i,s in enumerate(nose)]
    f.add('N700 printed nose - closed section loft','part','create_solid',dict(method=1,profiles=profiles,axis=None,path=[],
        angleRad=6.283185307179586,symmetric=False,booleanMode=0,targets=[]),profiles,key='part')
    regular=[section(x,r,bottom,shoulder,top) for x in (64,100,140,length)]
    for strip in (0,2,3,5):loft(f,f'N700 straight body strip {strip+1}',[[s[strip]] for s in regular])
    for side in (-1,1):
        at=lambda x,z:(x,side*r,z)
        boundary=poly([at(64,belt),at(length,belt),at(length,shoulder),at(64,shoulder)])
        windows=[rounded_rect(x,12.7,x+3.9,16.8,.65,at) for x in (79,85.5,92,98.5,105,111.5,118,124.5,131,137.5,144,150.5,157)]
        for x in (69,171.5):windows.append(rounded_rect(x,12.6,x+2.3,16.7,.4,at))
        plane(f,f'N700 side{side} with15 glazing openings',reverse(boundary) if side==1 else boundary,windows)
        for x in (67.6,170.1):
            extrude(f,f'N700 side{side} passenger door{x}',rounded_rect(x,belt+.2,x+5.1,17.25,.5,at),(0,side,0),.12,
                [rounded_rect(x+1.4,12.6,x+3.7,16.7,.4,at)])
        # A small service door beside the cab, represented by a perimeter wire.
        f.wire(f'N700 side{side} cab access seam',rounded_rect(59,9.6,63,16.9,.4,lambda x,z:(x,side*11.1,z)))
    # Close front/rear cross sections while preserving the actual roof profile.
    from practical_fixtures import bezier
    for x,s,label in ((0,nose[0],'nose tip'),(length,regular[-1],'rear end')):
        boundary=[bezier(p) for p in s]+[line3(s[-1][-1],s[0][0])]
        if x==length:boundary=reverse(boundary)
        holes=[] if x==0 else [rounded_rect(-2,10,2,17,.5,lambda y,z:(x,y,z))]
        guide=plane(f,'N700 '+label,boundary,holes)
        if x==0:reference_guides.append(guide)
    # Two curved cab glazing patches; source roof stations retained, relief .20mm.
    for strip in (2,3):
        patches=[]
        for s in (nose[-3],nose[-2]):
            p=subcurve(s[strip],.15,.88)
            patches.append([[tuple(v[i]+(.2 if i==2 else 0) for i in range(3)) for v in p]])
        loft(f,f'N700 cab glazing relief {strip}',patches)
    box(f,'N700 main floor',58,-9.2,5.2,length,9.2,6.2)
    # Nose belly joins the narrowing cross sections through a ruled underside.
    belly=[]
    for s in nose:
        a,b=s[-1][-1],s[0][0]
        belly.append([[a,tuple((2*a[i]+b[i])/3 for i in range(3)),tuple((a[i]+2*b[i])/3 for i in range(3)),b]])
    reference_guides.append(loft(f,'N700 nose underside',belly))
    bogies=(45.,155.)
    for x in bogies:box(f,f'N700 bogie bolster{x}',x-10,-7,3.9,x+10,7,5.65)
    wheelsets(f,'N700',[x+d for x in bogies for d in (-2500/300,2500/300)],radius=860/300,halftrack=4.6,wheelwidth=1.1)
    for x in (80,106,132):box(f,f'N700 underfloor module{x}',x,-8,4.7,x+18,8,5.6)
    # Rear gangway and coupler use native extrusions, not a decorative mesh.
    extrude(f,'N700 rear gangway frame',rounded_rect(-2.7,8.5,2.7,18.5,.5,lambda y,z:(length,y,z)),(-1,0,0),.4,
        [rounded_rect(-2,10,2,17,.5,lambda y,z:(length,y,z))])
    box(f,'N700 rear coupler',length-2,-1.1,3.6,length,1.1,5.3)
    for entity in f.d['entities']:
        if entity['id'] in reference_guides:entity.update(construction=True,visibility='hidden')
    f.save(out/'n700-complete-source.kcd2')
    (out/'n700-design-notes.json').write_text(json.dumps(dict(
        reference='https://www.n-sharyo.co.jp/business/tetsudo/pages/jrcn700.htm',scale=1/150,
        body_length_mm=length,width_mm=2*r,height_mm=top,door_relief_mm=.12,cab_glazing_relief_mm=.2,
        side_openings=30,wheel_diameter_mm=860/150,bogie_wheelbase_mm=2500/150,
        sections_mm=stations,photo_estimated=True,manufacturing='The nose tip curvature cannot accept a uniform0.8mm inward sheet offset. Its closed section loft is retained as an exact solid for printing; hidden construction guides retain its surface definitions. Straight body sheets and roof are the manufacturing approximation targets.',
        limitations='No factory section data. Body datum assumed at rail z0. Cab glazing relief is representational; evaluate seam overlap and printed detail before acceptance.'),indent=2),encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--template',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    with zipfile.ZipFile(a.template) as z:template=json.loads(z.read('document.json'))
    out=Path(a.output);out.mkdir(parents=True,exist_ok=True);build(template,out)
