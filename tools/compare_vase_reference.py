"""Reference photo, declared trace, and native spline comparison figure.
This is an analysis figure, not a screenshot or a metrology claim.
"""
import argparse,json,zipfile
from pathlib import Path
import numpy as np
from scipy.interpolate import BSpline,CubicHermiteSpline
from PIL import Image,ImageDraw,ImageFont

p=argparse.ArgumentParser();p.add_argument('--fixture',required=True);p.add_argument('--report',required=True)
p.add_argument('--photo',required=True);p.add_argument('--output',required=True);a=p.parse_args()
report=json.loads(Path(a.report).read_text(encoding='utf-8'))
with zipfile.ZipFile(a.fixture) as z:d=json.loads(z.read('document.json'))
segment=next(f['definition']['wire']['segments'][0] for f in d['features']
    if f['type']=='create_wire' and f['definition']['wire']['segments'][0]['type']=='cubic_b_spline')
controls=np.array([[p['x'],p['z']] for p in segment['controlPoints']]);n=len(controls)
curve=BSpline(np.arange(n+4),controls,3);t=np.linspace(3,n,10001);points=curve(t)
stations=np.array(report['photo_trace']['profile_r_z']);r,z=stations.T
slopes=np.diff(r)/np.diff(z);m=np.zeros(len(z));m[0]=slopes[0];m[-1]=slopes[-1]
for i in range(1,len(z)-1):
    if slopes[i-1]*slopes[i]>0:m[i]=2*slopes[i-1]*slopes[i]/(slopes[i-1]+slopes[i])
reference=CubicHermiteSpline(z,r,m);errors=points[:,0]-reference(points[:,1])
maximum=float(np.max(abs(errors)));rms=float(np.sqrt(np.mean(errors**2)))
image=Image.new('RGB',(1480,1100),'#f3f6fa');draw=ImageDraw.Draw(image)
font=lambda size:ImageFont.truetype('C:/Windows/Fonts/arial.ttf',size)
draw.text((35,26),'Vase reference study: photo and editable CAD profile',font=font(30),fill='#182842')
draw.text((35,75),'Met 24.85 / 447776 | Published H 330 x D 160 mm | CAD scale 1:2',font=font(22),fill='#45556e')
photo=Image.open(a.photo).convert('RGB');photo.thumbnail((500,780))
image.paste(photo,(35,135));draw.text((35,940),'Public Domain reference photograph',font=font(21),fill='#182842')
draw.text((650,130),'Native silhouette (mm)',font=font(23),fill='#182842')
cx,base,scale=820,920,4.6
draw.line((cx,base,cx,base-165*scale),fill='#ccd4df',width=1)
for height in (0,40,80,120,165):
    y=base-height*scale;draw.line((615,y,1025,y),fill='#dce3ec')
    draw.text((575,y-9),str(height),font=font(17),fill='#45556e')
for sign in (-1,1):
    path=[(cx+sign*radius*scale,base-height*scale) for radius,height in points]
    draw.line(path,fill='#087f75',width=4)
    for radius,height in stations:
        x,y=cx+sign*radius*scale,base-height*scale;draw.ellipse((x-3,y-3,x+3,y+3),fill='#da7930')
draw.line((650,971,685,971),fill='#087f75',width=4);draw.text((698,957),'Native spline',font=font(21),fill='#182842')
draw.ellipse((650,1001,658,1009),fill='#da7930');draw.text((698,992),'34 estimated photo stations',font=font(21),fill='#182842')
texts=['Fit to declared photo trace',f'Max radial error: {maximum:.5f} mm',f'RMS radial error: {rms:.5f} mm',
    '10,001 independent height samples','',f'Native D: {2*max(points[:,0]):.5f} mm','Native H: 165.00000 mm','',
    'These are fitting errors,','not errors to the physical vase.','','Photo width and height were',
    'normalized independently to','the published dimensions.','Single-photo aspect differs ~4%.','',
    'Perspective, asymmetry, wall','thickness and painted decoration','are not measured by this trace.']
for i,text in enumerate(texts):draw.text((1080,155+32*i),text,font=font(18 if i else 21),fill='#45556e' if i else '#182842')
draw.text((35,1050),'Analysis figure; no completed thickness/printing claim. Source: metmuseum.org/art/collection/search/447776',font=font(19),fill='#45556e')
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True);image.save(out)
out.with_suffix('.json').write_text(json.dumps(dict(max_radial_fit_error_mm=maximum,rms_radial_fit_error_mm=rms,
    samples=len(t),photo_normalization='Independent published height/diameter; photograph aspect differs about4 percent',
    scope='Fit to estimated photo stations, not measured accuracy to original object'),indent=2),encoding='utf-8')
print(out)
