"""Original dimension-controlled orthographic reference images; not a copied product drawing."""
from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
import math, os
from build_model import L,Y,FLOOR,EAVE,ROOF,WINDOWS,DOORS
GREY='#e5e7df';BLUE='#416a8c';GREEN='#70b8a4';INK='#253443'
def make_views(out):
    fontpath=Path(os.environ.get('WINDIR','/nonexistent'))/'Fonts/meiryo.ttc'
    font=ImageFont.truetype(str(fontpath),19) if fontpath.exists() else ImageFont.load_default()
    def canvas(w,h):
        im=Image.new('RGB',(round(w*10),round(h*10)),'white');return im,ImageDraw.Draw(im)
    def rect(d,box,fill=GREY,r=0):
        b=[round(v*10) for v in box]
        if r:d.rounded_rectangle(b,round(r*10),fill=fill,outline=INK,width=2)
        else:d.rectangle(b,fill=fill,outline=INK,width=2)
    # Side: pixel origin upper-left, bottom rail datum at 52mm.
    im,d=canvas(250,52)
    def zr(a,b,c,e,col=GREY,r=0):rect(d,(a,52-e,c,52-b),col,r)
    zr(0,14.3,L,EAVE)
    d.polygon([(0,(52-43.8)*10),(36,(52-ROOF)*10),(L*10,(52-ROOF)*10),(L*10,(52-EAVE)*10),(0,(52-EAVE)*10)],fill='#a9afb1',outline=INK)
    zr(0,26,L,38.5,BLUE);zr(0,24.7,L,26,GREEN)
    for x,w in WINDOWS:
        zr(x-w/2,27,x+w/2,37.6,'#acbec7',.65)
        zr(x-w/2+.2,31.9,x+w/2-.2,32.3,'#e6e7e2')
    for x in DOORS:
        zr(x-8.125,FLOOR,x+8.125,FLOOR+23,GREY,.45)
        for dx in [-6.5,1.6]:zr(x+dx,28.5,x+dx+5.1,37.1,'#acbec7',.7)
        d.line([(x*10,(52-FLOOR)*10),(x*10,(52-FLOOR-23)*10)],fill=INK,width=2)
    zr(14,FLOOR,21.8,37.8,GREY,.5);zr(15.3,29.7,20.6,36.7,'#acbec7',.6);zr(5.1,29.4,12.2,37.4,'#acbec7',.6)
    zr(100,45.65,143.5,48.75,'#9caaa9')
    for x in [28,61,81,168,195,224]:zr(x-3.6,45.4,x+3.6,47.2,'#a8b0ad')
    for x in [33.4375,210.3125]:
        zr(x-15,6.5,x+15,10.2,'#59636b')
        for dx in [-13.125,13.125]:d.ellipse(((x+dx-5.375)*10,(52-10.75)*10,(x+dx+5.375)*10,520),fill='#5b646b',outline=INK,width=2)
    d.text((50,490),'1/80  |  BODY 243.750 mm  |  COUPLERS 250.000 mm  |  ESTIMATED DETAIL LAYOUT',font=font,fill=INK)
    im.save(out/'side.png')
    # Front: 50mm wide, train centre at 25mm.
    im,d=canvas(50,52)
    def front_box(y,z,w,h,col=GREY,r=0):rect(d,(25+y,52-z-h,25+y+w,52-z),col,r)
    front_box(-Y,14.3,2*Y,EAVE-14.3,GREY,3.6)
    d.pieslice((10*(25-Y),10*(52-ROOF),10*(25+Y),10*(52+ROOF-2*EAVE)),180,360,fill='#a9afb1',outline=INK,width=2)
    front_box(-Y,26,2*Y,12.5,BLUE);front_box(-Y,24.7,2*Y,1.3,GREEN)
    for y in [-16.35,5.25]:
        front_box(y-.28,28.82,11.66,10.36,'#252b2d',1.25)
        front_box(y+.15,29.25,10.8,9.5,'#acbec7',.95)
    front_box(-3.95,FLOOR,7.9,22.9,GREY,.7);front_box(-2.7,29,5.4,8.4,'#acbec7',.65)
    front_box(-4.6,40.05,9.2,2.3,'#dce3df',.3)
    for y in [-12.1,12.1]:
        for z,r,color in [(25.5,1.82,'#f9eec4'),(20.3,.95,'#ad3435')]:
            d.ellipse(((25+y-r)*10,(52-z-r)*10,(25+y+r)*10,(52-z+r)*10),fill=color,outline=INK,width=3)
    d.text((12,490),'WIDTH 36.875 / ROOF 45.675 mm',font=font,fill=INK)
    im.save(out/'front.png')
    im,d=canvas(250,50)
    rect(d,(0,25-Y,L,25+Y),'#b9bdbb',3.6)
    rect(d,(100,14.4,143.5,35.6),'#9caaa9')
    for x in [108,121.7,135.4]:d.ellipse(((x-3.3)*10,217,(x+3.3)*10,283),fill='#66716d',outline=INK,width=2)
    for x in [28,61,81,168,195,224]:rect(d,(x-3.6,22,x+3.6,28),'#9caaa9')
    d.text((50,470),'TOP  |  1/80  |  NO PANTOGRAPH (KUHA)  |  ORIGINAL REFERENCE IMAGE',font=font,fill=INK)
    im.save(out/'top.png')
    # Presentation sheet, each viewport keeps an independent scale bar.
    sheet=Image.new('RGB',(2700,1780),'#f3f5f4');sd=ImageDraw.Draw(sheet)
    sd.text((50,20),'KUHA 115-1000 / NAGANO / 1:80 — DIMENSION-CONTROLLED THREE VIEWS',font=font,fill=INK)
    sheet.paste(Image.open(out/'side.png'),(50,70));sheet.paste(Image.open(out/'top.png'),(50,640))
    front=Image.open(out/'front.png');sheet.paste(front,(50,1200))
    sd.text((630,1250),'Official envelope: Shinano Railway company2025 p14 / JRE 20220928_ho04.',font=font,fill=INK)
    sd.text((630,1300),'Window layout, front curvature and local fittings are estimates.',font=font,fill=INK)
    sd.text((630,1350),'Print walls are deliberately increased inward: body 0.85 / roof 0.9 / floor 1.2 mm.',font=font,fill=INK)
    sd.text((630,1400),'Foggy grey / Advance blue / Refresh green.',font=font,fill=INK)
    sd.text((630,1450),'Roof height excludes AC, antenna and rooftop fittings.',font=font,fill=INK)
    sheet.save(out/'three-view.png')
