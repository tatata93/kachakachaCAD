"""Independent axisymmetric volume integral of the native uniform spline wall."""
import argparse,json,zipfile,math
import numpy as np
from scipy.interpolate import BSpline
from scipy.integrate import quad
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('--output',required=True);a=p.parse_args()
with zipfile.ZipFile(a.source) as z:doc=json.loads(z.read('document.json'))
feature=next(f for f in doc['features'] if f['type']=='create_wire' and f['definition']['wire']['segments'][0]['type']=='cubic_b_spline')
controls=feature['definition']['wire']['segments'][0]['controlPoints'];points=np.array([[p['x'],p['z']] for p in controls]);n=len(points)
curve=BSpline(np.arange(-3,n+1),points,3);d1=curve.derivative();d2=curve.derivative(2);thickness=.8
def inner(t):
    r,z=curve(t);dr,dz=d1(t);speed=np.hypot(dr,dz)
    return r-thickness*dz/speed,z+thickness*dr/speed
def integrand(t):
    r,z=curve(t);dr,dz=d1(t);ddr,ddz=d2(t);speed=np.hypot(dr,dz);ri,zi=inner(t)
    dzi=dz+thickness*(ddr*dz*dz-dr*dz*ddz)/speed**3
    return math.pi*(r*r*dz-ri*ri*dzi)
volume=0.;error=0.
for span in range(n-3):
    value,bound=quad(integrand,span,span+1,epsabs=1e-10,epsrel=1e-12,limit=100)
    volume+=value;error+=bound
for t,sign in ((0,-1),(n-3,1)):
    r,z=curve(t);ri,zi=inner(t);volume+=sign*math.pi*(r*r+r*ri+ri*ri)/3*(zi-z)
report={'source':str(Path(a.source).resolve()),'wall_mm':thickness,'control_points':n,'integration_spans':n-3,
    'volume_mm3':volume,'quadrature_error_estimate_mm3':error,'method':'Independent SciPy uniform cubic spline; exact normal-offset meridian; per-knot quadrature of pi*r^2 dz including straight end caps. No OCCT volume calculation used.'}
Path(a.output).write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2))
