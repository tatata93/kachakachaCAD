"""Native KCD2 feature authoring helpers; no imported mesh or frozen BRep."""
import json, math, uuid, zipfile
from pathlib import Path
NS = uuid.UUID('dfa91150-2026-4000-8000-000000000001')
def vec(p): return dict(zip(('x','y','z'),p))
def plus(a,b): return tuple(x+y for x,y in zip(a,b))
def mul(a,s): return tuple(x*s for x in a)
def expr(v): return {'expression':str(v),'quantity':'length','value':v}
class Model:
 def __init__(self,title):
  self.i=0; self.d={'format':'kachakachaCAD','schemaVersion':2,'documentId':str(uuid.uuid5(NS,title)),
   'revision':1,'units':{'length':'mm','displayAngle':'deg','storedAngle':'rad'},
   'tolerances':{'numericEpsilon':1e-12,'modelLinearMm':1e-6,'modelAngularRad':1e-9,'interactiveJoinMm':.01,'snapPickPx':12,'displayPickPx':8,'edgePickPx':6,'candidateMenuPx':14},
   'metadata':{'title':title,'author':'kachakachaCAD modeling benchmark','description':'1/80. Dimensioned envelope; photo-based details are approximations. Native editable features.'},
   'activeGroupId':None,'groups':[],'entities':[],'features':[],'rootOrder':[],'uiState':{},'assets':[],'referenceDimensions':[]}
  self.groups={};self.entities={};self.wires={}
 def id(self):
  self.i+=1;return str(uuid.uuid5(NS,str(self.i)))
 def group(self,name,parent=None):
  ident=self.id();g={'id':ident,'displayName':name,'parentGroupId':parent,'state':'visible','childOrder':[]}
  self.d['groups'].append(g);self.groups[ident]=g
  (self.groups[parent]['childOrder'] if parent else self.d['rootOrder']).append(ident)
  return ident
 def add(self,name,kind,typ,definition,group,inputs=(),visible=True):
  fid,eid=self.id(),self.id(); e={'id':eid,'kind':kind,'displayName':name,'createdBy':fid,'groupId':group,
    'visibility':'visible' if visible else 'hidden','editPolicy':'source' if kind in ['wire','work_plane'] else 'derived',
    'revision':0,'construction':False,'datum':False,'partProperties':{'purpose':'finished_model','manufacturing':None} if kind=='part' else None}
  self.d['entities'].append(e);self.entities[eid]=e
  self.d['features'].append({'id':fid,'type':typ,'displayName':name,'enabled':True,'revision':0,'definition':definition,
    'inputEntityIds':list(inputs),'derivedGroupId':None,'outputs':[{'key':kind,'entityId':eid,'kind':kind}]})
  self.groups[group]['childOrder'].append(eid);return eid
 def line(self,a,b):return {'id':self.id(),'type':'line','start':vec(a),'end':vec(b)}
 def arc(self,c,n,x,r,start,sweep):return {'id':self.id(),'type':'circular_arc','center':vec(c),'normal':vec(n),'xDirection':vec(x),'radiusMm':r,'startAngleRad':start,'sweepAngleRad':sweep}
 def wire(self,name,segments,g,visible=False):
  eid=self.add(name,'wire','create_wire',{'sourcePlaneId':None,'construction':False,'wire':{'closed':False,'segments':segments}},g,visible=visible)
  self.wires[eid]=segments;return eid
 def poly(self,name,pts,g,closed=True,visible=False):
  return self.wire(name,[self.line(a,b) for a,b in zip(pts,pts[1:]+([pts[0]] if closed else []))],g,visible)
 def circle(self,name,c,n,r,g):
  x=(1,0,0) if abs(n[0])<.9 else (0,1,0)
  return self.wire(name,[{'id':self.id(),'type':'circle','center':vec(c),'normal':vec(n),'xDirection':vec(x),'radiusMm':r}],g)
 def rect(self,name,o,u,v,w,h,g):
  return self.poly(name,[o,plus(o,mul(u,w)),plus(plus(o,mul(u,w)),mul(v,h)),plus(o,mul(v,h))],g)
 def rounded(self,name,o,u,v,w,h,r,g):
  n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
  def p(x,y):return plus(o,plus(mul(u,x),mul(v,y)))
  seg=[]
  for a,b,c,ang in [((r,0),(w-r,0),(w-r,r),-math.pi/2),((w,r),(w,h-r),(w-r,h-r),0),((w-r,h),(r,h),(r,h-r),math.pi/2),((0,h-r),(0,r),(r,r),math.pi)]:
   seg.extend([self.line(p(*a),p(*b)),self.arc(p(*c),n,u,r,ang,math.pi/2)])
  return self.wire(name,seg,g)
 def extrude(self,name,profiles,n,length,g):
  return self.add(name,'part','extrude',{'profiles':profiles,'direction':vec(n),'distance':expr(length),'extentMode':0,'booleanMode':0,'targets':[]},g,profiles)
 def box(self,name,x,y,z,a,b,c,g):
  p=self.rect(name+' 輪郭',(x,y,z),(1,0,0),(0,1,0),a,b,g);return self.extrude(name,[p],(0,0,1),c,g)
 def cylinder(self,name,p,n,r,length,g):
  w=self.circle(name+' 円',p,n,r,g);return self.extrude(name,[w],n,length,g)
 def ring(self,name,p,n,r,inner,length,g):
  w=self.circle(name+' 外周',p,n,r,g);hole=self.circle(name+' 内周',p,n,inner,g)
  return self.extrude(name,[w,hole],n,length,g)
 def tube(self,name,a,b,r,g):
  d=tuple(y-x for x,y in zip(a,b));length=math.sqrt(sum(x*x for x in d))
  return self.cylinder(name,a,mul(d,1/length),r,length,g)
 def guide(self,name,wires,method,roles,g):
  chains=[{'segments':[{'wireEntityId':w,'segmentId':s['id'],'range':{'first':0,'last':1}} for s in self.wires[w]],'reversed':[False]*len(self.wires[w])} for w in wires]
  return self.add(name,'guide_surface','create_guide_surface',{'method':method,'chains':chains,'roles':roles},g,wires,False)
 def save(self,path):
  path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
  info=zipfile.ZipInfo('document.json',date_time=(2026,10,5,0,0,0))
  info.compress_type=zipfile.ZIP_STORED
  with zipfile.ZipFile(path,'w') as z:z.writestr(info,json.dumps(self.d,ensure_ascii=False,separators=(',',':')))
