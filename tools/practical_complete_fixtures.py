"""Reference-based complete sample construction from editable wires/features.
Small fittings stay exact solids; the main shell supplies the sheet approximation.
"""
import argparse,json,math,zipfile
from pathlib import Path
from practical_fixtures import Fixture,xyz,bezier,line

def closed_wire(f,name,segments):
    eid,chain=f.wire(name,segments)
    f.d['features'][-1]['definition']['wire']['closed']=True
    return eid

def line3(a,b):return dict(type='line',start=xyz(a),end=xyz(b))

def revolved_solid(f,name,profile,center=(0,0,0)):
    cx,cy,cz=center
    axis,_=f.wire(name+' axis',[line3((cx,cy,cz),(cx,cy,cz+100))])
    segments=[bezier([(cx+r,cy,cz+z) for r,z in p]) for p in profile]
    wire=closed_wire(f,name+' closed section',segments)
    return f.add(name,'part','create_solid',dict(method=0,profiles=[wire],axis=axis,path=[],angleRad=2*math.pi,symmetric=False,booleanMode=0,targets=[]),[wire,axis],key='part')

def ring(f,name,r,z,minor):
    # Exact circle swept about the can axis gives the rolled bead, without an
    # impossible inward offset through a tiny bend radius.
    axis,_=f.wire(name+' axis',[line3((0,0,0),(0,0,100))])
    wire=closed_wire(f,name+' section',[dict(type='circle',center=xyz((r,0,z)),normal=xyz((0,-1,0)),xDirection=xyz((1,0,0)),radiusMm=minor)])
    return f.add(name,'part','create_solid',dict(method=0,profiles=[wire],axis=axis,path=[],angleRad=2*math.pi,symmetric=False,booleanMode=0,targets=[]),[wire,axis],key='part')

def polygon_profile(points):return [line(a,b) for a,b in zip(points,points[1:]+points[:1])]

def aerosol(template,out):
    f=Fixture(template,'KURE-556-complete-half-scale',
        'KURE 5-56 320ml https://www.kure.com/product/k1004/ published capped envelope H163 D66mm; scale1:2. '
        'Photo-estimated cap/body ratio. Concealed shoulder, bottom, valve and nozzle are illustrative, not measured factory geometry. '
        'Cap is shown exploded +45mm X for inspection/printing; assembled top81.5mm. Main shell0.8mm, cap0.8mm. '
        'Rolled beads are solid circular sections to preserve small detail without a self-intersecting wall offset. '
        'Sheet approximation applies to main shell; fittings are retained exact and verified separately.')
    profile=[line((16.2,2.5),(16.2,55)),[(16.2,55),(16.2,59),(10.6,61),(10.6,64)]]
    f.revolve('main shell - full circumference',profile,0,2*math.pi)
    ring(f,'lower rolled rim',15.85,1.1,.65)
    # A dished bottom and short skirt meet both the bead and the main shell.
    bottom=[line((0,1.25),(12.5,1.25)),[(12.5,1.25),(14,1.25),(14.8,.6),(15.4,.6)],
            line((15.4,.6),(16.1,.6)),line((16.1,.6),(16.1,3)),line((16.1,3),(15.3,3)),
            line((15.3,3),(15.3,1.5)),[(15.3,1.5),(14.6,1.5),(14,2.05),(12.5,2.05)],
            line((12.5,2.05),(0,2.05)),line((0,2.05),(0,1.25))]
    revolved_solid(f,'dished bottom and seam skirt',bottom)
    ring(f,'valve mounting bead',10.25,64,.65)
    revolved_solid(f,'valve cup',polygon_profile([(0,63.6),(10.6,63.6),(10.6,64.4),(3.1,64.4),(3.1,66),(0,66)]))
    # Nozzle actuator has a central stem; output bore is modeled by Boolean cut.
    nozzle=revolved_solid(f,'nozzle actuator',polygon_profile([(0,65.5),(2.8,65.5),(2.8,69),(5,69),(5,71.5),(0,71.5)]))
    hole=closed_wire(f,'nozzle spray hole',[dict(type='circle',center=xyz((0,-5.5,70.3)),normal=xyz((0,1,0)),xDirection=xyz((1,0,0)),radiusMm=.55)])
    drill=f.add('nozzle bore cutter','part','extrude',dict(profiles=[hole],direction=xyz((0,1,0)),distance=dict(expression='3',quantity='length',value=3),extentMode=0,booleanMode=0,targets=[]),[hole],key='part')
    f.add('nozzle actuator with spray bore','part','boolean',dict(mode=1,targets=[nozzle],tools=[drill]),[nozzle,drill],key='part')
    for e in f.d['entities']:
        if e['id'] in (nozzle,drill):e['visibility']='hidden'
    # Closed cap section with rounded crown, separate from the printable body.
    cap=[line((15.7,58),(16.5,58)),line((16.5,58),(16.5,79.8)),
         [(16.5,79.8),(16.5,80.8),(15.8,81.5),(14.8,81.5)],line((14.8,81.5),(0,81.5)),
         line((0,81.5),(0,80.7)),line((0,80.7),(14.8,80.7)),
         [(14.8,80.7),(15.3,80.7),(15.7,80.3),(15.7,79.8)],line((15.7,79.8),(15.7,58))]
    revolved_solid(f,'[separate] removable rounded cap',cap,(45,0,0))
    f.save(out/'aerosol-complete-source.kcd2')
    (out/'aerosol-design-notes.json').write_text(json.dumps(dict(source='https://www.kure.com/product/k1004/',
        scale=.5,assembled_envelope_mm=[33,33,81.5],shell_thickness_mm=.8,
        cap_exploded_offset_mm=[45,0,0],reference_accuracy='Envelope published; visible cap/body proportions photo estimated; concealed internal parts illustrative',
        approximation_scope='Main shell only. Small solid fittings retained exact because sheet unfolding of the rolled bead and actuator does not represent their manufacture.',
        disclaimer='Model demonstration, not a pressure vessel or functional aerosol design'),indent=2),encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--template',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    with zipfile.ZipFile(a.template) as z:template=json.loads(z.read('document.json'))
    out=Path(a.output);out.mkdir(parents=True,exist_ok=True);aerosol(template,out)
