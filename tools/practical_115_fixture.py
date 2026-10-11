"""115-series single cab car, editable approximate reference model at 1:100.
Only overall dimensions are sourced; detailed positions are photo estimates.
"""
import argparse,json,zipfile
from pathlib import Path
from practical_fixtures import Fixture,bezier
from practical_vehicle_fixtures import loft
from practical_parts import plane,poly,reverse,rounded_rect,box,extrude,circle,wheelsets,line3

def build(template,out):
    f=Fixture(template,'115-series-cab-car-1-100',
        '115-series cab-car geometry study at1:100. Overall20m x2.9m; body19.5m plus couplers is an estimate. '
        'User-supplied front photo/drawing from115.kcd2 and https://ja.wikipedia.org/wiki/国鉄115系電車 are local references. '
        'Windows, door positions, bogies, rooftop equipment and rounded front corners are photo estimates, not a factory drawing. '
        'Thin features are enlarged for inspectable printing. Source images are not redistributed.')
    length=195.;half=14.5;bottom=10.;shoulder=31.5;top=36.5
    # Planar window-bearing side sheets stay exact, including every aperture.
    windows=[(3,9,24,30),(15.5,22.5,23,30),(39,50,21,30),(52,63,21,30),(65,76,21,30),
             (78,89,21,30),(107,118,21,30),(120,131,21,30),(133,144,21,30),(146,157,21,30),(177,187,21,30)]
    for side in (-1,1):
        at=lambda x,z:(x,side*half,z)
        boundary=poly([at(0,bottom),at(length,bottom),at(length,shoulder),at(0,shoulder)])
        holes=[rounded_rect(a,c,b,d,.55,at) for a,b,c,d in windows]
        for x in (29.5,98.,168.):
            holes.extend([rounded_rect(x-4.7,23,x-1,29,.35,at),rounded_rect(x+1,23,x+4.7,29,.35,at)])
        if side==1:boundary=reverse(boundary)
        plane(f,f'115 side {side} with 17 windows',boundary,holes)
        # Three pairs of closed door leaves and their window apertures.
        for index,x in enumerate((29.5,98.,168.)):
            frame=rounded_rect(x-6.2,11,x+6.2,30,.6,at)
            leafholes=[rounded_rect(x-4.7,23,x-1,29,.35,at),rounded_rect(x+1,23,x+4.7,29,.35,at)]
            # Door outlines are shallow external relief; the wall stays sealed.
            extrude(f,f'115 side{side} door pair {index+1}',frame,(0,side,0),.18,leafholes)
            f.wire(f'115 side{side} door center seam {index+1}',[line3(at(x,11),at(x,30))])
    # Roof as two long developable strips; rounded crown, no artificial dome dip.
    for side in (-1,1):
        curves=[]
        for x in (0,4,190,length):
            p=[(x,-half,shoulder),(x,-half,top),(x,-7.7,top),(x,0,top)]
            if side==1:p=[(q[0],-q[1],q[2]) for q in reversed(p)]
            curves.append([p])
        loft(f,f'115 rounded roof half {side}',curves)
    for x,label in ((0,'cab front'),(length,'rear end')):
        at=lambda y,z:(x,y,z)
        boundary=poly([at(-half,bottom),at(half,bottom),at(half,shoulder)])[:-1]
        boundary += [bezier([at(half,shoulder),at(half,top),at(7.7,top),at(0,top)]),
                     bezier([at(0,top),at(-7.7,top),at(-half,top),at(-half,shoulder)])]
        boundary += poly([at(-half,shoulder),at(-half,bottom)])[:1]
        holes=[rounded_rect(-12,23,-4.1,31.8,.8,at),rounded_rect(4.1,23,12,31.8,.8,at),rounded_rect(-2.3,24,2.3,30,.35,at)]
        if x==0:boundary=reverse(boundary)
        plane(f,'115 '+label+' with three glazing openings',boundary,holes)
        # Gangway surround and lamp bezels connect to the front sheet.
        direction=(-1 if x==0 else 1,0,0)
        extrude(f,'115 '+label+' gangway frame',rounded_rect(-3.3,13,3.3,32,.5,at),direction,.4,
                [rounded_rect(-2.7,13.6,2.7,31.4,.4,at)])
        for y in (-10.5,10.5):
            extrude(f,'115 '+label+f' lamp bezel {y}',circle((x,y,19.2),1.1,(1,0,0),(0,1,0)),direction,.55,
                    [circle((x,y,19.2),.72,(1,0,0),(0,1,0))])
    # The plate and underframe join the shell; axles/wheels remain identifiable.
    box(f,'115 floor plate',0,-half,9.3,length,half,10.5)
    for x in (27.5,167.5):box(f,f'115 bogie bolster {x}',x-15,-9.5,5.7,x+15,9.5,9.6)
    wheelsets(f,'115', [15,40,155,180])
    for i,(a,b) in enumerate(((50,70),(73,92),(108,129),(132,148))):box(f,f'115 underfloor equipment {i+1}',a,-8,6.3,b,8,9.5)
    for x in (-2.5,length):box(f,f'115 coupler {x}',x,-1.8,6.8,x+2.5,1.8,10.2)
    extrude(f,'115 rooftop air conditioner',rounded_rect(81,-7.5,119,7.5,1.5,lambda x,y:(x,y,36.1)),(0,0,1),4.67)
    for x in (20,38,56,139,157,175):extrude(f,f'115 roof ventilator {x}',rounded_rect(x-3,-3,x+3,3,1,lambda x,y:(x,y,36.1)),(0,0,1),1.65)
    f.save(out/'115-complete-source.kcd2')
    (out/'115-design-notes.json').write_text(json.dumps(dict(reference='User local115.kcd2 front photo/drawing; secondary overall dimensions Wikipedia',
        scale=.01,body_length_mm=195,overall_length_with_couplers_mm=200,width_mm=29,roof_equipment_height_mm=40.77,
        side_window_count=34,front_rear_window_count=6,photo_estimated=True,
        limitations='Door relief 0.18mm is representational; roof equipment, bogies, windows and end details are not measured. Printed detail limits must be checked in slicer.'),indent=2),encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--template',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    with zipfile.ZipFile(a.template) as z:template=json.loads(z.read('document.json'))
    out=Path(a.output);out.mkdir(parents=True,exist_ok=True);build(template,out)
