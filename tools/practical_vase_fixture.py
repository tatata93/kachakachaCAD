"""Fit one editable uniform cubic B-spline to the documented vase photo trace.
The fit error is measured independently on 10,001 heights and recorded; no mesh
becomes the CAD source. Requires numpy/scipy only for this fixture generator.
"""
import argparse,json,zipfile
from pathlib import Path
import numpy as np
from scipy.interpolate import CubicHermiteSpline
from practical_fixtures import Fixture,xyz
from practical_complete_fixtures import closed_wire

def basis(t,count):
    scaled=np.asarray(t)*(count-3);spans=np.minimum(np.floor(scaled).astype(int),count-4)
    u=scaled-spans;u2=u*u;u3=u2*u
    weights=np.column_stack(((1-3*u+3*u2-u3)/6,(4-6*u2+3*u3)/6,(1+3*u+3*u2-3*u3)/6,u3/6))
    matrix=np.zeros((len(t),count))
    for j in range(4):matrix[np.arange(len(t)),spans+j]=weights[:,j]
    return matrix

p=argparse.ArgumentParser();p.add_argument('--template',required=True);p.add_argument('--trace',required=True);p.add_argument('--output',required=True);a=p.parse_args()
trace=json.loads(Path(a.trace).read_text(encoding='utf-8'));stations=np.asarray(trace['profile_r_z']);r,z=stations.T
slopes=np.diff(r)/np.diff(z);tangents=np.zeros(len(z));tangents[0]=slopes[0];tangents[-1]=slopes[-1]
for i in range(1,len(z)-1):
    if slopes[i-1]*slopes[i]>0:tangents[i]=2*slopes[i-1]*slopes[i]/(slopes[i-1]+slopes[i])
reference=CubicHermiteSpline(z,r,tangents);height=float(z[-1]);sample=np.linspace(0,1,2001);check=np.linspace(0,1,10001)
for count in (40,64,96,128,192,256):
    matrix=basis(sample,count);values=reference(sample*height)
    # Enforce endpoint radii as equality constraints using a nullspace solve.
    ends=matrix[[0,-1]];target=values[[0,-1]]
    _,_,vt=np.linalg.svd(ends,full_matrices=True);null=vt[2:].T
    particular=np.linalg.lstsq(ends,target,rcond=None)[0]
    controls=particular+null@np.linalg.lstsq(matrix@null,values-matrix@particular,rcond=None)[0]
    evaluated=basis(check,count)@controls;error=evaluated-reference(check*height)
    if np.max(abs(error))<=.05:break
else:raise RuntimeError('Profile fit exceeds the declared 0.05mm maximum error')
with zipfile.ZipFile(a.template) as archive:template=json.loads(archive.read('document.json'))
f=Fixture(template,'Met-vase-24-85-complete-half-scale',
    'Met Museum object24.85 /447776 public-domain photo. Published H330 D160mm, scale1:2. '
    '34 silhouette stations, C2 uniform cubic spline fit <=0.05mm to the traced profile. '
    'Photo perspective, handmade asymmetry and decoration are not captured by these numerical fit errors. '
    'Full circumference, open mouth, separately modeled flat bottom. Nominal wall0.8mm.')
points=[(float(controls[i]),0,(i-1)*height/(count-3)) for i in range(count)]
wire,chain=f.wire('vase profile - uniform cubic spline',[dict(type='cubic_b_spline',degree=3,periodic=False,controlPoints=[xyz(v) for v in points])])
f.add('complete vase wall guide','guide_surface','create_guide_surface',dict(chains=[chain],roles=[2],method=7,offsetDistanceMm=0,revolveAxisPoint=xyz((0,0,0)),revolveAxisDirection=xyz((0,0,1)),revolveAngleRad=float(2*np.pi),lockSectionOrder=True),[wire],key='surface')
bottom=closed_wire(f,'flat base plate profile',[dict(type='circle',center=xyz((0,0,0)),normal=xyz((0,0,1)),xDirection=xyz((1,0,0)),radiusMm=float(r[0]))])
f.add('flat base plate','part','extrude',dict(profiles=[bottom],direction=xyz((0,0,1)),distance=dict(expression='1.2',quantity='length',value=1.2),extentMode=0,booleanMode=0,targets=[]),[bottom],key='part')
out=Path(a.output);out.mkdir(parents=True,exist_ok=True);f.save(out/'vase-complete-source.kcd2')
report=dict(source=trace['source'],photo_trace=trace,control_point_count=count,fit_max_error_mm=float(max(abs(error))),fit_rms_error_mm=float(np.sqrt(np.mean(error**2))),
    fit_verification_points=len(check),fitted_maximum_diameter_mm=float(2*max(evaluated)),height_mm=height,
    note='Numerical fit error against a photo trace, not accuracy against the physical original. Flat base is modeled as a plate; it needs no curved approximation.')
(out/'vase-fit-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k!='photo_trace'},indent=2))
