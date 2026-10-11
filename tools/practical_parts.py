"""Native wire, planar-guide and extrusion helpers for reference sample details."""
import math
from practical_fixtures import xyz,bezier
from practical_complete_fixtures import closed_wire,line3

def poly(points):return [line3(a,b) for a,b in zip(points,points[1:]+points[:1])]
def rect(x0,y0,x1,y1,z=0):return poly([(x0,y0,z),(x1,y0,z),(x1,y1,z),(x0,y1,z)])
def circle(center,r,normal=(0,0,1),xdir=(1,0,0)):
    return [dict(type='circle',center=xyz(center),radiusMm=r,normal=xyz(normal),xDirection=xyz(xdir))]
def rounded_rect(x0,y0,x1,y1,r,map3=lambda x,y:(x,y,0)):
    k=4/3*math.tan(math.pi/8);result=[]
    vertices=[(x0+r,y0),(x1-r,y0),(x1,y0+r),(x1,y1-r),(x1-r,y1),(x0+r,y1),(x0,y1-r),(x0,y0+r)]
    controls=[[(x1-r+k*r,y0),(x1,y0+r-k*r)],[(x1,y1-r+k*r),(x1-r+k*r,y1)],
              [(x0+r-k*r,y1),(x0,y1-r+k*r)],[(x0,y0+r-k*r),(x0+r-k*r,y0)]]
    for i in range(4):
        a,b,c=vertices[2*i],vertices[2*i+1],vertices[(2*i+2)%8]
        result.append(line3(map3(*a),map3(*b)))
        result.append(bezier([map3(*b),*(map3(*p) for p in controls[i]),map3(*c)]))
    return result
def reverse(segments):
    result=[]
    for s in reversed(segments):
        if s['type']=='line':result.append(dict(type='line',start=s['end'],end=s['start']))
        elif s['type']=='cubic_bezier':result.append(dict(type='cubic_bezier',controlPoints=list(reversed(s['controlPoints']))))
        else:raise ValueError('Unsupported reversal in fixture helper')
    return result
def plane(f,name,boundary,holes=()):
    chains=[];wires=[]
    for i,segments in enumerate([boundary,*holes]):
        wire,chain=f.wire(name+(' outline' if i==0 else f' opening {i}'),segments)
        f.d['features'][-1]['definition']['wire']['closed']=True;wires.append(wire);chains.append(chain)
    return f.add(name,'guide_surface','create_guide_surface',dict(chains=chains,roles=[0]+[1]*len(holes),method=0,offsetDistanceMm=0,lockSectionOrder=True),wires,key='surface')
def extrude(f,name,boundary,direction,depth,holes=()):
    wires=[closed_wire(f,name+f' outline {i}',s) for i,s in enumerate([boundary,*holes])]
    return f.add(name,'part','extrude',dict(profiles=wires,direction=xyz(direction),distance=dict(expression=str(depth),quantity='length',value=depth),extentMode=0,booleanMode=0,targets=[]),wires,key='part')
def box(f,name,x0,y0,z0,x1,y1,z1):return extrude(f,name,rect(x0,y0,x1,y1,z0),(0,0,1),z1-z0)
def wheelsets(f,name,centers,radius=4.3,halftrack=9.0,wheelwidth=2.2):
    for i,x in enumerate(centers):
        extrude(f,f'{name} axle {i+1}',circle((x,-halftrack,radius),1.1,(0,1,0)),(0,1,0),2*halftrack)
        for side in (-1,1):
            extrude(f,f'{name} wheel {i+1} side{side}',circle((x,side*halftrack,radius),radius,(0,1,0)),(0,side,0),wheelwidth)
