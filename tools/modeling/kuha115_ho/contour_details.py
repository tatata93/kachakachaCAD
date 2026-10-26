"""Roof casing, floor apron and door sections; photo-estimated model geometry."""
import math
from fit_details import contact, replace_checks

REVISION = '2026-10-26-continuous-casing-apron-tapered-doors'
SIDE, LOW, WAIST, TAPER = 18.125, 13.8, 24.0, 1.1


def shrink(z):
    return TAPER * max(0., min(1., (WAIST-z)/(WAIST-LOW)))


def door(m, name, g, sign, x, width, profiles, low, height):
    """A constant-Y thickness band following the SAME lower law as the body."""
    high = low+height
    outer = lambda z: sign*(SIDE-.28-shrink(z))
    inner = lambda z: outer(z)-sign*.55
    heights = [low, WAIST, high]
    polygon = [(x,outer(z),z) for z in heights]
    polygon += [(x,inner(z),z) for z in reversed(heights)]
    band = m.extrude(name+'の上下断面', [m.poly(name+'裾絞り断面', polygon, g)], (1,0,0), width, g)
    mask = m.extrude(name+'の窓と丸隅マスク', profiles, (0,-sign,0), 3., g)
    leaf = m.boolean(name+'・裾絞り', band, [mask], 2, g)
    if not hasattr(m,'contour_doors'): m.contour_doors=[]
    m.contour_doors.append(dict(entity=leaf,side=sign,x=x,width=width,low=low,high=high))
    # Interior/exterior and the unfilled old planar position at several heights.
    for z in [low+.5,18.,21.,23.,25.,high-.5]:
        for yy,inside,label in [(outer(z)-sign*.275,True,'肉厚中心'),
                                (outer(z)+sign*.06,False,'車外側'),
                                (inner(z)-sign*.06,False,'車内側')]:
            m.points.append(dict(entity=leaf,label='裾絞り扉断面 '+label,
                point=[x+.65,yy,z],expectedInside=inside))
    m.points.append(dict(entity=leaf,label='裾絞り扉の旧平板位置は空間',
        point=[x+.65,sign*(SIDE-.40),low+.5],expectedInside=False))
    for z in [low+.5,18.,21.,22.]:
        for yy,inside in [(outer(z)-sign*.275,True),(outer(z)+sign*.06,False),(inner(z)-sign*.06,False)]:
            m.points.append(dict(entity=leaf,label='裾絞り扉断面 反対端',
                point=[x+width-.65,yy,z],expectedInside=inside))
    return m.finish(leaf,thickness=.55)


def cooler(m, g, ac, outside, x, z):
    # Extend the cover itself down to the roof, over its full footprint. There
    # are no isolated feet between an elevated flat-bottomed case and roof.
    lower = m.box('AU75Gの連続した下部キセ',x,-13.1875,42.,45.75,26.375,z+.9-42.,g)
    ac = m.joined('AU75G・端板と長辺下縁の連続キセ',[ac,lower],g,'アルミ銀')
    ac = m.boolean('AU75Gの下面を屋根曲面へ密着',ac,[outside],1,g,'アルミ銀')
    roof = m.finished['roof']
    for xx in [x+.8,x+22.,x+44.5]:
        for y in [-12.,-8.,0.,8.,12.]:
            roof_z = 5.675+math.sqrt(40.**2-y*y)
            m.points.extend([
                dict(entity=ac,label='キセ下縁の連続・屋根直上',point=[xx,y,roof_z+.12],expectedInside=True),
                dict(entity=ac,label='キセ下面の屋根空間',point=[xx,y,roof_z-.12],expectedInside=False)])
    for xx in [x+.2,x+21.5,x+44.6]:
        for y in [-12.,10.]:
            contact(m,ac,roof,'冷房キセ下縁の連続接触',1.,[xx,y,42.,xx+.7,y+2.,46.])
    m.keyparts['climateUnit']=[ac]
    return ac


def floor_apron(m):
    """Model-print integration, not a claim that the real apron is a monocoque."""
    floor, skirt = m.finished['floor'], m.finished['skirt']
    g=m.entities[floor]['groupId'];returns=[]
    for sign in [-1,1]:
        # Wide horizontal returns follow the folded skirt's rear and overlap
        # the floor underside; preserve the open centre and jumper recesses.
        def front(y): return -.55+1.75*(y-12.7)/4.4+.75-.20
        p=m.poly('スカート上縁から床板へ連続する返し',
            [(front(12.85),sign*12.85,13.0),(6.,sign*12.85,13.0),
             (6.,sign*16.,13.0),(front(16.),sign*16.,13.0)],g)
        returns.append(m.extrude('幅広いスカート上縁の返し',[p],(0,0,1),.78,g))
    combined=m.joined('床板・前端返し・スカート一体',[floor,skirt]+returns,g,'床下グレー',
        note='模型の一体印刷用。実車の取付金具と溶接構造を忠実に復元した意味ではない。中央連結器とジャンパ周辺は開放。')
    replace_checks(m,floor,combined);replace_checks(m,skirt,combined)
    m.checks=[c for c in m.checks if c['first']!=c['second']]
    m.finished['floor']=combined;m.finished['skirt']=combined
    m.keyparts['integratedFloorApron']=[combined]
    for sign in [-1,1]:
        for x in [1.4,2.5,3.5,4.5,5.5]:
            m.points.append(dict(entity=combined,label='床板とスカートの連続返し',
                point=[x,sign*14.,13.4],expectedInside=True))
    for y in [-6.,0.,6.]:
        m.points.append(dict(entity=combined,label='スカート上部の開放維持',
            point=[1.,y,12.],expectedInside=False))


def manifest(m):
    return dict(revision=REVISION,doorSections=m.contour_doors,
        sharedTumblehome=dict(side=SIDE,lowZ=LOW,waistZ=WAIST,taper=TAPER,doorRecess=.28,doorYThickness=.55),
        # coverTopZ is the smooth case surface; bands/grilles extend above it.
        cooler=dict(baseReferenceZ=43.6,coverTopZ=48.15,noExposedPedestals=True),
        skirt=dict(floorIntegrated=True,centralOpeningRetained=True,printConstruction=True),
        addedPointChecks=14*31+30+13-4)
