"""Reference-based, editable native CAD fixtures. Dimensions are mm.
Run from repository; --template is a deployed native sample used for schema defaults.
No generated mesh is used as the CAD source of truth.
"""
import argparse, copy, json, math, uuid, zipfile
from pathlib import Path

def uid(label): return str(uuid.uuid5(uuid.NAMESPACE_URL, 'kachacad-practical-20261011/'+label))
def xyz(v): return dict(zip(('x','y','z'),v))
def bezier(points): return {'type':'cubic_bezier','controlPoints':[xyz(v) for v in points]}

class Fixture:
    def __init__(self, template, name, description):
        self.d=copy.deepcopy(template); self.name=name; self.count=0
        for key in ('entities','features','groups','rootOrder','assets','referenceDimensions'): self.d[key]=[]
        self.d.update(documentId=uid(name),activeGroupId=None,uiState={},revision=1)
        self.d['metadata']={'title':name,'author':'CAD practical validation','description':description}
    def add(self,name,kind,typ,definition,inputs=(),key=None):
        self.count+=1; prefix=f'{self.name}/{self.count}'
        eid,fid=uid(prefix+'/entity'),uid(prefix+'/feature')
        e=dict(construction=False,createdBy=fid,datum=False,displayName=name,editPolicy='source' if kind=='wire' else 'derived',groupId=None,id=eid,kind=kind,partProperties=None,revision=0,visibility='visible')
        if kind=='part':e['partProperties']={'purpose':'finished_model','manufacturing':None}
        f=dict(definition=definition,derivedGroupId=None,displayName=name,enabled=True,id=fid,inputEntityIds=list(inputs),outputs=[dict(entityId=eid,key=key or kind,kind=kind)],revision=0,type=typ)
        self.d['entities'].append(e);self.d['features'].append(f);self.d['rootOrder'].append(eid)
        return eid
    def wire(self,name,segments):
        segments=copy.deepcopy(segments)
        for i,s in enumerate(segments):s['id']=uid(f'{self.name}/{self.count+1}/segment/{i}')
        eid=self.add(name,'wire','create_wire',dict(construction=False,sourcePlaneId=None,wire=dict(closed=False,segments=segments)))
        return eid,dict(reversed=[False]*len(segments),segments=[dict(range=dict(first=0,last=1),segmentId=s['id'],wireEntityId=eid) for s in segments])
    def revolve(self,name,profile,angle0,angle1):
        # Rotate the profile into each quadrant; explicit seams keep editable panels.
        c,s=math.cos(angle0),math.sin(angle0)
        segments=[]
        for points in profile:segments.append(bezier([(r*c,r*s,z) for r,z in points]))
        wire,chain=self.wire(name+' profile',segments)
        definition=dict(chains=[chain],roles=[2],method=7,offsetDistanceMm=0,revolveAxisPoint=xyz((0,0,0)),revolveAxisDirection=xyz((0,0,1)),revolveAngleRad=angle1-angle0,lockSectionOrder=True)
        return self.add(name,'guide_surface','create_guide_surface',definition,[wire],key='surface')
    def thicken(self,source,name,t=.8):
        return self.add(name,'part','thicken_surface',dict(surface=source,placement=2,thickness=dict(expression=str(t),quantity='length',value=t)),[source],key='part')
    def save(self,path):
        path.parent.mkdir(parents=True,exist_ok=True)
        with zipfile.ZipFile(path,'w',zipfile.ZIP_STORED) as z:z.writestr('document.json',json.dumps(self.d,ensure_ascii=False))

def line(a,b):return [a,(a[0]*2/3+b[0]/3,a[1]*2/3+b[1]/3),(a[0]/3+b[0]*2/3,a[1]/3+b[1]*2/3),b]
def aerosol(template,out):
    # KURE 5-56 320ml published envelope: 163 high x diameter 66.
    # 1:2 inspection model. Shoulder/rim stations are photo estimates, not factory dimensions.
    profile=[line((15.9,1),(16.5,1.6)),line((16.5,1.6),(16.5,3)),line((16.5,3),(16.2,3.3)),line((16.2,3.3),(16.2,63)),[(16.2,63),(16.2,66),(12.5,68.5),(11,69)],line((11,69),(10.6,70)),line((10.6,70),(10.6,71))]
    f=Fixture(template,'aerosol-556-body-half-scale','KURE https://www.kure.com/product/k1004/ published 163 x diameter 66 mm. Scale 1:2. Body shoulder and rim estimated from reference photo; cap/nozzle separate pending. Nominal wall 0.8mm; neutral-surface approximation excludes material compensation.')
    for q in range(4):f.revolve(f'body quadrant {q+1}',profile,q*math.pi/2,(q+1)*math.pi/2)
    f.save(out/'aerosol-body-surfaces.kcd2')
    for e in list(f.d['entities']):
        if e['kind']=='guide_surface':f.thicken(e['id'],e['displayName']+' ideal wall')
    f.save(out/'aerosol-body-ideal.kcd2')

def vase(template,out):
    # Met 24.85 / 447776: H330, maximum diameter160, model at 1:2.
    # Silhouette stations traced from the public-domain photograph. Perspective
    # and hand-made asymmetry prevent interpreting these as surveyed dimensions.
    # y is image downwards; r is averaged left/right silhouette distance in pixels.
    trace=[(1130,194),(1112,194),(1095,177),(1060,168),(1020,149),
           (994,125),(975,111),(952,107),(932,109),(922,121),(908,124),
           (891,111),(873,109),(830,132),(795,157),(750,188),
           (690,223),(620,253),(550,265),(490,254),(440,231),
           (407,203),(382,171),(367,123),(348,102),(310,92),
           (267,90),(222,99),(188,118),(165,140),(151,160),
           (128,165),(100,169),(79,166)]
    # Monotone cubic Hermite radii (no artificial overshoot at bulges/waists).
    stations=[(r*40/265,(1130-y)*165/1051) for y,r in trace]
    slopes=[(b[0]-a[0])/(b[1]-a[1]) for a,b in zip(stations,stations[1:])]
    tangents=[slopes[0]]+[0 if a*b<=0 else 2*a*b/(a+b) for a,b in zip(slopes,slopes[1:])]+[slopes[-1]]
    profile=[]
    for i,(a,b) in enumerate(zip(stations,stations[1:])):
        dz=(b[1]-a[1])/3
        profile.append([a,(a[0]+tangents[i]*dz,a[1]+dz),(b[0]-tangents[i+1]*dz,b[1]-dz),b])
    f=Fixture(template,'met-vase-24-85-half-scale','Metropolitan Museum 24.85, https://www.metmuseum.org/art/collection/search/447776 ; public domain photo. Published H330 x D160mm. 1:2 model; profile photo estimated; painted decoration omitted, not a scanned replica.')
    for q in range(4):f.revolve(f'vase quadrant {q+1}',profile,q*math.pi/2,(q+1)*math.pi/2)
    f.save(out/'vase-surfaces.kcd2')
    (out/'vase-profile.json').write_text(json.dumps({'source':'https://www.metmuseum.org/art/collection/search/447776','scale':.5,'height_mm':165,'diameter_mm':80,'profile_r_z':stations,'photo_trace_y_r':trace,'status':'photo-estimated, not factory or measured geometry'},indent=2),encoding='utf-8')

def controls(template,out):
    for name,profile in [('cylinder-control',[line((16,0),(16,70))]),
                         ('cone-control',[line((16,0),(8,70))])]:
        f=Fixture(template,name,'Analytical control specimen, dimensions in mm; no real-world replica claim.')
        f.revolve(name,profile,0,math.pi/2)
        f.save(out/(name+'-surfaces.kcd2'))

def aerosol_shell(template,out):
    # The folded rim is a separate manufactured feature. Do not run a 0.8mm
    # inward offset through its 0.3mm return: preserve that bad input separately.
    profile=[line((16.2,2.5),(16.2,62)),
             [(16.2,62),(16.2,66.0),(10.6,68.0),(10.6,71)]]
    for quadrants in (1,4):
        f=Fixture(template,f'aerosol-shell-{quadrants}-quadrants',
            'KURE 5-56 320ml, 1:2. Main shell only; rolled rim, bottom, valve and cap are separate parts pending. Smooth shoulder photo estimate, published complete envelope 163x66mm. This is not a complete can.')
        for q in range(quadrants):f.revolve(f'main shell quadrant {q+1}',profile,q*math.pi/2,(q+1)*math.pi/2)
        f.save(out/f'aerosol-shell-{quadrants}-surfaces.kcd2')

if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--template',required=True);a.add_argument('--output',default='practical-output');args=a.parse_args()
    with zipfile.ZipFile(args.template) as z:template=json.loads(z.read('document.json'))
    aerosol(template,Path(args.output))
    vase(template,Path(args.output))
    controls(template,Path(args.output))
    aerosol_shell(template,Path(args.output))
