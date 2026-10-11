"""Inspect actual slicer G-code and draw selected XY extrusion layers.

This is an independent G-code plot, not a screenshot of the slicer's GUI.
No printer communication and no G-code rewriting.
"""
import argparse,json,math,re
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont

p=argparse.ArgumentParser();p.add_argument('gcode');a=p.parse_args()
file=Path(a.gcode);layers={};height=None;xy=[0.,0.];relative=True;eold=0.
for line in file.read_text(encoding='utf-8').splitlines():
    if line.startswith('; Z_HEIGHT:'):
        height=float(line.split(':')[1]);layers.setdefault(height,[])
    code=line.split(';')[0].strip()
    if code=='M83':relative=True
    if code=='M82':relative=False
    tokens=code.split()
    if not tokens:continue
    values={m[0]:float(m[1:]) for m in tokens[1:] if re.fullmatch(r'[XYZIJE][-+]?\d*\.?\d+',m)}
    if tokens[0]=='G92' and 'E' in values:eold=values['E']
    if tokens[0] not in ('G0','G1','G2','G3'):continue
    end=[values.get('X',xy[0]),values.get('Y',xy[1])]
    extrusion=values.get('E',0) if relative else values.get('E',eold)-eold
    if 'E' in values:eold=values['E'] if not relative else eold+values['E']
    if height is not None and extrusion>0 and ('X' in values or 'Y' in values):
        path=[xy[:],end[:]]
        if tokens[0] in ('G2','G3') and ('I' in values or 'J' in values):
            center=[xy[0]+values.get('I',0),xy[1]+values.get('J',0)]
            radius=math.dist(xy,center)
            start=math.atan2(xy[1]-center[1],xy[0]-center[0]);stop=math.atan2(end[1]-center[1],end[0]-center[0])
            angle=(stop-start)%(2*math.pi) if tokens[0]=='G3' else -((start-stop)%(2*math.pi))
            steps=max(2,math.ceil(abs(angle)*radius/.15))
            path=[[center[0]+radius*math.cos(start+angle*i/steps),center[1]+radius*math.sin(start+angle*i/steps)] for i in range(steps+1)]
        layers[height].append(path)
    xy=end
heights=sorted(layers);chosen=[heights[0],heights[len(heights)//2],heights[-1]]
report={'gcode':str(file),'layer_count':len(heights),'empty_layers':[z for z in heights if not layers[z]],
        'z_min_mm':heights[0],'z_max_mm':heights[-1],'selected_layers_mm':chosen,
        'scope':'Positive XY extrusion, including brim. Arc fitting decoded. No physical print or GUI interaction claimed.'}
file.with_suffix('.layers.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
image=Image.new('RGB',(1500,600),'#f4f6fa');draw=ImageDraw.Draw(image)
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22);small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',16)
draw.text((30,20),'Actual Bambu Studio G-code: XY extrusion paths',fill='#14233d',font=font)
for panel,z in enumerate(chosen):
    paths=layers[z];points=[v for path in paths for v in path]
    lo=[min(v[j] for v in points) for j in (0,1)];hi=[max(v[j] for v in points) for j in (0,1)]
    scale=370/max(hi[j]-lo[j] for j in (0,1));left=panel*500+60
    draw.text((left,70),f'Layer {heights.index(z)+1}/{len(heights)} | Z {z:g} mm',fill='#14233d',font=font)
    draw.rectangle((left-10,115,left+390,515),fill='white',outline='#bac7d9')
    for path in paths:draw.line([(left+(v[0]-lo[0])*scale,500-(v[1]-lo[1])*scale) for v in path],fill='#007f82',width=2)
    draw.text((left,525),f'XY span {hi[0]-lo[0]:.3f} x {hi[1]-lo[1]:.3f} mm',fill='#14233d',font=small)
draw.text((30,570),'Independent plot of exported toolpaths; not a GUI screenshot or a completed physical print.',fill='#46546d',font=small)
image.save(file.with_suffix('.layers.png'));print(json.dumps(report))
