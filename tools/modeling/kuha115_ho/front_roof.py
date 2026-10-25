"""Short analytic-arc lofts; rejected long/complex spline trials stay in out/."""
import math


class Forehead:
    def __init__(self,length,side,eave,crown,joint,thickness=1.0):
        self.length,self.side,self.eave,self.crown,self.joint=length,side,eave,crown,joint
        self.thickness=thickness;self.radius=9.0;self.end=10.5;self.rt=40.0
        h=crown-eave;self.rs=(2*self.rt*h-side*side-h*h)/(2*(self.rt-side))
        self.cy=side-self.rs;self.zc=crown-self.rt
        self.theta=math.atan2(eave-self.zc,self.cy)

    def height(self,y,inset=0):
        y=abs(y)
        if y<=(self.rt-inset)*math.cos(self.theta):
            return self.zc+math.sqrt(max(0,(self.rt-inset)**2-y*y))
        return self.eave+math.sqrt(max(0,(self.rs-inset)**2-(y-self.cy)**2))

    def height_at(self,x,y):
        return self.height(y) if x>=self.radius else self.height(y)-self.radius+math.sqrt(self.radius**2-(self.radius-x)**2)

    def solid(self,m,g,name,inset):
        from revised import roof_profile
        radius=self.radius-inset
        profiles=[roof_profile(m,g,self.radius-radius*math.cos(t),self.crown,inset=inset,
            shift=radius*(math.sin(t)-1)) for t in [i*math.pi/18 for i in range(10)]]
        profiles.append(roof_profile(m,g,self.end,self.crown,inset=inset))
        front=m.loft(name+'丸い額から肩への短い円弧ロフト',profiles,g)
        back=m.extrude(name+'一般屋根',[profiles[-1]],(1,0,0),self.length-self.end,g)
        return m.joined(name+'丸額と一般屋根',[front,back],g,'屋根グレー')

    def build(self,m,g):
        outer=self.solid(m,g,'丸額外側',0);inner=self.solid(m,g,'丸額内側',self.thickness)
        for inset,name in [(0,'外側'),(self.thickness,'内側')]:
            wire=m.wire('丸額'+name+'の平面形',m.plan(self.joint,inset),g)
            clip=m.extrude('丸額'+name+'の平面制限',[wire],(0,0,1),12,g)
            if inset:inner=m.boolean('丸額内側の輪郭制限',inner,[clip],2,g,'屋根グレー')
            else:outer=m.boolean('丸額外側の輪郭制限',outer,[clip],2,g,'屋根グレー')
        return outer,m.boolean('額の平面帯を減らした中空屋根',outer,[inner],1,g,'屋根グレー')

    def checks(self,m,roof):
        for y in [-16.5,-14,-10,-6,0,6,10,14,16.5]:
            zo=self.height(y)-self.radius;zi=self.height(y,self.thickness)-(self.radius-self.thickness)
            for z in [self.joint+.5,self.joint+1,self.joint+1.5]:
                xo=self.radius-math.sqrt(self.radius**2-(z-zo)**2)
                xi=self.radius-math.sqrt((self.radius-self.thickness)**2-(z-zi)**2)
                for x,inside in [(xo-.18,False),((xo+xi)/2,True),(xi+.18,False)]:
                    m.points.append(dict(entity=roof,label='額の中央と左右肩の解析輪郭・内外',point=[x,y,z],expectedInside=inside))
                normal_z=z+.5
                phi=math.asin((normal_z-zo)/self.radius);c,s=math.cos(phi),math.sin(phi)
                delta=1e-4;dh=(self.height(y+delta)-self.height(y-delta))/(2*delta)
                normal=(-c,-s*dh,s);length=math.sqrt(sum(n*n for n in normal));point=(self.radius*(1-c),y,normal_z)
                for distance,inside in [(.12,False),(-.85,True)]:
                    m.points.append(dict(entity=roof,label='肩まで含む法線方向0.85mmの肉厚サンプル',
                        point=[point[i]+normal[i]*distance/length for i in range(3)],expectedInside=inside))
